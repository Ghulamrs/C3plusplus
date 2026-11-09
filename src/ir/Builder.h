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
    FuncId thunk(const FunctionFacts &facts, bool virt);     // Wrappers.cpp: a thunk or a virtual thunk
    FuncId initFunction(const FunctionFacts &facts);      // Wrappers.cpp: _GLOBAL__sub_I_<file>
    Lowering &l_;
};

}   // namespace ir
