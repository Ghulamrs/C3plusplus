// [basic.scope.block]: a typedef in a block is that block's. Two functions - or two specializations of one
// function template - each with `typedef ... X` of another type were "typedefed twice" (M1).
extern "C" int printf(const char *, ...);
template <class T> int f() { typedef T X; return (int)sizeof(X); }
template <class T> struct W {
    template <class U> W(U u) : v((int)u + f<U>()) {}
    int v;
};
template <class T> int g(T t) { typedef T Y; W<Y> w(t); return w.v + f<double>(); }
int a() { typedef int Z; return (int)sizeof(Z); }
int b() { typedef double Z; return (int)sizeof(Z); }
int main() { printf("%d %d %d %d\n", f<int>() + f<double>(), g(1), g(2.0), a() + b()); return 0; }
