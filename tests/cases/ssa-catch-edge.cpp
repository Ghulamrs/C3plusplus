// An exception an inner try's handlers do not take goes on to the enclosing try's: on Microsoft that way is the inner
// catchswitch's enclosing pad, an edge no succ list holds. A value loaded before such a try, changed in an outer handler
// and read after it; a store seen only by the outer handler; and the same from a try inside a handler.
extern "C" int printf(const char *, ...);

static int seen = 1;

static int thrower(int what) {
    if (what == 1) throw 7;
    if (what == 2) throw 2.5;
    return what * 10;
}

static int loadAcross(int what) {
    int before = seen;
    try {
        try {
            thrower(what);
        } catch (int) {
            seen = 11;
        }
        before += seen;
    } catch (double) {
        seen = 22;
    }
    return before * 100 + seen;
}

static int localAcross(int what) {
    int r = seen;
    try {
        try {
            r = thrower(what);
        } catch (int) {
            r = 11;
        }
        r += 1;
    } catch (double) {
        r = 22;
    }
    return r;
}

static int storeSeenOutside(int what) {
    int r = 0;
    try {
        r = 5;
        try {
            thrower(what);
            r = 1;
        } catch (int) {
            r = 2;
        }
        r = 3;
    } catch (double) {
        return r;
    }
    return r;
}

static int inHandler(int what) {
    int r = 4;
    try {
        try {
            thrower(1);
        } catch (int) {
            r = 6;
            try {
                thrower(what);
                r = 7;
            } catch (int) {
                r = 8;
            }
            r = 9;
        }
    } catch (double) {
        r += 30;
    }
    return r;
}

int main() {
    printf("%d %d %d\n", loadAcross(0), loadAcross(1), loadAcross(2));
    seen = 1;
    printf("%d %d %d\n", localAcross(0), localAcross(1), localAcross(2));
    printf("%d %d %d\n", storeSeenOutside(0), storeSeenOutside(1), storeSeenOutside(2));
    printf("%d %d\n", inHandler(0), inHandler(2));
    return 0;
}
