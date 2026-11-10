// **CFG simplification** (M3b.4 round 3, design section 4): a branch on a constant made a jump, the blocks no root
// reaches removed, a block merged into its one predecessor that jumps to it, an empty block that only jumps forwarded.
// A pad, a funclet's block and a block the runtime enters are fixed (fixedBlocks): never removed, merged or forwarded.

#include "SsaOpt.h"

#include <utility>

namespace ir {

bool isJumpEdge(const Inst &term, std::size_t s) {
    return term.op == Op::Br || term.op == Op::CondBr || term.op == Op::Switch || (term.op == Op::Invoke && s == 0);
}

std::vector<char> fixedBlocks(const Function &f) {
    const std::size_t n = f.blockArena.size();
    std::vector<char> fixed(n, 0);
    // A Microsoft try (one with a catchswitch) names blocks by index: its exits and its pad on the catchpads, the
    // enclosing pad and the handlers' last block on the catchswitch. A pad's `cases` elsewhere are selector indices.
    bool microsoft = false;
    for (BlockId b : f.blocks) microsoft = microsoft || (f.block(b).term.valid() && f.inst(f.block(b).term).op == Op::CatchSwitch);
    const auto name = [&](long long b) { if (b > 0 && static_cast<std::size_t>(b) < n) fixed[static_cast<std::size_t>(b)] = 1; };
    for (BlockId b : f.blocks) {
        const Block &blk = f.block(b);
        if (blk.pad || blk.funclet != 0) fixed[b.v] = 1;
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const Inst &i = f.inst(id);
            if (microsoft && i.op == Op::CatchPad) { name(i.k); for (long long c : i.cases) name(c); }
            if (i.op == Op::CatchSwitch) { name(i.k); name(static_cast<long long>(i.bits)); }
            for (std::size_t s = 0; s < i.succ.size(); s++)
                if (!isJumpEdge(i, s) && i.succ[s].block.v < n) fixed[i.succ[s].block.v] = 1;
        }
    }
    return fixed;
}

namespace {

struct Simplifier {
    Function &f;
    PassLog &log;
    std::vector<char> fixed, removed;
    std::vector<std::uint32_t> termBlock;                                   // by instruction: the block it ends
    std::vector<std::vector<std::pair<InstId, std::uint32_t> > > into;     // by block: (terminator, successor), maybe stale
    std::vector<ValueId> repl;                                              // by value: a merged block's parameter's argument
    bool changed;

    Simplifier(Function &fn, PassLog &l) : f(fn), log(l), fixed(fixedBlocks(fn)), removed(fn.blockArena.size(), 0),
        repl(fn.values.size()), changed(false) {}

    ValueId resolve(ValueId v) const {
        while (v.valid() && v.v < repl.size() && repl[v.v].valid()) v = repl[v.v];
        return v;
    }
    Inst &term(std::uint32_t b) { return f.insts[f.blockArena[b].term.v]; }
    bool hasTerm(std::uint32_t b) const { return f.blockArena[b].term.valid(); }

    void edges() {
        termBlock.assign(f.insts.size(), 0);
        into.assign(f.blockArena.size(), std::vector<std::pair<InstId, std::uint32_t> >());
        for (BlockId b : f.blocks) {
            if (removed[b.v] || !hasTerm(b.v)) continue;
            const InstId t = f.block(b).term;
            termBlock[t.v] = b.v;
            for (std::uint32_t s = 0; s < f.inst(t).succ.size(); s++) into[f.inst(t).succ[s].block.v].push_back(std::make_pair(t, s));
        }
    }
    // The edges into b now: from a block still in the body, still to b, each once.
    std::vector<std::pair<InstId, std::uint32_t> > edgesInto(std::uint32_t b) {
        std::vector<std::pair<InstId, std::uint32_t> > out;
        for (const std::pair<InstId, std::uint32_t> &e : into[b]) {
            const Inst &t = f.inst(e.first);
            const std::uint32_t from = termBlock[e.first.v];
            if (from == 0 || removed[from] || f.block(BlockId(from)).term.v != e.first.v) continue;
            if (e.second >= t.succ.size() || t.succ[e.second].block.v != b) continue;
            bool seen = false;
            for (const std::pair<InstId, std::uint32_t> &o : out) seen = seen || (o.first.v == e.first.v && o.second == e.second);
            if (!seen) out.push_back(e);
        }
        return out;
    }

    bool constant(ValueId v, long long &k) const {
        v = resolve(v);
        if (!v.valid() || !f.value(v).def.valid() || f.inst(f.value(v).def).op != Op::IConst) return false;
        k = f.inst(f.value(v).def).k;
        return true;
    }
    bool sameArgs(const Succ &a, const Succ &b) const {
        if (a.block.v != b.block.v || a.count != b.count) return false;
        for (std::uint32_t k = 0; k < a.count; k++) if (resolve(f.arg(a, k)).v != resolve(f.arg(b, k)).v) return false;
        return true;
    }
    void toJump(Inst &t, std::size_t keep, int &count) {
        const Succ s = t.succ[keep];
        t.op = Op::Br;
        t.count = 0;
        t.cases.clear();
        t.succ.assign(1, s);
        count++;
        changed = true;
    }

    // A condbr or switch whose value is a constant, or a condbr both of whose edges are one edge, becomes a br.
    void branches() {
        for (BlockId b : f.blocks) {
            if (removed[b.v] || !hasTerm(b.v)) continue;
            Inst &t = term(b.v);
            long long k = 0;
            if (t.op == Op::CondBr) {
                if (constant(f.operand(t, 0), k)) toJump(t, k != 0 ? 0 : 1, log.constBranches);
                else if (sameArgs(t.succ[0], t.succ[1])) toJump(t, 0, log.sameBranches);
            } else if (t.op == Op::Switch && constant(f.operand(t, 0), k)) {
                std::size_t to = 0;
                for (std::size_t c = 0; c < t.cases.size(); c++) if (t.cases[c] == k) { to = c + 1; break; }
                toJump(t, to, log.constBranches);
            }
        }
    }

    // The blocks neither the entry nor a fixed block reaches leave the body.
    void unreachable() {
        std::vector<char> seen(f.blockArena.size(), 0);
        std::vector<std::uint32_t> work;
        for (BlockId b : f.blocks)
            if (!removed[b.v] && (b.v == f.entry.v || fixed[b.v])) { seen[b.v] = 1; work.push_back(b.v); }
        while (!work.empty()) {
            const std::uint32_t b = work.back();
            work.pop_back();
            if (!hasTerm(b)) continue;
            for (const Succ &s : term(b).succ)
                if (s.block.v < seen.size() && !seen[s.block.v]) { seen[s.block.v] = 1; work.push_back(s.block.v); }
        }
        for (BlockId b : f.blocks)
            if (!removed[b.v] && !seen[b.v]) { removed[b.v] = 1; log.unreachable++; changed = true; }
    }

    // Block b, whose one edge in is a br from a, joins a: its parameters are that br's arguments, its instructions
    // follow a's, keeping their source positions, and its terminator is a's.
    void merge(std::uint32_t a, std::uint32_t b) {
        Block &into_ = f.blockArena[a], &from = f.blockArena[b];
        const Inst &br = f.inst(into_.term);
        for (std::size_t p = 0; p < from.params.size(); p++) repl[from.params[p].v] = resolve(f.arg(br.succ[0], static_cast<std::uint32_t>(p)));
        if (!from.insts.empty() && f.inst(from.insts[0]).pos == 0) f.insts[from.insts[0].v].pos = br.pos;
        into_.insts.insert(into_.insts.end(), from.insts.begin(), from.insts.end());
        into_.term = from.term;
        termBlock[from.term.v] = a;
        from.insts.clear();
        from.term = InstId();
        removed[b] = 1;
        log.merged++;
        changed = true;
    }
    void merges() {
        for (BlockId a : f.blocks) {
            for (;;) {
                if (removed[a.v] || fixed[a.v] || !hasTerm(a.v)) break;
                const Inst &t = term(a.v);
                if (t.op != Op::Br) break;
                const std::uint32_t b = t.succ[0].block.v;
                if (b == a.v || b == f.entry.v || fixed[b] || removed[b] || f.block(BlockId(b)).funclet != f.block(a).funclet) break;
                if (!hasTerm(b) || edgesInto(b).size() != 1) break;
                merge(a.v, b);
            }
        }
    }

    // An empty block that only jumps on: each edge into it goes where it goes, its parameters replaced by the edge's
    // arguments. Not where a parameter of it is read but by that jump, which the merged edge could not carry.
    void forwards() {
        std::vector<char> readElsewhere(f.values.size(), 0);
        for (BlockId b : f.blocks) {
            if (removed[b.v]) continue;
            const Block &blk = f.block(b);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!id.valid()) continue;
                const Inst &i = f.inst(id);
                for (std::uint32_t n = 0; n < i.count; n++) readElsewhere[resolve(f.operand(i, n)).v] = 1;
                const bool own = i.op == Op::Br && blk.insts.empty();
                for (const Succ &s : i.succ)
                    for (std::uint32_t a = 0; a < s.count; a++) {
                        const ValueId v = resolve(f.arg(s, a));
                        if (!own || f.value(v).param.v != b.v) readElsewhere[v.v] = 1;
                    }
            }
        }
        for (BlockId e : f.blocks) {
            if (removed[e.v] || fixed[e.v] || e.v == f.entry.v || !hasTerm(e.v)) continue;
            const Block &blk = f.block(e);
            if (!blk.insts.empty() || term(e.v).op != Op::Br || term(e.v).succ[0].block.v == e.v) continue;
            bool ok = true;
            for (ValueId p : blk.params) if (readElsewhere[p.v]) ok = false;
            const std::vector<std::pair<InstId, std::uint32_t> > in = edgesInto(e.v);
            for (const std::pair<InstId, std::uint32_t> &x : in)
                if (f.block(BlockId(termBlock[x.first.v])).funclet != blk.funclet || f.inst(x.first).succ[x.second].count != blk.params.size()) ok = false;
            if (!ok || in.empty()) continue;
            const Succ out = term(e.v).succ[0];
            for (const std::pair<InstId, std::uint32_t> &x : in) {
                Succ &s = f.insts[x.first.v].succ[x.second];
                std::vector<ValueId> args;
                for (std::uint32_t a = 0; a < out.count; a++) {
                    const ValueId v = resolve(f.arg(out, a));
                    args.push_back(f.value(v).param.v == e.v ? resolve(f.arg(s, f.value(v).index)) : v);
                }
                s.block = out.block;
                s.first = static_cast<std::uint32_t>(f.operands.size());
                s.count = out.count;
                f.operands.insert(f.operands.end(), args.begin(), args.end());
                into[out.block.v].push_back(x);
            }
            removed[e.v] = 1;
            log.forwarded++;
            changed = true;
        }
    }

    // Uses re-pointed; the removed blocks out of the body, and each value they define that the body does not read
    // given no type (no slot), its instruction emptied; a merged block's parameters likewise.
    void finish() {
        for (std::size_t k = 0; k < f.operands.size(); k++) f.operands[k] = resolve(f.operands[k]);
        std::vector<char> read(f.values.size(), 0);
        std::vector<BlockId> body;
        for (BlockId b : f.blocks) {
            if (removed[b.v]) continue;
            body.push_back(b);
            const Block &blk = f.block(b);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!id.valid()) continue;
                const Inst &i = f.inst(id);
                for (std::uint32_t n = 0; n < i.count; n++) read[f.operand(i, n).v] = 1;
                for (const Succ &s : i.succ) for (std::uint32_t a = 0; a < s.count; a++) read[f.arg(s, a).v] = 1;
            }
        }
        for (BlockId b : f.blocks) {
            if (!removed[b.v]) continue;
            Block &blk = f.blockArena[b.v];
            for (ValueId p : blk.params) if (!read[p.v]) f.values[p.v].type = TypeId();
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!id.valid()) continue;
                Inst &i = f.insts[id.v];
                if (i.result.valid() && read[i.result.v]) continue;
                if (i.result.valid()) f.values[i.result.v].type = TypeId();
                i.op = Op::Undef;
                i.count = 0;
                i.succ.clear();
            }
        }
        f.blocks.swap(body);
    }
};

}   // namespace

bool cfg(Function &f, PassLog &log) {
    if (!f.entry.valid()) return false;
    Simplifier s(f, log);
    // Each round that changes anything removes a block or a successor, so the rounds end.
    bool any = false;
    do {
        s.changed = false;
        s.branches();
        s.unreachable();
        s.edges();
        s.merges();
        s.forwards();
        any = any || s.changed;
    } while (s.changed);
    if (any) s.finish();
    return any;
}

}   // namespace ir
