// [over.literal]/5: a literal operator template's own list is one non-type pack of char.
template <class T> int operator"" _f() { return 0; }
int main() { return 0; }
