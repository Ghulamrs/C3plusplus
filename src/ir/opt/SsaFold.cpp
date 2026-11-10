// **Folding, copy propagation and algebraic simplification** (M3b.4 round 2, design section 4): one worklist over the
// def-use chains. The arithmetic is the constant evaluator's (sema/ConstArith.h), at the width the selector's register
// holds; what would trap, a shift past the width and a NaN are left to run, and a branch on a constant is round 3's.

#include "SsaOpt.h"
#include "../../sema/ConstArith.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace ir {

namespace {

using sema::Arith;

// The selector's register reads a value of up to four bytes as 32 bits, extended by the operator's reading.
long long widenAt(long long k, const Type &t, const Target &target, bool sign) {
    if (t.size(target) == 8) return k;
    const std::uint32_t low = static_cast<std::uint32_t>(k);
    return sign ? static_cast<long long>(static_cast<std::int32_t>(low)) : static_cast<long long>(low);
}

struct Folder {
    Function &f;
    const TypeTable &types;
    const Target &target;
    PassLog &log;
    Dominators dom;
    std::vector<std::uint32_t> blockOf;                         // by value: the block defining it, the entry a parameter
    std::vector<std::uint32_t> instBlock;                       // by instruction
    std::vector<std::vector<InstId> > users;                    // by value: the instructions reading it, terminators too
    std::vector<std::vector<std::pair<InstId, std::uint32_t> > > into;     // by block: (terminator, successor) edges
    std::vector<ValueId> repl;                                  // by value: what it became
    std::vector<char> gone, queued, blockQueued;
    std::vector<InstId> work;
    std::vector<std::uint32_t> blockWork;

    Folder(Function &fn, const TypeTable &t, const Target &tg, PassLog &l) : f(fn), types(t), target(tg), log(l), dom(fn) {}

    const Type &T(TypeId t) const { return *types.type(t); }
    ValueId resolve(ValueId v) const {
        while (v.valid() && v.v < repl.size() && repl[v.v].valid()) v = repl[v.v];
        return v;
    }
    ValueId op(const Inst &i, std::uint32_t n) const { return resolve(f.operand(i, n)); }
    bool integer(TypeId t) const { return t.valid() && T(t).isInteger(); }
    // 1 a float, 2 a double (a long double that is one too); 0 an x87 value or no floating type.
    int fp(TypeId t) const {
        if (!t.valid() || !T(t).isFloating() || T(t).isX87(target)) return 0;
        return T(t).kind() == Kind::Float ? 1 : 2;
    }
    bool intConst(ValueId v, long long &k) const {
        if (!v.valid() || !f.value(v).def.valid() || !integer(f.value(v).type)) return false;
        const Inst &d = f.inst(f.value(v).def);
        if (d.op != Op::IConst) return false;
        k = d.k;
        return true;
    }
    bool fpConst(ValueId v, double &d) const {
        if (!v.valid() || !f.value(v).def.valid() || f.inst(f.value(v).def).op != Op::FConst) return false;
        const unsigned long long bits = f.inst(f.value(v).def).bits;
        const int kind = fp(f.value(v).type);
        if (kind == 1) { float x; const unsigned int b = static_cast<unsigned int>(bits); std::memcpy(&x, &b, 4); d = x; }
        else if (kind == 2) std::memcpy(&d, &bits, 8);
        return kind != 0;
    }
    long long widen(long long k, TypeId t, bool sign) const { return widenAt(k, T(t), target, sign); }

    void pushInst(InstId id) {
        if (id.v >= queued.size() || queued[id.v] || gone[id.v]) return;
        queued[id.v] = 1;
        work.push_back(id);
    }
    void pushBlock(std::uint32_t b) {
        if (b == 0 || b >= blockQueued.size() || blockQueued[b]) return;
        blockQueued[b] = 1;
        blockWork.push_back(b);
    }
    void pushUsers(ValueId v) {
        for (InstId u : users[v.v]) {
            if (!isTerminator(f.inst(u).op)) { pushInst(u); continue; }
            for (const Succ &s : f.inst(u).succ) pushBlock(s.block.v);
        }
    }

    void build() {
        blockOf.assign(f.values.size(), 0);
        instBlock.assign(f.insts.size(), 0);
        users.assign(f.values.size(), std::vector<InstId>());
        into.assign(f.blockArena.size(), std::vector<std::pair<InstId, std::uint32_t> >());
        repl.assign(f.values.size(), ValueId());
        gone.assign(f.insts.size(), 0);
        queued.assign(f.insts.size(), 0);
        blockQueued.assign(f.blockArena.size(), 0);
        for (ValueId p : f.params) blockOf[p.v] = f.entry.v;
        if (f.sret.valid()) blockOf[f.sret.v] = f.entry.v;
        for (BlockId b : f.blocks) {
            const Block &blk = f.block(b);
            for (ValueId p : blk.params) blockOf[p.v] = b.v;
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!id.valid()) continue;
                const Inst &i = f.inst(id);
                instBlock[id.v] = b.v;
                if (i.result.valid()) blockOf[i.result.v] = b.v;
                for (std::uint32_t n = 0; n < i.count; n++) if (f.operand(i, n).valid()) users[f.operand(i, n).v].push_back(id);
                for (std::uint32_t s = 0; s < i.succ.size(); s++) {
                    const Succ &sc = i.succ[s];
                    if (sc.block.v != 0 && sc.block.v < into.size()) into[sc.block.v].push_back(std::make_pair(id, s));
                    for (std::uint32_t a = 0; a < sc.count; a++) if (f.arg(sc, a).valid()) users[f.arg(sc, a).v].push_back(id);
                }
            }
        }
    }

    void toConst(InstId id, long long k, int &count) {
        Inst &i = f.insts[id.v];
        i.op = Op::IConst;
        i.count = 0;
        const Type &t = T(i.type);
        i.k = sema::canonical(k, t.size(target), t.isSigned(target), t.isBool());
        count++;
        pushUsers(i.result);
    }
    void toFConst(InstId id, double d, int &count) {
        Inst &i = f.insts[id.v];
        i.op = Op::FConst;
        i.count = 0;
        if (fp(i.type) == 1) { const float x = static_cast<float>(d); unsigned int b; std::memcpy(&b, &x, 4); i.bits = b; }
        else std::memcpy(&i.bits, &d, 8);
        count++;
        pushUsers(i.result);
    }
    // The result is `v` wherever it is read; the instruction leaves its block, its value given no type (no slot).
    void replace(InstId id, ValueId v, int &count) {
        const ValueId r = f.inst(id).result;
        repl[r.v] = v;
        gone[id.v] = 1;
        users[v.v].insert(users[v.v].end(), users[r.v].begin(), users[r.v].end());
        count++;
        pushUsers(r);
    }
    bool same(ValueId v, TypeId t) const { return f.value(v).type == t; }

    void integerBinary(InstId id) {
        const Inst &i = f.inst(id);
        const ValueId a = op(i, 0), b = op(i, 1);
        if (!integer(i.type)) return;
        long long x = 0, y = 0;
        const bool ca = intConst(a, x), cb = intConst(b, y);
        if (ca && cb) {
            Arith ar = Arith::Add;
            bool uns = false;
            switch (i.op) {
            case Op::Add: ar = Arith::Add; break;
            case Op::Sub: ar = Arith::Sub; break;
            case Op::Mul: ar = Arith::Mul; break;
            case Op::And: ar = Arith::And; break;
            case Op::Or:  ar = Arith::Or; break;
            case Op::Xor: ar = Arith::Xor; break;
            case Op::Shl: ar = Arith::Shl; break;
            case Op::SDiv: ar = Arith::Div; break;
            case Op::UDiv: ar = Arith::Div; uns = true; break;
            case Op::SRem: ar = Arith::Mod; break;
            case Op::URem: ar = Arith::Mod; uns = true; break;
            case Op::LShr: ar = Arith::Shr; uns = true; break;
            case Op::AShr: ar = Arith::Shr; break;
            default: return;
            }
            const bool wide = T(i.type).size(target) == 8;
            const long long l = widen(x, i.type, !uns);
            long long r = widen(y, i.type, !uns);
            if (ar == Arith::Shl || ar == Arith::Shr) {
                if (y < 0 || y >= 8 * T(i.type).size(target)) { log.kept++; return; }
                r = y;
            }
            // A division the hardware traps on: by zero, and the least value by -1 at the register's width.
            const long long least = wide ? static_cast<long long>(1ULL << 63) : -2147483647LL - 1;
            if ((ar == Arith::Div || ar == Arith::Mod) && (r == 0 || (!uns && l == least && r == -1))) { log.kept++; return; }
            long long out = 0;
            if (sema::integerArith(ar, l, r, uns, &out) != sema::ArithFault::None) { log.kept++; return; }
            toConst(id, out, log.constants);
            return;
        }
        const bool sameAB = a.v == b.v;
        const ValueId keep = cb ? a : b;          // the other operand of a constant one
        const long long c = cb ? y : x;
        switch (i.op) {
        case Op::Add: case Op::Or: case Op::Xor:
            if ((ca || cb) && c == 0 && same(keep, i.type)) { replace(id, keep, log.identities); return; }
            break;
        case Op::Sub: case Op::Shl: case Op::LShr: case Op::AShr:
            if (cb && y == 0 && same(a, i.type)) { replace(id, a, log.identities); return; }
            break;
        case Op::Mul:
            if ((ca || cb) && c == 0) { toConst(id, 0, log.identities); return; }
            if ((ca || cb) && c == 1 && same(keep, i.type)) { replace(id, keep, log.identities); return; }
            break;
        default: break;
        }
        if (!sameAB) return;
        if ((i.op == Op::Sub || i.op == Op::Xor)) toConst(id, 0, log.identities);
        else if ((i.op == Op::And || i.op == Op::Or) && same(a, i.type)) replace(id, a, log.identities);
    }

    // `-` and `~`: a constant folded, the operator twice the operand itself.
    void integerUnary(InstId id) {
        const Inst &i = f.inst(id);
        const ValueId a = op(i, 0);
        long long x = 0;
        if (integer(i.type) && intConst(a, x)) {
            long long out = 0;
            sema::integerArith(i.op == Op::Neg ? Arith::Neg : Arith::Not, x, 0, false, &out);
            toConst(id, out, log.constants);
            return;
        }
        twice(id, a);
    }
    void twice(InstId id, ValueId a) {
        const Inst &i = f.inst(id);
        if (!f.value(a).def.valid() || gone[f.value(a).def.v]) return;
        const Inst &d = f.inst(f.value(a).def);
        if (d.op != i.op || d.count != 1) return;
        const ValueId inner = op(d, 0);
        if (same(inner, i.type)) replace(id, inner, log.identities);
    }

    void compare(InstId id) {
        const Inst &i = f.inst(id);
        const ValueId a = op(i, 0), b = op(i, 1);
        long long x = 0, y = 0, out = 0;
        if (a.v == b.v) {
            // An operand compared with itself answers as 0 with 0 does.
            if (foldCompare(i.cc, 0, 0, T(f.value(a).type), target, out)) toConst(id, out, log.identities);
            return;
        }
        if (intConst(a, x) && intConst(b, y) && foldCompare(i.cc, x, y, T(f.value(a).type), target, out))
            toConst(id, out, log.compares);
    }

    void floating(InstId id) {
        const Inst &i = f.inst(id);
        const int kind = fp(i.type);
        if (kind == 0) return;
        double x = 0, y = 0;
        if (i.op == Op::FNeg) {
            if (!fpConst(op(i, 0), x)) { twice(id, op(i, 0)); return; }
            if (x != x) { log.kept++; return; }
            toFConst(id, -x, log.floating);
            return;
        }
        if (!fpConst(op(i, 0), x) || !fpConst(op(i, 1), y)) return;
        const Arith ar = i.op == Op::FAdd ? Arith::Add : i.op == Op::FSub ? Arith::Sub : i.op == Op::FMul ? Arith::Mul : Arith::Div;
        double out = 0;
        bool ok;
        if (kind == 1) { float v = 0; ok = sema::floatingArith(ar, static_cast<float>(x), static_cast<float>(y), &v); out = v; }
        else ok = sema::floatingArith(ar, x, y, &out);
        // A NaN in or out is the target's to make: its sign and payload are not the host's to choose.
        if (!ok || x != x || y != y || out != out) { log.kept++; return; }
        toFConst(id, out, log.floating);
    }

    void fcompare(InstId id) {
        const Inst &i = f.inst(id);
        double x = 0, y = 0;
        if (!fpConst(op(i, 0), x) || !fpConst(op(i, 1), y)) return;
        Arith ar = Arith::Eq;
        switch (i.cc) {
        case Cmp::Oeq: ar = Arith::Eq; break;
        case Cmp::Une: ar = Arith::Ne; break;
        case Cmp::Olt: ar = Arith::Lt; break;
        case Cmp::Ole: ar = Arith::Le; break;
        case Cmp::Ogt: ar = Arith::Gt; break;
        case Cmp::Oge: ar = Arith::Ge; break;
        default: return;
        }
        long long out = 0;
        sema::floatingCompare(ar, x, y, &out);
        toConst(id, out, log.compares);
    }

    // The selector converts by the two types, whatever the operator: one type to itself is a copy.
    void convert(InstId id) {
        const Inst &i = f.inst(id);
        const ValueId a = op(i, 0);
        const TypeId from = f.value(a).type, to = i.type;
        if (from == to) { replace(id, a, log.copies); return; }
        long long x = 0;
        double d = 0;
        if (integer(from) && integer(to) && intConst(a, x)) {
            long long v = 0;
            if (foldIntConversion(x, T(to), target, v)) toConst(id, v, log.casts);
            else log.kept++;
            return;
        }
        if (integer(from) && fp(to) != 0 && intConst(a, x)) {
            const bool u64 = T(from).size(target) == 8 && !T(from).isSigned(target);
            const unsigned long long ux = static_cast<unsigned long long>(x);
            if (fp(to) == 1) toFConst(id, u64 ? static_cast<float>(ux) : static_cast<float>(x), log.casts);
            else toFConst(id, u64 ? static_cast<double>(ux) : static_cast<double>(x), log.casts);
            return;
        }
        if (fp(from) != 0 && fp(to) != 0 && fpConst(a, d)) {
            if (d != d) { log.kept++; return; }
            toFConst(id, fp(to) == 1 ? static_cast<double>(static_cast<float>(d)) : d, log.casts);
            return;
        }
        if (fp(from) != 0 && integer(to) && !T(to).isBool() && fpConst(a, d)) {
            // Only a value the destination holds after truncation: outside it, the conversion is the target's.
            const int bits = 8 * T(to).size(target);
            const bool sign = T(to).isSigned(target);
            const double lo = sign ? -std::ldexp(1.0, bits - 1) : 0.0, hi = std::ldexp(1.0, sign ? bits - 1 : bits);
            if (!(d > lo - 1.0 && d < hi)) { log.kept++; return; }
            if (!sign && d >= std::ldexp(1.0, 63)) toConst(id, static_cast<long long>(static_cast<unsigned long long>(d)), log.casts);
            else toConst(id, static_cast<long long>(d), log.casts);
        }
    }

    void simplify(InstId id) {
        if (gone[id.v] || !dom.reachable(instBlock[id.v])) return;
        const Inst &i = f.inst(id);
        if (i.isVolatile || !i.result.valid() || !i.type.valid()) return;
        switch (i.op) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::SDiv: case Op::UDiv: case Op::SRem: case Op::URem:
        case Op::And: case Op::Or: case Op::Xor: case Op::Shl: case Op::LShr: case Op::AShr:
            integerBinary(id); return;
        case Op::Neg: case Op::Not: integerUnary(id); return;
        case Op::ICmp: compare(id); return;
        case Op::FAdd: case Op::FSub: case Op::FMul: case Op::FDiv: case Op::FNeg: floating(id); return;
        case Op::FCmp: fcompare(id); return;
        case Op::Trunc: case Op::SExt: case Op::ZExt: case Op::FpToSi: case Op::FpToUi: case Op::SiToFp: case Op::UiToFp:
        case Op::FpExt: case Op::FpTrunc: case Op::Bitcast:
            convert(id); return;
        default: return;
        }
    }

    // `v` is available on entry to block b: a definition in a block that strictly dominates it.
    bool available(ValueId v, std::uint32_t b) const {
        const std::uint32_t db = blockOf[v.v];
        return db != 0 && db != b && dom.reachable(db) && dom.dominates(db, b);
    }

    // Each parameter of b that every edge passes one value for, or itself: that value, or a constant made at b's head.
    void parameters(std::uint32_t b) {
        if (b == f.entry.v || !dom.reachable(b) || f.block(BlockId(b)).pad || into[b].empty()) return;
        const std::size_t n = f.block(BlockId(b)).params.size();
        for (const std::pair<InstId, std::uint32_t> &e : into[b]) {
            const Inst &t = f.inst(e.first);
            if ((t.op == Op::Invoke && e.second == 1) || t.succ[e.second].count != n) return;
        }
        for (std::size_t p = 0; p < n; p++) {
            const ValueId param = f.block(BlockId(b)).params[p];
            if (repl[param.v].valid()) continue;
            ValueId one;
            bool single = true, constant = true;
            long long k = 0;
            for (const std::pair<InstId, std::uint32_t> &e : into[b]) {
                const ValueId v = resolve(f.arg(f.inst(e.first).succ[e.second], static_cast<std::uint32_t>(p)));
                if (v.v == param.v) continue;
                long long kv = 0;
                if (!intConst(v, kv) || (one.valid() && kv != k)) constant = false;
                if (!one.valid()) { one = v; k = kv; }
                else if (v.v != one.v) single = false;
            }
            if (!one.valid()) continue;
            if (single && available(one, b)) { repl[param.v] = one; }
            else if (constant) repl[param.v] = constantAt(b, f.value(param).type, k);
            else continue;
            users[repl[param.v].v].insert(users[repl[param.v].v].end(), users[param.v].begin(), users[param.v].end());
            log.arguments++;
            pushUsers(param);
        }
    }

    ValueId constantAt(std::uint32_t b, TypeId type, long long k) {
        Inst i;
        i.op = Op::IConst;
        i.type = type;
        i.k = k;
        i.first = static_cast<std::uint32_t>(f.operands.size());
        const InstId id(static_cast<std::uint32_t>(f.insts.size()));
        Value v;
        v.type = type;
        v.def = id;
        i.result = ValueId(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(v);
        f.insts.push_back(i);
        std::vector<InstId> &list = f.blockArena[b].insts;
        list.insert(list.begin(), id);
        blockOf.push_back(b);
        instBlock.push_back(b);
        users.push_back(std::vector<InstId>());
        repl.push_back(ValueId());
        gone.push_back(0);
        queued.push_back(0);
        return i.result;
    }

    void run() {
        for (std::uint32_t b : dom.rpo) {
            for (InstId id : f.block(BlockId(b)).insts) pushInst(id);
            if (!f.block(BlockId(b)).params.empty()) pushBlock(b);
        }
        std::size_t wi = 0, bi = 0;
        while (wi < work.size() || bi < blockWork.size()) {
            if (wi < work.size()) { const InstId id = work[wi++]; queued[id.v] = 0; simplify(id); continue; }
            const std::uint32_t b = blockWork[bi++];
            blockQueued[b] = 0;
            parameters(b);
        }
    }

    // Every use re-pointed; the replaced instructions out of their blocks, the replaced parameters out of their blocks
    // and of every edge's arguments, each such value given no type.
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
        for (BlockId b : f.blocks) {
            Block &blk = f.blockArena[b.v];
            std::vector<char> drop(blk.params.size(), 0);
            bool any = false;
            for (std::size_t p = 0; p < blk.params.size(); p++) if (repl[blk.params[p].v].valid()) { drop[p] = 1; any = true; }
            if (!any) continue;
            for (const std::pair<InstId, std::uint32_t> &e : into[b.v]) {
                Succ &sc = f.insts[e.first.v].succ[e.second];
                if (sc.count != blk.params.size()) continue;     // an invoke's unwind edge: the runtime's parameters
                const std::uint32_t first = static_cast<std::uint32_t>(f.operands.size());
                for (std::uint32_t a = 0; a < sc.count; a++) if (!drop[a]) { const ValueId v = f.operands[sc.first + a]; f.operands.push_back(v); }
                sc.first = first;
                sc.count = static_cast<std::uint32_t>(f.operands.size()) - first;
            }
            std::vector<ValueId> params;
            for (std::size_t p = 0; p < blk.params.size(); p++) {
                if (drop[p]) { f.values[blk.params[p].v].type = TypeId(); continue; }
                f.values[blk.params[p].v].index = static_cast<std::uint16_t>(params.size());
                params.push_back(blk.params[p]);
            }
            blk.params.swap(params);
        }
    }
};

}   // namespace

bool foldCompare(Cmp cc, long long x, long long y, const Type &t, const Target &target, long long &out) {
    Arith ar = Arith::Eq;
    bool uns = false;
    switch (cc) {
    case Cmp::Eq: ar = Arith::Eq; uns = true; break;
    case Cmp::Ne: ar = Arith::Ne; uns = true; break;
    case Cmp::Slt: ar = Arith::Lt; break;
    case Cmp::Sle: ar = Arith::Le; break;
    case Cmp::Sgt: ar = Arith::Gt; break;
    case Cmp::Sge: ar = Arith::Ge; break;
    case Cmp::Ult: ar = Arith::Lt; uns = true; break;
    case Cmp::Ule: ar = Arith::Le; uns = true; break;
    case Cmp::Ugt: ar = Arith::Gt; uns = true; break;
    case Cmp::Uge: ar = Arith::Ge; uns = true; break;
    default: return false;
    }
    sema::integerArith(ar, widenAt(x, t, target, !uns), widenAt(y, t, target, !uns), uns, &out);
    return true;
}

bool foldIntConversion(long long x, const Type &to, const Target &target, long long &out) {
    out = sema::canonical(x, to.size(target), to.isSigned(target), false);
    return !(to.isBool() && out != 0 && out != 1);      // the selector's movzbq, not a test
}

void fold(Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    if (!f.entry.valid()) return;
    Folder p(f, types, target, log);
    p.build();
    p.run();
    p.finish();
}

}   // namespace ir
