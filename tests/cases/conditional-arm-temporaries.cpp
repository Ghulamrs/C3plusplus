// [expr.cond]/1 evaluates one arm, so a temporary the other arm would make is never made and must not be
// destroyed. Only a `?:` of class type guarded them; on every other result each arm's temporaries were
// destroyed whatever ran - a destructor on a slot nothing built. Found self-hosting cxx3, where
// `c ? f(a + b) : g(a + b)` freed a string never constructed.
extern "C" int printf(const char *, ...);
int built, gone, unbuilt;
struct T {
    int magic, v;
    explicit T(int x);
    T(const T &o);
    ~T();
};
T::T(int x) : magic(0x5a5a), v(x) { built++; }
T::T(const T &o) : magic(0x5a5a), v(o.v) { built++; }
T::~T() { if (magic != 0x5a5a) unbuilt++; else gone++; magic = 0; }
T mk(int x) { return T(x); }
int val(const T &t) { return t.v; }
int store[4];
int &slot(const T &t) { return store[t.v % 4]; }
const int *at(const T &t) { return &store[t.v % 4]; }
int sink;
void note(const T &t) { sink += t.v; }
int main() {
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        bool c = i % 2 == 0;
        sum += c ? val(mk(10)) : val(mk(20));               // a scalar result
        (c ? slot(mk(1)) : slot(mk(2))) += i;               // an lvalue result
        sum += *(c ? at(mk(3)) : at(mk(2)));                // a pointer result
        c ? note(mk(100)) : note(T(200));                   // a void result
        sum += c ? val(T(5)) : 7;                           // only one arm makes one
    }
    printf("%d %d %d %d %d %d\n", sum, store[1], store[2], sink, built - gone, unbuilt);
    return 0;
}
