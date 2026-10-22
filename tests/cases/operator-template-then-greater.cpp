// The scan that tells a variable template from a function counted `operator<`'s `<` as an
// opening angle, so the next `>` closed it and the next `=` - `if (n > h) n = h;` in another
// function - was refused as a C++14 variable template.
extern "C" int printf(const char *, ...);
template <class T> struct Box { T v; };
template <class T> bool operator<(const Box<T> &a, const Box<T> &b) { return a.v < b.v; }
int clamp(int n, int h) { if (n > h) n = h; return n; }
int main() {
    Box<int> a = { 1 }, b = { 2 };
    printf("%d %d %d\n", a < b, clamp(9, 4), clamp(2, 4));
    return 0;
}
