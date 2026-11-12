// Postfix ++ and -- on a bit-field: refused by name ("not supported yet"), where [expr.post.incr] allows it; the
// prefix form works (M3b.7 rounds 6 B, 7 A).
extern "C" int printf(const char *, ...);
struct F { unsigned int a : 3; int c : 5; };
int main() {
    F f = { 6, -3 };
    int was = f.a++;
    int then = f.a++;
    int c = f.c--;
    printf("%d %d %d %d %d\n", was, then, static_cast<int>(f.a), c, static_cast<int>(f.c));
    return 0;
}
