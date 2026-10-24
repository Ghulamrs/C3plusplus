#pragma once

// **sema::Constexpr, out of the parser in place** - M2 step 1, doc-cxx3/M2-PLAN.md. Its functions moved here
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

class Constexpr {
public:
    explicit Constexpr(Parser &p) : p_(p) {}

    // Every value carried as a double, exactly, or the fold says so.
    bool foldDouble(const Expr &e, const Target &target, long double *out,
                    bool *past53, bool *x87Rounded) const;
    bool constantInitialiser(const Type *t, const Init &in, long long *out) const;
    bool constantFloatingInitialiser(const Type *t, const Init &in,
                                     long double *out) const;
    bool foldFloating(const Expr &e, long double *out) const;
    const ConstexprSlot *constexprSlot(const Var &v) const;
    // Folds the arguments and pushes a frame; false when the call cannot fold.
    bool enterConstexprCall(const Call &c, const ConstexprFn **fn,
                            std::size_t pos) const;
    const Expr *singleReturnValue(const Stmt &body) const;
    bool foldArgument(const Expr &a, ConstexprSlot *slot, std::size_t pos) const;
    bool foldObject(const Expr &e, ConstObject *out, std::size_t pos) const;
    bool foldObjectPointer(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const;
    bool objectAddress(const Expr &lvalue, ConstObjectPtr *obj, int *at, std::size_t pos) const;
    bool foldLeaf(const Expr &lvalue, ConstLeaf *out, std::size_t pos) const;
    bool runConstexpr(const Stmt &s, std::size_t pos) const;
    bool runConstexprEffect(const Expr &e, std::size_t pos) const;
    bool constructConst(const Call &c, std::size_t pos) const;
    std::string constObjectKey(const Var &v) const;
    bool instantiateForConstant(const std::string &symbol) const;
    static ConstObject subObject(const ConstObject &from, int at, int size);
    static void storeSubObject(ConstObject &into, int at, int size, const ConstObject &what);
    // The object a declaration's own statements build, run with `target` standing for it.
    bool foldDeclaredObject(const std::vector<const Stmt *> &init, const Var &target, ConstObject *out,
                            std::size_t pos);
    bool constObjectFromInit(const Type *t, const Init &in, int base, ConstObject *out) const;
    void constObjectPieces(const ConstObject &o, std::vector<GlobalPiece> &out);
    void refuseConstexprClassObject(const std::string &name, std::size_t pos) const;
    bool foldPointerConstant(const Expr &e, long long *out, std::size_t pos) const;
    bool fold(const Expr &e, long long *out, std::size_t pos) const;
    long long narrowTo(long long v, const Type *t) const;

    std::map<std::string, ConstexprFn> constexprFns_;   // by mangled symbol

    std::map<std::string, ConstexprCtor> constexprCtors_;   // by C1 and C2 symbol
    std::set<std::string> constexprCtorSymbols_;           // declared `constexpr` in their class
    // Named constexpr objects: a global by its name, a local by function and slot.
    std::map<std::string, ConstObject> constObjects_;

private:
    Parser &p_;
    mutable std::vector<std::vector<ConstexprSlot> > constexprFrames_;
};

}   // namespace sema
