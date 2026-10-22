// [over.match.funcs]/4: `take() &` gives its implicit object parameter the type
// `Buffer &`, and a temporary cannot bind a non-const lvalue reference - so
// `Buffer().take()` has no viable function. clang refuses it at the object.
struct Buffer {
    int data;
    int take() & { return data; }
};
int main() { return Buffer().take(); }
