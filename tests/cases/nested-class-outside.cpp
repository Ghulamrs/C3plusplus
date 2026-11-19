// [class.nest]/3: a nested class declared in its class and defined outside it, `struct Outer::Held {`.
// The definition is read in Outer's scope: it names Outer's members and types unqualified, reaches its
// private ones ([class.access.nest]), may be private itself, and its members are defined out of line too.
extern "C" int printf(const char *, ...);

class Outer {
    static int seed;
    typedef int Count;
    struct Hidden;
public:
    struct Held;
    class Kept;
    static int viaHidden(int k);
};
int Outer::seed = 40;

struct Outer::Held {
    Count x;
    int f() const;
    static int g(int k);
    int inlineSum() const { return x + seed; }
};
int Outer::Held::f() const { return x * 2; }
int Outer::Held::g(int k) { return k + seed; }

struct Outer::Hidden { int h; };
int Outer::viaHidden(int k) { Hidden d; d.h = k; return d.h + 1; }

struct Base { int b; };
class Outer::Kept final : public Base {
public:
    int k;
    int both() const;
};
int Outer::Kept::both() const { return b * 100 + k; }

namespace lib {
struct Box { struct Item; };
struct Box::Item { int w; int twice() const; };
int Box::Item::twice() const { return w * 2; }
}

int main() {
    Outer::Held h;
    h.x = 3;
    Outer::Kept kept;
    kept.b = 5;
    kept.k = 7;
    lib::Box::Item it;
    it.w = 21;
    printf("%d %d %d %d\n", h.f(), Outer::Held::g(2), h.inlineSum(), Outer::viaHidden(8));
    printf("%d %d %d\n", kept.both(), it.twice(), (int)sizeof(Outer::Held));
    return 0;
}
