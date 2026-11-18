// M8.1: a template body read once at its declaration, shown after the tree as `(read ...)`; a name whose
// meaning waits for the arguments is marked `dependent`, and a written `typename` is kept on its node.
template <class T> struct Box { typedef T type; };
int g(int v) { return v; }
template <class T> int twice(T x) { int k = 2; return g(x) + typename T::type(k) + k; }
int main() { return 0; }
