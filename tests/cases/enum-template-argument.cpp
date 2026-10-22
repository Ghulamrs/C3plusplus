// An enumeration as a template argument is a type of its own: `Box<Rank>`, `Box<Plain>` and
// `Box<int>` are three classes, and `std::vector<Rank>` holds a Rank (M1, ParserOverload.cpp).
#include <vector>
extern "C" int printf(const char *, ...);
template <class T> struct Box { T v; };
template <class T> int which(T) { return 1; }
int which(int) { return 2; }
enum class Rank { A, B, C };
enum Plain { X, Y };
int main() {
    Box<Rank> b = { Rank::C };
    Box<Plain> p = { Y };
    Box<int> i = { 5 };
    Box<const Rank *> cp = { &b.v };
    std::vector<Rank> ranks(2, Rank::B);
    ranks.push_back(Rank::A);
    int n = 0;
    for (std::size_t k = 0; k < ranks.size(); k++) n = n * 10 + (int)ranks[k];
    printf("%d %d %d %d %d %d\n", (int)b.v, (int)p.v, i.v, which(Rank::B) + which(X) + which(7), (int)*cp.v, n);
    return 0;
}
