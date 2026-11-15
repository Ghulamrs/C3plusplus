// Objects and functions spelled like C6000 registers (R114): each is a symbol wherever it is written.
extern "C" int printf(const char *, ...);

int A4[4] = { 1, 2, 3, 4 };
int A31 = 7;
int a4 = 5;
int *B31 = &A4[2];
static int B0 = 40;

extern "C" int B3(int x);
extern "C" int A10(int x);
int B3(int x) { return x + A31; }
int A10(int x) { return x * a4 + B0; }
int (*A0)(int) = B3;

extern "C" int A3(int x) {
    int r = 0;
    try {
        if (x > 2) throw x;
        r = B3(x);
    } catch (int e) {
        r = A10(e);
    }
    return r;
}

int main() {
    int s = 0;
    for (int i = 0; i < 4; i++) s += A4[i];
    printf("%d %d %d\n", s, B3(A4[2]), A10(A4[1]));
    printf("%d %d %d\n", *B31, B31[1], A0(a4));
    A31 = A31 + a4;
    a4 = B3(1);
    B0 = B0 + 1;
    printf("%d %d %d %d\n", A31, a4, A3(1), A3(3));
    return 0;
}
