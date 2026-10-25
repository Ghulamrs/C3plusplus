// [basic.link]/4: a name in an unnamed namespace has internal linkage, an object's as much as a
// function's. This unit and unnamed-namespace-two-units.part.cpp each define `count`, `table` and
// `ns::level` in one; cxx3 exported the objects, and the two units did not link. And clang writes no
// Itanium `L` for a name there, `static` or not: `_ZN12_GLOBAL__N_14bumpEi`, `_ZN12_GLOBAL__N_15sevenE`.
extern "C" int printf(const char *, ...);
int partTotal();

namespace {
unsigned count = 3;
int table[2];
const char *label = "main";
static int bump(int x) { return x + 1; }
static const int seven = 7;
}
namespace ns { namespace { int level; } int levelHere() { return level += 10; } }

int main()
{
    table[1] = 4;
    printf("%s %u %d %d %d\n", label, count, table[1], ns::levelHere(), partTotal() + bump(*&seven));
    return 0;
}
