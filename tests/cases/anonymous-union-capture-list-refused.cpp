int main() { union { int a; float f; }; a = 1; return [a]() { return a; }(); }
// And named in the capture list it does not name a variable - clang's words, at the name in the list.
