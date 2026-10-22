// A namespace first defined without `inline` may not be reopened with it: the first
// definition decides ([namespace.def]/8), and clang refuses the reopening at its name.
// The other way round - defined `inline`, reopened without - is allowed, and is in
// inline-namespace-nested.cpp.
namespace lib {
    namespace v2 { int a() { return 2; } }
    inline namespace v2 { int b() { return 3; } }
}
int main() { return lib::v2::a() + lib::v2::b(); }
