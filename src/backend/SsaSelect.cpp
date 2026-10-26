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
    cur = roundUp(cur, 16);
    ssaAllocaSlot_.assign(f.frame.size(), 0);
    for (std::size_t i = 0; i < f.frame.size(); i++) {
        const Type *t = ssaTypes_->type(f.frame[i].type);
        const int size = t->size(target_), align = f.frame[i].align > 0 ? f.frame[i].align : t->align(target_);
        cur = roundUp(cur + size, align);
        ssaAllocaSlot_[i] = cur;
    }
    ssaValueSlot_.assign(f.values.size(), 0);
    for (std::size_t i = 1; i < f.values.size(); i++) {
        if (!f.values[i].type.valid() || ssaTypes_->type(f.values[i].type)->isVoid()) continue;
        cur += 8;
        ssaValueSlot_[i] = cur;
    }
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
    using ir::Op;
    const Type *rt = i.type.valid() ? ssaTypes_->type(i.type) : nullptr;
    switch (i.op) {
    case Op::Alloca:
        // The frame object's address is the alloca's value: taken once, here, into its slot.
        a_->ins("lea", local(ssaAllocaSlot_[static_cast<std::size_t>(i.k)]), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case Op::Unreachable:
    case Op::Undef:
        return;
    case Op::IConst:
        a_->ins("mov", imm(i.k), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case Op::Null:
        a_->ins("mov", immText("0"), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case Op::FConst:
        if (genKind(rt) == Kind::Float) {
            a_->ins("mov", imm(static_cast<unsigned int>(i.bits)), reg("%eax"));
            a_->ins("movd", reg("%eax"), reg("%xmm0"));
        } else {
            a_->ins("movabs", imm(i.bits), reg("%rax"));
            a_->ins("movq", reg("%rax"), reg("%xmm0"));
        }
        ssaStore(f, i.result);
        return;
    case Op::Addr:
        if (target_.microsoftNames() || definedHere_.count(i.sym) != 0) a_->ins("lea", rip(i.sym), reg("%rax"));
        else a_->ins("mov", rip(i.sym + "@GOTPCREL"), reg("%rax"));
        if (i.k != 0) a_->ins("add", imm(i.k), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case Op::Load:
        ssaLoad(f, f.operand(i, 0));
        load(rt);
        ssaStore(f, i.result);
        return;
    case Op::Store: {
        const ir::ValueId p = f.operand(i, 0), v = f.operand(i, 1);
        ssaLoad(f, v);
        a_->ins("mov", vslot(p), reg(abi_.scratch));
        store(typeOf(f, v));
        return;
    }
    case Op::Gep:
        ssaLoad(f, f.operand(i, 0));
        if (i.count == 2) a_->ins("add", vslot(f.operand(i, 1)), reg("%rax"));
        else if (i.k != 0) a_->ins("add", imm(i.k), reg("%rax"));
        ssaStore(f, i.result);
        return;
    case Op::Add: case Op::Sub: case Op::Mul: case Op::And: case Op::Or: case Op::Xor:
    case Op::SDiv: case Op::UDiv: case Op::SRem: case Op::URem: case Op::Shl: case Op::LShr: case Op::AShr: {
        const bool wide = rt->size(target_) == 8;
        const char *acc_ = acc(rt), *d = rhs(rt);
        if (i.op == Op::Shl || i.op == Op::LShr || i.op == Op::AShr) {
            a_->ins("mov", vslot(f.operand(i, 1)), reg("%rcx"));
            ssaLoad(f, f.operand(i, 0));
            a_->ins(i.op == Op::Shl ? "shl" : i.op == Op::AShr ? "sar" : "shr", reg("%cl"), reg(acc_));
            canonicalise(rt);
            ssaStore(f, i.result);
            return;
        }
        a_->ins("mov", vslot(f.operand(i, 1)), reg(abi_.scratch));
        ssaLoad(f, f.operand(i, 0));
        switch (i.op) {
        case Op::Add: a_->ins("add", reg(d), reg(acc_)); break;
        case Op::Sub: a_->ins("sub", reg(d), reg(acc_)); break;
        case Op::Mul: a_->ins("imul", reg(d), reg(acc_)); break;
        case Op::And: a_->ins("and", reg(d), reg(acc_)); break;
        case Op::Or:  a_->ins("or", reg(d), reg(acc_)); break;
        case Op::Xor: a_->ins("xor", reg(d), reg(acc_)); break;
        default: {
            const bool sign = i.op == Op::SDiv || i.op == Op::SRem;
            if (sign) a_->ins(wide ? "cqo" : "cdq");
            else      a_->ins("xor", reg("%edx"), reg("%edx"));
            a_->ins(sign ? "idiv" : "div", reg(d));
            if (i.op == Op::SRem || i.op == Op::URem) a_->ins("mov", reg(wide ? "%rdx" : "%edx"), reg(acc_));
            break;
        }
        }
        canonicalise(rt);
        ssaStore(f, i.result);
        return;
    }
    case Op::Neg: case Op::Not:
        ssaLoad(f, f.operand(i, 0));
        a_->ins(i.op == Op::Neg ? "neg" : "not", reg(acc(rt)));
        canonicalise(rt);
        ssaStore(f, i.result);
        return;
    case Op::ICmp: {
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
    case Op::FAdd: case Op::FSub: case Op::FMul: case Op::FDiv: {
        const bool dbl = genKind(rt) == Kind::Double;
        const char *sfx = dbl ? "sd" : "ss";
        ssaLoad(f, f.operand(i, 0));
        a_->ins(dbl ? "movsd" : "movss", vslot(f.operand(i, 1)), reg("%xmm1"));
        const char *name = i.op == Op::FAdd ? "add" : i.op == Op::FSub ? "sub" : i.op == Op::FMul ? "mul" : "div";
        a_->ins(std::string(name) + sfx, reg("%xmm1"), reg("%xmm0"));
        ssaStore(f, i.result);
        return;
    }
    case Op::FNeg:
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
    case Op::FCmp: {
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
    case Op::Trunc: case Op::SExt: case Op::ZExt: case Op::FpToSi: case Op::FpToUi: case Op::SiToFp: case Op::UiToFp:
    case Op::FpExt: case Op::FpTrunc: case Op::Bitcast: case Op::PtrToInt: case Op::IntToPtr:
        ssaLoad(f, f.operand(i, 0));
        genConversion(typeOf(f, f.operand(i, 0)), rt);
        ssaStore(f, i.result);
        return;
    case Op::Br:
        ssaBranchArgs(f, i.succ[0]);
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
        return;
    case Op::CondBr: {
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
    case Op::Switch:
        for (std::size_t s = 0; s < i.succ.size(); s++) if (i.succ[s].count != 0) { ssaUnsupported(i); return; }
        ssaLoad(f, f.operand(i, 0));
        for (std::size_t s = 1; s < i.succ.size(); s++) caseBranch(i.cases[s - 1], ssaBlockLabel(i.succ[s].block));
        a_->ins("jmp", lbl(ssaBlockLabel(i.succ[0].block)));
        return;
    case Op::Ret:
        if (i.count == 1) ssaLoad(f, f.operand(i, 0));
        a_->ins("jmp", lbl(returnLabel_));
        return;
    case Op::Call: {
        // The walker's own call, over Var nodes naming the slots: placeArguments and every ABI rule stay where they are.
        const ir::CallInfo &c = f.calls[i.call];
        const Type *sig = ssaTypes_->type(c.signature);
        std::vector<ExprPtr> args;
        const std::uint32_t firstArg = c.symbol.empty() ? 1 : 0;
        for (std::uint32_t n = firstArg; n < i.count; n++) {
            const ir::ValueId v = f.operand(i, n);
            std::unique_ptr<Var> a(Var::local("$v" + std::to_string(v.v), static_cast<int>(ssaValueSlot_[v.v])));
            a->setType(typeOf(f, v));
            args.push_back(std::move(a));
        }
        ExprPtr callee;
        if (c.symbol.empty()) {
            const ir::ValueId v = f.operand(i, 0);
            std::unique_ptr<Var> p(Var::local("$v" + std::to_string(v.v), static_cast<int>(ssaValueSlot_[v.v])));
            p->setType(typeOf(f, v));
            callee = std::move(p);
        }
        std::unique_ptr<Call> call = make_owned<Call>(c.symbol, std::move(callee), std::move(args), c.variadic, 0, c.namedArgs);
        call->setSymbol(c.symbol);
        call->setHasThis(c.hasThis);
        call->setType(rt != nullptr ? rt : sig->returns());
        call->accept(*this);
        ssaStore(f, i.result);
        return;
    }
    default:
        ssaUnsupported(i);
    }
}

// The body: the parameters copied from the slots receiveParameters filled into their values' slots, then each block.
void X86_64Linux::selectBody(const Function &fn, const ir::Function &f) {
    const std::vector<Param> &ps = fn.params();
    for (std::size_t i = 0; i < ps.size() && i < f.params.size(); i++) {
        a_->ins("lea", local(ps[i].offset), reg("%rax"));
        load(ps[i].type);
        ssaStore(f, f.params[i]);
    }
    std::size_t lastPos = 0;
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        a_->defLabel(ssaBlockLabel(f.blocks[b]));
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const ir::Inst &in = f.inst(id);
            if (in.pos != 0 && in.pos != lastPos) { markLine(in.pos); lastPos = in.pos; }
            selectInst(f, in);
        }
    }
    // What walkBody writes after the body: the default return and the return label.
    if (isX87(fn.returns()))              a_->ins("fldz");
    else if (fn.returns()->isFloating())  a_->ins("pxor", reg("%xmm0"), reg("%xmm0"));
    else                                  a_->ins("mov", immText("0"), reg("%rax"));
    a_->defLabel(returnLabel_);
}
