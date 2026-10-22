// [basic.lookup.qual]/1: a using-declaration's qualifier is found innermost first, so
// `using inner::f;` written inside `outer` names `outer::inner::f`, not a file-scope `inner`.
extern "C" int printf(const char *, ...);
namespace inner { int f() { return 1; } }
namespace outer {
    namespace inner { int f() { return 2; } struct T { int v; }; }
    using inner::f;
    using inner::T;
    int g() { T t; t.v = 30; return f() + t.v; }
}
int main() {
    printf("%d %d\n", outer::g(), inner::f());
    return 0;
}
