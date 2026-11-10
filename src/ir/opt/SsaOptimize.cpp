// The driver of the SSA passes (SsaOpt.h): each pass at the levels that run it, then the verifier and the dump asked for.

#include "SsaOpt.h"

namespace ir {

namespace {

struct Pass { char name[16]; int level; bool again; };

// In the order they run, `run` dispatching by index; gvn after cfg (merged blocks: longer runs of loads), before dead (which
// takes what it leaves). Those marked `again` run once more, only where cfg, gvn or dead changed the function the first
// time: what a removed edge or value leaves to fold.
const Pass kPasses[] = {
    { "promote", 1, false },
    { "fold", 1, false },
    { "cfg", 1, false },
    { "gvn", 1, false },
    { "dead", 1, false },
    { "refold", 1, true },
    { "recfg", 1, true },
    { "regvn", 1, true },
    { "redead", 1, true },
};

bool run(std::size_t pass, Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    switch (pass) {
    case 0: promote(f, types, log); return false;
    case 1: case 5: fold(f, types, target, log); return false;
    case 2: case 6: return cfg(f, log);
    case 3: case 7: return values(f, log);
    case 4: case 8: return dead(f, log);
    default: return false;
    }
}

}   // namespace

bool isSsaPass(const std::string &name) {
    for (const Pass &p : kPasses) if (name == p.name) return true;
    return false;
}

void ssaOptimize(Function &f, int level, const TypeTable &types, const Target &target, PassLog &log) {
    bool changed = false;
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
        if (log.dumpAfter == p.name) log.dumped += dump(f, types);
    }
}

}   // namespace ir
