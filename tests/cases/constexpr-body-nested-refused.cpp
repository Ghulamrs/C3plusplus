// A block inside a C++11 constexpr body is C++14's: [dcl.constexpr]/3 lists what may stand beside the return.
constexpr int twice(int x) { { return x + x; } }
int table[twice(2)];
int main() { return sizeof table > 0 ? 0 : 1; }
