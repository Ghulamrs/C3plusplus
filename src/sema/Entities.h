#pragma once

// **The symbol tables' entries, out of the parser.** M2 step 1 (doc-cxx3/M2-PLAN.md): Sema's interfaces
// return these, and a type nested in Parser cannot be named before Parser is complete. The parser keeps
// a typedef for each, so its code names them as it always did.

#include "../Type.h"

#include <cstddef>
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

}   // namespace sema
