// The C6000's walker (tms6747; arm64's went in M5.3): an aggregate's member built from a temporary of its class,
// and a later member that throws - the ledger ends live=1 where clang ends balanced. -front=ssa answers clang's:
// tests/cases/ssa-aggregate-member-temporary.cpp is this program (M3b.7; tms6747.sh, M3b.8 f8a).
#include "../cases/ssa-aggregate-member-temporary.cpp"
