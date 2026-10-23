// A mem-initialiser's temporaries are destroyed once its member is built ([class.temporary]/4, each entry a
// full-expression). The list is read whole before the members are built in declaration order, and the
// first flush destroyed every entry's temporaries - including those of members not built yet, whose slots
// held whatever was there. Found as cxx3 compiled by itself crashing in Optimizer's constructor, whose
// `pipeline_(opt::pipelineFor())` had its result deleted before the call.
extern "C" int printf(const char *, ...);
int built, gone, unbuilt;
struct H {
    int magic, v;
    explicit H(int x);
    H(H &&o);
    ~H();
};
H::H(int x) : magic(0x5a5a), v(x) { built++; }
H::H(H &&o) : magic(0x5a5a), v(o.v) { o.v = -1; built++; }
H::~H() { if (magic != 0x5a5a) unbuilt++; else gone++; magic = 0; }
H makeH(int x) { return H(x); }
static int dirty(int k) {
    int junk[256];
    for (int i = 0; i < 256; i++) junk[i] = i + k;
    return junk[k];
}
struct O {
    H a;
    int k = 7;
    H b;
    int m;
    H c;
    explicit O(int s);
};
O::O(int s) : a(makeH(s)), b(makeH(s + 1)), m(s * 2), c(H(s + 2)) {}
int main() {
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        dirty(i);
        O o(i);
        sum += o.a.v + o.k + o.b.v + o.m + o.c.v;
    }
    printf("%d %d %d\n", sum, built - gone, unbuilt);
    return 0;
}
