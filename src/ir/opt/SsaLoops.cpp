// **Loops** (M3b.5 round 3, design section 4): the natural loops from the dominators, each given a preheader; a pure
// instruction over values the loop never defines moved there, innermost first (OptHoist's, before allocation); an int
// counter read as a 64-bit index carried at 64 bits (OptLoops's widenCounterIn, ported), its sign extension gone.

#include "SsaOpt.h"

#include <algorithm>

namespace ir {

namespace {

// Where a handle stands in a list of handles, or the end.
template <class Id> typename std::vector<Id>::iterator place(std::vector<Id> &list, Id x) {
    typename std::vector<Id>::iterator it = list.begin();
    while (it != list.end() && it->v != x.v) ++it;
    return it;
}

struct Looper {
    Function &f;
    const TypeTable &types;
    const Target &target;
    PassLog &log;
    std::vector<char> fixed;
    std::vector<std::uint32_t> blockOf;     // by value: the block defining it

    Looper(Function &fn, const TypeTable &t, const Target &tg, PassLog &l) : f(fn), types(t), target(tg), log(l) {}

    // A back edge is an edge to a block that dominates its source; one loop per head, its back edges' bodies joined.
    std::vector<Loop> find(const Dominators &dom) {
        std::vector<Loop> out;
        std::vector<int> byHead(f.blockArena.size(), -1);
        for (std::uint32_t b : dom.rpo) {
            if (!f.blockArena[b].term.valid()) continue;
            const Inst &t = f.inst(f.blockArena[b].term);
            for (std::size_t s = 0; s < t.succ.size(); s++) {
                const std::uint32_t h = t.succ[s].block.v;
                if (!dom.reachable(h) || !dom.dominates(h, b)) continue;
                if (byHead[h] < 0) {
                    byHead[h] = static_cast<int>(out.size());
                    out.push_back(Loop());
                    out.back().head = h;
                    out.back().pre = 0;
                    out.back().in.assign(f.blockArena.size(), 0);
                    out.back().in[h] = 1;
                }
                Loop &l = out[static_cast<std::size_t>(byHead[h])];
                if (std::find(l.latches.begin(), l.latches.end(), b) == l.latches.end()) l.latches.push_back(b);
                std::vector<std::uint32_t> work;
                if (!l.in[b]) { l.in[b] = 1; work.push_back(b); }
                while (!work.empty()) {
                    const std::uint32_t x = work.back();
                    work.pop_back();
                    for (std::uint32_t p : dom.preds[x]) if (!l.in[p]) { l.in[p] = 1; work.push_back(p); }
                }
            }
        }
        for (Loop &l : out) {
            for (std::uint32_t b : dom.rpo) if (l.in[b]) l.blocks.push_back(b);
            bool ok = true;
            for (std::uint32_t b : l.blocks) ok = ok && !fixed[b];
            if (!ok) continue;
            // The way in: one block outside, jumping to the head alone, in the head's funclet.
            std::uint32_t only = 0;
            int outside = 0;
            for (std::uint32_t p : dom.preds[l.head]) if (!l.in[p]) { outside++; only = p; }
            if (outside == 1 && f.inst(f.blockArena[only].term).op == Op::Br && f.blockArena[only].funclet == 0 && !fixed[only])
                l.pre = only;
        }
        return out;
    }

    // **A preheader** where the head is entered from more than one block, or by a branch that goes elsewhere too: the
    // head's parameters repeated, every jump in from outside sent there, a jump on to the head. False where it cannot.
    bool preheader(const Loop &l, const Dominators &dom) {
        const Block &head = f.blockArena[l.head];
        std::vector<std::uint32_t> from;
        for (std::uint32_t p : dom.preds[l.head]) {
            if (l.in[p] || std::find(from.begin(), from.end(), p) != from.end()) continue;
            const Inst &t = f.inst(f.blockArena[p].term);
            if (f.blockArena[p].funclet != 0) return false;
            for (std::size_t s = 0; s < t.succ.size(); s++)
                if (t.succ[s].block.v == l.head && !isJumpEdge(t, s)) return false;
            from.push_back(p);
        }
        if (from.empty()) return false;
        const BlockId pb(static_cast<std::uint32_t>(f.blockArena.size()));
        Block pre;
        for (std::size_t k = 0; k < head.params.size(); k++) {
            Value v;
            v.type = f.value(head.params[k]).type;
            v.param = pb;
            v.index = static_cast<std::uint16_t>(k);
            pre.params.push_back(ValueId(static_cast<std::uint32_t>(f.values.size())));
            f.values.push_back(v);
        }
        Inst br;
        br.op = Op::Br;
        br.at = f.inst(head.term).at;
        br.pos = f.inst(head.term).pos;
        Succ s;
        s.block = BlockId(l.head);
        s.first = static_cast<std::uint32_t>(f.operands.size());
        s.count = static_cast<std::uint32_t>(pre.params.size());
        for (ValueId v : pre.params) f.operands.push_back(v);
        br.succ.push_back(s);
        pre.term = InstId(static_cast<std::uint32_t>(f.insts.size()));
        f.insts.push_back(br);
        f.blockArena.push_back(pre);
        for (std::uint32_t p : from) {
            Inst &t = f.insts[f.blockArena[p].term.v];
            for (Succ &e : t.succ) if (e.block.v == l.head) e.block = pb;
        }
        f.blocks.insert(place(f.blocks, BlockId(l.head)), pb);
        fixed.push_back(0);
        log.preheaders++;
        return true;
    }

    void defined() {
        blockOf.assign(f.values.size(), 0);
        for (ValueId p : f.params) blockOf[p.v] = f.entry.v;
        if (f.sret.valid()) blockOf[f.sret.v] = f.entry.v;
        for (BlockId b : f.blocks) {
            for (ValueId p : f.block(b).params) blockOf[p.v] = b.v;
            for (InstId id : f.block(b).insts) if (f.inst(id).result.valid()) blockOf[f.inst(id).result.v] = b.v;
            const InstId t = f.block(b).term;
            if (t.valid() && f.inst(t).result.valid()) blockOf[f.inst(t).result.v] = b.v;
        }
    }

    bool outside(const Loop &l, ValueId v) const { return v.v < blockOf.size() && !l.in[blockOf[v.v]]; }

    // A block every way round the loop and every way out of it passes: what it computes runs before the loop is left.
    bool always(const Loop &l, const Dominators &dom, std::uint32_t b) const {
        for (std::uint32_t x : l.latches) if (!dom.dominates(b, x)) return false;
        for (std::uint32_t x : l.blocks) {
            const Inst &t = f.inst(f.blockArena[x].term);
            for (const Succ &s : t.succ) if (!l.in[s.block.v] && !dom.dominates(b, x)) return false;
        }
        return true;
    }

    // **The invariants hoisted**: a pure instruction whose operands are all defined outside the loop, in block order so
    // what one hoisted defines lets the next go. A division traps: only from a block always run, in a loop with no call.
    void hoist(const Loop &l, const Dominators &dom) {
        bool calls = false;
        for (std::uint32_t b : l.blocks) {
            const Block &blk = f.blockArena[b];
            for (InstId id : blk.insts) calls = calls || f.inst(id).op == Op::Call || f.inst(id).op == Op::Trap;
            const Op t = f.inst(blk.term).op;
            calls = calls || t == Op::Invoke || t == Op::Call;
        }
        Block &pre = f.blockArena[l.pre];
        for (std::uint32_t b : l.blocks) {
            const bool runs = !calls && always(l, dom, b);
            std::vector<InstId> kept;
            for (InstId id : f.blockArena[b].insts) {
                const Inst &i = f.inst(id);
                const int kind = pureKind(i);
                const bool traps = i.op == Op::SDiv || i.op == Op::UDiv || i.op == Op::SRem || i.op == Op::URem;
                bool move = kind >= 1 && !i.isVolatile && i.result.valid() && (!traps || runs);
                for (std::uint32_t n = 0; move && n < i.count; n++) move = outside(l, f.operand(i, n));
                if (!move) { kept.push_back(id); continue; }
                pre.insts.push_back(id);
                blockOf[i.result.v] = l.pre;
                if (kind == 1) continue;        // a constant goes with what reads it, as gvn already shares one
                log.hoisted++;
                if (traps) log.hoistedTraps++;
            }
            f.blockArena[b].insts.swap(kept);
        }
    }

    ValueId make(Op op, TypeId type, ValueId a, ValueId b, long long k, InstId site) {
        Inst i;
        i.op = op;
        i.type = type;
        i.k = k;
        i.first = static_cast<std::uint32_t>(f.operands.size());
        if (a.valid()) { f.operands.push_back(a); i.count++; }
        if (b.valid()) { f.operands.push_back(b); i.count++; }
        i.at = f.inst(site).at;
        i.pos = f.inst(site).pos;
        Value v;
        v.type = type;
        v.def = InstId(static_cast<std::uint32_t>(f.insts.size()));
        i.result = ValueId(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(v);
        f.insts.push_back(i);
        blockOf.push_back(0);
        return i.result;
    }
    bool constantOf(ValueId v, long long &k) const {
        if (!v.valid() || !f.value(v).def.valid() || f.inst(f.value(v).def).op != Op::IConst) return false;
        k = f.inst(f.value(v).def).k;
        return true;
    }
    bool int32(TypeId t) const {
        const Type &ty = *types.type(t);
        return ty.isInteger() && !ty.isBool() && ty.size(target) == 4 && ty.isSigned(target);
    }
    void addArg(Succ &s, ValueId v) {
        const std::uint32_t first = static_cast<std::uint32_t>(f.operands.size());
        for (std::uint32_t k = 0; k < s.count; k++) f.operands.push_back(f.operands[s.first + k]);
        f.operands.push_back(v);
        s.first = first;
        s.count++;
    }
    // A counter's start in [0, INT_MAX): a constant, or a made preheader's parameter every way in passes one constant.
    bool startOf(const Loop &l, ValueId v, long long &k) const {
        bool known = constantOf(v, k);
        if (!known && !f.value(v).def.valid() && f.value(v).param.v == l.pre) {
            for (BlockId b : f.blocks) {
                const Inst &t = f.inst(f.block(b).term);
                for (const Succ &s : t.succ) {
                    long long x = 0;
                    if (s.block.v != l.pre) continue;
                    if (!constantOf(f.arg(s, f.value(v).index), x) || (known && x != k)) return false;
                    k = x;
                    known = true;
                }
            }
        }
        return known && k >= 0 && k < 0x7fffffff;
    }
    static void insertAfter(std::vector<InstId> &list, InstId at, InstId id) {
        list.insert(place(list, at) + 1, id);
    }

    // **Counter widening**: a head parameter of type int entering as a constant in [0, INT_MAX), coming round as itself
    // plus a positive constant, tested `<` or `<=` against a bound the loop does not define, and sign-extended to 64 bits:
    // a 64-bit twin carried beside it replaces each extension, and every read of the int reads the twin truncated.
    bool widen(const Loop &l, std::size_t j) {
        Block &head = f.blockArena[l.head];
        const ValueId p = head.params[j];
        if (!int32(f.value(p).type)) return false;
        long long start = 0;
        if (!startOf(l, f.arg(f.inst(f.blockArena[l.pre].term).succ[0], static_cast<std::uint32_t>(j)), start)) return false;
        ValueId a;
        for (std::uint32_t x : l.latches) {
            const Inst &t = f.inst(f.blockArena[x].term);
            for (const Succ &s : t.succ) {
                if (s.block.v != l.head) continue;
                const ValueId v = f.arg(s, static_cast<std::uint32_t>(j));
                if (a.valid() && v.v != a.v) return false;
                a = v;
            }
        }
        if (!a.valid() || !f.value(a).def.valid()) return false;
        const InstId addId = f.value(a).def;
        const Inst &add = f.inst(addId);
        if (add.op != Op::Add || add.count != 2 || add.type != f.value(p).type) return false;
        const int which = f.operand(add, 0).v == p.v ? 1 : f.operand(add, 1).v == p.v ? 0 : -1;
        long long by = 0;
        if (which < 0 || !constantOf(f.operand(add, static_cast<std::uint32_t>(which)), by) || by <= 0 || by >= 0x7fffffff)
            return false;
        // The bound and the extensions, in the loop.
        bool bounded = false;
        TypeId wide;
        std::vector<InstId> sexts;
        for (std::uint32_t b : l.blocks) {
            const Block &blk = f.blockArena[b];
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                const Inst &i = f.inst(id);
                const ValueId o0 = i.count > 0 ? f.operand(i, 0) : ValueId();
                const bool counter = o0.v == p.v || o0.v == a.v;
                if (i.op == Op::ICmp && (i.cc == Cmp::Slt || i.cc == Cmp::Sle) && counter && outside(l, f.operand(i, 1))) bounded = true;
                if (i.op == Op::SExt && counter && types.type(i.type)->size(target) == 8 && (!wide.valid() || wide == i.type)) {
                    wide = i.type;
                    sexts.push_back(id);
                }
            }
        }
        if (!bounded || sexts.empty()) return false;
        const TypeId narrow = f.value(p).type;
        // The twin: a head parameter, its start and step made in the preheader, its next value after the int's.
        Value pv;
        pv.type = wide;
        pv.param = BlockId(l.head);
        pv.index = static_cast<std::uint16_t>(head.params.size());
        const ValueId P(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(pv);
        blockOf.push_back(l.head);
        f.blockArena[l.head].params.push_back(P);
        const InstId preTerm = f.blockArena[l.pre].term;
        const ValueId c64 = make(Op::IConst, wide, ValueId(), ValueId(), start, preTerm);
        const ValueId k64 = make(Op::IConst, wide, ValueId(), ValueId(), by, preTerm);
        f.blockArena[l.pre].insts.push_back(f.value(c64).def);
        f.blockArena[l.pre].insts.push_back(f.value(k64).def);
        const ValueId a64 = make(Op::Add, wide, P, k64, 0, addId);
        const ValueId pt = make(Op::Trunc, narrow, P, ValueId(), 0, addId);
        const ValueId at = make(Op::Trunc, narrow, a64, ValueId(), 0, addId);
        const std::uint32_t addBlock = blockOf[a.v];
        blockOf[c64.v] = blockOf[k64.v] = l.pre;
        blockOf[a64.v] = blockOf[at.v] = addBlock;
        blockOf[pt.v] = l.head;
        std::vector<InstId> &headList = f.blockArena[l.head].insts;
        headList.insert(headList.begin(), f.value(pt).def);
        insertAfter(f.blockArena[addBlock].insts, addId, f.value(a64).def);
        insertAfter(f.blockArena[addBlock].insts, f.value(a64).def, f.value(at).def);
        // Every use re-pointed - an extension to the twin, the int to the twin truncated - before the new edges' arguments;
        // the new instructions read only P, the step and the twin's next value, none of them replaced.
        std::vector<ValueId> repl(f.values.size());
        for (InstId id : sexts) {
            const Inst &i = f.inst(id);
            repl[i.result.v] = f.operand(i, 0).v == p.v ? P : a64;
        }
        repl[p.v] = pt;
        repl[a.v] = at;
        for (std::size_t k = 0; k < f.operands.size(); k++)
            if (f.operands[k].v < repl.size() && repl[f.operands[k].v].valid()) f.operands[k] = repl[f.operands[k].v];
        for (InstId id : sexts) {
            std::vector<InstId> &list = f.blockArena[blockOf[f.inst(id).result.v]].insts;
            list.erase(place(list, id));
            f.values[f.inst(id).result.v].type = TypeId();
        }
        addArg(f.insts[preTerm.v].succ[0], c64);
        for (std::uint32_t x : l.latches) {
            Inst &t = f.insts[f.blockArena[x].term.v];
            for (std::size_t s = 0; s < t.succ.size(); s++) if (t.succ[s].block.v == l.head) addArg(t.succ[s], a64);
        }
        log.widened++;
        return true;
    }

    bool run() {
        fixed = fixedBlocks(f);
        bool made = false;
        {
            const Dominators dom(f);
            const std::vector<Loop> loops = find(dom);
            for (const Loop &l : loops) {
                bool ok = !fixed[l.head];
                for (std::uint32_t b : l.blocks) ok = ok && !fixed[b];
                if (ok && l.pre == 0) made = preheader(l, dom) || made;
            }
        }
        const Dominators dom(f);
        std::vector<Loop> loops = find(dom);
        std::vector<std::size_t> order;
        for (std::size_t k = 0; k < loops.size(); k++) if (loops[k].pre != 0) order.push_back(k);
        // Innermost first: what an inner loop hoists to its preheader, inside the outer loop, the outer may hoist again.
        std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return loops[x].blocks.size() < loops[y].blocks.size(); });
        log.loopsFound += static_cast<int>(loops.size());
        defined();
        const int before = log.hoisted, widened = log.widened;
        for (std::size_t k : order) {
            hoist(loops[k], dom);
            for (std::size_t j = 0; j < f.blockArena[loops[k].head].params.size(); j++) widen(loops[k], j);
        }
        return made || log.hoisted != before || log.widened != widened;
    }
};

}   // namespace

std::vector<Loop> naturalLoops(Function &f, const Dominators &dom, const TypeTable &types, const Target &target, PassLog &log) {
    Looper p(f, types, target, log);
    p.fixed = fixedBlocks(f);
    return p.find(dom);
}

bool loops(Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    if (!f.entry.valid()) return false;
    Looper p(f, types, target, log);
    return p.run();
}

}   // namespace ir
