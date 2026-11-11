// [class.mem]: `T &ref, saved;` declares one reference member and one object - each declarator is asked alone,
// so a class whose constructor comes after its members binds `ref` and copies `saved` (the Classes.cpp shape).
extern "C" int printf(const char *, ...);
namespace {
struct Keyed {
    int &ref, saved;
    Keyed(int &r, int v);
    ~Keyed();
};
Keyed::Keyed(int &r, int v) : ref(r), saved(r) { ref = v; }
Keyed::~Keyed() { ref = saved; }
struct Two {
    const int &a, &b;
    int sum() const { return a + b; }
};
}
int main() {
    int k = 3;
    {
        Keyed g(k, 9);
        printf("%d %d\n", k, g.saved);
    }
    int x = 4, y = 5;
    Two t = { x, y };
    printf("%d %d\n", k, t.sum());
    return 0;
}
