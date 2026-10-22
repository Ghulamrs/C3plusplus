// [over.load]/2: member functions with the same name and parameter-type-list
// cannot be overloaded if some but not all of them have a ref-qualifier - the
// cv on `this` is not part of that list, so `take() const` beside `take() &`
// is refused too. clang refuses the second declaration, naming the first.
struct Buffer {
    int data;
    int take() & { return data; }
    int take() { return data; }
};
int main() { Buffer b; b.data = 1; return b.take(); }
