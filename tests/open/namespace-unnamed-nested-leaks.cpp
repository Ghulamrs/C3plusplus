// An unnamed namespace inside `n` is n's by its using-directive, not the file's: the global `f` is a function of its
// own. cxx1 keeps the directive for the rest of the file at every scope and takes the global definition for n's.
#include <cstdio>
namespace n {
namespace {
int f(int a, int b) { return a + b; }
}
}
int f(int a, int b) { return a - b; }
int main() {
    std::printf("%d %d\n", f(9, 2), n::f(9, 2));
    return 0;
}
