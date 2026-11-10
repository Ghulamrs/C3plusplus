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
    bool recorded(const std::string &name, const Type &cls, std::size_t at, std::string &symbol, TypeId &sig, bool &defaulted);
    FuncId thunk(const FunctionFacts &facts, bool virt);     // Wrappers.cpp: a thunk or a virtual thunk
    FuncId initFunction(const FunctionFacts &facts, bool tls);     // Wrappers.cpp: _GLOBAL__sub_I_<file>, or __tls_init
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
    FuncId atexitHelper(const FunctionFacts &facts, std::size_t index);   // MsShapes.cpp: cl's ??__F
    void writtenEntries(std::map<std::string, sema::TypedId> &named);
    Lowering &l_;
};

}   // namespace ir
