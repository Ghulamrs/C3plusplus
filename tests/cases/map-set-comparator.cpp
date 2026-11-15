// A map and a set ordered by a comparator named as their last template argument, and the default one named.
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
struct Backwards {
    bool operator()(int a, int b) const { return b < a; }
};
typedef std::map<int, std::string, Backwards> Down;
int main() {
    Down d;
    d[1] = "one"; d[3] = "three"; d[2] = "two";
    for (Down::const_iterator it = d.begin(); it != d.end(); ++it) std::printf("%d %s\n", it->first, it->second.c_str());
    std::map<int, int, std::less<int> > up;
    up[5] = 50; up[4] = 40;
    std::map<int, int> same = up;
    std::printf("%d %d %d %d\n", (int)d.count(3), (int)d.count(4), same.begin()->second, (int)(same == up));
    std::set<int, Backwards> s;
    s.insert(4); s.insert(9); s.insert(6);
    for (std::set<int, Backwards>::const_iterator it = s.begin(); it != s.end(); ++it) std::printf("%d ", *it);
    std::printf("%d\n", (int)s.count(6));
    return 0;
}
