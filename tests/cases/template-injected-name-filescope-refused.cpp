// Outside the template the bare name is the template, not a specialization, and
// needs an argument list - clang: "use of class template 'Tree' requires
// template arguments". Only inside it is `Tree::` the injected class name.
template <class T> struct Tree { static int next(int n) { return n + 1; } };
int main() { return Tree::next(1); }
