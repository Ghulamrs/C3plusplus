#include "Inliner.h"
#include "OptDeclines.h"

#include <algorithm>

Inliner::Inliner(const opt::Costs &costs) : costs_(costs) {}

// **Every site decided in one pass, the best first.** A site the budgets admit is charged to its caller and to the
// unit at once, so a worse site later finds what is left. GCC's badness, reduced: growth over how often the site
// runs, each loop level worth twice the one outside it; sorted stably, so equal sites keep program order.
void Inliner::decide(std::vector<Measure> measures, std::vector<Site> &sites) {
    measures_.swap(measures);
    for (const Measure &m : measures_) unitSize_ += m.size;
    std::vector<std::size_t> order;
    for (std::size_t k = 0; k < sites.size(); k++) {
        Site &s = sites[k];
        const Measure &callee = measures_[s.callee];
        s.taken = false;
        s.why = callee.why;
        if (!callee.eligible()) { opt::noteDecline(callee.why, callee.symbol + " in " + measures_[s.caller].symbol); continue; }
        s.growth = callee.size - s.callCost;
        order.push_back(k);
    }
    std::stable_sort(order.begin(), order.end(), [&sites](std::size_t a, std::size_t b) {
        const Site &x = sites[a], &y = sites[b];
        return static_cast<long>(x.growth) << y.loopDepth < static_cast<long>(y.growth) << x.loopDepth;
    });
    for (std::size_t k : order) {
        Site &s = sites[k];
        s.taken = withinBudgets(s, &s.why);
        if (!s.taken) {
            opt::noteDecline(s.why, measures_[s.callee].symbol + " in " + measures_[s.caller].symbol);
            continue;
        }
        charge(s);
    }
}

// **Three budgets, all of which must hold**: the site's growth against what a site this deep
// in loops may take; the caller's growth so far, with this one, against its own size; the
// unit's likewise. A budget of zero percent still admits a site that grows nothing.
bool Inliner::withinBudgets(const Site &site, const char **why) const {
    const int growth = site.growth;
    if (growth > costs_.inlineGrowth(site.loopDepth)) { *why = "inline-site-too-large"; return false; }
    *why = "";
    if (growth <= 0) return true;
    const Measure &c = measures_[site.caller];
    const long callerRoom = static_cast<long>(std::max(c.size, costs_.largeFunction())) * costs_.callerGrowthPercent() / 100;
    const long unitRoom = static_cast<long>(unitSize_) * costs_.unitGrowthPercent() / 100;
    if (c.taken + growth > callerRoom) { *why = "inline-caller-budget-spent"; return false; }
    if (unitTaken_ + growth > unitRoom) { *why = "inline-unit-budget-spent"; return false; }
    return true;
}

void Inliner::charge(const Site &site) {
    if (site.growth <= 0) return;
    measures_[site.caller].taken += site.growth;
    unitTaken_ += site.growth;
}
