// **Value numbering** (M3b.4 round 4, design section 4): a dominator-tree walk with a scoped table of the pure instructions,
// one found again where its first dominates (same funclet) replaced by it; a load by an earlier load or store of its block,
// or of its one predecessor's (by br or condbr), nothing between that writes or calls; a condbr on a value a dominating
// condbr's edge decided (its loop's way in), a jump.

#include "SsaOpt.h"

#include <map>
#include <utility>

namespace ir {

namespace {

// What makes two pure instructions the same value: everything the selector reads of one.
struct Key {
    Op op;
    Cmp cc;
    std::uint32_t type;
    long long k;
    unsigned long long bits;
    std::string sym;
    std::vector<std::uint32_t> ops;
    bool operator<(const Key &o) const {
        if (op != o.op) return op < o.op;
        if (cc != o.cc) return cc < o.cc;
        if (type != o.type) return type < o.type;
        if (k != o.k) return k < o.k;
        if (bits != o.bits) return bits < o.bits;
        if (sym != o.sym) return sym < o.sym;
        return ops < o.ops;
    }
};

}   // namespace

int pureKind(const Inst &i) {
    switch (i.op) {
    case Op::IConst: case Op::FConst: case Op::Null: case Op::Addr: return 1;
    case Op::Add: case Op::Sub: case Op::Mul: case Op::SDiv: case Op::UDiv: case Op::SRem: case Op::URem: case Op::SMulH:
    case Op::UMulH: case Op::And: case Op::Or: case Op::Xor: case Op::Shl: case Op::LShr: case Op::AShr: case Op::Neg: case Op::Not:
    case Op::FAdd: case Op::FSub: case Op::FMul: case Op::FDiv: case Op::FNeg: return 2;
    case Op::ICmp: case Op::FCmp: return 3;
    case Op::Trunc: case Op::SExt: case Op::ZExt: case Op::FpToSi: case Op::FpToUi: case Op::SiToFp: case Op::UiToFp:
    case Op::FpExt: case Op::FpTrunc: case Op::Bitcast: case Op::PtrToInt: case Op::IntToPtr: return 4;
    case Op::Gep: return 5;
    case Op::Select: return 6;
    default: return 0;
    }
}

namespace {

// Integer operators whose operands may be read in either order; a floating one keeps its order (which NaN comes out).
bool commutes(const Inst &i) {
    return i.op == Op::Add || i.op == Op::Mul || i.op == Op::SMulH || i.op == Op::UMulH || i.op == Op::And || i.op == Op::Or ||
           i.op == Op::Xor || (i.op == Op::ICmp && (i.cc == Cmp::Eq || i.cc == Cmp::Ne));
}

const std::uint32_t kNone = ~0u;

struct Numberer {
    Function &f;
    PassLog &log;
    Dominators dom;
    std::vector<std::uint32_t> blockOf;                 // by value: the block defining it, the entry a parameter
    std::vector<ValueId> repl;
    std::vector<char> gone;
    std::map<Key, ValueId> table;
    std::vector<std::pair<Key, ValueId> > undo;         // what each insertion displaced, none for a new key
    std::vector<signed char> decided;                   // by value: the successor a dominating test took, -1 none
    typedef std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<ValueId, bool> > Memory;   // (address, type): value, stored
    std::vector<Memory> left;                           // by block: what it had read or written at its end
    bool changed, jumped;

    Numberer(Function &fn, PassLog &l) : f(fn), log(l), dom(fn), changed(false), jumped(false) {}

    ValueId resolve(ValueId v) const {
        while (v.valid() && v.v < repl.size() && repl[v.v].valid()) v = repl[v.v];
        return v;
    }
    std::uint32_t funcletOf(ValueId v) const { return f.block(BlockId(blockOf[v.v])).funclet; }

    void replace(InstId id, ValueId v, int &count) {
        repl[f.inst(id).result.v] = v;
        gone[id.v] = 1;
        count++;
        changed = true;
    }

    void number(std::uint32_t b, InstId id) {
        const Inst &i = f.inst(id);
        const int kind = pureKind(i);
        Key key;
        key.op = i.op;
        key.cc = i.cc;
        key.type = i.type.v;
        key.k = i.k;
        key.bits = i.bits;
        key.sym = i.sym;
        for (std::uint32_t n = 0; n < i.count; n++) key.ops.push_back(resolve(f.operand(i, n)).v);
        if (commutes(i) && key.ops.size() == 2 && key.ops[1] < key.ops[0]) std::swap(key.ops[0], key.ops[1]);
        std::map<Key, ValueId>::iterator at = table.find(key);
        if (at != table.end() && funcletOf(at->second) == f.block(BlockId(b)).funclet) {
            replace(id, at->second, kind == 1 ? log.gvnConstants : kind == 2 ? log.gvnArith : kind == 3 ? log.gvnCompares :
                    kind == 4 ? log.gvnCasts : kind == 5 ? log.gvnGeps : log.gvnSelects);
            return;
        }
        // A first, or the first in this funclet: it shadows what the table held until the walk leaves the block.
        undo.push_back(std::make_pair(key, at != table.end() ? at->second : ValueId()));
        table[key] = i.result;
    }

    // One block's instructions in order: the pure numbered, the loads given what the block already read or wrote there.
    void visit(std::uint32_t b) {
        const Block &blk = f.block(BlockId(b));
        if (blk.pad) return;
        Memory memory;
        if (dom.preds[b].size() == 1 && dom.preds[b][0] == dom.idom[b] && b != f.entry.v) {
            const Block &p = f.block(BlockId(dom.preds[b][0]));
            const Op t = p.term.valid() ? f.inst(p.term).op : Op::Ret;
            if (!p.pad && p.funclet == blk.funclet && (t == Op::Br || t == Op::CondBr)) memory = left[dom.preds[b][0]];
        }
        for (InstId id : blk.insts) {
            const Inst &i = f.inst(id);
            if (!i.isVolatile && i.result.valid() && i.type.valid() && pureKind(i) != 0) { number(b, id); continue; }
            const bool load = i.op == Op::Load && i.count == 1 && i.result.valid() && i.type.valid();
            if (i.isVolatile || (!load && (i.op != Op::Store || i.count != 2))) { memory.clear(); continue; }
            if (load) {
                const std::pair<std::uint32_t, std::uint32_t> where(resolve(f.operand(i, 0)).v, i.type.v);
                Memory::iterator at = memory.find(where);
                if (at != memory.end()) replace(id, at->second.first, at->second.second ? log.gvnStored : log.gvnLoads);
                else memory[where] = std::make_pair(i.result, false);
                continue;
            }
            memory.clear();         // a store may write any address another load read
            const ValueId v = resolve(f.operand(i, 1));
            memory[std::make_pair(resolve(f.operand(i, 0)).v, f.value(v).type.v)] = std::make_pair(v, true);
        }
        left[b].swap(memory);
        Inst &t = f.insts[blk.term.valid() ? blk.term.v : 0];
        if (blk.term.valid() && t.op == Op::CondBr && decided[resolve(f.operand(t, 0)).v] >= 0) {
            const Succ s = t.succ[static_cast<std::size_t>(decided[resolve(f.operand(t, 0)).v])];
            t.op = Op::Br;
            t.count = 0;
            t.succ.assign(1, s);
            log.gvnBranches++;
            changed = jumped = true;
        }
    }

    // The value b's condbr tests, decided in c's subtree where one of its edges is c's way in, the rest c's back edges.
    std::uint32_t decide(std::uint32_t b, std::uint32_t c) {
        const Block &blk = f.block(BlockId(b));
        if (blk.pad || !blk.term.valid() || f.inst(blk.term).op != Op::CondBr) return kNone;
        for (std::uint32_t p : dom.preds[c]) if (p != b && !dom.dominates(c, p)) return kNone;
        const Inst &t = f.inst(blk.term);
        const std::uint32_t v = resolve(f.operand(t, 0)).v;
        if (t.succ[0].block.v == t.succ[1].block.v || decided[v] >= 0) return kNone;
        for (std::size_t s = 0; s < 2; s++) if (t.succ[s].block.v == c) decided[v] = static_cast<signed char>(s);
        return decided[v] >= 0 ? v : kNone;
    }

    void run() {
        blockOf.assign(f.values.size(), 0);
        repl.assign(f.values.size(), ValueId());
        gone.assign(f.insts.size(), 0);
        decided.assign(f.values.size(), -1);
        left.assign(f.blockArena.size(), Memory());
        for (ValueId p : f.params) blockOf[p.v] = f.entry.v;
        if (f.sret.valid()) blockOf[f.sret.v] = f.entry.v;
        for (BlockId b : f.blocks)
            for (InstId id : f.block(b).insts) if (f.inst(id).result.valid()) blockOf[f.inst(id).result.v] = b.v;
        // The dominator tree, each block's children in reverse postorder; walked by an explicit stack.
        std::vector<std::vector<std::uint32_t> > children(f.blockArena.size());
        for (std::size_t r = 1; r < dom.rpo.size(); r++) children[dom.idom[dom.rpo[r]]].push_back(dom.rpo[r]);
        std::vector<std::pair<std::uint32_t, std::size_t> > stack;      // (block, the undo log's size on entry)
        std::vector<std::uint32_t> set;                                 // by depth: the value its edge decided, or kNone
        std::vector<std::size_t> next(f.blockArena.size(), 0);
        stack.push_back(std::make_pair(f.entry.v, undo.size()));
        set.push_back(kNone);
        visit(f.entry.v);
        while (!stack.empty()) {
            const std::uint32_t b = stack.back().first;
            if (next[b] < children[b].size()) {
                const std::uint32_t c = children[b][next[b]++];
                stack.push_back(std::make_pair(c, undo.size()));
                set.push_back(decide(b, c));
                visit(c);
                continue;
            }
            if (set.back() != kNone) decided[set.back()] = -1;
            set.pop_back();
            for (std::size_t u = undo.size(); u > stack.back().second; u--) {
                const std::pair<Key, ValueId> &e = undo[u - 1];
                if (e.second.valid()) table[e.first] = e.second;
                else table.erase(e.first);
            }
            undo.resize(stack.back().second);
            stack.pop_back();
        }
    }

    // Every use re-pointed; the replaced instructions out of their blocks, each value given no type (no slot).
    void finish() {
        for (std::size_t k = 0; k < f.operands.size(); k++) f.operands[k] = resolve(f.operands[k]);
        for (BlockId b : f.blocks) {
            std::vector<InstId> &list = f.blockArena[b.v].insts;
            std::vector<InstId> kept;
            for (InstId id : list) {
                if (!gone[id.v]) { kept.push_back(id); continue; }
                f.values[f.insts[id.v].result.v].type = TypeId();
            }
            list.swap(kept);
        }
    }
};

}   // namespace

bool values(Function &f, PassLog &log) {
    if (!f.entry.valid()) return false;
    Numberer p(f, log);
    p.run();
    if (p.changed) p.finish();
    if (p.jumped) cfg(f, log);      // what a decided test no longer reaches
    return p.changed;
}

}   // namespace ir
