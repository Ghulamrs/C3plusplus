#include "DivMagic.h"

namespace opt {

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
