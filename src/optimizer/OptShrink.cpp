#include "OptPasses.h"

namespace opt {

namespace {

bool gpr(const MOperand &o) { return o.kind == MOperand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }
bool reg64(const MOperand &o) { return gpr(o) && o.reg.width == 8; }
// An array index scaled by an element of one byte.
bool scaleByOne(const MInst &i) {
    return i.m == mn::Imul && i.operands == 2 && i.a.kind == MOperand::Immediate && i.a.numeric && i.a.value == 1 && gpr(i.b);
}

// **The same result in fewer bytes**, given what is read after it: a write of
// four bytes zeroes the upper half, so where nobody reads that half, or the
// value is a small non-negative constant, the REX prefix goes.
bool shorter(MInst &i, RegSet wide, bool flagsLive) {
    if (gpr(i.b) && frameReg(i.b.reg.id)) return false;
    // `cmp $0, %r` is `test %r, %r`, a byte shorter, flags and all: both clear CF and OF and set ZF, SF and PF from %r.
    if (i.m.is({mn::Cmp, mn::Cmpl, mn::Cmpq}) && i.operands == 2 && i.a.kind == MOperand::Immediate && i.a.numeric &&
        i.a.value == 0 && gpr(i.b)) {
        i = MInst{mn::Test, i.b, i.b, 2};
        return true;
    }
    // A power of two is a shift, which is quicker and no longer.
    const long long v = i.a.value;
    if (i.m == mn::Imul && i.operands == 2 && i.a.kind == MOperand::Immediate && i.a.numeric && gpr(i.b) &&
        !flagsLive && v > 1 && v <= (1LL << 30) && (v & (v - 1)) == 0) {
        int n = 0;
        while ((1LL << n) != v) ++n;
        i = MInst{mn::Shl, MOperand::ofImm(n), i.b, 2};
        return true;
    }
    if (i.m.is({mn::Movzbq, mn::Movzwq}) && reg64(i.b)) {
        i.m = i.m == mn::Movzbq ? mn::Movzbl : mn::Movzwl;
        i.b.reg.width = 4;
        return true;
    }
    if (gpr(i.b) && i.b.reg.width == 4 && i.m.is({mn::Mov, mn::Movl}) && i.a.kind == MOperand::Immediate &&
        i.a.numeric && i.a.value == 0 && !flagsLive) {
        i = MInst{mn::Xor, i.b, i.b, 2};
        return true;
    }
    if (!reg64(i.b)) return false;
    const bool upperRead = (wide & bit(i.b.reg.id)) != 0;
    if (isMovQ(i.m) && i.a.kind == MOperand::Immediate && i.a.numeric) {
        const long long v = i.a.value;
        if (v == 0 && !flagsLive) { i = MInst{mn::Xor, MOperand::ofReg(i.b.reg.id, 4), MOperand::ofReg(i.b.reg.id, 4), 2}; return true; }
        if ((v >= 0 && v <= 0x7fffffffLL) || (!upperRead && v == static_cast<int>(v))) { i.m = mn::Mov; i.b.reg.width = 4; return true; }
        return false;
    }
    if (upperRead) return false;
    // Arithmetic whose low half depends on the low halves alone.
    const bool arith = i.m.is({mn::Add, mn::Sub, mn::And, mn::Or, mn::Xor, mn::Imul}) && i.operands == 2 && !flagsLive;
    if (arith && ((i.a.kind == MOperand::Immediate && i.a.numeric && i.a.value == static_cast<int>(i.a.value)) || reg64(i.a))) {
        if (reg64(i.a)) i.a.reg.width = 4;
        i.b.reg.width = 4;
        return true;
    }
    if (i.m == mn::Movslq && i.a.isMem()) { i.m = mn::Movl; i.b.reg.width = 4; return true; }
    if (i.m == mn::Movslq && gpr(i.a) && i.a.reg.id != i.b.reg.id) { i.m = mn::Mov; i.b.reg.width = 4; return true; }
    if (isMovQ(i.m) && i.a.isMem()) { i.m = mn::Movl; i.b.reg.width = 4; return true; }
    return false;
}

}

bool shrink(MStream &s, Flow &f, const Convention &c) {
    f.live(s);
    bool changed = false;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        Live live = blk.out;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            MEntry &en = s[k];
            if (en.kind != MEntry::Inst || en.dead) continue;
            f.joinPads(b, k, live);
            if (scaleByOne(en.ins) && !live.flags) { en.dead = changed = true; continue; }
            if (shorter(en.ins, live.wide, live.flags)) { f.effects[k] = effectsOf(en.ins, c); changed = true; }
            live.step(f.effects[k]);
        }
    }
    return changed;
}

}
