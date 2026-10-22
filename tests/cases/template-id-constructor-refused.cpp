// The same rule through a written argument list: `Tree<int>::Tree(5)` names the
// constructor, and clang refuses it. cpp11 had found the injected class name as
// a member type and built a temporary.
template <class T> struct Tree { int v; Tree(int x) : v(x) {} };
int main() { return Tree<int>::Tree(5).v; }
