// std::sort (L02) is O(n log n): 200000 elements in the shapes that undo a naive quicksort -
// random, sorted, reversed, all equal, organ pipe, few distinct values - plus a comparator and a
// class element. Each is checked sorted and summed; the insertion sort it replaced would spend
// billions of steps here and meet the suite's time limit. clang prints the same lines.
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>

static unsigned seed = 12345;
static int next() { seed = seed * 1103515245u + 12345u; return (int)((seed >> 8) % 1000000); }

static void check(const char *name, std::vector<int> &v) {
    std::sort(v.begin(), v.end());
    long long sum = 0;
    int ok = 1;
    for (std::size_t i = 0; i < v.size(); i++) {
        sum += v[i];
        if (i && v[i - 1] > v[i]) ok = 0;
    }
    std::printf("%s %d %lld\n", name, ok, sum);
}

struct ByDigits { bool operator()(int a, int b) const { return a % 1000 < b % 1000 || (a % 1000 == b % 1000 && a < b); } };

int main() {
    const int n = 200000;
    std::vector<int> v;
    for (int i = 0; i < n; i++) v.push_back(next());
    std::vector<int> w = v;
    check("random", v);
    check("sorted", v);
    std::reverse(v.begin(), v.end());
    check("reversed", v);
    std::vector<int> same(n, 7);
    check("equal", same);
    std::vector<int> pipe;
    for (int i = 0; i < n / 2; i++) pipe.push_back(i);
    for (int i = n / 2; i > 0; i--) pipe.push_back(i);
    check("pipe", pipe);
    std::vector<int> few;
    for (int i = 0; i < n; i++) few.push_back(next() % 3);
    check("few", few);
    std::sort(w.begin(), w.end(), ByDigits());
    int ok = 1;
    for (int i = 1; i < n; i++) if (ByDigits()(w[i], w[i - 1])) ok = 0;
    std::printf("comparator %d %d %d\n", ok, w[0], w[n - 1]);
    std::vector<std::string> s;
    for (int i = 0; i < 2000; i++) s.push_back(std::to_string(next()));
    std::sort(s.begin(), s.end());
    ok = 1;
    for (std::size_t i = 1; i < s.size(); i++) if (s[i] < s[i - 1]) ok = 0;
    std::printf("strings %d %s %s\n", ok, s.front().c_str(), s.back().c_str());
    return 0;
}
