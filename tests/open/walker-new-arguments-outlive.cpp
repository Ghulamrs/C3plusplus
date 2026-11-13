// The walker (tms6747): a temporary bound in a `new`'s constructor, or in a call a return is built from, outlives that
// call - the ledger ends gone=5 live=1 where clang ends 6 and 6. -front=ssa answers clang's:
// tests/cases/ssa-new-arguments-outlive.cpp is this program (M3b.7; tms6747.sh, M3b.8 f8a).
#include "../cases/ssa-new-arguments-outlive.cpp"
