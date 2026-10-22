// [over.match.oper]/3.2: where no operand has class type, a non-member operator is a candidate only
// if a parameter takes an operand's enumeration - so `e == X` is the built-in comparison, not a
// call through a class that converts from anything.
extern "C" int printf(const char *, ...);
struct Any { template <class T> Any(T) {} };
static bool operator==(const Any &, const Any &) { return false; }
enum E { X, Y };
enum F { P, Q };
static int operator+(F a, int b) { return 100 * (int)a + b; }
int main() {
    E e = X;
    F f = Q;
    Any any(3);
    printf("%d %d %d %d\n", (int)(e == X), (int)(e == Y), f + 5, (int)(any == any));
    return 0;
}
