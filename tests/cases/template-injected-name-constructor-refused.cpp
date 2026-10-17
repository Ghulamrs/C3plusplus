// [class.qual]/2: `Tree::Tree` names the constructor, not a type, inside the
// template as well - clang: "qualified reference to 'Tree' is a constructor
// name rather than a type in this context". `Tree(5)` is the temporary.
template <class T> struct Tree {
    int v;
    Tree(int x) : v(x) {}
    struct It { int get() { return Tree::Tree(5).v; } };
};
int main() { Tree<int>::It i; return i.get(); }
