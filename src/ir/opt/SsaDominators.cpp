// The dominator tree and frontiers of one function - the verifier's and mem2reg's, one implementation (SsaOpt.h).

#include "SsaOpt.h"

#include <algorithm>
#include <utility>

namespace ir {

std::uint32_t unwindsTo(const Function &f, const Inst &from) {
    long long k = from.k;
    for (std::size_t steps = 0; k > 0 && static_cast<std::size_t>(k) < f.blockArena.size() && steps < f.blockArena.size(); steps++) {
        const Block &to = f.blockArena[static_cast<std::size_t>(k)];
        if (to.insts.empty() || f.inst(to.insts[0]).op != Op::CatchPad || f.inst(to.insts[0]).k <= 0) return static_cast<std::uint32_t>(k);
        // A handler's block: what leaves it goes where its try's catchswitch sends what that try does not take.
        const Block &dispatch = f.blockArena[static_cast<std::size_t>(f.inst(to.insts[0]).k)];
        if (!dispatch.term.valid() || f.inst(dispatch.term).op != Op::CatchSwitch) return 0;
        k = f.inst(dispatch.term).k;
    }
    return 0;
}

std::vector<std::uint32_t> successors(const Function &f, std::uint32_t b) {
    std::vector<std::uint32_t> out;
    const Block &blk = f.blockArena[b];
    if (!blk.term.valid()) return out;
    const Inst &t = f.inst(blk.term);
    for (const Succ &s : t.succ) out.push_back(s.block.v);
    if ((t.op == Op::CatchSwitch || t.op == Op::CleanupRet) && unwindsTo(f, t) != 0) out.push_back(unwindsTo(f, t));
    return out;
}

Dominators::Dominators(const Function &f) : entry_(f.entry.v) {
    const std::size_t n = f.blockArena.size();
    preds.assign(n, std::vector<std::uint32_t>());
    order.assign(n, 0);
    idom.assign(n, 0);
    if (!f.entry.valid()) return;
    std::vector<char> seen(n, 0);
    // Reverse postorder by an explicit stack: the entry first, every reachable block once.
    std::vector<std::vector<std::uint32_t> > succs(n);
    std::vector<std::pair<std::uint32_t, std::size_t> > stack;
    stack.push_back(std::make_pair(f.entry.v, std::size_t(0)));
    seen[f.entry.v] = 1;
    succs[f.entry.v] = successors(f, f.entry.v);
    while (!stack.empty()) {
        const std::uint32_t b = stack.back().first;
        if (stack.back().second < succs[b].size()) {
            const std::uint32_t s = succs[b][stack.back().second++];
            if (s == 0 || s >= n) continue;
            preds[s].push_back(b);
            if (!seen[s]) { seen[s] = 1; succs[s] = successors(f, s); stack.push_back(std::make_pair(s, std::size_t(0))); }
        } else {
            rpo.push_back(b);
            stack.pop_back();
        }
    }
    std::reverse(rpo.begin(), rpo.end());
    for (std::size_t i = 0; i < rpo.size(); i++) order[rpo[i]] = static_cast<std::uint32_t>(i + 1);
    idom[f.entry.v] = f.entry.v;
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 1; i < rpo.size(); i++) {
            const std::uint32_t b = rpo[i];
            std::uint32_t best = 0;
            for (std::uint32_t p : preds[b]) {
                if (idom[p] == 0) continue;
                best = best == 0 ? p : intersect(best, p);
            }
            if (best != 0 && idom[b] != best) { idom[b] = best; changed = true; }
        }
    }
}

std::uint32_t Dominators::intersect(std::uint32_t a, std::uint32_t b) const {
    while (a != b) {
        while (order[a] > order[b]) a = idom[a];
        while (order[b] > order[a]) b = idom[b];
    }
    return a;
}

bool Dominators::dominates(std::uint32_t a, std::uint32_t b) const {
    while (b != a && b != entry_ && idom[b] != 0) b = idom[b];
    return a == b;
}

std::vector<std::vector<std::uint32_t> > Dominators::frontiers() const {
    std::vector<std::vector<std::uint32_t> > df(order.size());
    for (std::uint32_t b : rpo) {
        if (preds[b].size() < 2) continue;
        for (std::uint32_t p : preds[b])
            for (std::uint32_t r = p; r != idom[b]; r = idom[r]) {
                if (std::find(df[r].begin(), df[r].end(), b) == df[r].end()) df[r].push_back(b);
                if (r == entry_) break;
            }
    }
    for (std::size_t i = 0; i < df.size(); i++) std::sort(df[i].begin(), df[i].end());
    return df;
}

}   // namespace ir
