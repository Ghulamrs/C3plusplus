// [over.literal]/8: a literal operator has no default argument - a literal always hands over its value.
int operator"" _i(unsigned long long v = 3) { return (int)v; }
int main() { return 0; }
