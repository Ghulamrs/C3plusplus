// [usrlit.suffix]/1 reserves a ud-suffix that does not begin with '_': the declaration stands, and
// clang refuses the literal that would reach it.
int operator"" km(unsigned long long v) { return (int)v; }
int main() { return 12km; }
