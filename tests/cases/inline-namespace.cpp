// [namespace.def]/8: the members of an inline namespace are members of the namespace
// around it, for every lookup - so `lib::version` is `lib::v2::version`, written
// through either name, and the sibling `v1` keeps its own. The name the linker sees
// is the full path, `_ZN3lib2v27versionEv`, measured against clang.
extern "C" { int printf(const char *, ...); }
namespace lib {
    inline namespace v2 { int version() { return 2; } }
    namespace v1 { int version() { return 1; } }
}
int main() { printf("%d %d %d\n", lib::version(), lib::v2::version(), lib::v1::version()); }
