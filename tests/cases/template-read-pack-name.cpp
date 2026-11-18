// A function parameter pack's name outlives the instantiation that wrote it (count's `args`); the read of a
// later template's own pack of that name is its own, not the stale one (M8.1, Owned.h's make_owned).
#include <cstdio>

template <class... Ts> int count(Ts... args) { return static_cast<int>(sizeof...(args)); }
int first = count(1, 2.0, 'c');

template <class T> T pass(T v) { return v; }
template <class... A> int again(A... args) { return count(pass(args)...); }

int main() {
    std::printf("%d %d\n", first, again(1, 2));
    return 0;
}
