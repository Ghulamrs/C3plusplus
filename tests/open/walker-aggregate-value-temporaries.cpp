// The C6000's walker (tms6747; arm64's went in M5.3): temporaries an aggregate's member values leave, outliving the list, and a later value's throw -
// the ledger ends live=4 where clang ends balanced. -front=ssa answers clang's: tests/cases/ssa-aggregate-value-temporaries.cpp
// is this program (M3b.7; tms6747.sh, M3b.8 f8a).
#include "../cases/ssa-aggregate-value-temporaries.cpp"
