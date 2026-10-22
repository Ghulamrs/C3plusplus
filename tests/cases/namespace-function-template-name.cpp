// Two namespaces each declaring a function template of one name: [basic.lookup.unqual] takes the
// nearest namespace that declares it and [basic.lookup.qual] the one a qualifier names. Function
// templates were kept by their bare name, so m::twice and n::twice were one overload set and n's
// won for both calls - a silent wrong value (6 2 4 where clang prints 6 2 6).
extern "C" int printf(const char *, ...);
template <class T> T twice(T x) { return x - x; }
namespace n {
    template <class T> T twice(T x) { return x + x; }
    int run() { return twice(3); }
    namespace inner { int deep() { return twice(10); } }
}
namespace m {
    template <class T> T twice(T x) { return x * 3; }
    int run() { return twice(4); }
}
namespace empty { int viaGlobal() { return twice(5); } }
int main() {
    printf("%d %d %d\n", n::run(), n::twice(1), m::twice(2));
    printf("%d %d %d %d\n", m::run(), n::inner::deep(), twice(7), empty::viaGlobal());
    printf("%d %d %g\n", m::twice<int>(5), (int)n::twice<long>(6), m::twice(1.5));
    return 0;
}
