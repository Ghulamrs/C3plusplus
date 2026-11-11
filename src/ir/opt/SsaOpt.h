#pragma once

// **The passes on the SSA IR** (M3b.4, design section 4): each a file `Ssa*.cpp` here, run by `ssaOptimize` after the
// lowering's `verify`, re-verified after every pass under -check=ssa; -dump=ssa=<pass> writes the IR after that pass.

#include "../Ir.h"

#include <cstdint>
#include <map>
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
    // fold's, by kind: instructions made constants, comparisons, conversions, floating; identities, copies, block
    // parameters given their one value; and constant operands left to run (a trap, a shift past the width, a NaN).
    int constants, compares, casts, floating, identities, copies, arguments, kept;
    int constBranches, sameBranches, unreachable, merged, forwarded;   // cfg's: jumps made, blocks removed, merged, forwarded
    int deadInsts, deadParams, deadStores, deadAllocas;                // dead's: instructions, parameters, stores, frame objects
    // gvn's, by kind: constants and addresses, arithmetic, comparisons, conversions, geps, selects replaced; loads
    // given an earlier load's value, and a store's.
    int gvnConstants, gvnArith, gvnCompares, gvnCasts, gvnGeps, gvnSelects, gvnLoads, gvnStored;
    int threaded, threadedSwitch;       // jumps': edges sent past a condbr's test, and past a switch's
    // divide's: by a power of two, by a multiply-high signed and unsigned, of those at 64 bits; a constant divisor left.
    int divShifts, divSigned, divUnsigned, divWide, divLeft;
    // The volatile loads and stores in the body before the passes and after them: fewer only where cfg removed a block.
    int volatiles, volatilesKept;
    // inline's: the direct calls of a function of the unit, those cloned in place, and the rest by reason.
    int inlineSites = 0, inlined = 0; std::map<std::string, int> inlineRefused;
    int loopsFound, preheaders, hoisted, hoistedTraps, widened;    // loops': found, preheaders made, hoisted (divisions), widened
    int hoistedLoads = 0;                                          // of those hoisted, loads
    int unrolledFull = 0, unrolledPartial = 0;                     // unroll's: loops copied whole, and 4 or 2 times
    int rotated = 0;                                               // rotate's: loops whose head's test went to the latch
    PassLog(bool c, const std::string &after) : check(c), dumpAfter(after), failed(0), allocas(0), scalars(0), promoted(0),
        constants(0), compares(0), casts(0), floating(0), identities(0), copies(0), arguments(0), kept(0), constBranches(0),
        sameBranches(0), unreachable(0), merged(0), forwarded(0), deadInsts(0), deadParams(0), deadStores(0), deadAllocas(0),
        gvnConstants(0), gvnArith(0), gvnCompares(0), gvnCasts(0), gvnGeps(0), gvnSelects(0), gvnLoads(0), gvnStored(0),
        threaded(0), threadedSwitch(0), divShifts(0), divSigned(0), divUnsigned(0), divWide(0), divLeft(0), volatiles(0),
        volatilesKept(0), loopsFound(0), preheaders(0), hoisted(0), hoistedTraps(0), widened(0) {}
};

// The pass names -dump=ssa= takes, in the order they run.
bool isSsaPass(const std::string &name);
void ssaOptimize(Function &f, int level, const TypeTable &types, const Target &target, PassLog &log);

// Edge s of a terminator is a jump - br, condbr, switch, an invoke's normal return - and not the runtime's way in.
bool isJumpEdge(const Inst &term, std::size_t s);
// By block: none to remove, merge or forward - a pad, a funclet's, the runtime's way in, one a pad names (SsaCfg.cpp).
std::vector<char> fixedBlocks(const Function &f);
// The frame objects marked `gone` removed, the others renumbered in the entry's allocas and the calls (SsaPromote.cpp).
void dropFrameObjects(Function &f, const std::vector<char> &gone);

// **Inlining** (SsaInline.cpp), over the unit before any function's passes: Inliner's budgets, the callee's blocks cloned.
void inlineCalls(Module &m, int level, const TypeTable &types, PassLog &log);
// **mem2reg** (SsaPromote.cpp): a scalar alloca whose every use is a whole load or store becomes values.
void promote(Function &f, const TypeTable &types, PassLog &log);
// **Folding, copy propagation, algebraic simplification** (SsaFold.cpp), one worklist over the def-use chains.
void fold(Function &f, const TypeTable &types, const Target &target, PassLog &log);
// **CFG simplification** (SsaCfg.cpp): constant branches, unreachable blocks, merges, empty blocks forwarded. True on a change.
bool cfg(Function &f, PassLog &log);
// **Dead code and dead stores** (SsaDead.cpp): stores no load sees, then what nothing live reads. True on a change.
bool dead(Function &f, PassLog &log);
// **Value numbering** (SsaValues.cpp): a pure instruction a dominating twin computes, a load its block holds. True on a change.
bool values(Function &f, PassLog &log);
// **Jump threading** (SsaJumps.cpp): an edge passing a constant to a block that only tests it goes where the test goes.
bool jumps(Function &f, const TypeTable &types, const Target &target, PassLog &log);
// **Division by a constant** (SsaDivide.cpp): shifts, or a multiply-high by DivMagic.h's number. True on a change.
bool divide(Function &f, const TypeTable &types, const Target &target, PassLog &log);
// **LICM and counter widening** (SsaLoops.cpp): invariants to a preheader, an int index counter carried at 64 bits.
bool loops(Function &f, const TypeTable &types, const Target &target, PassLog &log);
// A natural loop (SsaLoops.cpp): pre is the one block outside, entering by a br, 0 for none or a loop holding a fixed block.
struct Loop { std::uint32_t head, pre; std::vector<std::uint32_t> blocks; std::vector<char> in; std::vector<std::uint32_t> latches; };
std::vector<Loop> naturalLoops(Function &f, const Dominators &dom, const TypeTable &types, const Target &target, PassLog &log);
// **Unrolling** (SsaUnroll.cpp): a loop counted by constants copied whole, or 4 or 2 times, within OptCosts.h's budget.
bool unroll(Function &f, const TypeTable &types, const Target &target, PassLog &log);
// **Rotation** (SsaLoops.cpp), after unroll: a small head's test repeated in the preheader and the latch. True on a change.
bool rotate(Function &f, const TypeTable &types, const Target &target, PassLog &log);
// gvn's kind of a pure instruction (SsaValues.cpp): 1 constant, 2 arithmetic, 3 compare, 4 cast, 5 gep, 6 select, else 0.
int pureKind(const Inst &i);

// fold's constant arithmetic, for the passes that evaluate (SsaFold.cpp): `cc` over two integer constants of type t
// read at the selector's width, and an integer constant converted to integer type `to`; false where fold leaves it.
bool foldCompare(Cmp cc, long long x, long long y, const Type &t, const Target &target, long long &out);
bool foldIntConversion(long long x, const Type &to, const Target &target, long long &out);

}   // namespace ir
