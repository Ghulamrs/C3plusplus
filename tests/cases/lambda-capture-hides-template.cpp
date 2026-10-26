// A lambda's capture is the enclosing function's local, and hides a template of the same name in its body
// ([basic.lookup.unqual]): `next < k` is a comparison, as it is outside the lambda. Found by self-hosting.
extern "C" int printf(const char *, ...);
namespace lib { template <class T> T next(T t) { return t + 1; } }
template <class T> T count(T t) { return t * 2; }

int f(int next, int k) {
    auto ref = [&](int b) -> int { return (next < k ? next : b); };
    auto copy = [=](int b) { return next < k ? b : next; };
    int count = 9;
    auto local = [&](int b) { return count < b ? count : b; };
    return ref(1) * 1000 + copy(2) * 100 + local(20);
}

int main() {
    printf("%d %d %d\n", f(5, 7), f(8, 7), lib::next(4) + count(3));
    return 0;
}
