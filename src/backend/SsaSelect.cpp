// **Selection of the SSA IR into the x86 walker's stream** (M3b.1, doc-cxx3/M3-IR.md 3.9). An SSA value has an 8-byte slot in
// the walker's canonical form (an x87 value 16), a local MIR's allocator gives a register (Local::value); an address of the
// frame, a constant and null have none, made at each use. Operands go through rax/xmm0/st0 and scratch; the ABI is the walker's.

#include "X86_64Linux.h"
#include "../ir/opt/SsaOpt.h"
#include "../Source.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {


long long roundUp(long long n, long long a) { return (n + a - 1) / a * a; }

typedef std::pair<int, std::uint32_t> Stay;     // how many loops an edge stays in, and its target
bool staysLess(const Stay &x, const Stay &y) { return x.first < y.first; }

// **The order the body's own blocks are written in where the passes run**: reverse postorder over the jumps, a loop's
// way out walked before its way on - its blocks stay together, what follows it after its latch (thread-jumps rotates
// that), an invoke's return under its call. A pad and a funclet's blocks keep their places; the rest fill the others.
std::vector<ir::BlockId> blockOrder(const ir::Function &f) {
    if (f.blocks.empty() || f.blocks[0].v != f.entry.v) return f.blocks;
    const ir::Dominators dom(f);
    const std::size_t n = f.blockArena.size();
    // The natural loops, one per head a jump goes back to: the blocks of each.
    std::vector<std::vector<char> > loops;
    std::vector<int> loopOf(n, -1);
    for (std::uint32_t b : dom.rpo) {
        if (!f.blockArena[b].term.valid()) continue;
        const ir::Inst &t = f.inst(f.blockArena[b].term);
        for (std::size_t s = 0; s < t.succ.size(); s++) {
            const std::uint32_t h = t.succ[s].block.v;
            if (!ir::isJumpEdge(t, s) || !dom.reachable(h) || !dom.dominates(h, b)) continue;
            if (loopOf[h] < 0) {
                loopOf[h] = static_cast<int>(loops.size());
                loops.push_back(std::vector<char>(n, 0));
                loops.back()[h] = 1;
            }
            std::vector<char> &in = loops[static_cast<std::size_t>(loopOf[h])];
            std::vector<std::uint32_t> work(1, b);
            while (!work.empty()) {
                const std::uint32_t x = work.back();
                work.pop_back();
                if (in[x]) continue;
                in[x] = 1;
                work.insert(work.end(), dom.preds[x].begin(), dom.preds[x].end());
            }
        }
    }
    std::vector<char> plain(n, 0), seen(n, 0);
    for (ir::BlockId b : f.blocks) plain[b.v] = !f.block(b).pad && f.block(b).funclet == 0;
    std::vector<ir::BlockId> walk;
    for (std::size_t r = 0; r < f.blocks.size(); r++) {
        if (!plain[f.blocks[r].v] || seen[f.blocks[r].v]) continue;
        // One tree, by an explicit stack: each block's jumps sorted so that the one staying deepest is walked last.
        std::vector<std::uint32_t> post;
        std::vector<std::pair<std::uint32_t, std::vector<Stay> > > stack;
        std::uint32_t enter = f.blocks[r].v;
        while (enter != 0 || !stack.empty()) {
            if (enter != 0) {
                seen[enter] = 1;
                std::vector<Stay> to;
                if (f.blockArena[enter].term.valid()) {
                    const ir::Inst &t = f.inst(f.blockArena[enter].term);
                    for (std::size_t s = t.succ.size(); s-- > 0; ) {
                        const std::uint32_t d = t.succ[s].block.v;
                        if (!ir::isJumpEdge(t, s) || d >= n || !plain[d]) continue;
                        int stays = 0;
                        for (const std::vector<char> &in : loops) stays += in[enter] && in[d] ? 1 : 0;
                        to.push_back(Stay(stays, d));
                    }
                }
                // Popped from the back, the way out first; among equals the true side, or the default, is walked last.
                std::stable_sort(to.begin(), to.end(), staysLess);
                std::reverse(to.begin(), to.end());
                stack.push_back(std::make_pair(enter, to));
                enter = 0;
            }
            std::vector<Stay> &to = stack.back().second;
            if (to.empty()) { post.push_back(stack.back().first); stack.pop_back(); continue; }
            const std::uint32_t d = to.back().second;
            to.pop_back();
            if (!seen[d]) enter = d;
        }
        for (std::size_t k = post.size(); k-- > 0; ) walk.push_back(ir::BlockId(post[k]));
    }
    std::vector<ir::BlockId> out = f.blocks;
    std::size_t w = 0;
    for (std::size_t k = 0; k < out.size(); k++) if (plain[out[k].v]) out[k] = walk[w++];
    return out;
}

}   // namespace

// The function `fn` is selected from SSA when the module holds its symbol.
const ir::Function *X86_64Linux::ssaFunction(const Function &fn) const {
    if (ssa_ == nullptr) return nullptr;
    const ir::FuncId id = ssa_->find(fn.symbol());
    return id.valid() ? &ssa_->functions[id.v] : nullptr;
}

// Parser::passedByAddress: a class whose copy is a call goes by address on both ABIs, one whose destruction is on Itanium.
bool X86_64Linux::ssaByAddress(const Type *t) const {
    return t->nonTrivialCopy() || (!target_.microsoftNames() && t->hasDestructor());
}

// The frame above the parser's parameter slots - the one part of its layout the selector reads - each alloca at its
// alignment, then one 8-byte slot per value, an x87 value's 16. Returns the new size.
int X86_64Linux::layoutSsa(const Function &fn, const ir::Function &f) {
    long long cur = 0;
    for (const Param &p : fn.params()) cur = std::max(cur, static_cast<long long>(p.offset));
    cur = std::max(cur, static_cast<long long>(fn.sretSlot()));   // where the prologue keeps the caller's result pointer
    cur = std::max(cur, static_cast<long long>(fn.regSaveSlot()));   // and a variadic function's register save area
    cur = roundUp(cur, 16);
    ssaAllocaSlot_.assign(f.frame.size(), 0);
    for (std::size_t i = 0; i < f.frame.size(); i++) {
        const Type *t = ssaTypes_->type(f.frame[i].type);
        const int size = t->size(target_), align = f.frame[i].align > 0 ? f.frame[i].align : t->align(target_);
        cur = roundUp(cur + size, align);
        ssaAllocaSlot_[i] = cur;
    }
    // A class passed by value has a copy of its own in the frame on the Microsoft ABI, as the parser gives each call -
    // an invoke's too, or its copies are written at slot 0, over the saved rbp and the return address.
    ssaArgSlots_.assign(f.calls.size(), std::vector<int>());
    for (std::size_t n = 1; n < f.insts.size(); n++) {
        const ir::Inst &i = f.insts[n];
        if (i.op != ir::Op::Call && i.op != ir::Op::Invoke) continue;
        const ir::CallInfo &c = f.calls[i.call];
        const Type *sig = ssaTypes_->type(c.signature);
        const std::uint32_t firstArg = c.symbol.empty() ? 1 : 0;
        std::vector<int> &slots = ssaArgSlots_[i.call];
        for (std::uint32_t k = firstArg; k < i.count; k++) {
            const std::size_t at = k - firstArg;
            const Type *want = at < sig->params().size() ? sig->params()[at] : nullptr;
            slots.push_back(0);
            if (want == nullptr || !want->isStructOrUnion() || ssaByAddress(want)) continue;
            cur = roundUp(cur + want->size(target_), want->align(target_));
            slots.back() = static_cast<int>(cur);
        }
    }
    // Under the passes a frame address derived has no slot either - but one a call, a va_ or a return reads past the
    // object's start keeps its own: the walker's node there is `&` of a slot, and its `lea` would hide the object's start.
    ssaAddr_.assign(f.values.size(), 0);
    std::size_t frame = 0;
    long long off = 0;
    for (int pass = 0; passesRun() && pass < 2; pass++)
        for (std::size_t b = 0; b < f.blocks.size(); b++) {
            const ir::Block &blk = f.block(f.blocks[b]);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!id.valid()) continue;
                const ir::Inst &i = f.inst(id);
                const bool reads = i.op == ir::Op::Call || i.op == ir::Op::Invoke || i.op == ir::Op::VaStart || i.op == ir::Op::VaArg || i.op == ir::Op::Ret;
                if (pass == 0 && i.result.valid() && (i.op == ir::Op::Bitcast || i.op == ir::Op::Gep))
                    ssaAddr_[i.result.v] = ssaFrameAddress(f, i.result, frame, off);
                for (std::uint32_t n = 0; pass == 1 && reads && n < i.count; n++) {
                    const ir::ValueId v = f.operand(i, n);
                    if (ssaAddr_[v.v] && ssaFrameAddress(f, v, frame, off) && off != 0) ssaAddr_[v.v] = 0;
                }
            }
        }
    ssaFoldGeps(f);
    ssaNarrowValues(f);
    ssaValueSlot_.assign(f.values.size(), 0);
    for (std::size_t i = 1; i < f.values.size(); i++) {
        if (!f.values[i].type.valid() || ssaTypes_->type(f.values[i].type)->isVoid()) continue;
        if (ssaRemat(f, ir::ValueId(static_cast<std::uint32_t>(i)))) continue;
        cur = isX87(ssaTypes_->type(f.values[i].type)) ? roundUp(cur + 16, 16) : cur + 8;
        ssaValueSlot_[i] = cur;
    }
    // The word the Microsoft runtime scribbles its state in, where the function has a pad (M3-IR 3.6).
    ssaUnwindHelpSlot_ = 0;
    if (usesFunclets() && landingPads_) { cur = roundUp(cur, 8) + 8; ssaUnwindHelpSlot_ = static_cast<int>(cur); }
    return static_cast<int>(roundUp(cur, 16));
}

// The slots the passes may promote: every value, and every alloca of a scalar.
void X86_64Linux::ssaScalars(const ir::Function &f, std::vector<opt::Local> &into) const {
    for (std::size_t i = 0; i < f.frame.size(); i++) {
        const Type *t = ssaTypes_->type(f.frame[i].type);
        if (t->isInteger() || t->isPointer() || t->isFloating())
            into.push_back(opt::Local{-ssaAllocaSlot_[i], t->size(target_), t->isFloating()});
    }
    for (std::size_t i = 1; i < f.values.size(); i++) {
        if (ssaValueSlot_[i] == 0) continue;
        const Type *t = ssaTypes_->type(f.values[i].type);
        into.push_back(opt::Local{-ssaValueSlot_[i], t->isFloating() ? t->size(target_) : 8, t->isFloating(), !t->isFloating()});
    }
}

Op X86_64Linux::vslot(ir::ValueId v) const { return local(ssaValueSlot_[v.v]); }
const Type *X86_64Linux::typeOf(const ir::Function &f, ir::ValueId v) const { return ssaTypes_->type(f.value(v).type); }

bool X86_64Linux::ssaRemat(const ir::Function &f, ir::ValueId v) const {
    if (!f.value(v).def.valid()) return false;
    const ir::Op op = f.inst(f.value(v).def).op;
    return op == ir::Op::Alloca || op == ir::Op::IConst || op == ir::Op::Null || ssaAddr_[v.v] != 0 || ssaFold_[v.v] != 0;
}

// Whether `v` is a frame object's address or one derived from it by pointer casts and constant geps: the object, the distance.
bool X86_64Linux::ssaFrameAddress(const ir::Function &f, ir::ValueId v, std::size_t &frame, long long &off) const {
    for (off = 0; f.value(v).def.valid(); v = f.operand(f.inst(f.value(v).def), 0)) {
        const ir::Inst &d = f.inst(f.value(v).def);
        if (d.op == ir::Op::Alloca) { frame = static_cast<std::size_t>(d.k); return true; }
        if (d.op == ir::Op::Bitcast && typeOf(f, v)->isPointer() && typeOf(f, f.operand(d, 0))->isPointer()) continue;
        if (d.op != ir::Op::Gep) return false;
        if (d.count == 1) { off += d.k; continue; }
        const ir::Value &by = f.value(f.operand(d, 1));
        if (!by.def.valid() || f.inst(by.def).op != ir::Op::IConst) return false;
        off += f.inst(by.def).k;
    }
    return false;
}

// Under the passes, a gep of a variable byte offset whose every use is the address of a scalar's load or store, none in
// a pad or a funclet (which read the body's values from their slots).
void X86_64Linux::ssaFoldGeps(const ir::Function &f) {
    ssaFold_.assign(f.values.size(), 0);
    if (!passesRun()) return;
    std::vector<int> good(f.values.size(), 0), all(f.values.size(), 0);
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const ir::Inst &i = f.inst(id);
            for (std::uint32_t n = 0; n < i.count; n++) all[f.operand(i, n).v]++;
            for (std::size_t s = 0; s < i.succ.size(); s++)
                for (std::uint32_t n = 0; n < i.succ[s].count; n++) all[f.arg(i.succ[s], n).v]++;
            const Type *t = i.op == ir::Op::Load ? ssaTypes_->type(i.type) : i.op == ir::Op::Store ? typeOf(f, f.operand(i, 1)) : nullptr;
            if (t != nullptr && !blk.pad && blk.funclet == 0 && (t->isInteger() || t->isPointer() || t->isFloating()) && f.operand(i, 0).v != (i.count > 1 ? f.operand(i, 1).v : 0))
                good[f.operand(i, 0).v]++;
        }
    }
    for (std::size_t v = 1; v < f.values.size(); v++) {
        const ir::Value &val = f.values[v];
        if (!val.def.valid() || ssaAddr_[v] || all[v] == 0 || good[v] != all[v]) continue;
        const ir::Inst &d = f.inst(val.def);
        ssaFold_[v] = d.op == ir::Op::Gep && d.count == 2;
    }
    // A signed 32-bit multiply-high by a constant whose one reader is a constant shift of its block is made there, one shift.
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        for (std::size_t k = 0; k < blk.insts.size() && !blk.pad && blk.funclet == 0; k++) {
            const ir::Inst &i = f.inst(blk.insts[k]);
            if (i.op != ir::Op::AShr || i.count != 2 || ssaTypes_->type(i.type)->size(target_) != 4) continue;
            const ir::ValueId m = f.operand(i, 0);
            const ir::Value &by = f.value(f.operand(i, 1));
            if (!by.def.valid() || f.inst(by.def).op != ir::Op::IConst || f.inst(by.def).k < 1 || f.inst(by.def).k > 31) continue;
            const ir::InstId def = f.value(m).def;
            if (all[m.v] != 1 || !def.valid() || f.inst(def).op != ir::Op::SMulH || f.inst(def).type != i.type) continue;
            const ir::Value &c = f.value(f.operand(f.inst(def), 1));
            bool here = false;
            for (std::size_t j = 0; j < k; j++) here = here || blk.insts[j].v == def.v;
            if (here && c.def.valid() && f.inst(c.def).op == ir::Op::IConst) ssaFold_[m.v] = 1;
        }
    }
}

// Under the passes, a 4-byte integer whose every reader takes its low four bytes alone - 32-bit arithmetic, a 32-bit
// compare, a block parameter read so - is stored as computed: no extension after the instruction that makes it.
void X86_64Linux::ssaNarrowValues(const ir::Function &f) {
    ssaNarrow_.assign(f.values.size(), 0);
    if (!passesRun()) return;
    for (std::size_t v = 1; v < f.values.size(); v++) {
        const Type *t = f.values[v].type.valid() ? ssaTypes_->type(f.values[v].type) : nullptr;
        ssaNarrow_[v] = t != nullptr && t->isInteger() && t->size(target_) == 4;
    }
    for (bool changed = true; changed; ) {
        changed = false;
        for (std::size_t b = 0; b < f.blocks.size(); b++) {
            const ir::Block &blk = f.block(f.blocks[b]);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!id.valid()) continue;
                const ir::Inst &i = f.inst(id);
                const bool arith = i.op == ir::Op::Add || i.op == ir::Op::Sub || i.op == ir::Op::Mul || i.op == ir::Op::And || i.op == ir::Op::Or ||
                                   i.op == ir::Op::Xor || i.op == ir::Op::Neg || i.op == ir::Op::Not;
                const Type *t = arith ? ssaTypes_->type(i.type) : i.op == ir::Op::ICmp ? typeOf(f, f.operand(i, 0)) : nullptr;
                const bool low = t != nullptr && t->isInteger() && t->size(target_) == 4 && !blk.pad && blk.funclet == 0;
                for (std::uint32_t n = 0; n < i.count; n++) {
                    char &at = ssaNarrow_[f.operand(i, n).v];
                    if (at && !low) { at = 0; changed = true; }
                }
                for (std::size_t s = 0; s < i.succ.size(); s++)
                    for (std::uint32_t n = 0; n < i.succ[s].count; n++) {
                        char &at = ssaNarrow_[f.arg(i.succ[s], n).v];
                        if (at && !ssaNarrow_[f.block(i.succ[s].block).params[n].v]) { at = 0; changed = true; }
                    }
            }
        }
    }
}

// The register a folded gep's offset is read from: its shift or multiply by 1, 2, 4 or 8 taken as the scale, an added
// constant as the displacement - at 64 bits, where both are exact. None where the offset is a constant.
ir::ValueId X86_64Linux::ssaIndex(const ir::Function &f, ir::ValueId v, int &scale, long long &disp) const {
    scale = 1;
    disp = 0;
    for (int step = 0; step < 2 && f.value(v).def.valid(); step++) {
        const ir::Inst &d = f.inst(f.value(v).def);
        if (d.op == ir::Op::IConst) { disp += d.k * scale; return ir::ValueId(); }
        if (d.count != 2 || typeOf(f, v)->size(target_) != 8) break;
        const ir::Value &by = f.value(f.operand(d, 1));
        if (!by.def.valid() || f.inst(by.def).op != ir::Op::IConst) break;
        const long long c = f.inst(by.def).k;
        if (step == 0 && d.op == ir::Op::Shl && c >= 0 && c <= 3) { scale = 1 << c; v = f.operand(d, 0); continue; }
        if (step == 0 && d.op == ir::Op::Mul && (c == 1 || c == 2 || c == 4 || c == 8)) { scale = static_cast<int>(c); v = f.operand(d, 0); continue; }
        if ((d.op == ir::Op::Add || d.op == ir::Op::Sub) && c > -(1LL << 24) && c < (1LL << 24)) {
            disp += (d.op == ir::Op::Add ? c : -c) * scale;
            v = f.operand(d, 0);
            step = 1;
            continue;
        }
        break;
    }
    return v;
}

// The address `p` into `base`: a folded gep made again at each access, its index in `index` - displaced, shifted, added,
// the shape fold-index turns into the access's own operand once registers are allocated.
void X86_64Linux::ssaAddress(const ir::Function &f, ir::ValueId p, const char *base, const char *index) {
    if (!ssaFold_[p.v]) { ssaLoadReg(f, p, base); return; }
    const ir::Inst &d = f.inst(f.value(p).def);
    int scale = 1;
    long long disp = 0;
    const ir::ValueId at = ssaIndex(f, f.operand(d, 1), scale, disp);
    if (at.valid()) {
        ssaLoadReg(f, at, index);
        if (disp != 0) a_->ins("add", imm(disp / scale), reg(index));
        if (scale > 1) a_->ins("shl", imm(scale == 2 ? 1 : scale == 4 ? 2 : 3), reg(index));
    }
    ssaLoadReg(f, f.operand(d, 0), base);
    if (at.valid()) a_->ins("add", reg(index), reg(base));
    else if (disp != 0) a_->ins("add", imm(disp), reg(base));
}

// A general value into the register named: one made afresh where it has no slot.
void X86_64Linux::ssaLoadReg(const ir::Function &f, ir::ValueId v, const char *r) {
    if (!ssaRemat(f, v)) { a_->ins("mov", vslot(v), reg(r)); return; }
    const ir::Inst &d = f.inst(f.value(v).def);
    std::size_t frame = 0;
    long long off = 0;
    if (d.op != ir::Op::IConst && d.op != ir::Op::Null && ssaFrameAddress(f, v, frame, off)) {
        a_->ins("lea", local(ssaAllocaSlot_[frame]), reg(r));
        if (off != 0) a_->ins("add", imm(off), reg(r));
    } else {
        a_->ins("mov", d.op == ir::Op::IConst ? imm(d.k) : immText("0"), reg(r));
    }
}

// The value as the walker's node: its slot's Var, or what makes it afresh - a frame object's `&`, a constant's number.
ExprPtr X86_64Linux::ssaExpr(const ir::Function &f, ir::ValueId v) const {
    if (!ssaRemat(f, v)) {
        std::unique_ptr<Var> p(Var::local("$v" + std::to_string(v.v), static_cast<int>(ssaValueSlot_[v.v])));
        p->setType(typeOf(f, v));
        return ExprPtr(p.release());
    }
    const ir::Inst &d = f.inst(f.value(v).def);
    if (d.op == ir::Op::IConst || d.op == ir::Op::Null) {
        std::unique_ptr<Num> n = make_owned<Num>(d.op == ir::Op::IConst ? static_cast<long long>(d.k) : 0LL);
        n->setType(typeOf(f, v));
        return ExprPtr(n.release());
    }
    // The object itself, or a cast of its address (layoutSsa leaves no other here).
    std::size_t k = 0;
    long long off = 0;
    ssaFrameAddress(f, v, k, off);
    std::unique_ptr<Var> o(Var::local("$a" + std::to_string(k), static_cast<int>(ssaAllocaSlot_[k])));
    o->setType(ssaTypes_->type(f.frame[k].type));
    std::unique_ptr<Unary> addr = make_owned<Unary>('&', ExprPtr(o.release()));
    addr->setType(typeOf(f, v));
    return ExprPtr(addr.release());
}

// A value into the accumulator: rax, xmm0 for a float or double, st0 for an x87 value.
void X86_64Linux::ssaLoad(const ir::Function &f, ir::ValueId v) {
    const Type *t = typeOf(f, v);
    if (isX87(t)) a_->ins("fldt", vslot(v));
    else if (t->isFloating()) a_->ins(genKind(t) == Kind::Float ? "movss" : "movsd", vslot(v), reg("%xmm0"));
    else ssaLoadReg(f, v, "%rax");
}

// The accumulator into a value's slot; st0 is popped into it.
void X86_64Linux::ssaStore(const ir::Function &f, ir::ValueId v) {
    if (!v.valid() || ssaValueSlot_[v.v] == 0) return;
    const Type *t = typeOf(f, v);
    if (isX87(t)) a_->ins("fstpt", vslot(v));
    else if (t->isFloating()) a_->ins(genKind(t) == Kind::Float ? "movss" : "movsd", reg("%xmm0"), vslot(v));
    else a_->ins("mov", reg("%rax"), vslot(v));
}

std::string X86_64Linux::ssaBlockLabel(ir::BlockId b) const { return label("ssa", static_cast<int>(b.v)); }

// A branch's arguments into the target's parameter slots as a parallel move: a copy whose parameter no copy left reads
// goes first, through the accumulator, and the allocator coalesces it where both slots became registers. A cycle left
// over is staged on the stack, so that each of its arguments is read before any is written.
void X86_64Linux::ssaBranchArgs(const ir::Function &f, const ir::Succ &s) {
    const ir::Block &to = f.block(s.block);
    std::vector<std::pair<ir::ValueId, ir::ValueId>> moves;
    for (std::uint32_t i = 0; i < s.count; i++)
        if (f.arg(s, i).v != to.params[i].v && ssaValueSlot_[to.params[i].v] != 0) moves.push_back(std::make_pair(f.arg(s, i), to.params[i]));
    for (std::size_t m = 0; m < moves.size(); ) {
        bool read = false;
        for (std::size_t o = 0; o < moves.size(); o++) read = read || (o != m && moves[o].first.v == moves[m].second.v);
        if (read) { m++; continue; }
        ssaLoad(f, moves[m].first);
        ssaStore(f, moves[m].second);
        moves.erase(moves.begin() + static_cast<std::ptrdiff_t>(m));
        m = 0;
    }
    for (std::size_t i = 0; i < moves.size(); i++) {
        const ir::ValueId v = moves[i].first;
        ssaLoad(f, v);
        if (isX87(typeOf(f, v))) { pushX87(); continue; }
        if (typeOf(f, v)->isFloating()) a_->ins("movq", reg("%xmm0"), reg("%rax"));
        pushArg();
    }
    for (std::size_t i = moves.size(); i-- > 0; ) {
        const ir::ValueId p = moves[i].second;
        if (isX87(typeOf(f, p))) { popX87(); ssaStore(f, p); continue; }
        a_->ins("pop", reg("%rax"));
        depth_--;
        if (typeOf(f, p)->isFloating()) a_->ins("movq", reg("%rax"), reg("%xmm0"));
        ssaStore(f, p);
    }
}

void X86_64Linux::ssaUnsupported(const ir::Inst &i) {
    std::fprintf(stderr, "codegen: the SSA selector has no rule for '%s' yet\n", ir::opName(i.op).c_str());
    compileFailed();
}

// One instruction. The operands are read from their slots; the result goes to its own.
void X86_64Linux::selectInst(const ir::Function &f, const ir::Inst &i) {
    const Type *rt = i.type.valid() ? ssaTypes_->type(i.type) : nullptr;
    if (i.result.valid() && (ssaAddr_[i.result.v] || ssaFold_[i.result.v])) return;   // made at each use
    // Of 32 bits by a constant, under the passes: one 64-bit multiply of the two extended, its high half shifted down.
    const auto high = [&](const ir::Inst &m, long long shift) {
        const bool sign = m.op == ir::Op::SMulH;
        const long long c = f.inst(f.value(f.operand(m, 1)).def).k;
        ssaLoad(f, f.operand(m, 0));
        if (sign) a_->ins("movslq", reg("%eax"), reg("%rax"));
        else      a_->ins("mov", reg("%eax"), reg("%eax"));
        a_->ins("mov", imm(sign ? static_cast<long long>(static_cast<std::int32_t>(c)) : static_cast<long long>(static_cast<std::uint32_t>(c))), reg(abi_.scratch));
        a_->ins("imul", reg(abi_.scratch), reg("%rax"));
        a_->ins(sign ? "sar" : "shr", imm(shift), reg("%rax"));
        if (sign != rt->isSigned(target_)) canonicalise(rt);    // the shift already extended it the result's way
        ssaStore(f, i.result);
    };
    switch (i.op) {
    case ir::Op::Alloca:
    case ir::Op::IConst:
    case ir::Op::Null:   // made afresh at each use (ssaRemat)
    case ir::Op::Unreachable:
    case ir::Op::Undef:
        return;
    case ir::Op::FConst:
        if (isX87(rt)) {
            // Written straight into the slot: the significand, the sign and exponent in the word above it.
            a_->ins("movabs", imm(i.bits), reg("%rax"));
            a_->ins("mov", reg("%rax"), vslot(i.result));
            a_->ins("movw", imm(i.k), local(ssaValueSlot_[i.result.v] - 8));
            return;
        }
        if (genKind(rt) == Kind::Float) {
            a_->ins("mov", imm(static_cast<unsigned int>(i.bits)), reg("%eax"));
            a_->ins("movd", reg("%eax"), reg("%xmm0"));
        } else {
            a_->ins("movabs", imm(i.bits), reg("%rax"));
            a_->ins("movq", reg("%rax"), reg("%xmm0"));
        }
        ssaStore(f, i.result);
        return;
    case ir::Op::Addr:
        if (target_.microsoftNames() || definedHere_.count(i.sym) != 0) a_->ins("lea", rip(i.sym), reg("%rax"));
        else a_->ins("mov", rip(i.sym + "@GOTPCREL"), reg("%rax"));
        if (i.k != 0) a_->ins("add", imm(i.k), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case ir::Op::TlsAddr:   // the walker's thread_local address, `k` set where a use goes through the ABI's wrapper
        tlsAddress(i.sym, i.k != 0);
        ssaStore(f, i.result);
        return;
    case ir::Op::Load:
        ssaAddress(f, f.operand(i, 0), "%rax", abi_.scratch);
        load(rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::Store: {
        const ir::ValueId p = f.operand(i, 0), v = f.operand(i, 1);
        ssaLoad(f, v);
        ssaAddress(f, p, abi_.scratch, "%rcx");
        store(typeOf(f, v));
        return;
    }
    case ir::Op::Memcpy:
        // The walker's block copy: from (%rax) to (scratch).
        ssaLoad(f, f.operand(i, 1));
        ssaLoadReg(f, f.operand(i, 0), abi_.scratch);
        copyBlock(static_cast<int>(i.k));
        return;
    case ir::Op::Memset: {
        ssaLoad(f, f.operand(i, 0));
        long long off = 0;
        for (; off + 8 <= i.k; off += 8) a_->ins("movq", immText("0"), mem(static_cast<int>(off), "%rax"));
        for (; off + 4 <= i.k; off += 4) a_->ins("movl", immText("0"), mem(static_cast<int>(off), "%rax"));
        for (; off < i.k; off++) a_->ins("movb", immText("0"), mem(static_cast<int>(off), "%rax"));
        return;
    }
    case ir::Op::Gep:
        if (i.count == 2) ssaLoadReg(f, f.operand(i, 1), abi_.scratch);
        ssaLoad(f, f.operand(i, 0));
        if (i.count == 2) a_->ins("add", reg(abi_.scratch), reg("%rax"));
        else if (i.k != 0) a_->ins("add", imm(i.k), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case ir::Op::Add: case ir::Op::Sub: case ir::Op::Mul: case ir::Op::And: case ir::Op::Or: case ir::Op::Xor:
    case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem: case ir::Op::URem: case ir::Op::Shl: case ir::Op::LShr: case ir::Op::AShr: {
        const bool wide = rt->size(target_) == 8;
        const char *acc_ = acc(rt), *d = rhs(rt);
        if (i.op == ir::Op::Shl || i.op == ir::Op::LShr || i.op == ir::Op::AShr) {
            // Under the passes a constant count inside the width is the instruction's own, and rcx stays free.
            const ir::Value &by = f.value(f.operand(i, 1));
            const bool fixed = passesRun() && by.def.valid() && f.inst(by.def).op == ir::Op::IConst && f.inst(by.def).k >= 0 &&
                               f.inst(by.def).k < (wide ? 64 : 32);
            const ir::ValueId of = f.operand(i, 0);
            if (fixed && ssaFold_[of.v] && f.inst(f.value(of).def).op == ir::Op::SMulH) { high(f.inst(f.value(of).def), 32 + f.inst(by.def).k); return; }
            if (!fixed) ssaLoadReg(f, f.operand(i, 1), "%rcx");
            ssaLoad(f, f.operand(i, 0));
            a_->ins(i.op == ir::Op::Shl ? "shl" : i.op == ir::Op::AShr ? "sar" : "shr", fixed ? imm(f.inst(by.def).k) : reg("%cl"), reg(acc_));
            canonicalise(rt);
            ssaStore(f, i.result);
            return;
        }
        ssaLoadReg(f, f.operand(i, 1), abi_.scratch);
        ssaLoad(f, f.operand(i, 0));
        switch (i.op) {
        case ir::Op::Add: a_->ins("add", reg(d), reg(acc_)); break;
        case ir::Op::Sub: a_->ins("sub", reg(d), reg(acc_)); break;
        case ir::Op::Mul: a_->ins("imul", reg(d), reg(acc_)); break;
        case ir::Op::And: a_->ins("and", reg(d), reg(acc_)); break;
        case ir::Op::Or:  a_->ins("or", reg(d), reg(acc_)); break;
        case ir::Op::Xor: a_->ins("xor", reg(d), reg(acc_)); break;
        default: {
            const bool sign = i.op == ir::Op::SDiv || i.op == ir::Op::SRem;
            if (sign) a_->ins(wide ? "cqo" : "cdq");
            else      a_->ins("xor", reg("%edx"), reg("%edx"));
            a_->ins(sign ? "idiv" : "div", reg(d));
            if (i.op == ir::Op::SRem || i.op == ir::Op::URem) a_->ins("mov", reg(wide ? "%rdx" : "%edx"), reg(acc_));
            break;
        }
        }
        if (!ssaNarrow_[i.result.v]) canonicalise(rt);
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::SMulH: case ir::Op::UMulH: {
        const ir::Value &by = f.value(f.operand(i, 1));
        if (passesRun() && rt->size(target_) == 4 && by.def.valid() && f.inst(by.def).op == ir::Op::IConst) { high(i, 32); return; }
        // The one-operand multiply: rdx:rax, of which rdx is the result.
        ssaLoadReg(f, f.operand(i, 1), abi_.scratch);
        ssaLoad(f, f.operand(i, 0));
        a_->ins(i.op == ir::Op::SMulH ? "imul" : "mul", reg(rhs(rt)));
        a_->ins("mov", reg(rt->size(target_) == 8 ? "%rdx" : "%edx"), reg(acc(rt)));
        canonicalise(rt);
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::Neg: case ir::Op::Not:
        ssaLoad(f, f.operand(i, 0));
        a_->ins(i.op == ir::Op::Neg ? "neg" : "not", reg(acc(rt)));
        if (!ssaNarrow_[i.result.v]) canonicalise(rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::ICmp: {
        const Type *t = typeOf(f, f.operand(i, 0));
        ssaLoadReg(f, f.operand(i, 1), abi_.scratch);
        ssaLoad(f, f.operand(i, 0));
        a_->ins("cmp", reg(rhs(t)), reg(acc(t)));
        const char *set = "sete";
        switch (i.cc) {
        case ir::Cmp::Eq: set = "sete"; break;
        case ir::Cmp::Ne: set = "setne"; break;
        case ir::Cmp::Slt: set = "setl"; break;
        case ir::Cmp::Sle: set = "setle"; break;
        case ir::Cmp::Sgt: set = "setg"; break;
        case ir::Cmp::Sge: set = "setge"; break;
        case ir::Cmp::Ult: set = "setb"; break;
        case ir::Cmp::Ule: set = "setbe"; break;
        case ir::Cmp::Ugt: set = "seta"; break;
        case ir::Cmp::Uge: set = "setae"; break;
        default: ssaUnsupported(i); return;
        }
        a_->ins(set, reg("%al"));
        a_->ins("movzbq", reg("%al"), reg("%rax"));
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::FAdd: case ir::Op::FSub: case ir::Op::FMul: case ir::Op::FDiv: {
        if (isX87(rt)) {
            // genX87Binary's stack: the left operand over the right, the result popped into the slot.
            ssaLoad(f, f.operand(i, 1));
            ssaLoad(f, f.operand(i, 0));
            a_->ins(i.op == ir::Op::FAdd ? "faddp" : i.op == ir::Op::FSub ? "fsubp" : i.op == ir::Op::FMul ? "fmulp" : "fdivp",
                    reg("%st"), reg("%st(1)"));
            ssaStore(f, i.result);
            return;
        }
        const bool dbl = genKind(rt) == Kind::Double;
        const char *sfx = dbl ? "sd" : "ss";
        ssaLoad(f, f.operand(i, 0));
        a_->ins(dbl ? "movsd" : "movss", vslot(f.operand(i, 1)), reg("%xmm1"));
        const char *name = i.op == ir::Op::FAdd ? "add" : i.op == ir::Op::FSub ? "sub" : i.op == ir::Op::FMul ? "mul" : "div";
        a_->ins(std::string(name) + sfx, reg("%xmm1"), reg("%xmm0"));
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::FNeg:
        ssaLoad(f, f.operand(i, 0));
        if (isX87(rt)) {
            a_->ins("fchs");
        } else if (genKind(rt) == Kind::Double) {
            a_->ins("movabs", imm(0x8000000000000000ULL), reg("%rax"));
            a_->ins("movq", reg("%rax"), reg("%xmm1"));
            a_->ins("xorpd", reg("%xmm1"), reg("%xmm0"));
        } else {
            a_->ins("mov", imm(0x80000000U), reg("%eax"));
            a_->ins("movd", reg("%eax"), reg("%xmm1"));
            a_->ins("xorps", reg("%xmm1"), reg("%xmm0"));
        }
        ssaStore(f, i.result);
        return;
    case ir::Op::FCmp: {
        const Type *t = typeOf(f, f.operand(i, 0));
        const bool dbl = genKind(t) == Kind::Double;
        const std::string ucomi = dbl ? "ucomisd" : "ucomiss";
        // As genFloatBinary and genX87Binary order them: `<` and `<=` compare the other way round and test above.
        const bool swapped = i.cc == ir::Cmp::Olt || i.cc == ir::Cmp::Ole;
        if (isX87(t)) {
            ssaLoad(f, f.operand(i, 1));
            ssaLoad(f, f.operand(i, 0));
            if (swapped) a_->ins("fxch", reg("%st(1)"));
            a_->ins("fucomip", reg("%st(1)"), reg("%st"));
            a_->ins("fstp", reg("%st(0)"));
        } else {
            ssaLoad(f, f.operand(i, 0));
            a_->ins(dbl ? "movsd" : "movss", vslot(f.operand(i, 1)), reg("%xmm1"));
            if (swapped) a_->ins(ucomi, reg("%xmm0"), reg("%xmm1"));
            else         a_->ins(ucomi, reg("%xmm1"), reg("%xmm0"));
        }
        switch (i.cc) {
        case ir::Cmp::Oeq: a_->ins("sete", reg("%al")); a_->ins("setnp", reg("%cl")); a_->ins("and", reg("%cl"), reg("%al")); break;
        case ir::Cmp::Une: a_->ins("setne", reg("%al")); a_->ins("setp", reg("%cl")); a_->ins("or", reg("%cl"), reg("%al")); break;
        case ir::Cmp::Olt: case ir::Cmp::Ogt: a_->ins("seta", reg("%al")); break;
        case ir::Cmp::Ole: case ir::Cmp::Oge: a_->ins("setae", reg("%al")); break;
        default: ssaUnsupported(i); return;
        }
        a_->ins("movzbq", reg("%al"), reg("%rax"));
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::Trunc: case ir::Op::SExt: case ir::Op::ZExt: case ir::Op::FpToSi: case ir::Op::FpToUi: case ir::Op::SiToFp: case ir::Op::UiToFp:
    case ir::Op::FpExt: case ir::Op::FpTrunc: case ir::Op::Bitcast: case ir::Op::PtrToInt: case ir::Op::IntToPtr:
        ssaLoad(f, f.operand(i, 0));
        genConversion(typeOf(f, f.operand(i, 0)), rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::Br:
        ssaBranchArgs(f, i.succ[0]);
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
        return;
    case ir::Op::CondBr: {
        ssaLoad(f, f.operand(i, 0));
        a_->ins("cmp", immText("0"), reg("%rax"));
        // Under the passes a side with no arguments is the jump taken: the false side where the true one is written
        // next or carries arguments, else the true side; the other side's arguments and its jump follow in line.
        const bool args0 = i.succ[0].count != 0, args1 = i.succ[1].count != 0;
        if (passesRun() && !args1 && (args0 || i.succ[0].block.v == ssaNext_)) {
            a_->ins("je", lbl(ssaBlockLabel(i.succ[1].block)));
            ssaBranchArgs(f, i.succ[0]);
            a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
            return;
        }
        if (!args0 && (!args1 || passesRun())) {
            a_->ins("jne", lbl(ssaBlockLabel(i.succ[0].block)));
            ssaBranchArgs(f, i.succ[1]);
            a_->ins("jmp", lbl(ssaBlockLabel(i.succ[1].block)));
            return;
        }
        const int id = nextLabel();
        a_->ins("je", lbl(label("ssaelse", id)));
        ssaBranchArgs(f, i.succ[0]);
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
        a_->defLabel(label("ssaelse", id));
        ssaBranchArgs(f, i.succ[1]);
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[1].block)));
        return;
    }
    case ir::Op::Switch: {
        // A case edge with arguments goes through a stub of its own that writes them, after the ladder.
        ssaLoad(f, f.operand(i, 0));
        std::vector<std::string> stubs(i.succ.size());
        for (std::size_t s = 1; s < i.succ.size(); s++) {
            stubs[s] = i.succ[s].count == 0 ? ssaBlockLabel(i.succ[s].block) : label("ssacase", nextLabel());
            caseBranch(i.cases[s - 1], stubs[s]);
        }
        ssaBranchArgs(f, i.succ[0]);
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
        for (std::size_t s = 1; s < i.succ.size(); s++) {
            if (i.succ[s].count == 0) continue;
            a_->defLabel(stubs[s]);
            ssaBranchArgs(f, i.succ[s]);
            a_->ins("jmp", lbl(ssaBlockLabel(i.succ[s].block)));
        }
        return;
    }
    case ir::Op::Ret:
        if (i.count == 1 && ssaTypes_->type(f.returns)->isStructOrUnion()) {
            // A class's address: the walker's own return copies it out - through the result pointer or into registers.
            std::unique_ptr<Unary> d = make_owned<Unary>('*', ssaExpr(f, f.operand(i, 0)));
            d->setType(ssaTypes_->type(f.returns));
            Return r{ExprPtr(d.release())};
            r.accept(*this);
            return;
        }
        if (i.count == 1) ssaLoad(f, f.operand(i, 0));
        a_->ins("jmp", lbl(returnLabel_));
        return;
    case ir::Op::Call:
        selectCall(f, i, rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::Invoke: {
        const int id = nextLabel();
        const std::string begin = label("ssatry", id), end = label("ssatryend", id);
        // Microsoft: the call's own range in the ip2state map, in its pad's state; nothing in the body stays in one.
        if (usesFunclets()) {
            // Inside a handler's funclet the call stays in the handler's state, which chains to the enclosing pad: FH3
            // searches from a catch funclet only the tries whose handler states hold it, and terminates otherwise.
            // A pad opened inside the handler (the invoke's `bits`) is a state of its own there, its rows after the funclet's.
            const bool inHandler = ssaFuncletState_ >= 0;
            defineStateLabel(begin);
            if (!inHandler) msAddIpRow(begin, ssaPadState_[i.succ[1].block.v]);
            else if (i.bits != 0) msAddFuncletRow(begin, ssaPadState_[i.succ[1].block.v], false);
            selectCall(f, i, rt);
            defineStateLabel(end);
            if (!inHandler) msAddIpRow(end, -1);
            else if (i.bits != 0) msAddFuncletRow(end, ssaFuncletState_, false);
            // A try's handlers hand back one of its exits: the flow is told the call may continue at each.
            const ir::Inst &pf = f.inst(f.block(i.succ[1].block).insts[0]);
            if (pf.op == ir::Op::CatchPad && pf.k == -1)
                for (std::size_t c = 0; c < pf.cases.size(); c++) exceptionRegion(begin, end, ssaBlockLabel(ir::BlockId(static_cast<std::uint32_t>(pf.cases[c]))));
            ssaStore(f, i.result);
            ssaBranchArgs(f, i.succ[0]);
            a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
            return;
        }
        // One call-site row per invoke, naming the pad's landing label and the chain its pad carries (M3-IR 3.6): the rows
        // are registered in address order and are disjoint, which is the order the walker's table wants and never sorts.
        a_->defLabel(begin);
        selectCall(f, i, rt);
        a_->defLabel(end);
        const ir::Block &pb = f.block(i.succ[1].block);
        const ir::Inst &pad = f.inst(pb.insts[0]);
        std::vector<int> indices;
        for (std::size_t k = 0; k < pad.cases.size(); k++) indices.push_back(static_cast<int>(pad.cases[k]));
        const std::string land = ssaLandLabel(i.succ[1].block);
        callSite(begin, end, land, pad.syms, indices, (pad.bits & 1) != 0, ++ssaRegionAt_, (pad.bits & 2) != 0);
        exceptionRegion(begin, end, land);
        ssaStore(f, i.result);
        ssaBranchArgs(f, i.succ[0]);
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
        return;
    }
    case ir::Op::CatchRet: {
        // The funclet leaves with the exit's address in rax; the exit's parameters are slots the body reads and the
        // passes may not hold in a register, written here from another function's text.
        ssaBranchArgs(f, i.succ[0]);
        const ir::Block &to = f.block(i.succ[0].block);
        if (passesRun()) for (std::size_t p = 0; p < to.params.size(); p++) optimizer_->sharedSlot(-ssaValueSlot_[to.params[p].v], 8);
        funcletLeave(ssaBlockLabel(i.succ[0].block));
        return;
    }
    case ir::Op::CatchPad:
    case ir::Op::CatchSwitch:
    case ir::Op::CleanupPad:
    case ir::Op::CleanupRet:
        return;     // the row facts the invokes read; the landing stores are the block's own, and a funclet's return its end (selectBody)
    case ir::Op::VaStart: case ir::Op::VaArg:
        selectVa(f, i, rt);
        return;
    case ir::Op::VaEnd:
        return;
    default:
        ssaUnsupported(i);
    }
}

// The walker's own va_start and va_arg over a Var naming the list pointer's slot: SysV's four-field record over the save
// area receiveParameters filled, Microsoft's char pointer stepped by 8 through the home area - one code for both fronts.
void X86_64Linux::selectVa(const ir::Function &f, const ir::Inst &i, const Type *rt) {
    const ir::ValueId v = f.operand(i, 0);
    ExprPtr p = ssaExpr(f, v);
    if (i.op == ir::Op::VaStart) { VaStart n(std::move(p)); n.accept(*this); return; }
    VaArg n(std::move(p));
    n.setType(rt);
    n.accept(*this);
    ssaStore(f, i.result);
}

// The walker's own call, over Var nodes naming the slots: placeArguments and every ABI rule stay where they are.
void X86_64Linux::selectCall(const ir::Function &f, const ir::Inst &i, const Type *rt) {
    {
        const ir::CallInfo &c = f.calls[i.call];
        const Type *sig = ssaTypes_->type(c.signature);
        std::vector<ExprPtr> args;
        const std::uint32_t firstArg = c.symbol.empty() ? 1 : 0;
        for (std::uint32_t n = firstArg; n < i.count; n++) {
            const ir::ValueId v = f.operand(i, n);
            const std::size_t at = n - firstArg;
            const Type *want = at < sig->params().size() ? sig->params()[at] : nullptr;
            ExprPtr a = ssaExpr(f, v);
            // Where the signature takes a class by value the operand is its address: the walker copies `*p` in - or, for
            // one the parser passes by address, hands the caller's copy's address on as materialiseCopy's node does.
            if (want != nullptr && want->isStructOrUnion() && typeOf(f, v)->isPointer() && ssaByAddress(want)) { args.push_back(std::move(a)); continue; }
            if (want != nullptr && want->isStructOrUnion() && typeOf(f, v)->isPointer()) {
                std::unique_ptr<Unary> d = make_owned<Unary>('*', std::move(a));
                d->setType(want);
                args.push_back(std::move(d));
                continue;
            }
            args.push_back(std::move(a));
        }
        ExprPtr callee;
        if (c.symbol.empty()) callee = ssaExpr(f, f.operand(i, 0));
        // A class result comes back into its alloca's slot and the walker leaves its address in rax: the call's value.
        // One built in the caller's object (c.sret) goes through this function's own result pointer, as `return f();` does.
        const int resultSlot = c.sret ? ssaSretSlot_ : c.resultAlloca >= 0 ? static_cast<int>(ssaAllocaSlot_[static_cast<std::size_t>(c.resultAlloca)]) : 0;
        std::unique_ptr<Call> call = make_owned<Call>(c.symbol, std::move(callee), std::move(args), c.variadic, resultSlot,
                                                     c.namedArgs, ssaArgSlots_[i.call]);
        call->setSymbol(c.symbol);
        call->setHasThis(c.hasThis);
        call->setResultIndirect(c.sret);
        call->setType(c.resultAlloca >= 0 || c.sret ? sig->returns() : rt != nullptr ? rt : sig->returns());
        call->accept(*this);
    }
}

std::string X86_64Linux::ssaLandLabel(ir::BlockId b) const { return label("ssaland", static_cast<int>(b.v)); }

// **A handler as a funclet** (M3-IR 3.6, the Microsoft half): its pad block and every block tagged with it emitted whole
// inside openFunclet/endFunclet, an alloca it reads taken afresh where it is read (ssaRemat, the body may hold it in a
// register); its state beside the try's under the same parent; its handler-map row, the try block registered at the last.
void X86_64Linux::selectHandler(const ir::Function &f, ir::BlockId hb) {
    const ir::Block &blk = f.block(hb);
    const ir::Inst &cp = f.inst(blk.insts[0]);
    const ir::BlockId tryPad(static_cast<std::uint32_t>(cp.k));
    const ir::Inst &cs = f.inst(f.block(tryPad).term);
    const ir::Inst &dispatch = f.inst(f.block(tryPad).insts[0]);
    const int state = ssaPadState_[hb.v], parent = cs.k != 0 ? ssaPadState_[static_cast<std::size_t>(cs.k)] : -1;
    const std::string funclet = openFunclet(cp.pos, false);
    msAddFuncletRow(funclet, state, true);
    ssaFuncletState_ = state;
    std::vector<ir::BlockId> blocks(1, hb);
    for (std::size_t b = 0; b < f.blocks.size(); b++) if (f.block(f.blocks[b]).funclet == hb.v) blocks.push_back(f.blocks[b]);
    a_->defLabel(ssaBlockLabel(hb));
    for (std::size_t b = 0; b < blocks.size(); b++) {
        const ir::Block &in = f.block(blocks[b]);
        if (b != 0) a_->defLabel(ssaBlockLabel(blocks[b]));
        for (std::size_t k = 0; k <= in.insts.size(); k++) {
            const ir::InstId id = k < in.insts.size() ? in.insts[k] : in.term;
            if (id.valid()) selectInst(f, f.inst(id));
        }
    }
    ssaFuncletState_ = -1;
    endFunclet(ssaBlockLabel(ir::BlockId(static_cast<std::uint32_t>(dispatch.cases[0]))));
    leaveFunclet();
    msSetState(state, parent, std::string());
    MsHandlerRow row;
    row.descriptor = cp.syms.empty() ? std::string() : cp.syms[0];
    row.byReference = (cp.bits & 1) != 0;
    row.constPointer = (cp.bits & 2) != 0;
    row.funclet = funclet;
    if (cp.count == 1) {
        // The caught object's slot: the runtime writes it, so the passes may not hold it in a register.
        const std::size_t k = static_cast<std::size_t>(f.inst(f.value(f.operand(cp, 0)).def).k);
        row.objectSlot = ssaAllocaSlot_[k];
        if (passesRun()) optimizer_->sharedSlot(-ssaAllocaSlot_[k], ssaTypes_->type(f.frame[k].type)->size(target_));
    }
    ssaTries_[tryPad.v].handlers.push_back(row);
    // The try's block goes into the map once every try's handlers are out, an inner try (made inside a handler, so its
    // last block, `cs.bits`, comes first) before the one around it, as the walker pushes them (selectBody).
    if (hb.v == cs.succ.back().block.v) ssaTryOrder_.push_back(std::make_pair(cs.bits, tryPad.v));
}

// The body: the parameters copied from the slots receiveParameters filled into their values' slots, then each block.
void X86_64Linux::selectBody(const Function &fn, const ir::Function &f) {
    const std::vector<Param> &ps = fn.params();
    for (std::size_t i = 0; i < ps.size() && i < f.params.size(); i++) {
        a_->ins("lea", local(ps[i].offset), reg("%rax"));
        load(ps[i].type);
        ssaStore(f, f.params[i]);
    }
    ssaSretSlot_ = fn.sretSlot();
    if (f.sret.valid() && fn.sretSlot() != 0) { a_->ins("mov", local(fn.sretSlot()), reg("%rax")); ssaStore(f, f.sret); }
    std::size_t lastPos = 0;
    ssaRegionAt_ = 0;
    ssaTries_.clear();
    ssaTryOrder_.clear();
    // Microsoft (M3-IR 3.6): every pad is a cleanup funclet and a state of the FH3 tree, numbered before the invokes
    // that name it; the scratch word is set to -2 before the first call, as the walker's entry sets it.
    ssaPadState_.assign(f.blockArena.size(), -1);
    if (usesFunclets()) {
        bool any = false;
        for (std::size_t b = 0; b < f.blocks.size(); b++)
            if (f.block(f.blocks[b]).pad) { ssaPadState_[f.blocks[b].v] = msAddState(); any = true; }
        if (any) { msSetUnwindHelpSlot(ssaUnwindHelpSlot_); storeUnwindHelp(ssaUnwindHelpSlot_); }
    }
    const std::vector<ir::BlockId> order = passesRun() ? blockOrder(f) : f.blocks;
    for (std::size_t b = 0; b < order.size(); b++) {
        const ir::Block &blk = f.block(order[b]);
        ssaNext_ = 0;
        if (blk.pad && usesFunclets() && f.inst(blk.insts[0]).op == ir::Op::CatchPad) {
            // A try's dispatch pad: its state and its try block, the handlers' states following the body's (M3-IR 3.6).
            const ir::Inst &first = f.inst(blk.insts[0]);
            if (first.k == -1) {
                const ir::Inst &cs = f.inst(blk.term);
                const int state = ssaPadState_[order[b].v];
                msSetState(state, cs.k != 0 ? ssaPadState_[static_cast<std::size_t>(cs.k)] : -1, std::string());
                MsTryBlock tb;
                tb.tryLow = state;
                tb.tryHigh = ssaPadState_[cs.succ[0].block.v] - 1;
                // The handlers' states and those of the pads made inside them, up to the last block the handlers made (`cs.bits`).
                tb.catchHigh = ssaPadState_[cs.succ.back().block.v];
                for (std::size_t p = cs.succ.back().block.v; p <= cs.bits && p < ssaPadState_.size(); p++)
                    tb.catchHigh = std::max(tb.catchHigh, ssaPadState_[p]);
                ssaTries_[order[b].v] = tb;
            } else {
                selectHandler(f, order[b]);
            }
            continue;
        }
        if (blk.funclet != 0) continue;    // a handler's block: emitted inside its funclet by selectHandler
        if (blk.pad && usesFunclets()) {
            // The funclet destroys and returns to the runtime; its cleanupret names the pad it chains to, or none.
            // Its blocks: the pad's, then the ones tagged with it (a counted destruction's), the cleanupret last of all.
            const std::string funclet = openFunclet(blk.insts.empty() ? 0 : f.inst(blk.insts[0]).pos, true);
            std::vector<ir::BlockId> blocks(1, order[b]);
            for (std::size_t o = 0; o < f.blocks.size(); o++) if (f.block(f.blocks[o]).funclet == order[b].v) blocks.push_back(f.blocks[o]);
            int toState = -1;
            for (std::size_t o = 0; o < blocks.size(); o++) {
                const ir::Block &in = f.block(blocks[o]);
                a_->defLabel(ssaBlockLabel(blocks[o]));
                for (std::size_t k = 0; k <= in.insts.size(); k++) {
                    const ir::InstId id = k < in.insts.size() ? in.insts[k] : in.term;
                    if (id.valid()) selectInst(f, f.inst(id));
                }
                if (in.term.valid() && f.inst(in.term).op == ir::Op::CleanupRet && !f.inst(in.term).succ.empty())
                    toState = ssaPadState_[f.inst(in.term).succ[0].block.v];
            }
            endCleanupFunclet();
            leaveFunclet();
            msSetState(ssaPadState_[order[b].v], toState, funclet);
            continue;
        }
        // A pad: the runtime lands here with the pointer in rax and the selector in edx, stored into the block's two
        // parameters; a branch from another pad arrives past the stores, at the block's own label, carrying both.
        if (blk.pad && blk.params.size() == 2) {
            a_->defLabel(ssaLandLabel(order[b]));
            a_->ins("mov", reg("%rax"), vslot(blk.params[0]));
            a_->ins("movslq", reg("%edx"), reg("%rdx"));     // the selector in its canonical 8-byte form
            a_->ins("mov", reg("%rdx"), vslot(blk.params[1]));
        }
        a_->defLabel(ssaBlockLabel(order[b]));
        if (b + 1 < order.size() && !f.block(order[b + 1]).pad && f.block(order[b + 1]).funclet == 0) ssaNext_ = order[b + 1].v;
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const ir::Inst &in = f.inst(id);
            if (in.pos != 0 && in.pos != lastPos) { markLine(in.pos); lastPos = in.pos; }
            selectInst(f, in);
        }
    }
    std::sort(ssaTryOrder_.begin(), ssaTryOrder_.end());
    for (std::size_t t = 0; t < ssaTryOrder_.size(); t++) msAddTryBlock(ssaTries_[ssaTryOrder_[t].second]);
    // A function with landing pads whose tries nothing can throw through registered no row: the prologue
    // named the LSDA already, so an empty row defines it, as the walker's empty try does.
    if (landingPads_ && callSites().empty() && !usesFunclets()) {
        const std::string none = label("ssatry", nextLabel());
        a_->defLabel(none);
        callSite(none, none, none, std::vector<std::string>(), std::vector<int>(), false, ++ssaRegionAt_, false);
    }
    // What walkBody writes after the body: the default return and the return label.
    if (isX87(fn.returns()))              a_->ins("fldz");
    else if (fn.returns()->isFloating())  a_->ins("pxor", reg("%xmm0"), reg("%xmm0"));
    else                                  a_->ins("mov", immText("0"), reg("%rax"));
    a_->defLabel(returnLabel_);
    returnThis(fn);
}
