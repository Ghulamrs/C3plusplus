// The C6000's walker (tms6747; arm64's went in M5.3): a temporary with a destructor in a declaration's initialiser or a mem-initialiser,
// alive when an exception leaves the function: flushTemporaries ends such a full expression outside the block's cleanup
// regions, so no pad destroys it and the function gets no landing pads - the ledger ends live=4 over=3 where clang ends 0.
// -front=ssa answers clang's: tests/cases/ssa-own-landing-pads.cpp is this program under that flag (M3b.2, round 21).
#include "../cases/ssa-own-landing-pads.cpp"
