// [lex.ext]/3: with no cooked form, a number takes the raw literal operator or the template - and the
// set may hold one of them, not both.
int operator"" _rt(const char *s) { return s[0]; }
template <char... Cs> int operator"" _rt() { return 0; }
int main() { return 12_rt; }
