// A lambda inside a lambda in a member function, both with [&]: the inner one names a member, so both take `this`.
#include <cstdio>
struct S {
    int h[3];
    int run(int v) const;
};
int S::run(int v) const {
    const auto outer = [&](int k) -> int {
        const auto inner = [&](int o) { return h[o] + k; };
        return inner(1) + v;
    };
    return outer(2);
}
int main() { S s = {{10, 20, 30}}; std::printf("%d\n", s.run(100)); return 0; }
