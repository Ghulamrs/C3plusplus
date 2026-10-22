// thread_local - [dcl.stc]/4, [basic.stc.thread]: one object per thread, at
// namespace scope, as a static data member and in a block, reached from a
// second thread whose copies start fresh. A constant initialiser is laid down
// in the TLS template (.tdata/.tbss, __thread_data, .tls$); a constructor or a
// call runs once per thread, before the first use in that thread (Itanium's
// _ZTW/_ZTH wrappers and __cxa_thread_atexit, dyld's _tlv_atexit, the
// Microsoft CRT's __tls_guard and __tlregdtor) - and the destructor runs when
// that thread ends, which the ledger counts: two threads, two D's, both gone.
// The second thread is made with the host's own call, under its own name.
#include "lifetime.h"

#ifdef _WIN32
extern "C" void *CreateThread(void *, unsigned long long, unsigned long (*)(void *),
                              void *, unsigned long, unsigned long *);
extern "C" unsigned long WaitForSingleObject(void *, unsigned long);
#else
extern "C" int pthread_create(void **, const void *, void *(*)(void *), void *);
extern "C" int pthread_join(void *, void **);
#endif

struct D {
    int v;
    D(int i);
    ~D();
};
D::D(int i) : v(i) { printf("D(%d)\n", i); lfBuilt(this, "D"); }
D::~D() { printf("~D(%d)\n", v); lfGone(this, "D"); }

int nextSeed();

thread_local int counter = 100;
thread_local int zeroed;
static thread_local long internal = 7;
namespace ns { thread_local int inNamespace = 3; }
struct Holder { static thread_local int member; };
thread_local int Holder::member = 40;
thread_local D built(nextSeed());
thread_local int fromCall = nextSeed() * 10;

int seeds = 0;
int nextSeed() { return ++seeds; }

int bump() {
    static thread_local int calls = 1000;
    thread_local int implicitlyStatic = 0;
    implicitlyStatic += 5;
    return calls++ + implicitlyStatic;
}

void work(const char *who) {
    counter += 1;
    zeroed += 2;
    internal += 1;
    ns::inNamespace += 1;
    Holder::member += 1;
    const int first = bump();
    const int second = bump();
    printf("%s: counter %d zeroed %d internal %ld ns %d member %d bump %d %d built %d fromCall %d\n",
           who, counter, zeroed, internal, ns::inNamespace, Holder::member, first, second, built.v, fromCall);
}

#ifdef _WIN32
unsigned long other(void *) { work("thread"); return 0; }
#else
void *other(void *) { work("thread"); return 0; }
#endif

int main() {
    lfWatch();
    work("main");
#ifdef _WIN32
    void *h = CreateThread(0, 0, other, 0, 0, 0);
    WaitForSingleObject(h, 0xFFFFFFFFUL);
#else
    void *t;
    pthread_create(&t, 0, other, 0);
    pthread_join(t, 0);
#endif
    work("main");
    printf("seeds %d\n", seeds);
    return 0;
}
