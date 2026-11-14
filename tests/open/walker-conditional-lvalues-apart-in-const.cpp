// The C6000's walker (tms6747; arm64's went in M5.3): `given ? *given : rows[k]`, a const and a plain lvalue of one class bound to `const Slot &`, is an
// lvalue ([expr.cond]/4) and the reference binds rows[k] itself; the walker binds a copy, so `&t.pick(..) == &t.rows[1]`
// prints 0 where clang prints 1. tests/cases/ssa-census-last-bodies.cpp is this program (M3b.7; tms6747.sh, M3b.8 f8a).
#include "../cases/ssa-census-last-bodies.cpp"
