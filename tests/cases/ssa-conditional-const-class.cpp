// `c ? cv : make()` of one class is a const prvalue, so it binds `const V &` and never `V &&`; a braced aggregate
// thrown (M3b.7 round 6a).
#include <cstdio>
#include <string>
#include <vector>

struct V {
    int v;
};

static V make(int k) {
    V r;
    r.v = k;
    return r;
}

struct Failure {
    std::string message;
    bool unsupported;
    int pos;
};

static void fail(const std::string &m, int pos) {
    throw Failure{ m, pos > 2, pos };
}

struct Refusal {
    std::string why;
    int pos;
    Refusal(const std::string &w, int p) : why(w), pos(p) {}
};

static void refuse(const std::string &m, int pos) {
    throw Refusal{ m, pos };
}

static int which(const V &x) { return 100 + x.v; }
static int which(V &&x) { return 200 + x.v; }

int main(int argc, char **) {
    const V c = make(3);
    V m = make(4);
    std::vector<V> out;
    for (int i = 0; i < 2; i++) {
        out.push_back(argc + i > 1 ? c : make(5));
        out.push_back(argc + i > 1 ? m : make(6));
    }
    std::printf("%d %d %d %d\n", which(argc > 1 ? c : make(5)), which(argc > 1 ? m : make(6)),
                which(argc > 0 ? c : make(7)), which(argc > 0 ? make(8) : c));
    for (const V &x : out) std::printf("%d\n", x.v);
    try {
        fail(std::string("bad"), argc + 2);
    } catch (const Failure &f) {
        std::printf("%s %d %d\n", f.message.c_str(), f.unsupported ? 1 : 0, f.pos);
    }
    try {
        refuse(std::string("no"), argc + 6);
    } catch (const Refusal &r) {
        std::printf("%s %d\n", r.why.c_str(), r.pos);
    }
    return 0;
}
