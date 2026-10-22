// A std::function owns a copy of what it holds ([func.wrap.func.con]): copied when it is copied,
// destroyed with it, replaced by assignment - whatever is moved or elided, every copy balances.
#define LF_BALANCE_ONLY
#include "lifetime.h"
#include <functional>
struct Counter {
    int k;
    Counter(int n) : k(n) { lfBuilt(this, "Counter"); }
    Counter(const Counter &o) : k(o.k) { lfBuilt(this, "Counter"); }
    ~Counter() { lfGone(this, "Counter"); }
    int operator()(int n) const { return n + k; }
};
int main() {
    lfWatch();
    int total = 0;
    {
        std::function<int(int)> a = Counter(1);
        std::function<int(int)> b = a;
        std::function<int(int)> c(std::move(b));
        total += a(10) + c(20);
        a = Counter(2);
        total += a(10);
        c = a;
        total += c(1);
        a = nullptr;
        std::function<int(int)> d;
        d = std::move(c);
        total += d(5);
    }
    printf("%d\n", total);
    return 0;
}
