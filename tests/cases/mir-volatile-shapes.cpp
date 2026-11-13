// R8's audit, round 2: volatile shapes round 1 did not hold to MIR -O2 - a bit-field written and read in a loop, a
// bit-field read twice in a callee, a volatile object copied out member by member. (Copied whole it is ill-formed in
// C++11 - the implicit copy constructor takes `const T &` - and a reference to volatile is refused by name here.)
extern "C" int printf(const char *, ...);

struct Bits { unsigned a : 3; unsigned b : 5; int c; };
struct Plain { int x, y; };
static volatile Bits vb;   // internal: cl decorates an external volatile name (volatile-object.notarget)

static int readTwice() { return vb.b + vb.b; }

int main() {
    vb.a = 5;
    vb.b = 17;
    vb.c = 3;
    int s = 0;
    for (int i = 0; i < 4; i++) { vb.a = i; s += vb.a; }
    volatile Plain vp;
    vp.x = 1;
    vp.y = 2;
    Plain p;
    p.x = vp.x;
    p.y = vp.y;
    printf("%d %d %d %d %d\n", s, readTwice(), vb.c, p.x + p.y, vb.a);
    return 0;
}
