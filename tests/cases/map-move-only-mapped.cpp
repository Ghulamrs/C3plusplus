// [map.access]/1: `operator[]` asks only that the mapped type be default-constructible - a vector of
// unique_ptr, or a unique_ptr itself - and the element was made by copying a default value (M1, the
// compiler's own ParserTopLevel.cpp). It is moved in now, in map and in unordered_map alike.
#include <map>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>
#include <cstdio>
int main() {
    std::map<std::string, std::vector<std::unique_ptr<int> > > m;
    m["a"].push_back(std::unique_ptr<int>(new int(3)));
    m["a"].push_back(std::unique_ptr<int>(new int(4)));
    m["b"];
    std::unordered_map<int, std::unique_ptr<int> > u;
    u[7] = std::unique_ptr<int>(new int(9));
    u[7].reset(new int(10));
    std::map<int, int> c;
    c[1] = 2;
    c[1] += 3;
    printf("%d %d %d %d %d %d\n", (int)m["a"].size(), *m["a"][1], *u[7], (int)m.size(), c[1], (int)c.size());
    return 0;
}
