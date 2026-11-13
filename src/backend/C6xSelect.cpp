// **Selection of the SSA IR into the C6000 walker's stream** (M6a, doc-cxx3/M6-PLAN.md). A value has a word of the frame,
// held extended as the walker holds A4; a frame object's address, a constant, null and a symbol have none, made at each
// use. The text is the walker's - serial, NOPs in the delay slots - so C6xSched and C6xPipe read it as they read its.

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

// The staging registers of a branch's block arguments: nobody's, as copyBlockBatched says.
const char *const kStage[] = { "B16", "B17", "B18", "B19", "B20", "B21", "B22", "B23" };
const std::uint32_t kStages = 8;

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
    if (lineSource() != nullptr) return "-g";
    if (fn.isVariadic() || f.variadic) return "variadic";
    if (fn.hasLandingPads() || f.landingPads) return "landing pads";
    if (fn.isNoexcept() || !fn.exceptionSpec().empty()) return "exception specification";
    if (fn.sretSlot() != 0 || f.sret.valid()) return "class result";
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
    for (const Param &p : fn.params()) cur = std::max(cur, p.offset);
    ssaValueSlot_.assign(f.values.size(), 0);
    for (std::size_t v = 1; v < f.values.size(); v++) {
        if (!f.values[v].type.valid() || ssaTypes_->type(f.values[v].type)->isVoid()) continue;
        if (remade(f, ir::ValueId(static_cast<std::uint32_t>(v)))) continue;
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
void Tms6747::ssaLoad(const ir::Function &f, ir::ValueId v, const char *reg) {
    if (!remade(f, v)) {
        const std::string m = ssaMem(ssaValueSlot_[v.v], 4);
        out_ << "\tLDW\t" << m << ", " << reg << "\n\tNOP\t4\n";
        return;
    }
    const ir::Inst &d = f.inst(f.value(v).def);
    const bool bSide = reg[0] == 'B';
    const char *a = bSide ? "A3" : reg;
    switch (d.op) {
    case ir::Op::IConst: movImm(reg, d.k); return;
    case ir::Op::Null: movImm(reg, 0); return;
    case ir::Op::Addr:
        movSym(a, d.sym);
        if (d.k != 0) regAdd(a, static_cast<int>(d.k), a);
        break;
    default:
        localAddr(ssaAllocaSlot_[static_cast<std::size_t>(d.k)], a);
        break;
    }
    if (bSide) out_ << "\tMV\tA3, " << reg << "\n";
}

void Tms6747::ssaStore(const ir::Function &f, ir::ValueId v, const char *reg) {
    (void)f;
    if (ssaValueSlot_[v.v] == 0) return;
    const std::string m = ssaMem(ssaValueSlot_[v.v], 4);
    out_ << "\tSTW\t" << reg << ", " << m << "\n";
}

// A branch's arguments into the target's parameters: every one read before any is written, through B16-B23.
void Tms6747::ssaMoves(const ir::Function &f, const ir::Succ &s) {
    const ir::Block &to = f.block(s.block);
    std::vector<std::pair<ir::ValueId, ir::ValueId> > moves;
    for (std::uint32_t k = 0; k < s.count; k++)
        if (f.arg(s, k).v != to.params[k].v && ssaValueSlot_[to.params[k].v] != 0) moves.push_back(std::make_pair(f.arg(s, k), to.params[k]));
    if (moves.empty()) return;
    for (std::size_t m = 0; m < moves.size(); m++) {
        if (remade(f, moves[m].first)) { ssaLoad(f, moves[m].first, kStage[m]); continue; }
        const std::string mem = ssaMem(ssaValueSlot_[moves[m].first.v], 4);
        out_ << "\tLDW\t" << mem << ", " << kStage[m] << "\n";
    }
    out_ << "\tNOP\t4\n";
    for (std::size_t m = 0; m < moves.size(); m++) ssaStore(f, moves[m].second, kStage[m]);
}

void Tms6747::selectBody(const Function &fn, const ir::Function &f) {
    resetLabels();
    clearCallSites();
    hasCall_ = false;
    usesSavedArgRegs_ = false;
    usesSavedPairRegs_ = false;
    callCount_ = 0;
    frame_ = layoutSsa(fn, f);
    // Each parameter from the slot emitParams wrote it to, into its value's word.
    const std::vector<Param> &ps = fn.params();
    for (std::size_t i = 0; i < ps.size(); i++) {
        const std::string m = ssaMem(ps[i].offset, accessSize(ps[i].type));
        loadFrom(ps[i].type, m);
        ssaStore(f, f.params[i], "A4");
    }
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        defineLabel(ssaBlockLabel(f.blocks[b]));
        const ir::BlockId next = b + 1 < f.blocks.size() ? f.blocks[b + 1] : ir::BlockId();
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (id.valid()) selectInst(f, f.inst(id), next);
        }
    }
    out_ << "\tZERO\tA4\n";     // falling off the end returns 0, as the walker's body does
}

void Tms6747::selectInst(const ir::Function &f, const ir::Inst &i, ir::BlockId next) {
    const Type *rt = i.type.valid() ? ssaTypes_->type(i.type) : nullptr;
    const auto finish = [&]() { narrowInt(rt); ssaStore(f, i.result, "A4"); };
    const auto two = [&]() { ssaLoad(f, f.operand(i, 0), "A4"); ssaLoad(f, f.operand(i, 1), "A6"); };
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
    switch (i.op) {
    case ir::Op::IConst: case ir::Op::Null: case ir::Op::Addr: case ir::Op::Alloca: case ir::Op::Undef: case ir::Op::Unreachable:
        return;
    case ir::Op::Load: {
        std::string mem;
        if (!ssaFrameMem(f, f.operand(i, 0), accessSize(rt), mem)) { ssaLoad(f, f.operand(i, 0), "A4"); mem = "*A4"; }
        loadFrom(rt, mem);
        ssaStore(f, i.result, "A4");
        return;
    }
    case ir::Op::Store: {
        const ir::ValueId p = f.operand(i, 0), v = f.operand(i, 1);
        const Type *vt = ssaType(f, v);
        ssaLoad(f, v, "A4");
        std::string mem;
        if (!ssaFrameMem(f, p, accessSize(vt), mem)) { ssaLoad(f, p, "A6"); mem = "*A6"; }
        store(vt, mem);
        return;
    }
    case ir::Op::Gep:
        ssaLoad(f, f.operand(i, 0), "A4");
        if (i.count == 2) { ssaLoad(f, f.operand(i, 1), "A6"); out_ << "\tADD\tA4, A6, A4\n"; }
        else if (i.k != 0) regAdd("A4", static_cast<int>(i.k), "A4");
        ssaStore(f, i.result, "A4");
        return;
    case ir::Op::Add: two(); out_ << "\tADD\tA4, A6, A4\n"; finish(); return;
    case ir::Op::Sub: two(); out_ << "\tSUB\tA4, A6, A4\n"; finish(); return;
    case ir::Op::Mul: two(); out_ << "\tMPY32\tA4, A6, A4\n\tNOP\t3\n"; finish(); return;
    case ir::Op::UMulH: two(); out_ << "\tMPY32U\tA4, A6, A1:A0\n\tNOP\t3\n\tMV\tA1, A4\n"; finish(); return;
    case ir::Op::And: two(); out_ << "\tAND\tA4, A6, A4\n"; finish(); return;
    case ir::Op::Or:  two(); out_ << "\tOR\tA4, A6, A4\n"; finish(); return;
    case ir::Op::Xor: two(); out_ << "\tXOR\tA4, A6, A4\n"; finish(); return;
    case ir::Op::Shl: two(); out_ << "\tSHL\tA4, A6, A4\n"; finish(); return;
    case ir::Op::LShr: two(); out_ << "\tSHRU\tA4, A6, A4\n"; finish(); return;
    case ir::Op::AShr: two(); out_ << "\tSHR\tA4, A6, A4\n"; finish(); return;
    case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem: case ir::Op::URem:
        // The EABI's helper, A4 by B4 into A4, as the walker calls it.
        ssaLoad(f, f.operand(i, 0), "A4");
        ssaLoad(f, f.operand(i, 1), "B4");
        openArea(8);
        call(i.op == ir::Op::SDiv ? "__c6xabi_divi" : i.op == ir::Op::UDiv ? "__c6xabi_divu" : i.op == ir::Op::SRem ? "__c6xabi_remi" : "__c6xabi_remu");
        closeArea();
        finish();
        return;
    case ir::Op::Neg: ssaLoad(f, f.operand(i, 0), "A4"); out_ << "\tNEG\tA4, A4\n"; finish(); return;
    case ir::Op::Not: ssaLoad(f, f.operand(i, 0), "A4"); out_ << "\tXOR\t-1, A4, A4\n"; finish(); return;
    case ir::Op::ICmp: {
        two();
        const bool u = i.cc >= ir::Cmp::Ult;
        switch (i.cc) {
        case ir::Cmp::Eq: out_ << "\tCMPEQ\tA4, A6, A4\n"; break;
        case ir::Cmp::Ne: out_ << "\tCMPEQ\tA4, A6, A4\n\tXOR\t1, A4, A4\n"; break;
        case ir::Cmp::Slt: case ir::Cmp::Ult: out_ << (u ? "\tCMPLTU" : "\tCMPLT") << "\tA4, A6, A4\n"; break;
        case ir::Cmp::Sgt: case ir::Cmp::Ugt: out_ << (u ? "\tCMPGTU" : "\tCMPGT") << "\tA4, A6, A4\n"; break;
        case ir::Cmp::Sle: case ir::Cmp::Ule: out_ << (u ? "\tCMPGTU" : "\tCMPGT") << "\tA4, A6, A4\n\tXOR\t1, A4, A4\n"; break;
        default: out_ << (u ? "\tCMPLTU" : "\tCMPLT") << "\tA4, A6, A4\n\tXOR\t1, A4, A4\n"; break;
        }
        ssaStore(f, i.result, "A4");
        return;
    }
    case ir::Op::Trunc: case ir::Op::Bitcast: case ir::Op::PtrToInt: case ir::Op::IntToPtr:
        ssaLoad(f, f.operand(i, 0), "A4");
        finish();
        return;
    case ir::Op::SExt: case ir::Op::ZExt: {
        ssaLoad(f, f.operand(i, 0), "A4");
        const int from = ssaType(f, f.operand(i, 0))->size(target_);
        const char *ext = i.op == ir::Op::SExt ? "\tEXT" : "\tEXTU";
        if (from == 1) out_ << ext << "\tA4, 24, 24, A4\n";
        else if (from == 2) out_ << ext << "\tA4, 16, 16, A4\n";
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
        if (i.result.valid() && rt != nullptr && !rt->isVoid()) finish();
        return;
    }
    case ir::Op::Ret:
        if (i.count == 1) ssaLoad(f, f.operand(i, 0), "A4");
        jump(returnLabel_);
        return;
    case ir::Op::Br:
        edge(i.succ[0], true);
        return;
    case ir::Op::CondBr: {
        ssaLoad(f, f.operand(i, 0), "A1");
        out_ << "\t[!A1]\tB\t" << target(i.succ[1]) << "\n\tNOP\t5\n";
        edge(i.succ[0], stubs.empty());
        writeStubs();
        return;
    }
    case ir::Op::Switch: {
        ssaLoad(f, f.operand(i, 0), "A4");
        for (std::size_t k = 0; k < i.cases.size(); k++) {
            movImm("A0", i.cases[k]);
            out_ << "\tCMPEQ\tA0, A4, A1\n\t[A1]\tB\t" << target(i.succ[k + 1]) << "\n\tNOP\t5\n";
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
