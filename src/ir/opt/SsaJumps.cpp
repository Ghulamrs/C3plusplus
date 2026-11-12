// **Jump threading** (M3b.4 round 5, design section 4; OptJumps.cpp's shape on SSA): a block that only tests - its
// instructions constants, compares and integer conversions read nowhere else - reached by an edge passing it constants
// that decide the test has that edge sent to the successor picked, with the arguments that successor takes.

#include "SsaOpt.h"

#include <map>

namespace ir {

namespace {

struct Threader {
    Function &f;
    const TypeTable &types;
    const Target &target;
    PassLog &log;
    std::vector<char> fixed;
    std::vector<std::uint32_t> defBlock;                // by value: the block whose parameter or result it is
    std::vector<std::vector<std::uint32_t> > usedIn;    // by value: the other blocks reading it
    std::vector<std::uint32_t> edgesIn;                 // by block: the edges into it from the body

    Threader(Function &fn, const TypeTable &t, const Target &tg, PassLog &l) : f(fn), types(t), target(tg), log(l),
        fixed(fixedBlocks(fn)) {}

    template <typename Visit> void each(const Block &blk, Visit visit) const {
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (id.valid()) visit(f.inst(id));
        }
    }
    void uses() {
        defBlock.assign(f.values.size(), 0);
        usedIn.assign(f.values.size(), std::vector<std::uint32_t>());
        edgesIn.assign(f.blockArena.size(), 0);
        for (BlockId b : f.blocks) {
            for (ValueId p : f.block(b).params) defBlock[p.v] = b.v;
            each(f.block(b), [&](const Inst &i) { if (i.result.valid()) defBlock[i.result.v] = b.v; });
        }
        const auto note = [&](ValueId v, std::uint32_t b) { if (v.valid() && defBlock[v.v] != b) usedIn[v.v].push_back(b); };
        for (BlockId b : f.blocks)
            each(f.block(b), [&](const Inst &i) {
                for (std::uint32_t n = 0; n < i.count; n++) note(f.operand(i, n), b.v);
                for (const Succ &s : i.succ) {
                    edgesIn[s.block.v]++;
                    for (std::uint32_t a = 0; a < s.count; a++) note(f.arg(s, a), b.v);
                }
            });
    }

    bool constant(ValueId v, long long &k) const {
        if (!v.valid() || !f.value(v).def.valid() || f.inst(f.value(v).def).op != Op::IConst) return false;
        k = f.inst(f.value(v).def).k;
        return true;
    }
    bool integer(TypeId t) const { return t.valid() && types.type(t)->isInteger(); }

    // Block t ends in a condbr or switch, has parameters, and its instructions are the test's, read nowhere else.
    bool testsOnly(std::uint32_t t) const {
        const Block &blk = f.block(BlockId(t));
        if (t == f.entry.v || fixed[t] || !blk.term.valid() || blk.params.empty()) return false;
        const Op op = f.inst(blk.term).op;
        if (op != Op::CondBr && op != Op::Switch) return false;
        for (InstId id : blk.insts) {
            const Inst &i = f.inst(id);
            if (i.isVolatile || !i.result.valid() || !usedIn[i.result.v].empty()) return false;
            if (i.op != Op::IConst && i.op != Op::ICmp && i.op != Op::Trunc && i.op != Op::SExt && i.op != Op::ZExt) return false;
        }
        return true;
    }

    // The successor t's terminator takes when edge e passes its parameters, or -1 where that is not decided.
    int pick(std::uint32_t t, const Succ &e) const {
        const Block &blk = f.block(BlockId(t));
        std::map<std::uint32_t, long long> known;
        for (std::uint32_t p = 0; p < blk.params.size(); p++) {
            long long k = 0;
            if (constant(f.arg(e, p), k)) known[blk.params[p].v] = k;
        }
        if (known.empty()) return -1;
        const auto value = [&](ValueId v, long long &k) -> bool {
            const std::map<std::uint32_t, long long>::const_iterator it = known.find(v.v);
            if (it == known.end()) return constant(v, k);
            k = it->second;
            return true;
        };
        for (InstId id : blk.insts) {
            const Inst &i = f.inst(id);
            long long x = 0, y = 0, out = 0;
            if (i.op == Op::ICmp) {
                if (value(f.operand(i, 0), x) && value(f.operand(i, 1), y) &&
                    foldCompare(i.cc, x, y, *types.type(f.value(f.operand(i, 0)).type), target, out))
                    known[i.result.v] = out;
            } else if (i.op != Op::IConst && integer(f.value(f.operand(i, 0)).type) && integer(i.type) &&
                       value(f.operand(i, 0), x) && foldIntConversion(x, *types.type(i.type), target, out)) {
                known[i.result.v] = out;
            }
        }
        const Inst &term = f.inst(blk.term);
        long long c = 0;
        if (!value(f.operand(term, 0), c)) return -1;
        if (term.op == Op::CondBr) return c != 0 ? 0 : 1;
        for (std::size_t k = 0; k < term.cases.size(); k++) if (term.cases[k] == c) return static_cast<int>(k + 1);
        return 0;
    }

    std::vector<char> reachableFrom(std::uint32_t b) const {
        std::vector<char> seen(f.blockArena.size(), 0);
        std::vector<std::uint32_t> work(1, b);
        seen[b] = 1;
        while (!work.empty()) {
            const std::uint32_t x = work.back();
            work.pop_back();
            if (!f.block(BlockId(x)).term.valid()) continue;
            for (std::uint32_t y : successors(f, x))
                if (!seen[y]) { seen[y] = 1; work.push_back(y); }
        }
        return seen;
    }
    void setArgs(Succ &s, const std::vector<ValueId> &args) {
        s.first = static_cast<std::uint32_t>(f.operands.size());
        s.count = static_cast<std::uint32_t>(args.size());
        f.operands.insert(f.operands.end(), args.begin(), args.end());
    }

    // Edge s of block pb threaded past the block it enters, if that block's test is decided there.
    bool thread(const Dominators &dom, std::uint32_t pb, std::size_t s) {
        const InstId tid = f.block(BlockId(pb)).term;
        if (!isJumpEdge(f.inst(tid), s)) return false;
        const Succ e = f.inst(tid).succ[s];
        const std::uint32_t t = e.block.v;
        if (t == pb || !dom.reachable(pb) || !testsOnly(t) || e.count != f.block(e.block).params.size()) return false;
        const int k = pick(t, e);
        if (k < 0) return false;
        const InstId testId = f.block(e.block).term;
        const Succ to = f.inst(testId).succ[static_cast<std::size_t>(k)];
        const std::uint32_t S = to.block.v;
        if (S == t || S == f.entry.v || fixed[S] || f.block(to.block).funclet != f.block(BlockId(pb)).funclet) return false;
        // A parameter of t read below it: where S dominates the read, it becomes a parameter of S (S's one edge in being
        // t's); a read elsewhere that S reaches would lose its definition, which refuses the edge.
        std::vector<ValueId> moved;
        std::vector<char> reach;
        for (ValueId p : f.block(e.block).params) {
            bool below = false;
            for (std::uint32_t u : usedIn[p.v]) {
                if (dom.reachable(u) && dom.dominates(S, u)) {
                    if (fixed[u]) return false;
                    below = true;
                    continue;
                }
                if (reach.empty()) reach = reachableFrom(S);
                if (reach[u]) return false;
            }
            if (below) moved.push_back(p);
        }
        if (!moved.empty() && edgesIn[S] != 1) return false;
        std::vector<ValueId> args;
        for (std::uint32_t a = 0; a < to.count; a++) {
            const ValueId v = f.arg(to, a);
            if (defBlock[v.v] != t) args.push_back(v);
            else if (f.value(v).param.v == t) args.push_back(f.arg(e, f.value(v).index));
            else return false;
        }
        std::vector<ValueId> fromT(f.operands.begin() + to.first, f.operands.begin() + to.first + to.count);
        std::vector<ValueId> repl(f.values.size());
        for (ValueId p : moved) {
            Value q;
            q.type = f.value(p).type;
            q.param = to.block;
            q.index = static_cast<std::uint16_t>(f.block(to.block).params.size());
            repl[p.v] = ValueId(static_cast<std::uint32_t>(f.values.size()));
            f.values.push_back(q);
            f.blockArena[S].params.push_back(repl[p.v]);
            fromT.push_back(p);
            args.push_back(f.arg(e, f.value(p).index));
        }
        (f.inst(testId).op == Op::Switch ? log.threadedSwitch : log.threaded)++;
        setArgs(f.insts[testId.v].succ[static_cast<std::size_t>(k)], fromT);
        Succ &w = f.insts[tid.v].succ[s];
        w.block = to.block;
        setArgs(w, args);
        if (moved.empty()) return true;
        // Every read S dominates, the edge just made among them where pb is below S, reads S's parameter.
        for (BlockId u : f.blocks) {
            if (!dom.reachable(u.v) || !dom.dominates(S, u.v)) continue;
            const Block &blk = f.block(u);
            for (std::size_t n = 0; n <= blk.insts.size(); n++) {
                const InstId id = n < blk.insts.size() ? blk.insts[n] : blk.term;
                if (!id.valid()) continue;
                Inst &i = f.insts[id.v];
                const auto swap = [&](ValueId &v) { if (v.v < repl.size() && repl[v.v].valid()) v = repl[v.v]; };
                for (std::uint32_t o = 0; o < i.count; o++) swap(f.operands[i.first + o]);
                for (const Succ &sc : i.succ) for (std::uint32_t a = 0; a < sc.count; a++) swap(f.operands[sc.first + a]);
            }
        }
        return true;
    }

    // One edge at a time, the facts made again after each; at most a block's worth of edges, so a cycle of tests ends.
    bool run() {
        bool any = false;
        for (std::size_t round = 0; round < f.blocks.size(); round++) {
            uses();
            const Dominators dom(f);
            bool done = false;
            for (std::size_t b = 0; b < f.blocks.size() && !done; b++) {
                const std::uint32_t pb = f.blocks[b].v;
                if (fixed[pb] || !f.block(f.blocks[b]).term.valid()) continue;
                for (std::size_t s = 0; s < f.inst(f.block(f.blocks[b]).term).succ.size() && !done; s++) done = thread(dom, pb, s);
            }
            if (!done) break;
            any = true;
        }
        return any;
    }
};

}   // namespace

bool jumps(Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    if (!f.entry.valid()) return false;
    Threader p(f, types, target, log);
    return p.run();
}

}   // namespace ir
