// The other half of [over.match.funcs]/4: `take() &&` gives its implicit object
// parameter the type `Buffer &&`, which an lvalue cannot bind - so `b.take()`
// on a named object has no viable function. clang refuses it at the object.
struct Buffer {
    int data;
    int take() && { return data; }
};
int main() { Buffer b; b.data = 1; return b.take(); }
