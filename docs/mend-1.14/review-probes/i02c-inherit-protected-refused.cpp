// access and explicit travel with the inherited constructor. clang: rejects both marked lines.
struct B { protected: B(int) {} public: explicit B(double) {} };
struct D : B { using B::B; };
int main() {
    D d(2.5);
#ifdef PROBE_IMPLICIT
    D e = 2.5;      // explicit
#endif
#if 1
    D f(1);         // protected
#endif
    return 0;
}
