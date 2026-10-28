// A local array of characters initialised from a string literal, lowered through the SSA IR (M3b.2): the literal's
// units copied in and the elements past them zeroed - sized and unsized, narrow and wide - beside an array of
// pointers to strings and an aggregate whose member is a scoped enumeration. Every value is printed as an integer.
extern "C" int printf(const char *, ...);

enum class Rank { A, B, C };
template <class T> struct Box { T v; int tag; };

static int narrow() {
    char full[6] = "hello";
    char padded[8] = "ab";
    char open[] = "xyz";
    int sum = 0;
    for (int i = 0; i < 6; i++) sum = sum * 10 + (full[i] != 0);
    for (int i = 0; i < 8; i++) sum += padded[i];
    return sum * 100 + (int)sizeof(open) * 10 + open[2];
}

static int wide() {
    wchar_t w[4] = L"ab";
    char16_t u[3] = u"q";
    char32_t big[5] = U"xyz";
    return (int)w[0] + (int)w[2] * 1000 + (int)u[0] + (int)u[1] * 7 + (int)big[2] + (int)big[4] * 3;
}

static int pointers() {
    const char *names[3] = { "one", "three", "fifteen" };
    int total = 0;
    for (int i = 0; i < 3; i++) { int n = 0; while (names[i][n] != 0) n++; total = total * 10 + n; }
    return total;
}

static int ranked() {
    Box<Rank> b = { Rank::C, 4 };
    Box<int> i = { 5, 6 };
    return (int)b.v * 100 + b.tag * 10 + i.v + i.tag;
}

int main() {
    printf("%d %d %d %d\n", narrow(), wide(), pointers(), ranked());
    return 0;
}
