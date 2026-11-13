// The walker (tms6747): a static reference's temporary is an ordinary full-expression one in the Ast - destroyed
// at once, the reference left dangling - where [class.temporary]/5 gives it static storage. The SSA lowering's
// staticConstruction answers clang's: tests/cases/static-reference-to-temporary.cpp is this program (lane c6n).
#include "../cases/static-reference-to-temporary.cpp"
