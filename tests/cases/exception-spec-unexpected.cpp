// [except.unexpected]: an exception a `throw(T...)` list does not allow unwinds the frame and then
// calls the unexpected handler, which may throw a type the list allows in its place - from a
// constructor's function try block too, whose handler's rethrow the list does not allow.
#include <exception>
extern "C" int printf(const char *, ...);

struct B { int v; };
struct Noisy {
    const char *n;
    Noisy(const char *s) : n(s) {}
    ~Noisy() { printf("~%s\n", n); }
};

void toInt() {
    printf("unexpected handler\n");
    throw 42;
}

void bad() throw(int) {
    Noisy x("bad");
    { Noisy y("inner"); throw B(); }
}

struct M {
    int id;
    M(int i) : id(i) { printf("M%d\n", id); if (id == 3) throw id; }
    ~M() { printf("~M%d\n", id); }
};
struct Base { Base() { printf("Base\n"); } ~Base() { printf("~Base\n"); } };

struct E : Base {
    M a;
    E() throw(double) try : a(3) {} catch (int i) { printf("handler %d\n", i); }
};

void toDouble() {
    printf("unexpected handler again\n");
    throw 2.5;
}

int main() {
    std::set_unexpected(toInt);
    try { bad(); }
    catch (int i) { printf("replaced %d\n", i); }
    catch (B &) { printf("B escaped\n"); }
    std::set_unexpected(toDouble);
    try { E e; } catch (double d) { printf("caught %g\n", d); }
    printf("done\n");
    return 0;
}
