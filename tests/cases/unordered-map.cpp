// <unordered_map> and <unordered_set>: a hash table of string keys growing past many rehashes, erase by key and by
// iterator during a walk, a copy - sums and counts only, the order of a walk being unspecified.
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdio>
struct Macro { std::string body; int params; Macro() : params(-1) {} };
int main() {
    std::unordered_map<std::string, Macro> macros;
    Macro m; m.body = "1 + 2"; m.params = 0;
    macros["ONE"] = m;
    for (int i = 0; i < 500; i++) { Macro x; x.params = i; macros["M" + std::to_string(i)] = x; }
    long sum = 0;
    for (std::unordered_map<std::string, Macro>::iterator it = macros.begin(); it != macros.end(); ++it) sum += it->second.params;
    std::unordered_map<std::string, Macro>::iterator f = macros.find("ONE");
    int erased = (int)macros.erase("M7");
    std::string rest = "M8";
    macros.erase(rest);
    int c = (int)macros.count("M7") + (int)macros.count("M9");
    std::unordered_map<int, int> sq;
    for (int i = 0; i < 100; i++) sq[i] = i * i;
    for (std::unordered_map<int, int>::iterator it = sq.begin(); it != sq.end(); ) { if (it->first % 2) it = sq.erase(it); else ++it; }
    long sqs = 0;
    for (const std::pair<const int, int> &p : sq) sqs += p.second;
    std::unordered_set<std::string> seen;
    int dup = 0;
    const char *words[] = { "a", "b", "a", "c", "b", "a" };
    for (int i = 0; i < 6; i++) if (!seen.insert(words[i]).second) dup++;
    std::unordered_map<std::string, Macro> copy = macros;
    printf("%ld %s %d %d %d %ld %d %d %d %d\n", sum, f->second.body.c_str(), erased, c, (int)macros.size(),
           sqs, (int)sq.size(), dup, (int)seen.size(), (int)copy.size());
    return 0;
}
