// [namespace.udir]/6: a name declared in the namespace and in an inline namespace of
// it, as two entities, is ambiguous when looked up through the enclosing name - clang
// says "reference to 'x' is ambiguous" at the name. Written through its own namespace,
// `lib::v2::x`, either one is reached without a word.
namespace lib {
    inline namespace v2 { int x = 1; }
    int x = 2;
}
int main() { return lib::v2::x + lib::x; }
