// `::Y` inside `namespace n` when only `n::Y` exists: [namespace.qual] asks the global namespace alone, so
// the nearer `n::Y` is not found and the name is an error (lane m6d-glob, 2026-11-19).
namespace n {
struct Y { int v; };
int f() {
    ::Y y;
    y.v = 1;
    return y.v;
}
}
int main() { return n::f(); }
