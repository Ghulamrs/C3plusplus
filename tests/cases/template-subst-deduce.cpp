// M8.2 2d: a call a function template answers, deduced at the instantiation under -templates=subst - its specialization
// requested as the replay's call requests it, then ranked with the other candidates ([over.match.best], [temp.func.order]).
extern "C" int printf(const char *, ...);

template <class T> T twice(T a) { return a + a; }
template <class T> T over(T a) { return a + 1; }
template <class T> T *over(T *p) { return p + 1; }
int over(int a) { return a + 100; }
namespace ns { template <class T> T neg(T a) { return -a; } }
template <class T, class U> T mix(T a, U b) { return a * static_cast<T>(b); }

template <class T> T useTwice(T a) { return twice(a); }
template <class T> T useOver(T a) { return over(a); }
template <class T> T *usePtr(T *p) { return over(p); }
template <class T> T useQualified(T a) { return ns::neg(a); }
template <class T> T useMix(T a, int k) { return mix(a, k); }
template <class T> void alone(T a) { twice(a); }

int main() {
    int arr[3] = { 1, 2, 3 };
    printf("%d %g\n", useTwice(4), useTwice(1.25));
    printf("%d %d\n", useOver(5), static_cast<int>(useOver(7.5)));
    printf("%d\n", *usePtr(arr));
    printf("%d %g\n", useQualified(3), useQualified(2.5));
    printf("%g\n", useMix(1.5, 4));
    alone(2);
    return 0;
}
