// **mem2reg** (M3b.4 round 1, design section 4): a frame slot whose address is only loaded and stored, whole and not
// volatile, becomes SSA values. Placement is pruned by the slot's liveness over the iterated dominance frontier of its
// stores; the renaming walks the dominator tree and writes each incoming value as an argument of the jump - never a phi.

#include "SsaOpt.h"

#include <algorithm>
#include <utility>

namespace ir {

namespace {

// One alloca of a scalar, and what its uses allow.
struct Slot {
    ValueId addr;
    TypeId type;
    std::uint32_t funclet;
    bool bad, seen;
    std::vector<std::uint32_t> loadsFirst, stores;      // blocks where a load comes before any store; blocks that store
    std::vector<std::uint32_t> params;                  // blocks given a parameter for it
    Slot() : funclet(0), bad(false), seen(false) {}
};

struct Promoter {
    Function &f;
    const TypeTable &types;
    Dominators dom;
    std::vector<int> slotOf;                            // by value: the slot an alloca's address is, -1 none
    std::vector<Slot> slots;
    std::vector<ValueId> repl;                          // by value: what a promoted load's result became
    std::vector<char> dead;                             // by instruction
    std::vector<std::vector<std::pair<int, ValueId> > > placed;   // by block: (slot, parameter), in slot order

    Promoter(Function &fn, const TypeTable &t) : f(fn), types(t), dom(fn) {}

    bool scalar(TypeId t) const {
        const Type &ty = *types.type(t);
        return !ty.isArray() && !ty.isStructOrUnion() && (ty.isInteger() || ty.isPointer() || ty.isFloating());
    }
    int slotAt(ValueId v) const { return v.valid() && v.v < slotOf.size() ? slotOf[v.v] : -1; }
    ValueId resolve(ValueId v) const {
        while (v.valid() && v.v < repl.size() && repl[v.v].valid()) v = repl[v.v];
        return v;
    }

    // The scalar allocas, then every use of each: a load or store of its whole type through it, all in one funclet.
    void collect() {
        slotOf.assign(f.values.size(), -1);
        const Block &entry = f.block(f.entry);
        for (InstId id : entry.insts) {
            const Inst &i = f.inst(id);
            if (i.op != Op::Alloca || !i.result.valid()) continue;
            Slot s;
            s.addr = i.result;
            s.type = f.frame[static_cast<std::size_t>(i.k)].type;
            if (!scalar(s.type)) continue;
            slotOf[i.result.v] = static_cast<int>(slots.size());
            slots.push_back(s);
        }
        for (std::size_t c = 1; c < f.calls.size(); c++)
            if (f.calls[c].resultAlloca >= 0)
                for (Slot &s : slots) if (f.inst(f.value(s.addr).def).k == f.calls[c].resultAlloca) s.bad = true;
        for (BlockId b : f.blocks) {
            const Block &blk = f.block(b);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!id.valid()) continue;
                const Inst &i = f.inst(id);
                for (std::uint32_t n = 0; n < i.count; n++) {
                    const int c = slotAt(f.operand(i, n));
                    if (c < 0) continue;
                    Slot &s = slots[static_cast<std::size_t>(c)];
                    const bool load = i.op == Op::Load && n == 0 && i.type == s.type;
                    const bool store = i.op == Op::Store && n == 0 && i.count == 2 && f.value(f.operand(i, 1)).type == s.type;
                    if ((!load && !store) || i.isVolatile) { s.bad = true; continue; }
                    if (!s.seen) { s.seen = true; s.funclet = blk.funclet; }
                    else if (s.funclet != blk.funclet) s.bad = true;
                    if (store) { if (s.stores.empty() || s.stores.back() != b.v) s.stores.push_back(b.v); }
                    else if ((s.stores.empty() || s.stores.back() != b.v) && (s.loadsFirst.empty() || s.loadsFirst.back() != b.v))
                        s.loadsFirst.push_back(b.v);
                }
                for (const Succ &sc : i.succ)
                    for (std::uint32_t a = 0; a < sc.count; a++) {
                        const int c = slotAt(f.arg(sc, a));
                        if (c >= 0) slots[static_cast<std::size_t>(c)].bad = true;
                    }
            }
        }
    }

    // A block the slot may not be live into: a pad, an unwind or funclet edge's target (the frame is the only way in);
    // a block that may not take a parameter: one reached by any edge but a jump, a conditional jump, a switch or an
    // invoke's normal return, or the entry.
    void edges(std::vector<char> &padIn, std::vector<char> &noParam) const {
        padIn.assign(f.blockArena.size(), 0);
        noParam.assign(f.blockArena.size(), 0);
        if (f.entry.valid()) noParam[f.entry.v] = 1;
        for (BlockId b : f.blocks) {
            if (f.block(b).pad) padIn[b.v] = 1;
            const InstId t = f.block(b).term;
            if (!t.valid()) continue;
            const Inst &i = f.inst(t);
            for (std::size_t s = 0; s < i.succ.size(); s++) {
                const std::uint32_t to = i.succ[s].block.v;
                if (to == 0 || to >= f.blockArena.size()) continue;
                const bool jump = i.op == Op::Br || i.op == Op::CondBr || i.op == Op::Switch || (i.op == Op::Invoke && s == 0);
                if (!jump) { noParam[to] = 1; padIn[to] = 1; }
            }
        }
    }

    // Liveness of each slot into each block, then the pruned placement over the iterated frontier of its stores.
    void place() {
        std::vector<char> padIn, noParam;
        edges(padIn, noParam);
        const std::vector<std::vector<std::uint32_t> > df = dom.frontiers();
        const std::size_t n = f.blockArena.size();
        std::vector<std::uint32_t> stores(n, 0), live(n, 0), queued(n, 0);
        placed.assign(n, std::vector<std::pair<int, ValueId> >());
        for (std::size_t c = 0; c < slots.size(); c++) {
            Slot &s = slots[c];
            if (s.bad) continue;
            const std::uint32_t mark = static_cast<std::uint32_t>(c + 1);
            for (std::uint32_t b : s.stores) stores[b] = mark;
            std::vector<std::uint32_t> work;
            for (std::uint32_t b : s.loadsFirst) if (dom.reachable(b) && live[b] != mark) { live[b] = mark; work.push_back(b); }
            while (!work.empty()) {
                const std::uint32_t b = work.back();
                work.pop_back();
                if (padIn[b]) { s.bad = true; break; }
                for (std::uint32_t p : dom.preds[b])
                    if (stores[p] != mark && live[p] != mark) { live[p] = mark; work.push_back(p); }
            }
            if (s.bad) continue;
            work.clear();
            for (std::uint32_t b : s.stores) if (dom.reachable(b) && queued[b] != mark) { queued[b] = mark; work.push_back(b); }
            std::vector<std::uint32_t> at;
            while (!work.empty()) {
                const std::uint32_t b = work.back();
                work.pop_back();
                // The whole iterated frontier is walked; only a block the slot is live into is given a parameter.
                for (std::uint32_t x : df[b]) {
                    if (live[x] == mark && std::find(at.begin(), at.end(), x) == at.end()) {
                        if (noParam[x]) { s.bad = true; break; }
                        at.push_back(x);
                    }
                    if (queued[x] != mark) { queued[x] = mark; work.push_back(x); }
                }
                if (s.bad) break;
            }
            if (s.bad) continue;
            std::sort(at.begin(), at.end());
            s.params = at;
        }
        // The parameters, made once every slot is decided: a block's in slot order, after the lowering's own.
        for (std::size_t c = 0; c < slots.size(); c++) {
            if (slots[c].bad) continue;
            for (std::uint32_t b : slots[c].params) {
                Value v;
                v.type = slots[c].type;
                v.param = BlockId(b);
                v.index = static_cast<std::uint16_t>(f.blockArena[b].params.size());
                const ValueId id(static_cast<std::uint32_t>(f.values.size()));
                f.values.push_back(v);
                f.blockArena[b].params.push_back(id);
                placed[b].push_back(std::make_pair(static_cast<int>(c), id));
            }
        }
    }

    // An undefined value of a type, made at the end of block b: what a path that never stored passes.
    ValueId undef(BlockId b, TypeId type) {
        Inst i;
        i.op = Op::Undef;
        i.type = type;
        i.first = static_cast<std::uint32_t>(f.operands.size());
        const InstId id(static_cast<std::uint32_t>(f.insts.size()));
        Value v;
        v.type = type;
        v.def = id;
        i.result = ValueId(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(v);
        f.insts.push_back(i);
        dead.push_back(0);
        f.blockArena[b.v].insts.push_back(id);
        return i.result;
    }

    int promoted(ValueId addr) const {
        const int c = slotAt(addr);
        return c >= 0 && !slots[static_cast<std::size_t>(c)].bad ? c : -1;
    }

    // One block's loads, stores and allocas of promoted slots, `cur` the value each slot holds on entry, updated.
    void rewrite(BlockId b, std::vector<ValueId> &cur, std::vector<std::pair<int, ValueId> > &undo) {
        const std::vector<InstId> list = f.block(b).insts;
        for (InstId id : list) {
            Inst &i = f.insts[id.v];
            if (i.op == Op::Alloca && promoted(i.result) >= 0) { dead[id.v] = 1; continue; }
            if (i.op != Op::Load && i.op != Op::Store) continue;
            const int c = promoted(f.operand(i, 0));
            if (c < 0) continue;
            if (i.op == Op::Store) {
                undo.push_back(std::make_pair(c, cur[static_cast<std::size_t>(c)]));
                cur[static_cast<std::size_t>(c)] = resolve(f.operand(i, 1));
                dead[id.v] = 1;
            } else if (cur[static_cast<std::size_t>(c)].valid()) {
                repl[i.result.v] = cur[static_cast<std::size_t>(c)];
                dead[id.v] = 1;
            } else {
                i.op = Op::Undef;   // read before any store: the load becomes the undefined value it reads
                i.count = 0;
            }
        }
    }

    // The arguments of each edge out of b into a block given parameters, appended after the lowering's own.
    void arguments(BlockId b, const std::vector<ValueId> &cur) {
        const InstId t = f.block(b).term;
        if (!t.valid()) return;
        for (std::size_t s = 0; s < f.inst(t).succ.size(); s++) {
            const std::uint32_t to = f.inst(t).succ[s].block.v;
            if (to == 0 || to >= placed.size() || placed[to].empty()) continue;
            std::vector<ValueId> args;
            for (const std::pair<int, ValueId> &p : placed[to]) {
                const ValueId v = cur[static_cast<std::size_t>(p.first)];
                args.push_back(v.valid() ? v : undef(b, slots[static_cast<std::size_t>(p.first)].type));
            }
            Succ &sc = f.insts[t.v].succ[s];
            const std::uint32_t first = static_cast<std::uint32_t>(f.operands.size());
            for (std::uint32_t a = 0; a < sc.count; a++) { const ValueId v = f.operands[sc.first + a]; f.operands.push_back(v); }
            f.operands.insert(f.operands.end(), args.begin(), args.end());
            sc.first = first;
            sc.count += static_cast<std::uint32_t>(args.size());
        }
    }

    // The renaming over the dominator tree, by an explicit stack; then the blocks the entry does not reach.
    void rename() {
        const std::size_t n = f.blockArena.size();
        std::vector<std::vector<std::uint32_t> > children(n);
        for (std::uint32_t b : dom.rpo) if (b != f.entry.v) children[dom.idom[b]].push_back(b);
        std::vector<ValueId> cur(slots.size());
        std::vector<std::pair<int, ValueId> > undo;
        // (block, the undo log's size on entry); a block is pushed again as its own exit marker with the high bit set.
        std::vector<std::pair<std::uint32_t, std::size_t> > stack(1, std::make_pair(f.entry.v, std::size_t(0)));
        while (!stack.empty()) {
            const std::pair<std::uint32_t, std::size_t> top = stack.back();
            stack.pop_back();
            if (top.first & 0x80000000u) {
                while (undo.size() > top.second) { cur[static_cast<std::size_t>(undo.back().first)] = undo.back().second; undo.pop_back(); }
                continue;
            }
            const BlockId b(top.first);
            const std::size_t mark = undo.size();
            for (const std::pair<int, ValueId> &p : placed[b.v]) {
                undo.push_back(std::make_pair(p.first, cur[static_cast<std::size_t>(p.first)]));
                cur[static_cast<std::size_t>(p.first)] = p.second;
            }
            rewrite(b, cur, undo);
            arguments(b, cur);
            stack.push_back(std::make_pair(b.v | 0x80000000u, mark));
            for (std::size_t k = children[b.v].size(); k-- > 0; ) stack.push_back(std::make_pair(children[b.v][k], std::size_t(0)));
        }
        std::vector<ValueId> none(slots.size());
        for (BlockId b : f.blocks) {
            if (dom.reachable(b.v)) continue;
            std::fill(none.begin(), none.end(), ValueId());
            rewrite(b, none, undo);
            arguments(b, none);
            undo.clear();
        }
    }

    // Uses re-pointed, the dead instructions taken out of their blocks and their values given no type (no slot), and
    // the promoted frame objects removed, the others renumbered.
    void finish() {
        for (std::size_t k = 0; k < f.operands.size(); k++) f.operands[k] = resolve(f.operands[k]);
        for (BlockId b : f.blocks) {
            std::vector<InstId> &list = f.blockArena[b.v].insts;
            std::vector<InstId> kept;
            for (InstId id : list) {
                if (!dead[id.v]) { kept.push_back(id); continue; }
                if (f.insts[id.v].result.valid()) f.values[f.insts[id.v].result.v].type = TypeId();
            }
            list.swap(kept);
        }
        std::vector<long long> renumber(f.frame.size(), -1);
        std::vector<char> gone(f.frame.size(), 0);
        for (const Slot &s : slots) if (!s.bad) gone[static_cast<std::size_t>(f.inst(f.value(s.addr).def).k)] = 1;
        std::vector<AllocaInfo> frame;
        for (std::size_t k = 0; k < f.frame.size(); k++) {
            if (gone[k]) continue;
            renumber[k] = static_cast<long long>(frame.size());
            frame.push_back(f.frame[k]);
        }
        f.frame.swap(frame);
        for (std::size_t i = 1; i < f.insts.size(); i++)
            if (f.insts[i].op == Op::Alloca && !dead[i]) f.insts[i].k = renumber[static_cast<std::size_t>(f.insts[i].k)];
        for (std::size_t c = 1; c < f.calls.size(); c++)
            if (f.calls[c].resultAlloca >= 0) f.calls[c].resultAlloca = static_cast<int>(renumber[static_cast<std::size_t>(f.calls[c].resultAlloca)]);
    }
};

}   // namespace

void promote(Function &f, const TypeTable &types, PassLog &log) {
    if (!f.entry.valid()) return;
    Promoter p(f, types);
    p.collect();
    p.place();
    int count = 0;
    for (const Slot &s : p.slots) if (!s.bad) count++;
    log.allocas += static_cast<int>(f.frame.size());
    log.scalars += static_cast<int>(p.slots.size());
    log.promoted += count;
    if (count == 0) return;
    p.repl.assign(f.values.size(), ValueId());
    p.dead.assign(f.insts.size(), 0);
    p.rename();
    p.finish();
}

}   // namespace ir
