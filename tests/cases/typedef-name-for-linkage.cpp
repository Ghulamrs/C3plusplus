// [dcl.typedef]/9: an unnamed class defined in a typedef declaration takes the typedef's name for
// linkage, so a function or a template over it has a name the linker can hold - `_Z1f2P2`,
// `Box<P2>`, `S::In`, `n::U`. cpp11 refused every one ("an unnamed class has no name"); found
// compiling `std::vector<SceneVertex>` over the C header idiom `typedef struct { ... } SceneVertex;`.
extern "C" int printf(const char *, ...);

typedef struct { float x, y; } P2;
struct S { typedef struct { int a; } In; };
namespace n { typedef union { int i; float f; } U; }
template <class T> struct Box { T t; Box() {} T get() const { return t; } };

float f(P2 p) { return p.x + p.y; }
int g(S::In v) { return v.a; }
int h(n::U u) { return u.i; }

int main() {
    P2 p = { 1.5f, 2.0f };
    S::In in = { 7 };
    n::U u;
    u.i = 9;
    Box<P2> b;
    b.t = p;
    printf("%g %d %d %g\n", f(p), g(in), h(u), b.get().y);
    return 0;
}
