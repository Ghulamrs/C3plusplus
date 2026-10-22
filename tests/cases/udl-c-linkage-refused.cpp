// [over.literal]/6: a literal operator has C++ language linkage - its name is not one C can spell.
extern "C" int operator"" _d(unsigned long long v);
int main() { return 0; }
