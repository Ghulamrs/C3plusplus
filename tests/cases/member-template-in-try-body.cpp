// A member template is instantiated where it is first called, its body read on the spot - and that
// reading is another function's. Called first inside a caller's `try` body, the body took the caller's
// handler chain for its own cleanup pad and jumped to a label its function never had: the assembler
// refused the file. Found compiling cxx3's own ParserTemplate.cpp for arm64 (vector::insert<It>).
extern "C" int printf(const char *, ...);
int built, gone;
struct Holder { int v; explicit Holder(int x); ~Holder(); };
Holder::Holder(int x) : v(x) { built++; }
Holder::~Holder() { gone++; }
int check(int k) { if (k < 0) throw k; return k; }
struct Box {
    int total;
    template <class It> int add(It first, It last) {
        Holder h(1);
        for (It i = first; i != last; ++i) total += check(*i);
        return total + h.v;
    }
};
int main() {
    int good[] = { 1, 2, 3 };
    int bad[] = { 4, -5, 6 };
    Box b = { 0 };
    int got = 0;
    try { got = b.add(good, good + 3); } catch (int e) { got = e; }
    printf("%d\n", got);
    try { got = b.add(bad, bad + 3); } catch (int e) { got = e; }
    printf("%d %d %d\n", got, b.total, built - gone);
    return 0;
}
