// [dcl.init]/17 and [class.copy]/7: `T t = u;` with u a T-derived object copies the T part
// through T's implicit copy constructor - slicing, ordinary C++98. cpp11 refuses it for a
// class with no copy constructor written ("'t' is 'struct T' and this is 'struct U'"), naming
// no feature. Found 2026-10-18 probing anonymous unions; it needs no union to show.
extern "C" int printf(const char *, ...);
struct T { char tag; int i; };
struct U : T { int j; };
int main() {
    U u; u.tag = 'q'; u.i = 3; u.j = 9;
    T t = u;
    T w; w = u;
    printf("%c %d %c %d\n", t.tag, t.i, w.tag, w.i);
    return 0;
}
