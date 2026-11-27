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

class TypedTree;
struct TypedId;

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
    const Expr *singleReturnValue(syntax::NodeId body, std::string *why) const;
    bool foldArgument(const Expr &a, ConstexprSlot *slot, std::size_t pos) const;
    bool foldObject(const Expr &e, ConstObject *out, std::size_t pos) const;
    bool foldObjectPointer(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const;
    bool objectAddress(const Expr &lvalue, ConstObjectPtr *obj, int *at, std::size_t pos) const;
    bool foldLeaf(const Expr &lvalue, ConstLeaf *out, std::size_t pos) const;
    bool runConstexpr(const Effect &e, std::size_t pos) const;
    bool runConstexprEffect(const Expr &e, std::size_t pos) const;
    bool constructConst(const Call &c, std::size_t pos) const;
    std::string constObjectKey(const Var &v) const;
    bool instantiateForConstant(const std::string &symbol, bool constructor) const;
    static ConstObject subObject(const ConstObject &from, int at, int size);
    static void storeSubObject(ConstObject &into, int at, int size, const ConstObject &what);
    // The object a declaration's own steps build, run with `target` standing for it.
    bool foldDeclaredObject(const std::vector<Effect> &init, const Var &target, ConstObject *out,
                            std::size_t pos);
    bool constObjectFromInit(const Type *t, const Init &in, int base, ConstObject *out) const;
    void constObjectPieces(const ConstObject &o, std::vector<GlobalPiece> &out);
    void refuseConstexprClassObject(const std::string &name, std::size_t pos) const;
    bool foldPointerConstant(const Expr &e, long long *out, std::size_t pos) const;
    bool fold(const Expr &e, long long *out, std::size_t pos) const;
    // While held on, a read's value-dependent names fold at their stand-in, for a parse that must go on (M8rev L2).
    struct StandIn {
        StandIn(const Constexpr &c, bool on) : c_(c), was_(c.standIn_) { c_.standIn_ = was_ || on; }
        ~StandIn() { c_.standIn_ = was_; }
        const Constexpr &c_;
        bool was_;
    };
    // fold's cases over a typed expression (M6d S6, C2); false with `*why` set where this lane does not reach the case.
    bool fold(const TypedTree &t, TypedId id, long long *out, const char **why) const;
    // foldDouble's over a typed expression (3R-d, eighth lane): its own reader, foldDouble's arithmetic.
    bool foldFloating(const TypedTree &t, TypedId id, long double *out, bool *past53, bool *x87Rounded, const char **why) const;
    std::string foldReport(int *differ) const;
    long long narrowTo(long long v, const Type *t) const;

    bool checkFold_ = false;     // -check=fold

    std::map<std::string, ConstexprFn> constexprFns_;   // by mangled symbol

    std::map<std::string, ConstexprCtor> constexprCtors_;   // by C1 and C2 symbol
    std::set<std::string> constexprCtorSymbols_;           // declared `constexpr` in their class
    // Named constexpr objects: a global by its name, a local by function and slot.
    std::map<std::string, ConstObject> constObjects_;

private:
    void checkFold(syntax::NodeId s, bool ok, long long value, const Type *asked) const;
    Parser &p_;
    mutable std::vector<std::vector<ConstexprSlot> > constexprFrames_;
    mutable int foldDepth_ = 0, foldAgreed_ = 0, foldDiffer_ = 0;
    mutable bool standIn_ = false;
    mutable std::map<std::string, int> foldNotReached_;
    mutable std::string foldDiffers_;
};

}   // namespace sema
