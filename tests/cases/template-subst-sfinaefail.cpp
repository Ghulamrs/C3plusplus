// SFINAE trials whose signature holds types alone, the failed substitution shown by the kernel under the deduced binding with
// -templates=subst (slice 4e): a member type a complete class lacks, in a parameter and in the return type, a class template's
// specialization made at the use that lacks it, and a qualifier that is no class. The check replays each beside and holds them.
extern "C" int printf(const char *, ...);

struct A { typedef int type; };
struct C { typedef char other; };
template <class T> struct Value {};
template <> struct Value<int> { typedef int type; };

template <class T> int which(T, typename T::type * = 0) { return 1; }
template <class T> int which(T, typename T::other * = 0) { return 2; }
template <class T> typename T::type twice(T) { return 3; }
template <class T> typename T::other twice(T) { return 'x'; }
template <class T> typename Value<T>::type val(T) { return 4; }
int val(double) { return 5; }
template <class T> typename T::type scal(T) { return 6; }
int scal(long) { return 7; }

int main() {
    A a;
    C c;
    printf("%d %d %d %d\n", which(a), which(c), twice(a), twice(c));
    printf("%d %d %d %d\n", val(1), val(1.5), scal(a), scal(2));
    return 0;
}
