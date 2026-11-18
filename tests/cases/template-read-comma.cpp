// M8.1, the review's M5: a comma expression over an operand of dependent type
// is dependent ([temp.dep.expr]/1 - an `operator,` for T could give it any
// type), so the read leaves g to the instantiation; tests/syntax/template-read-marks
// shows the call marked. Both instantiations take g(int).
#include <cstdio>
int g(int v) { return v + 1; }
template <class T> int f(T x, int k) { return g((x += 1, k)); }
int main() {
    std::printf("%d %d\n", f(1, 2), f(2.5, 4));
    return 0;
}
