// M8.1, the review's M1: [temp.dep]/3 - unqualified lookup never looks in a
// dependent base, at the definition or the instantiation, so `nb` is not
// declared and clang refuses both uses (10:15 and 10:32). cxx1 replays the body
// with B<int> complete and finds nb (prints 1). The read marks the name
// `unfound`, not `dependent` (tests/syntax/template-read-marks), for M8.5's refusal.
#include <cstdio>
template <class T> struct B { int nb; B() : nb(0) {} };
int g(int v) { return v; }
template <class T> struct C : B<T> {
    int f() { nb = 1; return g(nb); }
};
int main() {
    C<int> c;
    std::printf("%d\n", c.f());
    return 0;
}
