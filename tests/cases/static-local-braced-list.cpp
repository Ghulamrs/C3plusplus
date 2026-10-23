// [dcl.init.list]/6: the array behind a braced list lives as long as the initializer_list, to the end of
// the full expression that builds the object. A static local built from one destroyed the array at every
// return of its function - including every call after the first, when nothing had built it (M1: stage 2's
// jump threading, whose `static const std::set<std::string> tests = {...}` freed the stack).
#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int built, gone;
struct Word {
    std::string text;
    Word(const char *t) : text(t) { built++; }
    Word(const Word &o) : text(o.text) { built++; }
    ~Word() { gone++; }
    bool operator<(const Word &o) const { return text < o.text; }
};

static int known(const std::string &k) {
    static const std::set<std::string> tests = {"cmp", "cmpb", "a mnemonic long enough to live on the heap"};
    return (int)tests.count(k);
}

static int words() {
    static const std::vector<Word> kept = {"one", "two", "three"};
    return (int)kept.size();
}

static const std::set<std::string> global = {"x", "y", "a global entry long enough to live on the heap"};

int main() {
    int n = 0;
    for (int i = 0; i < 5; i++) n += known("cmpb") + known("jmp") + words();
    std::printf("%d %d\n", n, (int)global.size());
    std::printf("%d\n", built - gone);
    return 0;
}
