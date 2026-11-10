// Division by a constant on SSA (M3b.4 round 5): / and % of int, unsigned, long long and unsigned long long by powers
// of two, by multipliers, by negative divisors, by unsigned divisors with no exact multiplier and past the signed range,
// over values at the ends of each range. Its -O1 -dump=ssa=divide on x86_64-linux is ssa-divide.divide.
#include <climits>
#include <cstdio>

static const int ints[] = { 0, 1, -1, 2, -2, 3, -3, 6, -7, 7, 99, -100, 1000, -12345, 65535, 1 << 30, INT_MAX,
                            INT_MAX - 1, INT_MIN, INT_MIN + 1 };
static const unsigned uints[] = { 0u, 1u, 2u, 3u, 7u, 99u, 1000u, 65535u, 0x7fffffffu, 0x80000000u, 0xfffffffeu,
                                  0xffffffffu, 123456789u };
static const long long wides[] = { 0, 1, -1, 7, -7, 1000000007LL, -999999999999LL, LLONG_MAX, LLONG_MIN, LLONG_MIN + 1,
                                   LLONG_MAX - 5, 1LL << 40, -(1LL << 40) };
static const unsigned long long uwides[] = { 0, 1, 7, 1000000007ULL, ULLONG_MAX, ULLONG_MAX - 1, 1ULL << 63,
                                             (1ULL << 63) - 1, 0x123456789abcdefULL };

#define COUNT(a) static_cast<int>(sizeof(a) / sizeof(a[0]))

static unsigned long long mix(unsigned long long h, unsigned long long v) { return (h ^ v) * 1099511628211ULL; }

#define I(d) { unsigned long long h = 14695981039346656037ULL; \
    for (int i = 0; i < COUNT(ints); i++) { const int n = ints[i]; \
        h = mix(mix(h, static_cast<unsigned>(n / (d))), static_cast<unsigned>(n % (d))); } \
    std::printf("int %d: %llx\n", d, h); }
#define U(d) { unsigned long long h = 14695981039346656037ULL; \
    for (int i = 0; i < COUNT(uints); i++) { const unsigned n = uints[i]; h = mix(mix(h, n / (d)), n % (d)); } \
    std::printf("unsigned %u: %llx\n", d, h); }
#define W(d) { unsigned long long h = 14695981039346656037ULL; \
    for (int i = 0; i < COUNT(wides); i++) { const long long n = wides[i]; \
        h = mix(mix(h, static_cast<unsigned long long>(n / (d))), static_cast<unsigned long long>(n % (d))); } \
    std::printf("long long %lld: %llx\n", d, h); }
#define UW(d) { unsigned long long h = 14695981039346656037ULL; \
    for (int i = 0; i < COUNT(uwides); i++) { const unsigned long long n = uwides[i]; h = mix(mix(h, n / (d)), n % (d)); } \
    std::printf("unsigned long long %llu: %llx\n", d, h); }

static void ints32() {
    I(2) I(4) I(8) I(1024) I(1 << 30) I(3) I(5) I(6) I(7) I(10) I(25) I(100) I(641) I(1000) I(65537) I(INT_MAX)
    I(-2) I(-8) I(-3) I(-7) I(-1000) I(1)
}

static void uints32() {
    U(2u) U(16u) U(1u << 31) U(3u) U(5u) U(7u) U(10u) U(25u) U(100u) U(641u) U(1000u) U(0x7fffffffu) U(0x80000001u) U(1u)
}

static void wides64() {
    W(2LL) W(1LL << 40) W(3LL) W(7LL) W(10LL) W(1000LL) W(1000000007LL) W(LLONG_MAX) W(-3LL) W(-16LL) W(-1000000007LL)
}

static void uwides64() {
    UW(2ULL) UW(1ULL << 40) UW(1ULL << 63) UW(3ULL) UW(5ULL) UW(7ULL) UW(10ULL) UW(1000ULL) UW(1000000007ULL)
    UW((1ULL << 62) + 1)
}

// A divisor held in a local: promote and fold make it the constant the pass sees.
static int local(int n) {
    const int ten = 10;
    int q = n / ten, r = n % ten;
    return q * 100 + r;
}

int main() {
    ints32();
    uints32();
    wides64();
    uwides64();
    std::printf("%d %d %d\n", local(1234), local(-1234), local(7));
    return 0;
}
