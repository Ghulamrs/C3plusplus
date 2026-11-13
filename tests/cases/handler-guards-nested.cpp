// Two objects with a destructor in a handler, the inner in a block of its own, and an exception thrown out of the inner
// block: on the Ast.h path for x86_64-linux one destructor does not run (gone 3 for 4); -front=ssa and Windows are right.
extern "C" int printf(const char *, ...);

static int gone = 0;

struct Guard {
    ~Guard();
};

Guard::~Guard() { gone++; }

static int thrower(int what) {
    if (what == 1) throw 7;
    if (what == 2) throw 2.5;
    return what * 10;
}

static int twoGuards(int what) {
    int r = 2;
    try {
        try {
            thrower(1);
        } catch (int) {
            Guard outer;
            r = 3;
            {
                Guard inner;
                r = 4;
                thrower(what);
            }
            r = 5;
        }
    } catch (double) {
        r += 20;
    }
    return r;
}

int main() {
    const int c = twoGuards(0), d = twoGuards(2);
    printf("%d %d %d\n", c, d, gone);
    return 0;
}
