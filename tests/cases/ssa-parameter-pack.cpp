// A function parameter pack under the SSA lowering: `Ts... rest` is as many parameters as members, `rest...` at a
// call as many arguments - by value, by const reference, of mixed types, empty, through a pattern, in a member
// function template, and with `sizeof...`.
extern "C" int printf(const char *, ...);

static long long total() { return 0; }
template <class T, class... Ts> long long total(T first, Ts... rest) { return first + total(rest...); }

static int widest() { return 0; }
template <class T, class... Ts> int widest(const T &first, const Ts &... rest) {
    const int w = widest(rest...);
    return (int)sizeof(T) > w ? (int)sizeof(T) : w;
}

template <class... Ts> int howMany(Ts... xs) { return (int)sizeof...(xs); }

static int twice(int x) { return x * 2; }
static int add3(int a, int b, int c) { return a * 100 + b * 10 + c; }
template <class... Ts> int doubled(Ts... xs) { return add3(twice(xs)...); }

struct Acc {
    int sum;
    Acc() : sum(0) {}
    void put() {}
    template <class T, class... Ts> void put(T v, Ts... more) { sum += (int)v; put(more...); }
};

template <class... Ts> int forwardTo(Ts... xs) { return add3(xs...); }

int main() {
    printf("total %lld\n", total(1, 2, 3, 4));
    printf("total mixed %lld\n", total('a', (short)2, 3LL, true));
    printf("widest %d\n", widest('x', (short)1, 2.5, 7));
    printf("howMany %d %d %d\n", howMany(), howMany(1), howMany(1, 'c', 2.0));
    printf("doubled %d\n", doubled(1, 2, 3));
    Acc a;
    a.put(5, 'b', 2.75, 10LL);
    printf("acc %d\n", a.sum);
    printf("forwardTo %d\n", forwardTo(4, 5, 6));
    return 0;
}
