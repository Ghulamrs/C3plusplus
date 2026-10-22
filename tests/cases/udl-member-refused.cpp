// [over.literal]/2: a literal operator is a namespace-scope function - a friend may declare one, a
// member may not be one.
struct S { int operator"" _c(unsigned long long v); };
int main() { return 0; }
