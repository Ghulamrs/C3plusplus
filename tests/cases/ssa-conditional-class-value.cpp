// A conditional of two class objects ([expr.cond]/6) as a return's value and as a local's initialiser, the class one with
// constructors and nothing to destroy; and members whose names only begin with `template`.
#include <stdio.h>

struct Id { unsigned v; Id(); explicit Id(unsigned value); bool valid() const { return v != 0; } };
Id::Id() : v(0) {}
Id::Id(unsigned value) : v(value) {}
struct Box { Id held; int templates_; int templateName_; Id id() const { return held; } };

Id make(unsigned x) { return Id(x); }
Id ofBox(const Box *b) { return b == 0 ? Id() : b->id(); }
Id either(bool c, unsigned x) { return c ? make(x) : Id(); }
Id lvalueArm(bool c, const Id &a) { return c ? a : Id(9); }
unsigned local(bool c, unsigned x) {
    const Id i = c ? make(x) : Id();
    return i.v;
}
int names(const Box &b, Box *p) {
    p->templates_ += 1;
    return b.templates_ + b.templateName_ + p->templateName_;
}

int main() {
    Box b;
    b.held = Id(7);
    b.templates_ = 3;
    b.templateName_ = 4;
    const Id k(5);
    printf("%u %u\n", ofBox(0).v, ofBox(&b).v);
    printf("%u %u\n", either(true, 2).v, either(false, 2).v);
    printf("%u %u\n", lvalueArm(true, k).v, lvalueArm(false, k).v);
    printf("%u %u\n", local(true, 6), local(false, 6));
    printf("%d %d\n", names(b, &b), b.templates_);
    return 0;
}
