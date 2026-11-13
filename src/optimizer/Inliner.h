#pragma once

// **Which calls are walked in place of a call instruction.** GCC's ipa-inline reduced to what the walker can know before it walks: every function of the unit measured once, every call site judged in one pass over the unit - the best sites first, as GCC's badness queue orders them - against three budgets the level's costs set, per site, per caller and per unit; then the walker asks about each site as it reaches it and gets the decision already made.
// What makes a callee safe to walk in place at all (no landing pad, no cleanup to run on unwind, no goto label and no switch - the parser names their labels once per function, and a body walked twice would name them twice - not variadic, no register save area, no Microsoft throw - its object stays in the frame that threw until its handler ends, and a funclet walks its own callees over that frame) is decided here too; what makes a site safe (a direct call, no stack arguments, not inside another callee walked in place) only the walker knows, and it asks before this is asked.
// Who asks it now (x86's SSA inline, tms6747's walker) is docs/OPTIMIZER-IR.md; the long form is docs/CLAUDE-history.md, "The optimizer's design notes".

#include "../Ast.h"
#include "OptCosts.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Inliner {
public:
    // What is known of one function, by FuncId - on the Ast.h path its place in Program::functions, from 1: its body's
    // size (AST nodes, or SSA instructions), its frame, why it is ineligible, and the growth its own sites have taken.
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

    // **The one decision both paths ask**: the functions measured by their front, the sites in program order.
    void decide(std::vector<Measure> measures, std::vector<Site> &sites);

    // The Ast.h path: every function of the unit measured and every site decided, before any is walked.
    void summarize(const Program &program);

    // Whether the callee may be walked in place anywhere.
    bool eligible(const Function &callee) const { const Measure *m = measureOf(callee); return m != nullptr && m->eligible(); }

    // Whether this call is walked in place.
    bool allows(const Call &site) const;

    // The callee's body in AST nodes, for a target that takes only the smallest.
    int size(const Function &fn) const { const Measure *m = measureOf(fn); return m != nullptr ? m->size : 0; }
    // No call and no loop in it: straight-line code, which walked in place is shorter than a call.
    bool leaf(const Function &fn) const { const Measure *m = measureOf(fn); return m != nullptr && m->leaf; }

    // The largest frame any site of the unit brings in, for a caller whose frame must be fixed before its body is walked.
    int largestFrame() const { return largestFrame_; }

private:
    const opt::Costs &costs_;
    std::vector<Measure> measures_;                 // by FuncId, 0 none
    std::map<std::string, std::uint32_t> bySymbol_; // the Ast.h path's FuncId of a symbol
    std::map<const Call *, bool> decided_;          // the Ast.h path's call instruction is its node: looked up, never walked
    int unitSize_ = 0;
    int unitTaken_ = 0;
    int largestFrame_ = 0;

    const Measure *measureOf(const Function &fn) const;
    // Whether the site's growth is within what a site this deep in loops may take, and the caller and the unit have the room.
    bool withinBudgets(const Site &site, const char **why) const;
    void charge(const Site &site);
};
