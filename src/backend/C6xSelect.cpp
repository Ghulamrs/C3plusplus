// **Selection of the SSA IR into the C6000 walker's stream** (M6a, doc-cxx3/M6-PLAN.md). A value has a register (ssaAssign)
// or a word of the frame, held extended as the walker holds A4; a frame object's address, a constant and a symbol are made
// at each use. The text is the walker's - serial, NOPs in the delay slots - so C6xSched and C6xPipe read it as they read its.

#include "Tms6747.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

int roundUp(int n, int a) { return a > 0 ? (n + a - 1) / a * a : n; }

bool remade(const ir::Function &f, ir::ValueId v) {
    const ir::Value &val = f.value(v);
    if (!val.def.valid()) return false;
    const ir::Op op = f.inst(val.def).op;
    return op == ir::Op::Alloca || op == ir::Op::IConst || op == ir::Op::Null || op == ir::Op::Addr;
}

// A remade value worth a register over a loop: a constant or a symbol's address.
bool hoists(const ir::Function &f, ir::ValueId v) {
    const ir::Value &val = f.value(v);
    return val.def.valid() && (f.inst(val.def).op == ir::Op::IConst || f.inst(val.def).op == ir::Op::Addr);
}

// The staging registers of a branch's block arguments: nobody's, as copyBlockBatched says.
const char *const kStage[] = { "B16", "B17", "B18", "B19", "B20", "B21", "B22", "B23" };
const std::uint32_t kStages = 8;

// A value's home: the registers the walker's planner promotes locals to (saved, bits 0-7), then A16-A31, which no call keeps,
// then B24-B31 (M6b round 3), given where a function is optimised. kBside: the homes of the B file.
const char *const kHome[] = { "A10", "A11", "A12", "A13", "B10", "B11", "B12", "B13", "A16", "A17", "A18", "A19",
                              "A20", "A21", "A22", "A23", "A24", "A25", "A26", "A27", "A28", "A29", "A30", "A31",
                              "B24", "B25", "B26", "B27", "B28", "B29", "B30", "B31" };
const unsigned kKept = 0xffu, kScratch = 0xffffff00u, kScratchA = 0xffff00u, kBside = 0xff0000f0u;

bool callsOut(ir::Op op) {     // a call, or the EABI's divide helper
    return op == ir::Op::Call || op == ir::Op::SDiv || op == ir::Op::UDiv || op == ir::Op::SRem || op == ir::Op::URem;
}

// A constant of lo..hi, as movImm would write it.
bool smallConst(const ir::Function &f, ir::ValueId v, long long lo, long long hi, long long &k) {
    if (!f.value(v).def.valid()) return false;
    const ir::Inst &d = f.inst(f.value(v).def);
    if (d.op != ir::Op::IConst && d.op != ir::Op::Null) return false;
    k = d.op == ir::Op::Null ? 0 : static_cast<int>(d.k);
    return k >= lo && k <= hi;
}

}   // namespace

// The function `fn` from the module where its body is inside the subset; otherwise null, and the walker's, the reason named.
const ir::Function *Tms6747::ssaChoose(const Function &fn) {
    if (ssa_ == nullptr) return nullptr;
    const ir::FuncId id = ssa_->find(fn.symbol());
    const std::string why = !id.valid() ? std::string("outside the SSA lowering") : ssaOutside(fn, ssa_->functions[id.v]);
    if (why.empty()) { ssaSelected_++; return &ssa_->functions[id.v]; }
    ssaWalked_++;
    if (std::getenv("CXX1_SSA_WHY") != nullptr) std::fprintf(stderr, "c6x-walker %s: %s\n", fn.symbol().c_str(), why.c_str());
    return nullptr;
}

// Why a body is outside the first subset (M6a): words only, no pad, no class by value, direct calls in registers.
std::string Tms6747::ssaOutside(const Function &fn, const ir::Function &f) const {
    if (fn.isVariadic() || f.variadic) return "variadic";
    if (fn.hasLandingPads() || f.landingPads) return "landing pads";
    if (fn.isNoexcept() || !fn.exceptionSpec().empty()) return "exception specification";
    if (fn.sretSlot() != 0 || f.sret.valid() || fn.returns()->isStructOrUnion()) return "class result";
    if (fn.returns()->isFloating() || (!fn.returns()->isVoid() && accessSize(fn.returns()) > 4)) return "floating or 64-bit result";
    if (fn.params().size() != f.params.size()) return "parameters differ";
    for (const Param &p : fn.params()) if (p.type->isStructOrUnion() || accessSize(p.type) > 4) return "class or 64-bit parameter";
    for (std::size_t v = 1; v < f.values.size(); v++) {
        if (!f.values[v].type.valid()) continue;
        const Type *t = ssaTypes_->type(f.values[v].type);
        if (t->isVoid()) continue;
        if (t->isFloating()) return "floating value";
        if (t->isStructOrUnion() || t->isArray() || t->isFunction()) return "aggregate value";
        if (t->size(target_) > 4) return "64-bit value";
    }
    for (ir::BlockId b : f.blocks) {
        const ir::Block &blk = f.block(b);
        if (blk.pad || blk.funclet != 0) return "landing pads";
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const ir::Inst &i = f.inst(id);
            for (const ir::Succ &s : i.succ) if (s.count > kStages) return "a branch past eight arguments";
            switch (i.op) {
            case ir::Op::IConst: case ir::Op::Null: case ir::Op::Undef: case ir::Op::Addr: case ir::Op::Alloca:
            case ir::Op::Load: case ir::Op::Store: case ir::Op::Gep:
            case ir::Op::Add: case ir::Op::Sub: case ir::Op::Mul: case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem:
            case ir::Op::URem: case ir::Op::UMulH: case ir::Op::And: case ir::Op::Or: case ir::Op::Xor: case ir::Op::Shl:
            case ir::Op::LShr: case ir::Op::AShr: case ir::Op::Neg: case ir::Op::Not:
            case ir::Op::Trunc: case ir::Op::SExt: case ir::Op::ZExt: case ir::Op::Bitcast: case ir::Op::PtrToInt: case ir::Op::IntToPtr:
            case ir::Op::Br: case ir::Op::CondBr: case ir::Op::Switch: case ir::Op::Ret: case ir::Op::Unreachable:
                break;
            case ir::Op::ICmp:
                if (i.cc > ir::Cmp::Uge) return "a floating comparison";
                break;
            case ir::Op::Call: {
                const ir::CallInfo &c = f.calls[i.call];
                if (c.symbol.empty()) return "indirect call";
                if (c.variadic) return "variadic call";
                if (c.sret || c.resultAlloca >= 0) return "class result of a call";
                if (i.count > static_cast<std::uint32_t>(abi_.intCount)) return "stack arguments";
                for (const Type *p : ssaTypes_->type(c.signature)->params())    // TI passes a small class in registers
                    if (p->isStructOrUnion()) return "class argument";
                break;
            }
            default:
                return "op " + ir::opName(i.op);
            }
        }
    }
    return std::string();
}

// The parser's parameter slots stay where emitParams writes them; above them a word per value, then each alloca.
int Tms6747::layoutSsa(const Function &fn, const ir::Function &f) {
    int cur = 0;
    for (std::size_t i = 0; i < fn.params().size(); i++) if (ssaHome_[f.params[i].v] == 0) cur = std::max(cur, fn.params()[i].offset);
    ssaValueSlot_.assign(f.values.size(), 0);
    for (std::size_t v = 1; v < f.values.size(); v++) {
        if (!f.values[v].type.valid() || ssaTypes_->type(f.values[v].type)->isVoid()) continue;
        if (remade(f, ir::ValueId(static_cast<std::uint32_t>(v))) || ssaHome_[v] != 0 || ssaFused_[v]) continue;
        cur = roundUp(cur, 4) + 4;
        ssaValueSlot_[v] = cur;
    }
    ssaAllocaSlot_.assign(f.frame.size(), 0);
    for (std::size_t a = 0; a < f.frame.size(); a++) {
        const Type *t = ssaTypes_->type(f.frame[a].type);
        const int align = f.frame[a].align > 0 ? f.frame[a].align : t->align(target_);
        cur = roundUp(cur + t->size(target_), std::max(align, 1));
        ssaAllocaSlot_[a] = cur;
    }
    return roundUp(cur, 8);
}

std::string Tms6747::ssaMem(int off, int size) {
    const int disp = -(saveBytes_ + off);
    if (frameFits(disp, size)) return frameOperand(disp, size);
    localAddr(off, "A0");
    return "*A0";
}

// A load or store through a frame object's address, or a constant step from one, is the frame operand itself.
bool Tms6747::ssaFrameMem(const ir::Function &f, ir::ValueId p, int size, std::string &mem) {
    long long step = 0;
    if (!f.value(p).def.valid()) return false;
    const ir::Inst *d = &f.inst(f.value(p).def);
    if (d->op == ir::Op::Gep && d->count == 1 && f.value(f.operand(*d, 0)).def.valid()) {
        step = d->k;
        d = &f.inst(f.value(f.operand(*d, 0)).def);
    }
    if (d->op != ir::Op::Alloca) return false;
    const long long disp = -(saveBytes_ + ssaAllocaSlot_[static_cast<std::size_t>(d->k)]) + step;
    if (disp > 0 || !frameFits(static_cast<int>(disp), size)) return false;
    mem = frameOperand(static_cast<int>(disp), size);
    return true;
}

// v into reg: made afresh where it has no slot, else read from its word.
void Tms6747::ssaLoad(const ir::Function &f, ir::ValueId v, const char *reg, bool fresh) {
    const char *h = fresh ? nullptr : ssaHomeOf(v);
    if (h != nullptr) {
        if (std::string(h) != reg) ssaEmit(c6x::MInst("MV", h, reg));
        return;
    }
    if (!remade(f, v)) {
        const std::string m = ssaMem(ssaValueSlot_[v.v], 4);
        ssaEmit(c6x::MInst("LDW", m, reg));
        ssaNop(4);
        return;
    }
    const ir::Inst &d = f.inst(f.value(v).def);
    const bool bSide = reg[0] == 'B';
    const char *a = bSide ? "A3" : reg;
    switch (d.op) {
    case ir::Op::IConst: case ir::Op::Null: {
        const long long k = d.op == ir::Op::Null ? 0 : static_cast<int>(d.k);
        if (k >= -32768 && k <= 32767) ssaEmit(c6x::MInst("MVK", k, reg)); else movImm(reg, k);
        return;
    }
    case ir::Op::Addr:
        movSym(a, d.sym);
        if (d.k != 0) regAdd(a, static_cast<int>(d.k), a);
        break;
    default:
        localAddr(ssaAllocaSlot_[static_cast<std::size_t>(d.k)], a);
        break;
    }
    if (bSide) ssaEmit(c6x::MInst("MV", "A3", reg));
}

void Tms6747::ssaStore(const ir::Function &f, ir::ValueId v, const char *reg) {
    (void)f;
    if (const char *h = ssaHomeOf(v)) {
        if (std::string(h) != reg) ssaEmit(c6x::MInst("MV", reg, h));
        return;
    }
    if (ssaValueSlot_[v.v] == 0) return;
    const std::string m = ssaMem(ssaValueSlot_[v.v], 4);
    ssaEmit(c6x::MInst("STW", reg, m));
}

const char *Tms6747::ssaHomeOf(ir::ValueId v) const { return ssaHome_[v.v] != 0 ? kHome[ssaHome_[v.v] - 1] : nullptr; }

// v where an instruction can read it: its home on the A side, else `scratch`, loaded.
std::string Tms6747::ssaUse(const ir::Function &f, ir::ValueId v, const char *scratch) {
    const char *h = ssaHomeOf(v);
    if (h != nullptr && h[0] == 'A') return h;
    ssaLoad(f, v, scratch);
    return scratch;
}

// v at its home on either side where the homes were given by side (ssaSides_) - ssaPut decides whether the instruction may read it there.
std::string Tms6747::ssaRead(const ir::Function &f, ir::ValueId v, const char *scratch) {
    const char *h = ssaHomeOf(v);
    return h != nullptr && ssaSides_ ? std::string(h) : ssaUse(f, v, scratch);
}

// **An instruction over the homes as they stand** (M6b round 3): written as built where one source at most crosses
// (c6x::legal, the scheduler's reading); else its B sources through A4 and A6 and its result in A4. The destination used.
std::string Tms6747::ssaPut(c6x::MInst t) {
    if (!c6x::legal(t)) {
        const int last = t.operands - 1;
        for (int o = 0; o < last; o++) {
            if (!t.op[o].isReg() || c6x::sideOfReg(t.op[o].reg) == 0) continue;
            bool a4 = false;
            for (int q = 0; q < last; q++) a4 = a4 || (q != o && t.op[q].isReg() && t.op[q].reg == c6x::regNumber("A4"));
            const char *s = a4 ? "A6" : "A4";
            ssaEmit(c6x::MInst("MV", t.op[o], s));
            t.op[o] = c6x::MOperand(s);
        }
        if (t.op[last].isReg() && c6x::sideOfReg(t.op[last].reg) == 1) t.op[last] = c6x::MOperand("A4");
    }
    ssaEmit(t);
    return t.op[t.operands - 1].str();
}

// A branch's arguments into the target's parameters: one by one where no move writes what another reads, else every
// one read before any is written, through B16-B23.
void Tms6747::ssaMoves(const ir::Function &f, const ir::Succ &s) {
    const ir::Block &to = f.block(s.block);
    std::vector<std::pair<ir::ValueId, ir::ValueId> > moves;
    for (std::uint32_t k = 0; k < s.count; k++) {
        const ir::ValueId a = f.arg(s, k), p = to.params[k];
        if (a.v == p.v || (ssaHome_[p.v] == 0 && ssaValueSlot_[p.v] == 0)) continue;
        if (ssaHome_[p.v] != 0 && ssaHome_[p.v] == ssaHome_[a.v]) continue;
        moves.push_back(std::make_pair(a, p));
    }
    bool clash = false;
    for (std::size_t m = 0; m < moves.size(); m++)
        for (std::size_t n = 0; n < moves.size(); n++)
            if (m != n && (moves[m].second.v == moves[n].first.v || (ssaHome_[moves[m].second.v] != 0 && ssaHome_[moves[m].second.v] == ssaHome_[moves[n].first.v]))) clash = true;
    if (!clash) {
        for (std::size_t m = 0; m < moves.size(); m++) {
            if (const char *h = ssaHomeOf(moves[m].second)) { ssaLoad(f, moves[m].first, h); continue; }
            const std::string r = ssaUse(f, moves[m].first, "A4");
            ssaStore(f, moves[m].second, r.c_str());
        }
        return;
    }
    bool loads = false;
    for (std::size_t m = 0; m < moves.size(); m++) {
        if (remade(f, moves[m].first) || ssaHome_[moves[m].first.v] != 0) { ssaLoad(f, moves[m].first, kStage[m]); continue; }
        const std::string mem = ssaMem(ssaValueSlot_[moves[m].first.v], 4);
        ssaEmit(c6x::MInst("LDW", mem, kStage[m]));
        loads = true;
    }
    if (loads) ssaNop(4);
    for (std::size_t m = 0; m < moves.size(); m++) ssaStore(f, moves[m].second, kStage[m]);
}

// **Homes for the values** (M6b), as arm64's ssaAssign: a linear scan over live ranges, a range that crosses a call in a
// saved register only, the heavier range taking a lighter one's place; a compare read by its block's branch alone has none.
void Tms6747::ssaAssign(const Function &fn, const ir::Function &f) {
    const std::size_t nv = f.values.size(), nb = f.blockArena.size();
    ssaHome_.assign(nv, 0);
    ssaFused_.assign(nv, 0);
    const char *knob = std::getenv("CXX1_C6X_HOMES");      // 0: none; 1: the saved registers alone
    const int mode = knob != nullptr ? std::atoi(knob) : 2;
    std::vector<int> first(nb, 0), last(nb, 0), start(nv, -1), end(nv, -1), uses(nv, 0), home(nv, -1), calls;
    std::vector<long long> weight(nv, 0);
    std::vector<char> hot(nv, 0);       // a constant or an address a loop reads where no instruction can hold it
    bool wideCall = fn.params().size() > 6;
    // A block's depth: an edge to a block written no later is a loop's, and every block between the two is inside it.
    std::vector<std::size_t> index(nb, 0), reach(ssaOrder_.size(), 0);
    std::vector<int> depth(ssaOrder_.size(), 0);
    for (std::size_t b = 0; b < ssaOrder_.size(); b++) index[ssaOrder_[b].v] = b;
    for (std::size_t b = 0; b < ssaOrder_.size(); b++) {
        if (!f.block(ssaOrder_[b]).term.valid()) continue;
        const ir::Inst &t = f.inst(f.block(ssaOrder_[b]).term);
        for (std::size_t s = 0; s < t.succ.size(); s++)
            if (index[t.succ[s].block.v] <= b) reach[index[t.succ[s].block.v]] = std::max(reach[index[t.succ[s].block.v]], b + 1);
    }
    for (std::size_t h = 0; h < ssaOrder_.size(); h++)
        for (std::size_t b = h; b < reach[h]; b++) depth[b]++;
    std::vector<char> latch(ssaOrder_.size(), 0);
    for (std::size_t h = 0; h < ssaOrder_.size(); h++) if (reach[h] != 0) latch[reach[h] - 1] = 1;
    ssaInner_.assign(nb, 0);
    for (std::size_t b = 1; b < ssaOrder_.size(); b++) ssaInner_[ssaOrder_[b].v] = depth[b] > 0 && reach[b] == 0 && !latch[b - 1];
    const std::size_t words = (nv + 63) / 64;
    std::vector<unsigned long long> in(nb * words, 0), out(nb * words, 0);
    for (std::size_t i = 0; i < f.params.size(); i++) start[f.params[i].v] = 0;
    int at = 2;
    // Positions two apart: a block's parameters at its first, each instruction after; a use in another block is live in.
    for (std::size_t b = 0; b < ssaOrder_.size(); b++) {
        const std::uint32_t id = ssaOrder_[b].v;
        const ir::Block &blk = f.block(ssaOrder_[b]);
        first[id] = at;
        for (std::size_t p = 0; p < blk.params.size(); p++) { start[blk.params[p].v] = at; home[blk.params[p].v] = static_cast<int>(id); }
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId iid = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!iid.valid()) continue;
            const ir::Inst &i = f.inst(iid);
            at += 2;
            if (i.result.valid()) { start[i.result.v] = at; home[i.result.v] = static_cast<int>(id); }
            if (callsOut(i.op)) calls.push_back(at);
            if (i.op == ir::Op::Call && i.count > 6) wideCall = true;
        }
        last[id] = at;
        at += 2;
    }
    for (std::size_t b = 0; b < ssaOrder_.size(); b++) {
        const std::uint32_t id = ssaOrder_[b].v;
        const ir::Block &blk = f.block(ssaOrder_[b]);
        int pos = first[id];
        const long long heat = 1ll << (3 * std::min(depth[b], 6));
        const auto use = [&](ir::ValueId v, int late) {
            if (!v.valid()) return;
            end[v.v] = std::max(end[v.v], pos + late);
            uses[v.v]++;
            weight[v.v] += heat;
            if (home[v.v] != static_cast<int>(id)) in[id * words + v.v / 64] |= 1ull << (v.v % 64);
        };
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId iid = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!iid.valid()) continue;
            const ir::Inst &i = f.inst(iid);
            pos += 2;
            for (std::uint32_t n = 0; n < i.count; n++) {
                use(f.operand(i, n), 0);
                long long k = 0;
                const bool held = i.op != ir::Op::Mul && smallConst(f, f.operand(i, n), -16, 15, k);
                if (optimize_ != 0 && depth[b] > 0 && !callsOut(i.op) && !held && f.operand(i, n).valid()) hot[f.operand(i, n).v] = 1;
            }
            for (std::size_t s = 0; s < i.succ.size(); s++)
                for (std::uint32_t n = 0; n < i.succ[s].count; n++) use(f.arg(i.succ[s], n), 1);
            if (i.result.valid()) weight[i.result.v] += heat;
            const bool compare = i.op == ir::Op::ICmp && k + 1 == blk.insts.size() && blk.term.valid();
            if (compare && f.inst(blk.term).op == ir::Op::CondBr && f.operand(f.inst(blk.term), 0).v == i.result.v) ssaFused_[i.result.v] = 1;
        }
    }
    for (std::size_t v = 1; v < nv; v++) if (ssaFused_[v] && uses[v] != 1) ssaFused_[v] = 0;
    // Live in = used from another block, or live out and not defined here; to a fixed point, last block first.
    for (bool changed = true; changed; ) {
        changed = false;
        for (std::size_t b = ssaOrder_.size(); b-- > 0; ) {
            const std::uint32_t id = ssaOrder_[b].v;
            const ir::Block &blk = f.block(ssaOrder_[b]);
            if (!blk.term.valid()) continue;
            const ir::Inst &t = f.inst(blk.term);
            for (std::size_t s = 0; s < t.succ.size(); s++)
                for (std::size_t w = 0; w < words; w++) out[id * words + w] |= in[t.succ[s].block.v * words + w];
            for (std::size_t w = 0; w < words; w++) {
                const unsigned long long add = out[id * words + w] & ~in[id * words + w];
                for (std::size_t bit = 0; bit < 64 && (add >> bit) != 0; bit++) {
                    if (((add >> bit) & 1) == 0 || home[w * 64 + bit] == static_cast<int>(id)) continue;
                    in[id * words + w] |= 1ull << bit;
                    changed = true;
                }
            }
        }
    }
    for (std::size_t b = 0; b < ssaOrder_.size(); b++) {
        const std::uint32_t id = ssaOrder_[b].v;
        for (std::size_t v = 1; v < nv; v++) {
            const unsigned long long bit = 1ull << (v % 64);
            if (in[id * words + v / 64] & bit) { start[v] = start[v] < 0 ? first[id] : std::min(start[v], first[id]); end[v] = std::max(end[v], first[id]); }
            if (out[id * words + v / 64] & bit) end[v] = std::max(end[v], last[id] + 1);
        }
    }
    std::vector<std::size_t> order;
    for (std::size_t v = 1; v < nv; v++) {
        if (!f.values[v].type.valid() || ssaTypes_->type(f.values[v].type)->isVoid() || start[v] < 0 || ssaFused_[v]) continue;
        // A remade value a loop reads keeps a register where one is free, and no weight: any other value takes it back.
        hot[v] = hot[v] && hoists(f, ir::ValueId(static_cast<std::uint32_t>(v)));
        if (remade(f, ir::ValueId(static_cast<std::uint32_t>(v))) && !hot[v]) continue;
        if (hot[v]) weight[v] = 0;
        end[v] = std::max(end[v], start[v] + 1);
        order.push_back(v);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return start[a] < start[b]; });
    // An edge's argument and its parameter are partners: one takes the other's register where it is free, so no move.
    std::vector<std::vector<std::size_t> > partner(nv);
    for (std::size_t b = 0; b < ssaOrder_.size(); b++) {
        const ir::Block &blk = f.block(ssaOrder_[b]);
        if (!blk.term.valid()) continue;
        const ir::Inst &t = f.inst(blk.term);
        for (std::size_t s = 0; s < t.succ.size(); s++)
            for (std::uint32_t n = 0; n < t.succ[s].count && n < f.block(t.succ[s].block).params.size(); n++) {
                const ir::ValueId a = f.arg(t.succ[s], n), q = f.block(t.succ[s].block).params[n];
                if (!a.valid() || a.v == q.v) continue;
                partner[a.v].push_back(q.v);
                partner[q.v].push_back(a.v);
            }
    }
    // Bit r is kHome[r] free. A function with a seventh argument, its own or a call's, leaves A10, B10, A12 and B12 to it.
    const unsigned kept = mode <= 0 ? 0u : wideCall ? kKept & ~0x55u : kKept;
    const bool sides = optimize_ != 0 && std::getenv("CXX1_C6X_NOSIDES") == nullptr;
    const unsigned scratch = mode >= 2 ? (sides ? kScratch : kScratchA) : 0u;
    ssaSides_ = sides;
    std::vector<int> arrives(nv, -1);
    for (std::size_t i = 0; i < f.params.size(); i++) arrives[f.params[i].v] = static_cast<int>(i);
    // The file a value wants: a pointer parameter its arriving register's, a load or a step its base's, else most of its operands'.
    const auto wants = [&](std::size_t v) -> unsigned {
        const auto file = [&](ir::ValueId o) { return !o.valid() || ssaHome_[o.v] == 0 ? -1 : static_cast<int>((kBside >> (ssaHome_[o.v] - 1)) & 1); };
        const bool base = ssaTypes_->type(f.values[v].type)->isPointer() || ssaTypes_->type(f.values[v].type)->isReference();
        if (arrives[v] >= 0) return base && arrives[v] < abi_.intCount && abi_.intRegs[arrives[v]][0] == 'B' ? kBside : ~kBside;
        int a = 0, b = 0;
        const ir::Value &val = f.values[v];
        if (!val.def.valid()) {
            for (std::size_t q = 0; q < partner[v].size(); q++) (file(ir::ValueId(static_cast<std::uint32_t>(partner[v][q]))) == 1 ? b : a)++;
            return b > 0 && b >= a ? kBside : ~kBside;
        }
        const ir::Inst &d = f.inst(val.def);
        if ((d.op == ir::Op::Load || d.op == ir::Op::Gep) && d.count > 0) return file(f.operand(d, 0)) == 1 ? kBside : ~kBside;
        for (std::uint32_t n = 0; n < d.count; n++) { const int s = file(f.operand(d, n)); if (s == 1) b++; else if (s == 0) a++; }
        return b > a ? kBside : ~kBside;
    };
    unsigned freeNow = kept | scratch, taken = 0;
    std::vector<std::size_t> active;
    int turn = 0, turnBlock = -1;       // the rest rotate within a block, so an unrolled body's turns use different registers
    for (std::size_t n = 0; n < order.size(); n++) {
        const std::size_t v = order[n];
        for (std::size_t a = 0; a < active.size(); )
            if (end[active[a]] <= start[v]) {
                freeNow |= 1u << (ssaHome_[active[a]] - 1);
                active.erase(active.begin() + static_cast<std::ptrdiff_t>(a));
            } else a++;
        bool crosses = false;
        for (std::size_t c = 0; c < calls.size() && !crosses; c++) crosses = calls[c] > start[v] && calls[c] < end[v];
        unsigned pick = crosses ? 0 : freeNow & scratch;
        // A value no call crosses takes a remade one's register before it takes a saved one.
        for (std::size_t a = 0; a < active.size() && pick == 0 && !hot[v] && !crosses; a++) {
            const unsigned bit = 1u << (ssaHome_[active[a]] - 1);
            if (!hot[active[a]] || (scratch & bit) == 0) continue;
            pick = bit;
            ssaHome_[active[a]] = 0;
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(a));
        }
        if (pick == 0 && (!hot[v] || crosses)) pick = freeNow & kept;
        if (pick == 0 && hot[v]) continue;
        if (pick == 0) {
            std::size_t light = active.size();
            for (std::size_t a = 0; a < active.size(); a++) {
                const std::size_t o = active[a];
                if (crosses && ((kept >> (ssaHome_[o] - 1)) & 1) == 0) continue;
                if (light == active.size() || weight[o] < weight[active[light]]) light = a;
            }
            if (light == active.size() || weight[active[light]] >= weight[v]) continue;
            pick = 1u << (ssaHome_[active[light]] - 1);
            ssaHome_[active[light]] = 0;
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(light));
        }
        if (sides && (pick & wants(v)) != 0) pick &= wants(v);
        int r = -1;
        for (std::size_t q = 0; q < partner[v].size() && r < 0; q++)
            if (ssaHome_[partner[v][q]] != 0 && ((pick >> (ssaHome_[partner[v][q]] - 1)) & 1) != 0) r = ssaHome_[partner[v][q]] - 1;
        if (home[v] != turnBlock) { turn = 0; turnBlock = home[v]; }
        if (r < 0 && ((pick & ~scratch) != 0 || optimize_ == 0)) { r = 0; while (((pick >> r) & 1) == 0) r++; }       // nothing schedules -O0; a save costs
        if (r < 0) {
            r = turn < 32 && pick >> turn != 0 ? turn : 0;
            while (((pick >> r) & 1) == 0) r++;
            turn = r + 1;
        }
        ssaHome_[v] = r + 1;
        freeNow &= ~(1u << r);
        taken |= 1u << r;
        active.push_back(v);
    }
    for (int r = 0; r < 8; r++) if (taken & (1u << r)) promoted_.push_back(kHome[r]);
}

// The compare `c` into d, less its last XOR: true where d holds the opposite answer.
bool Tms6747::ssaCompare(const ir::Function &f, const ir::Inst &c, const std::string &d) {
    const std::string a = ssaUse(f, f.operand(c, 0), "A4");
    const bool u = c.cc >= ir::Cmp::Ult;
    long long k = 0;
    if ((c.cc == ir::Cmp::Eq || c.cc == ir::Cmp::Ne) && smallConst(f, f.operand(c, 1), -16, 15, k)) {
        ssaEmit(c6x::MInst("CMPEQ", k, a, d));
        return c.cc == ir::Cmp::Ne;
    }
    const std::string b = ssaUse(f, f.operand(c, 1), "A6");
    const bool gt = c.cc == ir::Cmp::Sgt || c.cc == ir::Cmp::Ugt || c.cc == ir::Cmp::Sle || c.cc == ir::Cmp::Ule;
    const char *op = c.cc == ir::Cmp::Eq || c.cc == ir::Cmp::Ne ? "CMPEQ" : gt ? (u ? "CMPGTU" : "CMPGT") : (u ? "CMPLTU" : "CMPLT");
    ssaEmit(c6x::MInst(op, a, b, d));
    return c.cc == ir::Cmp::Ne || c.cc == ir::Cmp::Sle || c.cc == ir::Cmp::Ule || c.cc == ir::Cmp::Sge || c.cc == ir::Cmp::Uge;
}

void Tms6747::selectBody(const Function &fn, const ir::Function &f) {
    resetLabels();
    clearCallSites();
    hasCall_ = false;
    usesSavedArgRegs_ = false;
    usesSavedPairRegs_ = false;
    callCount_ = 0;
    const std::string head = out_.str();        // the stream holds the body alone; what stood before it goes back first
    out_.str(std::string());
    ssaMir_.clear();
    ssaOrder_ = optimize_ != 0 ? ssaBlockOrder(f) : f.blocks;       // a loop's blocks together, its latch last
    ssaAssign(fn, f);
    frame_ = layoutSsa(fn, f);
    // Each parameter into its home by emitParams (regOf_), or from the slot emitParams wrote it to into its value's word.
    const std::vector<Param> &ps = fn.params();
    for (std::size_t i = 0; i < ps.size(); i++) {
        if (const char *h = ssaHomeOf(f.params[i])) { regOf_[ps[i].offset] = h; continue; }
        const std::string m = ssaMem(ps[i].offset, accessSize(ps[i].type));
        loadFrom(ps[i].type, m);
        ssaStore(f, f.params[i], "A4");
    }
    // -g: each variable at its alloca's slot (ssaPlace_); a value in a register has no record, as the walker's regOf_.
    ssaPlace_.assign(fn.locals().size(), -1);
    ssaInsts_ = 0;
    ssaCounting_ = lineSource() != nullptr;
    if (lineSource() != nullptr) {
        resetBlocks(fn.blocks());
        const std::vector<int> at = ssaLocalAllocas(fn, f);
        for (std::size_t n = 0; n < at.size(); n++) if (at[n] >= 0) ssaPlace_[n] = ssaAllocaSlot_[static_cast<std::size_t>(at[n])];
    }
    std::size_t lastPos = 0;
    for (std::size_t b = 0; b < ssaOrder_.size(); b++) {
        const ir::Block &blk = f.block(ssaOrder_[b]);
        defineLabel(ssaBlockLabel(ssaOrder_[b]));
        const ir::BlockId next = b + 1 < ssaOrder_.size() ? ssaOrder_[b + 1] : ir::BlockId();
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            // -g: the statements and lexical blocks that begin before the instruction (ir::Inst::lines), as arm64's selector.
            const ir::Inst &in = f.inst(id);
            for (std::size_t m = 0; m < in.lines.size(); m++) {
                const std::uint32_t block = ir::kBlockOpens | ir::kBlockCloses, at = in.lines[m];
                if (at & block) markBlock(fn.blockAt(at & ~block), (at & ir::kBlockCloses) != 0); else markLine(at);
            }
            if (in.pos != 0 && in.pos != lastPos) { markLine(in.pos); lastPos = in.pos; }
            selectInst(f, in, next);
            ssaInsts_++;
        }
    }
    for (std::size_t k = 0; k < ssaLater_.size(); k++) {      // none is left unless the last block cannot be left
        defineLabel(ssaLater_[k].first);
        ssaMoves(f, *ssaLater_[k].second);
        jump(ssaBlockLabel(ssaLater_[k].second->block));
    }
    ssaLater_.clear();
    closeOpenBlocks();
    ssaCounting_ = false;
    ssaEmit(c6x::MInst("ZERO", "A4"));     // falling off the end returns 0, as the walker's body does
    out_ << head;
    c6x::write(ssaMir_, out_);
    if (std::getenv("CXX1_C6X_MIR") != nullptr) {
        const c6x::Census c = c6x::census(ssaMir_);
        std::fprintf(stderr, "c6x-mir %s: %d instructions, %d labels, %d verbatim, %d opaque operands\n", fn.symbol().c_str(),
                     c.instructions, c.labels, c.verbatim, c.opaque);
    }
}

// What a walker's helper wrote since the last instruction, read onto the stream where it stood.
void Tms6747::ssaAbsorb() {
    if (out_.tellp() <= 0) return;
    c6x::read(out_.str(), ssaMir_);
    out_.str(std::string());
}

// `[A1] B l` or `[!A1] B l`, and the five cycles a branch takes.
void Tms6747::ssaBranchIf(bool zero, const std::string &l) {
    c6x::MInst b("B", l);
    b.pred = static_cast<std::uint8_t>(c6x::regNumber("A1"));
    b.predNot = zero;
    ssaEmit(b);
    ssaNop(5);
}

void Tms6747::selectInst(const ir::Function &f, const ir::Inst &i, ir::BlockId next) {
    const Type *rt = i.type.valid() ? ssaTypes_->type(i.type) : nullptr;
    // The result where it is computed: its home on the A side, A4 where it has none or is narrowed after.
    const char *home = i.result.valid() ? ssaHomeOf(i.result) : nullptr;
    const std::string wide = home != nullptr && home[0] == 'A' ? home : "A4";
    // On either side where the homes were given by side (`own`, read by `y`): ssaPut answers where the result went.
    const std::string own = home != nullptr && ssaSides_ ? home : wide;
    std::string d = rt != nullptr && !rt->isVoid() && rt->size(target_) < 4 ? "A4" : own;
    const auto finish = [&]() { if (d == "A4") { narrowInt(rt); ssaStore(f, i.result, "A4"); } };
    const auto x = [&](std::uint32_t n) { return ssaUse(f, f.operand(i, n), n == 0 ? "A4" : "A6"); };
    const auto y = [&](std::uint32_t n) { return ssaRead(f, f.operand(i, n), n == 0 ? "A4" : "A6"); };
    const auto bin = [&](const char *op) {
        const std::string a = y(0), b = y(1);
        d = ssaPut(c6x::MInst(op, a, b, d));
    };
    // An operation whose right operand of lo..hi is written in the instruction (`first`: before the register).
    const auto binK = [&](const char *op, long long lo, long long hi, bool first) {
        long long k = 0;
        if (!smallConst(f, f.operand(i, 1), lo, hi, k)) { bin(op); return; }
        const std::string a = y(0);
        d = ssaPut(first ? c6x::MInst(op, k, a, d) : c6x::MInst(op, a, k, d));
    };
    // An edge: its arguments, then the jump, or none where the target is written next.
    const auto edge = [&](const ir::Succ &s, bool fall) {
        ssaMoves(f, s);
        if (!fall || s.block.v != next.v) jump(ssaBlockLabel(s.block));
    };
    // A conditional edge that carries arguments goes through a stub written after the block.
    std::vector<std::pair<std::string, const ir::Succ *> > stubs;
    const auto target = [&](const ir::Succ &s) -> std::string {
        bool moves = false;
        for (std::uint32_t k = 0; k < s.count; k++) moves = moves || f.arg(s, k).v != f.block(s.block).params[k].v;
        if (!moves) return ssaBlockLabel(s.block);
        stubs.push_back(std::make_pair(label("ssaedge", nextLabel()), &s));
        return stubs.back().first;
    };
    const auto writeStubs = [&]() {
        for (std::size_t k = 0; k < stubs.size(); k++) { defineLabel(stubs[k].first); edge(*stubs[k].second, false); }
    };
    // The stubs of branches that fell into their next block, written where the text has just jumped away.
    const auto writeLater = [&]() {
        for (std::size_t k = 0; k < ssaLater_.size(); k++) { defineLabel(ssaLater_[k].first); edge(*ssaLater_[k].second, false); }
        ssaLater_.clear();
    };
    switch (i.op) {
    case ir::Op::IConst: case ir::Op::Addr:
        if (home != nullptr) ssaLoad(f, i.result, home, true);      // made once, where a loop reads it from a register
        return;
    case ir::Op::Null: case ir::Op::Alloca: case ir::Op::Undef: case ir::Op::Unreachable:
        return;
    case ir::Op::Load: {
        std::string mem;
        if (!ssaFrameMem(f, f.operand(i, 0), accessSize(rt), mem)) mem = "*" + y(0);
        const int sz = rt->size(target_);
        const bool sign = rt->isSigned(target_);
        ssaEmit(c6x::MInst(sz == 1 ? (sign ? "LDB" : "LDBU") : sz == 2 ? (sign ? "LDH" : "LDHU") : "LDW", mem, own));
        ssaNop(4);
        if (own == "A4") ssaStore(f, i.result, "A4");
        return;
    }
    case ir::Op::Store: {
        const ir::ValueId p = f.operand(i, 0), v = f.operand(i, 1);
        const int sz = accessSize(ssaType(f, v));
        const std::string from = ssaRead(f, v, "A4");
        std::string mem;
        if (!ssaFrameMem(f, p, sz, mem)) mem = "*" + ssaRead(f, p, "A6");
        ssaEmit(c6x::MInst(sz == 1 ? "STB" : sz == 2 ? "STH" : "STW", from, mem));
        return;
    }
    case ir::Op::Gep: {
        if (i.count == 2) {
            const std::string p = y(0), b = y(1);
            if (ssaPut(c6x::MInst("ADD", p, b, own)) == "A4") ssaStore(f, i.result, "A4");
            return;
        }
        const std::string a = x(0);
        if (i.k != 0) regAdd(a.c_str(), static_cast<int>(i.k), wide.c_str());
        else if (a != wide) ssaEmit(c6x::MInst("MV", a, wide));
        if (wide == "A4") ssaStore(f, i.result, "A4");
        return;
    }
    case ir::Op::Add: case ir::Op::Sub: {
        long long k = 0;
        if (smallConst(f, f.operand(i, 1), -31, 31, k)) {
            const bool add = (i.op == ir::Op::Add) == (k >= 0);
            const std::string a = y(0);
            d = ssaPut(c6x::MInst(add ? "ADD" : "SUB", a, k < 0 ? -k : k, d));
        } else bin(i.op == ir::Op::Add ? "ADD" : "SUB");
        finish();
        return;
    }
    case ir::Op::Mul: {
        // By a power of two on either side: a shift.
        long long k = 0;
        const int side = smallConst(f, f.operand(i, 1), 1, 1 << 30, k) && (k & (k - 1)) == 0 ? 0 :
                         smallConst(f, f.operand(i, 0), 1, 1 << 30, k) && (k & (k - 1)) == 0 ? 1 : -1;
        if (side < 0) { bin("MPY32"); ssaNop(3); finish(); return; }
        int sh = 0;
        while ((1ll << sh) < k) sh++;
        const std::string a = ssaUse(f, f.operand(i, static_cast<std::uint32_t>(side)), "A4");
        ssaEmit(c6x::MInst("SHL", a, sh, d));
        finish();
        return;
    }
    case ir::Op::UMulH: { const std::string a = x(0), b = x(1); ssaEmit(c6x::MInst("MPY32U", a, b, "A1:A0")); ssaNop(3); ssaEmit(c6x::MInst("MV", "A1", d)); finish(); return; }
    case ir::Op::And: binK("AND", -16, 15, true); finish(); return;
    case ir::Op::Or:  binK("OR", -16, 15, true); finish(); return;
    case ir::Op::Xor: binK("XOR", -16, 15, true); finish(); return;
    case ir::Op::Shl: binK("SHL", 0, 31, false); finish(); return;
    case ir::Op::LShr: binK("SHRU", 0, 31, false); finish(); return;
    case ir::Op::AShr: binK("SHR", 0, 31, false); finish(); return;
    case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem: case ir::Op::URem:
        // The EABI's helper, A4 by B4 into A4, as the walker calls it.
        ssaLoad(f, f.operand(i, 0), "A4");
        ssaLoad(f, f.operand(i, 1), "B4");
        openArea(8);
        call(i.op == ir::Op::SDiv ? "__c6xabi_divi" : i.op == ir::Op::UDiv ? "__c6xabi_divu" : i.op == ir::Op::SRem ? "__c6xabi_remi" : "__c6xabi_remu");
        closeArea();
        narrowInt(rt);
        ssaStore(f, i.result, "A4");
        return;
    case ir::Op::Neg: case ir::Op::Not: {
        const std::string a = x(0);
        ssaEmit(i.op == ir::Op::Neg ? c6x::MInst("NEG", a, d) : c6x::MInst("XOR", -1, a, d));
        finish();
        return;
    }
    case ir::Op::ICmp:
        if (ssaFused_[i.result.v]) return;      // written by the branch that reads it
        if (ssaCompare(f, i, wide)) ssaEmit(c6x::MInst("XOR", 1, wide, wide));
        if (wide == "A4") ssaStore(f, i.result, "A4");
        return;
    case ir::Op::Trunc: case ir::Op::Bitcast: case ir::Op::PtrToInt: case ir::Op::IntToPtr: {
        const std::string a = x(0);
        if (a != d) ssaEmit(c6x::MInst("MV", a, d));
        finish();
        return;
    }
    case ir::Op::SExt: case ir::Op::ZExt: {
        const std::string a = x(0);
        const int from = ssaType(f, f.operand(i, 0))->size(target_);
        const char *ext = i.op == ir::Op::SExt ? "EXT" : "EXTU";
        if (from == 1) ssaEmit(c6x::MInst(ext, a, 24, 24, d));
        else if (from == 2) ssaEmit(c6x::MInst(ext, a, 16, 16, d));
        else if (a != d) ssaEmit(c6x::MInst("MV", a, d));
        finish();
        return;
    }
    case ir::Op::Call: {
        const ir::CallInfo &c = f.calls[i.call];
        for (std::uint32_t k = 0; k < i.count; k++) ssaLoad(f, f.operand(i, k), abi_.intRegs[k]);
        if (i.count > 6) usesSavedArgRegs_ = true;     // A10, B10, A12, B12 are callee-saved
        openArea(8);
        call(c.symbol);
        closeArea();
        if (i.result.valid() && rt != nullptr && !rt->isVoid()) { narrowInt(rt); ssaStore(f, i.result, "A4"); }
        return;
    }
    case ir::Op::Ret:
        if (i.count == 1) ssaLoad(f, f.operand(i, 0), "A4");
        jump(returnLabel_);
        writeLater();
        return;
    case ir::Op::Br:
        edge(i.succ[0], true);
        if (i.succ[0].block.v != next.v) writeLater();
        return;
    case ir::Op::CondBr: {
        const ir::ValueId c = f.operand(i, 0);
        bool flip = false;
        if (ssaFused_[c.v]) flip = ssaCompare(f, f.inst(f.value(c).def), "A1"); else ssaLoad(f, c, "A1");
        ssaBranchIf(!flip, target(i.succ[1]));
        if (i.succ[0].block.v == next.v) { ssaLater_.insert(ssaLater_.end(), stubs.begin(), stubs.end()); stubs.clear(); }
        edge(i.succ[0], stubs.empty());
        writeStubs();
        return;
    }
    case ir::Op::Switch: {
        const std::string v = x(0);
        for (std::size_t k = 0; k < i.cases.size(); k++) {
            movImm("A0", i.cases[k]);
            ssaEmit(c6x::MInst("CMPEQ", "A0", v, "A1"));
            ssaBranchIf(false, target(i.succ[k + 1]));
        }
        edge(i.succ[0], stubs.empty());
        writeStubs();
        return;
    }
    default:
        unsupported(("the SSA op " + ir::opName(i.op)).c_str());
        return;
    }
}
