// The driver of the SSA passes (SsaOpt.h): each pass at the levels that run it, then the verifier and the dump asked for.

#include "SsaOpt.h"

namespace ir {

namespace {

struct Pass { char name[16]; int level; };

// In the order they run; `run` below dispatches by the index here.
const Pass kPasses[] = {
    { "promote", 1 },
};

void run(std::size_t pass, Function &f, const TypeTable &types, PassLog &log) {
    switch (pass) {
    case 0: promote(f, types, log); break;
    default: break;
    }
}

}   // namespace

bool isSsaPass(const std::string &name) {
    for (const Pass &p : kPasses) if (name == p.name) return true;
    return false;
}

void ssaOptimize(Function &f, int level, const TypeTable &types, PassLog &log) {
    for (std::size_t k = 0; k < sizeof kPasses / sizeof kPasses[0]; k++) {
        const Pass &p = kPasses[k];
        if (level >= p.level) run(k, f, types, log);
        if (log.check) {
            std::string faults;
            if (verify(f, types, faults) != 0) { log.failed++; log.faults += "  after " + std::string(p.name) + ":\n" + faults; }
        }
        if (log.dumpAfter == p.name) log.dumped += dump(f, types);
    }
}

}   // namespace ir
