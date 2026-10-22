// [lex.ext]/8: adjacent strings are one literal, and two different ud-suffixes cannot both be its.
typedef decltype(sizeof 0) size_t;
int operator"" _x(const char *, size_t n) { return (int)n; }
int operator"" _y(const char *, size_t n) { return (int)n; }
int main() { return "a"_x "b"_y; }
