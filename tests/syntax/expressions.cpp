// The expression productions M2.8 records: every binary level, assignment, the conditional, comma, prefix and
// postfix operators, casts, calls, subscripts and members, and the literals and names under them.
struct S { int v; int get() const { return v; } };
int g(int, int);
int f(int a, int *p, S s, S *q) {
    int b = a * 2 + p[1] - a / 3 % 4;
    b <<= a >> 1;
    b += g(a, -b) ? a : *p;
    b = (a < b) != (a >= b) && (a & 1 | b ^ 2);
    b = s.get() + q->v + (int)2.5 + q[0].v++ + ~a + 'c';
    b = (a, b), ++b;
    return b == 3 || !a;
}
