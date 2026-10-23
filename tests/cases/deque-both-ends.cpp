// <deque> at both ends, past several blocks, with a reference held across the pushes; and beside it a map's and a
// set's range erase, make_shared with arguments and without, and remove_if (M1).
#include <deque>
#include <string>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <vector>
#include <cstdio>
struct P { int a; std::string b; P(int x, const char *y) : a(x), b(y) {} };
int main() {
    std::deque<std::string> q;
    for (int i = 0; i < 300; i++) { q.push_back("b" + std::to_string(i)); q.push_front("f" + std::to_string(i)); }
    std::string &keep = q[300];
    for (int i = 0; i < 50; i++) { q.pop_front(); q.pop_back(); q.push_back("x"); }
    std::string acc;
    while (!q.empty() && acc.size() < 12) { acc += q.front(); q.pop_front(); }
    std::deque<int> d = { 1, 2, 3 };
    d.push_front(0);
    long s = 0;
    for (std::deque<int>::iterator it = d.begin(); it != d.end(); ++it) s += *it * 10;
    std::map<int, int> m;
    for (int i = 0; i < 20; i++) m[i] = i;
    m.erase(m.lower_bound(5), m.lower_bound(15));
    std::set<int> st;
    for (int i = 0; i < 10; i++) st.insert(i);
    st.erase(st.find(2), st.find(7));
    std::shared_ptr<P> sp = std::make_shared<P>(7, "seven");
    std::shared_ptr<int> si = std::make_shared<int>();
    std::vector<int> v = { 1, 2, 3, 4, 5, 6 };
    v.erase(std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; }), v.end());
    printf("%s %s %d %ld %d %d %d %s %d %d %d\n", keep.c_str(), acc.c_str(), (int)d.size(), s, (int)m.size(),
           (int)st.size(), sp->a, sp->b.c_str(), *si, (int)v.size(), v[2]);
    return 0;
}
