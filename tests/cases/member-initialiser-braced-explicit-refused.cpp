// `= {...}` on a member is copy-list-initialisation, so an explicit constructor may not be chosen.
struct E { int v; explicit E(int x) : v(x) {} };
struct H { E e = {6}; H() {} };
int main() { H h; return h.e.v; }
