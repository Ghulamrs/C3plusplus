// A thread_local defined in another translation unit, reached through what
// the ABI gives for one it cannot see the initialiser of: Itanium's `_ZTW`
// wrapper (initial-exec through the GOT, `_ZTH` called if the link found one),
// the Microsoft CRT's `__tls_guard` and `__dyn_tls_on_demand_init`. The other
// unit builds `counted` with a constructor, so the wrapper's call is what runs
// it in this thread before the first read; `plain` is constant-initialised
// there and reached the same way, since this unit cannot tell.
extern "C" int printf(const char *, ...);

struct Counted {
    int value;
    Counted(int v);
    ~Counted();
};

extern thread_local Counted counted;
extern thread_local int plain;
namespace other { extern thread_local long wide; }

int readTwice() {
    const int first = counted.value;
    plain += 1;
    other::wide += 10;
    return first + counted.value;
}

int main() {
    printf("%d\n", readTwice());
    printf("%d %ld\n", plain, other::wide);
    {
        extern thread_local int plain;
        plain += 100;
    }
    printf("%d\n", plain);
    return 0;
}
