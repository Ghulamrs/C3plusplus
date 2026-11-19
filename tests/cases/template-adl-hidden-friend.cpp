// A friend defined in its class is found by argument-dependent lookup alone ([namespace.memdef]/3): a function template's
// call over the class reaches it beside the template's specialization, and the exact non-template wins.
extern "C" int printf(const char *, ...);

namespace door {
struct Key {
    int code;
    friend int open(const Key &k) { return k.code * 2; }
};
struct Bolt { int code; };
}
template <class T> int open(const T &) { return -1; }
template <class T> int tryOpen(const T &a) { return open(a); }

int main() {
    door::Key k = { 21 };
    door::Bolt b = { 5 };
    printf("%d %d %d\n", tryOpen(k), tryOpen(b), tryOpen(3));
    return 0;
}
