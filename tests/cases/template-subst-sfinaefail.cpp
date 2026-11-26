// SFINAE trials whose signature holds types alone, the failed substitution shown by the kernel under the deduced binding with
// -templates=subst (slice 4e): a member type a complete class lacks, in a parameter and in the return type, drops the candidate.
// The check replays each beside and holds the two failures equal.
extern "C" int printf(const char *, ...);

struct A { typedef int type; };
struct C { typedef char other; };

template <class T> int which(T, typename T::type * = 0) { return 1; }
template <class T> int which(T, typename T::other * = 0) { return 2; }
template <class T> typename T::type twice(T) { return 3; }
template <class T> typename T::other twice(T) { return 'x'; }

int main() {
    A a;
    C c;
    printf("%d %d %d %d\n", which(a), which(c), twice(a), twice(c));
    return 0;
}
