#pragma once

// **The builder of the compiler-written functions** (M3b.3, doc-cxx3/M3-PLAN.md): a function with no syntax body - an
// implicit special member, a thunk, the init function - built as SSA from its class's layout and the parser's records of
// what it chose, through the lowering's own prologue, member, base and cleanup machinery; no second copy of any of it.

#include "Lowering.h"

namespace ir {

class Builder {
public:
    explicit Builder(Lowering &lowering) : l_(lowering) {}
    // The function `facts` names, of the synthesised `kind`, built into the module - or none, with the reason it is left (a kind not built yet, by its name).
    FuncId build(const FunctionFacts &facts, const std::string &kind, std::string &why);

private:
    // M3b.3 lane B (CompleteObject.cpp): the deleting destructor, and the complete constructor and destructor of a class with
    // virtual bases - Itanium's C1 and D1 around a C2 or D2, cl's ??_D - each from the records its synthesiser leaves.
    static bool completeKind(const std::string &kind);
    FuncId completeObject(const FunctionFacts &facts, const std::string &kind, std::size_t index);
    // The function's own parameters after `this` and a VTT, and cl's most-derived flag last; each kind's body.
    void ownParameters(const FunctionFacts &facts, const std::string &kind);
    bool recorded(const std::string &what, std::string &symbol, TypeId &sig) const;
    void vttHeld();
    ValueId deleting(const Type &cls);
    void completeDtor(const Type &cls);
    void completeCtor(const Type &cls);
    Lowering &l_;
};

}   // namespace ir
