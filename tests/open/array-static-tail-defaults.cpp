// A static array's tail built by a default constructor completed with default arguments: the
// parser hands a local's tail defaults to its `var` node by the local's serial, a static has none.
extern "C" int printf(const char *, ...);
struct E { int a, b; E(int a = 3, int b = 4); ~E(); };
E::E(int x, int y) : a(x), b(y) {}
E::~E() { printf("~E %d %d\n", a, b); }
E sd[2] = { 9 };
int main() {
    for (int i = 0; i < 2; i++) printf("sd %d %d\n", sd[i].a, sd[i].b);
    return 0;
}
