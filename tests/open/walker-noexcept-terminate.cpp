// The walker (arm64-darwin, tms6747): a throw out of a noexcept function calls abort, so a set_terminate handler
// never runs; the SSA lowering calls std::terminate (review R11, 2026-11-13) and answers clang's:
// tests/cases/noexcept-set-terminate.cpp is this program, and -callee beside it.
#include "../cases/noexcept-set-terminate.cpp"
