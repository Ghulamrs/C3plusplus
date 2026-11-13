// **Selection of the SSA IR into the arm64 walker's stream** (M5.1, M5.2; doc-cxx3/M5-PLAN.md). Every value has an 8-byte slot
// below x29 in canonical form; operands travel in x0/x1 and d0/d1, x9 addresses. A class is its address, copied in and out
// by the walker's own code; an invoke is one LSDA row. The prologue, the epilogue and placeArguments are the walker's.

#include "Arm64Darwin.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

long long roundUp(long long n, long long a) { return (n + a - 1) / a * a; }

// What a slot holds in round 1: an integer, a pointer or a float of 1, 2, 4 or 8 bytes.
bool a64Scalar(const Type *t, const Target &target) {
    if (t == nullptr || t->isStructOrUnion() || t->isArray()) return false;
    if (!(t->isInteger() || t->isPointer() || t->isFloating() || t->isNullPtr())) return false;
    const int s = t->size(target);
    return s == 1 || s == 2 || s == 4 || s == 8;
}

std::string a64Fp(const Type *t, int n) { return std::string(t->kind() == Kind::Float ? "s" : "d") + std::to_string(n); }

bool a64Selects(ir::Op op) {
    switch (op) {
    case ir::Op::IConst: case ir::Op::FConst: case ir::Op::Null: case ir::Op::Addr: case ir::Op::Undef:
    case ir::Op::Alloca: case ir::Op::Load: case ir::Op::Store: case ir::Op::Memcpy: case ir::Op::Memset: case ir::Op::Gep:
    case ir::Op::Add: case ir::Op::Sub: case ir::Op::Mul: case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem: case ir::Op::URem:
    case ir::Op::SMulH: case ir::Op::UMulH: case ir::Op::And: case ir::Op::Or: case ir::Op::Xor: case ir::Op::Shl: case ir::Op::LShr:
    case ir::Op::AShr: case ir::Op::Neg: case ir::Op::Not: case ir::Op::ICmp:
    case ir::Op::FAdd: case ir::Op::FSub: case ir::Op::FMul: case ir::Op::FDiv: case ir::Op::FNeg: case ir::Op::FCmp:
    case ir::Op::Trunc: case ir::Op::SExt: case ir::Op::ZExt: case ir::Op::FpToSi: case ir::Op::FpToUi: case ir::Op::SiToFp:
    case ir::Op::UiToFp: case ir::Op::FpExt: case ir::Op::FpTrunc: case ir::Op::Bitcast: case ir::Op::PtrToInt: case ir::Op::IntToPtr:
    case ir::Op::Br: case ir::Op::CondBr: case ir::Op::Switch: case ir::Op::Ret: case ir::Op::Unreachable: case ir::Op::Call:
    case ir::Op::Invoke: case ir::Op::CatchPad: case ir::Op::CleanupPad:
        return true;
    default:
        return false;
    }
}

}   // namespace

// **The fallback seam**: why `f` is not selected, empty where every part of it is inside the subset.
std::string Arm64Darwin::ssaOutside(const Function &fn, const ir::Function &f) const {
    if (lineSource() != nullptr) return "-g";
    if (f.variadic) return "a variadic function";
    if (fn.frameAlign() > 16 || f.frameAlign > 16) return "an over-aligned frame";
    if (f.params.size() != fn.params().size()) return "parameters the parser counts differently";
    for (std::size_t i = 0; i < fn.params().size(); i++)
        if (!a64Scalar(fn.params()[i].type, target_) && !fn.params()[i].type->isStructOrUnion()) return "a parameter that is no scalar and no class";
    const Type *ret = ssaTypes_->type(f.returns);
    // A reference comes back as the address it is: the operand of every `ret` is then a pointer (asked below).
    const bool address = ret != nullptr && (ret->isStructOrUnion() || ret->isReference());
    if (ret == nullptr || (!ret->isVoid() && !a64Scalar(ret, target_) && !address)) return "a result that is no scalar and no class";
    if (ret->isStructOrUnion() && planFor(ret).byRef && fn.sretSlot() == 0) return "a class result with no pointer slot";
    for (std::size_t i = 0; i < f.frame.size(); i++)
        if (f.frame[i].align > 16 || ssaTypes_->type(f.frame[i].type)->align(target_) > 16) return "an over-aligned local";
    for (std::size_t v = 1; v < f.values.size(); v++) {
        if (!f.values[v].type.valid()) continue;
        const Type *t = ssaTypes_->type(f.values[v].type);
        if (!t->isVoid() && !a64Scalar(t, target_)) return "a value that is no scalar";
    }
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        if (blk.funclet != 0 || (blk.pad && blk.params.size() != 2)) return "a pad that is no landing pad";
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const ir::Inst &i = f.inst(id);
            if (!a64Selects(i.op)) return "the instruction '" + ir::opName(i.op) + "'";
            if ((i.op == ir::Op::Memcpy || i.op == ir::Op::Memset) && (i.k < 0 || i.k > 4096)) return "a block past 4096 bytes";
            if (i.op == ir::Op::Ret && i.count == 1 && address && !typeOf(f, f.operand(i, 0))->isPointer()) return "a class or a reference returned as a value";
            if (i.op != ir::Op::Call && i.op != ir::Op::Invoke) continue;
            if (i.op == ir::Op::Invoke && !f.block(i.succ[1].block).pad) return "an invoke whose unwind edge is no pad";
            const ir::CallInfo &c = f.calls[i.call];
            const Type *sig = ssaTypes_->type(c.signature);
            if (sig == nullptr || !sig->isFunction()) return "a call with no signature";
            const bool object = c.sret || c.resultAlloca >= 0;
            if (object != sig->returns()->isStructOrUnion()) return "a call whose class result has no object";
            if (c.sret && fn.sretSlot() == 0) return "a call into a result pointer the function lacks";
            const std::uint32_t first = c.symbol.empty() ? 1 : 0;
            for (std::uint32_t n = first; n < i.count; n++) {
                const std::size_t at = n - first;
                const bool extra = c.namedArgs >= 0 && at >= static_cast<std::size_t>(c.namedArgs);
                if (ssaArgType(f, i, at)->isStructOrUnion() && (extra || !typeOf(f, f.operand(i, n))->isPointer())) return "a class through '...'";
                if (extra && typeOf(f, f.operand(i, n))->kind() == Kind::Float) return "a float through '...'";
            }
        }
    }
    return std::string();
}

// The function's SSA body where it is selected; the walker's reason on stderr under CXX1_SSA_WHY, the census's switch.
const ir::Function *Arm64Darwin::ssaChoose(const Function &fn) {
    if (ssa_ == nullptr) return nullptr;
    const ir::FuncId id = ssa_->find(fn.symbol());
    const std::string why = id.valid() ? ssaOutside(fn, ssa_->functions[id.v]) : std::string("no SSA body");
    if (why.empty()) { ssaSelected_++; return &ssa_->functions[id.v]; }
    ssaWalked_++;
    if (std::getenv("CXX1_SSA_WHY") != nullptr) std::fprintf(stderr, "a64 walker %s: %s\n", fn.symbol().c_str(), why.c_str());
    return nullptr;
}

// The frame above the parser's parameter slots: each alloca at its alignment, then one 8-byte slot per value.
int Arm64Darwin::layoutSsa(const Function &fn, const ir::Function &f) {
    long long cur = 0;
    for (const Param &p : fn.params()) cur = std::max(cur, static_cast<long long>(p.offset));
    cur = roundUp(std::max(cur, static_cast<long long>(fn.sretSlot())), 16);
    ssaAllocaSlot_.assign(f.frame.size(), 0);
    for (std::size_t i = 0; i < f.frame.size(); i++) {
        const Type *t = ssaTypes_->type(f.frame[i].type);
        const int size = t->size(target_), align = f.frame[i].align > 0 ? f.frame[i].align : t->align(target_);
        cur = roundUp(cur + size, align > 0 ? align : 1);
        ssaAllocaSlot_[i] = cur;
    }
    // One area for every call's copies of the classes it passes by a copy's address: a call's operands are values already.
    long long area = 0;
    for (std::size_t n = 1; n < f.insts.size(); n++) {
        const ir::Inst &i = f.insts[n];
        if (i.op != ir::Op::Call && i.op != ir::Op::Invoke) continue;
        long long here = 0;
        for (std::uint32_t k = f.calls[i.call].symbol.empty() ? 1 : 0; k < i.count; k++) {
            const Type *t = ssaArgType(f, i, k - (f.calls[i.call].symbol.empty() ? 1 : 0));
            if (t->isStructOrUnion() && planFor(t).byRef) here += roundUp(t->size(target_), 16);
        }
        area = std::max(area, here);
    }
    cur = roundUp(cur, 16) + area;
    ssaArgArea_ = cur;
    ssaValueSlot_.assign(f.values.size(), 0);
    for (std::size_t i = 1; i < f.values.size(); i++) {
        if (!f.values[i].type.valid() || ssaTypes_->type(f.values[i].type)->isVoid()) continue;
        cur += 8;
        ssaValueSlot_[i] = cur;
    }
    return static_cast<int>(roundUp(cur, 16));
}

// The address x29 - off into `dst`.
void Arm64Darwin::ssaFrame(const char *dst, long long off) {
    if (off < 4096) { out_ << "  sub " << dst << ", x29, #" << off << "\n"; return; }
    movImm("x9", off);
    out_ << "  sub " << dst << ", x29, x9\n";
}

// A slot read ("ld") or written ("st"): unscaled from x29 where the offset reaches, through x9 otherwise.
void Arm64Darwin::ssaSlot(const char *op, const std::string &reg, long long off) {
    if (off <= 255) { out_ << "  " << op << "ur " << reg << ", [x29, #-" << off << "]\n"; return; }
    ssaFrame("x9", off);
    out_ << "  " << op << "r " << reg << ", [x9]\n";
}

// Operand `v` into register `n` of its kind: xN, or dN/sN for a floating value.
void Arm64Darwin::ssaLoad(const ir::Function &f, ir::ValueId v, int n) {
    const Type *t = typeOf(f, v);
    ssaSlot("ld", t->isFloating() ? a64Fp(t, n) : "x" + std::to_string(n), ssaValueSlot_[v.v]);
}

// The result, from x0 or d0/s0, into its slot.
void Arm64Darwin::ssaStore(const ir::Function &f, ir::ValueId v) {
    if (!v.valid() || ssaValueSlot_[v.v] == 0) return;
    const Type *t = typeOf(f, v);
    ssaSlot("st", t->isFloating() ? a64Fp(t, 0) : std::string("x0"), ssaValueSlot_[v.v]);
}

// A branch's arguments into the target's parameter slots as a parallel move of whole slots: a copy whose parameter no
// copy left reads goes first; a cycle left over is staged on the stack, each argument read before any is written.
void Arm64Darwin::ssaBranchArgs(const ir::Function &f, const ir::Succ &s) {
    const ir::Block &to = f.block(s.block);
    std::vector<std::pair<ir::ValueId, ir::ValueId> > moves;
    for (std::uint32_t i = 0; i < s.count; i++)
        if (f.arg(s, i).v != to.params[i].v && ssaValueSlot_[to.params[i].v] != 0) moves.push_back(std::make_pair(f.arg(s, i), to.params[i]));
    for (std::size_t m = 0; m < moves.size(); ) {
        bool read = false;
        for (std::size_t o = 0; o < moves.size(); o++) read = read || (o != m && moves[o].first.v == moves[m].second.v);
        if (read) { m++; continue; }
        ssaSlot("ld", "x0", ssaValueSlot_[moves[m].first.v]);
        ssaSlot("st", "x0", ssaValueSlot_[moves[m].second.v]);
        moves.erase(moves.begin() + static_cast<std::ptrdiff_t>(m));
        m = 0;
    }
    for (std::size_t i = 0; i < moves.size(); i++) { ssaSlot("ld", "x0", ssaValueSlot_[moves[i].first.v]); push(); }
    for (std::size_t i = moves.size(); i-- > 0; ) { pop("x0"); ssaSlot("st", "x0", ssaValueSlot_[moves[i].second.v]); }
}

// The type argument `n` travels as: the signature's class where the operand is its address and the caller's to copy, as
// x86's selector asks; a class the parser passes by address is the pointer it already is.
const Type *Arm64Darwin::ssaArgType(const ir::Function &f, const ir::Inst &i, std::size_t n) const {
    const ir::CallInfo &c = f.calls[i.call];
    const Type *sig = ssaTypes_->type(c.signature);
    const Type *want = sig != nullptr && sig->isFunction() && n < sig->params().size() ? sig->params()[n] : nullptr;
    const Type *t = typeOf(f, f.operand(i, static_cast<std::uint32_t>(n) + (c.symbol.empty() ? 1 : 0)));
    return want != nullptr && want->isStructOrUnion() && !Abi::passedByAddress(*want, target_) ? want : t;
}

// A direct or indirect call under AAPCS64 as Darwin has it: placeArguments for the named arguments, the ones through
// '...' in 8-byte stack words above them; the callee through x17. A class result comes back into its object, whose
// address is the call's value: x8 names it for one returned indirectly, the walker's aggResultInto stores the rest.
void Arm64Darwin::selectCall(const ir::Function &f, const ir::Inst &i) {
    const ir::CallInfo &c = f.calls[i.call];
    const std::uint32_t first = c.symbol.empty() ? 1 : 0;
    const std::size_t total = i.count - first;
    const std::size_t named = c.namedArgs < 0 ? total : std::min(total, static_cast<std::size_t>(c.namedArgs));
    std::vector<const Type *> types;
    for (std::size_t n = 0; n < named; n++) types.push_back(ssaArgType(f, i, n));
    const Placement plan = placeArguments(types);
    const int variadicBase = static_cast<int>(roundUp(plan.stackBytes, 8));
    const int bytes = static_cast<int>(roundUp(variadicBase + static_cast<long long>(total - named) * 8, 16));
    if (bytes > 0) { movImm("x9", bytes); out_ << "  sub sp, sp, x9\n"; }
    // A class passed by a copy's address: the copy is made in the frame's area, at `copyAt` below x29.
    std::vector<long long> copyAt(named, 0);
    long long used = 0;
    for (std::size_t n = 0; n < named; n++) {
        if (!types[n]->isStructOrUnion() || !plan.args[n].plan.byRef) continue;
        copyAt[n] = ssaArgArea_ - used;
        used += roundUp(types[n]->size(target_), 16);
        ssaLoad(f, f.operand(i, first + static_cast<std::uint32_t>(n)), 0);
        ssaFrame("x1", copyAt[n]);
        copyBlock(types[n]->size(target_), "x0", "x1");
    }
    for (std::size_t n = 0; n < total; n++) {
        const ir::ValueId v = f.operand(i, first + static_cast<std::uint32_t>(n));
        const Type *t = n < named ? types[n] : typeOf(f, v);
        if (n < named && plan.args[n].inRegister) continue;
        const int at = n < named ? plan.args[n].stackOffset : variadicBase + static_cast<int>(n - named) * 8;
        if (t->isStructOrUnion() && copyAt[n] != 0) {
            ssaFrame("x0", copyAt[n]);
            out_ << "  str x0, [sp, #" << at << "]\n";
            continue;
        }
        ssaLoad(f, v, 0);
        if (t->isStructOrUnion()) {
            out_ << "  add x11, sp, #" << at << "\n";
            copyBlock(t->size(target_), "x0", "x11");
            continue;
        }
        const int size = n < named ? t->size(target_) : 8;
        if (t->isFloating()) out_ << "  str " << (n < named ? a64Fp(t, 0) : std::string("d0")) << ", [sp, #" << at << "]\n";
        else out_ << (size == 1 ? "  strb w0" : size == 2 ? "  strh w0" : size == 4 ? "  str w0" : "  str x0") << ", [sp, #" << at << "]\n";
    }
    for (std::size_t n = 0; n < named; n++) {
        if (!plan.args[n].inRegister) continue;
        const ir::ValueId v = f.operand(i, first + static_cast<std::uint32_t>(n));
        const Type *t = types[n];
        if (t->isStructOrUnion()) {
            if (copyAt[n] != 0) ssaFrame("x9", copyAt[n]); else ssaSlot("ld", "x9", ssaValueSlot_[v.v]);
            aggToRegisters(plan.args[n].plan, plan.args[n].firstReg);
            continue;
        }
        ssaSlot("ld", t->isFloating() ? a64Fp(t, plan.args[n].firstReg) : std::string(abi_.intRegs[plan.args[n].firstReg]), ssaValueSlot_[v.v]);
    }
    // The object a class result comes back into: this function's own result pointer's, or a frame object.
    const Type *sig = ssaTypes_->type(c.signature);
    const bool object = c.sret || c.resultAlloca >= 0;
    const auto objectIn = [&](const char *reg) {
        if (c.sret) { localIn(reg, sretSlot_); out_ << "  ldr " << reg << ", [" << reg << "]\n"; }
        else ssaFrame(reg, ssaAllocaSlot_[static_cast<std::size_t>(c.resultAlloca)]);
    };
    if (object && planFor(sig->returns()).byRef) objectIn("x8");
    if (c.symbol.empty()) {
        ssaSlot("ld", "x17", ssaValueSlot_[f.operand(i, 0).v]);
        out_ << "  blr x17\n";
    } else {
        out_ << "  bl _" << c.symbol << "\n";
    }
    if (bytes > 0) { movImm("x9", bytes); out_ << "  add sp, sp, x9\n"; }
    if (object) { objectIn("x9"); aggResultInto(sig->returns()); return; }
    const Type *rt = i.type.valid() ? ssaTypes_->type(i.type) : nullptr;
    if (rt != nullptr && !rt->isVoid() && !rt->isFloating()) narrowInt(rt);
}

// One instruction: the operands read from their slots, the result written to its own.
void Arm64Darwin::selectInst(const ir::Function &f, const ir::Inst &i) {
    const Type *rt = i.type.valid() ? ssaTypes_->type(i.type) : nullptr;
    switch (i.op) {
    case ir::Op::Undef:
        return;
    case ir::Op::Unreachable:
        out_ << "  brk #0x1\n";
        return;
    case ir::Op::Alloca:
        ssaFrame("x0", ssaAllocaSlot_[static_cast<std::size_t>(i.k)]);
        ssaStore(f, i.result);
        return;
    case ir::Op::IConst:
        movImm("x0", i.k);
        narrowInt(rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::Null:
        out_ << "  mov x0, #0\n";
        ssaStore(f, i.result);
        return;
    case ir::Op::FConst:
        movImm("x9", static_cast<long long>(i.bits));
        out_ << "  fmov " << a64Fp(rt, 0) << (rt->kind() == Kind::Float ? ", w9\n" : ", x9\n");
        ssaStore(f, i.result);
        return;
    case ir::Op::Addr: {
        // A symbol this unit defines, a label of its own (a string's `.L.str.N`: no identifier begins with '.'), or one through the GOT.
        const bool here = definedHere_.count(i.sym) != 0, own = !here && !i.sym.empty() && i.sym[0] == '.';
        const std::string name = (own ? "" : "_") + i.sym;
        if (here || own) out_ << "  adrp x0, " << name << "@PAGE\n  add x0, x0, " << name << "@PAGEOFF\n";
        else out_ << "  adrp x0, " << name << "@GOTPAGE\n  ldr x0, [x0, " << name << "@GOTPAGEOFF]\n";
        if (i.k != 0) { movImm("x9", i.k); out_ << "  add x0, x0, x9\n"; }
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::Load:
        ssaLoad(f, f.operand(i, 0), 0);
        load(rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::Store:
        ssaLoad(f, f.operand(i, 1), 0);
        ssaSlot("ld", "x1", ssaValueSlot_[f.operand(i, 0).v]);
        storeThrough(typeOf(f, f.operand(i, 1)), "x1");
        return;
    case ir::Op::Memcpy:
        ssaLoad(f, f.operand(i, 1), 0);
        ssaLoad(f, f.operand(i, 0), 1);
        copyBlock(static_cast<int>(i.k), "x0", "x1");
        return;
    case ir::Op::Memset: {
        ssaLoad(f, f.operand(i, 0), 0);
        long long off = 0;
        for (; off + 8 <= i.k; off += 8) out_ << "  str xzr, [x0, #" << off << "]\n";
        for (; off + 4 <= i.k; off += 4) out_ << "  str wzr, [x0, #" << off << "]\n";
        for (; off < i.k; off++) out_ << "  strb wzr, [x0, #" << off << "]\n";
        return;
    }
    case ir::Op::Gep:
        ssaLoad(f, f.operand(i, 0), 0);
        if (i.count == 2) { ssaLoad(f, f.operand(i, 1), 1); out_ << "  add x0, x0, x1\n"; }
        else if (i.k != 0) { movImm("x9", i.k); out_ << "  add x0, x0, x9\n"; }
        ssaStore(f, i.result);
        return;
    case ir::Op::Add: case ir::Op::Sub: case ir::Op::Mul: case ir::Op::And: case ir::Op::Or: case ir::Op::Xor:
    case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem: case ir::Op::URem: case ir::Op::Shl: case ir::Op::LShr: case ir::Op::AShr: {
        // At the type's width - 32 bits for anything narrower, whose operands are canonical - then extended again.
        const char *r = rt->size(target_) == 8 ? "x" : "w";
        ssaLoad(f, f.operand(i, 0), 0);
        ssaLoad(f, f.operand(i, 1), 1);
        if (i.op == ir::Op::SRem || i.op == ir::Op::URem) {
            out_ << (i.op == ir::Op::SRem ? "  sdiv " : "  udiv ") << r << "2, " << r << "0, " << r << "1\n";
            out_ << "  msub " << r << "0, " << r << "2, " << r << "1, " << r << "0\n";
        } else {
            const char *name = i.op == ir::Op::Add ? "add" : i.op == ir::Op::Sub ? "sub" : i.op == ir::Op::Mul ? "mul" :
                               i.op == ir::Op::And ? "and" : i.op == ir::Op::Or ? "orr" : i.op == ir::Op::Xor ? "eor" :
                               i.op == ir::Op::SDiv ? "sdiv" : i.op == ir::Op::UDiv ? "udiv" : i.op == ir::Op::Shl ? "lsl" :
                               i.op == ir::Op::LShr ? "lsr" : "asr";
            out_ << "  " << name << " " << r << "0, " << r << "0, " << r << "1\n";
        }
        narrowInt(rt);
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::SMulH: case ir::Op::UMulH: {
        const bool sign = i.op == ir::Op::SMulH;
        ssaLoad(f, f.operand(i, 0), 0);
        ssaLoad(f, f.operand(i, 1), 1);
        if (rt->size(target_) == 8) out_ << (sign ? "  smulh" : "  umulh") << " x0, x0, x1\n";
        else out_ << (sign ? "  smull x0, w0, w1\n  asr x0, x0, #32\n" : "  umull x0, w0, w1\n  lsr x0, x0, #32\n");
        narrowInt(rt);
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::Neg: case ir::Op::Not: {
        const char *r = rt->size(target_) == 8 ? "x" : "w";
        ssaLoad(f, f.operand(i, 0), 0);
        out_ << (i.op == ir::Op::Neg ? "  neg " : "  mvn ") << r << "0, " << r << "0\n";
        narrowInt(rt);
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::ICmp: {
        // Below 32 bits an ordered compare extends both sides its own way first, whatever the operands' type says.
        static const char *const cond[] = {"eq", "ne", "lt", "le", "gt", "ge", "lo", "ls", "hi", "hs"};
        if (static_cast<int>(i.cc) > static_cast<int>(ir::Cmp::Uge)) { unsupported("this integer compare from SSA"); return; }
        const int size = typeOf(f, f.operand(i, 0))->size(target_);
        const bool sign = i.cc >= ir::Cmp::Slt && i.cc <= ir::Cmp::Sge, ordered = i.cc >= ir::Cmp::Slt;
        ssaLoad(f, f.operand(i, 0), 0);
        ssaLoad(f, f.operand(i, 1), 1);
        if (size < 4 && ordered) {
            const std::string ext = std::string(sign ? "  sxt" : "  uxt") + (size == 1 ? "b" : "h");
            out_ << ext << " w0, w0\n" << ext << " w1, w1\n";
        }
        out_ << (size == 8 ? "  cmp x0, x1\n" : "  cmp w0, w1\n") << "  cset x0, " << cond[static_cast<int>(i.cc)] << "\n";
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::FAdd: case ir::Op::FSub: case ir::Op::FMul: case ir::Op::FDiv: {
        ssaLoad(f, f.operand(i, 0), 0);
        ssaLoad(f, f.operand(i, 1), 1);
        const char *name = i.op == ir::Op::FAdd ? "fadd" : i.op == ir::Op::FSub ? "fsub" : i.op == ir::Op::FMul ? "fmul" : "fdiv";
        out_ << "  " << name << " " << a64Fp(rt, 0) << ", " << a64Fp(rt, 0) << ", " << a64Fp(rt, 1) << "\n";
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::FNeg:
        ssaLoad(f, f.operand(i, 0), 0);
        out_ << "  fneg " << a64Fp(rt, 0) << ", " << a64Fp(rt, 0) << "\n";
        ssaStore(f, i.result);
        return;
    case ir::Op::FCmp: {
        // The ordered conditions as the flags give them: `mi` and `ls` are false on an unordered pair, `ne` true.
        const Type *t = typeOf(f, f.operand(i, 0));
        const char *cond = i.cc == ir::Cmp::Oeq ? "eq" : i.cc == ir::Cmp::Une ? "ne" : i.cc == ir::Cmp::Olt ? "mi" :
                           i.cc == ir::Cmp::Ole ? "ls" : i.cc == ir::Cmp::Ogt ? "gt" : i.cc == ir::Cmp::Oge ? "ge" : nullptr;
        if (cond == nullptr) { unsupported("this floating compare from SSA"); return; }
        ssaLoad(f, f.operand(i, 0), 0);
        ssaLoad(f, f.operand(i, 1), 1);
        out_ << "  fcmp " << a64Fp(t, 0) << ", " << a64Fp(t, 1) << "\n  cset x0, " << cond << "\n";
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::Trunc: case ir::Op::SExt: case ir::Op::ZExt: case ir::Op::FpToSi: case ir::Op::FpToUi: case ir::Op::SiToFp: case ir::Op::UiToFp:
    case ir::Op::FpExt: case ir::Op::FpTrunc: case ir::Op::Bitcast: case ir::Op::PtrToInt: case ir::Op::IntToPtr:
        // The walker's conversion between the two types, as x86's selector takes it.
        ssaLoad(f, f.operand(i, 0), 0);
        genConversion(typeOf(f, f.operand(i, 0)), rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::Br:
        ssaBranchArgs(f, i.succ[0]);
        out_ << "  b " << ssaBlockLabel(i.succ[0].block) << "\n";
        return;
    case ir::Op::CondBr: {
        ssaLoad(f, f.operand(i, 0), 0);
        if (i.succ[0].count == 0) {
            out_ << "  cbnz x0, " << ssaBlockLabel(i.succ[0].block) << "\n";
        } else {
            const std::string other = label("ssaelse", nextLabel());
            out_ << "  cbz x0, " << other << "\n";
            ssaBranchArgs(f, i.succ[0]);
            out_ << "  b " << ssaBlockLabel(i.succ[0].block) << "\n" << other << ":\n";
        }
        ssaBranchArgs(f, i.succ[1]);
        out_ << "  b " << ssaBlockLabel(i.succ[1].block) << "\n";
        return;
    }
    case ir::Op::Switch: {
        // A case edge with arguments goes through a stub of its own that writes them, after the ladder.
        ssaLoad(f, f.operand(i, 0), 0);
        std::vector<std::string> stubs(i.succ.size());
        for (std::size_t s = 1; s < i.succ.size(); s++) {
            stubs[s] = i.succ[s].count == 0 ? ssaBlockLabel(i.succ[s].block) : label("ssacase", nextLabel());
            caseBranch(i.cases[s - 1], stubs[s]);
        }
        ssaBranchArgs(f, i.succ[0]);
        out_ << "  b " << ssaBlockLabel(i.succ[0].block) << "\n";
        for (std::size_t s = 1; s < i.succ.size(); s++) {
            if (i.succ[s].count == 0) continue;
            out_ << stubs[s] << ":\n";
            ssaBranchArgs(f, i.succ[s]);
            out_ << "  b " << ssaBlockLabel(i.succ[s].block) << "\n";
        }
        return;
    }
    case ir::Op::Ret:
        if (i.count == 1) ssaLoad(f, f.operand(i, 0), 0);
        if (i.count == 1 && ssaTypes_->type(f.returns)->isStructOrUnion()) returnAgg(ssaTypes_->type(f.returns));
        out_ << "  b " << returnLabel_ << "\n";
        return;
    case ir::Op::Call:
        selectCall(f, i);
        ssaStore(f, i.result);
        return;
    case ir::Op::Invoke: {
        // One call-site row per invoke, in address order and disjoint, with its pad's chain (M3-IR 3.13), as x86 writes it.
        const int id = nextLabel();
        const std::string begin = label("ssatry", id), end = label("ssatryend", id);
        out_ << begin << ":\n";
        selectCall(f, i);
        out_ << end << ":\n";
        const ir::Inst &pad = f.inst(f.block(i.succ[1].block).insts[0]);
        std::vector<int> indices;
        for (std::size_t k = 0; k < pad.cases.size(); k++) indices.push_back(static_cast<int>(pad.cases[k]));
        callSite(begin, end, ssaLandLabel(i.succ[1].block), pad.syms, indices, (pad.bits & 1) != 0, ++ssaRegionAt_, (pad.bits & 2) != 0);
        ssaStore(f, i.result);
        ssaBranchArgs(f, i.succ[0]);
        out_ << "  b " << ssaBlockLabel(i.succ[0].block) << "\n";
        return;
    }
    case ir::Op::CatchPad: case ir::Op::CleanupPad:
        return;     // the row facts the invokes read; the landing stores are the block's own (selectBody)
    default:
        unsupported("an SSA instruction the selector's census let through");
    }
}

// The body: the parameters from the slots the walker's prologue filled into their values, then the blocks in order.
void Arm64Darwin::selectBody(const Function &fn, const ir::Function &f) {
    const std::vector<Param> &ps = fn.params();
    for (std::size_t i = 0; i < ps.size() && i < f.params.size(); i++) {
        localIn("x0", ps[i].offset);
        load(ps[i].type);
        ssaStore(f, f.params[i]);
    }
    if (f.sret.valid() && sretSlot_ != 0) { localIn("x0", sretSlot_); out_ << "  ldr x0, [x0]\n"; ssaStore(f, f.sret); }
    ssaRegionAt_ = 0;
    if (!f.blocks.empty() && f.blocks[0].v != f.entry.v) out_ << "  b " << ssaBlockLabel(f.entry) << "\n";
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        // A pad: the runtime lands with the exception in x0 and the selector in w1, the block's two parameters; a
        // branch from another pad arrives past the stores, at the block's own label, carrying both.
        if (blk.pad) {
            out_ << ssaLandLabel(f.blocks[b]) << ":\n";
            ssaStore(f, blk.params[0]);
            out_ << "  sxtw x0, w1\n";
            ssaSlot("st", "x0", ssaValueSlot_[blk.params[1].v]);
        }
        out_ << ssaBlockLabel(f.blocks[b]) << ":\n";
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (id.valid()) selectInst(f, f.inst(id));
        }
    }
    // Pads no invoke names: the prologue named the LSDA already, so an empty row defines it, as the walker's empty try does.
    if (f.landingPads && callSites().empty()) {
        const std::string none = label("ssatry", nextLabel());
        out_ << none << ":\n";
        callSite(none, none, none, std::vector<std::string>(), std::vector<int>(), false, ++ssaRegionAt_, false);
    }
}
