extern "C" int printf(const char *, ...);
struct CT { int v; int which; template <class U> CT(U u) : v((int)u), which(3) {} CT(int, int) : which(4) {} };
struct DCT : CT { using CT::CT; };
int main() { DCT c(3, 4); printf("%d\n", c.which); DCT d(7.9); printf("%d %d\n", d.v, d.which); return 0; }
