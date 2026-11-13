// **Selection of the SSA IR into the arm64 walker's stream** (M5.1, M5.2; doc-cxx3/M5-PLAN.md). Every value has an 8-byte slot
// below x29 in canonical form; operands travel in x0/x1 and d0/d1, x9 addresses. A class is its address, copied in and out
// by the walker's own code; an invoke is one LSDA row. The prologue, the epilogue and placeArguments are the walker's.

#include "Arm64Darwin.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {

long long roundUp(long long n, long long a) { return (n + a - 1) / a * a; }

// What a slot holds as a value: an integer, a pointer, a pointer to data member or a float of 1, 2, 4 or 8 bytes.
bool a64Scalar(const Type *t, const Target &target) {
    if (t == nullptr || t->isStructOrUnion() || t->isArray()) return false;
    if (!(t->isInteger() || t->isPointer() || t->isFloating() || t->isNullPtr() || t->isMemberPointer())) return false;
    const int s = t->size(target);
    return s == 1 || s == 2 || s == 4 || s == 8;
}

// A class or an array as a value is its address in the slot, as the walker's load leaves it (x86's selector's rule).
bool a64Whole(const Type *t) { return t != nullptr && (t->isStructOrUnion() || t->isArray()); }

std::string a64Fp(const Type *t, int n) { return std::string(t->kind() == Kind::Float ? "s" : "d") + std::to_string(n); }

// The instructions a class value may be the result or an operand of: its address read, stored, passed or moved.
bool a64Keeps(ir::Op op) {
    return op == ir::Op::Load || op == ir::Op::Store || op == ir::Op::Call || op == ir::Op::Invoke || op == ir::Op::Ret ||
           op == ir::Op::Br || op == ir::Op::CondBr || op == ir::Op::Switch || op == ir::Op::Undef || op == ir::Op::VaArg;
}

// A bitcast between a class or an array and an address - an array's decay - moves the address it is.
bool a64Moves(const Type *to, const Type *from) {
    return (a64Whole(to) || a64Whole(from)) && (a64Whole(to) || to->isPointer()) && (a64Whole(from) || from->isPointer());
}

// Whether an alloca's text (the lowering names it by its declaration, `const char * label`) declares `name`: a whole word of it.
bool a64Declares(const std::string &text, const std::string &name) {
    for (std::size_t at = text.find(name); !name.empty() && at != std::string::npos; at = text.find(name, at + 1)) {
        const std::size_t end = at + name.size();
        const bool before = at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_');
        const bool after = end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
        if (!before && !after) return true;
    }
    return false;
}

// A constant as its slot holds it: the type's bits, extended as the type says.
long long a64Canonical(long long k, const Type *t, const Target &target) {
    const int bits = 8 * t->size(target);
    if (bits >= 64) return k;
    const unsigned long long low = static_cast<unsigned long long>(k) & ((1ull << bits) - 1);
    const bool minus = t->isSigned(target) && ((low >> (bits - 1)) & 1) != 0;
    return static_cast<long long>(minus ? low | ~((1ull << bits) - 1) : low);
}

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
    case ir::Op::Invoke: case ir::Op::CatchPad: case ir::Op::CleanupPad: case ir::Op::TlsAddr: case ir::Op::VaStart:
    case ir::Op::VaArg: case ir::Op::VaEnd:
        return true;
    default:
        return false;
    }
}

}   // namespace

// **The fallback seam**: why `f` is not selected, empty where every part of it is inside the subset.
std::string Arm64Darwin::ssaOutside(const Function &fn, const ir::Function &f) const {
    if (f.params.size() != fn.params().size()) return "parameters the parser counts differently";
    for (std::size_t i = 0; i < fn.params().size(); i++)
        if (!a64Scalar(fn.params()[i].type, target_) && !fn.params()[i].type->isStructOrUnion() && !fn.params()[i].type->isReference())
            return "a parameter that is no scalar, no class and no reference";
    const Type *ret = ssaTypes_->type(f.returns);
    // A reference comes back as the address it is: the operand of every `ret` is then a pointer (asked below).
    const bool address = ret != nullptr && (ret->isStructOrUnion() || ret->isReference());
    if (ret == nullptr || (!ret->isVoid() && !a64Scalar(ret, target_) && !address)) return "a result that is no scalar and no class";
    if (ret->isStructOrUnion() && planFor(ret).byRef && fn.sretSlot() == 0) return "a class result with no pointer slot";
    for (std::size_t v = 1; v < f.values.size(); v++) {
        if (!f.values[v].type.valid()) continue;
        const Type *t = ssaTypes_->type(f.values[v].type);
        if (!t->isVoid() && !a64Scalar(t, target_) && !a64Whole(t)) return "a value that is no scalar, no class and no array";
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
            if (i.op == ir::Op::Ret && i.count == 1 && address && !typeOf(f, f.operand(i, 0))->isPointer() &&
                !(ret->isStructOrUnion() && a64Whole(typeOf(f, f.operand(i, 0))))) return "a class or a reference returned as a value";
            if (i.op == ir::Op::Bitcast && a64Moves(typeOf(f, i.result), typeOf(f, f.operand(i, 0)))) continue;
            if (!a64Keeps(i.op)) {
                if (i.result.valid() && a64Whole(typeOf(f, i.result))) return "a class value from '" + ir::opName(i.op) + "'";
                for (std::uint32_t n = 0; n < i.count; n++)
                    if (a64Whole(typeOf(f, f.operand(i, n)))) return "a class value into '" + ir::opName(i.op) + "'";
            }
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
                const Type *ot = typeOf(f, f.operand(i, n));
                if (ssaArgType(f, i, at)->isStructOrUnion() && (extra || !(ot->isPointer() || a64Whole(ot)))) return "a class through '...'";
                const Type *want = at < sig->params().size() ? sig->params()[at] : nullptr;
                if (a64Whole(ot) && !(want != nullptr && want->isStructOrUnion() && !Abi::passedByAddress(*want, target_)))
                    return "a class value the callee takes by address";
                if (extra && typeOf(f, f.operand(i, n))->kind() == Kind::Float) return "a float through '...'";
            }
        }
    }
    return std::string();
}

// The function's SSA body where it is selected; the walker's reason on stderr under CXX1_SSA_WHY, the census's switch.
const ir::Function &Arm64Darwin::ssaChoose(const Function &fn) {
    const ir::FuncId id = ssa_ != nullptr ? ssa_->find(fn.symbol()) : ir::FuncId();
    const std::string why = id.valid() ? ssaOutside(fn, ssa_->functions[id.v]) : std::string("no SSA body");
    if (why.empty()) { ssaSelected_++; return ssa_->functions[id.v]; }
    unsupported(("'" + fn.displayName() + "', " + why + ",").c_str());
}

// The frame above the parser's parameter slots: each alloca at its alignment, then one 8-byte slot per value.
int Arm64Darwin::layoutSsa(const Function &fn, const ir::Function &f) {
    ssaAssign(f);
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
    // A value in a register has no slot; a frame the saves' offsets could not reach keeps every value in its slot.
    const long long base = cur;
    for (int pass = 0; pass < 2; pass++) {
        cur = base;
        ssaValueSlot_.assign(f.values.size(), 0);
        for (std::size_t i = 1; i < f.values.size(); i++) {
            if (!f.values[i].type.valid() || ssaTypes_->type(f.values[i].type)->isVoid() || ssaReg_[i] != 0 || (ssaImm_[i] & 2) || ssaFold_[i]) continue;
            cur += 8;
            ssaValueSlot_[i] = cur;
        }
        if (roundUp(cur, 16) + kCalleeArea <= 65535) break;
        ssaReg_.assign(f.values.size(), 0);
        ssaSaved_.clear();
    }
    return static_cast<int>(roundUp(cur, 16));
}

// **Registers by a linear scan over the values' live ranges** (M5.2 round 3): liveness over the blocks, a range the hull
// of where the value is live; one that crosses a call takes x19-x28 or d8-d15, another x12-x15 or d16-d23 first.
void Arm64Darwin::ssaAssign(const ir::Function &f) {
    const std::size_t nv = f.values.size(), nb = f.blockArena.size();
    ssaReg_.assign(nv, 0);
    ssaImm_.assign(nv, 0);
    ssaImmK_.assign(nv, 0);
    ssaFlags_.assign(nv, 0);
    ssaFold_.assign(nv, 0);
    ssaLow_.assign(nv, 0);
    ssaSaved_.clear();
    if (optimize_ <= 0 || realign_ > 0) return;
    std::vector<int> first(nb, 0), last(nb, 0), start(nv, -1), end(nv, -1), uses(nv, 0), folds(nv, 0), home(nv, -1), calls;
    std::vector<const ir::Inst *> def(nv, nullptr);
    std::vector<long long> weight(nv, 0);
    // A block's depth: an edge to a block written no later is a loop's, and every block between the two is inside it.
    std::vector<std::size_t> index(nb, 0), reach(f.blocks.size(), 0);
    std::vector<int> depth(f.blocks.size(), 0);
    for (std::size_t b = 0; b < f.blocks.size(); b++) index[f.blocks[b].v] = b;
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        if (!f.block(f.blocks[b]).term.valid()) continue;
        const ir::Inst &t = f.inst(f.block(f.blocks[b]).term);
        for (std::size_t s = 0; s < t.succ.size(); s++)
            if (index[t.succ[s].block.v] <= b) reach[index[t.succ[s].block.v]] = std::max(reach[index[t.succ[s].block.v]], b + 1);
    }
    for (std::size_t h = 0; h < f.blocks.size(); h++)
        for (std::size_t b = h; b < reach[h]; b++) depth[b]++;
    const std::size_t words = (nv + 63) / 64;
    std::vector<unsigned long long> in(nb * words, 0), out(nb * words, 0);
    for (std::size_t i = 0; i < f.params.size(); i++) start[f.params[i].v] = 0;
    if (f.sret.valid()) start[f.sret.v] = 0;
    int at = 2;
    // Positions two apart: a block's parameters at its first, each instruction after; a use in another block is live in.
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const std::uint32_t id = f.blocks[b].v;
        const ir::Block &blk = f.block(f.blocks[b]);
        first[id] = at;
        for (std::size_t p = 0; p < blk.params.size(); p++) { start[blk.params[p].v] = at; home[blk.params[p].v] = static_cast<int>(id); }
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId iid = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!iid.valid()) continue;
            const ir::Inst &i = f.inst(iid);
            at += 2;
            if (i.result.valid()) { start[i.result.v] = at; home[i.result.v] = static_cast<int>(id); def[i.result.v] = &i; }
            if (i.result.valid() && (i.op == ir::Op::IConst || i.op == ir::Op::Null)) {
                ssaImmK_[i.result.v] = i.op == ir::Op::Null ? 0 : a64Canonical(i.k, ssaTypes_->type(i.type), target_);
                if (ssaImmK_[i.result.v] >= 0 && ssaImmK_[i.result.v] < 4096) ssaImm_[i.result.v] = 1;
            }
            if (i.op == ir::Op::Call || i.op == ir::Op::Invoke || i.op == ir::Op::TlsAddr) calls.push_back(at);
        }
        last[id] = at;
        at += 2;
    }
    ssaDefs_ = def;
    ssaFolds(f, def, home);
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const std::uint32_t id = f.blocks[b].v;
        const ir::Block &blk = f.block(f.blocks[b]);
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
            for (std::uint32_t n = 0; n < i.count; n++) use(f.operand(i, n), 0);
            // A folded address's base and index, and a folded multiply's operands, are read here.
            const bool access = (i.op == ir::Op::Load || i.op == ir::Op::Store) && i.count >= 1;
            if (access && ssaFold_[f.operand(i, 0).v] == 1) {
                const ir::Inst &g = *def[f.operand(i, 0).v];
                use(f.operand(g, 0), 0);
                if (g.count == 2 && ssaFold_[f.operand(g, 1).v] == 1) use(f.operand(*def[f.operand(g, 1).v], 0), 0);
                else if (g.count == 2) use(f.operand(g, 1), 0);
            }
            for (std::uint32_t n = 0; i.op == ir::Op::Add && n < i.count; n++)
                if (ssaFold_[f.operand(i, n).v] == 2) { use(f.operand(*def[f.operand(i, n).v], 0), 0); use(f.operand(*def[f.operand(i, n).v], 1), 0); }
            // A branch's argument is read after an invoke's result is written.
            for (std::size_t s = 0; s < i.succ.size(); s++)
                for (std::uint32_t n = 0; n < i.succ[s].count; n++) use(f.arg(i.succ[s], n), 1);
            // A small constant on the right of an add, a subtraction or a compare of words is an immediate.
            const bool folds2 = i.op == ir::Op::Add || i.op == ir::Op::Sub ||
                                (i.op == ir::Op::ICmp && typeOf(f, f.operand(i, 0))->size(target_) >= 4);
            if (folds2 && i.count == 2 && (ssaImm_[f.operand(i, 1).v] & 1)) { folds[f.operand(i, 1).v]++; weight[f.operand(i, 1).v] -= heat; }
            if (i.result.valid()) weight[i.result.v] += heat;
            const bool compare = (i.op == ir::Op::ICmp || i.op == ir::Op::FCmp) && k + 1 == blk.insts.size() && blk.term.valid();
            if (compare && f.inst(blk.term).op == ir::Op::CondBr && f.operand(f.inst(blk.term), 0).v == i.result.v) ssaFlags_[i.result.v] = 1;
        }
    }
    for (std::size_t v = 1; v < nv; v++) {
        if (ssaFlags_[v] && uses[v] != 1) ssaFlags_[v] = 0;
        if ((ssaImm_[v] & 1) && uses[v] > 0 && folds[v] == uses[v]) ssaImm_[v] |= 2;
    }
    // Live in = used from another block, or live out and not defined here; to a fixed point, last block first.
    for (bool changed = true; changed; ) {
        changed = false;
        for (std::size_t b = f.blocks.size(); b-- > 0; ) {
            const std::uint32_t id = f.blocks[b].v;
            const ir::Block &blk = f.block(f.blocks[b]);
            if (!blk.term.valid()) continue;
            const ir::Inst &t = f.inst(blk.term);
            for (std::size_t s = 0; s < t.succ.size(); s++)
                for (std::size_t w = 0; w < words; w++) out[id * words + w] |= in[t.succ[s].block.v * words + w];
            for (std::size_t w = 0; w < words; w++) {
                unsigned long long add = out[id * words + w] & ~in[id * words + w];
                for (std::size_t bit = 0; bit < 64 && (add >> bit) != 0; bit++) {
                    if (((add >> bit) & 1) == 0 || home[w * 64 + bit] == static_cast<int>(id)) continue;
                    in[id * words + w] |= 1ull << bit;
                    changed = true;
                }
            }
        }
    }
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const std::uint32_t id = f.blocks[b].v;
        for (std::size_t v = 1; v < nv; v++) {
            const unsigned long long bit = 1ull << (v % 64);
            if (in[id * words + v / 64] & bit) { start[v] = start[v] < 0 ? first[id] : std::min(start[v], first[id]); end[v] = std::max(end[v], first[id]); }
            if (out[id * words + v / 64] & bit) end[v] = std::max(end[v], last[id] + 1);
        }
    }
    std::vector<std::size_t> order;
    for (std::size_t v = 1; v < nv; v++) {
        if (!f.values[v].type.valid() || ssaTypes_->type(f.values[v].type)->isVoid() || start[v] < 0 || (ssaImm_[v] & 2) || ssaFlags_[v] || ssaFold_[v]) continue;
        end[v] = std::max(end[v], start[v] + 1);
        order.push_back(v);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return start[a] < start[b]; });
    // Bit r of a file is register r free: scratch below, the saved ones above; a range ended gives its register back.
    const unsigned scratch[2] = {0xf000u, 0xff0000u}, kept[2] = {0x1ff80000u, 0xff00u};
    unsigned freeNow[2] = {scratch[0] | kept[0], scratch[1] | kept[1]}, taken[2] = {0, 0};
    std::vector<std::size_t> active;
    for (std::size_t n = 0; n < order.size(); n++) {
        const std::size_t v = order[n];
        const int file = ssaTypes_->type(f.values[v].type)->isFloating() ? 1 : 0;
        for (std::size_t a = 0; a < active.size(); )
            if (end[active[a]] <= start[v]) {
                freeNow[ssaTypes_->type(f.values[active[a]].type)->isFloating() ? 1 : 0] |= 1u << ssaReg_[active[a]];
                active.erase(active.begin() + static_cast<std::ptrdiff_t>(a));
            } else a++;
        const std::vector<int>::const_iterator call = std::upper_bound(calls.begin(), calls.end(), start[v]);
        const bool crosses = call != calls.end() && *call < end[v];
        unsigned pick = crosses ? 0 : freeNow[file] & scratch[file];
        if (pick == 0) pick = freeNow[file] & kept[file];
        if (pick == 0) {
            // The lightest range in a register this one could take gives it up where this one weighs more.
            std::size_t light = active.size();
            for (std::size_t a = 0; a < active.size(); a++) {
                const std::size_t o = active[a];
                if ((ssaTypes_->type(f.values[o].type)->isFloating() ? 1 : 0) != file || (crosses && ((kept[file] >> ssaReg_[o]) & 1) == 0)) continue;
                if (light == active.size() || weight[o] < weight[active[light]]) light = a;
            }
            if (light == active.size() || weight[active[light]] >= weight[v]) continue;
            pick = 1u << ssaReg_[active[light]];
            ssaReg_[active[light]] = 0;
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(light));
        }
        int r = 0;
        while (((pick >> r) & 1) == 0) r++;
        ssaReg_[v] = r;
        freeNow[file] &= ~(1u << r);
        taken[file] |= 1u << r;
        active.push_back(v);
    }
    // A constant left without a register has no slot either: it is written again where it is read.
    for (std::size_t v = 1; v < nv; v++)
        if (def[v] != nullptr && (def[v]->op == ir::Op::IConst || def[v]->op == ir::Op::Null) && ssaReg_[v] == 0) ssaImm_[v] |= 2;
    for (int r = 19; r <= 28; r++) if (taken[0] & (1u << r)) ssaSaved_.push_back("x" + std::to_string(r));
    for (int r = 8; r <= 15; r++) if (taken[1] & (1u << r)) ssaSaved_.push_back("d" + std::to_string(r));
}

namespace {

// The scale an index multiplied (or shifted) by `k` gives an access of `sz` bytes, 0 where it is none.
bool a64Scaled(const ir::Function &f, const ir::Inst *m, const std::vector<const ir::Inst *> &def, int sz) {
    if (m == nullptr || m->count != 2 || (m->op != ir::Op::Mul && m->op != ir::Op::Shl) || sz < 2) return false;
    const ir::Inst *k = def[f.operand(*m, 1).v];
    if (k == nullptr || k->op != ir::Op::IConst) return false;
    return m->op == ir::Op::Mul ? k->k == sz : k->k >= 1 && k->k <= 3 && (1 << k->k) == sz;
}

}   // namespace

// **What folds into its reader** (M5.3): a gep whose every reader is a load or store of its block at an offset the access
// scales, with its index's multiply where that is the access's size; a multiply whose one reader is the add after it.
// `ssaLow_`: a signed 32-bit result read only as 32 bits (word operations, compares, stores, returns, the edges into such).
void Arm64Darwin::ssaFolds(const ir::Function &f, const std::vector<const ir::Inst *> &def, const std::vector<int> &home) {
    const std::size_t nv = f.values.size();
    std::vector<int> count(nv, 0), size(nv, 0);
    std::vector<char> need(nv, 0), wrong(nv, 0);
    std::vector<std::pair<std::uint32_t, std::uint32_t> > edges;
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId iid = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!iid.valid()) continue;
            const ir::Inst &i = f.inst(iid);
            const Type *rt = i.type.valid() ? ssaTypes_->type(i.type) : nullptr;
            const bool word = rt != nullptr && rt->isInteger() && rt->size(target_) == 4;
            for (std::uint32_t n = 0; n < i.count; n++) {
                const ir::ValueId v = f.operand(i, n);
                count[v.v]++;
                const Type *ot = typeOf(f, v);
                // The address of a load or store, one of its block, at an offset it scales.
                const bool access = (i.op == ir::Op::Load || i.op == ir::Op::Store) && n == 0 && !a64Whole(i.op == ir::Op::Load ? rt : typeOf(f, f.operand(i, 1)));
                const int sz = access ? (i.op == ir::Op::Load ? rt : typeOf(f, f.operand(i, 1)))->size(target_) : 0;
                const ir::Inst *g = def[v.v];
                const bool fits = access && g != nullptr && g->op == ir::Op::Gep && home[v.v] == static_cast<int>(f.blocks[b].v) &&
                                  (g->count == 2 || (g->k >= 0 && g->k % sz == 0 && g->k / sz < 4096));
                if (!fits) wrong[v.v] = 1;
                else if (size[v.v] == 0 || size[v.v] == sz) size[v.v] = sz;
                else size[v.v] = -1;
                // Whether the upper half of a signed word is read here.
                if (!ot->isInteger() || ot->size(target_) != 4) continue;
                const bool arith = word && (i.op == ir::Op::Add || i.op == ir::Op::Sub || i.op == ir::Op::Mul || i.op == ir::Op::And ||
                                            i.op == ir::Op::Or || i.op == ir::Op::Xor || i.op == ir::Op::SDiv || i.op == ir::Op::UDiv ||
                                            i.op == ir::Op::SRem || i.op == ir::Op::URem || i.op == ir::Op::Shl || i.op == ir::Op::LShr ||
                                            i.op == ir::Op::AShr || i.op == ir::Op::Neg || i.op == ir::Op::Not);
                const bool narrow = (i.op == ir::Op::Trunc || i.op == ir::Op::SExt || i.op == ir::Op::ZExt || i.op == ir::Op::Bitcast) &&
                                    rt != nullptr && rt->isInteger() && rt->size(target_) <= 4;
                const bool safe = arith || narrow || i.op == ir::Op::ICmp || i.op == ir::Op::Ret || i.op == ir::Op::CondBr ||
                                  (i.op == ir::Op::Store && n == 1);
                if (!safe) need[v.v] = 1;
            }
            for (std::size_t s = 0; s < i.succ.size(); s++)
                for (std::uint32_t n = 0; n < i.succ[s].count; n++) {
                    count[f.arg(i.succ[s], n).v]++;
                    wrong[f.arg(i.succ[s], n).v] = 1;
                    edges.push_back(std::make_pair(f.arg(i.succ[s], n).v, f.block(i.succ[s].block).params[n].v));
                }
        }
    }
    for (std::size_t v = 1; v < nv; v++) if (f.values[v].type.valid() && wrong[v] == 0 && count[v] > 0 && def[v] != nullptr && def[v]->op == ir::Op::Gep) {
        ssaFold_[v] = 1;
        const ir::Inst &g = *def[v];
        if (g.count != 2) continue;
        const ir::ValueId x = f.operand(g, 1);
        if (count[x.v] == 1 && home[x.v] == home[v] && size[v] > 0 && a64Scaled(f, def[x.v], def, size[v])) ssaFold_[x.v] = 1;
    }
    // A multiply just before the add it alone feeds, at the add's width: no range grows past a value written between
    // them. Not `msub`: the bench's hash ran 132 -> 136 ms with it (a remainder by a constant), so a sub keeps its mul.
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        for (std::size_t k = 0; k < blk.insts.size(); k++) {
            const ir::Inst &i = f.inst(blk.insts[k]);
            if (i.op != ir::Op::Add || i.count != 2 || (ssaImm_[f.operand(i, 1).v] & 1)) continue;
            const int width = ssaTypes_->type(i.type)->size(target_);
            if (width != 4 && width != 8) continue;
            for (std::uint32_t n = 0; n < 2; n++) {
                const ir::ValueId m = f.operand(i, n);
                if (def[m.v] == nullptr || def[m.v]->op != ir::Op::Mul || count[m.v] != 1 || k == 0 || f.inst(blk.insts[k - 1]).result.v != m.v ||
                    ssaFold_[m.v] != 0 || ssaTypes_->type(def[m.v]->type)->size(target_) != width || f.operand(i, 0).v == f.operand(i, 1).v) continue;
                ssaFold_[m.v] = 2;
                break;
            }
        }
    }
    for (bool changed = true; changed; ) {
        changed = false;
        for (std::size_t e = 0; e < edges.size(); e++)
            if (need[edges[e].second] && !need[edges[e].first]) { need[edges[e].first] = 1; changed = true; }
    }
    for (std::size_t v = 1; v < nv; v++) ssaLow_[v] = need[v] ? 0 : 1;
}

// A load's or a store's address: the folded gep's base, offset or index (scaled), else `[xN]` of its own register.
std::string Arm64Darwin::ssaAddress(const ir::Function &f, ir::ValueId a, int n, int sz) {
    if (ssaFold_[a.v] != 1) return "[x" + std::to_string(ssaUse(f, a, n)) + "]";
    const ir::Inst *g = ssaDefs_[a.v];
    const std::string base = "[x" + std::to_string(ssaUse(f, f.operand(*g, 0), n));
    if (g->count == 1) return g->k == 0 ? base + "]" : base + ", #" + std::to_string(g->k) + "]";
    const ir::ValueId x = f.operand(*g, 1);
    if (ssaFold_[x.v] != 1) return base + ", x" + std::to_string(ssaUse(f, x, 2)) + "]";
    const ir::Inst *m = ssaDefs_[x.v];
    const int shift = sz == 8 ? 3 : sz == 4 ? 2 : 1;
    return base + ", x" + std::to_string(ssaUse(f, f.operand(*m, 0), 2)) + ", lsl #" + std::to_string(shift) + "]";
}

// A value as its 8 bytes, to x0 or from it: the parallel move's staging.
void Arm64Darwin::ssaRaw(const ir::Function &f, ir::ValueId v, bool write) {
    const int r = ssaReg_[v.v];
    if (r == 0 && (ssaImm_[v.v] & 2)) { movImm("x0", ssaImmK_[v.v]); return; }
    if (r == 0) { ssaSlot(write ? "st" : "ld", "x0", ssaValueSlot_[v.v]); return; }
    const std::string own = (typeOf(f, v)->isFloating() ? "d" : "x") + std::to_string(r);
    out_ << (own[0] == 'd' ? "  fmov " : "  mov ") << (write ? own : std::string("x0")) << ", " << (write ? std::string("x0") : own) << "\n";
}

// Value `v` into the register named: a move from its own register, or a read of its slot.
void Arm64Darwin::ssaInto(const ir::Function &f, ir::ValueId v, const std::string &reg) {
    const int r = ssaReg_[v.v];
    if (r == 0 && (ssaImm_[v.v] & 2)) { movImm(reg.c_str(), ssaImmK_[v.v]); return; }
    if (r == 0) { ssaSlot("ld", reg, ssaValueSlot_[v.v]); return; }
    if (typeOf(f, v)->isFloating()) out_ << "  fmov " << reg << ", " << reg[0] << r << "\n";
    else out_ << "  mov " << reg << ", x" << r << "\n";
}

// The register an operand is read from: its own, or register `n` of its kind after a read of its slot.
int Arm64Darwin::ssaUse(const ir::Function &f, ir::ValueId v, int n) {
    if (ssaReg_[v.v] != 0) return ssaReg_[v.v];
    ssaLoad(f, v, n);
    return n;
}

// The address x29 - off into `dst`.
void Arm64Darwin::ssaFrame(const char *dst, long long off) {
    if (realign_ > 0) { localIn(dst, static_cast<int>(off)); return; }   // the floor in x19, as the walker's slots
    if (off < 4096) { out_ << "  sub " << dst << ", x29, #" << off << "\n"; return; }
    movImm("x9", off);
    out_ << "  sub " << dst << ", x29, x9\n";
}

// A slot read ("ld") or written ("st"): unscaled from x29 where the offset reaches, through x9 otherwise.
void Arm64Darwin::ssaSlot(const char *op, const std::string &reg, long long off) {
    if (off <= 255 && realign_ == 0) { out_ << "  " << op << "ur " << reg << ", [x29, #-" << off << "]\n"; return; }
    ssaFrame("x9", off);
    out_ << "  " << op << "r " << reg << ", [x9]\n";
}

// Operand `v` into register `n` of its kind: xN, or dN/sN for a floating value.
void Arm64Darwin::ssaLoad(const ir::Function &f, ir::ValueId v, int n) {
    const Type *t = typeOf(f, v);
    ssaInto(f, v, t->isFloating() ? a64Fp(t, n) : "x" + std::to_string(n));
}

// The result, from x0 or d0/s0, into its slot.
void Arm64Darwin::ssaStore(const ir::Function &f, ir::ValueId v) {
    if (!v.valid()) return;
    const Type *t = typeOf(f, v);
    if (ssaReg_[v.v] != 0 && t->isFloating()) out_ << "  fmov " << a64Fp(t, ssaReg_[v.v]) << ", " << a64Fp(t, 0) << "\n";
    else if (ssaReg_[v.v] != 0) out_ << "  mov x" << ssaReg_[v.v] << ", x0\n";
    if (ssaReg_[v.v] != 0 || ssaValueSlot_[v.v] == 0) return;
    ssaSlot("st", t->isFloating() ? a64Fp(t, 0) : std::string("x0"), ssaValueSlot_[v.v]);
}

// A branch's arguments into the target's parameters as a parallel move of whole places (two values may share a register):
// a copy whose place no copy left reads goes first; a cycle left over is staged on the stack, each read before any write.
bool Arm64Darwin::ssaBranchArgs(const ir::Function &f, const ir::Succ &s, bool ask) {
    const ir::Block &to = f.block(s.block);
    const auto place = [&](ir::ValueId v) {
        return ssaReg_[v.v] != 0 ? (typeOf(f, v)->isFloating() ? 100 : 0) + ssaReg_[v.v] : 1000 + ssaValueSlot_[v.v];
    };
    std::vector<std::pair<ir::ValueId, ir::ValueId> > moves;
    for (std::uint32_t i = 0; i < s.count; i++)
        if (place(f.arg(s, i)) != place(to.params[i]) && place(to.params[i]) != 1000) moves.push_back(std::make_pair(f.arg(s, i), to.params[i]));
    if (ask) return !moves.empty();
    for (std::size_t m = 0; m < moves.size(); ) {
        bool read = false;
        for (std::size_t o = 0; o < moves.size(); o++) read = read || (o != m && place(moves[o].first) == place(moves[m].second));
        if (read) { m++; continue; }
        const int r = ssaReg_[moves[m].second.v];
        if (r != 0) ssaInto(f, moves[m].first, (typeOf(f, moves[m].second)->isFloating() ? "d" : "x") + std::to_string(r));
        else { ssaRaw(f, moves[m].first, false); ssaRaw(f, moves[m].second, true); }
        moves.erase(moves.begin() + static_cast<std::ptrdiff_t>(m));
        m = 0;
    }
    for (std::size_t i = 0; i < moves.size(); i++) { ssaRaw(f, moves[i].first, false); push(); }
    for (std::size_t i = moves.size(); i-- > 0; ) { pop("x0"); ssaRaw(f, moves[i].second, true); }
    return true;
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
            if (copyAt[n] != 0) ssaFrame("x9", copyAt[n]); else ssaInto(f, v, "x9");
            aggToRegisters(plan.args[n].plan, plan.args[n].firstReg);
            continue;
        }
        ssaInto(f, v, t->isFloating() ? a64Fp(t, plan.args[n].firstReg) : std::string(abi_.intRegs[plan.args[n].firstReg]));
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
        ssaInto(f, f.operand(i, 0), "x17");
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
    const int d = ssaDef(i.result);
    const std::string xd = "x" + std::to_string(d);
    switch (i.op) {
    case ir::Op::Undef:
        return;
    case ir::Op::Unreachable:
        out_ << "  brk #0x1\n";
        return;
    case ir::Op::Alloca:
        ssaFrame(xd.c_str(), ssaAllocaSlot_[static_cast<std::size_t>(i.k)]);
        ssaKeep(f, i.result);
        return;
    case ir::Op::IConst: {
        if (ssaImm_[i.result.v] & 2) return;
        // Optimized, the constant is written in its canonical form at once.
        movImm(xd.c_str(), optimize_ > 0 ? a64Canonical(i.k, rt, target_) : i.k);
        if (optimize_ <= 0) narrowInt(rt, d, d);
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::Null:
        if (ssaImm_[i.result.v] & 2) return;
        out_ << "  mov " << xd << ", #0\n";
        ssaKeep(f, i.result);
        return;
    case ir::Op::FConst:
        movImm("x9", static_cast<long long>(i.bits));
        out_ << "  fmov " << a64Fp(rt, d) << (rt->kind() == Kind::Float ? ", w9\n" : ", x9\n");
        ssaKeep(f, i.result);
        return;
    case ir::Op::Addr: {
        // A symbol this unit defines, a label of its own (a string's `.L.str.N`: no identifier begins with '.'), or one through the GOT.
        const bool here = definedHere_.count(i.sym) != 0, own = !here && !i.sym.empty() && i.sym[0] == '.';
        const std::string name = (own ? "" : "_") + i.sym;
        if (here || own) out_ << "  adrp " << xd << ", " << name << "@PAGE\n  add " << xd << ", " << xd << ", " << name << "@PAGEOFF\n";
        else out_ << "  adrp " << xd << ", " << name << "@GOTPAGE\n  ldr " << xd << ", [" << xd << ", " << name << "@GOTPAGEOFF]\n";
        if (i.k != 0) { movImm("x9", i.k); out_ << "  add " << xd << ", " << xd << ", x9\n"; }
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::TlsAddr:
        tlsAddress(i.sym, i.k != 0);
        ssaStore(f, i.result);
        return;
    case ir::Op::VaStart: case ir::Op::VaArg:
        ssaLoad(f, f.operand(i, 0), 0);
        if (i.op == ir::Op::VaStart) { vaStartAt(); return; }
        vaArgAt(rt);
        ssaStore(f, i.result);
        return;
    case ir::Op::VaEnd:
        return;
    case ir::Op::Load: {
        // The walker's load, from the address's own register into the result's.
        if (a64Whole(rt)) { ssaLoad(f, f.operand(i, 0), 0); ssaStore(f, i.result); return; }
        const int sz = rt->size(target_);
        const std::string at = ssaAddress(f, f.operand(i, 0), 0, sz);
        const bool sign = rt->isSigned(target_);
        if (rt->isFloating()) out_ << "  ldr " << a64Fp(rt, d);
        else out_ << (sz == 1 ? (sign ? "  ldrsb x" : "  ldrb w") : sz == 2 ? (sign ? "  ldrsh x" : "  ldrh w") : sz == 4 ? (sign ? "  ldrsw x" : "  ldr w") : "  ldr x") << d;
        out_ << ", " << at << "\n";
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::Store: {
        const Type *t = typeOf(f, f.operand(i, 1));
        const int v = ssaUse(f, f.operand(i, 1), 0), sz = t->size(target_);
        if (a64Whole(t)) { copyBlock(sz, ("x" + std::to_string(v)).c_str(), ("x" + std::to_string(ssaUse(f, f.operand(i, 0), 1))).c_str()); return; }
        const std::string to = ssaAddress(f, f.operand(i, 0), 1, sz);
        if (t->isFloating()) out_ << "  str " << a64Fp(t, v);
        else out_ << (sz == 1 ? "  strb w" : sz == 2 ? "  strh w" : sz == 4 ? "  str w" : "  str x") << v;
        out_ << ", " << to << "\n";
        return;
    }
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
    case ir::Op::Gep: {
        if (ssaFold_[i.result.v]) return;
        const int a = ssaUse(f, f.operand(i, 0), 0);
        const int b = i.count == 2 ? ssaUse(f, f.operand(i, 1), 1) : 0;
        if (i.count == 2) out_ << "  add " << xd << ", x" << a << ", x" << b << "\n";
        else if (i.k > 0 && i.k < 4096 && optimize_ > 0) out_ << "  add " << xd << ", x" << a << ", #" << i.k << "\n";
        else if (i.k != 0) { movImm("x9", i.k); out_ << "  add " << xd << ", x" << a << ", x9\n"; }
        else if (d != a) out_ << "  mov " << xd << ", x" << a << "\n";
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::Add: case ir::Op::Sub: case ir::Op::Mul: case ir::Op::And: case ir::Op::Or: case ir::Op::Xor:
    case ir::Op::SDiv: case ir::Op::UDiv: case ir::Op::SRem: case ir::Op::URem: case ir::Op::Shl: case ir::Op::LShr: case ir::Op::AShr: {
        // At the type's width - 32 bits for anything narrower, whose operands are canonical - then extended again.
        const char *r = rt->size(target_) == 8 ? "x" : "w";
        if (ssaFold_[i.result.v]) return;
        // A multiply folded into this add: `madd` with the other side as the addend.
        const int fused = ssaFold_[f.operand(i, 0).v] == 2 ? 0 : ssaFold_[f.operand(i, 1).v] == 2 ? 1 : -1;
        if (fused >= 0 && i.op == ir::Op::Add) {
            const ir::Inst &m = *ssaDefs_[f.operand(i, static_cast<std::uint32_t>(fused)).v];
            const int x = ssaUse(f, f.operand(m, 0), 0), y = ssaUse(f, f.operand(m, 1), 1);
            const int c = ssaUse(f, f.operand(i, static_cast<std::uint32_t>(1 - fused)), 2);
            out_ << "  madd " << r << d << ", " << r << x << ", " << r << y << ", " << r << c << "\n";
            if (rt->size(target_) != 4 || (rt->isSigned(target_) && !ssaLow_[i.result.v])) narrowInt(rt, d, d);
            ssaKeep(f, i.result);
            return;
        }
        const int a = ssaUse(f, f.operand(i, 0), 0);
        if ((ssaImm_[f.operand(i, 1).v] & 1) && (i.op == ir::Op::Add || i.op == ir::Op::Sub)) {
            out_ << (i.op == ir::Op::Add ? "  add " : "  sub ") << r << d << ", " << r << a << ", #" << ssaImmK_[f.operand(i, 1).v] << "\n";
        } else if (i.op == ir::Op::SRem || i.op == ir::Op::URem) {
            const int b = ssaUse(f, f.operand(i, 1), 1);
            out_ << (i.op == ir::Op::SRem ? "  sdiv " : "  udiv ") << r << "2, " << r << a << ", " << r << b << "\n";
            out_ << "  msub " << r << d << ", " << r << "2, " << r << b << ", " << r << a << "\n";
        } else {
            const int b = ssaUse(f, f.operand(i, 1), 1);
            const char *name = i.op == ir::Op::Add ? "add" : i.op == ir::Op::Sub ? "sub" : i.op == ir::Op::Mul ? "mul" :
                               i.op == ir::Op::And ? "and" : i.op == ir::Op::Or ? "orr" : i.op == ir::Op::Xor ? "eor" :
                               i.op == ir::Op::SDiv ? "sdiv" : i.op == ir::Op::UDiv ? "udiv" : i.op == ir::Op::Shl ? "lsl" :
                               i.op == ir::Op::LShr ? "lsr" : "asr";
            out_ << "  " << name << " " << r << d << ", " << r << a << ", " << r << b << "\n";
        }
        // A 32-bit operation left the upper half zero already.
        if (optimize_ <= 0 || rt->size(target_) != 4 || (rt->isSigned(target_) && !ssaLow_[i.result.v])) narrowInt(rt, d, d);
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::SMulH: case ir::Op::UMulH: {
        const bool sign = i.op == ir::Op::SMulH;
        const int a = ssaUse(f, f.operand(i, 0), 0), b = ssaUse(f, f.operand(i, 1), 1);
        if (rt->size(target_) == 8) out_ << (sign ? "  smulh " : "  umulh ") << xd << ", x" << a << ", x" << b << "\n";
        else out_ << (sign ? "  smull " : "  umull ") << xd << ", w" << a << ", w" << b << "\n" << (sign ? "  asr " : "  lsr ") << xd << ", " << xd << ", #32\n";
        narrowInt(rt, d, d);
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::Neg: case ir::Op::Not: {
        const char *r = rt->size(target_) == 8 ? "x" : "w";
        const int a = ssaUse(f, f.operand(i, 0), 0);
        out_ << (i.op == ir::Op::Neg ? "  neg " : "  mvn ") << r << d << ", " << r << a << "\n";
        if (optimize_ <= 0 || rt->size(target_) != 4 || (rt->isSigned(target_) && !ssaLow_[i.result.v])) narrowInt(rt, d, d);
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::ICmp: {
        // Below 32 bits an ordered compare extends both sides its own way first, whatever the operands' type says.
        static const char *const cond[] = {"eq", "ne", "lt", "le", "gt", "ge", "lo", "ls", "hi", "hs"};
        static const int opposite[] = {1, 0, 5, 4, 3, 2, 9, 8, 7, 6};
        if (static_cast<int>(i.cc) > static_cast<int>(ir::Cmp::Uge)) { unsupported("this integer compare from SSA"); return; }
        const int size = typeOf(f, f.operand(i, 0))->size(target_);
        const bool sign = i.cc >= ir::Cmp::Slt && i.cc <= ir::Cmp::Sge, ordered = i.cc >= ir::Cmp::Slt;
        const bool imm = (ssaImm_[f.operand(i, 1).v] & 1) != 0 && size >= 4;
        int a = ssaUse(f, f.operand(i, 0), 0), b = imm ? 0 : ssaUse(f, f.operand(i, 1), 1);
        if (size < 4 && ordered) {
            const std::string ext = std::string(sign ? "  sxt" : "  uxt") + (size == 1 ? "b" : "h");
            out_ << ext << " w0, w" << a << "\n" << ext << " w1, w" << b << "\n";
            a = 0, b = 1;
        }
        const char *r = size == 8 ? "x" : "w";
        out_ << "  cmp " << r << a << ", ";
        if (imm) out_ << "#" << ssaImmK_[f.operand(i, 1).v] << "\n"; else out_ << r << b << "\n";
        if (ssaFlags_[i.result.v]) { ssaCond_ = cond[static_cast<int>(i.cc)]; ssaNot_ = cond[opposite[static_cast<int>(i.cc)]]; return; }
        out_ << "  cset " << xd << ", " << cond[static_cast<int>(i.cc)] << "\n";
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::FAdd: case ir::Op::FSub: case ir::Op::FMul: case ir::Op::FDiv: {
        const int a = ssaUse(f, f.operand(i, 0), 0), b = ssaUse(f, f.operand(i, 1), 1);
        const char *name = i.op == ir::Op::FAdd ? "fadd" : i.op == ir::Op::FSub ? "fsub" : i.op == ir::Op::FMul ? "fmul" : "fdiv";
        out_ << "  " << name << " " << a64Fp(rt, d) << ", " << a64Fp(rt, a) << ", " << a64Fp(rt, b) << "\n";
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::FNeg: {
        const int a = ssaUse(f, f.operand(i, 0), 0);
        out_ << "  fneg " << a64Fp(rt, d) << ", " << a64Fp(rt, a) << "\n";
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::FCmp: {
        // The ordered conditions as the flags give them: `mi` and `ls` are false on an unordered pair, `ne` true.
        const Type *t = typeOf(f, f.operand(i, 0));
        const char *cond = i.cc == ir::Cmp::Oeq ? "eq" : i.cc == ir::Cmp::Une ? "ne" : i.cc == ir::Cmp::Olt ? "mi" :
                           i.cc == ir::Cmp::Ole ? "ls" : i.cc == ir::Cmp::Ogt ? "gt" : i.cc == ir::Cmp::Oge ? "ge" : nullptr;
        if (cond == nullptr) { unsupported("this floating compare from SSA"); return; }
        const int a = ssaUse(f, f.operand(i, 0), 0), b = ssaUse(f, f.operand(i, 1), 1);
        out_ << "  fcmp " << a64Fp(t, a) << ", " << a64Fp(t, b) << "\n";
        // Each condition's opposite is its flags' complement, an unordered pair included.
        const char *other = i.cc == ir::Cmp::Oeq ? "ne" : i.cc == ir::Cmp::Une ? "eq" : i.cc == ir::Cmp::Olt ? "pl" :
                            i.cc == ir::Cmp::Ole ? "hi" : i.cc == ir::Cmp::Ogt ? "le" : "lt";
        if (ssaFlags_[i.result.v]) { ssaCond_ = cond; ssaNot_ = other; return; }
        out_ << "  cset " << xd << ", " << cond << "\n";
        ssaKeep(f, i.result);
        return;
    }
    case ir::Op::Trunc: case ir::Op::SExt: case ir::Op::ZExt: case ir::Op::FpToSi: case ir::Op::FpToUi: case ir::Op::SiToFp: case ir::Op::UiToFp:
    case ir::Op::FpExt: case ir::Op::FpTrunc: case ir::Op::Bitcast: case ir::Op::PtrToInt: case ir::Op::IntToPtr:
    {
        // The walker's conversion between the two types, as x86's selector takes it; between integers it is narrowInt alone.
        const Type *from = typeOf(f, f.operand(i, 0));
        if (a64Moves(rt, from) || (!from->isFloating() && !rt->isFloating())) {
            const int a = ssaUse(f, f.operand(i, 0), 0);
            if (!a64Moves(rt, from)) narrowInt(rt, d, a);
            else if (d != a) out_ << "  mov " << xd << ", x" << a << "\n";
            ssaKeep(f, i.result);
            return;
        }
        ssaLoad(f, f.operand(i, 0), 0);
        genConversion(from, rt);
        ssaStore(f, i.result);
        return;
    }
    case ir::Op::Br:
        ssaBranchArgs(f, i.succ[0]);
        ssaJump(i.succ[0].block);
        return;
    case ir::Op::CondBr: {
        // A compare fused into the branch left its condition and the opposite; another condition is a value.
        std::string yes = std::string("  b.") + (ssaCond_ != nullptr ? ssaCond_ : "") + " ", no = std::string("  b.") + (ssaNot_ != nullptr ? ssaNot_ : "") + " ";
        if (ssaCond_ == nullptr) {
            const std::string c = "x" + std::to_string(ssaUse(f, f.operand(i, 0), 0)) + ", ";
            yes = "  cbnz " + c, no = "  cbz " + c;
        }
        ssaCond_ = ssaNot_ = nullptr;
        // An edge that moves nothing is branched to directly; the true edge being the next block, the false one is taken.
        const bool moves = optimize_ > 0 ? ssaBranchArgs(f, i.succ[0], true) : i.succ[0].count != 0;
        if (!moves && i.succ[0].block.v == ssaNext_ && !ssaBranchArgs(f, i.succ[1], true)) {
            out_ << no << ssaBlockLabel(i.succ[1].block) << "\n";
            return;
        }
        if (!moves) {
            out_ << yes << ssaBlockLabel(i.succ[0].block) << "\n";
        } else {
            const std::string other = label("ssaelse", nextLabel());
            out_ << no << other << "\n";
            ssaBranchArgs(f, i.succ[0]);
            out_ << "  b " << ssaBlockLabel(i.succ[0].block) << "\n" << other << ":\n";
        }
        ssaBranchArgs(f, i.succ[1]);
        ssaJump(i.succ[1].block);
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
        ssaJump(i.succ[0].block);
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
    std::vector<const Type *> ptypes;
    for (std::size_t i = 0; i < ps.size(); i++) ptypes.push_back(ps[i].type);
    const Placement plan = placeArguments(ptypes);
    for (std::size_t i = 0; i < ps.size() && i < f.params.size(); i++) {
        // Taken from the incoming register (ssaDirect_), x0 and d0 staging a slot's only after their own parameter is read.
        const Type *pt = ps[i].type->isReference() ? typeOf(f, f.params[i]) : ps[i].type;
        const int r = ssaReg_[f.params[i].v], from = plan.args[i].firstReg;
        if (ssaDirect_ && pt->isFloating()) {
            if (r != 0 || from != 0) out_ << "  fmov " << a64Fp(pt, r) << ", " << a64Fp(pt, from) << "\n";
            if (r == 0) ssaStore(f, f.params[i]);
            continue;
        }
        if (ssaDirect_) {
            narrowInt(pt, r, from);
            if (r == 0) ssaStore(f, f.params[i]);
            continue;
        }
        localIn("x0", ps[i].offset);
        load(ps[i].type->isReference() ? typeOf(f, f.params[i]) : ps[i].type);
        ssaStore(f, f.params[i]);
    }
    if (f.sret.valid() && sretSlot_ != 0) { localIn("x0", sretSlot_); out_ << "  ldr x0, [x0]\n"; ssaStore(f, f.sret); }
    ssaRegionAt_ = 0;
    std::size_t lastPos = 0;
    if (!f.blocks.empty() && f.blocks[0].v != f.entry.v) out_ << "  b " << ssaBlockLabel(f.entry) << "\n";
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const ir::Block &blk = f.block(f.blocks[b]);
        ssaNext_ = optimize_ > 0 && b + 1 < f.blocks.size() && !f.block(f.blocks[b + 1]).pad ? f.blocks[b + 1].v : 0;
        // A pad: the runtime lands with the exception in x0 and the selector in w1, the block's two parameters; a
        // branch from another pad arrives past the stores, at the block's own label, carrying both.
        if (blk.pad) {
            out_ << ssaLandLabel(f.blocks[b]) << ":\n";
            ssaStore(f, blk.params[0]);
            out_ << "  sxtw x0, w1\n";
            ssaStore(f, blk.params[1]);
        }
        out_ << ssaBlockLabel(f.blocks[b]) << ":\n";
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const ir::InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            // -g: the statements and lexical blocks that begin before the instruction (ir::Inst::lines), as x86's selector.
            const ir::Inst &in = f.inst(id);
            for (std::size_t m = 0; m < in.lines.size(); m++) {
                const std::uint32_t block = ir::kBlockOpens | ir::kBlockCloses, at = in.lines[m];
                if (at & block) markBlock(fn.blockAt(at & ~block), (at & ir::kBlockCloses) != 0); else markLine(at);
            }
            if (in.pos != 0 && in.pos != lastPos) { markLine(in.pos); lastPos = in.pos; }
            selectInst(f, in);
        }
    }
    closeOpenBlocks();
    // Pads no invoke names: the prologue named the LSDA already, so an empty row defines it, as the walker's empty try does.
    if (f.landingPads && callSites().empty()) {
        const std::string none = label("ssatry", nextLabel());
        out_ << none << ":\n";
        callSite(none, none, none, std::vector<std::string>(), std::vector<int>(), false, ++ssaRegionAt_, false);
    }
}

// **-g's variables in a selected frame** (M5.2 round 4): the parser's locals, each at the slot of the alloca the lowering
// made for it - matched by name in declaration order, both lists being in it. A parameter without a live alloca keeps
// the slot the walker's prologue filled; a local without one (promoted at -O1 and -O2) has no record.
const std::vector<Local> *Arm64Darwin::ssaLocals(const Function &fn, const ir::Function &f) {
    std::vector<char> live(f.frame.size(), 0);
    for (std::size_t b = 0; b < f.blocks.size(); b++)
        for (std::size_t k = 0; k < f.block(f.blocks[b]).insts.size(); k++) {
            const ir::Inst &i = f.inst(f.block(f.blocks[b]).insts[k]);
            if (i.op == ir::Op::Alloca && i.k >= 0 && static_cast<std::size_t>(i.k) < live.size()) live[i.k] = 1;
        }
    std::map<std::string, std::size_t> next;
    ssaLocals_.push_back(std::vector<Local>());
    for (const Local &l : fn.locals()) {
        std::size_t &j = next[l.name];
        while (l.staticName.empty() && j < f.frame.size() && !a64Declares(f.frame[j].name, l.name)) j++;
        Local d = l;
        const bool slot = l.staticName.empty() && j < f.frame.size() && live[j];
        if (slot) d.offset = static_cast<int>(ssaAllocaSlot_[j]);
        if (l.staticName.empty() && j < f.frame.size()) j++;
        if (!slot && l.staticName.empty() && !l.isParam) {
            if (std::getenv("CXX1_SSA_WHY") != nullptr) std::fprintf(stderr, "a64 -g %s: '%s' has no slot\n", fn.symbol().c_str(), l.name.c_str());
            continue;
        }
        ssaLocals_.back().push_back(d);
    }
    return &ssaLocals_.back();
}
