#pragma once

// **Which calls are walked in place of a call instruction.** GCC's ipa-inline reduced to what the walker can know before it walks: every function of the unit measured once, every call site judged in one pass over the unit - the best sites first, as GCC's badness queue orders them - against three budgets the level's costs set, per site, per caller and per unit; then the walker asks about each site as it reaches it and gets the decision already made.
// What makes a callee safe to walk in place at all (no landing pad, no cleanup to run on unwind, no goto label and no switch - the parser names their labels once per function, and a body walked twice would name them twice - not variadic, no register save area, no Microsoft throw - its object stays in the frame that threw until its handler ends, and a funclet walks its own callees over that frame) is decided here too; what makes a site safe (a direct call, no stack arguments, not inside another callee walked in place) only the walker knows, and it asks before this is asked.
// Who asks it now (the SSA inliner, SsaInline.cpp) is docs/OPTIMIZER-IR.md; the long form is docs/CLAUDE-history.md, "The optimizer's design notes".

#include "OptCosts.h"

#include <cstdint>
#include <string>
#include <vector>

class Inliner {
public:
    // What is known of one function, by FuncId: its body's size in SSA instructions, its frame, why it is ineligible, and the growth its own sites have taken.
    struct Measure {
        std::string symbol;
        int size = 0, frame = 0, taken = 0;
        bool leaf = false;
        const char *why = "";
        bool eligible() const { return *why == '\0'; }
    };
    // One call of a function this unit defines: the loops it sits in, what the call itself costs, and - decided -
    // what walking the callee in its place adds to the caller, whether it is taken and, where not, why.
    struct Site {
        std::uint32_t caller, callee;
        int loopDepth, callCost, growth;
        bool taken;
        const char *why;
    };

    explicit Inliner(const opt::Costs &costs);

    // **The one decision**: the functions measured by the SSA inliner, the sites in program order.
    void decide(std::vector<Measure> measures, std::vector<Site> &sites);

private:
    const opt::Costs &costs_;
    std::vector<Measure> measures_;                 // by FuncId, 0 none
    int unitSize_ = 0;
    int unitTaken_ = 0;

    // Whether the site's growth is within what a site this deep in loops may take, and the caller and the unit have the room.
    bool withinBudgets(const Site &site, const char **why) const;
    void charge(const Site &site);
};
