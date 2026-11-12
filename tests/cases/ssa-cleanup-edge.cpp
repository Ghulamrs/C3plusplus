// A destructor's cleanup inside a handler, with no pad of the handler's own around it: on Microsoft its cleanupret goes
// where the handler unwinds - the enclosing try's catchswitch, or the caller - not back to the handler's entry. A value
// changed in the handler before a throw and read in the outer handler; a global the same; two guards, chained.
extern "C" int printf(const char *, ...);

static int seen = 1, gone = 0;

struct Guard {
    ~Guard();
};

Guard::~Guard() { gone++; }

static int thrower(int what) {
    if (what == 1) throw 7;
    if (what == 2) throw 2.5;
    return what * 10;
}

static int localThrough(int what) {
    int r = 1;
    try {
        try {
            thrower(1);
        } catch (int) {
            Guard g;
            r = 5;
            thrower(what);
            r = 6;
        }
        r += 10;
    } catch (double) {
        r += 100;
    }
    return r;
}

static int globalThrough(int what) {
    int before = seen;
    try {
        try {
            thrower(1);
        } catch (int) {
            Guard g;
            seen = 9;
            thrower(what);
            seen = 4;
        }
    } catch (double) {
        before += seen * 10;
    }
    return before;
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

static void toCaller(int what) {
    try {
        thrower(1);
    } catch (int) {
        Guard g;
        seen = 30;
        thrower(what);
        seen = 40;
    }
}

int main() {
    printf("%d %d\n", localThrough(0), localThrough(2));
    seen = 1;
    const int a = globalThrough(0);
    seen = 1;
    const int b = globalThrough(2);
    printf("%d %d %d\n", a, b, seen);
    const int c = twoGuards(0), d = twoGuards(2);
    printf("%d %d %d\n", c, d, gone);
    try {
        toCaller(2);
    } catch (double) {
        printf("caught %d\n", seen);
    }
    printf("gone %d\n", gone);
    return 0;
}
