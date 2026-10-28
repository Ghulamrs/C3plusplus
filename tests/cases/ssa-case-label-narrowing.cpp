// A case label's cast narrows, as [conv.integral]/3 says: the lowering folded `(char)258` as 258, so a switch took
// its default where it should not.
#include <cstdio>

int pick(int k) {
    switch (k) {
    case (char)257: return 1;
    case (char)258: return 3;
    case (unsigned char)-1: return 5;
    case (short)65539: return 7;
    case (signed char)200: return 8;
    default: return 9;
    }
}

int wide(long long k) {
    switch (k) {
    case (unsigned)-1: return 1;
    case (int)4294967294LL: return 2;
    case (unsigned short)-2: return 4;
    default: return 9;
    }
}

int main() {
    const int ks[] = { 1, 2, 255, 3, -56, 258, 0 };
    for (int i = 0; i < 7; i++) printf("%d ", pick(ks[i]));
    const long long ws[] = { 4294967295LL, -2, 4294967294LL, 65534, 2 };
    for (int i = 0; i < 5; i++) printf("%d ", wide(ws[i]));
    printf("\n");
    return 0;
}
