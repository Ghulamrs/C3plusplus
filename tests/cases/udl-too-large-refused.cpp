// [lex.ext]/3: the cooked form is handed the literal as an unsigned long long, and this one does not
// fit - a raw literal operator could have taken its digits.
int operator"" _k(unsigned long long v) { return (int)v; }
int main() { return 123456789012345678901234567890_k; }
