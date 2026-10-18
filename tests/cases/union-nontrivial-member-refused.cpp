// [class.union]/2 (C++11's unrestricted union): a member with a non-trivial default
// constructor deletes the union's, so `U u;` is ill-formed - clang: "call to
// implicitly-deleted default constructor of 'U'". cpp11 compiles it and builds nothing.
// An anonymous union refuses such a member by name; a named one is this over-acceptance.
struct In { int x; In(); };
In::In() : x(3) {}
union U { In in; int i; };
int main() { U u; return u.i; }
