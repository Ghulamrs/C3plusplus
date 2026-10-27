// M3b.2: a static data member named in a function body - one folded in its class is its value, any other the
// one object its symbol names - and a static member function's address taken as a value.
extern "C" int printf(const char *, ...);

struct Counter {
    static const int limit = 7;
    static const unsigned long none = ~0ul;
    static int total;
    static double scale;
    static int twice(int x) { return 2 * x; }
    static int thrice(int x) { return 3 * x; }
    int bump(int by) { total += by; return total < limit ? total : limit; }
};
int Counter::total = 3;
double Counter::scale = 1.5;

template <class T> struct Box { static T kept; static int size(int k) { return (int)sizeof(T) + k; } };
template <class T> T Box<T>::kept = T(5);

static int apply(int (*f)(int), int v) { return f(v); }

static int readAll() {
    int s = Counter::limit + Counter::total;
    Counter::total = Counter::total + 1;
    s += Counter::none == ~0ul ? 100 : 0;
    s += (int)(Counter::scale * 4);
    return s + Box<int>::kept;
}

static int pick(bool which) {
    int (*f)(int) = which ? Counter::twice : &Counter::thrice;
    return apply(f, 10) + apply(&Counter::twice, 1) + apply(Box<short>::size, 0);
}

int main() {
    Counter c;
    printf("%d\n", readAll());
    const int bumped = c.bump(2);
    printf("%d %d\n", bumped, Counter::total);
    printf("%d %d\n", pick(true), pick(false));
    Box<double>::kept += 0.25;
    printf("%.2f %d\n", Box<double>::kept, Counter::limit);
    return 0;
}
