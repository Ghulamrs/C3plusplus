// The walker (tms6747; the selector leaves these functions to it): an exception out of an inner block of a handler
// destroys the block's object and not the handler's own - gone 3 for 4. The SSA path answers clang's:
// tests/cases/handler-guards-nested.cpp is this program (lane c6n, 2026-11-14).
#include "../cases/handler-guards-nested.cpp"
