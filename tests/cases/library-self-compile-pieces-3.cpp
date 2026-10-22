// What the compiler's own src/ asks of the library, the third part (M1, 2026-10-23): seekp to a string
// stream's end, string::erase of the character at an iterator, and string's iterator typedefs.
#include <sstream>
#include <string>
extern "C" int printf(const char *, ...);
int main() {
    std::ostringstream out;
    out << "abcdef";
    std::string text = out.str();
    text.erase(text.size() - 2);
    out.str(text);
    out.seekp(0, std::ios::end);
    out << "Z";
    std::string s = "  xy";
    while (!s.empty() && s[0] == ' ') s.erase(s.begin());
    std::string::iterator after = s.begin();
    after = s.erase(after);
    printf("%s %s %c %d\n", out.str().c_str(), s.c_str(), *after, (int)out.good());
    return 0;
}
