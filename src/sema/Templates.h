#pragma once

// **sema::Templates, out of the parser in place** - M2 step 1, doc-cxx3/M2-PLAN.md. Its functions moved here
// whole; they still read the parser's state through p_, and the state follows in a later commit.

#include "Entities.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Type.h"
#include "../syntax/SyntaxId.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Parser;

namespace sema {

class Templates {
public:
    explicit Templates(Parser &p) : p_(p) {}

    // Try every function template named `name` against these argument types,
    // instantiating each that deduces - a substitution failure just drops that
    // one.
    void instantiateViableTemplates(const std::string &name,
                                    const std::vector<const Type *> &argTypes,
                                    std::size_t pos);
    void instantiateViableTemplates(const std::vector<TemplateDecl> &overloads,
                                    const std::vector<const Type *> &argTypes, std::size_t pos);
    // The function templates a call of `name` names, [basic.lookup.qual] and [basic.lookup.unqual]:
    // the nearest namespace declaring one - or the one a qualifier names - in `*ns`; else every one.
    // A `scope` is the namespace a qualifier resolved to, absolute: `m` for `m::twice(2)`.
    bool functionHidesTemplates(const std::string &name) const;
    std::vector<TemplateDecl> templatesNamed(const std::string &name, const std::string &scope,
                                             std::string *ns) const;
    // The same, for a member function template of `cls`.
    void instantiateViableMemberTemplates(const Type *cls,
                                          const std::string &name,
                                          const std::vector<const Type *> &argTypes,
                                          std::size_t pos);
    // The same by the table's own key - "CMatrix<3,3>::CMatrix<3,3>" for a constructor template.
    void instantiateViableMemberTemplatesUnder(const std::string &key,
                                               const std::vector<const Type *> &argTypes,
                                               std::size_t pos);
    // Every constructor template of `cls` that deduces from these argument types,
    // instantiated so the constructor set that is then resolved holds it.
    void instantiateConstructorTemplates(const Type *cls,
                                         const std::vector<const Type *> &argTypes,
                                         std::size_t pos);
    void instantiateConstructorTemplates(const Type *cls,
                                         const std::vector<ExprPtr> &args, std::size_t pos);
    void replayMemberTemplate(const TemplateDecl &mt, const std::string &display, const std::string &key,
                              const std::vector<TemplateArg> &args, const Type *patternFn, bool declOnly);
    const Signature *instantiateMemberTemplate(
        const TemplateDecl &mt, const std::vector<const Type *> &binding,
        const std::vector<long long> &values,
        const std::vector<TemplateArg> &args, std::size_t pos);
    // Its definition reached: the bodies owed by specializations a call made from the declaration alone.
    void definedOutOfLine(const TemplateDecl &mt);
    // `Box<int, 3>` where a type was expected.
    const Type *instantiateClass(const TemplateDecl &decl, std::size_t pos);
    void instantiatePending();
    // Past kMaxNesting bodies deep, a specialization asked for from inside another's body is refused.
    void refuseInstantiationDepth(const std::string &name, int depth, std::size_t pos) const;
    bool memberIsUsed(const std::string &key) const;
    // "twice<int>" - the table key and what the diagnostics call it.
    std::string specializationKey(const std::string &name,
                                  const std::vector<TemplateArg> &args) const;
    // `packs` is indexed like the others and read only for a parameter that
    // is one; everything else ignores it.
    void bindTemplateParameters(const std::vector<TemplateParam> &params,
                                const std::vector<const Type *> &binding,
                                const std::vector<long long> &values,
                                const Packs &packs,
                                std::vector<Shadow> *undo);
    // [temp.deduct.type] rather than [temp.deduct.call]: this matches a template
    // *argument* against a pattern, where nothing decays and nothing may differ.
    // Deduction from a call is looser, because a conversion may still bridge it.
    bool matchPattern(const Type *pattern, const Type *arg,
                      std::vector<const Type *> *binding,
                      std::string *why, Packs *packs = nullptr, std::vector<long long> *values = nullptr) const;
    bool matchFunctionParams(const Type *pattern, const Type *arg, std::vector<const Type *> *binding,
                             std::vector<long long> *values, std::string *why, Packs *packs, bool deduce) const;
    // The partial specialization these arguments ask for, or npos for none.
    // Refuses by name where two match and neither is more specialized.
    std::size_t choosePartial(const TemplateDecl &decl,
                              const std::vector<TemplateArg> &args,
                              std::vector<const Type *> *binding,
                              std::vector<long long> *values,
                              Packs *packs,
                              std::size_t pos);
    // [temp.class.order]: whether one pattern is at least as specialized as
    // another, which is asked by matching each against the other.
    bool atLeastAsSpecialized(const TemplateDecl::Partial &a,
                              const TemplateDecl::Partial &b) const;
    bool moreSpecialized(const TemplateDecl::Partial &a,
                         const TemplateDecl::Partial &b) const;
    static bool mentionsPackExpansion(const Type *t);
    // `Box<T *>` after `template <class T> struct` - the argument list of a
    // partial specialization, read as patterns.
    void partialArguments(TemplateDecl::Partial *ps,
                          const std::vector<TemplateParam> &primary);
    void unbindTemplateParameters(const std::vector<Shadow> &undo);
    // Deduction: the arguments worked out from the call rather than written.
    // Answers false and fills `why` when some parameter cannot be deduced.
    bool deduceTemplateArguments(const TemplateDecl &decl,
                                 const std::vector<const Type *> &argTypes,
                                 std::vector<const Type *> *binding,
                                 std::vector<long long> *values,
                                 Packs *packs,
                                 std::string *why, std::size_t explicitCount = 0);
    // `values` receives a deduced non-type argument (the 3 in V<3>); null where only type parameters occur.
    // `nested`: inside a template-id an argument is matched as it is - no decay, and a cv-qualifier the pattern
    // does not write is deduced ([temp.deduct.type]/8: `It<C, V>` against `It<S, const int>` is V = const int).
    bool deduceOne(const Type *pattern, const Type *arg,
                   std::vector<const Type *> *binding,
                   std::vector<long long> *values, std::string *why,
                   Packs *packs = nullptr, bool nested = false) const;
    // An argument's type for deduction: an lvalue of type U goes as `U &` ([temp.deduct.call]/3).
    const Type *deductionArgType(const Expr &e) const;
    // Is this pattern parameter `T &&` with T a bare template parameter - a forwarding reference?
    static bool forwardingReference(const Type *pattern);
    void checkNonTypeArgument(const Type *pt, long long v, std::size_t argPos) const;
    // ---- Rung 7.1: `auto` ----
    // [dcl.spec.auto] deduces a variable's `auto` **as if by template argument deduction
    // from a call**, so deduceOne does the work and Kind::Deduced stands in.
    static bool mentionsDeduced(const Type *t);
    const Type *substituteDeduced(const Type *t, const Type *with);
    // The same deduction against a type already in hand, for a `for` whose
    // initialiser is built rather than written.
    const Type *deduceAutoFrom(const Type *declared, const Type *from,
                               const std::string &name, std::size_t pos);
    // The template a specialization is of, as a binding: its stand-in, or a pattern's own parameter.
    const Type *templateOf(const Type *spec) const;
    // [temp.arg.template]/3 as clang applies it under -std=c++11: whether a template with
    // parameters `arg` can take the argument list the parameter's own list `param` forms.
    static bool templateTemplateMatches(const std::vector<TemplateParam> &param,
                                        const std::vector<TemplateParam> &arg, std::string *why,
                                        std::size_t *at);
    // The argument list a deduction's binding amounts to, one TemplateArg per parameter.
    std::vector<TemplateArg> argumentsFromBinding(const std::vector<TemplateParam> &params,
                                                  const std::vector<const Type *> &binding,
                                                  const std::vector<long long> &values,
                                                  const Packs &packs) const;
    void requireDeducibleAfterPack(const TemplateDecl &decl, const Type *fn);
    void instantiateViableMemberTemplate(const TemplateDecl &mt,
                                         const std::vector<const Type *> &argTypes, std::size_t pos);
    bool deduceFromArgument(const Type *param, const Type *arg, std::vector<const Type *> *binding,
                            std::vector<long long> *values, std::string *why, Packs *packs = nullptr);

    std::map<std::string, TemplateDecl> templates_;
    // **Function templates overload; class templates do not.**
    std::map<std::string, std::vector<TemplateDecl> > fnTemplates_;
    // Member function templates by "<ownerTag>::<member>", every one of a name (operator* twice).
    std::map<std::string, std::vector<TemplateDecl> > memberTemplates_;

    std::vector<Specialization> specializations_;
    // M8.1: a class a read held and then needed the members of is made where the read's declaration stands ([temp.inst]/1),
    // and seated among the replays where the program first asks - what its making made, in the order it made it.
    struct Held { TemplateDecl decl; std::vector<const Type *> binding; std::vector<long long> values; std::vector<TemplateArg> args; Packs packs; std::size_t pos; };
    struct Seat { std::size_t index; std::string key; std::size_t fnFrom, fnTo; };
    std::map<const Type *, Held> held_;
    mutable std::vector<const Type *> wanted_;
    std::map<std::string, std::vector<Seat> > seat_;
    std::vector<Seat> seatLog_;
    bool making_ = false;
    void want(const Type *held) const { wanted_.push_back(held); }
    bool makeWanted();
    void asked(const std::string &key, std::size_t pos);
    void noteMade(const std::string &key, std::size_t from, std::size_t fnFrom = static_cast<std::size_t>(-1));
    // Each function such a class declared, seated where the program asked: the order implicit members are defined in.
    std::map<std::size_t, std::size_t> fnSeat_;
    std::vector<std::size_t> seatedOrder(std::size_t n) const;
    const Type *makeClass(const TemplateDecl &decl, const std::string &tag, const std::vector<const Type *> &binding, const std::vector<long long> &values,
                          const std::vector<TemplateArg> &args, const Packs &packs, std::size_t pos);

    // **The census of the doors** (M8.0): what each instantiation was, counted where it is made; CXX1_TEMPLATE_CENSUS prints it.
    enum Door { FunctionBody, ClassBody, MemberBody, MemberTemplate, DependentBase, SfinaeTrial, SfinaeFailed, AliasId,
                ConstexprAhead, BySubstitution, Doors };
    void count(Door d, std::size_t n = 1) { census_[d] += static_cast<long>(n); }
    std::string censusLine() const;
    // **-check=templates** (M8.1): each body a door replays after node `mark`, held against the first read; the read itself is always made.
    bool checkBodies_ = false;
    void holdBodies(std::size_t mark);
    // A function template's definition read once in pattern mode where it is declared; at the unit's end, held against its first replay.
    // The class doors read the same way, with no flag since commit 5: `which` is the out-of-line member or the partial.
    enum ReadDoor { ReadFunction, ReadClass, ReadOutOfLine, ReadPartial, ReadAlias, ReadMember, ReadDoors };
    void readDeclaration(const TemplateDecl &decl, ReadDoor door = ReadFunction, std::size_t which = 0);
    void tallyRead(ReadDoor door, std::size_t mark, std::string why, bool wantsBody, std::size_t at);
    // The class door's member templates, each read over its own parameters numbered past the class's `outer`.
    void readMemberTemplates(const std::string &readTag, std::size_t outer, Program *pattern);
    // A class's own member templates, once the class is complete: each read as a function template is.
    void readMemberTemplatesOf(const std::string &tag);
    std::vector<TemplateDecl> memberTemplatesUnder(const std::string &tag) const;
    const Type *readClassBody(const TemplateDecl &decl, const std::string &tag, bool partial, std::size_t start,
                              const std::vector<TemplateArg> &args, const std::vector<TemplateParam> &useParams,
                              const std::vector<const Type *> &useBinding, const std::vector<long long> &useValues,
                              const Packs &packs, std::vector<PendingBody> *bodies, std::vector<Shadow> *kept);
    std::map<std::string, const Type *> readPattern_;
    std::vector<Type *> readShallow_;
    std::string readingTag_, readingRead_;      // the class door's class while its body is read: `C<$T0>` and its read's tag
    std::size_t readSerial_ = 0;
    bool classDoor_ = false;
    void holdDeclarations();
    std::vector<syntax::NodeId> declRead_[ReadDoors];
    std::string bodiesReport() const;
    // M8.1-early (Q6's stand-in, off by default; deleted when M8.2 lands): -templates=subst or CXX1_TEMPLATE_SUBST, and -g.
    bool substitute_ = false, substituteDebug_ = false;
    // Function specialization `i` made from a replayed sibling's body; else `why` says what refused it.
    bool substituteFunction(std::size_t i, std::string *why);
    // A specialization made by substitution: its symbol to the sibling whose body it shares, and the types that change.
    struct Substituted { std::string pattern; std::vector<const Type *> from, to; };
    std::map<std::string, Substituted> substituted_;
    const Type *substituted(const Substituted &s, const Type *t) const;
    // **M8.2, slice 1's scaffolding**: a specialization's types by parameter index; a non-type parameter or a pack replays by name.
    struct Binding { std::vector<TypeId> types; };
    // Specialization `i` built from its template's body read at the declaration, under `b`; false with `why` the reason it replays.
    bool instantiate(std::size_t i, const Binding &b, std::string *why);
    std::string substShape(syntax::NodeId body) const;          // the first thing in a read body the slice does not cover, or empty
    std::map<std::size_t, syntax::NodeId> readBody_;            // a function template's read body, by its `afterParams`
    std::map<std::string, int> substCensus_;                     // -check=templates under -templates=subst: covered, replayed by reason
    // Slice 2b: a covered specialization's read body and its parameters' types, by symbol; what the lowering made of each.
    struct Bound { syntax::NodeId read; Substituted s; };
    std::map<std::string, Bound> bound_;
    std::map<std::string, int> builtCensus_;
    // Slice 2c, step 1: a read's frame steps by its `afterParams`; the frame they lay under a binding against the replay's.
    std::map<std::size_t, std::vector<FrameEvent> > readFrames_;
    std::map<std::string, int> frameCensus_;
    std::string layFrame(const std::vector<FrameEvent> &read, const std::vector<FrameEvent> &replay, const Substituted &s) const;
    std::string builtReport() const;

private:
    long census_[Doors] = {};
    // A template body's syntax by its declarator's place, the nodes already held, and the tally: first, same, text, shape.
    std::map<std::uint32_t, syntax::NodeId> bodyOnce_;
    std::vector<char> bodySeen_;
    long bodyTally_[4] = {};
    long signatureTally_[2] = {};       // replays of a function template whose signature was kept, and those whose parameters agree
    std::set<std::uint32_t> bodyShapes_;
    // The declaration's own read: read, same, text, shape, never replayed; and each reason one was not read, counted.
    long declTally_[ReadDoors][5] = {};
    std::set<std::uint32_t> declShapes_;
    std::vector<std::string> declShapeTrees_;     // each shape's two trees: the read's, then the first replay's
    std::map<std::string, int> declUnread_[ReadDoors];
    std::map<std::string, int> refusedSubst_;     // each reason substitution fell back to replay, counted
    Parser &p_;
    // An instantiation made, keyed by the template's own position and its arguments.
    std::map<std::string, std::size_t> memberTemplateMade_;
    struct Owed { std::vector<const Type *> binding; std::vector<long long> values; std::vector<TemplateArg> args; std::size_t pos; };
    std::map<std::string, std::vector<Owed> > owedBodies_;   // by the declaration's "<owner>::<name>@<declaredAt>"
};

// A substitution as a function of a type, which TypedTree::retype is handed.
struct SubstitutedTypes {
    const Templates *tmpl; const Templates::Substituted *s;
    const Type *operator()(const Type *t) const { return tmpl->substituted(*s, t); }
};

}   // namespace sema
