// A static_cast to a reference does not take const off, whichever class it names.
struct B { int x; };
struct D : B { int c; };
int main() { const D d = {}; B &b = static_cast<B &>(d); return b.x; }
