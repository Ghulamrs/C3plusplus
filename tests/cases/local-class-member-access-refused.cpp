// A local class in a function that is not a member of P has that function's access, which is none.
class P { int secret_; public: P() : secret_(1) {} };
int f(const P &p) { struct L { static int g(const P &q) { return q.secret_; } }; return L::g(p); }
int main() { P p; return f(p); }
