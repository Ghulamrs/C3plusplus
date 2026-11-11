#include "Inliner.h"
#include "OptDeclines.h"

#include <algorithm>

namespace {

// **A body's size is its node count**, every expression and statement one; and each call in
// it is noted with the loops it sits inside. The unit of GCC's inline parameters is its own
// estimate of instructions, and a node here is near enough one to take its numbers as a start.
class Measurer final : public Visitor {
public:
    int nodes = 0;
    bool cleanups = false;                          // a block that runs destructors on unwind
    bool labels = false;                            // a goto's label or a case's: the parser names them once per function
    std::vector<std::pair<const Call *, int>> calls; // each call, and its loop depth
    bool loops = false;                             // a while, do or for anywhere in it
    bool throws = false;                            // a Microsoft throw, whose object outlives its frame's walk in place

    void visit(const Num &) override { ++nodes; }
    void visit(const Var &) override { ++nodes; }
    void visit(const StrLit &) override { ++nodes; }
    void visit(const Goto &) override { ++nodes; labels = true; }
    void visit(const Break &) override { ++nodes; }
    void visit(const Continue &) override { ++nodes; }
    void visit(const FuncletLeave &) override { ++nodes; labels = true; }
    void visit(const Call &n) override {
        ++nodes;
        calls.push_back(std::make_pair(&n, depth_));
        if (n.symbol() == "_CxxThrowException") throws = true;
        if (n.callee() != nullptr) n.callee()->accept(*this);
        for (const ExprPtr &a : n.args()) a->accept(*this);
    }
    void visit(const VaStart &n) override { ++nodes; n.list().accept(*this); }
    void visit(const VaArg &n) override { ++nodes; n.list().accept(*this); }
    void visit(const Assign &n) override { ++nodes; n.target().accept(*this); n.value().accept(*this); }
    void visit(const Unary &n) override { ++nodes; n.operand().accept(*this); }
    void visit(const Binary &n) override { ++nodes; n.lhs().accept(*this); n.rhs().accept(*this); }
    void visit(const Postfix &n) override { ++nodes; n.target().accept(*this); }
    void visit(const Cast &n) override { ++nodes; n.value().accept(*this); }
    void visit(const Comma &n) override { ++nodes; n.left().accept(*this); n.right().accept(*this); }
    void visit(const Conditional &n) override {
        ++nodes;
        n.cond().accept(*this); n.thenArm().accept(*this); n.elseArm().accept(*this);
    }
    void visit(const MemberAccess &n) override { ++nodes; n.object().accept(*this); }
    void visit(const ExprStmt &n) override { ++nodes; n.expr().accept(*this); }
    void visit(const Return &n) override { ++nodes; if (n.hasValue()) n.value().accept(*this); }
    void visit(const Block &n) override {
        ++nodes;
        if (n.unwindCleanup()) cleanups = true;
        for (const StmtPtr &s : n.body()) s->accept(*this);
    }
    void visit(const If &n) override {
        ++nodes;
        n.cond().accept(*this); n.thenArm().accept(*this);
        if (n.elseArm() != nullptr) n.elseArm()->accept(*this);
    }
    void visit(const While &n) override { ++nodes; n.cond().accept(*this); inLoop(n.body()); }
    void visit(const DoWhile &n) override { ++nodes; inLoop(n.body()); n.cond().accept(*this); }
    void visit(const For &n) override {
        ++nodes;
        if (n.init() != nullptr) n.init()->accept(*this);
        if (n.cond() != nullptr) n.cond()->accept(*this);
        if (n.step() != nullptr) n.step()->accept(*this);
        inLoop(n.body());
    }
    void visit(const Switch &n) override { ++nodes; labels = true; n.cond().accept(*this); n.body().accept(*this); }
    void visit(const Case &n) override { ++nodes; n.body().accept(*this); }
    void visit(const Label &n) override { ++nodes; labels = true; n.body().accept(*this); }
    void visit(const Try &n) override {
        ++nodes;
        for (const StmtPtr &s : n.body()) s->accept(*this);
        if (n.hasPad()) n.pad().accept(*this);
        if (n.cleanup() != nullptr) n.cleanup()->accept(*this);
        for (const MsHandler &h : n.handlers())
            if (h.body != nullptr) h.body->accept(*this);
    }

private:
    int depth_ = 0;
    void inLoop(const Node &body) { loops = true; ++depth_; body.accept(*this); --depth_; }
};

// What a call sequence costs, in the same measure: the call itself and the moves that place its arguments.
int callCost(const Call &site) { return 1 + static_cast<int>(site.args().size()); }

int roundedFrame(const Function &fn) { return (fn.frameSize() + 15) & ~15; }

}

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
        largestFrame_ = std::max(largestFrame_, measures_[s.callee].frame);
    }
}

// The Ast.h path's measures: every function's nodes, and every call of one this unit defines, a call of itself
// included, listed with its caller.
void Inliner::summarize(const Program &program) {
    std::vector<Measure> measures(1);
    std::vector<std::vector<std::pair<const Call *, int>>> calls(1);
    for (const Function &fn : program.functions) {
        Measurer m;
        fn.body().accept(m);
        Measure measure;
        measure.symbol = fn.symbol();
        measure.size = m.nodes;
        measure.frame = roundedFrame(fn);
        measure.why = fn.hasLandingPads() ? "inline-callee-landing-pad" : fn.isVariadic() ? "inline-callee-variadic"
                    : fn.regSaveSlot() != 0 ? "inline-callee-register-save-area" : m.cleanups ? "inline-callee-cleanup"
                    : m.labels ? "inline-callee-label-or-switch" : m.throws ? "inline-callee-throws-in-its-frame" : "";
        measure.leaf = m.calls.empty() && !m.loops;
        bySymbol_[fn.symbol()] = static_cast<std::uint32_t>(measures.size());
        measures.push_back(measure);
        calls.push_back(m.calls);
    }
    std::vector<Site> sites;
    std::vector<const Call *> nodes;
    for (std::size_t f = 1; f < calls.size(); f++)
        for (const auto &call : calls[f]) {
            const auto callee = bySymbol_.find(call.first->symbol());
            if (callee == bySymbol_.end()) continue;
            sites.push_back(Site{static_cast<std::uint32_t>(f), callee->second, call.second, callCost(*call.first), 0, false, ""});
            nodes.push_back(call.first);
        }
    decide(measures, sites);
    for (std::size_t k = 0; k < sites.size(); k++)
        if (measures_[sites[k].callee].eligible()) decided_[nodes[k]] = sites[k].taken;
}

const Inliner::Measure *Inliner::measureOf(const Function &fn) const {
    const auto it = bySymbol_.find(fn.symbol());
    return it != bySymbol_.end() ? &measures_[it->second] : nullptr;
}

bool Inliner::allows(const Call &site) const {
    const auto it = decided_.find(&site);
    return it != decided_.end() && it->second;
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
