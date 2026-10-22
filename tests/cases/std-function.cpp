// std::function, [func.wrap.func]: a function, a functor, a lambda with a capture, a copy and a
// move, assignment, the comparisons with nullptr, a recursive lambda through one, a reference
// parameter, a vector of them, and an empty one called - bad_function_call ([func.wrap.badcall]).
#include <functional>
#include <vector>
#include <cstdio>
static int twice(int n) { return 2 * n; }
struct Adder { int k; int operator()(int n) const { return n + k; } };
static int apply(const std::function<int(int)> &f, int n) { return f(n); }
static std::function<int(int)> scaler(int by) { return [by](int n) { return n * by; }; }
int main() {
    std::function<int(int)> a = twice, b = Adder{5}, c;
    int cap = 10;
    std::function<int(int)> d = [cap](int n) { return n * cap; };
    std::function<int(int)> e = d;
    std::function<int(int)> f(std::move(a));
    printf("%d %d %d %d\n", f(3), b(3), d(3), e(3));
    int empty = (bool)c, isNull = c == nullptr, notNull = d != nullptr, nullFirst = nullptr == b;
    printf("%d %d %d %d\n", empty, isNull, notNull, nullFirst);
    c = b;
    int afterAssign = c(1);
    c = nullptr;
    printf("%d %d\n", afterAssign, (int)(bool)c);
    std::function<void(int &)> g = [](int &r) { r += 7; };
    int z = 1;
    g(z);
    std::function<int()> h = [cap]() { return cap + 1; };
    std::function<int(int)> fact = [&](int n) { return n ? n * fact(n - 1) : 1; };
    printf("%d %d %d\n", z, h(), fact(5));
    std::vector<std::function<int(int)> > all;
    all.push_back(twice);
    all.push_back(Adder{100});
    all.push_back(scaler(3));
    int sum = 0;
    for (std::size_t i = 0; i < all.size(); ++i) sum += apply(all[i], 4);
    printf("%d\n", sum);
    b.swap(e);
    printf("%d %d\n", b(2), e(2));
    try {
        std::function<int()> none;
        none();
    } catch (const std::bad_function_call &x) {
        printf("caught %d\n", x.what() != 0);
    }
    return 0;
}
