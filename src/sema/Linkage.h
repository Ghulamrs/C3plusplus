#pragma once

// **sema::Linkage, out of the parser in place** - M2 step 1, doc-cxx3/M2-PLAN.md. Its functions moved here
// whole; they still read the parser's state through p_, and the state follows in a later commit.

#include "Entities.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Type.h"

#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Parser;

namespace sema {

class Linkage {
public:
    explicit Linkage(Parser &p) : p_(p) {}

    std::string deletingDestructorSymbol(const std::string &cls);
    // The name a base subobject's constructor is called by: Itanium's C2
    // rather than the C1 the signature carries, and on Windows the one name
    // there is.
    std::string baseConstructorSymbol(const Signature &ctor, const Type *base);
    std::string initFunctionSymbol() const;
    std::string atexitHelperName(const std::string &object) const;
    // With its type, an inline function's static local takes the ABI's name (1.28).
    std::string uniqueStaticSymbol(const std::string &name, const Type *type = nullptr);
    // A static data member: declared inside the class, defined outside it,
    // and reached by all three of `C::n`, `obj.n` and `p->n`.
    std::string staticMemberSymbol(const std::string &cls, const std::string &name,
                                   const Type *t, Access access, std::size_t pos);
    std::string memberSymbol(const std::string &cls, const std::string &name,
                             const Type *fn, Access access, bool constThis,
                             RefQual refThis, std::size_t pos, bool isVirtual = false,
                             bool isStatic = false);
    // **Microsoft mangles a parameter from before [dcl.fct]/5 drops its cv.**
    const Type *parameterAsWritten(const Type *declared, const Type *adjusted);
    // `fn` respelled for the Microsoft mangler where this function's
    // parameters were written differently from their types, and `fn` itself
    // otherwise.
    const Type *manglingType(const Type *fn);
    std::string anonymousUnionSymbol(const Type *u, std::size_t pos);
    std::string functionSymbol(const std::string &name, const Type *returns,
                               const std::vector<const Type *> &params,
                               bool variadic, bool internal, std::size_t pos);
    std::string dataSymbol(const std::string &name, const Type *type,
                           bool isStatic, std::size_t pos);

    std::vector<std::string> staticSymbols_;

private:
    Parser &p_;
};

}   // namespace sema
