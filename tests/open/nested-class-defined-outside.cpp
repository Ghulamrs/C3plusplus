// [class.nest]/3: a nested class declared in its class may be defined outside it, `struct Outer::Inner {...};`.
// cxx3 refuses it with "expected a name" at the '::' - found writing friend-class-qualified (M2.1, 2026-10-24).
extern "C" int printf(const char *, ...);
struct Outer { struct Inner; int k; };
struct Outer::Inner { int v; int twice() const { return 2 * v; } };
int main() {
    Outer::Inner i = { 21 };
    printf("%d\n", i.twice());
    return 0;
}
