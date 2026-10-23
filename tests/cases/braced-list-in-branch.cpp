// [dcl.init.list]/6: a braced list's backing array made inside an expression lives to the end of that full
// expression. It was destroyed at the end of the enclosing scope instead - on every path, so a branch that
// never built the array destroyed it all the same (M1: stage 2's C6000 address folding, whose
// `rebuilt("MV", std::vector<std::string>{ I, T })` sat in an `else if`).
#include <cstdio>
#include <string>
#include <vector>

static int built, gone;
struct Tag {
    int v;
    Tag(int n) : v(n) { built++; }
    Tag(const Tag &o) : v(o.v) { built++; }
    ~Tag() { gone++; }
};

static int total(const std::vector<std::string> &v) {
    int n = 0;
    for (std::size_t i = 0; i < v.size(); i++) n += (int)v[i].size();
    return n;
}
static int sum(const std::vector<Tag> &v) {
    int n = 0;
    for (std::size_t i = 0; i < v.size(); i++) n += v[i].v;
    return n;
}

static int pick(int k, const std::string &a, const std::string &b) {
    int r = 0;
    if (k == 0) r = 1;
    else if (k == 1) r = total(std::vector<std::string>{ a, b });
    else r = total({ a, "a literal long enough to live on the heap" });
    return r;
}

static int tags(int k) {
    if (k > 1) return sum({ Tag(k), Tag(2 * k) });
    return 0;
}

int main() {
    std::string a = "a first string long enough for the heap", b = "second";
    int s = 0, t = 0;
    for (int i = 0; i < 6; i++) s += pick(i % 3, a, b);
    for (int i = 0; i < 4; i++) t += tags(i);
    std::printf("%d %d\n", s, t);
    std::printf("%d\n", built - gone);
    return 0;
}
