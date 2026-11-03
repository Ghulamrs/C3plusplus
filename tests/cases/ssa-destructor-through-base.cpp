// A destructor called by name ends the life of the subobject it names, [class.dtor]/14: `p->~Base()` with Base a
// typedef of a base, and `q->B::~B()`, destroy the B alone; `p->~T()` in a template, and `r->~D()`, the whole D.
// The SSA lowering takes the call where the destructor the parser called is the one the object's class has.
#include <new>
extern "C" int printf(const char *, ...);
struct B { int b; ~B() { printf("~B "); } };
struct D : B { int d; ~D() { printf("~D "); } };
typedef B Base;

static void viaTypedef(void *buf) { D *p = new (buf) D; p->~Base(); }
static void viaQualified(void *buf) { D *q = new (buf) D; q->B::~B(); }
static void viaOwn(void *buf) { D *r = new (buf) D; r->~D(); }
template <class T> static void destroy(T *t) { t->~T(); }

int main() {
    int buf[(sizeof(D) + sizeof(int) - 1) / sizeof(int)];     // D holds ints alone
    viaTypedef(buf);
    printf("| ");
    viaQualified(buf);
    printf("| ");
    viaOwn(buf);
    printf("| ");
    destroy(new (static_cast<void *>(buf)) D);
    printf("\n");
    return 0;
}
