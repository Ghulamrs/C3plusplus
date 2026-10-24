#pragma once

#include "../Abi.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Mangle.h"
#include "../Type.h"
#include "../sema/Entities.h"
#include "../sema/Lookup.h"
#include "../sema/Overload.h"
#include "../sema/Templates.h"
#include "../sema/Classes.h"
#include "../sema/Constexpr.h"
#include "../sema/Linkage.h"
#include "../syntax/Syntax.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Source;
namespace sema { class Typer; class Adapter; }

class Parser {
public:
    // **The whole ABI table, not the three fields of it a return happens to
    // need.** They arrived as three positional arguments, which is a thing to get
    // in the wrong order once; the parser and the backends now read one struct.
    Parser(const Source &src, std::vector<Token> tokens,
           TypeTable &types, const Target &target, const Abi &abi)
        : src_(src), tokens_(std::move(tokens)), types_(types), target_(target),
          abi_(abi), lookup_(*this), overload_(*this), tmpl_(*this), classes_(*this), constant_(*this), linkage_(*this) {}

    // What the grammar recorded, for -dump=syntax.
    const syntax::Tree &syntaxTree() const { return syntaxTree_; }
    Program parse();

private:
    typedef sema::Local Local;

    // A frame slot a closed scope gave back, for a later local of exactly that stored type: `FreeSlot`, in `freeSlots_`.
    struct FreeSlot { const Type *stored; int offset; int align; };

    typedef sema::GlobalSym GlobalSym;

    typedef sema::DynamicSpec DynamicSpec;

    typedef sema::Signature Signature;

    typedef sema::VSlot VSlot;
    // `override` and `final` as written on one declaration, each with where it was written.
    // A constructor rather than member initialisers: g++ refuses those in a default argument of the enclosing class.
    struct VirtSpecifiers {
        bool isOverride, isFinal;
        std::size_t overrideAt, finalAt;
        VirtSpecifiers() : isOverride(false), isFinal(false), overrideAt(0), finalAt(0) {}
    };
    typedef sema::VtableGroup VtableGroup;
    typedef sema::VttLayout VttLayout;
    std::map<std::string, VttLayout> vtts_;
    // The hidden VTT parameter of the C2 or D2 being emitted, -1 outside one.
    int vttSlot_ = -1;
    const Type *vttType() const {
        return types_.pointerTo(types_.pointerTo(types_.get(Kind::Void)));
    }
    typedef sema::VptrSite VptrSite;
    typedef sema::Overrider Overrider;
    typedef sema::VcallEntry VcallEntry;
    typedef sema::Subobject Subobject;
    bool takesVtt(const Type *cls) const {
        return !target_.microsoftNames() && cls != nullptr &&
               cls->hasVirtualBase();
    }

    // **cl's hidden most-derived flag, and who wants it.**
    std::set<std::string> msVbaseCtors_;
    // The tags of those classes, for the destructor side.
    std::set<std::string> msVbaseClasses_;
    // Where the flag sits in the constructor being emitted, -1 for a function that has none.
    int msVbInitSlot_ = -1;
    typedef sema::PendingBody PendingBody;
    std::vector<PendingBody> pendingBodies_;
    // **Where a declaration actually put its signature.**
    std::size_t signatureAddedUnder(const std::string &key,
                                    std::size_t before) const {
        const std::vector<std::size_t> *set = lookup_.overloadsOf(key);
        if (set == nullptr || set->empty()) return PendingBody::npos();
        return set->back() >= before ? set->back() : PendingBody::npos();
    }
    void replayInlineBodies(std::vector<PendingBody> mine);
    // **[dcl.inline]/6: a member defined inside its class is implicitly
    // inline**, and so is every member of a template specialization - which is
    // to say, everything that reaches `topLevel` through a *replay*.
    bool replayingInline_ = false;
    void skipBracedBlock();

    // ---- Rung 5.1: the template table, and nothing instantiated ----
    typedef sema::PackMembers PackMembers;
    typedef sema::Packs Packs;
    typedef sema::TemplateParam TemplateParam;
    // A non-type parameter's type under a binding: T's binding for `T v`, the written type otherwise.
    static const Type *paramTypeIn(const TemplateParam &p, const std::vector<const Type *> &binding) {
        if (p.typeOf >= 0 && static_cast<std::size_t>(p.typeOf) < binding.size() &&
            binding[p.typeOf] != nullptr) return binding[p.typeOf];
        return p.type;
    }
    typedef sema::TemplateDecl TemplateDecl;
    // Set one-shot while a member-template specialization's body is replayed through declareMember:
    // the source member name, the display name with its arguments ("head<3>"), and those arguments,
    // so declareMember keys and mangles the specialization rather than the plain member.
    bool memberTemplateInst_ = false;
    std::string memberTemplateOf_;
    std::string memberTemplateName_;
    std::vector<TemplateArg> memberTemplateArgs_;
    Access memberTemplateAccess_ = Access::Public;
    // The pattern signature of the specialization being replayed, which the Itanium name is spelled from.
    const Type *memberTemplatePattern_ = nullptr;
    // Set once the specialization's body has begun: a failure after that is the body's, not a substitution's.
    bool memberTemplateBody_ = false;
    // A constructor template specialization's C2 by its C1: a pattern's C2 cannot be recomputed from the substituted parameters.
    std::map<std::string, std::string> constructorC2_;
    // Member-template specializations made as *candidates*: one never chosen is emitted by neither oracle.
    std::vector<std::size_t> candidateSpecializations_;
    void pruneUnchosenCandidates(Program &program);
    // Inline functions the program wrote, emitted where defined and dropped at the end unless odr-used.
    std::set<std::string> discardableInline_;
    void pruneUnusedInline(Program &program);
    bool isMemberTemplate(const Type *obj, const std::string &name) const {
        return obj != nullptr &&
               tmpl_.memberTemplates_.count(obj->unqualified()->tag() + "::" + name) != 0;
    }
    // The class that declares `name` as a member template, `cls` or a base of it, or null.
    const Type *memberTemplateOwner(const Type *cls, const std::string &name) const {
        const Type *d = declaringClassOf(cls, name);
        return d != nullptr && isMemberTemplate(d, name) ? d : nullptr;
    }
    ExprPtr memberTemplateCall(ExprPtr object, const Type *obj,
                               const std::string &name, std::size_t pos);
    // **A template is keyed by its bare name, and named by either spelling.**
    std::map<std::string, TemplateDecl>::const_iterator
    findTemplate(const std::string &written) const {
        std::map<std::string, TemplateDecl>::const_iterator it =
            tmpl_.templates_.find(written);
        if (it != tmpl_.templates_.end()) return it;
        const std::string::size_type cut = written.rfind("::");
        if (cut == std::string::npos) return tmpl_.templates_.end();
        return tmpl_.templates_.find(written.substr(cut + 2));
    }
    // Whether a name, either spelling, is a class template - which is the one
    // question the type and expression paths both have to ask.
    bool isClassTemplate(const std::string &written) const {
        std::map<std::string, TemplateDecl>::const_iterator it =
            findTemplate(written);
        return it != tmpl_.templates_.end() && it->second.isClass;
    }

    // ---- Rung 5.2: function templates, explicit arguments ----
    typedef sema::Specialization Specialization;
    // Set while a specialization's definition is being replayed: the name the
    // function must be given, and the symbol it must carry. One-shot, taken
    // by the declarator that names the function.
    std::string instantiationKey_;
    std::string instantiationOf_;
    // And its display name, Specialization::display, for the Function made (1.29).
    std::string instantiationDisplay_;
    // Depth of the specialization being replayed (0 outside); its requests are one deeper (Specialization::depth).
    int instantiationDepth_ = 0;
    // A `>` inside a template argument list closes it and is not an operator
    // - [temp.names], and the reason a comparison there must be parenthesised.
    // Cleared inside parentheses, where a `>` is an operator again.
    bool inTemplateArgs_ = false;
    const std::string &instantiationName(const std::string &name) const {
        return (!instantiationKey_.empty() && name == instantiationOf_)
                   ? instantiationKey_ : name;
    }
    ExprPtr templateCall(Program *program, const std::string &scope = std::string());
    // A static member reached through a class template-id, after its `::`.
    ExprPtr templateIdMember(const Type *cls, std::size_t pos);
    // Set while a class template is being instantiated.
    std::string classInstantiationTag_;
    // The template's own name, for the explicit-specialization path.
    std::string classInstantiationOf_;
    // **Held bodies are deferred only for an implicit instantiation**, which happens in the middle of whatever asked for the class.
    bool deferSpecializationBodies_ = false;
    // `template <> struct Box<int> { ... };`.
    bool explicitSpecialization();
    // The arguments that tag was built from, and the bodies the class came back with.
    std::vector<TemplateArg> instantiatingArgs_;
    // The template's namespace, travelling the same short distance for the same reason.
    std::string instantiatingNamespace_;
    // The class template's own parameters and the arguments this instantiation
    // bound them to, travelling the same short distance so a member function
    // template declared in the body can capture them.
    std::vector<TemplateParam> instantiatingParams_;
    std::vector<const Type *> instantiatingBinding_;
    std::vector<long long> instantiatingValues_;
    std::vector<PendingBody> heldForSpecialization_;
    // Set while a template's declaration is read as a *pattern*, with Kind::TemplateParam in place of the arguments.
    bool patternOnly_ = false;

    // **A trial, and everything it has to put back.** Forming a candidate's signature
    // reads tokens, pushes classes and binds parameters; if it fails half way through,
    // none of that may be left behind for the next candidate to trip over.
    struct Trial {
        Parser *p;
        std::size_t at;
        std::size_t classes;
        bool pattern;
        // **The half-taken `>>` counts as state too.** `angleSplit_` marks the token
        // whose first `>` has been consumed, and a substitution that fails between the
        // halves would leave the outer list unterminated at the next reading.
        std::size_t split;
        std::size_t syntaxMark;
        explicit Trial(Parser *parser);
        ~Trial();
    };
    const Signature &instantiate(const TemplateDecl &decl,
                                 const std::vector<const Type *> &binding,
                                 const std::vector<long long> &values,
                                 const std::vector<TemplateArg> &args,
                                 std::size_t pos,
                                 const Packs &packs =
                                     Packs());
    // The template's declaration re-read with `binding` in force. Answers its
    // function type and, through `name`, the name it declares.
    const Type *readTemplateDeclaration(const TemplateDecl &decl,
                                        const std::vector<const Type *> &binding,
                                        const std::vector<long long> &values,
                                        std::string *name,
                                        std::string *qualifier = nullptr,
                                        const Packs *packs = nullptr,
                                        bool *construction = nullptr);
    // The type parameters bound now, counted: `X::type` as a type needs `typename` (A27).
    std::map<std::string, int> boundTypeParams_;
    // The non-type parameters bound now, by name: in their template's scope they hide an enclosing one.
    std::map<std::string, int> boundValueParams_;
    typedef sema::PackBinding PackBinding;
    typedef sema::Shadow Shadow;
    std::map<std::string, PackBinding> packs_;
    // While a signature is read as a pattern for deduction, a non-type parameter is bound here.
    std::map<std::string, int> nonTypePatternParams_;
    // `Ts... rest` in a parameter list. Answers false where the tokens are
    // not that; otherwise reads it and adds what it expands to.
    bool packParameter(std::vector<const Type *> *types,
                       std::vector<std::string> *names);
    // The bindings a scope made, put back however it is left: a trial's failure inside them included.
    struct BoundParams {
        Parser *p;
        std::vector<Shadow> undo;
        explicit BoundParams(Parser *parser) : p(parser) {}
        ~BoundParams() { p->tmpl_.unbindTemplateParameters(undo); }
        void unbindNow() { p->tmpl_.unbindTemplateParameters(undo); undo.clear(); }
    };
    // An argument's type as a parameter sees it.
    const Type *decayedArgument(const Type *a) const;
    // ---- WS-E1 (review 2026-10-08): forwarding references, F30, F31, F34, alias templates ----

    // `[const] Ts [& | &&] ...`: the tokens it takes (0 if it is not one), where Ts is, and how a member is wrapped.
    std::size_t packDeclarator(int *wrap, std::size_t *nameAt) const;
    const Type *wrapPackMember(const Type *member, int wrap);
    // `f(g(a)...)`, `std::forward<A>(a)...`: one argument per member, the pattern read once for each.
    bool expandPackPattern(std::vector<ExprPtr> &args);
    // The same for `{ 0, Ns... }` in a braced list and `List<(Ns * 2)...>` in an argument list.
    typedef sema::Init Init;
    bool expandPackInitialiser(std::vector<Init> &items);
    bool expandPackValues(TemplateArg &a);
    // Where a pattern's `...` ends and how many members it expands to, and one member's tokens bound.
    std::size_t packPatternEnd(bool angle, std::size_t *members) const;
    void bindPatternMember(std::size_t from, std::size_t end, std::size_t m, std::vector<Shadow> *undo);
    // `template <> int twice<int>(int)` - F31.
    bool functionExplicitSpecialization(std::size_t pos);
    // A deduction left parameter `i` unbound: its default, if it has one, replayed with the earlier ones bound.
    bool defaultTemplateArgument(const TemplateDecl &decl, std::size_t i,
                                 std::vector<const Type *> *binding, std::vector<long long> *values);
    // Reads the initialiser to learn its type, puts the tokens back, and
    // answers the declared type with `auto` replaced.
    const Type *deduceAuto(const Type *declared, const std::string &name,
                           std::size_t pos);
    // ---- Rungs 7.3 and 7.2: the range-based `for`, and `decltype` ----
    // A declaration followed by `:` takes a scan to tell from `for (int x = a?b:c;)`;
    // and [dcl.type.simple] separates `decltype(x)` from `decltype((x))` by shape.
    const Type *decltypeSpecifier();
    bool atNamePath() const;
    bool atRangeFor() const;
    StmtPtr rangeForStatement(int scope);
    // [stmt.ranged]'s begin-expr and end-expr for a class range, and the slots
    // holding them. Returns the iterator type; fills `setup` with the three
    // assignments the loop needs in front of it.
    const Type *classRangeEnds(ExprPtr range, std::size_t rpos,
                               std::vector<StmtPtr> &setup, int *bSlot,
                               int *eSlot, std::string *bName,
                               std::string *eName);
    StmtPtr initRangeLocal(const std::string &name, const Type *t, int off, ExprPtr init,
                           std::size_t pos);
    // Where a range-for's own objects became alive: a statement index and how many then.
    std::vector<std::pair<std::size_t, std::size_t> > rangeBuilt_;
    // The argument list at a use: `<int, 3>` read into types and values.
    void templateArguments(const TemplateDecl &decl,
                           std::vector<const Type *> *binding,
                           std::vector<long long> *values,
                           std::vector<TemplateArg> *args,
                           Packs *packs = nullptr);
    // **A name declared as an object is not a template name.**
    bool isTemplateName(const std::string &name, bool qualified = false) const {
        if (tmpl_.templates_.find(name) == tmpl_.templates_.end()) return false;
        if (qualified) return true;
        return lookup_.findLocal(name) == nullptr && lookup_.findGlobal(name) == nullptr;
    }
    bool templateDeclaration();
    // `allowUnnamed` for a template template parameter's own list, whose names nothing refers to.
    void templateParameters(std::vector<TemplateParam> &params, bool allowUnnamed = false);
    // ---- Template template parameters, [temp.param]/1 and [temp.arg.template] ----
    // The stand-in a template template parameter is bound to: the template's name as a Type.
    const Type *templateNameTypeOf(const TemplateDecl &decl) const;
    // `$TT0`: a pattern's stand-in for template template parameter 0, and which one it is.
    static bool templateParamRef(const std::string &name, int *index);
    // The argument written for a template template parameter: a class or alias template's name, or a bound parameter's.
    const TemplateDecl &templateTemplateArgument(const TemplateDecl &decl, const TemplateParam &p);
    // What the declaration after `template <...>` actually declares: a class template, a
    // function template, or a member of a class template defined outside it.
    // `qualifier` is the class for that last one and empty otherwise.
    std::string templatedName(const std::vector<TemplateParam> &params,
                              bool *isClass, std::string *qualifier,
                              bool *construction = nullptr, const Type **pattern = nullptr);
    // [temp.param]/11: the first parameter after a pack, or npos; and the class template's refusal of one.
    static std::size_t afterPack(const std::vector<TemplateParam> &params);
    void refuseAfterPack(const std::vector<TemplateParam> &params);
    // `sawInit`, when given, says the declaration ended at a `;` having passed
    // an `=` at depth zero - which is a static data member of a class template
    // being *defined* out of line, not a member left undefined.
    bool skipTemplatedDefinition(bool *sawInit = nullptr, bool *sawParen = nullptr);
    void skipTemplateArguments();
    // `Box<T>::Box(` or `Box<T>::~Box(`.
    bool atOutOfLineSpecial(std::string *what, std::string *nested = nullptr);
    // The whole of what a use of a template does in 5.1.
    void refuseTemplateId();

    // **A `>>` closes two argument lists, and the lexer hands it over as one token.**
    // Held bodies record absolute token indices, so nothing may be inserted: the first
    // `>` is taken by leaving this index behind, the second by advancing past it.
    std::size_t angleSplit_ = static_cast<std::size_t>(-1);
    bool atClosingAngle() const;
    void takeClosingAngle();

    // Set only while an inline body is being replayed.
    std::string inlineOwner_;
    // The same class as the source spells it.
    std::string inlineOwnerName_;

    // **Emitting a vtable uses everything the table points at.**
    void markSymbolUsed(const std::string &symbol);

    // **Mark a signature that came out of `lookup_.functions_` used, and the one place the pointer arithmetic this file warns about is written.**
    void markUsed(const Signature *f);
    // Marks lookup_.functions_[i] used, and what its body used; while a candidate's body is read, records it instead.
    void noteUsed(std::size_t i);
    std::vector<std::size_t> *candidateUses_ = nullptr;
    // **A vtable entry, a typeinfo field and the vptr are one pointer wide** - eight bytes on the 64-bit
    // targets, four on the C6000 - and an offset stored among them is as wide, signed. Not ptrdiffType(): two
    // overloads chosen by the caller's constness were a trap once a caller moved to sema (M2.3).
    int pointerBytes() const { return target_.sizeOf(Kind::Pointer); }
    const Type *vtableOffsetType() const {
        return types_.get(pointerBytes() == 8 ? Kind::LongLong : Kind::Int);
    }
    // **How far the address point sits past the table's first byte.**
    int vtableHeaderBytes(const Type *cls) const {
        if (target_.microsoftNames()) return 0;
        int n = 0;
        const std::vector<Type::BaseSpec> &bs = cls->bases();
        for (std::size_t i = 0; i < bs.size(); i++) if (bs[i].isVirtual) n++;
        return (n + 2) * pointerBytes();
    }


    // `(*this).m`, typed as the member is declared.
    ExprPtr thisMember(int thisSlot, const Type *cls, const Member &m);
    // `PB::v` inside a member function: `(*this).PB::v`, or null where there is no such `this` (1.27).
    ExprPtr qualifiedMemberThroughThis(const Type *cls, const Member &m,
                                       const std::string &member, std::size_t pos);
    // Reaching a member that lives in a virtual base.
    void refuseVirtualBaseMember(const Type *staticType, const Member &m,
                                 const std::string &name, std::size_t pos);



    // [dcl.init]/7 for an object declared const with no initialiser at all.
    void requireConstInitialised(const Type *t, const std::string &name,
                                 std::size_t pos);
    struct Alive;
    // The `if (mostDerived)` a Microsoft constructor opens with: the vbtable pointers and the virtual
    // bases, built only by the complete object. With `vbs`, a count of those built is zeroed first and
    // `vbs` gets each one's destruction on that count, for a region over the rest, [except.ctor]/3.
    std::vector<StmtPtr> guardedVirtualBaseInit(
        const Type *type, const std::string &cls, int thisSlot, int flagSlot,
        std::size_t pos, int srcSlot, bool moving,
        std::map<std::string, std::vector<ExprPtr> > *vbaseArgs,
        std::vector<Alive> *vbs = nullptr);

    typedef sema::Rank Rank;



    struct Declared {
        std::string name;
        const Type *type;
        std::size_t pos;

        std::size_t paramsAt;
        // The class named before a `::` in a definition's declarator -
        // `int Point::get()` declares nothing called "Point::get", it defines
        // the `get` that Point already declared.
        std::string qualifier;
        Declared(std::string name = std::string(), const Type *type = nullptr,
                 std::size_t pos = 0, std::size_t paramsAt = 0,
                 std::string qualifier = std::string())
            : name(std::move(name)), type(type), pos(pos), paramsAt(paramsAt),
              qualifier(std::move(qualifier)) {}
    };

    enum StorageClass { StorageNone, StorageStatic, StorageExtern, StorageTypedef,
                        StorageRegister, StorageAuto };

    struct Qualifiers {
        bool isConst = false;
        bool isVolatile = false;
        // `constexpr` implies const on an object, so isConst is set with it and almost everything downstream needs to know nothing more.
        bool isConstexpr = false;
        // `inline` on a function: its definition may appear in several
        // translation units, so it is emitted as a weak/COMDAT definition the
        // linker folds - the same treatment a template specialization gets.
        bool isInline = false;
        int alignAs = 0;   // `alignas(N)` among the specifiers: the largest, else 0
        bool isThreadLocal = false;   // `thread_local`, [dcl.stc]/4: one object per thread
    };

    typedef sema::TypedefName TypedefName;

    typedef sema::EnumConst EnumConst;

    const Source &src_;
    std::vector<Token> tokens_;
    TypeTable &types_;
    const Target &target_;

    // Read here for one question - how a class comes back - and by each backend for the rest.
    const Abi &abi_;
    // **Sema's modules, out of the parser in place** (M2 step 1, doc-cxx3/M2-PLAN.md): each reads the
    // parser's state through a reference back to it until that state moves to the module.
    friend class sema::Lookup;
    sema::Lookup lookup_;
    friend class sema::Overload;
    sema::Overload overload_;
    friend class sema::Templates;
    sema::Templates tmpl_;
    friend class sema::Classes;
    sema::Classes classes_;
    friend class sema::Constexpr;
    sema::Constexpr constant_;
    friend class sema::Linkage;
    sema::Linkage linkage_;
    // M2.10: the typer reads the syntax tree and what the parser kept beside it.
    friend class sema::Typer;
    friend class sema::Adapter;
    // **The syntax tree** (M2 step 2): a node recorded where the grammar returns an Ast.h node, its operands'
    // nodes as children. `note` records one on `e`; `syn` reads one back, none for a node the grammar made itself.
    // What a conditional being read has recorded of its operands (M2 step 2), until conditional() notes it.
    struct CondParts {
        bool seen;
        std::size_t pos;
        syntax::NodeId parts[3];
        CondParts() : seen(false), pos(0) {}
    };
    syntax::NodeId unaryOperand_;
    CondParts condParts_;
    // The arguments the last parseArguments read, as syntax, for the call postfix records.
    std::vector<syntax::NodeId> callArgs_;
    // Counts the argument lists parseArguments has read, so a postfix step can tell whether it read one.
    unsigned argGen_ = 0;
    struct PostfixStep {
        bool active, callFollows;
        syntax::Kind kind;
        std::string text;
        std::size_t pos, callPos;
        syntax::NodeId operand, index;
        unsigned argGen;
        PostfixStep() : active(false), callFollows(false), kind(syntax::Kind::Other), pos(0), callPos(0), argGen(0) {}
    };
    PostfixStep beginPostfix(const ExprPtr &n) const;
    ExprPtr notePostfix(ExprPtr n, PostfixStep &st);
    syntax::Tree syntaxTree_;
    // A node over the parts parsed since node `mark` whose locations lie from `lo` to the last token read: a statement's
    // or a declaration's, each part having taken its own (Tree::adopt). noteStmt puts it on the Ast.h statement too.
    syntax::NodeId noteParts(syntax::Kind kind, const std::string &text, std::size_t pos, std::size_t mark, std::size_t lo) {
        const std::size_t hi = at_ > 0 && tokens_[at_ - 1].pos > lo ? tokens_[at_ - 1].pos : lo;
        return syntaxTree_.add(kind, text, pos, syntaxTree_.adopt(mark, lo, hi));
    }
    StmtPtr noteStmt(StmtPtr s, syntax::Kind kind, const std::string &text, std::size_t pos, std::size_t mark,
                     std::size_t lo) {
        const syntax::NodeId id = noteParts(kind, text, pos, mark, lo);
        if (s) s->setSyntax(id);
        return s;
    }
    // The type each `var` node declares, by node - what Sema's typing reads a name's type from (M2.10).
    std::unordered_map<std::uint32_t, const Type *> declaredTypes_;
    ExprPtr noteCast(ExprPtr e, const Type *to, const std::string &written, std::size_t pos, syntax::NodeId operand) {
        e = note(std::move(e), syntax::Kind::Cast, written, pos, std::vector<syntax::NodeId>(1, operand));
        if (e) castTypes_[e->syntax().v] = to;
        return e;
    }
    // A literal's token, and the type a C-style cast names, by node: what typing either reads.
    std::unordered_map<std::uint32_t, std::size_t> literalTokens_;
    std::unordered_map<std::uint32_t, const Type *> castTypes_;
    // What a sizeof, alignof or noexcept node folded to, and its type: the constant the adapter writes (M2.12).
    std::unordered_map<std::uint32_t, std::pair<long long, const Type *> > keywordValues_;
    // The `function` nodes whose names outside them are not the file's - a member's, a namespace's, a template's.
    std::set<std::uint32_t> scopedFunctions_;
    // **What the parser decides of layout as it reads, by node** (M2.11): a `var`'s frame slot, a block's or a `for`'s
    // scope, and the symbol a `function` node was defined as - what the adapter cannot derive from types.
    std::unordered_map<std::uint32_t, Local> declaredLocals_;
    std::unordered_map<std::uint32_t, int> syntaxScopes_;
    std::unordered_map<std::uint32_t, std::string> syntaxFunctions_;
    std::unordered_map<std::uint32_t, const Type *> declaredReturns_;   // a reference return as written (M2.12)
    // A call's frame slots - its result's, each argument's, and whether the result's holds a pointer - by node (M2.12).
    struct CallSlots { int result = 0; bool indirect = false; std::vector<int> args; std::string symbol; };
    std::unordered_map<std::uint32_t, CallSlots> callSlots_;
    // A block's cleanup slots - the exception pointer's and the selector's - and the `.ret` a return saves its value
    // in before destroying what is alive, by node (M2.12h). The last pair wrapCleanups chose is what block() records.
    std::unordered_map<std::uint32_t, std::pair<int, int> > blockCleanups_;
    std::pair<int, int> lastCleanupSlots_;
    struct ReturnTemp { std::string name; int slot = 0; };
    std::unordered_map<std::uint32_t, ReturnTemp> returnTemps_;
    ReturnTemp pendingReturnTemp_;
    // A `try` the adapter can rebuild, by node: its slots, its chain's label, its types and each handler's facts.
    struct TryHandlerFacts { int scope = 0; std::string endLabel; int padPtr = 0, padSel = 0; bool canThrow = false; };
    struct TryFacts {
        bool simple = false; int ptr = 0, sel = 0; std::string chain;
        std::size_t aliveOutside = 0, owesFrom = 0; bool unwindsHere = false;
        std::vector<std::string> types; std::vector<int> indices; std::vector<TryHandlerFacts> handlers;
    };
    std::unordered_map<std::uint32_t, TryFacts> tryFacts_;
    std::set<std::string> wrappedBodies_;
    // The static locals guardOnce builds at run time; the adapter takes only those laid down as data.
    std::set<std::string> guardedStatics_;
    TryFacts pendingTry_;
    // A `throw` of a scalar on the Itanium targets: the slot holding the exception's storage, and its type_info.
    struct ThrowFacts { std::string temp; int slot = 0; std::string info; };
    std::unordered_map<std::uint32_t, ThrowFacts> throwFacts_;
    ThrowFacts pendingThrow_;
    void noteCallSlots(const Call &c) {
        if (!c.syntax().valid()) return;
        CallSlots &k = callSlots_[c.syntax().v];
        k.result = c.resultSlot();
        k.indirect = c.resultIndirect();
        k.symbol = c.symbol();
        k.args.clear();
        for (std::size_t i = 0; i < c.args().size(); i++) k.args.push_back(c.argSlot(i));
    }
    std::unordered_map<std::uint32_t, std::pair<std::string, std::string> > stringLiterals_;   // a string's label and text
    // A handler's caught object as a `var` node, so a name in its block finds it.
    void noteCaught(const std::string &name, const Type *type, std::size_t pos) {
        const syntax::NodeId id = noteParts(syntax::Kind::Var, type->describe() + " " + name, pos, syntaxTree_.size(), pos);
        declaredTypes_[id.v] = type;
        noteDeclaredLocal(id, name);
    }
    void noteDeclaredLocal(syntax::NodeId id, const std::string &name) {
        if (const Local *l = lookup_.findLocal(name)) declaredLocals_[id.v] = *l;
    }
    // **One declarator's `var` node**, made when the declarator's scope closes - which a loop body that leaves by
    // `continue` from several places does in one place - over the parts its parse recorded: a bound, an initialiser.
    struct VarNote {
        Parser &p;
        std::size_t mark, lo;
        const Declared *d = nullptr;
        bool function = false;      // a function's declaration with no definition: its node is over its parameters
        VarNote(Parser &parser, std::size_t m, std::size_t l) : p(parser), mark(m), lo(l) {}
        // The type and the name; an unnamed parameter's `$unnamed<n>` is the parser's, and no source spells it.
        std::string varText() const {
            const std::string name = d->name.empty() || d->name[0] == '$' ? std::string() : d->name;
            const std::string type = d->type != nullptr ? d->type->describe() : std::string();
            return type.empty() || name.empty() ? type + name : type + " " + name;
        }
        ~VarNote() {
            if (d == nullptr) return;
            const syntax::NodeId id = function
                ? p.noteParts(syntax::Kind::Function, d->name, d->pos, mark, lo)
                : p.noteParts(syntax::Kind::Var, varText(), d->pos, mark, lo);
            if (!function && d->type != nullptr) p.declaredTypes_[id.v] = d->type;
            if (!function) p.noteDeclaredLocal(id, d->name);
        }
    };
    struct KeywordForm { std::size_t start, mark; ExprPtr e; };
    KeywordForm unaryForm();
    ExprPtr noteKeywordForm(syntax::Kind kind, KeywordForm f);
    // The statement kind the tokens ahead begin, and its text - a label's name; Other for a block, which records itself.
    syntax::Kind statementKindAhead(std::string *text) const;
    syntax::NodeId syn(const ExprPtr &e) const { return e ? e->syntax() : syntax::NodeId(); }
    ExprPtr note(ExprPtr e, syntax::Kind kind, const std::string &text, std::size_t pos,
                 const std::vector<syntax::NodeId> &children = std::vector<syntax::NodeId>()) {
        if (e) e->setSyntax(syntaxTree_.add(kind, text, pos, children));
        if (kind == syntax::Kind::Call) {
            if (const Call *c = dynamic_cast<const Call *>(e.get())) noteCallSlots(*c);
            else noteTemporarySlot(e.get());
        }
        return e;
    }
    // A class temporary `T(a)` is `*(ctor(&$tmp, a), &$tmp)`, its slot the parser's: kept by node (M2.12i).
    std::unordered_map<std::uint32_t, int> temporarySlots_;
    // A value a const or rvalue reference binds copied into a frame slot by bindReference: its name and slot, by node.
    std::unordered_map<std::uint32_t, std::pair<std::string, int> > bindTemps_;
    // A lambda whose captures are scalar locals: the closure's slot and each capture's member and source, by node.
    struct LambdaCapture { std::string member; int offset = 0; const Type *type = nullptr;
                           std::string src; int srcOffset = 0; const Type *srcType = nullptr; bool srcConst = false; };
    struct LambdaFacts { bool simple = false; std::string slotName; int slot = 0; const Type *type = nullptr;
                         std::vector<LambdaCapture> captures; };
    std::unordered_map<std::uint32_t, LambdaFacts> lambdaFacts_;
    // A case label's folded value and the number caseLabel gave it, by node (M2.12n).
    struct CaseFacts { long long value; bool isDefault; int id; };
    std::unordered_map<std::uint32_t, CaseFacts> caseFacts_;
    LambdaFacts pendingLambda_;
    void noteTemporarySlot(const Expr *e) {
        const Unary *star = dynamic_cast<const Unary *>(e);
        const Comma *both = star != nullptr && star->op() == '*' ? dynamic_cast<const Comma *>(&star->operand()) : nullptr;
        const Unary *at = both != nullptr ? dynamic_cast<const Unary *>(&both->right()) : nullptr;
        const Var *tmp = at != nullptr && at->op() == '&' ? dynamic_cast<const Var *>(&at->operand()) : nullptr;
        if (tmp != nullptr && tmp->name() == "$tmp") temporarySlots_[e->syntax().v] = tmp->offset();
    }
    // **How a class goes to a function, and the two ABIs part company here.** Itanium
    // passes one by address whenever copying or destroying it is a call, and the caller
    // destroys the copy; Microsoft passes by the size rules and the callee destroys.
    bool passedByAddress(const Type *t) const {
        if (!t->isStructOrUnion()) return false;
        if (t->nonTrivialCopy()) return true;
        return !target_.microsoftNames() && t->hasDestructor();
    }
    bool returnsIndirectly(const Type *t, bool memberFn = false) const {
        int size = t->size(target_);

        // A class whose copy or destruction is a call is returned through a
        // hidden pointer whatever its size - the caller owns the storage and
        // the callee builds into it. Both ABIs agree here, measured.
        if (t->nonTrivialCopy() || t->hasDestructor()) return true;
        if (containsX87(t, target_)) return true;
        if (abi_.aggregatesByReference) {
            // **The Microsoft size rule is for free functions only.** A class returned
            // by value from a non-static member goes through the hidden pointer whatever
            // its size - measured with clang for this ABI, %rdx against %eax.
            if (memberFn) return true;
            return !(size == 1 || size == 2 || size == 4 || size == 8);
        }
        if (abi_.homogeneousFloatAggregates) {
            Kind elem;
            if (homogeneousFloatCount(t, &elem) > 0) return false;
        }
        return size > abi_.structReturnLimit;
    }
    bool variadicBody_ = false;

    std::size_t at_ = 0;

    std::vector<Local> locals_;

    std::vector<::Local> fnVars_;
    bool inParams_ = false;
    std::vector<std::size_t> scopeStarts_;
    std::vector<int> blocks_;
    std::vector<int> blockStack_;

    bool atFunctionBody_ = false;
    int frameSize_ = 0;
    std::vector<FreeSlot> freeSlots_;    // slots closed scopes gave back - see leaveScope
    int localSerial_ = 0;
    const Type *returnType_ = nullptr;
    // The frame slot the caller's hidden return pointer is kept in, or 0 - where `return` builds a class.
    int sretSlot_ = 0;
    // The one local every `return` of this body names, built where the caller asked (NRVO), or empty.
    std::string nrvoName_;
    std::string nrvoCandidate() const;
    // Set while a lambda's body is read to find its return type: a `return`
    // writes the type of its operand here, the first one deciding, and is
    // not checked against returnType_, there being none yet.
    const Type **deducingReturn_ = nullptr;
    std::string functionName_;
    // Above 0 inside sizeof, decltype, noexcept: [expr.prim.general]/13 lets `S::x` go without an object.
    int unevaluated_ = 0;
    // A `::`-qualified name being resolved: its key is absolute, no enclosing namespace asked.
    bool absoluteLookup_ = false;

    int strings_ = 0;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
    int caseIds_ = 0;

    // Objects that have been constructed and not yet destroyed - the entries
    // of alive_, below, innermost last.
    struct Alive {
        std::string name;
        int offset;
        const Type *cls;
        // Set for a by-value class parameter that arrived by address: its slot
        // holds the caller's pointer, so the object's address is what the slot
        // *contains* rather than where the slot sits.
        bool byAddress;
        long long count = 0;   // an array of this many `cls`, destroyed last first; 0 for one object
        // A subobject a constructor has built, [except.ctor]/3: `offset` is then the slot of `this`,
        // and the object `owner`'s member `member` or its base `base` - both -1 for the whole object.
        const Type *owner = nullptr;
        int member = -1, base = -1;
        int builtSlot = -1;   // an array being built: the slot counting its `cls` elements built so far
        // Destroyed only where the slot holds more than `whenAbove`: cl's virtual bases, built under a flag.
        int whenSlot = -1, whenAbove = 0;

        // C++11 has no aggregate with a member initialiser, so the braces call this.
        Alive(std::string name = std::string(), int offset = 0, const Type *cls = nullptr,
              bool byAddress = false)
            : name(std::move(name)), offset(offset), cls(cls), byAddress(byAddress) {}
    };
    // **A function written on its own, between the statements of another**: its pads are its own,
    // so the enclosing `try`'s chain and pad flag are set aside while it is built.
    struct OwnPads {
        Parser &p;
        std::string chain, handler;
        bool pads;
        explicit OwnPads(Parser &parser)
            : p(parser), chain(parser.tryChainLabel_), handler(parser.handlerResumeLabel_),
              pads(parser.functionHasPads_) {
            p.tryChainLabel_.clear();
            p.handlerResumeLabel_.clear();
            p.functionHasPads_ = false;
        }
        bool made() const { return p.functionHasPads_; }
        ~OwnPads() { p.tryChainLabel_ = chain; p.handlerResumeLabel_ = handler; p.functionHasPads_ = pads; }
    };
    // [except.ctor]/3 over an element loop: `loop` counts in `indexSlot` the elements of `a` built.
    StmtPtr guardElements(StmtPtr loop, Alive a, int indexSlot, std::size_t pos);

    // One automatic object a jump may not land past - see Local::guardsJump - or with `region`
    // a `try` block (1) or a handler (2), which [except.handle]/2 lets a jump leave and never enter.
    // Its serial is the identity: a slot may be shared, a name declared again in an inner block.
    struct JumpGuard {
        std::string name; int serial; int region;
        // C++11 has no aggregate with a member initialiser, so the braces call this.
        JumpGuard(std::string name = std::string(), int serial = 0, int region = 0)
            : name(std::move(name)), serial(serial), region(region) {}
    };
    std::vector<JumpGuard> jumpGuards() const;
    void checkJump(const std::vector<JumpGuard> &from,
                   const std::vector<JumpGuard> &to, std::size_t pos,
                   const std::string &jump, const std::string &origin) const;

    struct SwitchCtx {
        std::vector<const Case *> cases;
        const Case *deflt;
        const Type *governing;
        // What was in scope at the `switch` itself, which is where every one
        // of its case labels is jumped to from.
        std::vector<JumpGuard> guards;
    };
    std::vector<SwitchCtx> switches_;

    // A label or a goto, with what was in scope where it was written.
    struct LabelDef {
        std::string name;
        std::size_t pos;
        std::vector<JumpGuard> guards;
        // The objects alive there - a copy of alive_. A goto holding one its label does
        // not is a jump out of that object's scope and destroys it on the way: the calls
        // go into `cleanups`, filled by resolveGotos(). Null for a label.
        std::vector<Alive> alive;
        Block *cleanups;
        // A goto's own: whether it destroys what it leaves (a parent's half of a funclet's jump
        // does not), and the exit a Microsoft handler leaves its funclet by.
        bool destroys;
        std::string msExit;

        // C++11 has no aggregate with a member initialiser, so the braces call this.
        LabelDef(std::string name = std::string(), std::size_t pos = 0,
                 std::vector<JumpGuard> guards = std::vector<JumpGuard>(),
                 std::vector<Alive> alive = std::vector<Alive>(), Block *cleanups = nullptr,
                 bool destroys = true, std::string msExit = std::string())
            : name(std::move(name)), pos(pos), guards(std::move(guards)), alive(std::move(alive)),
              cleanups(cleanups), destroys(destroys), msExit(std::move(msExit)) {}
    };
    // `goto name;` as written here - what the jump destroys, ends and leaves is decided once the label is read.
    StmtPtr gotoStatement(const std::string &name, std::size_t pos, bool destroys);
    // alive_.size() at the entry to each loop body, and to each loop body
    // or switch body: what `continue` and `break` respectively destroy on
    // the way out is everything built since.
    std::vector<std::size_t> loopMarks_;
    std::vector<std::size_t> breakMarks_;
    StmtPtr jumpLeaving(StmtPtr jump, std::size_t mark, std::size_t pos,
                      int endsCatches);
    std::vector<LabelDef> labels_;
    std::vector<LabelDef> gotos_;

    const Token &peek() const { return tokens_[at_]; }
    const Token &peekAt(std::size_t n) const;
    bool endsInEmptyCall() const;
    bool consume(const char *s);
    void expect(const char *s);
    std::string expectIdent(const char *what);
    long long expectNumber(const char *what);

    bool atTypeName() const;
    const Type *memberTypeWalk(const Type *t);
    const Type *structOrUnionSpecifier(Kind kind, bool isClass = false);
    void checkAccessible(const Type *object, const Member &m, std::size_t pos) const;
    // A constructor is keyed in the function table under "Point::Point", so resolving
    // one is resolving an overload set like any other. And the last component of a
    // qualified tag: "Outer::Inner" is "Inner", which is what those two are written as.
    static std::string localOf(const std::string &qualified) {
        const std::size_t at = qualified.rfind("::");
        return at == std::string::npos ? qualified : qualified.substr(at + 2);
    }
    static std::string constructorKey(const std::string &cls) {
        return cls + "::" + localOf(cls);
    }
    // Whether the class declares a constructor - a constructor *template* counts, [class.ctor]/5,
    // though none of its specializations is in the table until something asks for one.
    bool hasConstructors(const std::string &cls) const {
        return lookup_.overloadsOf(constructorKey(cls)) != nullptr ||
               tmpl_.memberTemplates_.count(constructorKey(cls)) != 0;
    }
    void declareConstructor(const std::string &cls, std::size_t pos, Access access,
                            bool isExplicit);
    void declareDestructor(const std::string &cls, std::size_t pos, Access access,
                           bool isVirtual);
    void registerDestructor(const std::string &cls, std::size_t pos,
                            Access access, bool isVirtual, bool implicit,
                            VirtSpecifiers virt = VirtSpecifiers());
    // A member of this type, or an array of one, has a destructor to run.
    bool memberHasDestructor(const Type *t) const;
    // The base-object destructor of `cls`'s base `which`, on `this` - D2, or cl's `??1`.
    StmtPtr baseDestructorCall(const Type *cls, std::size_t which, int thisSlot, std::size_t pos);
    // The class's own base-object destructor, on `this` and not through its vtable.
    StmtPtr selfDestructorCall(const Type *cls, int thisSlot, std::size_t pos);
    // A constructor the compiler writes, `steps` in order with `done[i]` what step i finishes (no owner:
    // nothing) - each guarding the steps after it. Written on its own, so no enclosing `try` is its own.
    StmtPtr guardConstruction(std::vector<StmtPtr> steps, std::vector<Alive> done, std::size_t pos,
                              bool *guarded);
    // cl's virtual-base `init` in front of `rest`: the count zeroed, and the rest guarded by `vbs`.
    StmtPtr withVirtualBases(std::vector<StmtPtr> init, const std::vector<Alive> &vbs, StmtPtr rest,
                             std::size_t pos);
    // `rest` in one region destroying all of `as`, last first, on unwind.
    StmtPtr guardSubobjects(StmtPtr rest, const std::vector<Alive> &as, std::size_t pos);
    // [except.ctor]/3: `rest`, run after a subobject is built, in a region destroying it on unwind - and
    // `before` with it, which a pad handing over to a function try block's handlers owes as well.
    StmtPtr guardSubobject(StmtPtr rest, const Alive &a, std::size_t pos,
                           const std::vector<Alive> &before = std::vector<Alive>());
    // The construction calls for a class's virtual bases, reached by the
    // constant offset this class laid them down at. Shared by both of the
    // above and by the implicit members, which need the identical walk.
    std::vector<StmtPtr> virtualBaseCalls(const Type *type, int thisSlot,
                                          bool building, std::size_t pos,
                                          int srcSlot = -1, bool moving = false,
                                          std::map<std::string,
                                              std::vector<ExprPtr> >
                                              *vbaseArgs = nullptr,
                                          std::vector<int> *builtBase = nullptr);
    static std::string destructorKey(const std::string &cls) {
        return cls + "::~" + localOf(cls);
    }
    // `using B::f;` in a class body, by the class's tag: the base and the name, joined at completion (M1 L2).
    std::map<std::string, std::vector<std::pair<const Type *, std::string> > > usingMembers_;
    void declareUsingMembers(const std::string &tag, const Type *type);
    // `using B::B;` [class.inhctor]: the bases a class inherits constructors from (Classes::inheriting_ says what each forwards to).
    std::map<std::string, std::vector<const Type *> > inheritFrom_;
    // An inherited constructor's symbol to the class that first declared it - `E : D : B` makes E's `ECI11BEi`.
    std::map<std::string, const Type *> inheritedFrom_;
    // The vtable, type_info and name string each class emitted, by tag, so the key-function rule can take them back.
    std::map<std::string, std::vector<std::string> > classSymbols_;
    void markInlineBody(std::size_t which) { if (which != PendingBody::npos()) lookup_.functions_[which].inlineBody = true; }
    static std::string assignmentKey(const std::string &cls) {
        return cls + "::operator=";
    }
    // `int i = 0; while (i < count) { one; i = i + 1; }` over an array member's elements.
    StmtPtr eachElement(int indexSlot, long long count, StmtPtr one);
    StmtPtr eachElement(int indexSlot, ExprPtr count, StmtPtr one);
    // The two loops an array of a class needs, each a file-local function
    // synthesized once per class: `(T *base, size_t n)` building every
    // element with the default constructor, and destroying them last first.
    std::string vectorLoopName(const char *which, const Type *cls, std::size_t pos);
    std::string vectorConstructor(const Type *cls, std::size_t pos);
    std::string vectorDestructor(const Type *cls, std::size_t pos);
    ExprPtr callVectorLoop(const std::string &fn, const Type *cls, ExprPtr base,
                           ExprPtr count, std::size_t pos);
    // The bytes before an array from `new T[n]` that hold n, for delete[].
    int arrayCookie(const Type *elem) const;
    std::vector<StmtPtr> buildStaticArrayConstruction(Declared &d,
                                                      const std::string &symbol,
                                                      const std::string &helper);
    const Signature *destructorOf(const Type *cls) const;
    // **Whether a class is a POD for layout**, which decides tail-padding reuse.
    bool podForLayout(const Type *t) const;
    // [class.mem]/1: one member of each name in a class, bases apart.
    void refuseDuplicateMember(const std::vector<Member> &members,
                               std::size_t ownFrom, const std::string &name,
                               const std::string &tag, std::size_t pos);
    ExprPtr destructorCall(ExprPtr address, const Signature &dtor, std::size_t pos);
    void destroyObject(std::vector<StmtPtr> &into, const Alive &a, std::size_t pos);

    // Objects that have been constructed and not yet destroyed, innermost last.
    std::vector<Alive> alive_;
    // Temporaries this full expression has made for by-value class arguments.
    struct Temporary {
        int slot;
        const Type *type;
        int flag;
        // Storage a new-expression allocated and is still building in: [expr.new]/20
        // gives it back through the matching deallocation function if that throws.
        bool newStorage = false;
        bool newArray = false, newNothrow = false;
        int newCookie = 0;
        // **The storage `__cxa_allocate_exception` handed back**, which is a temporary of the throw's own full
        // expression and is released with `__cxa_free_exception` rather than destroyed - and **an array of `type`**,
        // `count` long: a braced list's backing array made inside an expression.
        bool exceptionStorage = false;
        long long count = 0;
        Temporary(int slot = 0, const Type *type = nullptr, int flag = 0)
            : slot(slot), type(type), flag(flag) {}
    };

    // **The ways out of a Microsoft handler**, one record per `try`: a funclet
    // cannot jump into its parent, so each early exit names a label the parent
    // lays down after the `try` and hands it back as the address to carry on at.
    struct LocalTypeUndo { std::string name; const Type *had; };   // see localTypeUndo_
    struct MsExit {
        std::string ret, brk, cont;   // the labels, empty until an exit asks
        std::vector<std::pair<std::string, std::string> > gotos;   // an exit label, and the goto's
        int retSlot = 0;              // the returned value, saved for the parent
        std::string retTemp;
    };
    // **Everything that belongs to the function currently being parsed.** cxx1
    // re-enters parsing from a saved token index in eight places.
    struct FunctionState {
        std::size_t at = 0;
        std::string currentFunction, currentFunctionName, functionName;
        std::map<std::string, const Type *> localTypes;
        std::vector<LocalTypeUndo> localTypeUndo;
        std::vector<std::size_t> localTypeUndoStarts;
        std::vector<Local> locals;
        std::vector<::Local> fnVars;
        std::vector<std::size_t> scopeStarts;
        std::vector<int> blocks, blockStack;
        std::vector<LabelDef> labels, gotos;
        int frameSize = 0, thisOffset = 0, frameAlign = 16;
        std::vector<FreeSlot> freeSlots;
        int localSerial = 0;
        const Type *currentClass = nullptr;
        const Type *returnType = nullptr;
        int sretSlot = 0;
        std::string nrvoName;
        const Type **deducingReturn = nullptr;
        syntax::NodeId unaryOperand;
        CondParts condParts;
        std::vector<syntax::NodeId> callArgs;
        int unevaluated = 0;
        bool absoluteLookup = false;
        int lambdaCount = 0;
        bool atFunctionBody = false;
        std::vector<Alive> alive;
        std::vector<Temporary> pendingTemps;
        std::size_t bodyCleanupFrom = 0;
        bool functionHasPads = false, functionHasTry = false;
        std::vector<std::string> functionTypes;
        bool variadicBody = false, inStaticMember = false, inParams = false;
        std::vector<std::string> staticSymbols;
        std::vector<std::size_t> pendingDefaults;
        bool pendingNoexcept = false;
        DynamicSpec pendingSpec;
        bool inNoexceptFunction = false;
        bool replayingInline = false;
        bool currentFunctionInline = false;
        int tssSeq = 0;
        // The six a review found still missing, each with a program that
        // reached it: `break` and `case` written in a lambda inside a loop or
        // a switch were accepted and segfaulted the compiler.
        int loopDepth = 0, switchDepth = 0;
        std::vector<SwitchCtx> switches;
        std::vector<std::size_t> breakMarks;
        bool inTryBody = false, inMsHandler = false, inHandlerBody = false, inCtorTryHandler = false;
        MsExit *msExit = nullptr;
        int handlerDepth = 0;
        std::vector<int> handlerLoopDepth, handlerSwitchDepth;
        std::vector<std::size_t> handlerFrom, tryFrom;
        std::string handlerResumeLabel;
        // The enclosing `try` body's chain: a body replayed inside one is another function's (M1).
        std::string tryChainLabel;
        int tryChainPointerSlot = 0, tryChainSelectorSlot = 0;
        std::size_t tryChainAliveFrom = 0;
        std::vector<Try *> tryBodySegments;
        int handlerResumePtr = 0, handlerResumeSel = 0;
        int mayThrow = 0;
        bool conditionDecl = false;
        std::string conditionName;
        // Block-scope, and a lambda's body is its own block: a using-directive
        // written in one reached the end of the enclosing function.
        std::vector<std::string> usingNamespaces;
    };
    // **A parse whose result is discarded must put back what it accumulated.**
    struct Discarded {
        Parser *p;
        std::vector<Temporary> temps;
        std::vector<Alive> alive;
        explicit Discarded(Parser *pp)
            : p(pp), temps(pp->pendingTemps_), alive(pp->alive_) {}
        ~Discarded() { p->pendingTemps_ = temps; p->alive_ = alive; }
        Discarded(const Discarded &) = delete;
        Discarded &operator=(const Discarded &) = delete;
    };

    struct Unevaluated {
        Parser *p;
        explicit Unevaluated(Parser *pp) : p(pp) { p->unevaluated_++; }
        ~Unevaluated() { p->unevaluated_--; }
        Unevaluated(const Unevaluated &) = delete;
        Unevaluated &operator=(const Unevaluated &) = delete;
    };

    // **How deep the parser's own recursion is**, one per level of nesting (parentheses, lists, blocks, lambdas,
    // instantiations), refused by name past kMaxNesting before the stack runs out (review of 1.25, fix 5).
    // The 256 MB stack overflows at 5000+ instantiations: 2048 leaves room twice over, twice [implimits]' 1024.
    static const int kMaxNesting = 2048;
    int nesting_ = 0;
    struct Nesting {
        Parser *p;
        Nesting(Parser *pp, std::size_t pos, const char *what);
        ~Nesting() { p->nesting_--; }
        Nesting(const Nesting &) = delete;
        Nesting &operator=(const Nesting &) = delete;
    };

    FunctionState captureFunctionState() const;
    void restoreFunctionState(const FunctionState &s);
    void clearFunctionState();

    // **-keep-going** (M0 of cpp11 2): what a declaration that failed half-way leaves to be put back.
    // The guards that unwind with the exception - Nesting, Unevaluated, Discarded - restore themselves.
    struct DeclarationSnapshot {
        FunctionState fn;
        std::size_t classes = 0, namespaces = 0, split = 0;
        std::size_t instArgs = 0, instParams = 0, instBinding = 0, instValues = 0;
        bool unnamedNamespace = false, pattern = false, memberTemplateInst = false;
        bool definingQualified = false, directLinkage = false;
        // Set partway through a construct: left set by an error, `n > 0` reads as a closing angle.
        bool templateArgs = false, replaying = false, memberTemplateBody = false, deferBodies = false;
        bool overloadArgument = false, pendingNoexcept = false, pendingExplicit = false, tlsObject = false;
        int instantiationDepth = 0, nesting = 0;
        const Type *overloadTarget = nullptr;
        int cLinkage = 0;
        std::string inlineOwner;
        std::map<std::string, int> boundTypes, boundValues;
    };
    DeclarationSnapshot snapshotDeclaration() const;
    void restoreDeclaration(const DeclarationSnapshot &s);
    // One declaration; under -keep-going an error in it is reported and parsing goes on after it.
    void topLevelRecovering(Program &program);
    // Where a declaration from `from` ends - its `;`, its body's `}`, or the scope's `}` - always past `from`.
    std::size_t declarationEnd(std::size_t from) const;

    // `except` is the frame offset of an object not to destroy - the one being returned,
    // which the caller destroys instead. And a cleanup region's landing pad: destroy
    // alive_[from..to), last first, and hand the exception back to the unwinder.

    // A pad's steps as a block: all but the last (the resume, or the hand-over) in an inner block marked as unwinding.
    static StmtPtr unwindPad(std::vector<StmtPtr> steps);

    // The call that hands the exception in `pointerSlot` back to the unwinder, as this target's runtime takes it.
    StmtPtr resumeUnwinding(int pointerSlot);
    StmtPtr cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                       const std::vector<Temporary> &temps,
                       std::size_t pos,
                       const std::string &chainLabel = std::string());
    // The block's statements with each stretch that has objects alive turned
    // into a cleanup region of its own.
    std::vector<StmtPtr> wrapMsCleanups(
        std::vector<StmtPtr> body,
        const std::vector<std::pair<std::size_t, std::size_t> > &built,
        std::size_t aliveAtEntry, std::size_t pos,
        const std::vector<Temporary> &temps = std::vector<Temporary>());
    // `tryAt` names the statements a region must not span: a `try` is a row of
    // its own in the same table and destroys what it found alive itself.
    std::vector<StmtPtr> wrapCleanups(
        std::vector<StmtPtr> body,
        const std::vector<std::pair<std::size_t, std::size_t> > &built,
        std::size_t aliveAtEntry, std::size_t pos,
        const std::vector<Temporary> &temps = std::vector<Temporary>(),
        const std::vector<std::size_t> &tryAt = std::vector<std::size_t>());
    // `to` bounds the top of the range, for a cleanup pad that must destroy
    // only what existed at its point in the block.
    void emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                         std::size_t pos, int except = -1,
                         std::size_t to = static_cast<std::size_t>(-1));
    // `valueInit` is the `{}` half of [dcl.init]/8: the object is zeroed before
    // the constructor runs, and only where nobody wrote that constructor.
    StmtPtr constructLocal(const Declared &d, int offset,
                           std::vector<ExprPtr> args, bool copyInit = false,
                           bool valueInit = false);
    // The same construction into an object named by `symbol` when one is
    // given and by a frame slot otherwise - a static local, a file-scope
    // object and a static data member are the first kind.
    StmtPtr constructObject(const Declared &d, const std::string &symbol,
                            int offset, std::vector<ExprPtr> args,
                            bool copyInit, bool valueInit);
    ExprPtr objectAt(const Declared &d, const std::string &symbol, int offset);

    // **What a class with constructors is initialised from** - `(args)`,
    // `= expr`, `{}`, `= {}`, or a braced list for an initializer_list
    // constructor - shared by every storage duration, so it is written once.
    struct CtorInit {
        std::vector<ExprPtr> args;
        std::vector<StmtPtr> ilSetup;
        bool copyInit = false;
        bool valueInit = false;
        bool listInit = false;
        // A same-class source and no copy constructor: the bytes are asked for.
        bool trivialCopy = false;
    };
    CtorInit readConstructorInitialiser(const Declared &d);

    // **Dynamic initialisation** - [basic.start.init]/2 and [stmt.dcl]/4. The
    // statements of _GLOBAL__sub_I_<file>, its frame and its guards, built as
    // objects are met; parse() makes them one function the backends register.
    std::vector<StmtPtr> dynInit_;
    int dynInitFrame_ = 0;
    std::vector<int> dynInitGuards_;
    // **The same for thread storage** - [basic.stc.thread]: `__tls_init`, its frame and its guards,
    // built while tlsObject_ says the object being declared is a thread_local; the init functions
    // below write to initSink(), and registerDestruction asks the target's threadAtexit().
    std::vector<StmtPtr> tlsInit_;
    int tlsInitFrame_ = 0;
    std::vector<int> tlsInitGuards_;
    bool tlsObject_ = false;
    std::vector<StmtPtr> &initSink() { return tlsObject_ ? tlsInit_ : dynInit_; }
    // The external thread_locals this unit defines, those __tls_init builds (external and internal),
    // and those it reaches: what the Itanium wrappers and `_ZTH` aliases are made from.
    std::set<std::string> tlsDefined_, tlsDynamic_, tlsReached_, tlsInternalDynamic_, tlsHere_;
    void tlsDefining(const std::string &symbol, bool external);
    void finishTlsInit(Program &program);
    // The flags a Var reaching a thread_local carries, see Var::viaWrapper.
    void tlsVar(Var *v, bool viaWrapper, bool external);
    // A block-scope static just declared is a thread_local: marked, and taken out of the debug records.
    void markThreadLocal(std::size_t local, bool threadLocal);
    // **The widest alignment a slot of the function being parsed asked for**, 16 until one asks for
    // more; kept by the function's symbol for parse() to hand each Function, since fifteen sites
    // build one and the backends realign the frame from this.
    int frameAlign_ = 16;
    std::map<std::string, int> frameAligns_;
    // What a namespace-scope declaration says of its object's thread storage, recorded on the
    // symbol and checked against what an earlier declaration said - [dcl.stc]/4, clang's words.
    void noteThreadLocal(GlobalSym &g, bool prior, bool threadLocal, const std::string &name,
                         bool viaWrapper, bool external, bool defines, std::size_t pos);
    // `thread_local` written where only a variable may carry it - clang's words.
    void refuseThreadLocalHere(bool written, std::size_t pos);
    // Everything below runs inside the init function's frame. The state
    // struct is what every other re-entry door saves, and this is one more.
    FunctionState enterInitFunction();
    void leaveInitFunction(const FunctionState &outer);
    void finishDynamicInit(Program &program);
    // The construction of a class object with static storage duration and its
    // destructor's registration, in the frame in force: [basic.start.term]
    // through __cxa_atexit, or atexit and a helper on the Microsoft ABI.
    std::vector<StmtPtr> buildStaticConstruction(const Declared &d,
                                                 const std::string &symbol,
                                                 const std::string &helper);
    void registerDestruction(const Declared &d, const std::string &symbol,
                             const std::string &helper,
                             std::vector<StmtPtr> &into);
    ExprPtr functionAddress(const std::string &symbol, const Type *fnType);
    // A file-scope object with a constructor, and a static data member of one.
    void dynamicInitialise(Declared &d, const std::string &symbol,
                           const std::string &helper, bool once, std::vector<GlobalPiece> *folded = nullptr);
    // [stmt.dcl]/4.
    StmtPtr guardOnce(const std::string &symbol, std::vector<StmtPtr> body);
    StmtPtr guardOnceThread(const std::string &symbol, std::vector<StmtPtr> body);
    // Where guardSlots_ stood when the init function's frame was entered.
    std::size_t initGuardMark_ = 0;
    void staticLocalWithConstructor(Declared &d,
                                    std::vector<StmtPtr> &inits);
    // Whether that symbol is one - the per-unit `owner.name` has a dot, the ABI's names have none -
    // so that it and its guard are external and in a COMDAT, one object for the program.
    bool sharedStaticLocal(const std::string &symbol) const {
        return currentFunctionInline_ && symbol.find('.') == std::string::npos;
    }
    // A reference with static storage duration: bound statically where the
    // initialiser is the address of a global, and before main otherwise.
    void bindStaticReference(const Declared &d, const std::string &symbol,
                             std::vector<GlobalPiece> &pieces, bool &hasInit,
                             std::vector<StmtPtr> *into);

    // Whether a call to this function has to be given an object. A member's
    // `owner` used to answer it on its own, and a static member is exactly the
    // case where the two questions come apart.
    static bool needsThis(const Signature &sig) {
        return !sig.owner.empty() && !sig.isStaticMember;
    }
    // Whether any function under this key is a static member. A call written
    // `C::f(...)` means one only if it is - `Base::f(...)` on an ordinary
    // member is a different construct and must not be taken for this one.
    bool hasStaticMemberNamed(const std::string &key) const {
        const std::vector<std::size_t> *set = lookup_.overloadsOf(key);
        if (set == nullptr) return false;
        for (std::size_t i = 0; i < set->size(); i++)
            if (lookup_.functions_[(*set)[i]].isStaticMember) return true;
        return false;
    }
    void declareStaticMember(const std::string &cls, Type *owner,
                             const Declared &d, Access access, bool volatileWritten,
                             bool threadLocal, bool isConstexpr = false);
    void defineConstexprStaticMember(const std::string &full, const std::string &symbol, const Declared &d);
    std::set<std::string> inClassConstexprStatics_;
    void defineStaticMember(Declared &d, Program &program, bool threadLocal);
    // **What a static member *defined* out of line is worth when read** - the
    // [expr.const]/3 read-back a namespace-scope const already makes.
    struct StaticConst {
        bool known = false;   long long value = 0;
        bool dknown = false;  long double dvalue = 0;
    };
    std::map<std::string, StaticConst> staticConsts_;
    // The static data members an evaluated expression named, by symbol: a class template's
    // is defined only where one of these says so - [temp.inst]/3.
    std::set<std::string> usedStaticMembers_;
    const StaticConst *findStaticConst(const std::string &symbol) const {
        std::map<std::string, StaticConst>::const_iterator i =
            staticConsts_.find(symbol);
        return i == staticConsts_.end() ? nullptr : &i->second;
    }
    ExprPtr staticMemberRef(const Type *owner, const Type::StaticMember &s,
                            const std::string &cls, std::size_t pos);
    // `S::x` for a non-static data member, in an unevaluated operand: an lvalue of its type.
    ExprPtr memberWithoutObject(const Type *cls, const Member &m, std::size_t pos);
    ExprPtr unevaluatedMember();
    void declareMember(const std::string &cls, const Declared &d, bool constThis,
                       RefQual refThis, Access access, bool inUnion, bool isVirtual,
                       bool isStatic = false, bool isPure = false,
                       VirtSpecifiers virt = VirtSpecifiers());
    // [class.virtual]/4: the slot a member is about to take must not be `final`.
    void checkNotFinal(const VSlot &s, const std::string &cls, const std::string &name,
                       std::size_t pos) const;
    // The entry a pure virtual's slot holds: the runtime routine that reports
    // a call reaching a function the class never defined. Both measured -
    // `__cxa_pure_virtual` from clang, `_purecall` from cl.
    void checkNotAbstract(const Type *t, std::size_t pos, const std::string &what);
    const char *pureVirtualSymbol() const {
        return target_.microsoftNames() ? "_purecall" : "__cxa_pure_virtual";
    }
    // `S a[4];` - the default constructor once per element, and `S a[3] = { ... };` - each element
    // from its initialiser, the rest value-initialised. Apart from constructLocal, which builds a
    // single object and names the class through d.type, which for an array is the array.
    StmtPtr constructLocalArray(const Declared &d, int offset, int indexSlot);
    StmtPtr constructLocalArrayFromList(Declared &d, int offset,
                                        const std::function<int()> &declareLate,
                                        const std::string &symbol = std::string());
    ExprPtr memberCall(ExprPtr object, const Type *cls, const std::string &name,
                       std::size_t pos);
    // `forceOwner` names the class a *qualified* call reached - `b.Base::f()`
    // or, inside a member, `Base::f()`.
    ExprPtr memberCallWith(ExprPtr object, const Type *cls,
                           const std::string &name, std::size_t pos,
                           std::vector<ExprPtr> args,
                           const Type *forceOwner = nullptr);

    // The class a member template's signature is read in, for lookup alone ([basic.scope.class]).
    const Type *signatureClass_ = nullptr;
    // The classes whose bodies are being parsed, outermost first, which gives a nested class its tag.
    std::vector<const Type *> classStack_;
    // Whether the class being parsed, or the one whose member function is being parsed, is this class or something derived from it.
    bool insideClass(const Type *cls) const;
    // **[class.access]/6**.
    bool definesMemberOf(const Type *cls, std::size_t from) const;
    // `Point::Point(` and `Outer::Inner::~Inner(` have no type before the
    // name and the name IS a type, so the specifier list has to decline them.
    bool atUntypedMemberDefinition() const;
    const Type *currentClass_ = nullptr;
    int thisOffset_ = 0;
    const Type *enumSpecifier();
    bool atDeclarationStart() const;
    // Where a written qualified type name ends, **past a template argument list**.
    std::size_t qualifiedTypeEndPastArgs() const;
    // `volatile` is read and dropped, which is honest on an object and not
    // once the qualifier is under a `*`, a `&`, or on `this`. These two say so
    // by name rather than letting a wrong linkage name out.
    void refuseVolatilePointer();
    bool sawVolatile();
    void refuseVolatileWithLinkage(bool written, bool internal, std::size_t pos);
    void refuseVolatileUnderADeclarator(StorageClass storage);
    const Type *specifiers(StorageClass *storage, Qualifiers *quals = nullptr);
    const Type *unqualifiedSpecifiers(StorageClass *storage, Qualifiers *quals);

    Declared declarator(const Type *base, bool nameOptional = false,
                        bool insideParens = false);
    // `operator+` where a declarator wants a name: the whole of it is the name, so every
    // table keys it as it keys `get`. Then a lambda - rung 7.6 - and `P(1)`, a class
    // temporary built into a slot of this frame.
    ExprPtr classTemporary(const Type *cls, std::size_t pos);
    ExprPtr lambdaExpression();
    // [expr.prim.lambda]/4 with no trailing return type: a body that is one
    // `return expression;` has that expression's type, and anything else is void. The
    // expression is read with the parameters in scope and then put back, as 7.1 does.
    const Type *deduceLambdaReturn(std::size_t paramsFrom, std::size_t paramsTo,
                                   std::size_t bodyFrom, std::size_t bodyTo,
                                   const std::vector<std::string> &capNames,
                                   const std::vector<const Type *> &capTypes);
    int lambdaCount_ = 0;
    // Closures met while a body is read for its return type, counted apart.
    int deducedClosures_ = 0;
    // A closure's return-type typedef needs a name no other lambda will take.
    int lambdaRetSeq_ = 0;
    // The class a closure captured `this` from, by closure tag.
    std::map<std::string, const Type *> closureOuter_;
    // A local class's enclosing member function's class, by its tag: [class.local]/1 (M1 L10).
    std::map<std::string, const Type *> localClassOuter_;
    // The class a lambda was written in, by closure tag, `this` captured or
    // not: [expr.prim.lambda]/7 puts the body in that member function's scope,
    // so its statics, enumerators and nested types are named bare.
    std::map<std::string, const Type *> closureScope_;
    const Type *lambdaScope() const;
    // The const locals with known values of the function a lambda was written in, by closure tag:
    // its body reads them without a capture, [expr.prim.lambda]/12 (1.27).
    std::map<std::string, std::vector<Local> > closureConstants_;
    std::vector<Local> constantLocals() const;
    bool hasOrdinaryMember(const Type *cls, const std::string &name);
    // Does this type mention a template parameter - the one test the pattern read's stand-ins ask.
    bool dependentType(const Type *t) const;
    bool dependentArgs(const std::vector<TemplateArg> &args) const;
    // **Expression SFINAE**: an expression over a pattern's parameters, read as a tree that types
    // nothing (ParserDependent.cpp). `angle` stops at an argument list's `>`; `comma` lets `,` in.
    bool dependentAhead(bool angle) const;
    const DepExpr *dependentExpression(bool angle, bool comma);
    const DepExpr *depAssignment(bool angle);
    const DepExpr *depBinary(int least, bool angle);
    const DepExpr *depUnary(bool angle);
    const DepExpr *depPostfix(bool angle);
    const DepExpr *depPrimary(bool angle);
    const DepExpr *depCast(const Type *to);
    const DepExpr *depNode(DepExpr e);
    void depRefuse(std::size_t pos, const std::string &what);
    std::string depText(std::size_t from, std::size_t to) const;
    // The function an expression designates as an lvalue, or null.
    const Type *functionDesignated(const Expr &e) const;
    // `$this` - the member a `[this]` capture holds.
    static const char *capturedThis() { return "$this"; }
    // The class the enclosing function's `this` points at, const and all.
    const Type *capturedThisClass();
    // Is this name one of the enclosing class's own.
    bool namesOwnMember(const std::string &name);
    // Inside a closure that captured `this`, the pointer it holds.
    ExprPtr capturedThisPointer();
    // Does the code being parsed have a member's-eye view of this class?
    bool insideAccessOf(const Type *cls,
                        Access access = Access::Private) const;
    // **Accessibility asked from a named class rather than from the one being parsed.**
    bool accessibleFrom(const Type *from, const Type *owner, Access a) const;
    // Where `base` sits in `derived` along a path accessible here, [class.access.base]/4; -1 if none.
    int accessibleBaseOffset(const Type *derived, const Type *base) const;
    // Whether code here may step from `d` to a base it names with access `how`.
    bool baseStepAccessible(const Type *d, Access how) const;
    // [class.access.base]/5: a member declared in `decl` with `own` access, named in `n`.
    bool memberReachable(const Type *n, const Type *decl, Access own) const;
    // A name that is a *capture of the lambda around this one*.
    ExprPtr outerCaptureAccess(const std::string &name);
    const Type *referenceCaptureType(const Type *captured);
    // **One closure type per lambda written, however often it is read.** 7.1 reads an
    // `auto` initialiser twice, so a lambda reached here twice and made two classes, and
    // the declaration then refused its own initialiser. Keyed by the '[' token.
    struct MadeLambda {
        const Type *type;
        std::size_t end;
        // How many tokens the lambda spans, for a reading of a copy of them.
        std::size_t span = 0;
        // The captures have to be copied in on *every* reading, not only the
        // one that built the class - the second reading was handing back an
        // uninitialised closure and the lambda saw whatever was on the stack.
        std::vector<std::string> names;
        std::vector<const Type *> types;
        std::vector<int> offsets;
    };
    // One closure object: a frame slot, and each capture copied into it.
    ExprPtr buildClosure(const MadeLambda &made, std::size_t pos);
    ExprPtr buildClosureObject(const MadeLambda &made, std::size_t pos);
    void noteClosure(const MadeLambda &made, const std::string &name, int off);
    // The slot a closure object is built in, where `e` is one as buildClosureObject made it; -1 otherwise.
    static int closureSlotOf(const Expr *e);
    // A closure met while an enclosing lambda's body was read for its return type (A21),
    // by that reading's function and the `[`'s source position: the real reading, from a
    // copy of the tokens, takes it back, so the deduced return type and the object agree.
    typedef std::map<std::pair<std::string, std::size_t>, MadeLambda> ClosureMap;
    ClosureMap deducedAt_;
    // **Keyed by the function being compiled as well**: a template's body is replayed for every instantiation, and one closure shared by `tw<int>` and `tw<double>` ran int's body for both.
    ClosureMap lambdaAt_;

    std::string operatorName();
    // **The type a conversion function converts to**, set by operatorName when it reads one and read by the caller straight after.
    const Type *conversionTarget_ = nullptr;
    // Whether a member name is a conversion function's. The punctuation forms
    // are `operator+` with no space; this one is `operator bool`, with one.
    static bool isConversionName(const std::string &name) {
        return name.compare(0, 9, "operator ") == 0;
    }
    std::string declaredName(const char *what);

    // [class.friend].
    std::map<std::string, std::vector<std::string> > friends_;

    // The linkage name of the function whose body is being read, which is the whole of what an access check needs to ask about friendship.
    std::string currentFunction_;
    // The same function's *source* name, which a local class is qualified by.
    std::string currentFunctionName_;
    // Whether that function is inline (written so, in-class, templated): its static locals are COMDATs (1.28).
    bool currentFunctionInline_ = false;
    // cl's `$TSS0`, `$TSS1` guards count up within one function.
    int tssSeq_ = 0;

    bool isFriendOf(const Type *cls) const;

    // A block's typedefs are its own ([basic.scope.block]): what each replaced, put back as the scope closes.
    std::vector<LocalTypeUndo> localTypeUndo_;
    std::vector<std::size_t> localTypeUndoStarts_;
    // [class.local]. A class defined in a function body belongs to that function: two
    // functions may each define `struct L` and they are different types. Both facts come
    // from these tables - a qualified tag, and a scope emptied when the function ends.
    std::map<std::string, const Type *> localTypes_;   // written name -> type
    std::map<std::string, std::string> localClassOwner_;  // tag -> enclosing symbol
    // An operator this compiler can *name* but cannot yet reach from an expression is
    // refused where it is declared. The arity decides which - `operator-` is two
    // functions and only one is missing - so it is asked once the parameters are known.
    void checkOperatorDeclarable(const std::string &name,
                                 const std::vector<const Type *> &params,
                                 bool member, std::size_t pos,
                                 bool internal = false);
    // [over.literal]/2-/8: where a literal operator may be declared, and the lists it may take.
    void checkLiteralOperator(const std::string &name, const std::vector<const Type *> &params,
                              bool member, std::size_t pos);
    // [over.literal]/5: a literal operator template's own list is one `char...` and its function's empty.
    void checkLiteralOperatorTemplate(const TemplateDecl &decl, const Type *pattern);
    // [lex.ext]/2-8: a literal with a ud-suffix, as the call of the literal operator it names.
    ExprPtr userDefinedLiteral(Program *program);
    // Whether the literal at the cursor - adjacent strings taken together - carries a ud-suffix.
    bool atUserDefinedLiteral() const;
    const Type *arraySuffix(const Type *base, std::size_t pos);
    const Type *promote(const Type *t) const;
    const Type *usualArithmetic(const Type *a, const Type *b) const;
    ExprPtr convert(ExprPtr e, const Type *to, bool allowExplicit = false) const;
    ExprPtr convertNode(ExprPtr e, const Type *to, bool allowExplicit) const;
    // The base subobject of a glvalue of a derived class, `*(B *)&d` - a base's offset carried.
    ExprPtr baseSubobject(ExprPtr e, const Type *base) const;
    ExprPtr assignValue(ExprPtr n, ExprPtr value, std::size_t pos);
    StmtPtr oneStatement(std::vector<StmtPtr> all);
    ExprPtr zeroOf(const Type *to);
    ExprPtr enumeratorValue(const EnumConst &e);
    ExprPtr thisObject();
    ExprPtr namedAfter(ExprPtr effect, ExprPtr obj);
    // [conv.mem]: a pointer to a member of B becomes one to a member of D, D
    // derived from B; the base's offset, or -1 where that is not the case.
    int memberPointerToDerived(const Type *from, const Type *to) const;
    ExprPtr convertMemberPointer(ExprPtr e, const Type *to, int shift) const;
    const Type *unsignedVersion(const Type *t) const;

    ExprPtr decay(ExprPtr e);
    void requireScalar(const Expr &e, std::size_t pos, const char *what);

    void checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                         const std::string &what) const;

    int declare(const std::string &name, const Type *type, std::size_t pos,
                int alignAtLeast = 0);
    int allocateFrameSlot(const Type *type, int alignAtLeast = 0);
    // A synthesised function's own frame, opened mid-function: size 0 and no free slot, the caller's put back by closeFrame.
    struct FrameSave { int size; std::vector<FreeSlot> free; };
    FrameSave openFrame();
    void closeFrame(const FrameSave &saved);
    int alignasSpecifier();
    void refuseWeakAlignas(int asked, const Type *t, std::size_t pos);
    void declareStaticLocal(const std::string &name, const Type *type,
                            std::size_t pos, const std::string &symbol);
    void requireAssignable(const Expr &e, std::size_t pos, const char *what);
    // **A template's own scope has none of the locals of the function that named it**: held aside
    // while its declaration or class body is read, put back on every way out.
    struct LocalsHidden {
        Parser *p;
        std::vector<Local> held;
        explicit LocalsHidden(Parser *q) : p(q) { held.swap(p->locals_); }
        ~LocalsHidden() { held.swap(p->locals_); }
    };
    bool localHidesType() const;
    std::size_t temporaryCallEnd() const;
    bool atTemporaryMemberAccess() const;
    void enterScope();
    void leaveScope();

    int enterBlock();
    void leaveBlock();
    int currentBlock() const { return blockStack_.empty() ? 0 : blockStack_.back(); }

    const Type *composite(const Type *a, const Type *b);
    const Type *compositePointer(const Type *a, const Type *b);
    void declareFunction(const std::string &name, const Type *returns,
                         const std::vector<const Type *> &params,
                         bool variadic, bool defining, std::size_t pos,
                         bool internal = false);
    ExprPtr defaultPromote(ExprPtr e);

    const Type *decayedType(const Type *t);
    // `&` or `&&` after a member's parameter list - [dcl.fct]/6 - consumed; None where there is none.
    RefQual refQualifier();
    void refuseMemberPointerCategory(const Expr &mp, bool lvalue, std::size_t pos) const;
    void refuseSpecialRefQualifier(const char *what) const;

    ExprPtr newExpression(std::size_t pos);
    ExprPtr deleteExpression(std::size_t pos);
    // `p->~T()`, `o.~T()`, `p->B::~B()` and the pseudo-destructor on a scalar; null, nothing read, when the tokens are not that.
    ExprPtr explicitDestructorCall(ExprPtr &object, std::size_t pos);
    // The virtual destructor read out of the object held in `temp`: Itanium's D1 or D0 slot, Microsoft's `??_G` with the flag.
    ExprPtr virtualDestructorCall(const std::string &temp, int slot, const Type *t,
                                  const Type *cls, bool deleting, std::size_t pos);
    // `try { ... } catch (T e) { ... }` - rung 6.3.
    // A constructor's function try block hands its body in as `bodyFn` - parsed and assembled with the
    // `try` open - and `rethrows`: each handler ends rethrowing, [except.handle]/15.
    StmtPtr tryStatement(std::size_t pos, const std::function<StmtPtr()> &bodyFn = nullptr,
                         bool rethrows = false, const std::string &spec = std::string());
    // Inside a handler of a constructor's function try block, where `return` is ill-formed.
    bool inCtorTryHandler_ = false;
    // Set while a try's body is read, so a try inside one is refused.
    bool inTryBody_ = false;
    // Inside a Microsoft `catch` body, which is compiled as a funclet - a function of its own.
    bool inMsHandler_ = false;
    MsExit *msExit_ = nullptr;
    int msUnwindHelp();          // a fresh slot; the walker keeps one per function
    std::string msExitLabel(std::string &label, const char *kind);
    // Inside a handler's own block, on any target.
    bool inHandlerBody_ = false;
    // **How many Itanium handlers the statement being parsed is inside**, and
    // the loop and switch depth each of them was entered at.
    int handlerDepth_ = 0;
    std::vector<int> handlerLoopDepth_;
    std::vector<int> handlerSwitchDepth_;
    // **The pad that ends the catch this statement is inside**, for the way
    // out no jump can be written for: an exception.
    std::string handlerResumeLabel_;
    int handlerResumePtr_ = 0;
    int handlerResumeSel_ = 0;
    // Where an exception leaving this point goes.
    std::string unwindTarget(int *ptrSlot, int *selSlot) const;
    // One temporary of a block, released under its guard by a pad.
    void releaseGuarded(std::vector<StmtPtr> &steps, const Temporary &t);
    // `body` under a cleanup of its own: `onUnwind` runs when an exception leaves it, and the unwinding goes on.
    StmtPtr cleanupRegion(std::vector<StmtPtr> body, StmtPtr onUnwind);
    // Where each open handler's block begins, telling a `goto` from a jump.
    std::vector<std::size_t> handlerFrom_;
    // Where each open `try` block begins, for the jumps [except.handle]/2 forbids into one.
    std::vector<std::size_t> tryFrom_;
    // The `__cxa_end_catch` calls a jump out of `count` handlers has to make, appended after whatever destructors that jump already runs.
    void endCatches(std::vector<StmtPtr> &into, int count);
    // How many handlers a `break` or a `continue` here leaves.
    int handlersLeftByBreak() const;
    int handlersLeftByContinue() const;
    // Set when this function has a landing pad, so its prologue names the personality routine and the table.
    bool functionHasPads_ = false;
    // **The selector is an index into the whole function's type table, not into one try's handler list.**
    std::vector<std::string> functionTypes_;
    // **Set while a `try`'s body is parsed, empty everywhere else.**
    std::string tryChainLabel_;
    int tryChainPointerSlot_ = 0;
    int tryChainSelectorSlot_ = 0;
    // **What a `try` owes on its way out**: a block splits its cleanup region around the `try`s that are its own
    // statements, and only those - an enclosing block's region covers the rest. So the block reading a statement
    // says where its own objects start, and at which token the statement began (M1).
    std::size_t blockOwesFrom_ = 0;
    std::size_t blockStatementAt_ = static_cast<std::size_t>(-1);
    // How much was alive when the `try` was entered. A segment's pad destroys
    // everything the *body* built, not only what its own block did: it jumps to
    // the chain instead of resuming, so no enclosing pad runs after it.
    std::size_t tryChainAliveFrom_ = 0;
    std::vector<Try *> tryBodySegments_;
    // 1-based, first occurrence winning, matching Walker::lsdaTable byte for byte.
    int typeIndexFor(const std::string &symbol);
    // Set where a function writes a `try`.
    bool functionHasTry_ = false;
    // `throw x;` - rung 6.2. Answers the statement it lowers to.
    StmtPtr throwStatement(ExprPtr value, std::size_t pos);
    StmtPtr microsoftThrow(ExprPtr value, std::size_t pos);
    // The copy constructors and destructor a Microsoft ThrowInfo chain names, marked used, and the chain recorded.
    void finishMicrosoftThrow(MicrosoftThrow &names, bool thrown);
    // A call to something in the runtime, named by its symbol and needing no
    // declaration - the same shape callAllocator has used for operator new.
    // A temporary built by a named constructor from arguments already parsed.
    ExprPtr constructTemporary(const Type *plain, const Signature &ctor,
                               std::vector<ExprPtr> args, bool zeroFirst,
                               std::size_t pos);
    // **[over.ics.user]**.
    const Signature *convertingConstructor(const Type *to, const Expr &from,
                                           std::size_t pos = 0, bool directInit = false);
    // The conversion function on `from` giving `to`, or - with `to` null - any
    // scalar, `bool` first. The mirror of the constructor above.
    const Signature *conversionFunction(const Type *from, const Type *to,
                                        bool allowExplicit = false);
    // A class where a number or a pointer is wanted, converted by its own.
    ExprPtr contextualScalar(ExprPtr e, std::size_t pos, const char *what);
    // The single conversion to a scalar, or null where there are none or two.
    const Signature *soleNumericConversion(const Type *from);
    // The conversion itself, or null when none is needed or possible.
    ExprPtr userConversion(const Type *param, ExprPtr &arg, std::size_t pos);
    // **[over.ics.user] through a conversion *function***: one argument of
    // another class that converts to `cls`. An explicit one counts only in
    // direct-initialisation, [over.match.copy]/1.
    bool convertThroughConversionFunction(std::vector<ExprPtr> &args,
                                          const Type *cls, bool directInit,
                                          std::size_t pos);
    bool constructorViable(const Type *cls, const std::vector<ExprPtr> &args,
                           std::size_t pos = 0);
    void refuseUnrelatedClassCast(const Expr &v, const Type *to,
                                  std::size_t pos, const char *what);
    // `return value;` built through the hidden pointer by the move or copy constructor; `*sret` is the answer.
    ExprPtr buildReturned(ExprPtr value, std::size_t pos, bool *named);
    bool pendingTemporaryAt(int slot) const;
    // **Copy-initialise a class into a slot somebody else owns**, as one
    // expression. `constructTemporary` and `materialiseCopy` each allocate a
    // slot of their own, and both arms of a `?:` have to build into one.
    ExprPtr buildInto(const Type *cls, int slot, ExprPtr value, int guard,
                      std::size_t pos);
    // Every temporary an arm made gets a guard, set at the end of that arm.
    ExprPtr markArmTemporaries(ExprPtr arm, std::size_t from, std::size_t to);
    // The same, for an operand `&&` or `||` may skip; the operand's value is
    // kept and handed back, since the comparison is what reads it.
    ExprPtr markSkippableTemporaries(ExprPtr operand, std::size_t from,
                                     std::size_t to);
    ExprPtr rethrowCall();
    ExprPtr runtimeCall(const char *symbol, const Type *returns,
                        std::vector<ExprPtr> args);
    const Signature *classAllocator(const Type *made, const char *which);
    ExprPtr typeidExpression(std::size_t pos);
    ExprPtr deallocate(const Type *pointee, ExprPtr raw, std::size_t pos);
    ExprPtr deallocateArray(const Type *pointee, ExprPtr raw, std::size_t pos);
    // `new (a, b) T`: the `operator new` the placement arguments reach, class first.
    ExprPtr userPlacementAllocation(const Type *made, bool array, ExprPtr bytes,
                                    std::vector<ExprPtr> &extra, std::size_t pos);
    ExprPtr callNothrowDeallocator(bool array, ExprPtr raw);
    ExprPtr callNothrowAllocator(bool array, ExprPtr bytes, ExprPtr tag, std::size_t pos);
    ExprPtr callAllocator(const char *itanium, const char *microsoft,
                          const Type *returns, ExprPtr arg, std::size_t pos);
    int newTemps_ = 0;

    void parseArguments(std::vector<ExprPtr> &args);
    // The copy the caller makes for a by-value class argument: a temporary in the
    // caller's frame, built by the copy constructor, whose address the callee receives.
    // And `p != 0 ? (body, 1) : 0`, which [expr.delete]/2 wraps a destructor call in.
    ExprPtr guardAgainstNull(const std::string &temp, int slot,
                             const Type *ptr, ExprPtr body);
    ExprPtr materialiseCopy(const Type *type, ExprPtr arg, std::size_t pos,
                            const std::string &what,
                            std::vector<Temporary> &destroy);
    std::vector<Temporary> pendingTemps_;
    // The temporaries each statement of the block being parsed made, so that the cleanup regions can name them.
    std::vector<Temporary> statementTemps_;
    // **Where a function body's cleanup regions start**, which on Windows is
    // before its by-value parameters: the callee destroys those there, and an
    // exception leaving the body has to reach them.
    std::size_t bodyCleanupFrom_ = 0;
    int guardFlag();
    // **Every guard starts clear, and the one place that is true of every path
    // is the function's entry.**
    std::vector<int> guardSlots_;
    ExprPtr setGuard(int flag, int value);
    // **Taking over a call's result slot takes over its destruction too.**
    void claimCallResult(Call &c, int slot);
    // A temporary's destruction as an expression - its destructor, or the class's array loop; null if none.
    ExprPtr temporaryDestruction(const Temporary &t, std::size_t pos);
    ExprPtr endFullExpression(ExprPtr e);
    // Drop the temporary this expression yields from the pending list, because its ownership is leaving the frame.
    bool releaseTemporary(const Expr &value);
    // [class.temporary]/5 - a temporary bound to a reference lives to the end of the reference's scope.
    bool extendTemporary(const Expr &addr, const std::string &name);
    // The index in pendingTemps_ of the temporary a bound address names, or -1.
    int pendingTemporaryOf(const Expr &e) const;
    void flushTemporaries(std::vector<StmtPtr> &into);
    ExprPtr completeCall(const std::string &name, const std::string &symbol,
                         ExprPtr callee, const Type *returns,
                         const std::vector<const Type *> &params, bool variadic,
                         std::size_t pos, std::vector<ExprPtr> args,
                        bool hasThis = false, int vbInit = 1);

    void parameterTypes(std::vector<const Type *> &params, bool &variadic);
    std::vector<const Type *> writtenParams_;
    std::vector<const Type *> writtenFor_;

    // **A default argument is kept as a place in the token stream, not as a parsed
    // expression**: [dcl.fct.default] evaluates it afresh at every call that leaves it
    // out. `pendingDefaults_` carries them to declare(); `defaultArgs_` keys them.
    std::vector<std::size_t> pendingDefaults_;
    std::map<std::string, std::vector<std::size_t> > defaultArgs_;
    // **[dcl.fct.default]/5 reads a default in the scope of its declaration.**
    std::map<std::string, std::vector<std::string> > defaultArgNamespace_;
    // Past one default argument.
    void skipDefaultArgument();
    // **A member's own initialiser is kept as a place in the token stream**, the same way a default argument is and for the same reason.
    std::vector<std::string> namespaceStack_;
    // **A using-declaration is an alias, not a table.**
    std::map<std::string, std::string> usingDeclarations_;
    // `namespace A = N;` - the alias's qualified name to the namespace it names, for the
    // readers that meet the alias without a `::` after it; every other use is rewritten.
    std::map<std::string, std::string> namespaceAliases_;
    void namespaceAlias(const std::string &name, std::size_t pos);
    // WS-D: `auto f(params) -> T`, the return type read after the parameters.
    void refuseTrailingBase(const Type *declared, std::size_t pos);
    const Type *readTrailingReturn(std::size_t paramsOpen);
    std::size_t skipFunctionQualifiers(std::size_t i) const;
    bool autoBeforeQualifiedName() const;
    bool trailingArrowAhead() const;
    StmtPtr delegatingCall(const Declared &d, const Type *cls, std::vector<ExprPtr> args,
                           std::size_t pos, std::size_t entries);
    std::string resolveNamespaceName(const std::string &written) const;
    // **An unnamed namespace is a named one that nothing outside can name.**
    bool inUnnamedNamespace_ = false;
    // **The body being read belongs to a static member.**
    bool inStaticMember_ = false;
    // A static member's definition is read in its class's scope, as a static
    // member function's body is - [class.static.data]/2; put back on the way out.
    struct StaticMemberScope {
        Parser *p; const Type *cls; bool wasStatic;
        StaticMemberScope(Parser *parser, const Type *owner)
            : p(parser), cls(parser->currentClass_), wasStatic(parser->inStaticMember_) {
            if (owner != nullptr) { p->currentClass_ = owner; p->inStaticMember_ = true; }
        }
        ~StaticMemberScope() { p->currentClass_ = cls; p->inStaticMember_ = wasStatic; }
    };
    // While a thread_local is declared: its construction, binding and destructor go to __tls_init.
    struct ThreadLocalObject {
        Parser *p; bool was;
        ThreadLocalObject(Parser *parser, bool tls) : p(parser), was(parser->tlsObject_) { p->tlsObject_ = tls; }
        ~ThreadLocalObject() { p->tlsObject_ = was; }
    };
    // Whether a declaration here has internal linkage: written `static`, or
    // inside an unnamed namespace, which says the same thing about a name.
    bool internalLinkage(StorageClass sc) const {
        return sc == StorageStatic || inUnnamedNamespace_;
    }
    std::string namespacePrefix() const {
        std::string out;
        for (std::size_t i = 0; i < namespaceStack_.size(); i++)
            out += namespaceStack_[i] + "::";
        return out;
    }
    typedef sema::TableSizes TableSizes;
    // The namespace an out-of-line definition was written for, while it is read; its defaults read there.
    std::vector<std::string> definitionNamespace_;
    // `int lib::f(int) {}` reaches an f declared in an inline namespace of lib; an unqualified one declares anew.
    bool definingQualified_ = false;
    static std::vector<std::string> namespacePathStack(const std::string &ns);
    // How many tokens from `from` spell a namespace qualifier, `lib :: v2 ::` being four; 0 for none.
    std::size_t namespaceQualifierTokens(std::size_t from) const;
    std::size_t qualifiedTypeEnd() const;
    // The type the chain qualifiedTypeEnd measured reaches, a member typedef included.
    const Type *qualifiedTypeAt(std::size_t end) const;

    std::map<std::string, std::size_t> memberInit_;
    // The members whose initialiser is written `= {...}`, copy-list rather than direct (M1 L4).
    std::set<std::string> memberInitCopy_;
    // While a member's braced initialiser is laid down by emitInit: the member, reached through `this`.
    struct MemberRoot { int thisSlot; const Type *cls; const Member *member; };
    const MemberRoot *memberRoot_ = nullptr;
    // **[class.union]/5, an anonymous union.** Its members' positions, recorded as its body is read,
    // for the refusals the class around it raises; the `$S<n>` count clang numbers its objects by.
    std::map<const Type *, std::vector<std::size_t> > anonUnionMemberPos_;
    int anonUnionSeq_ = 0;
    bool anonymousUnionAhead() const;
    void refuseAnonymousUnionShape(const Type *u, std::size_t pos);
    void declareAnonymousUnionObject(const Type *u, StorageClass sc, std::size_t pos, bool local);
    std::set<const Type *> unionsNamedBy(const Type *cls, const std::set<std::string> &names) const;
    // Whether this class wrote a member initialiser - [dcl.init.aggr]/1.
    bool hasMemberInitialiser(const std::string &tag) const;
    // [class.ctor]/5: default-initialising a class whose implicit default
    // constructor is deleted - a const or reference member with no initialiser.
    void refuseDeletedDefaultInit(const Type *t, const std::string &name,
                                  std::size_t pos);
    // To the ',' or ';' that ends one, counting brackets.
    void skipMemberInitialiser();
    // The statements a constructor of this class needs for the members it did
    // not initialise itself - [class.base.init]/9, where an initialiser the
    // class wrote stands in for a mem-initialiser nobody wrote.
    std::vector<StmtPtr> memberInitialisers(const std::string &tag,
                                            const Type *type, int thisSlot,
                                            const std::set<std::string> &already,
                                            std::size_t pos);
    // The same for one member: the store its own initialiser asks for, or
    // nullptr where the class wrote none.
    StmtPtr memberInitialiser(const std::string &tag, const Type *type,
                              const Member &m, int thisSlot, std::size_t pos);
    // The constructor call for a class-typed member - [class.base.init]/8 with `args`
    // empty, the mem-initialiser `: m(args)` otherwise. Answers nullptr for a member
    // whose type has no constructors, leaving `args` untouched for the scalar path.
    StmtPtr constructMember(const std::string &cls, const Type *type,
                            const Member &m, int thisSlot,
                            std::vector<ExprPtr> &args, std::size_t pos,
                            bool implicit);
    // [dcl.fct.default]/4: the defaults have to be a suffix of the parameter list. Asked
    // by both parameter-list parsers - declarations and definitions - because a
    // definition may carry the defaults where the declaration did not.
    void requireDefaultsAreASuffix(const std::vector<std::size_t> &defaults,
                                   std::size_t pos);
    // Fill in what the call left out, by re-reading each default expression. By value for
    // the same reason, and this is the site that does the parsing: re-reading tokens is
    // exactly what can grow `lookup_.functions_` under a caller still holding a reference.
    void applyDefaults(Signature f, std::vector<ExprPtr> &args,
                       std::size_t pos);
    void blockFunctionDeclaration(const Declared &d);

    ExprPtr finishCall(const std::string &name, const std::string &symbol,
                       ExprPtr callee, const Type *returns,
                       const std::vector<const Type *> &params, bool variadic,
                       std::size_t pos);

    // How deep inside 'extern "C"' the parser is - zero is C++ linkage, unless someone says otherwise -
    // and whether the declaration being read is the one written straight after `extern "C"`, no
    // braces, which [dcl.link]/7 reads as if it said `extern`.
    int cLinkage_ = 0;
    bool directLinkage_ = false;
    bool linkageSpecification();

    ExprPtr objectRef(const std::string &name);
    const Type *qualifiedMemberScope(const Type *obj, std::string &name,
                                     std::size_t pos);
    void refuseAmbiguousMember(const Type *cls, const Member &m, std::size_t pos);
    // The two halves of it, because class scope sits between them.
    ExprPtr localRef(const std::string &name);
    ExprPtr globalRef(const std::string &name);
    ExprPtr useReference(ExprPtr e);
    ExprPtr bindReference(const Type *ref, ExprPtr init, std::size_t pos,
                          const std::string &what);
    int refTemps_ = 0;

    struct InitStep {
        const Member *member;
        long long index;

        // C++11 has no aggregate with a member initialiser, so the braces call this.
        InitStep(const Member *member = nullptr, long long index = 0)
            : member(member), index(index) {}
    };
    bool isAggregateClass(const Type *t) const;
    bool bracedInitialiserAhead() const;
    bool constructedClass(const Type *t) const;
    bool needsConstruction(const Type *t) const;
    void initConstruct(const std::string &name, std::vector<InitStep> &path, const Type *type,
                       std::vector<ExprPtr> args, bool valueInit, std::size_t pos, std::vector<StmtPtr> &out);

    // **[dcl.init]/8: `T()` on a class with no user-provided constructor is
    // value-initialisation, which zeroes it.** `initZero` says the same in statements;
    // these two say it as an expression, rooted at a frame slot and not at a name.
    ExprPtr pathAccess(ExprPtr root, const std::vector<InitStep> &path);
    void zeroLeaves(const Expr &root, const Type *type,
                    std::vector<InitStep> &path, std::vector<ExprPtr> &out);
    ExprPtr zeroChain(const Expr &root, const Type *type);
    ExprPtr functionalCast(const Type *to, std::size_t pos);
    // `T{}` in an expression: the value-initialisation `T()` already gives.
    ExprPtr bracedValueInit(const Type *to, std::size_t pos);
    const Type *simpleTypeKeyword() const;

    struct InitCursor {
        std::vector<Init> *items;
        std::size_t at;

        // C++11 has no aggregate with a member initialiser, so the braces call this.
        InitCursor(std::vector<Init> *items = nullptr, std::size_t at = 0)
            : items(items), at(at) {}
        bool done() const { return items == nullptr || at >= items->size(); }
        Init &cur() const { return (*items)[at]; }
    };

    // **[dcl.spec.auto]/7: every declarator of one `auto` declaration deduces
    // the same type.**
    void checkOneDeducedType(const Type *&first, const Type *now,
                             const std::string &name, std::size_t pos);
    // What the last `auto` deduced *for itself* - `auto *p = &i` leaves `int`
    // here and returns `int *`. Written by deduceAutoFrom and read by the
    // declaration loops, which compare declarators of one declaration.
    const Type *lastDeducedAuto_ = nullptr;
    Init parseInitialiser();
    // A12/A13: whether a file-scope initialiser folds to bytes; one that does
    // not is stored by the init function instead, once the object is declared.
    bool staticallyInitialisable(const Type *t, Init &in);
    void dynamicInitialiseScalar(const std::string &name, const Type *type,
                                 Init &in);
    void dynamicInitialiseStaticMember(const std::string &name,
                                       const std::string &symbol, const Type *type,
                                       Init &in, bool once);
    // The weak guard a template's static member is built under, so a second
    // unit's copy does not build it again; wraps `body` and returns the test.
    StmtPtr guardTemplateMember(const std::string &symbol,
                                std::vector<StmtPtr> body);
    // The initialiser inside the parentheses of `int z(5);` - one expression, [dcl.init]/16.
    Init parenthesisedInitialiser(const Declared &d);
    // **Is the `(` this sits on an initialiser rather than a parameter list?**
    bool atParenInitialiser();
    // Whether braces initialise this declaration; refuses a pair with a value in it, `{}` being parseInitialiser's.
    bool atBracedInitialiser(const std::string &name);
    // Does the initialiser ahead name `name`? Only then is it registered first ([basic.scope.pdecl]/1).
    bool initialiserNames(const std::string &name) const;

    typedef sema::ConstexprFn ConstexprFn;

    typedef sema::ConstLeaf ConstLeaf;
    typedef sema::ConstObject ConstObject;
    typedef sema::ConstObjectPtr ConstObjectPtr;
    typedef sema::ConstexprCtor ConstexprCtor;

    typedef sema::ConstexprSlot ConstexprSlot;

    void foldLocalObject(const Declared &d, int off, const std::vector<StmtPtr> &inits, std::size_t from);
    ExprPtr targetFor(const std::string &name, const std::vector<InitStep> &path);

    void initStore(const std::string &name, std::vector<InitStep> &path,
                   ExprPtr value, std::size_t pos, std::vector<StmtPtr> &out);
    void initZero(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, std::size_t pos, std::vector<StmtPtr> &out);
    void emitString(const std::string &name, std::vector<InitStep> &path,
                    const Type *type, const StrLit *s, std::size_t pos,
                    std::vector<StmtPtr> &out);
    void emitFill(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, InitCursor &c, std::vector<StmtPtr> &out);
    void emitAggregate(const std::string &name, std::vector<InitStep> &path,
                       const Type *type, InitCursor &c, std::size_t pos,
                       std::vector<StmtPtr> &out);
    // **List-initialisation of a class with an initializer_list constructor.**
    const Signature *initializerListConstructor(const Type *cls,
                                                const Type **ilType);
    ExprPtr buildInitializerList(const Type *ilType, Init &in, std::size_t pos,
                                 std::vector<StmtPtr> &into, bool flush = true);
    int ilTemps_ = 0;

    void emitInit(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, Init &in, std::vector<StmtPtr> &out);
    // [dcl.init.list]/7: a value inside braces may not narrow. Asked of every
    // scalar a braced list reaches, for a local and at file scope alike.
    void checkNarrowing(const Type *to, const Expr &value, std::size_t pos,
                        const std::string &what);

    void flattenScalar(const Type *type, Init &in, int base,
                       std::vector<GlobalPiece> &out);
    void flattenFill(const Type *type, InitCursor &c, int base,
                     std::vector<GlobalPiece> &out);
    void flattenAggregate(const Type *type, InitCursor &c, int base,
                          std::vector<GlobalPiece> &out);
    void flattenInit(const Type *type, Init &in, int base,
                     std::vector<GlobalPiece> &out);

    void skipInit(const Type *type, InitCursor &c);
    long long inferredLength(const Init &in, const Type *element, std::size_t pos);
    static const StrLit *stringInitialiser(const Init &in, const Type *type);
    // [dcl.init.string]/2: the array holds the string and its '\0', where C drops the '\0'.
    void requireStringFits(const Type *array, const StrLit *s, std::size_t pos);

    void topLevel(Program &program);
    // A `{...}`, or with `functionTry` a function's whole body that is one `try` statement, [dcl.fct.def.general].
    StmtPtr block(bool functionTry = false);
    StmtPtr statement();
    StmtPtr statementBody();
    StmtPtr declarationBody();
    StmtPtr forStatement();
    StmtPtr switchStatement();
    StmtPtr caseLabel();
    StmtPtr gotoLabel();
    StmtPtr declaration();
    // **A declaration in a condition** - [stmt.select]/2 and [stmt.iter]/2.
    bool conditionDecl_ = false;
    std::string conditionName_;
    bool atConditionDeclaration() const;
    ExprPtr ifConditionDeclaration(std::vector<StmtPtr> &setup);
    ExprPtr whileConditionDeclaration();
    void resolveGotos();

    bool staticAssertion();
    // `V<N>` naming the class whose body is being read: steps to the `(` after it when it does.
    bool atOwnTemplateId(const std::string &tag, const std::string &local);
    bool ownArgsThenParen() const;
    const Type *declaringClassOf(const Type *cls, const std::string &name) const;
    // `using X = T;` is an alias declaration and not a using-declaration: true if one was read.
    bool aliasDeclaration(const std::string &prefix);
    // ---- WS-C2 (review 2026-10-08): attributes read and ignored, [dcl.attr.grammar]/5 ----
    bool skipAttributes();
    void skipBalanced();
    bool attributedDeclarationAhead();
    std::size_t lastAttributeEnd_ = static_cast<std::size_t>(-1);
    // `extern template ...;` is read and suppresses nothing: true if one was read.
    bool externTemplateDeclaration();
    bool exceptionSpecification();
    const Type *adjustedSpecType(const Type *t, std::size_t pos);
    // ---- WS-C1 (review 2026-10-08): the virt-specifier-seq and `= default` / `= delete`, read behind the exception specification ----
    VirtSpecifiers pendingVirt_;
    void readVirtSpecifiers();
    VirtSpecifiers takeVirtSpecifiers() { VirtSpecifiers v = pendingVirt_; pendingVirt_ = VirtSpecifiers(); return v; }
    void refuseVirtSpecifiers(const char *what);
    // `= default` or `= delete` after the declarator - [dcl.fct.def.default], [dcl.fct.def.delete] - for the declare* that follows.
    enum class SpecialDef { None, Defaulted, Deleted };
    SpecialDef pendingSpecial_ = SpecialDef::None;
    std::size_t pendingSpecialAt_ = 0;
    void readDefaultedOrDeleted();
    SpecialDef takeSpecialDef() { SpecialDef s = pendingSpecial_; pendingSpecial_ = SpecialDef::None; return s; }
    // A special member written `= default` is the implicit one, with the access it was written under.
    struct DefaultedMember { bool is; Access access; DefaultedMember() : is(false), access(Access::Public) {} };
    struct Defaulted { DefaultedMember defaultCtor, copyCtor, moveCtor, copyAssign, moveAssign, dtor; };
    std::map<std::string, Defaulted> defaulted_;
    void defaultedMember(DefaultedMember &m, Access access, std::size_t pos, const std::string &what);
    void applyDefaultedAccess(const std::string &tag, const Type *type);
    void refuseDeleted(const Signature &f, std::size_t pos) const;
    Defaulted defaultedFor(const std::string &tag) const;
    static bool deletedSpecial(const Signature *f) { return f != nullptr && f->deleted; }
    static bool nonPublicDefault(const DefaultedMember &m) { return m.is && m.access != Access::Public; }
    void refuseConstexprConstructorBody(std::size_t parenAt) const;
    void refuseScopedConversion(const Type *from, const Type *to, std::size_t pos) const;
    void refuseScopedOperand(const Type *a, const Type *b, const char *op, std::size_t pos) const;
    // `E::a`, `n::E::a`, `C::E::a` - an enumerator named through its enumeration, [dcl.enum]/11.
    ExprPtr enumeratorThroughEnum();
    bool atSwitchCondition() const;
    // The classes befriended by each class, by tag - [class.friend]/2.
    std::map<std::string, std::vector<std::string> > friendClasses_;
    void befriendClass(const std::string &owner, bool keyed);
    bool isFriendClassOf(const Type *cls) const;
    // ---- end WS-C1 ----
    // Set by exceptionSpecification() at each place a parameter list can be closed, read and cleared by whichever declare* call follows.
    bool pendingNoexcept_ = false;
    // The other half of what exceptionSpecification() read: `throw(T...)` with a type in it.
    DynamicSpec pendingSpec_;
    // **Is the function being parsed declared `noexcept`?** [except.spec]/9.
    bool inNoexceptFunction_ = false;
    // Set when `explicit operator T()` is seen, consumed by the next declareFunction of a conversion name, which stamps the signature.
    bool pendingExplicitConversion_ = false;
    // How many potentially-throwing things the expression being parsed has
    // reached. `noexcept(e)` reads it; nothing else does.
    int mayThrow_ = 0;
    long long constantExpression(const char *what);

    void typedefFunctionSuffix(Declared &td);

    bool foldAddress(const Expr &e, std::string *sym, long long *off) const;
    bool addressConstant(const Type *t, const Init &in);
    const Type *constexprObject(const Type *t);
    // A namespace-scope constexpr pointer's address constant, by its key, for one defined from it.
    std::map<std::string, std::pair<std::string, long long> > constAddresses_;
    bool addressOfObject(const Expr &e, std::string *sym, long long *off) const;

    ExprPtr expr();
    ExprPtr assign();
    ExprPtr conditional();
    ExprPtr conditionalNode();
    ExprPtr bitOr();
    ExprPtr bitXor();
    ExprPtr bitAnd();
    ExprPtr compound(BinOp op, ExprPtr target, ExprPtr value, std::size_t pos);
    ExprPtr incDec(ExprPtr target, bool increment, bool prefix, std::size_t pos);
    ExprPtr clonePure(const Expr &e);
    ExprPtr cloneLvalue(const Expr &e, std::size_t pos);
    ExprPtr shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr relational();
    ExprPtr shift();
    ExprPtr add();
    ExprPtr mul();
    ExprPtr castExpr();
    // At a `(`: does a type-id run from here to a `)`? [dcl.ambig.res]/2 - `(S::m)` naming a data member does not.
    bool parenHoldsTypeId();
    // `a .* p` and `p ->* q` - [expr.mptr.oper], which sits between a cast and
    // a multiplication. What it builds is the object's address plus the
    // offset the member pointer holds, read as the member's type.
    ExprPtr memberPointerExpr();
    ExprPtr applyMemberPointer(ExprPtr addr, ExprPtr mp, std::size_t pos,
                               bool constObject);
    // `&S::f` - the ABI's pair, built into a slot of this frame.
    // `vtableCode` is Itanium's 1 + slot offset for a virtual function, 0 for
    // a plain one; `code` names the function - or cl's vcall thunk - to hold.
    ExprPtr boundMemberPointer(const Type *cls, const Signature &f,
                               std::size_t pos, long long vtableCode = 0,
                               const std::string &code = std::string());
    // The null pointer to a member function, the same pair with a null code word (1.27).
    ExprPtr nullMemberFunctionPointer(const Type *mp);
    // `throw e` as an expression - an arm of `?:`, the operand of `noexcept` - of type void, and the
    // statements a `throw` lowers to, which are all expressions, as one comma chain (1.27).
    ExprPtr throwExpression();
    ExprPtr statementsAsExpression(StmtPtr s, std::size_t pos);
    // How many throw-expressions have been read, which tells a `?:` that an arm is one.
    int throwsSeen_ = 0;
    // Whether the function template signature readTemplateDeclaration last read says `noexcept` (1.27).
    bool patternNoexcept_ = false;
    // **`o.*p` for a member *function* pointer has to carry two things to the call** -
    // the object's address and the code pointer - and no expression holds a pair. The
    // address is left here and the `(` in `postfix` picks it up. One token wide.
    ExprPtr boundThis_;
    const Type *boundFn_ = nullptr;
    std::size_t boundAt_ = 0;
    ExprPtr staticCast(std::size_t pos);
    ExprPtr constCast(std::size_t pos);
    ExprPtr dynamicCast(std::size_t pos);
    ExprPtr dynamicCastToVoid(ExprPtr v, const Type *to);
    // A function named but not called, as a pointer to it (null for none), bare or qualified.
    ExprPtr functionAsValue(const std::string &key, std::size_t pos);
    // [over.over]: what an overloaded function's name converts to while its operand is read.
    const Type *overloadTarget_ = nullptr;
    // Set while a call argument that is a function's name and nothing else is read: its set waits.
    bool overloadArgument_ = false;
    // Held over an initialiser, an assignment's right side, a cast's operand and a return's value.
    struct OverloadTarget {
        Parser &p; const Type *was; bool wasArgument;
        OverloadTarget(Parser &parser, const Type *t, bool argument = false)
            : p(parser), was(parser.overloadTarget_), wasArgument(parser.overloadArgument_) {
            p.overloadTarget_ = t; p.overloadArgument_ = argument;
        }
        ~OverloadTarget() { p.overloadTarget_ = was; p.overloadArgument_ = wasArgument; }
    };
    // The one function of the set whose type `target` names - of `cls` for a member - or null.
    const Signature *pickByTarget(const std::vector<std::size_t> &set, const Type *cls,
                                  const Type *target) const;
    // A function's address as a value, and a member function's as the ABI's pair.
    ExprPtr functionAddress(const Signature &sig, const std::string &key);
    ExprPtr memberFunctionAddress(const Type *cls, const Signature &f, const std::string &key,
                                  std::size_t pos);
    // `f`, `&f`, `n::f` or `&S::f` alone up to the argument's `,` or `)`, the next token first.
    bool atBareFunctionArgument() const;
    // The class scope's member functions of `name` as a value - its static ones - or null where it has none.
    ExprPtr memberFunctionByName(const std::string &name, std::size_t pos);
    // [over.over]/1's parameter target: an overloaded name as an argument waits behind a
    // placeholder, as a braced list does, until the parameter it initialises is known.
    struct OverloadArg {
        const Expr *placeholder; long long serial;
        std::vector<std::size_t> set; std::string key; const Type *cls; std::size_t pos;
    };
    std::vector<OverloadArg> overloadArgs_;
    long long overloadSerial_ = 0;
    ExprPtr overloadPlaceholder(const std::vector<std::size_t> &set, const std::string &key,
                                const Type *cls, std::size_t pos);
    const OverloadArg *overloadArgOf(const Expr &e) const;
    // The function of the set `param` takes, as a value - null where none or several are of its type.
    ExprPtr overloadArgumentFor(const Type *param, const OverloadArg &oa);
    // Before `key`'s set is ranked: each candidate's parameter picks, and they must pick one function.
    void pickOverloadArguments(const std::string &key, std::vector<ExprPtr> &args,
                               const Type *object);
    // A placeholder that reached a call no parameter picked it on, refused by name.
    void refuseOverloadArguments(const std::vector<ExprPtr> &args, const std::string &callee) const;
    ExprPtr reinterpretCast(std::size_t pos);
    ExprPtr unary();
    ExprPtr unaryNode();
    ExprPtr postfix();
    ExprPtr primary(Program *program);
    ExprPtr primaryNode(Program *program);

    ExprPtr arithmetic(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    ExprPtr comparison(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    // Null unless an operand has class type, in which case the call it built.
    // Takes its operands by reference because it leaves them alone when it
    // answers null, and the built-in path below still needs them.
    ExprPtr overloadedBinary(BinOp op, ExprPtr &lhs, ExprPtr &rhs,
                             std::size_t pos);

    typedef sema::OperatorChoice OperatorChoice;
    // `-v`, `!v`, `*p`, `++v`. Null when the operand is not a class or the class has no
    // such operator, and the built-in path below is then reached unchanged - which keeps
    // `&obj` the ordinary address-of for a class that declares no `operator&`.
    ExprPtr overloadedUnary(const char *spelling, ExprPtr &operand,
                            std::size_t pos);
    const Type *ptrdiffType();
    ExprPtr pointerAdd(ExprPtr p, ExprPtr n);
    ExprPtr pointerSub(ExprPtr l, ExprPtr r, std::size_t pos);

    // ----- WS-E2: initialisation, expressions, casts (review 2026-10-08)

    // `base`'s offset in `derived` by an accessible path, -1 if none; *viaVirtual if it crosses a virtual base.
    int downcastOffset(const Type *derived, const Type *base, bool *viaVirtual) const;
    // `(D *)p` for a `B *p`: the pointer moved back by `off` bytes, a null kept null.
    ExprPtr downcastPointer(ExprPtr p, const Type *to, int off);
    // [over.match.list]'s second phase: the braces' elements as a constructor's arguments,
    // `args` filled from `in`; a copy-list-init refuses an `explicit` one; no element narrows.
    Signature listConstructor(const Type *plain, Init &in, std::vector<ExprPtr> &args,
                              bool copyList, std::size_t pos);
    // `T{...}` as an expression, [expr.type.conv]/2 - the '{' still ahead.
    ExprPtr listTemporary(const Type *to, std::size_t pos);
    // A list-initialised temporary of `to` from an already-read list, as an xvalue.
    ExprPtr listTemporaryFrom(const Type *to, Init &in, bool copyList, std::size_t pos);
    std::vector<ExprPtr> bracedInitArguments(const Type *to, Init &in, std::size_t pos);
    // An aggregate built in a hidden local of this frame from its braced list.
    ExprPtr aggregateTemporary(const Type *plain, Init &in, std::size_t pos);
    // A scalar from its one braced element, [dcl.init.list]/3 - narrowing refused.
    ExprPtr scalarFromList(const Type *to, Init &in, std::size_t pos);
    // A braced list over `ilType`'s element, its backing array's setup folded in front.
    ExprPtr initializerListExpr(const Type *ilType, Init &in, std::size_t pos);
    // A braced call argument is parsed as an Init behind a placeholder and built once
    // the parameter is known - here, before the overload set is ranked.
    struct BracedArg { const Expr *placeholder; Init init; };
    std::vector<BracedArg> bracedArgs_;
    ExprPtr bracedPlaceholder(Init in);
    bool isBracedPlaceholder(const Expr &e) const;
    void buildBracedArguments(const std::string &key, std::vector<ExprPtr> &args,
                              std::size_t pos, const Type *object = nullptr);
    void buildBracedArguments(const std::vector<const Type *> &params,
                              std::vector<ExprPtr> &args, std::size_t pos);
    ExprPtr bracedArgumentFor(const Type *param, Init &in, std::size_t pos);
    // Whether a braced list could initialise a parameter of this type at all ([over.ics.list]),
    // which tells the overloads a braced argument could go to from those it cannot (1.25).
    bool bracedListViable(const Type *param, const Init &in);
    const Init *bracedInitOf(const Expr &e) const;
    Signature resolveOverloadBraced(const std::string &key, std::vector<ExprPtr> &args,
                                    std::size_t pos, const Type *object = nullptr,
                                    bool objectLvalue = true);
    bool onlyImplicitConstructors(const std::string &tag) const;
    int listTemps_ = 0;
    // `std::initializer_list<E>` itself, which a braced list makes rather than calls.
    static bool isInitializerListType(const Type *t);
    // `std::initializer_list<elem>`, instantiated through tokens naming a hidden typedef.
    const Type *initializerListOf(const Type *elem, std::size_t pos);
    int ilElemSeq_ = 0;
    // `auto x = {...}`, the '{' ahead: the list's one element type, read and rewound.
    const Type *deduceBracedAuto(const std::string &name, std::size_t pos);
    // [expr.prim.lambda]/6: a captureless closure's `__invoke` and its conversion to a pointer to it.
    void captureFreeConversion(const std::string &tag, const std::string &local, const Type *returns,
                               const std::vector<const Type *> &params, std::size_t pos,
                               std::vector<PendingBody> &bodies);
    int lambdaConvSeq_ = 0;
    // `dynamic_cast` after its operand is read: a pointer target, and a reference one through it.
    ExprPtr dynamicCastFrom(ExprPtr v, const Type *to, std::size_t pos);
    ExprPtr dynamicCastReference(ExprPtr v, const Type *to, std::size_t pos);

    Program *current_ = nullptr;
};
