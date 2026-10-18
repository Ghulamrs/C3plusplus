// An anonymous union in a block: its members are locals sharing one frame
// slot, given back once when the scope closes; a static one is one static
// object under the members' names. A nested block declares its own, hiding
// the outer's. A lambda cannot capture one - clang refuses it, and so does
// this; anonymous-union-lambda-capture-refused.cpp is that case.
extern "C" int printf(const char *, ...);

static int twice() {
    static union { int count; double unused; };
    count += 2;
    return count;
}

int main() {
    union { int a; float f; };
    a = 0x3f800000;
    printf("%g %d\n", f, (int)sizeof(a));
    {
        union { int a; double d; };
        a = 5;
        printf("%d\n", a);
        d = 0.5;
        printf("%g %d\n", d, (int)sizeof(a));
    }
    printf("%d\n", a == 0x3f800000);
    int first = twice();
    int second = twice();
    printf("%d %d\n", first, second);
    return 0;
}
