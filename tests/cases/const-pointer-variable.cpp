// A const pointer object is `Q` in a Microsoft name where a plain one is `P` - cl's `?cp@@3QEADEA`
// for `char *const cp` - and cxx1 wrote `P` (found by M1's constexpr pointer, 2026-10-23).
extern "C" int printf(const char *, ...);
char text[] = "abc";
extern char *const cp;
char *const cp = text;
const char *const ccp = text + 1;
struct S { static char *const sp; };
char *const S::sp = text + 2;
const char *const *where() { return &ccp; }
int main() {
    printf("%c %c %c %c\n", *cp, *ccp, *S::sp, **where());
    return 0;
}
