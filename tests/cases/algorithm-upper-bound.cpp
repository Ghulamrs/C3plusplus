// std::upper_bound (M5.4): the first element greater than the value, over empty, one-element and
// repeated runs, at both ends and between; arm64's selector calls it, so the self-host reads it too.
#include <algorithm>
#include <vector>
#include <cstdio>

static void at(const std::vector<int> &v, int x) {
    std::vector<int>::const_iterator i = std::upper_bound(v.begin(), v.end(), x);
    std::printf("%d -> %d%s\n", x, (int)(i - v.begin()), i == v.end() ? " end" : "");
}

int main() {
    std::vector<int> none;
    at(none, 3);
    std::vector<int> one(1, 5);
    at(one, 4); at(one, 5); at(one, 6);
    int raw[] = {1, 2, 2, 2, 4, 7, 7, 9};
    std::vector<int> v(raw, raw + 8);
    for (int x = 0; x <= 10; x++) at(v, x);
    const int *p = std::upper_bound(raw, raw + 8, 2);
    std::printf("pointer %d\n", (int)(p - raw));
    return 0;
}
