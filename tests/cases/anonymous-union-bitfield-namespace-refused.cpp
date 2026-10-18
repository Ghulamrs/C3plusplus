static union { int lo : 4; int b; }; int main() { lo = 1; return lo; }
// A bit-field of an anonymous union at namespace or block scope would be a name with no object to be a field of: refused by name. clang compiles it.
