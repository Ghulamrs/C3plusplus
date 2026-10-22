// A constructor template is a candidate by its signature; its body is
// instantiated only if overload resolution chooses it ([temp.inst]/3). Ranking
// pair<pair<int, int>, int>'s constructors deduces pair(const pair<U1, U2> &)
// from the inner pair, whose body cannot build the outer one - and that must
// not be an error, since the plain constructor is the one chosen.
#include <map>
#include <string>
#include <utility>
#include <vector>
extern "C" int printf(const char *, ...);

template <class T> struct W {
    int v;
    W(int x) : v(x) {}
    template <class U> W(const U &u) : v(u.missing) {}
};

int main() {
    std::vector<std::pair<std::string, std::string> > env;
    env.push_back(std::make_pair("__DATE__", std::string("Oct 23 2026")));
    std::pair<long, double> p = std::make_pair(7, 2.5f);
    std::pair<std::pair<int, int>, int> nest(std::pair<int, int>(1, 2), 3);
    std::pair<std::pair<int, int>, int> again(nest);
    std::map<std::pair<unsigned long, unsigned long>, int> m;
    m[std::make_pair(4ul, 5ul)] = 9;
    W<int> w(3);
    printf("%s=%s %ld %g %d %d %d %d %d\n", env[0].first.c_str(), env[0].second.c_str(),
           p.first, p.second, nest.first.second, again.second,
           m[std::pair<unsigned long, unsigned long>(4, 5)], w.v, (int)m.size());
    return 0;
}
