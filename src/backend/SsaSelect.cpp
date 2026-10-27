// **Selection of the SSA IR into the x86 walker's stream** (M3b.1, doc-cxx3/M3-IR.md 3.9). Every SSA value has a frame
// slot of 8 bytes holding the walker's canonical form (sign- or zero-extended to 64 bits, a float in its 4); an instruction
// loads its operands into rax/xmm0 and the scratch register, computes, stores. The ABI code is the walker's own.

#include "X86_64Linux.h"
#include "../ir/Ir.h"
#include "../Source.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {


long long roundUp(long long n, long long a) { return (n + a - 1) / a * a; }

}   // namespace

// The function `fn` is selected from SSA when the module holds its symbol.
const ir::Function *X86_64Linux::ssaFunction(const Function &fn) const {
    if (ssa_ == nullptr) return nullptr;
    const ir::FuncId id = ssa_->find(fn.symbol());
    return id.valid() ? &ssa_->functions[id.v] : nullptr;
}

// The frame above the parser's parameter slots - the one part of its layout the selector reads - each alloca at its
// alignment, then one 8-byte slot per value. Returns the new size.
int X86_64Linux::layoutSsa(const Function &fn, const ir::Function &f) {
    long long cur = 0;
    for (const Param &p : fn.params()) cur = std::max(cur, static_cast<long long>(p.offset));
    cur = std::max(cur, static_cast<long long>(fn.sretSlot()));   // where the prologue keeps the caller's result pointer
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
            if (want == nullptr || !want->isStructOrUnion()) continue;
            cur = roundUp(cur + want->size(target_), want->align(target_));
            slots.back() = static_cast<int>(cur);
        }
    }
    ssaValueSlot_.assign(f.values.size(), 0);
    for (std::size_t i = 1; i < f.values.size(); i++) {
        if (!f.values[i].type.valid() || ssaTypes_->type(f.values[i].type)->isVoid()) continue;
        cur += 8;
        ssaValueSlot_[i] = cur;
    }
    // The word the Microsoft runtime scribbles its state in, where the function has a pad (M3-IR 3.6).
    ssaUnwindHelpSlot_ = 0;
    if (usesFunclets() && fn.hasLandingPads()) { cur = roundUp(cur, 8) + 8; ssaUnwindHelpSlot_ = static_cast<int>(cur); }
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
        into.push_back(opt::Local{-ssaValueSlot_[i], t->isFloating() ? t->size(target_) : 8, t->isFloating()});
    }
}

Op X86_64Linux::vslot(ir::ValueId v) const { return local(ssaValueSlot_[v.v]); }
const Type *X86_64Linux::typeOf(const ir::Function &f, ir::ValueId v) const { return ssaTypes_->type(f.value(v).type); }

// A value into the accumulator: rax, or xmm0 for a float or double.
void X86_64Linux::ssaLoad(const ir::Function &f, ir::ValueId v) {
    const Type *t = typeOf(f, v);
    if (t->isFloating()) a_->ins(genKind(t) == Kind::Float ? "movss" : "movsd", vslot(v), reg("%xmm0"));
    else a_->ins("mov", vslot(v), reg("%rax"));
}

// The accumulator into a value's slot.
void X86_64Linux::ssaStore(const ir::Function &f, ir::ValueId v) {
    if (!v.valid() || ssaValueSlot_[v.v] == 0) return;
    const Type *t = typeOf(f, v);
    if (t->isFloating()) a_->ins(genKind(t) == Kind::Float ? "movss" : "movsd", reg("%xmm0"), vslot(v));
    else a_->ins("mov", reg("%rax"), vslot(v));
}

std::string X86_64Linux::ssaBlockLabel(ir::BlockId b) const { return label("ssa", static_cast<int>(b.v)); }

// A branch's arguments into the target's parameter slots, staged on the stack so that an argument that is itself a
// parameter of the target is read before any is written.
void X86_64Linux::ssaBranchArgs(const ir::Function &f, const ir::Succ &s) {
    if (s.count == 0) return;
    const ir::Block &to = f.block(s.block);
    for (std::uint32_t i = 0; i < s.count; i++) {
        const ir::ValueId v = f.arg(s, i);
        if (typeOf(f, v)->isFloating()) { ssaLoad(f, v); a_->ins("movq", reg("%xmm0"), reg("%rax")); }
        else ssaLoad(f, v);
        pushArg();
    }
    for (std::uint32_t i = s.count; i-- > 0; ) {
        a_->ins("pop", reg("%rax"));
        depth_--;
        const ir::ValueId p = to.params[i];
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
    switch (i.op) {
    case ir::Op::Alloca:
        // The frame object's address is the alloca's value: taken once, here, into its slot.
        a_->ins("lea", local(ssaAllocaSlot_[static_cast<std::size_t>(i.k)]), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case ir::Op::Unreachable:
    case ir::Op::Undef:
        return;
    case ir::Op::IConst:
        a_->ins("mov", imm(i.k), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case ir::Op::Null:
        a_->ins("mov", immText("0"), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case ir::Op::FConst:
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
    case ir::Op::Load:
        ssaLoad(f, f.operand(i, 0));
        load(rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::Store: {
        const ir::ValueId p = f.operand(i, 0), v = f.operand(i, 1);
        ssaLoad(f, v);
        a_->ins("mov", vslot(p), reg(abi_.scratch));
        store(typeOf(f, v));
        return;
    }
    case ir::Op::Memcpy:
        // The walker's block copy: from (%rax) to (scratch).
        ssaLoad(f, f.operand(i, 1));
        a_->ins("mov", vslot(f.operand(i, 0)), reg(abi_.scratch));
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
        ssaLoad(f, f.operand(i, 0));
        if (i.count == 2) a_->ins("add", vslot(f.operand(i, 1)), reg("%rax"));
        else if (i.k != 0) a_->ins("add", imm(i.k), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case ir::Op::Add: case ir::Op::Sub: case ir::Op::Mul: case ir::Op::And: case ir::Op::Or: case ir::Op::Xor:
    case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem: case ir::Op::URem: case ir::Op::Shl: case ir::Op::LShr: case ir::Op::AShr: {
        const bool wide = rt->size(target_) == 8;
        const char *acc_ = acc(rt), *d = rhs(rt);
        if (i.op == ir::Op::Shl || i.op == ir::Op::LShr || i.op == ir::Op::AShr) {
            a_->ins("mov", vslot(f.operand(i, 1)), reg("%rcx"));
            ssaLoad(f, f.operand(i, 0));
            a_->ins(i.op == ir::Op::Shl ? "shl" : i.op == ir::Op::AShr ? "sar" : "shr", reg("%cl"), reg(acc_));
            canonicalise(rt);
            ssaStore(f, i.result);
            return;
        }
        a_->ins("mov", vslot(f.operand(i, 1)), reg(abi_.scratch));
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
        canonicalise(rt);
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::Neg: case ir::Op::Not:
        ssaLoad(f, f.operand(i, 0));
        a_->ins(i.op == ir::Op::Neg ? "neg" : "not", reg(acc(rt)));
        canonicalise(rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::ICmp: {
        const Type *t = typeOf(f, f.operand(i, 0));
        a_->ins("mov", vslot(f.operand(i, 1)), reg(abi_.scratch));
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
        if (genKind(rt) == Kind::Double) {
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
        ssaLoad(f, f.operand(i, 0));
        a_->ins(dbl ? "movsd" : "movss", vslot(f.operand(i, 1)), reg("%xmm1"));
        // As genFloatBinary orders them: `<` and `<=` compare the other way round and test above.
        const bool swapped = i.cc == ir::Cmp::Olt || i.cc == ir::Cmp::Ole;
        if (swapped) a_->ins(ucomi, reg("%xmm0"), reg("%xmm1"));
        else         a_->ins(ucomi, reg("%xmm1"), reg("%xmm0"));
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
        if (i.succ[0].count == 0 && i.succ[1].count == 0) {
            a_->ins("jne", lbl(ssaBlockLabel(i.succ[0].block)));
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
    case ir::Op::Switch:
        for (std::size_t s = 0; s < i.succ.size(); s++) if (i.succ[s].count != 0) { ssaUnsupported(i); return; }
        ssaLoad(f, f.operand(i, 0));
        for (std::size_t s = 1; s < i.succ.size(); s++) caseBranch(i.cases[s - 1], ssaBlockLabel(i.succ[s].block));
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
        return;
    case ir::Op::Ret:
        if (i.count == 1 && ssaTypes_->type(f.returns)->isStructOrUnion()) {
            // A class's address: the walker's own return copies it out - through the result pointer or into registers.
            std::unique_ptr<Var> p(Var::local("$v" + std::to_string(f.operand(i, 0).v),
                                              static_cast<int>(ssaValueSlot_[f.operand(i, 0).v])));
            p->setType(typeOf(f, f.operand(i, 0)));
            std::unique_ptr<Unary> d = make_owned<Unary>('*', ExprPtr(p.release()));
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
            const bool inHandler = ssaFuncletState_ >= 0;
            defineStateLabel(begin);
            if (!inHandler) msAddIpRow(begin, ssaPadState_[i.succ[1].block.v]);
            selectCall(f, i, rt);
            defineStateLabel(end);
            if (!inHandler) msAddIpRow(end, -1);
            // A try's handlers hand back one of its exits: the flow is told the call may continue at each.
            const ir::Inst &pf = f.inst(f.block(i.succ[1].block).insts[0]);
            if (pf.op == ir::Op::CatchPad && pf.k == -1)
                for (std::size_t c = 0; c < pf.cases.size(); c++) exceptionRegion(begin, end, ssaBlockLabel(ir::BlockId(static_cast<std::uint32_t>(pf.cases[c]))));
            ssaStore(f, i.result);
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
    default:
        ssaUnsupported(i);
    }
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
            const ir::Inst &def = f.inst(f.value(v).def);
            // In a funclet an alloca's address is taken afresh: the body's slot for it may have been promoted to a register.
            if (ssaInFunclet_ && def.op == ir::Op::Alloca && (want == nullptr || !want->isStructOrUnion())) {
                std::unique_ptr<Var> o(Var::local("$a" + std::to_string(def.k), static_cast<int>(ssaAllocaSlot_[static_cast<std::size_t>(def.k)])));
                o->setType(ssaTypes_->type(f.frame[static_cast<std::size_t>(def.k)].type));
                std::unique_ptr<Unary> addr = make_owned<Unary>('&', ExprPtr(o.release()));
                addr->setType(typeOf(f, v));
                args.push_back(std::move(addr));
                continue;
            }
            std::unique_ptr<Var> a(Var::local("$v" + std::to_string(v.v), static_cast<int>(ssaValueSlot_[v.v])));
            a->setType(typeOf(f, v));
            // Where the signature takes a class by value the operand is its address: the walker copies `*p` in.
            if (want != nullptr && want->isStructOrUnion() && typeOf(f, v)->isPointer()) {
                std::unique_ptr<Unary> d = make_owned<Unary>('*', ExprPtr(a.release()));
                d->setType(want);
                args.push_back(std::move(d));
                continue;
            }
            args.push_back(std::move(a));
        }
        ExprPtr callee;
        if (c.symbol.empty()) {
            const ir::ValueId v = f.operand(i, 0);
            std::unique_ptr<Var> p(Var::local("$v" + std::to_string(v.v), static_cast<int>(ssaValueSlot_[v.v])));
            p->setType(typeOf(f, v));
            callee = std::move(p);
        }
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
// inside openFunclet/endFunclet, each alloca it reads taken afresh by lea first (the body's slot for the address may be a
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
    ssaInFunclet_ = true;
    ssaFuncletState_ = state;
    std::vector<ir::BlockId> blocks(1, hb);
    for (std::size_t b = 0; b < f.blocks.size(); b++) if (f.block(f.blocks[b]).funclet == hb.v) blocks.push_back(f.blocks[b]);
    a_->defLabel(ssaBlockLabel(hb));
    std::vector<char> made(f.frame.size(), 0);
    for (std::size_t b = 0; b < blocks.size(); b++) {
        const ir::Block &in = f.block(blocks[b]);
        for (std::size_t k = 0; k <= in.insts.size(); k++) {
            const ir::InstId id = k < in.insts.size() ? in.insts[k] : in.term;
            if (!id.valid()) continue;
            const ir::Inst &i = f.inst(id);
            std::vector<ir::ValueId> used;
            for (std::uint32_t n = 0; n < i.count; n++) used.push_back(f.operand(i, n));
            for (std::size_t s = 0; s < i.succ.size(); s++)
                for (std::uint32_t a = 0; a < i.succ[s].count; a++) used.push_back(f.arg(i.succ[s], a));
            for (std::size_t u = 0; u < used.size(); u++) {
                const ir::Inst &def = f.inst(f.value(used[u]).def);
                if (!f.value(used[u]).def.valid() || def.op != ir::Op::Alloca || made[static_cast<std::size_t>(def.k)]) continue;
                made[static_cast<std::size_t>(def.k)] = 1;
                a_->ins("lea", local(ssaAllocaSlot_[static_cast<std::size_t>(def.k)]), reg("%rax"));
                ssaStore(f, def.result);
            }
        }
    }
    for (std::size_t b = 0; b < blocks.size(); b++) {
        const ir::Block &in = f.block(blocks[b]);
        if (b != 0) a_->defLabel(ssaBlockLabel(blocks[b]));
        for (std::size_t k = 0; k <= in.insts.size(); k++) {
            const ir::InstId id = k < in.insts.size() ? in.insts[k] : in.term;
            if (id.valid()) selectInst(f, f.inst(id));
        }
    }
    ssaInFunclet_ = false;
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
    MsTryBlock &tb = ssaTries_[tryPad.v];
    tb.handlers.push_back(row);
    if (hb.v == cs.succ.back().block.v) msAddTryBlock(tb);
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
    // Microsoft (M3-IR 3.6): every pad is a cleanup funclet and a state of the FH3 tree, numbered before the invokes
    // that name it; the scratch word is set to -2 before the first call, as the walker's entry sets it.
    ssaPadState_.assign(f.blockArena.size(), -1);
    if (usesFunclets()) {
        bool any = false;
        for (std::size_t b = 0; b < f.blocks.size(); b++)
            if (f.block(f.blocks[b]).pad) { ssaPadState_[f.blocks[b].v] = msAddState(); any = true; }
        if (any) { msSetUnwindHelpSlot(ssaUnwindHelpSlot_); storeUnwindHelp(ssaUnwindHelpSlot_); }
    }
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        if (blk.pad && usesFunclets() && f.inst(blk.insts[0]).op == ir::Op::CatchPad) {
            // A try's dispatch pad: its state and its try block, the handlers' states following the body's (M3-IR 3.6).
            const ir::Inst &first = f.inst(blk.insts[0]);
            if (first.k == -1) {
                const ir::Inst &cs = f.inst(blk.term);
                const int state = ssaPadState_[f.blocks[b].v];
                msSetState(state, cs.k != 0 ? ssaPadState_[static_cast<std::size_t>(cs.k)] : -1, std::string());
                MsTryBlock tb;
                tb.tryLow = state;
                tb.tryHigh = ssaPadState_[cs.succ[0].block.v] - 1;
                tb.catchHigh = ssaPadState_[cs.succ.back().block.v];
                ssaTries_[f.blocks[b].v] = tb;
            } else {
                selectHandler(f, f.blocks[b]);
            }
            continue;
        }
        if (blk.funclet != 0) continue;    // a handler's block: emitted inside its funclet by selectHandler
        if (blk.pad && usesFunclets()) {
            // The funclet destroys and returns to the runtime; its cleanupret names the pad it chains to, or none.
            const std::string funclet = openFunclet(blk.insts.empty() ? 0 : f.inst(blk.insts[0]).pos, true);
            ssaInFunclet_ = true;
            a_->defLabel(ssaBlockLabel(f.blocks[b]));
            for (std::size_t k = 0; k < blk.insts.size(); k++) selectInst(f, f.inst(blk.insts[k]));
            ssaInFunclet_ = false;
            endCleanupFunclet();
            leaveFunclet();
            int toState = -1;
            if (blk.term.valid() && f.inst(blk.term).op == ir::Op::CleanupRet && !f.inst(blk.term).succ.empty())
                toState = ssaPadState_[f.inst(blk.term).succ[0].block.v];
            msSetState(ssaPadState_[f.blocks[b].v], toState, funclet);
            continue;
        }
        // A pad: the runtime lands here with the pointer in rax and the selector in edx, stored into the block's two
        // parameters; a branch from another pad arrives past the stores, at the block's own label, carrying both.
        if (blk.pad && blk.params.size() == 2) {
            a_->defLabel(ssaLandLabel(f.blocks[b]));
            a_->ins("mov", reg("%rax"), vslot(blk.params[0]));
            a_->ins("movslq", reg("%edx"), reg("%rdx"));     // the selector in its canonical 8-byte form
            a_->ins("mov", reg("%rdx"), vslot(blk.params[1]));
        }
        a_->defLabel(ssaBlockLabel(f.blocks[b]));
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const ir::Inst &in = f.inst(id);
            if (in.pos != 0 && in.pos != lastPos) { markLine(in.pos); lastPos = in.pos; }
            selectInst(f, in);
        }
    }
    // A function the parser gave landing pads whose tries nothing can throw through registered no row: the prologue
    // named the LSDA already, so an empty row defines it, as the walker's empty try does.
    if (fn.hasLandingPads() && callSites().empty() && !usesFunclets()) {
        const std::string none = label("ssatry", nextLabel());
        a_->defLabel(none);
        callSite(none, none, none, std::vector<std::string>(), std::vector<int>(), false, ++ssaRegionAt_, false);
    }
    // What walkBody writes after the body: the default return and the return label.
    if (isX87(fn.returns()))              a_->ins("fldz");
    else if (fn.returns()->isFloating())  a_->ins("pxor", reg("%xmm0"), reg("%xmm0"));
    else                                  a_->ins("mov", immText("0"), reg("%rax"));
    a_->defLabel(returnLabel_);
}
