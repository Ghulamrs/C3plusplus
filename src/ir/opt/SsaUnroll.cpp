// **Unrolling** (M3b.5 round 4, design section 4): an innermost loop counted by constants - a head parameter entering
// as one, coming round as itself plus one, its one exit a condbr on it against one - copied whole where its turns times
// its body fit OptCosts.h's budget, else 4 or 2 times with the exit's test kept in the one copy the count leaves by.

#include "SsaOpt.h"
#include "../../optimizer/OptCosts.h"

#include <memory>

namespace ir {

namespace {

// What makes a loop counted: the exit's block and the edge that stays, the counter and its next value, and the turns.
struct Counted {
    std::uint32_t exit;
    std::size_t stay;
    ValueId p, a;
    long long step, by;   // by: the add's or sub's constant, step its signed effect
    long long turns;    // the back edges taken before the exit's test leaves
};

struct Unroller {
    Function &f;
    const TypeTable &types;
    const Target &target;
    PassLog &log;
    std::vector<char> fixed;
    std::vector<std::uint32_t> blockOf;     // by value: the block defining it

    Unroller(Function &fn, const TypeTable &t, const Target &tg, PassLog &l) : f(fn), types(t), target(tg), log(l) {}

    bool constantOf(ValueId v, long long &k) const {
        if (!v.valid() || !f.value(v).def.valid() || f.inst(f.value(v).def).op != Op::IConst) return false;
        k = f.inst(f.value(v).def).k;
        return true;
    }
    void defined() {
        blockOf.assign(f.values.size(), 0);
        for (BlockId b : f.blocks) {
            for (ValueId p : f.block(b).params) blockOf[p.v] = b.v;
            for (InstId id : f.block(b).insts) if (f.inst(id).result.valid()) blockOf[f.inst(id).result.v] = b.v;
        }
    }
    bool inLoop(const Loop &l, ValueId v) const { return v.v < blockOf.size() && blockOf[v.v] != 0 && l.in[blockOf[v.v]]; }
    // The values an integer type holds that no turn wraps past: its own range, at 64 bits half of it.
    bool range(TypeId t, long long &lo, long long &hi) const {
        const Type &ty = *types.type(t);
        if (!ty.isInteger() || ty.isBool()) return false;
        const int bits = ty.size(target) * 8;
        if (bits >= 64) { lo = -(1LL << 62); hi = 1LL << 62; return true; }
        lo = ty.isSigned(target) ? -(1LL << (bits - 1)) : 0;
        hi = ty.isSigned(target) ? (1LL << (bits - 1)) - 1 : (1LL << bits) - 1;
        return true;
    }

    // **The body's measure**, in instructions and terminators; -1 where a block or an instruction keeps it whole.
    int measure(const Loop &l) const {
        int n = 0;
        for (std::uint32_t b : l.blocks) {
            const Block &blk = f.blockArena[b];
            if (fixed[b] || blk.funclet != 0 || blk.pad || !blk.term.valid()) return -1;
            const Op t = f.inst(blk.term).op;
            if (t != Op::Br && t != Op::CondBr && t != Op::Switch) return -1;
            for (InstId id : blk.insts) {
                const Inst &i = f.inst(id);
                if (i.isVolatile || i.op == Op::Call || i.op == Op::Trap || i.op == Op::Alloca || i.op == Op::VaStart ||
                    i.op == Op::VaArg || i.op == Op::VaEnd || i.op == Op::CatchPad || i.op == Op::CleanupPad) return -1;
            }
            n += static_cast<int>(blk.insts.size()) + 1;
        }
        return n;
    }

    // **Counted**: one latch, one exit - a condbr in a block every turn passes, testing the counter, its next value or
    // either truncated against a constant - and the counter entering as a constant; its turns found by running the test.
    bool counted(const Loop &l, const Dominators &dom, Counted &c) {
        if (l.pre == 0 || l.latches.size() != 1) return false;
        int exits = 0;
        for (std::uint32_t b : l.blocks) {
            const Inst &t = f.inst(f.blockArena[b].term);
            for (std::size_t s = 0; s < t.succ.size(); s++)
                if (!l.in[t.succ[s].block.v]) { exits++; c.exit = b; c.stay = 1 - s; }
        }
        if (exits != 1 || f.inst(f.blockArena[c.exit].term).op != Op::CondBr || !dom.dominates(c.exit, l.latches[0])) return false;
        const Inst &br = f.inst(f.blockArena[c.exit].term);
        const ValueId cond = f.operand(br, 0);
        if (!f.value(cond).def.valid() || f.inst(f.value(cond).def).op != Op::ICmp) return false;
        const Inst &cmp = f.inst(f.value(cond).def);
        long long bound = 0;
        const int side = constantOf(f.operand(cmp, 1), bound) ? 0 : constantOf(f.operand(cmp, 0), bound) ? 1 : -1;
        if (side < 0) return false;
        const ValueId tested = f.operand(cmp, static_cast<std::uint32_t>(side));
        ValueId x = tested;
        if (f.value(x).def.valid() && f.inst(f.value(x).def).op == Op::Trunc) x = f.operand(f.inst(f.value(x).def), 0);
        // The back edge's argument for each head parameter, then the counter: x itself, or the add x is.
        const Inst &latch = f.inst(f.blockArena[l.latches[0]].term);
        int back = -1;
        for (std::size_t s = 0; s < latch.succ.size(); s++) if (latch.succ[s].block.v == l.head) back = back == -1 ? static_cast<int>(s) : -2;
        if (back < 0) return false;
        const Block &head = f.blockArena[l.head];
        for (std::size_t j = 0; j < head.params.size() && !c.p.valid(); j++) {
            const ValueId a = f.arg(latch.succ[static_cast<std::size_t>(back)], static_cast<std::uint32_t>(j));
            if (x.v != head.params[j].v && x.v != a.v) continue;
            if (!f.value(a).def.valid() || (f.inst(f.value(a).def).op != Op::Add && f.inst(f.value(a).def).op != Op::Sub) || !inLoop(l, a)) return false;
            const Inst &add = f.inst(f.value(a).def);
            const int k = f.operand(add, 0).v == head.params[j].v ? 1 : add.op == Op::Add && f.operand(add, 1).v == head.params[j].v ? 0 : -1;
            if (k < 0 || !constantOf(f.operand(add, static_cast<std::uint32_t>(k)), c.step) || c.step == 0) return false;
            c.by = c.step;
            if (add.op == Op::Sub) c.step = -c.step;
            c.p = head.params[j];
            c.a = a;
            long long start = 0;
            if (!constantOf(f.arg(f.inst(f.blockArena[l.pre].term).succ[0], static_cast<std::uint32_t>(j)), start)) return false;
            long long lo = 0, hi = 0, tlo = 0, thi = 0;
            if (!range(f.value(c.p).type, lo, hi) || !range(f.value(tested).type, tlo, thi)) return false;
            lo = lo > tlo ? lo : tlo;
            hi = hi < thi ? hi : thi;
            const Type &tt = *types.type(f.value(tested).type);
            for (long long n = 0; n < (1 << 16); n++) {
                const long long pv = start + n * c.step, av = pv + c.step, xv = x.v == c.p.v ? pv : av;
                long long out = 0;
                if (pv < lo || pv > hi || (x.v == c.a.v && (av < lo || av > hi))) return false;
                if (!foldCompare(cmp.cc, side == 0 ? xv : bound, side == 0 ? bound : xv, tt, target, out)) return false;
                if ((out != 0 ? 0u : 1u) != c.stay) { c.turns = n; break; }
                if (n + 1 == (1 << 16)) return false;
            }
        }
        if (!c.p.valid()) return false;
        // A value the loop defines and a block outside it reads must dominate the exit: the copy that leaves defines it.
        for (BlockId b : f.blocks) {
            if (l.in[b.v]) continue;
            const Block &blk = f.block(b);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const Inst &i = f.inst(k < blk.insts.size() ? blk.insts[k] : blk.term);
                for (std::uint32_t n = 0; n < i.count; n++)
                    if (inLoop(l, f.operand(i, n)) && !dom.dominates(blockOf[f.operand(i, n).v], c.exit)) return false;
                for (const Succ &s : i.succ)
                    for (std::uint32_t n = 0; n < s.count; n++)
                        if (inLoop(l, f.arg(s, n)) && !dom.dominates(blockOf[f.arg(s, n).v], c.exit)) return false;
            }
        }
        return true;
    }

    ValueId newValue(TypeId type, InstId def, BlockId param, std::size_t index) {
        Value v;
        v.type = type;
        v.def = def;
        v.param = param;
        v.index = static_cast<std::uint16_t>(index);
        f.values.push_back(v);
        return ValueId(static_cast<std::uint32_t>(f.values.size() - 1));
    }
    InstId constant(TypeId type, long long k, const Inst &site) {
        Inst i;
        i.op = Op::IConst;
        i.type = type;
        i.k = k;
        i.at = site.at;
        i.pos = site.pos;
        const InstId id(static_cast<std::uint32_t>(f.insts.size()));
        i.result = newValue(type, id, BlockId(), 0);
        f.insts.push_back(i);
        return id;
    }
    void toJump(Inst &t, std::size_t keep) {
        const Succ s = t.succ[keep];
        t.op = Op::Br;
        t.count = 0;
        t.succ.assign(1, s);
    }

    // **The copies**: copy 0 is the loop as it stands; copy c's counter is the first's plus c steps, so the back edge
    // passes the first's plus `copies` steps. Each back edge enters the next copy, the last's the first (or, copied whole,
    // itself, never reached); the exit's test stays in the copy the count leaves by and is a jump in the others.
    void copy(const Loop &l, const Counted &c, std::size_t copies, bool whole) {
        const std::size_t leaves = whole ? static_cast<std::size_t>(c.turns) : static_cast<std::size_t>(c.turns) % copies;
        const std::size_t nv = f.values.size();
        std::vector<std::vector<ValueId> > vmap(copies, std::vector<ValueId>());
        std::vector<std::vector<std::uint32_t> > bmap(copies, std::vector<std::uint32_t>(f.blockArena.size(), 0));
        for (std::uint32_t b : l.blocks) bmap[0][b] = b;
        std::vector<BlockId> made;
        const TypeId ct = f.value(c.p).type;
        const InstId addId = f.value(c.a).def;
        for (std::size_t k = 1; k < copies; k++) {
            vmap[k].assign(nv, ValueId());
            for (std::uint32_t b : l.blocks) {
                const BlockId nb(static_cast<std::uint32_t>(f.blockArena.size()));
                Block blk;
                const Block &from = f.blockArena[b];
                blk.funclet = from.funclet;
                for (std::size_t j = 0; j < from.params.size(); j++) {
                    blk.params.push_back(newValue(f.value(from.params[j]).type, InstId(), nb, j));
                    vmap[k][from.params[j].v] = blk.params.back();
                }
                const std::vector<InstId> insts = from.insts;
                const InstId term = from.term;
                f.blockArena.push_back(blk);
                if (b == l.head) {
                    // The counter: the first copy's plus k steps, and its next value plus k + 1.
                    const Inst site = f.inst(addId);
                    const InstId kc = constant(ct, c.by * static_cast<long long>(k), site);
                    Inst q = site;
                    q.first = static_cast<std::uint32_t>(f.operands.size());
                    q.count = 2;
                    f.operands.push_back(c.p);
                    f.operands.push_back(f.inst(kc).result);
                    const InstId qi(static_cast<std::uint32_t>(f.insts.size()));
                    q.result = newValue(ct, qi, BlockId(), 0);
                    f.insts.push_back(q);
                    vmap[k][c.p.v] = q.result;
                    f.blockArena[nb.v].insts.push_back(kc);
                    f.blockArena[nb.v].insts.push_back(qi);
                    f.blockArena[nb.v].insts.push_back(constant(ct, c.by * static_cast<long long>(k + 1), site));
                }
                for (InstId id : insts) {
                    Inst i = f.inst(id);
                    const InstId ni(static_cast<std::uint32_t>(f.insts.size()));
                    if (i.result.valid()) { vmap[k][i.result.v] = newValue(f.value(i.result).type, ni, BlockId(), 0); i.result = vmap[k][i.result.v]; }
                    f.insts.push_back(i);
                    f.blockArena[nb.v].insts.push_back(ni);
                }
                Inst t = f.inst(term);
                f.blockArena[nb.v].term = InstId(static_cast<std::uint32_t>(f.insts.size()));
                f.insts.push_back(t);
                bmap[k][b] = nb.v;
                made.push_back(nb);
            }
        }
        // The operands, each copy's read through its map; the counter's next value from the first copy's counter.
        const auto mapped = [&](std::size_t k, ValueId v) { return k == 0 || v.v >= nv || !vmap[k][v.v].valid() ? v : vmap[k][v.v]; };
        const auto rewrite = [&](std::size_t k, Inst &i) {
            const std::uint32_t first = static_cast<std::uint32_t>(f.operands.size());
            for (std::uint32_t n = 0; n < i.count; n++) f.operands.push_back(mapped(k, f.operands[i.first + n]));
            i.first = first;
            for (Succ &s : i.succ) {
                const std::uint32_t at = static_cast<std::uint32_t>(f.operands.size());
                for (std::uint32_t n = 0; n < s.count; n++) f.operands.push_back(mapped(k, f.operands[s.first + n]));
                s.first = at;
            }
        };
        for (std::size_t k = 1; k < copies; k++) {
            const std::uint32_t head = bmap[k][l.head];
            for (std::uint32_t b : l.blocks) {
                Block &blk = f.blockArena[bmap[k][b]];
                for (std::size_t n = b == l.head ? 3 : 0; n < blk.insts.size(); n++) {
                    Inst &i = f.insts[blk.insts[n].v];
                    rewrite(k, i);
                    if (i.result.valid() && i.result.v == vmap[k][c.a.v].v) {
                        const bool left = f.operands[i.first].v == vmap[k][c.p.v].v;
                        f.operands[i.first + (left ? 0 : 1)] = c.p;
                        f.operands[i.first + (left ? 1 : 0)] = f.inst(f.blockArena[head].insts[2]).result;
                    }
                }
                rewrite(k, f.insts[blk.term.v]);
            }
        }
        // The edges: within a copy, to its own blocks; a back edge to the next copy.
        for (std::size_t k = 0; k < copies; k++) {
            for (std::uint32_t b : l.blocks) {
                Inst &t = f.insts[f.blockArena[bmap[k][b]].term.v];
                for (Succ &s : t.succ) {
                    if (!l.in[s.block.v]) continue;
                    if (s.block.v != l.head) { s.block = BlockId(bmap[k][s.block.v]); continue; }
                    if (b != l.latches[0]) { s.block = BlockId(bmap[k][l.head]); continue; }
                    s.block = BlockId(k + 1 < copies ? bmap[k + 1][l.head] : whole ? bmap[k][l.head] : l.head);
                }
            }
            Inst &e = f.insts[f.blockArena[bmap[k][c.exit]].term.v];
            if (k != leaves) toJump(e, c.stay);
            else if (whole) toJump(e, 1 - c.stay);
        }
        // Outside, a value of the loop is the leaving copy's.
        if (leaves != 0)
            for (BlockId b : f.blocks) {
                if (l.in[b.v]) continue;
                Block &blk = f.blockArena[b.v];
                for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                    Inst &i = f.insts[(k < blk.insts.size() ? blk.insts[k] : blk.term).v];
                    for (std::uint32_t n = 0; n < i.count; n++) f.operands[i.first + n] = mapped(leaves, f.operands[i.first + n]);
                    for (const Succ &s : i.succ)
                        for (std::uint32_t n = 0; n < s.count; n++) f.operands[s.first + n] = mapped(leaves, f.operands[s.first + n]);
                }
            }
        // The copies placed after the loop's last block, in order.
        std::size_t last = 0;
        for (std::size_t k = 0; k < f.blocks.size(); k++) if (l.in[f.blocks[k].v]) last = k;
        f.blocks.insert(f.blocks.begin() + static_cast<std::ptrdiff_t>(last + 1), made.begin(), made.end());
        if (whole) log.unrolledFull++;
        else log.unrolledPartial++;
    }

    bool run() {
        const std::unique_ptr<opt::Costs> costs = opt::Costs::forLevel(2);    // the pass runs at -O2 alone
        const int budget = costs->unrollBudget();
        std::vector<char> done(f.blockArena.size(), 0);
        bool changed = false;
        for (bool again = true; again;) {
            again = false;
            fixed = fixedBlocks(f);
            defined();
            const Dominators dom(f);
            const std::vector<Loop> loops = naturalLoops(f, dom, types, target, log);
            for (const Loop &l : loops) {
                bool inner = !done[l.head];
                for (const Loop &o : loops) inner = inner && (o.head == l.head || !l.in[o.head]);
                if (!inner) continue;
                done[l.head] = 1;
                const int size = measure(l);
                Counted c = Counted();
                if (size < 0 || 2 * size > budget || !counted(l, dom, c)) continue;
                const long long whole = (c.turns + 1) * size;
                copy(l, c, whole <= budget ? static_cast<std::size_t>(c.turns + 1) : 4 * size <= budget ? 4 : 2, whole <= budget);
                done.resize(f.blockArena.size(), 1);
                changed = again = true;
                break;
            }
        }
        return changed;
    }
};

}   // namespace

bool unroll(Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    if (!f.entry.valid()) return false;
    Unroller u(f, types, target, log);
    return u.run();
}

}   // namespace ir
