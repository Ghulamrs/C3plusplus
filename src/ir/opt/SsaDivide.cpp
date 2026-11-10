// **Division by a constant** (M3b.4 round 5, design section 4): sdiv, udiv, srem and urem of a 4- or 8-byte integer by
// a constant 2 <= |d| < 2^(bits-1) become shifts for a power of two, else a multiply-high by DivMagic.h's number, the
// one OptDivide.cpp and the C6000 take; at -O2 only, as OptDivide. An unsigned divisor with no exact multiplier is left.

#include "SsaOpt.h"
#include "../../optimizer/DivMagic.h"
#include "../../sema/ConstArith.h"

namespace ir {

namespace {

struct Divider {
    Function &f;
    const TypeTable &types;
    const Target &target;
    PassLog &log;
    std::vector<InstId> list;       // the block's instructions as rewritten
    InstId site;                    // the division rewritten: each new instruction takes its type and position
    TypeId type;

    Divider(Function &fn, const TypeTable &t, const Target &tg, PassLog &l) : f(fn), types(t), target(tg), log(l) {}

    void operands(Inst &i, ValueId a, ValueId b) {
        i.first = static_cast<std::uint32_t>(f.operands.size());
        i.count = 0;
        if (a.valid()) { f.operands.push_back(a); i.count++; }
        if (b.valid()) { f.operands.push_back(b); i.count++; }
    }
    // A new instruction before the division.
    ValueId make(Op op, ValueId a, ValueId b, long long k = 0) {
        Inst i;
        i.op = op;
        i.type = type;
        i.k = k;
        operands(i, a, b);
        i.at = f.inst(site).at;
        i.pos = f.inst(site).pos;
        const InstId id(static_cast<std::uint32_t>(f.insts.size()));
        Value v;
        v.type = type;
        v.def = id;
        i.result = ValueId(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(v);
        f.insts.push_back(i);
        list.push_back(id);
        return i.result;
    }
    ValueId konst(long long k) {
        const Type &t = *types.type(type);
        return make(Op::IConst, ValueId(), ValueId(), sema::canonical(k, t.size(target), t.isSigned(target), false));
    }
    ValueId by(Op op, ValueId a, long long k) { return make(op, a, konst(k)); }
    // The division itself is the last step, so its value is read where it was.
    void last(Op op, ValueId a, ValueId b) {
        Inst &i = f.insts[site.v];
        i.op = op;
        i.k = 0;
        operands(i, a, b);
        list.push_back(site);
    }

    bool rewrite(InstId id) {
        const Inst &i = f.inst(id);
        const bool sign = i.op == Op::SDiv || i.op == Op::SRem, rem = i.op == Op::SRem || i.op == Op::URem;
        if (!(sign || i.op == Op::UDiv || i.op == Op::URem) || i.isVolatile || !i.type.valid()) return false;
        const Type &t = *types.type(i.type);
        const ValueId n = f.operand(i, 0), dv = f.operand(i, 1);
        const int size = t.isInteger() && !t.isBool() ? t.size(target) : 0;
        if ((size != 4 && size != 8) || f.value(n).type != i.type) return false;
        if (!f.value(dv).def.valid() || f.inst(f.value(dv).def).op != Op::IConst) return false;
        const int bits = 8 * size;
        const long long raw = f.inst(f.value(dv).def).k;
        const long long sd = bits == 32 ? static_cast<long long>(static_cast<std::int32_t>(raw)) : raw;
        const bool negative = sign && sd < 0;
        unsigned long long d = bits == 32 && !sign ? static_cast<std::uint32_t>(raw) : static_cast<unsigned long long>(sd);
        if (negative) d = 0 - d;
        unsigned long long M = 0;
        int s = 0;
        const bool power = (d & (d - 1)) == 0;
        if (d < 2 || d >= (1ULL << (bits - 1)) || (!sign && !power && !opt::unsignedMagic(d, M, s, bits))) {
            log.divLeft++;
            return false;
        }
        site = id;
        type = i.type;
        int k = 0;
        while ((1ULL << k) < d) k++;
        if (power && !sign) {
            if (rem) last(Op::And, n, konst(static_cast<long long>(d - 1)));
            else last(Op::LShr, n, konst(k));
            log.divShifts++;
        } else if (power) {
            // n rounded toward zero: 2^k - 1 added where n is negative, then shifted, or its low bits cleared.
            const ValueId bias = k == 1 ? by(Op::LShr, n, bits - 1) : by(Op::LShr, by(Op::AShr, n, bits - 1), bits - k);
            const ValueId sum = make(Op::Add, n, bias);
            if (rem) last(Op::Sub, n, by(Op::And, sum, -static_cast<long long>(d)));
            else if (negative) last(Op::Neg, by(Op::AShr, sum, k), ValueId());
            else last(Op::AShr, sum, konst(k));
            log.divShifts++;
        } else if (sign) {
            // Hacker's Delight 10-1: the high half, n added where M reads negative, shifted, plus one where n is negative.
            opt::signedMagic(d, M, s, bits);
            ValueId q = make(Op::SMulH, n, konst(static_cast<long long>(M)));
            if ((M >> (bits - 1)) & 1) q = make(Op::Add, q, n);
            if (s > 0) q = by(Op::AShr, q, s);
            const ValueId up = by(Op::LShr, n, bits - 1);
            if (rem) last(Op::Sub, n, make(Op::Mul, make(Op::Add, q, up), konst(static_cast<long long>(d))));
            else if (negative) last(Op::Neg, make(Op::Add, q, up), ValueId());
            else last(Op::Add, q, up);
            log.divSigned++;
        } else {
            if (!rem && s == 0) last(Op::UMulH, n, konst(static_cast<long long>(M)));
            else {
                ValueId q = make(Op::UMulH, n, konst(static_cast<long long>(M)));
                if (rem) {
                    if (s > 0) q = by(Op::LShr, q, s);
                    last(Op::Sub, n, make(Op::Mul, q, konst(static_cast<long long>(d))));
                } else last(Op::LShr, q, konst(s));
            }
            log.divUnsigned++;
        }
        if (bits == 64) log.divWide++;
        return true;
    }
};

}   // namespace

bool divide(Function &f, const TypeTable &types, const Target &target, PassLog &log) {
    if (!f.entry.valid()) return false;
    Divider p(f, types, target, log);
    bool changed = false;
    for (BlockId b : f.blocks) {
        if (f.block(b).pad) continue;
        const std::vector<InstId> old = f.block(b).insts;
        p.list.clear();
        bool any = false;
        for (InstId id : old) {
            if (p.rewrite(id)) any = true;
            else p.list.push_back(id);
        }
        if (any) { f.blockArena[b.v].insts = p.list; changed = true; }
    }
    return changed;
}

}   // namespace ir
