struct B { template <class T> T twice(T x) const { return x + x; } };
struct D : private B { };
int main() { D d; return d.twice(4); }
