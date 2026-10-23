// [stmt.for]/1: a for's condition is converted to bool as a while's is - through a class's
// conversion function. `for (std::string l; std::getline(in, l);)` took the stream's address
// for the condition and looped for ever (found as stage 2's arm64 peephole hanging, M1).
#include <cstdio>
#include <sstream>
#include <string>

struct Countdown {
    int left;
    explicit operator bool() const { return left > 0; }
};

int main() {
    std::istringstream in("one\ntwo\nthree\n");
    int lines = 0;
    for (std::string l; std::getline(in, l);) lines++;
    std::printf("%d\n", lines);

    std::istringstream again("a b c");
    std::string word;
    int words = 0;
    for (; again >> word;) words++;
    std::printf("%d %s\n", words, word.c_str());

    Countdown c = {3};
    int turns = 0;
    for (; c; c.left--) turns++;
    std::printf("%d\n", turns);
    return 0;
}
