// M3b.2: a closure's body naming a data member or calling a member function of the class whose `this` it captured,
// by [this] and by [=]/[&], in a const member function, through a base, and from a lambda inside a lambda.
extern "C" int printf(const char *, ...);

struct Base {
    int b;
    int baseTwice() const { return b * 2; }
};

struct Box : Base {
    int n;
    int scale;
    int add(int k) { n += k; return n; }
    int read() const { return n; }
    int run() {
        auto f = [this](int k) { return add(k) + scale; };
        auto g = [=]() { return n * 10 + baseTwice(); };
        auto h = [&]() { scale = scale + 1; return read() + b; };
        int x = f(3);
        int y = g();
        int z = h();
        return x * 10000 + y * 100 + z;
    }
    int look() const {
        auto f = [this]() { return n + scale + read(); };
        return f();
    }
    int nested() {
        auto outer = [this]() {
            auto inner = [this]() { return add(1) + b; };
            const int i = inner();
            return i + n;
        };
        return outer();
    }
};

int main() {
    Box x;
    x.b = 3;
    x.n = 4;
    x.scale = 5;
    int r = x.run();
    printf("%d %d %d\n", r, x.n, x.scale);
    int l = x.look();
    printf("%d\n", l);
    int m = x.nested();
    printf("%d %d\n", m, x.n);
    return 0;
}
