// The function-template half: T must deduce as int. clang: 6 4.
extern "C" int printf(const char *, ...);
namespace n {
    typedef double T;
    template <class T> T twice(T x) { return x + x; }
    int run() { printf("%d %d\n", twice(3), (int)sizeof(twice(3))); return 0; }
}
int main() { return n::run(); }
