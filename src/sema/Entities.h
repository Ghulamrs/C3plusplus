#pragma once

// **The symbol tables' entries, out of the parser.** M2 step 1 (doc-cxx3/M2-PLAN.md): Sema's interfaces
// return these, and a type nested in Parser cannot be named before Parser is complete. The parser keeps
// a typedef for each, so its code names them as it always did.

#include "../Type.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace sema {

struct Local {
    std::string name;
    int offset;
    const Type *type;
    bool isConst;
    std::string staticName;
    bool isRegister = false;
    // [expr.const]: a const object of integral type initialised with a constant
    // expression *is* one, so its value has to be kept where fold() can find it.
    // The object still has an address; this is what it is worth when read.
    bool isConstantValue = false;
    long long constantValue = 0;
    // **And the same for a floating one.**
    bool isConstantDouble = false;
    long double constantDouble = 0;
    // A const local read in a lambda for its value only: no odr-use, no capture ([expr.prim.lambda]/12, 1.27).
    bool constantOnly = false;
    // **[class.copy]/31 elides a `return` copy, and excludes a parameter.**
    bool isParameter = false;
    // A by-value class parameter that arrived by address is lowered to a reference, so by its slot alone it looks like `T &t`.
    bool byValueByAddress = false;
    // **[stmt.dcl]/3: a jump may not enter this object's scope** - set for an automatic object with an initialiser, a constructor or a destructor, the three things a jump landing past its declaration would skip.
    bool guardsJump = false;
    // A member of a local anonymous union: its slot is the union's, given back once, by the union.
    bool sharesSlot = false;
    // Which object this is: two locals of one function may share a frame slot once the first's scope has closed.
    int serial = 0;
    // Its position among a trailing return type's parameters, unnamed ones counted - `fp_`, `fp0_`.
    int trailingParam = -1;
    // A static or extern thread_local in a block, and whether a use goes through its wrapper.
    bool isThreadLocal = false;
    bool tlsViaWrapper = false;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    Local(std::string name = std::string(), int offset = 0, const Type *type = nullptr,
          bool isConst = false, std::string staticName = std::string())
        : name(std::move(name)), offset(offset), type(type), isConst(isConst),
          staticName(std::move(staticName)) {}
};

struct GlobalSym {
    std::string name;
    std::string symbol;
    const Type *type;
    bool isConst;
    bool emitted;
    bool hasInit;
    bool isConstantValue;   // as for Local, above
    long long constantValue;
    bool isConstantDouble = false;  // and the floating one, as for Local
    long double constantDouble = 0;
    // **thread_local** - [basic.stc.thread]: whether a use reaches it through the ABI's wrapper
    // or guard (declared elsewhere, or built by __tls_init), and whether it has external linkage.
    bool isThreadLocal = false;
    bool tlsViaWrapper = false;
    bool tlsExternal = false;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    GlobalSym(std::string name = std::string(), std::string symbol = std::string(),
              const Type *type = nullptr, bool isConst = false, bool emitted = false,
              bool hasInit = false, bool isConstantValue = false, long long constantValue = 0)
        : name(std::move(name)), symbol(std::move(symbol)), type(type), isConst(isConst),
          emitted(emitted), hasInit(hasInit), isConstantValue(isConstantValue),
          constantValue(constantValue) {}
};

// **`throw(T...)` as written**, adjusted by [except.spec]/2; `throw()` is noexcept and not one of these.
struct DynamicSpec {
    bool written = false;
    std::vector<const Type *> types;
    std::size_t pos = 0;
    bool sameSet(const DynamicSpec &o) const;
};

struct Signature {
    std::string name;
    std::string symbol;
    const Type *returns;
    std::vector<const Type *> params;
    bool variadic;
    bool defined;
    std::size_t pos;
    // A name with C linkage, and `main`, carry one symbol and so can hold one function.
    bool cLinkage;
    // The class this belongs to, empty for a free function. A member is
    // keyed in the table as "Point::get", so overload resolution works on
    // members with no second implementation of it.
    std::string owner;
    bool constThis;
    Access access;
    bool isVirtual;
    // **`explicit` on a constructor**, which changes nothing about the function and only about who may pick it.
    bool isExplicit = false;
    // **A static member function is a member in every way but the one that matters at a call**.
    bool isStaticMember = false;
    // **`noexcept` on this function**, which in C++11 is not part of its type.
    bool isNoexcept = false;
    DynamicSpec spec = DynamicSpec();
    // **Nobody wrote this one.** An implicitly declared special member is put in the
    // table so overload resolution finds it, and given a body only if something
    // calls it - which is what keeps the symbol list level with the oracles.
    bool implicit = false;
    bool used = false;
    // A ranked-only member template's body error, reported if it is chosen ([temp.inst]/3).
    std::string bodyFailure;
    // What this candidate's body used, marked only once it is chosen (M1, std::function's converting candidates).
    std::vector<std::size_t> deferredUses;
    std::size_t bodyFailurePos = 0;
    // **A specialization is a candidate like any other, and loses a tie.**
    // [over.match.best]: where a specialization and an ordinary function are equally
    // good, the ordinary one wins. Its unbound parameter list serves [temp.func.order].
    bool fromTemplate = false;
    const Type *pattern = nullptr;
    // Defined inside its class, so [dcl.inline]/6 makes it inline and never the key function.
    bool inlineBody = false;
    // `= delete` - [dcl.fct.def.delete]: ranked like any other candidate, and refused where chosen.
    bool deleted = false;
    // **The ref-qualifier, half of a member's identity beside its const** - [over.load]/2.
    RefQual refThis = RefQual::None;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    Signature(std::string name = std::string(), std::string symbol = std::string(),
              const Type *returns = nullptr,
              std::vector<const Type *> params = std::vector<const Type *>(),
              bool variadic = false, bool defined = false, std::size_t pos = 0,
              bool cLinkage = false, std::string owner = std::string(),
              bool constThis = false, Access access = Access::Public, bool isVirtual = false)
        : name(std::move(name)), symbol(std::move(symbol)), returns(returns),
          params(std::move(params)), variadic(variadic), defined(defined), pos(pos),
          cLinkage(cLinkage), owner(std::move(owner)), constThis(constThis), access(access),
          isVirtual(isVirtual) {}
};

struct TypedefName {
    std::string name;
    const Type *type;
};

struct EnumConst {
    std::string name;
    long long value;
    // The enumeration's type when an enum-base chose one, else null: int.
    const Type *type;
    // A bound non-type template parameter's declared type, which only a signature's expression spells.
    const Type *paramType = nullptr;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    EnumConst(std::string name = std::string(), long long value = 0,
              const Type *type = nullptr)
        : name(std::move(name)), value(value), type(type) {}
};

// The four tables' sizes before a declaration, so the entries it added can be aliased.
struct TableSizes { std::size_t functions, globals, typedefs, enums; };

// How well one argument matches one parameter, in [over.ics.scs]'s order - the values are compared, so do
// not reorder this enum. The three UserDefined values are one rank with its second standard conversion behind
// it, [over.ics.rank]/3.3, told apart only through the same conversion function (1.27).
enum class Rank { Identity, Qualification, Promotion, Conversion,
                  UserDefined, UserDefinedPromotion, UserDefinedConversion,
                  Ellipsis, None };

// Which side of [over.match.oper] won.
enum class OperatorChoice { None, Member, NonMember };

// **A pack's members as bound**: the types, and for a non-type pack the values beside them,
// the types then being the element type once per value. One per template parameter.
struct PackMembers : std::vector<const Type *> {
    std::vector<long long> values;
    PackMembers() {}
    PackMembers(const std::vector<const Type *> &t) : std::vector<const Type *>(t) {}
};
typedef std::vector<PackMembers> Packs;
// One template parameter as written: `class T` / `typename T`, or a non-type one
// such as `int N`. Nothing is bound to either yet.
struct TemplateParam {
    std::string name;
    const Type *type = nullptr;   // null for a type parameter
    // `T v` after `class T`: the index of T, and `type` is T standing for itself. -1 otherwise.
    int typeOf = -1;
    std::size_t pos = 0;
    // Where its name is, or would be: what clang points at for a template template argument of the wrong kind.
    std::size_t namePos = 0;
    // `class...`
    bool isPack = false;
    // **`template <class> class C`** - [temp.param]/1: a parameter standing for a template, with
    // its own list in `inner`, whose names nothing can refer to. `type` is null, as a type parameter's.
    bool isTemplate = false;
    std::vector<TemplateParam> inner;
    // A default argument, kept as a half-open token range [begin, end) the way a default
    // *function* argument is - instantiation replays tokens, so a default is replayed too, with
    // the earlier parameters bound. begin == 0 means there is none.
    std::size_t defBegin = 0;
    std::size_t defEnd = 0;
};

// What is known about a name that names a template. `start` is the
// `template` keyword: instantiation here will be a replay of these
// tokens, so where they begin is the thing worth keeping.
struct TemplateDecl {
    std::string name;
    // The namespace it was declared in, `std::` included. Carried to the
    // specialization for the manglers alone - see Type::templateNamespace.
    std::string ns;
    std::vector<TemplateParam> params;
    bool isClass = false;
    // Written `class` rather than `struct`: a specialization made while the
    // template is only declared has to describe itself the way it was declared.
    bool classKey = false;
    bool defined = false;
    // `template <class T> using Ptr = T *;` - kept with the class templates, its type-id at aliasAt.
    bool isAlias = false;
    std::size_t aliasAt = 0;
    // **A pattern's stand-in for template template parameter `ttParam`**, entered under the
    // parameter's name while a pattern is read; -1 for every template a program declares.
    int ttParam = -1;
    std::size_t start = 0;
    // The token after the `>` that closed the parameter list, which is
    // where the declaration proper begins. Instantiating is re-reading
    // from here with the arguments bound.
    std::size_t afterParams = 0;
    std::size_t pos = 0;
    // **A member of this class template defined outside it** - `template <class T>
    // T Box<T>::get(int)`. Kept on the class rather than as a template of its own,
    // because that is what it is. Replayed when the class is instantiated.
    struct OutOfLine {
        std::size_t start = 0;
        std::string member;    // "get", or the class's name for a ctor
        bool destructor = false;
        // A constructor, replayed like a held inline one from `start`, the `Box` after `Box<T>::`.
        bool constructor = false;
        // **A static data member, not a member function.**
        bool isData = false;
        // A member of a nested class, `Tree<T>::It::Deep::count`: "It::Deep", under the specialization.
        std::string nested;
    };
    std::vector<OutOfLine> outOfLine;

    // **A partial specialization: a second body for the argument lists that match a
    // pattern.** Its arguments are patterns rather than types, which is why they are
    // kept as written and matched at every use rather than resolved once.
    struct Partial {
        std::vector<TemplateParam> params;
        // One written argument. A type argument is a pattern; a non-type
        // one is either a value or one of this specialization's own
        // parameters, which is the only shape of it that deduces.
        struct Arg {
            bool isType = true;
            const Type *type = nullptr;
            bool isParam = false;
            std::size_t param = 0;
            long long value = 0;
            // `R...` written as the last argument of the pattern, where R is this
            // specialization's own pack. It takes every argument the fixed ones in
            // front did not, which lets `L<T, R...>` peel one type off any length.
            bool isPackExpansion = false;
        };
        std::vector<Arg> args;
        std::size_t bodyAt = 0;      // the '{' of its class body
        std::size_t pos = 0;
    };
    std::vector<Partial> partials;

    // ---- A member function template ----
    // `template <int M> CVector<M> head() const { ... }` written inside a
    // class.
    bool isMember = false;
    Access memberAccess = Access::Public;
    std::string ownerTag;
    const Type *ownerType = nullptr;
    std::vector<TemplateParam> classParams;
    std::vector<const Type *> classBinding;
    std::vector<long long> classValues;
    Packs classPacks;           // the class's packs, which the member template's body may expand (L16)
    // **A constructor template**: named by its class's own name, keyed under constructorKey(ownerTag).
    bool isConstructor = false;
    bool isExplicit = false;    // `explicit` on a constructor template, read in the class and replayed past
    // The class's name as the source spells it - `CMatrix` for `CMatrix<3,3>` - which inlineOwnerName_ wants.
    std::string ownerLocal;
};

// **A pack while a specialization is being read**: the types it was given and, once its function
// parameters have been made, the names they were given - `rest` becomes `rest$0` and `rest$1`, and
// `rest...` in a call is those two names. A non-type pack holds its values beside, of `valueType`.
struct PackBinding {
    std::vector<const Type *> types;
    std::vector<std::string> names;
    bool isValue = false;
    const Type *valueType = nullptr;
    std::vector<long long> values;
    bool standsForItself() const {
        return types.size() == 1 && types[0]->kind() == Kind::TemplateParam;
    }
};
// What a binding does to the two name tables, and how to put them back.
struct Shadow {
    std::string name;
    bool isType = true;
    bool isPack = false;
    bool had = false;
    std::size_t was = 0;
    PackBinding hadPack;
    // A non-type parameter bound as a pattern reference rather than a value
    // - `N` in `V<N>` while a signature is read for deduction.
    bool isParamRef = false;
    bool hadParamRef = false;
    int paramRefWas = 0;
    // A template template parameter is an entry in `templates_` under its name; this is what it hid.
    bool isTemplate = false;
    bool hadTemplate = false;
    TemplateDecl hadDecl;
};

}   // namespace sema
