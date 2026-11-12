// **Dead code and dead stores** (M3b.4 round 3, design section 4). A store to a frame object whose address only loads
// and stores read, which no load sees before it is stored again or the function ends; then, from what has an effect,
// the values it reads: an instruction or a block parameter nothing live reads leaves, an object left unused with it.

#include "SsaOpt.h"

#include <utility>

namespace ir {

namespace {

// What stays whether or not it is read: a store, a call, a pad, a terminator, a volatile access, a trap, and a
// division whose divisor is not a constant the hardware divides by without a fault.
bool effect(const Function &f, const Inst &i) {
    if (i.isVolatile || isTerminator(i.op)) return true;
    switch (i.op) {
    case Op::Store: case Op::Memcpy: case Op::Memset: case Op::Call: case Op::Invoke: case Op::Trap:
    case Op::VaStart: case Op::VaArg: case Op::VaEnd: case Op::CatchPad: case Op::CleanupPad:
        return true;
    case Op::SDiv: case Op::UDiv: case Op::SRem: case Op::URem: {
        const ValueId d = f.operand(i, 1);
        if (!f.value(d).def.valid() || f.inst(f.value(d).def).op != Op::IConst) return true;
        const long long k = f.inst(f.value(d).def).k;
        return k == 0 || k == -1;
    }
    default:
        return false;
    }
}

struct Eliminator {
    Function &f;
    PassLog &log;
    std::vector<char> fixed, dead, liveParam;           // fixed by block; dead by instruction; liveParam by value
    std::vector<std::vector<std::pair<InstId, std::uint32_t> > > into;     // by block: (terminator, successor)

    Eliminator(Function &fn, PassLog &l) : f(fn), log(l), fixed(fixedBlocks(fn)) {}

    template <class Visit> void each(Visit visit) {
        for (BlockId b : f.blocks) {
            const Block &blk = f.block(b);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (id.valid()) visit(b.v, id, f.inst(id));
            }
        }
    }

    // The frame objects only loads and stores of their own type reach through their address, none volatile, none a
    // call's result; then by block the stores no load can see, by a backward liveness over every edge, unwind ones too.
    void stores() {
        std::vector<int> slotOf(f.values.size(), -1);
        std::vector<char> bad;
        std::vector<TypeId> type;
        for (InstId id : f.block(f.entry).insts) {
            const Inst &i = f.inst(id);
            if (i.op != Op::Alloca || !i.result.valid()) continue;
            slotOf[i.result.v] = static_cast<int>(bad.size());
            bad.push_back(0);
            type.push_back(f.frame[static_cast<std::size_t>(i.k)].type);
            for (std::size_t c = 1; c < f.calls.size(); c++) if (f.calls[c].resultAlloca == i.k) bad.back() = 1;
        }
        const std::size_t n = bad.size();
        if (n == 0) return;
        const auto at = [&](ValueId v) { return v.valid() && v.v < slotOf.size() ? slotOf[v.v] : -1; };
        each([&](std::uint32_t, InstId, const Inst &i) {
            for (std::uint32_t k = 0; k < i.count; k++) {
                const int c = at(f.operand(i, k));
                if (c < 0) continue;
                const bool load = i.op == Op::Load && k == 0 && i.type == type[static_cast<std::size_t>(c)];
                const bool store = i.op == Op::Store && k == 0 && i.count == 2 && f.value(f.operand(i, 1)).type == type[static_cast<std::size_t>(c)];
                if ((!load && !store) || i.isVolatile) bad[static_cast<std::size_t>(c)] = 1;
            }
            for (const Succ &s : i.succ) for (std::uint32_t a = 0; a < s.count; a++) if (at(f.arg(s, a)) >= 0) bad[static_cast<std::size_t>(at(f.arg(s, a)))] = 1;
        });
        // A slot's access in a block: +1 a load reads it first, -1 a store kills it first, 0 neither.
        const std::size_t blocks = f.blockArena.size();
        if (n * blocks > (std::size_t(1) << 24)) return;
        std::vector<signed char> first(n * blocks, 0);
        std::vector<char> liveIn(n * blocks, 0), liveOut(n * blocks, 0);
        each([&](std::uint32_t b, InstId, const Inst &i) {
            if ((i.op != Op::Load && i.op != Op::Store) || at(f.operand(i, 0)) < 0) return;
            const std::size_t c = static_cast<std::size_t>(at(f.operand(i, 0)));
            if (!bad[c] && first[b * n + c] == 0) first[b * n + c] = i.op == Op::Load ? 1 : -1;
        });
        for (bool again = true; again; ) {
            again = false;
            for (std::size_t r = f.blocks.size(); r-- > 0; ) {
                const std::uint32_t b = f.blocks[r].v;
                const std::vector<std::uint32_t> to = successors(f, b);
                for (std::size_t c = 0; c < n; c++) {
                    char out = 0;
                    for (std::uint32_t s : to) out = static_cast<char>(out | liveIn[s * n + c]);
                    const char in = first[b * n + c] == 1 ? 1 : first[b * n + c] == -1 ? 0 : out;
                    liveOut[b * n + c] = out;
                    if (in != liveIn[b * n + c]) { liveIn[b * n + c] = in; again = true; }
                }
            }
        }
        std::vector<char> live(n);
        for (BlockId b : f.blocks) {
            const Block &blk = f.block(b);
            for (std::size_t c = 0; c < n; c++) live[c] = liveOut[b.v * n + c];
            for (std::size_t k = blk.insts.size(); k-- > 0; ) {
                const Inst &i = f.inst(blk.insts[k]);
                if ((i.op != Op::Load && i.op != Op::Store) || at(f.operand(i, 0)) < 0) continue;
                const std::size_t c = static_cast<std::size_t>(at(f.operand(i, 0)));
                if (bad[c]) continue;
                if (i.op == Op::Load) { live[c] = 1; continue; }
                if (!live[c]) { dead[blk.insts[k].v] = 1; log.deadStores++; }
                live[c] = 0;
            }
        }
    }

    // A block whose parameters may go: every edge in a jump carrying one argument each - not the entry, nor fixed.
    std::vector<char> droppable() {
        std::vector<char> ok(f.blockArena.size(), 0);
        into.assign(f.blockArena.size(), std::vector<std::pair<InstId, std::uint32_t> >());
        for (BlockId b : f.blocks) ok[b.v] = b.v != f.entry.v && !fixed[b.v] && !f.block(b).params.empty();
        each([&](std::uint32_t, InstId id, const Inst &i) {
            for (std::uint32_t s = 0; s < i.succ.size(); s++) {
                const std::uint32_t to = i.succ[s].block.v;
                into[to].push_back(std::make_pair(id, s));
                if (!isJumpEdge(i, s) || i.succ[s].count != f.block(i.succ[s].block).params.size()) ok[to] = 0;
            }
        });
        return ok;
    }

    // Liveness from the effects back through operands, and through an edge's argument to a live parameter; the
    // parameters of a block that keeps them all are live, so every argument into it is.
    void values(const std::vector<char> &ok) {
        std::vector<char> live(f.insts.size(), 0);
        liveParam.assign(f.values.size(), 0);
        std::vector<ValueId> work;
        std::vector<char> held(f.frame.size(), 0);     // a call's result object, which the call names by index
        for (std::size_t c = 1; c < f.calls.size(); c++) if (f.calls[c].resultAlloca >= 0) held[static_cast<std::size_t>(f.calls[c].resultAlloca)] = 1;
        const auto mark = [&](ValueId v) {
            if (!v.valid()) return;
            const Value &x = f.value(v);
            if (x.def.valid()) { if (!live[x.def.v]) { live[x.def.v] = 1; work.push_back(v); } return; }
            if (x.param.valid() && !liveParam[v.v]) { liveParam[v.v] = 1; if (ok[x.param.v]) work.push_back(v); }
        };
        each([&](std::uint32_t, InstId id, const Inst &i) {
            if (dead[id.v] || !(effect(f, i) || (i.op == Op::Alloca && held[static_cast<std::size_t>(i.k)]))) return;
            live[id.v] = 1;
            for (std::uint32_t k = 0; k < i.count; k++) mark(f.operand(i, k));
            for (const Succ &s : i.succ)
                if (!ok[s.block.v]) for (std::uint32_t a = 0; a < s.count; a++) mark(f.arg(s, a));
        });
        while (!work.empty()) {
            const ValueId v = work.back();
            work.pop_back();
            const Value &x = f.value(v);
            if (x.def.valid()) {
                const Inst &i = f.inst(x.def);
                for (std::uint32_t k = 0; k < i.count; k++) mark(f.operand(i, k));
                continue;
            }
            for (const std::pair<InstId, std::uint32_t> &e : into[x.param.v]) mark(f.arg(f.inst(e.first).succ[e.second], x.index));
        }
        each([&](std::uint32_t, InstId id, const Inst &) { if (!live[id.v]) dead[id.v] = 1; });
    }

    // The dead instructions out of their blocks and their values given no type; the dead parameters out of their
    // blocks and every edge's arguments; the frame objects whose allocas left with them.
    bool finish(const std::vector<char> &ok) {
        bool any = false;
        std::vector<char> gone(f.frame.size(), 0);
        for (BlockId b : f.blocks) {
            std::vector<InstId> &list = f.blockArena[b.v].insts;
            std::vector<InstId> kept;
            for (InstId id : list) {
                if (!dead[id.v]) { kept.push_back(id); continue; }
                const Inst &i = f.inst(id);
                if (i.op == Op::Alloca) { gone[static_cast<std::size_t>(i.k)] = 1; log.deadAllocas++; }
                else if (i.op != Op::Store) log.deadInsts++;
                if (i.result.valid()) f.values[i.result.v].type = TypeId();
                any = true;
            }
            list.swap(kept);
        }
        for (BlockId b : f.blocks) {
            if (!ok[b.v]) continue;
            Block &blk = f.blockArena[b.v];
            std::vector<ValueId> params;
            for (ValueId p : blk.params) if (liveParam[p.v]) params.push_back(p);
            if (params.size() == blk.params.size()) continue;
            for (const std::pair<InstId, std::uint32_t> &e : into[b.v]) {
                Succ &sc = f.insts[e.first.v].succ[e.second];
                const std::uint32_t first = static_cast<std::uint32_t>(f.operands.size());
                for (std::uint32_t a = 0; a < sc.count; a++)
                    if (liveParam[blk.params[a].v]) { const ValueId v = f.operands[sc.first + a]; f.operands.push_back(v); }
                sc.first = first;
                sc.count = static_cast<std::uint32_t>(f.operands.size()) - first;
            }
            for (ValueId p : blk.params) if (!liveParam[p.v]) { f.values[p.v].type = TypeId(); log.deadParams++; }
            for (std::size_t p = 0; p < params.size(); p++) f.values[params[p].v].index = static_cast<std::uint16_t>(p);
            blk.params.swap(params);
            any = true;
        }
        for (std::size_t k = 0; k < gone.size(); k++) if (gone[k]) { dropFrameObjects(f, gone); break; }
        return any;
    }
};

}   // namespace

bool dead(Function &f, PassLog &log) {
    if (!f.entry.valid()) return false;
    Eliminator e(f, log);
    e.dead.assign(f.insts.size(), 0);
    e.stores();
    const std::vector<char> ok = e.droppable();
    e.values(ok);
    return e.finish(ok);
}

}   // namespace ir
