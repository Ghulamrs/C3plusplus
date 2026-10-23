// [over.match.ctor] with [class.conv.ctor]/2: an explicit constructor template is not a converting one.
struct S { int v; template <class T> explicit S(T t) : v((int)t) {} };
int main() { S s = 3; return s.v; }
