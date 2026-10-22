// [basic.lookup.unqual]/8: inside a member function the class's own members are found before the
// namespace's names - a data member called `vector` hides a class template of that name, and
// `vector[0]` subscripts the member. cpp11 read it as the template and refused an instantiation.
// Found compiling the COM Core CMatrix, whose rows are a member named `vector`.
extern "C" int printf(const char *, ...);

template <class T> struct vector { T t; };
template <int M> struct Mx {
    int vector[M];
    void set(int x) { vector[0] = x; vector[M - 1] = x * 2; }
    int sum() const { int s = 0; for (int i = 0; i < M; i++) s += vector[i]; return s; }
};
struct Plain {
    double vector[2];
    static int count;
    double twice() { vector[1] = vector[0] * 2; return vector[1]; }
};
int Plain::count = 4;

int main() {
    Mx<3> m = {};
    m.set(5);
    Plain p;
    p.vector[0] = 1.5;
    vector<int> v = { 9 };
    printf("%d %g %d\n", m.sum(), p.twice(), v.t);
    return 0;
}
