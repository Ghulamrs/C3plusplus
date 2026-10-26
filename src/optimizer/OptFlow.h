#pragma once

// **x86's instructions in the shared flow graph**: how each moves control, and
// the flow built from the effects table under a calling convention.

#include "OptEffects.h"

namespace opt {

Control controlOf(const MInst &i);

struct Flow : FlowOf<MEntry> {
    void build(const MStream &s, const Convention &c, const std::vector<Region> &regions) {
        FlowOf<MEntry>::build(s, [&c](const MInst &i) { return effectsOf(i, c); }, regions);
    }
};

}
