// `new (p) T()` value-initialises, and for a scoped enumeration that is its own zero - the int zero
// it was built from has no conversion to one. It is `include/vector`'s resize, so no
// `vector<enum class>` could grow (M1 L17, Fable's review).
#include <new>
#include <vector>
extern "C" int printf(const char *, ...);
enum class Kind { A, B, C };
enum Plain { P0, P1 };
int main() {
    Kind arr[2] = {Kind::C, Kind::B};
    Kind *p = arr;
    new (p) Kind();
    Kind *heap = new Kind();
    Kind *many = new Kind[3]();
    Plain *pl = new Plain();
    std::vector<Kind> v;
    v.resize(3);
    v[1] = Kind::B;
    printf("%d %d %d %d %d %d %d %d\n", (int)arr[0], (int)arr[1], (int)*heap, (int)many[2], (int)*pl,
           (int)v.size(), (int)v[0], (int)v[1]);
    delete heap;
    delete[] many;
    delete pl;
    return 0;
}
