// A function type as a template argument ([temp.arg.type]): a partial specialization peels R(A...)
// and a pointer to a function apart, and both ABIs spell the type into a name.
extern "C" int printf(const char *, ...);
template <class S> struct F { static int kind() { return 0; } };
template <class R, class... A> struct F<R(A...)> { static int kind() { return 100 + (int)sizeof...(A); } };
template <class R, class A> struct F<R (*)(A)> { static int kind() { return 200; } };
template <class T> struct Box { T *p; };
namespace ns { struct Spelling {}; }
static int twice(int n) { return 2 * n; }
int a(Box<void(int)>) { return 1; }
int b(Box<void(ns::Spelling &)>) { return 2; }
int c(const Box<int()> &) { return 3; }
int e(Box<void()>, Box<void()>) { return 4; }
int j(Box<int(int, char)>, Box<const int *(const char *, long)>) { return 5; }
int k(Box<void(int, ...)> *) { return 6; }
int l(Box<void(Box<void(int)>)>) { return 7; }
struct S { int call(Box<int(int, char)>) { return 8; } };
int main() {
    Box<int(char)> bc = { 0 };
    Box<void()> bv = { 0 };
    printf("%d %d %d %d %d %d\n", F<int(char)>::kind(), F<void()>::kind(), F<int(char, long, double)>::kind(),
           F<int (*)(int)>::kind(), F<int>::kind(), (int)(bc.p == 0 && bv.p == 0) + twice(0));
    Box<void(int)> v1 = { 0 };
    Box<void(ns::Spelling &)> v2 = { 0 };
    Box<int()> v3 = { 0 };
    Box<int(int, char)> v4 = { 0 };
    Box<const int *(const char *, long)> v5 = { 0 };
    Box<void(int, ...)> v6 = { 0 };
    Box<void(Box<void(int)>)> v7 = { 0 };
    S s;
    int sum = a(v1) + b(v2) + c(v3) + e(bv, bv) + j(v4, v5) + k(&v6) + l(v7) + s.call(v4);
    printf("%d\n", sum);
    return 0;
}
