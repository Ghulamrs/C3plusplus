// The shapes of try/catch the SSA lowering takes (M3b.2, the exception growth): scalars thrown from a callee and caught
// by value, by reference and by catch(...); a handler left by return, break and continue (the catch ended first); a
// try inside a try's body, inside a handler, and a handler that throws out to an enclosing try; a try nothing throws
// through. The functions here hold no object a destructor would destroy, so every landing pad comes from a try.
extern "C" int printf(const char *, ...);

static int thrower(int what) {
    if (what == 1) throw 7;
    if (what == 2) throw 2.5;
    if (what == 3) throw 'c';
    return what * 10;
}

static int byValue(int what) {
    int r = -1;
    try {
        r = thrower(what);
    } catch (int e) {
        r = 100 + e;
    } catch (double d) {
        r = 200 + (int)(d * 2);
    } catch (...) {
        r = 300;
    }
    return r;
}

static int byReference(int what) {
    try {
        return thrower(what);
    } catch (const int &e) {
        return 1000 + e;
    } catch (char &c) {
        c = 'x';
        return 2000 + c;
    }
}

static int leftByReturn(int what) {
    try {
        thrower(what);
    } catch (int e) {
        return e + 1;       // ends the catch first
    }
    return 0;
}

static int inLoop() {
    int sum = 0;
    for (int i = 0; i < 6; i++) {
        try {
            sum += thrower(i);
        } catch (int e) {
            sum += e;
            continue;
        } catch (double unused) {
            break;
        } catch (...) {
            sum += 1;
        }
        sum += 1000;
    }
    return sum;
}

static int nestedInBody(int what) {
    int r = 0;
    try {
        try {
            r = thrower(what);
        } catch (int e) {
            r = 11;
        }
        r += 1;
    } catch (double) {
        r = 22;
    } catch (...) {
        r = 33;
    }
    return r;
}

static int nestedInHandler(int what) {
    int r = 0;
    try {
        thrower(1);
    } catch (int e) {
        try {
            r = thrower(what);
        } catch (double unused) {
            r = 44 + e;
        }
    }
    return r;
}

static int throwsOutOfHandler(int what) {
    int r = 0;
    try {
        try {
            thrower(1);
        } catch (int e) {
            r = thrower(what);      // 2 throws a double past this handler
        }
    } catch (double d) {
        r = 55 + (int)d;
    }
    return r;
}

static int callAfterTry(int what) {
    try {
        thrower(0);
    } catch (int e) {
        return e;
    }
    return thrower(what);       // outside every try: a throw here leaves the function through the table's gap
}

static int throughGap(int what) {
    try {
        return callAfterTry(what);
    } catch (int e) {
        return 66 + e;
    }
}

static int quiet(int n) {
    try {
        n = n * 2;
    } catch (int unused) {
        n = -1;
    }
    return n;
}

int main() {
    printf("%d %d %d %d\n", byValue(0), byValue(1), byValue(2), byValue(3));
    printf("%d %d %d\n", byReference(0), byReference(1), byReference(3));
    printf("%d %d\n", leftByReturn(0), leftByReturn(1));
    printf("%d\n", inLoop());
    printf("%d %d %d %d\n", nestedInBody(0), nestedInBody(1), nestedInBody(2), nestedInBody(3));
    printf("%d %d\n", nestedInHandler(0), nestedInHandler(2));
    printf("%d %d\n", throwsOutOfHandler(0), throwsOutOfHandler(2));
    printf("%d %d\n", throughGap(0), throughGap(1));
    printf("%d\n", quiet(21));
    return 0;
}
