// M3b.7 r8b, r9b: class locals a lambda initialises (C6xPipe's emitWindow, giveRegisters) and a braced return of a struct.
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

struct R { std::string text; bool a, b, c; };
struct C {
    std::string text;
    bool m, i;
    C(std::string t = std::string(), bool m = false, bool i = false);
};
C::C(std::string t, bool m_, bool i_) : text(t), m(m_), i(i_) {}

static C made(int k) {
    if (k < 0) return {};       // r9b: MasmSpelling::render's value-initialised return, by a constructor of defaults
    if (k == 0) return { std::string("none"), false, true };
    return { std::to_string(k * 3), k > 1, k > 2 };
}

static R render(int k) {
    if (k == 0) return { std::string("zero"), false, true, false };
    std::string v = std::to_string(k);
    return { v, k > 1, true, k > 2 };
}

static int window(std::vector<std::string> pool[2]) {
    auto isFree = [&](int s, int k) { return std::find(pool[s].begin(), pool[s].end(), std::string(s ? "B" : "A") + std::to_string(k)) != pool[s].end(); };
    auto preds = [&](int s) { int n = 0; for (int k = 0; k < 3; k++) if (isFree(s, k)) n++; return n; };
    return preds(0) * 10 + preds(1);
}

static int give(const std::vector<std::string> &ops) {
    auto each = [&](const std::string &op, const std::function<void(int)> &f) {
        for (std::size_t i = 0; i < op.size(); i++) if (op[i] == '%') f(op[i + 1] - '0');
    };
    int sum = 0;
    for (std::size_t o = 0; o < ops.size(); o++) each(ops[o], [&](int n) { sum += n; });
    return sum;
}

// r9b: a local array of classes captured by reference - emitWindow's pool, giveRegisters' freeList.
static int local(int k) {
    std::vector<std::string> pool[2];
    pool[0].push_back("A0"); pool[1].push_back("B" + std::to_string(k));
    auto isFree = [&](int s, int n) { return std::find(pool[s].begin(), pool[s].end(), std::string(s ? "B" : "A") + std::to_string(n)) != pool[s].end(); };
    int ints[2] = { 4, 5 };
    auto pick = [&](int s) { return ints[s]; };
    int sum = 0;
    auto each = [&](const std::function<void(int)> &f) { f(1); f(k); };
    each([&](int n) { pool[1].push_back("B" + std::to_string(n)); sum += n; });
    return (isFree(0, 0) ? 1000 : 0) + (isFree(1, 2) ? 100 : 0) + pick(1) * 10 + sum + static_cast<int>(pool[1].size());
}

int main() {
    for (int k = 0; k < 4; k++) { R r = render(k); std::printf("%s %d %d %d\n", r.text.c_str(), r.a, r.b, r.c); }
    for (int k = -1; k < 4; k++) { C c = made(k); std::printf("%s %d %d\n", c.text.c_str(), c.m, c.i); }
    std::vector<std::string> pool[2];
    pool[0].push_back("A0"); pool[0].push_back("A2"); pool[1].push_back("B1");
    std::printf("window %d\n", window(pool));
    std::vector<std::string> ops; ops.push_back("%1,%2"); ops.push_back("%3");
    std::printf("give %d\n", give(ops));
    std::printf("local %d %d\n", local(2), local(3));
    return 0;
}
