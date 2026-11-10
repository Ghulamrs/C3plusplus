#pragma once

// **The constant evaluator's arithmetic** (sema::Constexpr), on operands already read: the one implementation, which
// the SSA folding (src/ir/opt/SsaFold.cpp) calls too. Integers in 64 bits, wrapping; floating in the operands' type.

namespace sema {

enum class Arith { Add, Sub, Mul, Div, Mod, Shl, Shr, And, Or, Xor, Neg, Not, Eq, Ne, Lt, Le, Gt, Ge };
enum class ArithFault { None, NotArith, DivZero, Shift };

// `uns`: division, remainder, a right shift and an ordering read the operands unsigned; a shift counts 0 to 63.
inline ArithFault integerArith(Arith op, long long l, long long r, bool uns, long long *out) {
    const unsigned long long ul = static_cast<unsigned long long>(l), ur = static_cast<unsigned long long>(r);
    switch (op) {
    case Arith::Add: *out = static_cast<long long>(ul + ur); return ArithFault::None;
    case Arith::Sub: *out = static_cast<long long>(ul - ur); return ArithFault::None;
    case Arith::Mul: *out = static_cast<long long>(ul * ur); return ArithFault::None;
    case Arith::Div:
    case Arith::Mod:
        if (r == 0) return ArithFault::DivZero;
        if (!uns && ul == (1ULL << 63) && r == -1) { *out = op == Arith::Div ? l : 0; return ArithFault::None; }
        if (op == Arith::Div) *out = uns ? static_cast<long long>(ul / ur) : l / r;
        else *out = uns ? static_cast<long long>(ul % ur) : l % r;
        return ArithFault::None;
    case Arith::Shl:
    case Arith::Shr:
        if (r < 0 || r >= 64) return ArithFault::Shift;
        if (op == Arith::Shl) *out = static_cast<long long>(ul << r);
        else *out = uns ? static_cast<long long>(ul >> r) : (l >> r);
        return ArithFault::None;
    case Arith::And: *out = l & r; return ArithFault::None;
    case Arith::Or:  *out = l | r; return ArithFault::None;
    case Arith::Xor: *out = l ^ r; return ArithFault::None;
    case Arith::Neg: *out = static_cast<long long>(0ULL - ul); return ArithFault::None;
    case Arith::Not: *out = ~l; return ArithFault::None;
    case Arith::Eq: *out = l == r; return ArithFault::None;
    case Arith::Ne: *out = l != r; return ArithFault::None;
    case Arith::Lt: *out = uns ? ul <  ur : l <  r; return ArithFault::None;
    case Arith::Le: *out = uns ? ul <= ur : l <= r; return ArithFault::None;
    case Arith::Gt: *out = uns ? ul >  ur : l >  r; return ArithFault::None;
    case Arith::Ge: *out = uns ? ul >= ur : l >= r; return ArithFault::None;
    }
    return ArithFault::NotArith;
}

// In F itself (float or double): false for an operator it does not take, or a division by zero, which is left to run.
template <class F> bool floatingArith(Arith op, F l, F r, F *out) {
    switch (op) {
    case Arith::Add: *out = l + r; return true;
    case Arith::Sub: *out = l - r; return true;
    case Arith::Mul: *out = l * r; return true;
    case Arith::Div: if (r == 0) return false;
                     *out = l / r; return true;
    default: return false;
    }
}

// The six comparisons as C++ answers them: an unordered operand makes every one false but `!=`.
template <class F> bool floatingCompare(Arith op, F l, F r, long long *out) {
    switch (op) {
    case Arith::Eq: *out = l == r; return true;
    case Arith::Ne: *out = l != r; return true;
    case Arith::Lt: *out = l <  r; return true;
    case Arith::Le: *out = l <= r; return true;
    case Arith::Gt: *out = l >  r; return true;
    case Arith::Ge: *out = l >= r; return true;
    default: return false;
    }
}

// A value as a register holds it: sign- or zero-extended to 64 bits from `size` bytes, a bool 0 or 1.
inline long long canonical(long long v, int size, bool sign, bool boolean) {
    if (boolean) return v != 0 ? 1 : 0;
    if (size >= 8) return v;
    const unsigned long long mask = (1ULL << (size * 8)) - 1;
    unsigned long long u = static_cast<unsigned long long>(v) & mask;
    if (sign && (u >> (size * 8 - 1)) != 0) u |= ~mask;
    return static_cast<long long>(u);
}

}   // namespace sema
