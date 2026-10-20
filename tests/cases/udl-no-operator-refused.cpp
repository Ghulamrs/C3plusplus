// [lex.ext]/3: an integer literal with a ud-suffix needs a literal operator taking unsigned long long,
// a raw one taking const char *, or a literal operator template, and nothing here declares any.
int operator"" _other(unsigned long long v) { return (int)v; }
int main() { return 12_zz; }
