#pragma once

// **The passes on the SSA IR** (M3b.4, design section 4): each a file `Ssa*.cpp` here, run by `ssaOptimize` after the
// lowering's `verify`, re-verified after every pass under -check=ssa; -dump=ssa=<pass> writes the IR after that pass.

#include "../Ir.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ir {

// Cooper, Harvey and Kennedy's dominators over the blocks the entry reaches, every vector by block arena index.
struct Dominators {
    std::vector<std::vector<std::uint32_t> > preds;     // the reachable predecessors, one per edge
    std::vector<std::uint32_t> rpo, order, idom;        // order: the place in reverse postorder from 1, 0 unreachable
    explicit Dominators(const Function &f);
    std::uint32_t intersect(std::uint32_t a, std::uint32_t b) const;
    bool dominates(std::uint32_t a, std::uint32_t b) const;
    bool reachable(std::uint32_t b) const { return order[b] != 0; }
    // The dominance frontier of every reachable block, each list ascending.
    std::vector<std::vector<std::uint32_t> > frontiers() const;
private:
    std::uint32_t entry_;
};

// What the passes did over a unit, and what -check=ssa and -dump=ssa=<pass> asked of them.
struct PassLog {
    bool check;
    std::string dumpAfter, dumped, faults;
    int failed, allocas, scalars, promoted;     // frame objects; those of a scalar type; those promoted
    PassLog(bool c, const std::string &after) : check(c), dumpAfter(after), failed(0), allocas(0), scalars(0), promoted(0) {}
};

// The pass names -dump=ssa= takes, in the order they run.
bool isSsaPass(const std::string &name);
void ssaOptimize(Function &f, int level, const TypeTable &types, PassLog &log);

// **mem2reg** (SsaPromote.cpp): a scalar alloca whose every use is a whole load or store becomes values.
void promote(Function &f, const TypeTable &types, PassLog &log);

}   // namespace ir
