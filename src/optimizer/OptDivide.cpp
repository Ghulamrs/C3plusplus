#include "OptPasses.h"
#include "DivMagic.h"

#include <algorithm>

namespace opt {

namespace {

bool gpr(const MOperand &o) { return o.kind == MOperand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }

MInst two(mn::Mn m, MOperand a, MOperand b) { return MInst{m, a, b, 2}; }
MOperand r32(int id) { return MOperand::ofReg(id, 4); }
MOperand r64(int id) { return MOperand::ofReg(id, 8); }

}

// **`mov $d, %R; cdq; idiv %R` (or `xor %edx, %edx; div %R`) becomes a
// multiply by the divisor's magic number**, eax and edx left as the divide
// leaves them; R, dead after, carries the product.
bool divideByConstant(MStream &s, Flow &f) {
    f.live(s);
    struct Site { int at; std::vector<MInst> seq; };
    std::vector<Site> sites;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        Live live = blk.out;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            if (s[k].kind != MEntry::Inst || s[k].dead) continue;
            f.joinPads(b, k, live);
            const Live after = live;
            live.step(f.effects[k]);
            const MInst &dv = s[k].ins;
            if (!dv.m.is({mn::Idiv, mn::Div}) || dv.operands != 1 || !gpr(dv.a) || dv.a.reg.width != 4) continue;
            const int R = dv.a.reg.id;
            if (R == RAX || R == RDX || frameReg(R) || (after.regs & bit(R))) continue;
            int k1 = k - 1, k0;
            while (k1 >= blk.begin && s[k1].kind == MEntry::Inst && s[k1].dead) --k1;
            k0 = k1 - 1;
            while (k0 >= blk.begin && s[k0].kind == MEntry::Inst && s[k0].dead) --k0;
            if (k0 < blk.begin || s[k1].kind != MEntry::Inst || s[k0].kind != MEntry::Inst) continue;
            const MInst &ext = s[k1].ins, &mv = s[k0].ins;
            const bool sign = dv.m == mn::Idiv;
            if (sign ? ext.m != mn::Cdq : !(ext.m == mn::Xor && ext.a.isReg(RDX) && ext.b.isReg(RDX) && ext.a.reg.width == 4)) continue;
            if (!isMovQL(mv.m) || mv.a.kind != MOperand::Immediate || !mv.a.numeric || !gpr(mv.b) || mv.b.reg.id != R ||
                mv.b.reg.width < 4)
                continue;
            const long long d = mv.b.reg.width == 4 ? static_cast<unsigned>(mv.a.value) : mv.a.value;
            if (d < 2 || d >= (1LL << 31)) continue;
            unsigned long long M;
            int sh;
            if (sign) signedMagic(static_cast<unsigned>(d), M, sh);
            else if (!unsignedMagic(static_cast<unsigned>(d), M, sh)) continue;
            Site site{k0, {}};
            std::vector<MInst> &q = site.seq;
            q.push_back(two(sign ? mn::Movslq : mn::Mov, r32(RAX), sign ? r64(RDX) : r32(RDX)));
            q.push_back(two(mn::Mov, MOperand::ofImm(static_cast<long long>(M)), r32(R)));
            q.push_back(two(mn::Imul, r64(RDX), r64(R)));
            q.push_back(two(sign ? mn::Sar : mn::Shr, MOperand::ofImm(32 + sh), r64(R)));
            if (sign) {
                q.push_back(two(mn::Mov, r64(RDX), r64(RAX)));
                q.push_back(two(mn::Shr, MOperand::ofImm(63), r64(RAX)));
                q.push_back(two(mn::Add, r32(R), r32(RAX)));
                q.push_back(two(mn::Mov, r32(RAX), r32(R)));
            } else {
                q.push_back(two(mn::Mov, r32(R), r32(RAX)));
            }
            q.push_back(two(mn::Imul, MOperand::ofImm(d), r32(R)));
            q.push_back(two(mn::Sub, r32(R), r32(RDX)));
            s[k0].dead = s[k1].dead = s[k].dead = true;
            sites.push_back(site);
        }
    }
    if (sites.empty()) return false;
    std::stable_sort(sites.begin(), sites.end(), [](const Site &x, const Site &y) { return x.at > y.at; });
    for (const Site &site : sites) {
        std::vector<MEntry> es;
        for (const MInst &i : site.seq) { MEntry e; e.ins = i; es.push_back(e); }
        s.insert(s.begin() + site.at, es.begin(), es.end());
    }
    return true;
}

// **Hacker's Delight's magic number for a signed divisor** d >= 2: q = (n * M) >> (bits + s), M taken unsigned in a
// product twice as wide, plus one where n is negative. At 64 bits q1 and q2 wrap as the book's own words do.
void signedMagic(unsigned long long d, unsigned long long &M, int &s, int bits) {
    const unsigned long long two = 1ULL << (bits - 1);
    const unsigned long long anc = two - 1 - two % d;
    int p = bits - 1;
    unsigned long long q1 = two / anc, r1 = two - q1 * anc;
    unsigned long long q2 = two / d, r2 = two - q2 * d;
    unsigned long long delta;
    do {
        ++p;
        q1 *= 2; r1 *= 2;
        if (r1 >= anc) { ++q1; r1 -= anc; }
        q2 *= 2; r2 *= 2;
        if (r2 >= d) { ++q2; r2 -= d; }
        delta = d - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));
    M = bits == 64 ? q2 + 1 : (q2 + 1) & ((1ULL << bits) - 1);
    s = p - bits;
}

// An unsigned divisor's multiplier where one below 2^bits is exact: M = ceil(2^(bits+s) / d) with
// M*d - 2^(bits+s) < 2^s; false where none is. q and r are 2^(bits+s)'s quotient and remainder by d.
bool unsignedMagic(unsigned long long d, unsigned long long &M, int &s, int bits) {
    const unsigned long long top = 1ULL << (bits - 1), all = bits == 64 ? ~0ULL : (1ULL << bits) - 1;
    unsigned long long q = top / d, r = top % d;
    for (s = 0; s < bits; ++s) {
        if (q >= top) return false;
        q *= 2; r *= 2;
        if (r >= d) { ++q; r -= d; }
        if (r != 0 && q == all) return false;
        M = q + (r != 0 ? 1 : 0);
        if ((r != 0 ? d - r : 0) < (1ULL << s)) return true;
    }
    return false;
}

// The same with the 33-bit multipliers kept, for 2 <= d < 2^31: M below 2^33,
// `add` set where it is 2^32 or more, q then ((n - hi) >> 1) + hi >> (s - 1).
void unsignedMagicAdd(unsigned d, unsigned long long &M, int &s, bool &add) {
    for (s = 0; s < 32; ++s) {
        const unsigned long long p = 1ULL << (32 + s);
        M = (p + d - 1) / d;
        if (M * d - p <= (1ULL << s)) break;
    }
    add = M >= (1ULL << 32);
    M &= 0xffffffffULL;
}

}
