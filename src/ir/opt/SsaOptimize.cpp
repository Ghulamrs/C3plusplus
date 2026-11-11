// The driver of the SSA passes (SsaOpt.h): each pass at the levels that run it, then the verifier and the dump asked for.

#include "SsaOpt.h"

namespace ir {

namespace {

struct Pass { char name[16]; int level; bool again; };

// In the order they run, `run` dispatching by index: jumps before cfg (which removes what threading leaves unreached),
// gvn after cfg (longer runs of loads), divide after gvn (-O2's alone), loops after divide (what it may hoist is left),
// unroll after loops (the counter widened, the invariants out), rotate after unroll (which reads the head's test), dead last; `again`: once more after a change.
const Pass kPasses[] = {
    { "promote", 1, false },
    { "fold", 1, false },
    { "jumps", 1, false },
    { "cfg", 1, false },
    { "gvn", 1, false },
    { "divide", 2, false },
    { "loops", 2, false },
    { "unroll", 2, false },
    { "rotate", 2, false },
    { "dead", 1, false },
    { "refold", 1, true },
    { "rejumps", 1, true },
    { "recfg", 1, true },
    { "regvn", 1, true },
    { "redead", 1, true },
};

bool run(std::size_t pass, Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    switch (pass) {
    case 0: promote(f, types, log); return false;
    case 1: case 10: fold(f, types, target, log); return false;
    case 2: case 11: return jumps(f, types, target, log);
    case 3: case 12: return cfg(f, log);
    case 4: case 13: return values(f, log);
    case 5: return divide(f, types, target, log);
    case 6: return loops(f, types, target, log);
    case 7: return unroll(f, types, target, log);
    case 8: return rotate(f, types, target, log);
    case 9: case 14: return dead(f, log);
    default: return false;
    }
}

int volatileAccesses(const Function &f) {
    int n = 0;
    for (BlockId b : f.blocks)
        for (InstId i : f.block(b).insts) n += f.inst(i).isVolatile ? 1 : 0;
    return n;
}

}   // namespace

bool isSsaPass(const std::string &name) {
    for (const Pass &p : kPasses) if (name == p.name) return true;
    return name == "inline";    // the unit's pass, run before these (inlineCalls)
}

void ssaOptimize(Function &f, int level, const TypeTable &types, const Target &target, PassLog &log) {
    bool changed = false;
    int volatiles = volatileAccesses(f);
    log.volatiles += volatiles;
    for (std::size_t k = 0; k < sizeof kPasses / sizeof kPasses[0]; k++) {
        const Pass &p = kPasses[k];
        if (level >= p.level && (!p.again || changed)) {
            const bool c = run(k, f, types, target, log);
            if (!p.again) changed = changed || c;
        }
        if (log.check) {
            std::string faults;
            if (verify(f, types, faults) != 0) { log.failed++; log.faults += "  after " + std::string(p.name) + ":\n" + faults; }
        }
        // A volatile access leaves the body only with a block cfg found unreachable.
        const int now = volatileAccesses(f);
        if (log.check && now != volatiles && (now > volatiles || (k != 3 && k != 12))) {
            log.failed++;
            log.faults += "  after " + std::string(p.name) + ": " + f.symbol + ": volatile accesses " + std::to_string(volatiles) +
                          " became " + std::to_string(now) + "\n";
        }
        volatiles = now;
        if (log.dumpAfter == p.name) log.dumped += dump(f, types);
    }
    log.volatilesKept += volatiles;
}

}   // namespace ir
