// Nesting and reopening. An inline namespace inside an inline one is the outermost's
// too, transitively; one inside a plain namespace inside an inline one is reached
// through the plain name without the inline one; a namespace first defined inline may
// be reopened with or without the word ([namespace.def]/8 - the first definition
// decides); and `inline namespace { }` is the unnamed namespace with the mark on it.
extern "C" { int printf(const char *, ...); }
namespace lib {
    inline namespace v2 { int a() { return 2; } }
    namespace v2 { int b() { return 3; } }          // reopened without `inline`
    inline namespace v2 { int c() { return 4; } }   // and with it
}
namespace outer {
    inline namespace mid {
        inline namespace deep { int d() { return 5; } }
        namespace plain {
            inline namespace in { int e() { return 6; } }
        }
    }
}
inline namespace { int anon() { return 7; } }
int main() {
    printf("%d %d %d %d %d %d %d %d\n", lib::a(), lib::b(), lib::c(), outer::d(), outer::mid::deep::d(),
           outer::plain::e(), outer::mid::plain::in::e(), anon());
}
