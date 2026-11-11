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

    // The object an address is into: the alloca or the symbol a chain of geps and bitcasts starts from, else none.
    InstId root(ValueId v) const {
        while (v.valid() && f.value(v).def.valid()) {
            const Inst &i = f.inst(f.value(v).def);
            if (i.op == Op::Alloca || i.op == Op::Addr) return f.value(v).def;
            if ((i.op != Op::Gep && i.op != Op::Bitcast) || i.count == 0) break;
            v = f.operand(i, 0);
        }
        return InstId();
    }
    // A named object itself, an alloca or a symbol, through bitcasts alone: a load of it cannot trap, run or not.
    bool named(ValueId v) const {
        while (v.valid() && f.value(v).def.valid() && f.inst(f.value(v).def).op == Op::Bitcast)
            v = f.operand(f.inst(f.value(v).def), 0);
        if (!v.valid() || !f.value(v).def.valid()) return false;
        return f.inst(f.value(v).def).op == Op::Alloca || f.inst(f.value(v).def).op == Op::Addr;
    }
    // No store of the loop writes what `address` reads: each stores into another named object than the load reads.
    bool unwritten(const std::vector<InstId> &stores, ValueId address) const {
        const InstId r = root(address);
        if (!r.valid()) return stores.empty();
        const Inst &o = f.inst(r);
        for (InstId id : stores) {
            const InstId w = root(f.operand(f.inst(id), 0));
            if (!w.valid()) return false;
            const Inst &x = f.inst(w);
            if (x.op == o.op && (o.op == Op::Alloca ? x.k == o.k : x.sym == o.sym)) return false;
        }
        return true;
    }

    // **The invariants hoisted**: a pure instruction whose operands are all defined outside the loop, in block order so
    // what one hoisted defines lets the next go. A division traps, a load may: only from a block always run (a load of a
    // named object from any), in a loop with no call; a load besides where stores write only other named objects.
    void hoist(const Loop &l, const Dominators &dom) {
        bool calls = false, clobbers = false;
        std::vector<InstId> stores;
        for (std::uint32_t b : l.blocks) {
            const Block &blk = f.blockArena[b];
            for (InstId id : blk.insts) {
                const Inst &i = f.inst(id);
                calls = calls || i.op == Op::Call || i.op == Op::Trap;
                if (i.op == Op::Store && !i.isVolatile) stores.push_back(id);
                else clobbers = clobbers || i.isVolatile || i.op == Op::Memcpy || i.op == Op::Memset || i.op == Op::Call ||
                                i.op == Op::VaStart || i.op == Op::VaArg || i.op == Op::VaEnd;
            }
            const Op t = f.inst(blk.term).op;
            calls = calls || t == Op::Invoke || t == Op::Call;
        }
        clobbers = clobbers || calls;
        Block &pre = f.blockArena[l.pre];
        for (std::uint32_t b : l.blocks) {
            const bool runs = !calls && always(l, dom, b);
            std::vector<InstId> kept;
            for (InstId id : f.blockArena[b].insts) {
                const Inst &i = f.inst(id);
                const int kind = pureKind(i);
                const bool traps = i.op == Op::SDiv || i.op == Op::UDiv || i.op == Op::SRem || i.op == Op::URem;
                const bool load = i.op == Op::Load && i.count == 1 && !i.isVolatile && i.result.valid() && !clobbers &&
                                  (runs || named(f.operand(i, 0)));
                bool move = (kind >= 1 || (load && unwritten(stores, f.operand(i, 0)))) && !i.isVolatile && i.result.valid() &&
                            (!traps || runs);
                for (std::uint32_t n = 0; move && n < i.count; n++) move = outside(l, f.operand(i, n));
                if (!move) { kept.push_back(id); continue; }
                pre.insts.push_back(id);
                blockOf[i.result.v] = l.pre;
                if (kind == 1) continue;        // a constant goes with what reads it, as gvn already shares one
                log.hoisted++;
                if (kind == 0) log.hoistedLoads++;
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

    // An int `x + k` or `x - k`, k a constant: its x and +-k, false for anything else.
    bool offsetOf(ValueId v, ValueId &x, long long &k) const {
        if (!v.valid() || !f.value(v).def.valid()) return false;
        const Inst &i = f.inst(f.value(v).def);
        if ((i.op != Op::Add && i.op != Op::Sub) || i.count != 2 || !int32(i.type)) return false;
        const int c = constantOf(f.operand(i, 1), k) ? 1 : i.op == Op::Add && constantOf(f.operand(i, 0), k) ? 0 : -1;
        if (c < 0 || k <= -0x7fffffff || k >= 0x7fffffff) return false;
        x = f.operand(i, static_cast<std::uint32_t>(1 - c));
        if (i.op == Op::Sub) k = -k;
        return true;
    }

    // **Counter widening**: a head parameter of type int coming round as itself plus or minus a constant, tested against a
    // bound the loop does not define, and sign-extended to 64 bits, itself or plus a constant: a 64-bit twin carried beside
    // it replaces each extension, and every read of the int reads the twin truncated. A signed int never wraps.
    bool widen(const Loop &l, std::size_t j) {
        Block &head = f.blockArena[l.head];
        const ValueId p = head.params[j];
        if (!int32(f.value(p).type)) return false;
        long long start = 0;
        const ValueId in = f.arg(f.inst(f.blockArena[l.pre].term).succ[0], static_cast<std::uint32_t>(j));
        const bool constant = startOf(l, in, start);
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
        ValueId stepped;
        long long by = 0;
        if (!offsetOf(a, stepped, by) || stepped.v != p.v || by == 0 || f.inst(addId).type != f.value(p).type) return false;
        // The bound and the extensions, in the loop.
        bool bounded = false;
        TypeId wide;
        std::vector<InstId> sexts;
        std::vector<long long> offsets;     // by extension: 0 for the counter's own, else the constant added first
        for (BlockId bid : f.blocks) {      // an extension past the loop too: what it reads dominates it, and so the twin
            const std::uint32_t b = bid.v;
            const Block &blk = f.blockArena[b];
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                const Inst &i = f.inst(id);
                const ValueId o0 = i.count > 0 ? f.operand(i, 0) : ValueId();
                const bool counter = o0.v == p.v || o0.v == a.v;
                if (i.op == Op::ICmp && i.cc <= Cmp::Sge && i.count == 2 && (counter || f.operand(i, 1).v == p.v ||
                    f.operand(i, 1).v == a.v) && outside(l, f.operand(i, counter ? 1 : 0)) && l.in[b]) bounded = true;
                ValueId x;
                long long plus = 0;
                const bool offset = !counter && offsetOf(o0, x, plus) && plus != 0 && (x.v == p.v || x.v == a.v);
                if (i.op == Op::SExt && (counter || offset) && types.type(i.type)->size(target) == 8 && (!wide.valid() || wide == i.type)) {
                    wide = i.type;
                    sexts.push_back(id);
                    offsets.push_back(offset ? plus : 0);
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
        const ValueId c64 = constant ? make(Op::IConst, wide, ValueId(), ValueId(), start, preTerm)
                                     : make(Op::SExt, wide, in, ValueId(), 0, preTerm);
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
        for (std::size_t n = 0; n < sexts.size(); n++) {
            const Inst &i = f.inst(sexts[n]);
            if (offsets[n] == 0) repl[i.result.v] = f.operand(i, 0).v == p.v ? P : a64;
        }
        repl[p.v] = pt;
        repl[a.v] = at;
        for (std::size_t k = 0; k < f.operands.size(); k++)
            if (f.operands[k].v < repl.size() && repl[f.operands[k].v].valid()) f.operands[k] = repl[f.operands[k].v];
        // An extension of the counter plus k becomes the twin plus k, where it stands.
        for (std::size_t n = 0; n < sexts.size(); n++) {
            const InstId id = sexts[n];
            if (offsets[n] != 0) {
                ValueId x;
                long long k = 0;
                offsetOf(f.operand(f.inst(id), 0), x, k);
                const ValueId kk = make(Op::IConst, wide, ValueId(), ValueId(), k, preTerm);
                f.blockArena[l.pre].insts.push_back(f.value(kk).def);
                blockOf[kk.v] = l.pre;
                Inst &i = f.insts[id.v];
                i.op = Op::Add;
                i.first = static_cast<std::uint32_t>(f.operands.size());
                i.count = 2;
                f.operands.push_back(x.v == pt.v ? P : a64);
                f.operands.push_back(kk);
                continue;
            }
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

    // What a repeated instruction reads: the map's value for one the head defines, else the value itself.
    static ValueId mapped(const std::vector<ValueId> &m, ValueId v) { return v.v < m.size() && m[v.v].valid() ? m[v.v] : v; }
    void replaceIn(std::uint32_t b, const std::vector<ValueId> &m) {
        const Block &blk = f.blockArena[b];
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const Inst &i = f.inst(k < blk.insts.size() ? blk.insts[k] : blk.term);
            for (std::uint32_t n = 0; n < i.count; n++) f.operands[i.first + n] = mapped(m, f.operands[i.first + n]);
            for (const Succ &s : i.succ)
                for (std::uint32_t n = 0; n < s.count; n++) f.operands[s.first + n] = mapped(m, f.operands[s.first + n]);
        }
    }
    ValueId parameter(std::uint32_t b, TypeId type) {
        Value v;
        v.type = type;
        v.param = BlockId(b);
        v.index = static_cast<std::uint16_t>(f.blockArena[b].params.size());
        const ValueId id(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(v);
        f.blockArena[b].params.push_back(id);
        return id;
    }
    // The head's instructions repeated at the end of `from`, which jumped to it: its test there, on to the head with the
    // values `ins` the loop reads, or out to `x` with the values `outs` read past the loop.
    void repeat(std::uint32_t from, const Loop &l, const std::vector<InstId> &body, const Inst &test, std::size_t out,
                std::uint32_t x, const std::vector<ValueId> &ins, const std::vector<ValueId> &outs) {
        std::vector<ValueId> m(f.values.size());
        const InstId term = f.blockArena[from].term;
        const Succ in = f.inst(term).succ[0];
        for (std::uint32_t k = 0; k < in.count; k++) m[f.blockArena[l.head].params[k].v] = f.operands[in.first + k];
        for (InstId id : body) {
            Inst c = f.inst(id);
            const std::uint32_t first = static_cast<std::uint32_t>(f.operands.size());
            for (std::uint32_t n = 0; n < c.count; n++) { const ValueId v = mapped(m, f.operands[c.first + n]); f.operands.push_back(v); }
            c.first = first;
            Value v;
            v.type = f.value(c.result).type;
            v.def = InstId(static_cast<std::uint32_t>(f.insts.size()));
            m[c.result.v] = ValueId(static_cast<std::uint32_t>(f.values.size()));
            c.result = m[c.result.v];
            f.values.push_back(v);
            f.insts.push_back(c);
            f.blockArena[from].insts.push_back(v.def);
        }
        Inst t = test;
        Succ on, off;
        const ValueId cond = mapped(m, f.operands[test.first]);
        t.first = static_cast<std::uint32_t>(f.operands.size());
        f.operands.push_back(cond);
        on.block = BlockId(l.head);
        on.first = static_cast<std::uint32_t>(f.operands.size());
        on.count = in.count + static_cast<std::uint32_t>(ins.size());
        for (std::uint32_t k = 0; k < in.count; k++) { const ValueId v = f.operands[in.first + k]; f.operands.push_back(v); }
        for (ValueId v : ins) f.operands.push_back(mapped(m, v));
        off.block = BlockId(x);
        off.first = static_cast<std::uint32_t>(f.operands.size());
        off.count = static_cast<std::uint32_t>(outs.size());
        for (ValueId v : outs) f.operands.push_back(mapped(m, v));
        t.succ[1 - out] = on;
        t.succ[out] = off;
        f.insts[term.v] = t;
    }

    // **Rotation**: a loop left only by its head's test, the head a few pure instructions and loads, has them repeated in
    // the preheader and the latch - one conditional jump a turn. What the head defined comes to it as parameters, and to
    // what follows the loop as the parameters of a block made on the way out. False where the loop is another shape.
    bool rotate(const Loop &l, const Dominators &dom) {
        if (l.pre == 0 || l.latches.size() != 1 || l.latches[0] == l.head) return false;
        const std::uint32_t latch = l.latches[0];
        const Inst test = f.inst(f.blockArena[l.head].term);
        const std::vector<InstId> body = f.blockArena[l.head].insts;
        if (test.op != Op::CondBr || f.inst(f.blockArena[latch].term).op != Op::Br || body.size() > 8) return false;
        if (l.in[test.succ[0].block.v] == l.in[test.succ[1].block.v]) return false;
        const std::size_t out = l.in[test.succ[0].block.v] ? 1 : 0;
        const std::uint32_t after = test.succ[out].block.v;
        if (fixed[after] || f.blockArena[after].funclet != 0 || f.blockArena[l.head].funclet != 0 || f.blockArena[latch].funclet != 0)
            return false;
        for (std::uint32_t b : l.blocks) {
            const Inst &t = f.inst(f.blockArena[b].term);
            for (std::size_t s = 0; s < t.succ.size(); s++)
                if (b != l.head && !l.in[t.succ[s].block.v] && isJumpEdge(t, s)) return false;
        }
        // What the head defines: 1 a parameter, 2 an instruction's value, 3 a constant, which stays where it is read.
        std::vector<char> kind(f.values.size(), 0), inside(f.values.size(), 0), past(f.values.size(), 0);
        for (ValueId p : f.blockArena[l.head].params) kind[p.v] = 1;
        for (InstId id : body) {
            const Inst &i = f.inst(id);
            if (!i.result.valid() || i.isVolatile || (pureKind(i) < 1 && (i.op != Op::Load || i.count != 1))) return false;
            kind[i.result.v] = pureKind(i) == 1 ? 3 : 2;
        }
        // Its readers: in the loop past the head's own instructions and test, and outside, which the way out must dominate.
        const bool sole = dom.preds[after].size() == 1;
        for (BlockId bid : f.blocks) {
            const std::uint32_t b = bid.v;
            const Block &blk = f.blockArena[b];
            for (std::size_t k = b == l.head ? blk.insts.size() : 0; k <= blk.insts.size(); k++) {
                const Inst &i = f.inst(k < blk.insts.size() ? blk.insts[k] : blk.term);
                for (std::uint32_t n = b == l.head ? i.count : 0; n < i.count; n++) {
                    const ValueId v = f.operand(i, n);
                    if (kind[v.v] == 0) continue;
                    if (l.in[b]) inside[v.v] = 1;
                    else if (!sole || !dom.reachable(b) || !dom.dominates(after, b)) return false;
                    else past[v.v] = 1;
                }
                for (std::size_t s = 0; s < i.succ.size(); s++)
                    for (std::uint32_t n = 0; n < i.succ[s].count; n++) {
                        const ValueId v = f.arg(i.succ[s], n);
                        if (kind[v.v] == 0) continue;
                        if (l.in[b] && !(b == l.head && s == out)) inside[v.v] = 1;
                        else if (!l.in[b] && (!sole || !dom.reachable(b) || !dom.dominates(after, b))) return false;
                        else past[v.v] = 1;
                    }
            }
        }
        // The way out: a block of the values read past the loop, jumping on with the head's own arguments.
        const std::uint32_t x = static_cast<std::uint32_t>(f.blockArena.size());
        f.blockArena.push_back(Block());
        fixed.push_back(0);
        std::vector<ValueId> ins, outs, toX(f.values.size()), toHead(f.values.size());
        for (std::uint32_t v = 0; v < kind.size(); v++) {
            if (kind[v] != 0 && past[v]) outs.push_back(ValueId(v));
            if (kind[v] == 2 && inside[v]) ins.push_back(ValueId(v));
        }
        for (ValueId v : outs) toX[v.v] = parameter(x, f.value(v).type);
        Inst br;
        br.op = Op::Br;
        br.at = test.at;
        br.pos = test.pos;
        br.succ.push_back(test.succ[out]);
        f.blockArena[x].term = InstId(static_cast<std::uint32_t>(f.insts.size()));
        f.insts.push_back(br);
        f.blocks.insert(place(f.blocks, BlockId(after)), BlockId(x));
        repeat(l.pre, l, body, test, out, x, ins, outs);
        repeat(latch, l, body, test, out, x, ins, outs);
        for (BlockId b : f.blocks) if (b.v >= l.in.size() || !l.in[b.v]) replaceIn(b.v, toX);
        // The head: its values the loop reads are parameters, its constants stay, its test is a jump on.
        for (ValueId v : ins) toHead[v.v] = parameter(l.head, f.value(v).type);
        std::vector<InstId> kept;
        for (InstId id : body) {
            if (kind[f.inst(id).result.v] == 3) kept.push_back(id);
            else f.values[f.inst(id).result.v].type = TypeId();
        }
        f.blockArena[l.head].insts.swap(kept);
        br.succ[0] = test.succ[1 - out];
        f.insts[f.blockArena[l.head].term.v] = br;
        for (std::uint32_t b : l.blocks) replaceIn(b, toHead);
        log.rotated++;
        return true;
    }
    // One loop at a time, the dominators and the loops found again after each.
    bool rotateAll() {
        bool any = false;
        for (bool again = true; again;) {
            again = false;
            fixed = fixedBlocks(f);
            const Dominators dom(f);
            const std::vector<Loop> loops = find(dom);
            for (std::size_t k = 0; k < loops.size() && !again; k++) again = rotate(loops[k], dom);
            any = any || again;
        }
        return any;
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

bool rotate(Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    if (!f.entry.valid()) return false;
    Looper p(f, types, target, log);
    return p.rotateAll();
}

}   // namespace ir
