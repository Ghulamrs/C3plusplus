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
    // Lane A's kinds (BuilderCopy.cpp): the implicit copy and move members, the array loops and the static array's exit helper.
    void copyOrMove(const FunctionFacts &facts);
    void vectorLoop(const FunctionFacts &facts, bool constructs);
    void arrayHelper(const FunctionFacts &facts);
    ValueId elementAt(ValueId base, ValueId i, const Type &et);
    ValueId into(ValueId p, long long at, TypeId to);
    void openBody();
    bool recorded(const std::string &name, const Type &cls, std::size_t at, std::string &symbol, TypeId &sig);
    Lowering &l_;
};

}   // namespace ir
