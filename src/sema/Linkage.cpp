// sema::Linkage - see Linkage.h.

#include "Linkage.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"
#include "../Source.h"
#include "../Mangle.h"
#include "../Operator.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace sema {

// The name the linker is given: `main` keeps its own, extern "C" keeps a name with external
// linkage as written ([dcl.link]/4 - an internal one is a C++ name there too, `_ZL2sfi`), and
// everything else is mangled in the target's ABI.
std::string Linkage::functionSymbol(const std::string &name, const Type *returns,
                                   const std::vector<const Type *> &params,
                                   bool variadic, bool internal, std::size_t pos) {
    if ((p_.cLinkage_ > 0 && !internal) || name == "main") return name;
    const Type *fn = p_.types_.functionType(returns, params, variadic);
    std::string out, why;
    bool ok = p_.target_.microsoftNames()
            ? microsoftFunctionName(name, manglingType(fn), internal, &out, &why)
            : itaniumFunctionName(name, fn, internal, &out, &why);
    if (!ok)
        p_.src_.fail(pos, "'" + name + "' cannot be given a name the linker can "
                       "hold: " + why);
    return out;
}

std::string Linkage::deletingDestructorSymbol(const std::string &cls) {
    return p_.target_.microsoftNames()
         ? microsoftDeletingDestructorName(cls, p_.lookup_.findTypedef(cls))
         : itaniumDeletingDestructorName(cls, p_.lookup_.findTypedef(cls));
}

std::string Linkage::baseConstructorSymbol(const Signature &ctor, const Type *base) {
    if (p_.target_.microsoftNames()) return ctor.symbol;
    // A constructor template's C2 was spelled with its C1, from the pattern.
    const std::map<std::string, std::string>::const_iterator tc2 = p_.constructorC2_.find(ctor.symbol);
    if (tc2 != p_.constructorC2_.end()) return tc2->second;
    const Type *fnType = p_.types_.functionType(p_.types_.get(Kind::Void), ctor.params,
                                             false);
    std::string sub, why;
    // An inherited constructor's base-subobject form is its CI2.
    const std::map<std::string, const Type *>::const_iterator inh = p_.inheritedFrom_.find(ctor.symbol);
    if (inh != p_.inheritedFrom_.end())
        return itaniumInheritingConstructorName(base->tag(), base, inh->second, fnType, false, &sub, &why)
               ? sub : ctor.symbol;
    if (itaniumConstructorName(base->tag(), base, fnType, false, &sub, &why))
        return sub;
    return ctor.symbol;
}

std::string Linkage::staticMemberSymbol(const std::string &cls,
                                       const std::string &name, const Type *t,
                                       Access access, std::size_t pos) {
    if (!p_.target_.microsoftNames()) return itaniumStaticMemberName(cls, p_.lookup_.findTypedef(cls), name);
    // Microsoft writes the access as a digit where a member function writes a letter,
    // so a static member that changes from private to public changes its symbol on
    // Windows and keeps it on Linux - the same asymmetry, measured the same way.
    const char code = access == Access::Public    ? '2'
                    : access == Access::Protected ? '1'
                                                  : '0';
    std::string out, why;
    if (!microsoftStaticMemberName(cls, p_.lookup_.findTypedef(cls), name, t, code, &out, &why))
        p_.src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// A member function's linkage name. Never plain, and never affected by
// `extern "C"`: a member cannot have C linkage, so the two ABIs are the only
// choice here.
std::string Linkage::memberSymbol(const std::string &cls, const std::string &name,
                                 const Type *fn, Access access, bool constThis,
                                 RefQual refThis, std::size_t pos, bool isVirtual,
                                 bool isStatic) {
    // Q public, I protected, A private - the Microsoft ABI puts the access in
    // the name and Itanium does not, both measured.
    const char code = isStatic && access == Access::Public    ? 'S'
                    : isStatic && access == Access::Protected ? 'K'
                    : isStatic                                ? 'C'
                    : isVirtual        ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    // A class defined inside a function body.
    const std::string *owner = p_.lookup_.localOwnerOf(cls);

    std::string out, why;
    const Type *shown = p_.target_.microsoftNames() ? manglingType(fn) : fn;
    bool ok = p_.target_.microsoftNames()
            ? (owner != nullptr
                   ? microsoftLocalMemberName(*owner, cls, p_.lookup_.findTypedef(cls), name,
                                              shown, code, constThis, refThis, &out, &why)
                   : microsoftMemberName(cls, p_.lookup_.findTypedef(cls), name, shown, code,
                                         constThis, refThis, &out, &why))
            : (owner != nullptr
                   ? itaniumLocalMemberName(*owner, cls, p_.lookup_.findTypedef(cls), name,
                                            fn, constThis, refThis, &out, &why)
                   : itaniumMemberName(cls, p_.lookup_.findTypedef(cls), name, fn, constThis,
                                       refThis, &out, &why));
    if (!ok)
        p_.src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// A variable at namespace scope is mangled by the Microsoft ABI and left
// alone by Itanium. A static one is nobody else's business either way, so it
// keeps the name it was written with.
std::string Linkage::dataSymbol(const std::string &name, const Type *type,
                               bool isStatic, std::size_t pos) {
    // [dcl.link]/4: extern "C" names a variable with external linkage only; an internal one
    // is spelled as any internal C++ name is - `_ZL2sv`, or as written on Microsoft.
    if (p_.cLinkage_ > 0 && !isStatic) return name;
    if (!p_.target_.microsoftNames()) return itaniumDataName(name, isStatic);
    // Microsoft mangles a variable only where something outside could name it.
    // An internal one keeps what it was written with - measured against clang,
    // which spells it the same way.
    if (isStatic && name.find("::") == std::string::npos) return name;
    std::string out, why;
    if (!microsoftDataName(name, type, &out, &why))
        p_.src_.fail(pos, "'" + name + "' cannot be given a name the linker can "
                       "hold: " + why);
    return out;
}

const Type *Linkage::parameterAsWritten(const Type *declared,
                                       const Type *adjusted) {
    if (!adjusted->isPointer()) return adjusted;
    // An array of const elements is a const *array*, so isConst() answers for
    // `const int a[]` as well - and both want the same const pointer.
    if (declared->isArray() || declared->isConst())
        return p_.types_.withConst(adjusted);
    return adjusted;
}

// **A function type is interned on its parameters, so two functions with one
// signature are one type** - which is why the written list cannot be kept on
// it: `f1(int a[])` and `f2(int *a)` would then be the same Q.
const Type *Linkage::manglingType(const Type *fn) {
    if (p_.writtenParams_.empty() || p_.writtenFor_ != fn->params()) return fn;
    return p_.types_.functionType(fn->returns(), p_.writtenParams_, fn->isVariadicFn());
}

// `_GLOBAL__sub_I_<file>`, the name clang gives it: the main file's basename
// with every character that is not a letter, a digit, '_' or '.' made '_'.
std::string Linkage::initFunctionSymbol() const {
    std::string file = p_.src_.files().empty() ? std::string("a.cpp")
                                            : p_.src_.files().front();
    const std::size_t slash = file.find_last_of("/\\");
    if (slash != std::string::npos) file = file.substr(slash + 1);
    for (std::size_t i = 0; i < file.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(file[i]);
        if (!std::isalnum(c) && c != '_' && c != '.') file[i] = '_';
    }
    return "_GLOBAL__sub_I_" + file;
}

// `??__F<scoped>@YAXXZ`, cl's name for the function atexit is handed for one
// object - measured: ??__Fg1@@YAXXZ, ??__Fmg@M@N@@YAXXZ, and for a static
// member the object's whole symbol, ??__F?member@H@@2US@@A@@YAXXZ.
std::string Linkage::atexitHelperName(const std::string &scoped) const {
    return "??__F" + scoped + "@YAXXZ";
}

// A static local's symbol: the *mangled* function plus the object's name, numbered where a name repeats (the bare
// name made S::f, T::f, f(double) one `f.n`). **An inline function's static local is one object for the program**
// ([basic.def.odr]/6): the ABI's name in a COMDAT (1.28, item 14) - cl `?a@?1??f@@YAHXZ@4HA`, Itanium `_ZZ1fvE1a`.
std::string Linkage::uniqueStaticSymbol(const std::string &name, const Type *type) {
    const std::string &owner = p_.currentFunction_.empty() ? p_.functionName_
                                                        : p_.currentFunction_;
    if (p_.currentFunctionInline_ && type != nullptr) {
        std::string abi;
        if (p_.target_.microsoftNames()) {
            std::string data, why;
            const std::string head = "?" + name + "@@3";
            if (!owner.empty() && owner[0] == '?' && microsoftDataName(name, type, &data, &why) &&
                data.compare(0, head.size(), head) == 0)
                abi = "?" + name + "@?" + std::to_string(p_.currentBlock() + 1) + "?" + owner + "@4" +
                      data.substr(head.size());
        } else if (owner.compare(0, 2, "_Z") == 0) {
            abi = "_ZZ" + owner.substr(2) + "E" + std::to_string(name.size()) + name;
            int seen = 0;
            for (const std::string &used : staticSymbols_)
                if (used == abi || used.compare(0, abi.size() + 1, abi + "_") == 0) seen++;
            if (seen > 0) abi += "_" + std::to_string(seen - 1);
        }
        if (!abi.empty()) {
            staticSymbols_.push_back(abi);
            return abi;
        }
    }
    std::string symbol = owner + "." + name;
    for (int n = 1; ; n++) {
        bool taken = false;
        for (const std::string &used : staticSymbols_)
            if (used == symbol) { taken = true; break; }
        if (!taken) break;
        symbol = owner + "." + name + "." + std::to_string(n);
    }
    staticSymbols_.push_back(symbol);
    return symbol;
}

// The storage's name, measured from clang: Itanium spells the first member as a mangled variable with
// no `L` (`_Z1a`, `_ZN1n1eE`); Microsoft numbers the object `$S<n>` of type `<unnamed-type-$S<n>>`.
std::string Linkage::anonymousUnionSymbol(const Type *u, std::size_t pos) {
    const std::string hidden = "$S" + std::to_string(p_.anonUnionSeq_);
    const std::string first = u->members().empty() ? hidden : u->members()[0].name;
    if (!p_.target_.microsoftNames()) {
        const std::string q = p_.namespacePrefix() + first;
        if (q.find("::") != std::string::npos) return itaniumDataName(q, false);
        return "_Z" + std::to_string(first.size()) + first;
    }
    Type *shape = p_.types_.structType(Kind::Union, p_.namespacePrefix() + "<unnamed-type-" + hidden + ">");
    shape->setLocalName("<unnamed-type-" + hidden + ">");
    if (!p_.namespaceStack_.empty()) shape->setInNamespace();
    std::string out, why;
    if (!microsoftDataName(p_.namespacePrefix() + hidden, shape, &out, &why))
        p_.src_.fail(pos, "this anonymous union cannot be given a name the linker can hold: " + why);
    return out;
}

}   // namespace sema
