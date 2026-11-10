#pragma once

// Hacker's Delight's multipliers for division by a constant of `bits` (32 or 64), 2 <= d < 2^(bits-1), shared
// by the x86 pass (OptDivide.cpp), the C6000 backend (Tms6747.cpp) and the SSA pass (SsaDivide.cpp).
namespace opt {
void signedMagic(unsigned long long d, unsigned long long &M, int &s, int bits = 32);
bool unsignedMagic(unsigned long long d, unsigned long long &M, int &s, int bits = 32);
void unsignedMagicAdd(unsigned d, unsigned long long &M, int &s, bool &add);
}
