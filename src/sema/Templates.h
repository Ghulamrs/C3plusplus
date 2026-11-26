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
    // M8.4 slice 4a: a door with no reason line counted by why it falls back, "<door>: <why>" - each door's sum its count.
    mutable std::map<std::string, long> fallbackCensus_;               // mutable: the class door's list (`idUnder`) is const
    mutable long deducedDefaults_ = 0;
    void countWhy(Door d, const std::string &why);
    void countDefault(const TemplateParam &p, bool deduced) const;     // a default template argument replayed, by kind
    // R2, slice 4b: a type default made by the kernel from its declaration's read (null: replayed, the reason counted), and
    // under the check held against its replay; `defaultMade_` the last default's way, `defaultReport_` by reason. 4c: a value's
    // (into `value`, its type as declared) and a template's too.
    const Type *defaultMade(const TemplateDecl &decl, std::size_t i, const std::vector<const Type *> &binding,
                            const std::vector<long long> &values, long long *value);
    bool defaultHeld(const TemplateParam &p, const Type *made, const Type *replayed, long long value, long long replayedValue);
    mutable bool defaultMade_ = false;
    std::string defaultFailure_;     // 4c: the kernel's answer that the default does not substitute, in the replay's words
    mutable std::map<std::string, long> defaultReport_;
    void countAhead(const std::string &tag, bool constructor);         // constexpr-ahead, by whether its class was made
    std::string trialShape(const TemplateDecl &decl, bool defaulted) const;
    std::set<std::size_t> memberTemplateRead_;                         // a member template's body the read read, by `afterParams`
    // R0 (D5, D7): each parser re-entry under a binding, a bind or a jump of `p_.at_`, read or instantiation, by the door
    // DoorAt names; an instantiation's bind kept by its template's site. Counts only, printed under -templates=subst.
    mutable const char *door_ = nullptr;
    struct DoorAt {
        const Templates *t; const char *was;
        DoorAt(const Templates *of, const char *door) : t(of), was(of->door_) { of->door_ = door; }
        ~DoorAt() { t->door_ = was; }
    };
    mutable std::map<std::string, long> reentries_;
    mutable std::map<std::size_t, std::set<std::string> > reenteredAt_;
    void reentered(const char *what, bool standIn = false, std::size_t site = static_cast<std::size_t>(-1)) const;
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
    struct ReadId { std::string name; std::vector<TemplateArg> args; };
    std::map<const Type *, ReadId> readIds_;     // a shallow class the read's end cleared: its template-id, for the class door
    std::string readingTag_, readingRead_;      // the class door's class while its body is read: `C<$T0>` and its read's tag
    std::size_t readSerial_ = 0;
    bool classDoor_ = false;
    void holdDeclarations();
    std::vector<syntax::NodeId> declRead_[ReadDoors];
    std::string bodiesReport() const;
    // M8.2 (off by default): -templates=subst or CXX1_TEMPLATE_SUBST.
    bool substitute_ = false;
    // A specialization's binding as types: each of `from` (the pattern's parameters) is `to`; `values`, a class's by index, bound `T a[N]`.
    // `bounds`: a data member's first bound over the values (`T a[N + 1]`), folded by classShape, by the member's name; packs: by parameter, or none.
    struct Substituted { std::vector<const Type *> from, to; std::vector<long long> values; std::map<std::string, long long> bounds; Packs packs;
                         std::map<int, std::string> templates;     // a template template parameter's index: the template bound
                         std::vector<std::pair<const Type *, const Type *> > needed; };     // ids its door made at first need: the map's
    struct FoldLeaf { virtual ~FoldLeaf() {} virtual bool leaf(const DepExpr *e, long long *out) const = 0; };     // 4d: a size, a static
    // R2, slice 4d: a value trial's ids, their value arguments folded under the deduced binding (`keys`: each id's, all folded; else
    // none and why counted); under the check held against the replay's ids after it (`replayed`: it succeeded). Counted in `valueReport_`.
    void valueTrial(const TemplateDecl &decl, const std::vector<const Type *> &binding, const std::vector<long long> &values,
                    const Packs &packs, std::size_t pos, std::vector<std::string> *keys);
    void valueHeld(const std::vector<std::string> &keys, bool replayed);
    std::map<std::string, long> valueReport_;
    const char *trialSubstituted(const TemplateDecl &decl, const std::vector<const Type *> &binding, const std::vector<long long> &values,
                                 const Packs &packs, Substituted *s);
    // 4e: a function template's types-alone signature under the deduced binding through the kernel: the replay's words where a
    // complete class it carries lacks the member type named or a qualifier is no class (the candidate drops), else empty; `sfinaeReport_`.
    // `formed`: the whole signature substituted where it forms (the candidate declared from it), else null.
    std::string sfinaeSubstituted(const TemplateDecl &decl, const std::vector<const Type *> &binding, const std::vector<long long> &values,
                                  const Packs &packs, std::size_t pos, const Type **formed);
    const char *formedWhy(const TemplateDecl &decl, const Type *fn) const;     // why a formed one is still read again, or null
    std::string trialKind(const Type *e) const;     // the kind of the leaf the kernel did not carry, the census's split
    bool madeAtUse(const Type *e) const;     // a not-carried leaf the trial asks again At the use
    // 4f: a return `decltype` of a member call the binding types (its one public member function's return), or why not.
    const char *decltypeUnder(const Substituted &s, const Type *fn, const Type *leaf, std::size_t pos, const Type **out);
    std::string formedHeld(const TemplateDecl &decl, const Type *formed, const Signature &read) const;     // -check=templates' line
    std::map<std::string, long> sfinaeReport_;
    bool idUnder(const Substituted &s, const Type *e, std::string *name, std::vector<TemplateArg> *args) const;     // `$TT<i>` renamed, its list laid
    std::map<std::string, TemplateDecl>::const_iterator aliasedClass(const TemplateDecl &alias) const;     // `using A = B<T>;`'s B
    // **R1's kernel** (D6): `t` under `s`, total - `why` names the first part `s` cannot carry, empty when it carries all; `type` is
    // then the carried parts substituted and `at` the part that failed. A class template-id: `s`'s own map (Mapped), the
    // program's by its key (Declared; Keyed the map first, under every declarator), or made through makeClass (Made, non-const).
    struct Substitution { const Type *type; const char *why; const Type *at; bool carried() const { return why == nullptr; } };
    enum Ids { Mapped, Declared, Made, Keyed };
    Substitution substitute(const Substituted &s, const Type *t, Ids ids = Mapped) const;
    struct At { std::size_t pos; };
    Substitution substitute(const Substituted &s, const Type *t, At at);     // Made, at `at.pos`
    Substitution need(const Substituted &s, const Type *t);     // a door's: Made at first need, at `p_.at_`
    Substitution substituteIn(const Substituted &s, const Type *t, Ids ids, Templates *maker, std::size_t pos) const;
    Substitution specializationIn(const Substituted &s, const Type *o, Templates *maker, std::size_t pos) const;     // the id step
    bool argsUnder(const Substituted &s, const Type *o, const std::string &name, std::vector<TemplateArg> *args, Templates *maker = nullptr,
                   std::size_t pos = static_cast<std::size_t>(-1)) const;
    const Type *makeId(const std::string &name, const std::vector<TemplateArg> &args, std::size_t pos, bool *made);     // looked up, or made
    const Type *dataUnder(const Substituted &s, const Member &m) const;     // a read data member's type under `s`, its bound too
    struct Substitutions { std::vector<const Type *> types; const char *why; const Type *at; };
    Substitutions substitute(const Substituted &s, const std::vector<const Type *> &params) const;     // `Args...` one per member
    Substitutions substitute(const Substituted &s, const std::vector<const Type *> &params, At at);     // Made, at `at.pos`
    Substitutions substituteList(const Substituted &s, const std::vector<const Type *> &params, Templates *maker, std::size_t pos) const;
    Substitutions need(const Substituted &s, const std::vector<const Type *> &params);     // the list form's, the same
    bool argUnder(const Substituted &s, TemplateArg *a, bool bound, Templates *maker = nullptr,     // one of argsUnder's: a pack
                  std::size_t pos = static_cast<std::size_t>(-1)) const;                             // (`X<Ts...>`) expanded
    bool argTypeUnder(const Substituted &s, const Type **t, Templates *maker, std::size_t pos) const;     // Made: false if not carried
    const Type *carriedId(const Substituted &s, const Type *e, const Substituted *early) const;
    const Type *valueUnder(const Substituted &s, const Type *t) const;      // an expression's: a reference reads as its referent
    std::pair<const Type *, bool> castUnder(const Substituted &s, const std::pair<const Type *, bool> &read) const;
    std::pair<TypeId, bool> castRecord(std::uint32_t id, const std::string &binding) const;     // a node's cast, under `binding`
    // **M8.2**: a specialization's types and its non-type arguments' values by parameter index; a pack replays by name.
    struct Binding { std::vector<TypeId> types; std::vector<long long> values; };
    // Specialization `i` built from its template's body read at the declaration, under `b`; false with `why` the reason it replays.
    bool instantiate(std::size_t i, const Binding &b, std::string *why);
    // The first thing a read body holds apart, or empty; `calls` takes each dependent-name call's function, by its node.
    std::string substShape(syntax::NodeId body, const Substituted &s, std::map<std::uint32_t, std::size_t> *calls,
                           std::map<std::uint32_t, const Type *> *members, std::map<std::uint32_t, std::pair<std::string, const Type *> > *assigns,
                           std::vector<syntax::NodeId> *requests);     // requests: calls a function template answers
    const Type *boundObjectClass(syntax::NodeId member, const Substituted &s) const;     // `o.m`'s class where `o` is of one bound
    std::string boundMemberCall(syntax::NodeId call, const Substituted &s, const std::map<std::uint32_t, std::size_t> &calls,
                                std::size_t *chosen, const Type **full);     // why `o.f(x)` replays, or empty
    std::string valueCast(syntax::NodeId id, const Substituted &s);     // why a cast to no reference replays, or empty
    std::string boundClass(const Type &t) const;       // why a class bound to a type parameter replays, or empty: a class
    bool boundObject(syntax::NodeId id, const Substituted &s, bool classBound) const;     // an lvalue of a bound class, by its name or a pointer's
    bool overRead(const Type *read) const;     // a read type over the parameters: a class it names comes from the binding
    // Slice 2f: each value-dependent name of a read worth its value under `values`, by node; why it is not, or empty.
    std::string valueShape(syntax::NodeId body, const Specialization &sp, std::map<std::uint32_t, std::size_t> *values) const;
    bool foldUnder(syntax::NodeId id, const std::map<std::string, int> &params, const std::map<std::string, int> &consts,
                   int depth, long long *out) const;
    std::string dependentCall(syntax::NodeId call, const Substituted &s, std::size_t *chosen);   // why it waits, or empty
    std::string templateCandidates(syntax::NodeId call, const Substituted &s);    // a function template deduced at the instantiation
    std::string requestCall(const Specialization sp, syntax::NodeId call, const Substituted &s,
                            const std::map<std::uint32_t, std::size_t> &calls, std::size_t *chosen);
    // `calls` the body's records so far, null before any is asked: an argument that is a call then waits for its request.
    std::string callArgTypes(syntax::NodeId call, const Substituted &s, const std::map<std::uint32_t, std::size_t> *calls,
                             std::vector<const Type *> *argTypes);
    std::string nondependentCall(syntax::NodeId call) const;       // why a call in a read body waits for the instantiation, or empty
    void callsUsed(syntax::NodeId body, const std::map<std::uint32_t, std::size_t> &calls);
    std::map<std::size_t, syntax::NodeId> readBody_;            // a function template's read body, by its `afterParams`
    std::map<std::string, int> substCensus_;                     // -check=templates under -templates=subst: covered, replayed by reason
    // M8.3: a class template's or a partial's class read at the declaration, by the token its body is read from.
    std::map<std::size_t, const Type *> readClass_;
    std::map<std::size_t, std::string> readOwn_;     // its own key, a partial's the written one (`pair<const $T0,$T1>`)
    std::set<const Type *> readPartials_;            // the read classes of partials, laid over their own parameters
    std::map<std::string, int> classCensus_;                     // the class door under -templates=subst: laid equal, apart, replayed by reason
    std::string classShape(std::size_t start, const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding, Substituted *s,
                           const Substituted *early = nullptr, std::vector<const Type *> *wanted = nullptr);     // early: ids carried before the replay
    bool earlyArgs(const Substituted &s, const Type *e, const Type *read, std::string *name, std::vector<TemplateArg> *args, bool itself = false,
                   const std::vector<const Type *> *pending = nullptr, bool base = false, bool argsFirst = false) const;     // pending: ids wanted first; base: 3z
    bool readId(const Type *e, std::string *name, std::vector<TemplateArg> *args) const;     // its own id, or readIds_'s; earlyArgs: ids to make first
    bool namesItself(std::size_t start) const;     // an id the read names with its own class as an argument (deque's iterator)
    std::string classLaid(const Type *read, const Substituted &s, const Type *made) const;     // the read's members laid under `s`, against `made`
    // Slice 3b: the read class's member functions, specials among them, as the read declared them, with the read's tag.
    std::map<std::size_t, std::pair<std::string, std::vector<Signature> > > readMembers_;
    std::string classMembers(std::size_t start, Substituted s, const Type *made, const std::string &tag, std::size_t fnFrom,
                             std::map<std::string, std::string> *symbols) const;     // symbols: each read member's to the replay's
    std::map<std::string, std::string> readSymbol_;     // a covered class's, which its bodies' records are typed by (Typer::record)
    const std::string &symbolUnder(const std::string &symbol) const;     // readSymbol_'s for a read member's, else `symbol`
    bool overRead(const Substituted &s, const Type *t) const;     // a class the read named over its parameter, the map lacks
    void readStands(std::size_t start, const Type *made, Substituted *s) const;     // the read class, and one declared apart, for `made`
    int offsetUnder(const Substituted &s, const Type *owner, const std::string &spelled, int offset) const;     // a read member's in `s`'s class
    // Slice 2b: a covered specialization's read body and its parameters' types, by symbol; what the lowering made of each.
    struct Bound { syntax::NodeId read; Substituted s; std::map<std::uint32_t, std::size_t> calls, values; std::map<std::uint32_t, const Type *> members;
                   std::map<std::uint32_t, std::pair<std::string, const Type *> > assigns; bool folds = false; };
    // calls: callee and call nodes; values: 2f's folds, each + 2^31; members: a member call's function type, `this` first;
    // assigns: `a.m = b.m`'s operator= (Parser::assignCalls_'s fact), by the value's node; folds: 3R-d's, each value on its node
    std::map<std::string, Bound> bound_;
    std::map<std::string, Substituted> coveredClass_;      // slice 3b, step 2: a covered class's binding, by its tag
    // While the function door asks a body's shape: its binding, and each id the id step made over it at first need (R1, m8r1k).
    struct Needing { const Substituted *s; std::vector<std::pair<const Type *, const Type *> > made; };
    Needing *needing_ = nullptr;
    // Step 4: the read's held member bodies with each one's symbol, by start; a class's door reason, by tag.
    std::map<std::size_t, std::vector<std::pair<PendingBody, std::string> > > readBodies_;
    std::map<std::string, std::string> classWhy_;
    std::string classSkip(std::size_t start, const std::string &tag, const std::vector<PendingBody> &bodies, const std::string &name,
                          std::size_t typedefsFrom);     // the class skip's shape
    std::map<std::size_t, std::vector<std::pair<std::string, const Type *> > > readTypedefs_;     // step (2): by start, in order
    std::string typedefsCarried(std::size_t start, const Substituted &s, std::vector<const Type *> *wanted = nullptr);
    std::vector<std::pair<std::string, const Type *> > memberTypedefs(const std::string &owner, const std::string &injected, std::size_t from) const;
    std::map<std::size_t, std::vector<MemberRecord> > readOrder_;     // R3 3R-a: by start, each member declaration in order
    const std::vector<MemberRecord> &orderOf(std::size_t start) const;
    std::string beyondInOrder(std::size_t start) const;     // a class body's member beyond data members and functions, by name
    std::vector<std::size_t> enumsInOrder(std::size_t start, bool *other) const;     // the `enum [E] { ... };` staticsFromRead reads
    std::vector<std::pair<std::size_t, std::size_t> > nestedByPath(std::size_t start) const;     // slice 3v: `struct N { ... };`, read by path
    std::string classBefore(std::size_t start, const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding, Substituted *s,
                            std::vector<const Type *> *wanted = nullptr);
    void laidFromRead(const Type *read, const Substituted &s, ClassLaidOut *laid) const;
    std::vector<Type::BaseSpec> basesUnder(const Type *read, const Substituted &s, BasesLaid *bases, const std::string &seed = std::string()) const;
    std::string vtableHeld(const std::string &readTag, const std::string &tag, const std::map<std::string, std::string> &symbols) const;
    std::string fromReadWhyNot(std::size_t start, const std::vector<TemplateParam> &params) const;     // step (1): what a class made from the read cannot carry yet, by name
    const Type *classFromRead(const TemplateDecl &decl, const std::string &tag, bool partial, std::size_t start,
                              const std::vector<TemplateArg> &args, const Substituted &s, std::size_t pos, std::vector<PendingBody> *bodies,
                              const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding, const std::vector<long long> &values);
    std::size_t staticAt(std::size_t start, const std::string &n, bool *cx, std::size_t *name, std::uint32_t *init = nullptr) const;     // a static member's declarator
    void staticAgainst(std::uint32_t init, const Type *owner, const std::string &n, const Substituted &s, const std::vector<TemplateParam> &params,
                       const std::vector<long long> &values);     // 3R-d's census: the read's node folded, against the replay
    bool staticFolded(syntax::NodeId init, const Type *owner, const Substituted &s, const std::vector<TemplateParam> &params,
                      const std::map<std::string, int> &named, long long *out);
    mutable std::size_t staticAsked_ = 0, staticDisagree_ = 0, staticAbsent_ = 0, staticUnfolded_ = 0;
    std::map<std::string, std::size_t> staticWhy_;     // the census's unfolded, by why
    std::size_t ownTypedefAt(std::size_t start, const std::string &n) const;     // `typedef X<num, den> n;`, X its own template
    bool typedefCarried(std::size_t start, const Substituted &s, const Type *read, const std::vector<const Type *> *wanted) const;
    std::size_t typedefByPath(std::size_t start, const Substituted &s, const std::pair<std::string, const Type *> &td) const;     // slice 3u
    std::string staticShape(std::size_t start, const Substituted &s) const;     // what keeps the read's static data members from the read
    void staticsFromRead(std::size_t start, const TemplateDecl &decl, const std::string &tag, const Substituted &s, Type *owner, const Type *scope,
                         const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding, const std::vector<long long> &values,
                         std::vector<PendingBody> *bodies = nullptr);     // with bodies, nestedByPath's classes too
    std::string staticsHeld(std::size_t start, const TemplateDecl &decl, const std::string &tag, const Substituted &s, const Type *made,
                            const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding, const std::vector<long long> &values);
    std::vector<const TemplateDecl *> templatesOf(const std::string &tag) const;     // a class body's member templates, in order
    void templatesFromRead(std::size_t start, const std::string &tag, const TemplateDecl &decl, const std::vector<TemplateParam> &params,
                           const std::vector<const Type *> &binding, const std::vector<long long> &values, const std::vector<TemplateArg> &args,
                           const Packs &packs);
    std::string templatesHeld(const std::string &readTag, const Substituted &s, const std::string &tag) const;
    void membersFromRead(std::size_t start, const std::string &tag, Substituted s, const Type *made, std::vector<PendingBody> *bodies, std::size_t pos);
    void idsInside(std::size_t start, const Signature &a, std::size_t pos, Substituted *ids, Substituted *s);     // its own template under other arguments, made reading `a`
    std::string memberSymbol(const Signature &a, const std::string &name, const Substituted &s, const std::string &tag) const;
    std::string memberSymbol(const Signature &a, const std::string &name, const Signature &under, const std::string &tag) const;
    std::string memberName(const Signature &a, const std::string &readTag, const std::string &tag, const Substituted &s) const;
    std::string bodyKey(std::size_t start, const std::pair<PendingBody, std::string> &h, const std::string &tag, const Substituted &s) const;
    std::string classWhy(const std::string &tag) const;
    std::map<std::string, int> memberCensus_;
    std::string memberShape(const PendingBody &body, const Substituted &s, Bound *made);     // a covered body: typed beside its replay
    const Type *chosenType(const std::string &binding, std::uint32_t id) const;     // the function a bound call's node records
    std::map<std::string, int> builtCensus_;
    // Slice 2c, step 1: a read's frame steps by its `afterParams`; the frame they lay under a binding against the replay's.
    std::map<std::size_t, std::vector<FrameEvent> > readFrames_;
    std::map<std::string, int> frameCensus_;
    std::string layFrame(const std::vector<FrameEvent> &read, const std::vector<FrameEvent> &replay, const Substituted &s, int start, int *raised, std::vector<int> *laid);
    // Step 2: a Function's shell, from the laid `d`, `S`, `V` and `e` steps - held against the replay's function.
    std::map<std::string, int> shellCensus_;
    std::string shellApart(const Function &g, const Function &f) const;
    // Step 2's end: the function the read made at its declaration, by `afterParams`; the replay's made from it and the shell.
    std::vector<Function> readFns_;
    std::map<std::size_t, std::size_t> readFnAt_;
    void keepRead(std::size_t at, const Function &f);
    void memberFrame(const PendingBody &body, const Substituted &s, const std::vector<FrameEvent> &replayed);     // slice 3b: frame, shell
    // A replayed body's start where the bodies replayed before it in one call had counted a call that may throw (mayThrow_).
    std::set<std::size_t> throwsBefore_;
    bool wrapInherited(const PendingBody &body) const;
    std::map<std::string, int> madeCensus_;
    std::string madeFromRead(const Specialization &sp, const Substituted &s, const std::vector<FrameEvent> &read, const std::vector<int> &laid, std::vector<Function> *out);
    std::string madeApart(const Function &g, const Function &f) const;
    // A covered body's constants as the Typer reads them under `symbol`'s binding, against its replay's: apart fails the unit.
    void constantsHeld(const std::string &symbol, syntax::NodeId replay, std::size_t start);
    std::string constantsApart(const std::string &symbol, syntax::NodeId read, syntax::NodeId replay) const;
    long long constantOf(const Bound *b, syntax::NodeId id, bool *has) const;
    bool madeWithoutReplay(const Specialization &sp, const Substituted &s, int startAlign, int alignHad, std::string *why, const PendingBody *member = nullptr,
                           const std::vector<std::string> *guards = nullptr);
    std::map<std::size_t, std::pair<std::string, Substituted> > memberMade_;     // by start: a covered member body owed no replay, its symbol
    bool madeMember(const PendingBody &body);           // in replayInlineBodies, in place of its topLevel: false replays it
    typedef std::vector<std::pair<std::string, std::pair<std::string, const Type *> > > Built;     // memberBuilt_ keys: constructor, type
    std::string membersBuilt(const PendingBody &body, const std::string &symbol, Built *out, std::vector<std::string> *guards,
                             const Substituted &s) const;     // what the read builds members by
    std::vector<FrameEvent> guardedFrame(const std::vector<FrameEvent> &read, std::size_t guards) const;     // with the guards' slots
    std::string builtApart(const std::string &symbol, const Built &own) const;
    std::vector<std::string> builtFailed_;      // -check=templates: a covered body whose members' constructors differ, by place
    void memberNamed(const PendingBody &body, Function *g) const;     // its display name; a constructor's or destructor's alias
    std::map<std::string, int> replayCensus_;                   // a covered body: with no replay, beside one, replayed by reason
    // The measure (M8.3): each class specialization the unit makes, once by its tag - made from the read or replayed, and why.
    std::map<std::string, std::string> specializationCensus_;
    int specializationAgain_ = 0;                               // a tag makeClass met again: counted once, and said
    std::set<std::string> unreplayed_;                          // by symbol: typed from its read, which is its root
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
    Parser &p_;
    // An instantiation made, keyed by the template's own position and its arguments.
    std::map<std::string, std::size_t> memberTemplateMade_;
    struct Owed { std::vector<const Type *> binding; std::vector<long long> values; std::vector<TemplateArg> args; std::size_t pos; };
    std::map<std::string, std::vector<Owed> > owedBodies_;   // by the declaration's "<owner>::<name>@<declaredAt>"
};

}   // namespace sema
