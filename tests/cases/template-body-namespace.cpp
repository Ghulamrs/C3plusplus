// A class template used from outside its namespace reads its body, and its members' bodies,
// in that namespace: a member's parameter, a data member and a return type naming a class
// the namespace declares were "not declared" when it was first used from another one.
extern "C" int printf(const char *, ...);
namespace lib {
    struct Item { int v; };
    int bump(int k) { return k + 1; }
    template <class T> struct Holder {
        Item item;
        T extra;
        void set(Item i) { item = i; }
        Item get() const { return item; }
        int next() const { return bump(item.v); }
    };
}
namespace app {
    int use() {
        lib::Holder<int> h;
        lib::Item i;
        i.v = 41;
        h.set(i);
        h.extra = 0;
        return h.next() + h.get().v * 0;
    }
}
int main() {
    printf("%d\n", app::use());
    return 0;
}
