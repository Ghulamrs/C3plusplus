// [string.rfind]: the last occurrence that begins at or before pos, for a character, a C string,
// a counted C string and a string; an empty needle is found at min(pos, size()). cpp11's <string>
// had the one-character form alone, so `path.rfind(".txt")` was refused - found 2026-10-18.
#include <string>
#include <cstdio>
static int at(std::string::size_type n) { return n == std::string::npos ? -1 : (int)n; }
int main() {
    std::string s = "a/b/c.txt.txt";
    std::string empty;
    std::string txt = ".txt";
    printf("%d %d %d %d\n", at(s.rfind('/')), at(s.rfind('/', 2)), at(s.rfind('/', 0)), at(s.rfind('z')));
    printf("%d %d %d %d\n", at(s.rfind(".txt")), at(s.rfind(".txt", 8)), at(s.rfind(".txt", 4)), at(s.rfind(".tx", 100)));
    printf("%d %d %d\n", at(s.rfind(txt)), at(s.rfind(txt, 9)), at(s.rfind(std::string("a/b"))));
    printf("%d %d %d %d\n", at(s.rfind("")), at(s.rfind("", 3)), at(s.rfind("", 99)), at(empty.rfind("")));
    printf("%d %d %d %d\n", at(empty.rfind('x')), at(empty.rfind("x")), at(s.rfind("txt.txt.txt.txt")), at(s.rfind("txtX", 99, 3)));
    printf("%d %d\n", at(s.rfind('a', 0)), at(s.find_last_of('t')));
    return 0;
}
