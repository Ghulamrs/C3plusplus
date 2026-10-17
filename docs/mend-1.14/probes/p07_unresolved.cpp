// `sr T_`: a qualified name through a template parameter, and what it does to the substitution table.
struct S { static int count(); static const int value = 3; int size() const { return 1; } };
template <class T, class U> auto q1(T t, U u) -> decltype(T::count() + u) { return 0; }
template <class T> auto q2(T t) -> decltype(T::value) { return 0; }
template <class T, class U> auto q3(T t, U u) -> decltype(T::value + u) { return 0; }
template <class T> auto q4(T t) -> decltype(t.size() + T::value) { return 0; }
template <class T> auto q5(T t) -> decltype(+t) { return +t; }
template <class T> auto q6(T t) -> decltype(t.size() == T::count()) { return true; }
int main() { S s; int i = 0; q1(s, i); q2(s); q3(s, i); q4(s); q5(i); q6(s); return 0; }
