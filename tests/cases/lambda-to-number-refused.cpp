// A captureless lambda converts to a function pointer, and no standard conversion takes that to an
// `int` - the parameter is not viable, as clang says. The conversion step took any scalar for any scalar.
static int g(int k) { return k; }
int main() { return g([](int i) { return i; }); }
