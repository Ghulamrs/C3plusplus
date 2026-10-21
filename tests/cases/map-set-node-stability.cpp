// [associative.reqmts]: an insertion invalidates no iterator and an erasure only the erased
// one, so `m.erase(it++)` walks a map. cppparse's optimizer writes it and crashed when the map
// kept its elements in a vector. A random walk of inserts and erasures, checked as it goes.
#include <map>
#include <set>
#include <cstdio>

static unsigned seed = 12345;
static unsigned next() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; }

int main() {
    std::map<int, int> m;
    std::set<int> s;
    long sum = 0;
    for (int round = 0; round < 3000; round++) {
        int k = (int)(next() % 200);
        if (next() % 3 != 0) {
            std::pair<std::map<int, int>::iterator, bool> r = m.insert(std::make_pair(k, round));
            s.insert(k);
            if (!r.second) r.first->second += 1;
        } else {
            m.erase(k);
            s.erase(k);
        }
        if (round % 500 == 499) {
            for (std::map<int, int>::iterator it = m.begin(); it != m.end();)
                if (it->first % 7 == 0) m.erase(it++); else ++it;
            for (std::set<int>::iterator it = s.begin(); it != s.end();)
                if (*it % 7 == 0) s.erase(it++); else ++it;
        }
    }
    int prev = -1;
    bool ordered = true;
    for (std::map<int, int>::const_iterator it = m.begin(); it != m.end(); ++it) {
        if (it->first <= prev) ordered = false;
        prev = it->first;
        sum += it->first * 3 + it->second;
    }
    std::map<int, int>::iterator lo = m.lower_bound(50), hi = m.upper_bound(150);
    int between = 0;
    for (; lo != hi; ++lo) between++;
    std::set<int> copy(s.begin(), s.end());
    std::map<int, int>::const_iterator found;
    found = m.find(m.begin()->first);
    std::printf("%d %d %d %ld %d %d %d\n", (int)m.size(), (int)s.size(), ordered, sum, between,
                copy == s, found == m.begin());
    std::map<int, int>::iterator last = m.end();
    --last;
    std::printf("%d %d\n", last->first, *--s.end());
    std::map<int, int> mc(m.begin(), m.end());
    bool same = mc == m;
    mc[1000] = 1;
    std::printf("%d %d %d\n", same, mc != m, m < mc);
    return 0;
}
