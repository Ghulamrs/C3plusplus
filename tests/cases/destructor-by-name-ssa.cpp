// M3b.2: [class.dtor]/14 lowered to SSA - a class's own destructor called by name on an object in storage the program
// manages, through `->` and `.`, its effect seen; a scalar's and a plain class's pseudo-destructors beside it.
#include <new>
extern "C" int printf(const char *, ...);

static int ended = 0;
struct Logged { int id; ~Logged() { ended += id; } };
struct Plain { int a; };
static void endPointer(Logged *p) { p->~Logged(); }
static void endObject(Logged &r) { r.~Logged(); }
static void endPlain(Plain *p) { p->~Plain(); }

int main() {
    alignas(8) unsigned char buf[32];
    Logged *p = new (buf) Logged();
    p->id = 5;
    endPointer(p);
    Logged *q = new (buf + 8) Logged();
    q->id = 20;
    endObject(*q);
    Plain *r = new (buf + 16) Plain();
    endPlain(r);
    printf("%d\n", ended);
    return 0;
}
