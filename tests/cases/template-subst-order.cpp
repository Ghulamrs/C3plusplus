// M8.2 2d: a body with more than one call a function template answers, under -templates=subst - the specializations
// requested in the replay's order (the read's node order: a call's node is noted as its door fires), so the object's is the same.
extern "C" int printf(const char *, ...);

template <class T> T neg(T a) { return -a; }
template <class T> T twice(T a) { return a + a; }
template <class T> T sq(T a) { return a * a; }

template <class T> T chain(T a) { T b = neg(a); T c = twice(b); return sq(c) + neg(c); }
template <class T> T pair2(T a, T b) { return twice(a) - sq(b); }
template <class T> void calls(T a) { sq(a); twice(a); }

int main() {
    printf("%d %g\n", chain(3), chain(1.5));
    printf("%d %g\n", pair2(2, 3), pair2(0.5, 1.5));
    calls(4);
    calls(2.0f);
    return 0;
}
