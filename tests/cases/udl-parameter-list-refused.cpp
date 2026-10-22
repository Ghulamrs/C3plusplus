// [over.literal]/3: a literal operator takes one of a short list of parameter lists, and int is not one.
int operator"" _a(int v) { return v; }
int main() { return 0; }
