// A braced initialiser with values on a member of class or array type - `GnuSpelling gnu_{out_};`,
// `bool cross[2] = {false, false};` - builds it: a class with constructors through one, direct- or
// copy-list as written, and `{}` there is its default constructor, not zeroed bytes; an aggregate or an
// array of scalars leaf by leaf. Refused by name until the compiler's own C6xModel.h met it (M1 L4).
extern "C" int printf(const char *, ...);
struct Out { int k; };
struct Gnu { Out &o; int z; Gnu(Out &x) : o(x), z(5) {} };
struct Agg { int a, b; };
struct C { int v; C() : v(77) {} C(int x) : v(x + 1) {} };
struct E { int v; explicit E(int x) : v(x) {} };
struct H {
    Out out_;
    Gnu gnu_{out_};
    bool cross[2] = {false, true};
    int grid[2][3] = {{1, 2, 3}, {4}};
    Agg agg = {7, 8};
    Agg agg2{9};
    C c0{};
    C c1 = {0};
    C c2{4};
    E e{6};
    H() {}
};
int main() {
    H h;
    printf("%d %d %d %d %d %d %d %d %d %d %d %d %d\n", h.gnu_.z, (int)h.cross[0], (int)h.cross[1], h.grid[0][2],
           h.grid[1][0], h.grid[1][2], h.agg.b, h.agg2.a, h.agg2.b, h.c0.v, h.c1.v, h.c2.v, h.e.v);
    return 0;
}
