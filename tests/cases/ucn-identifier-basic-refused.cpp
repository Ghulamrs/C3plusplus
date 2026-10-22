// [lex.charset]/2: outside a literal a universal character name may not name a basic source
// character - `\u0041` is an A, and an A is written as one.
int \u0041b = 1;
int main() { return 0; }
