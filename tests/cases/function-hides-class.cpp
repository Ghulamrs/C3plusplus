// [basic.scope.hiding]/2: a function of the class's name hides the class, so `st(&s)` beside
// `struct st` is a call - the `struct stat` and `stat()` idiom of <sys/stat.h> - while the
// elaborated `struct st` still names the class.
extern "C" int printf(const char *, ...);
struct st { int size; };
int st(struct st *p) { p->size = 42; return 0; }
int main() {
    struct st s;
    s.size = 0;
    int rc = st(&s);
    printf("%d %d %d\n", rc, s.size, (int)sizeof(struct st));
    return 0;
}
