// M8.1, the review's M5, M2 and M1 in the read's tree: a comma over a dependent operand is dependent and its call
// waits; `.template cv<int>` keeps `template` and its arguments; an unqualified name a left-out base would have to
// supply is `unfound`, never `dependent` ([temp.dep]/3 - clang refuses it), where `this->nb` stays dependent.
int g(int v) { return v; }
template <class T> int comma(T x, int k) { return g((x += 1, k)); }
template <class T> int member(T b, int k) { return b.template cv<int>(k) + b.cv(k); }
template <class T> struct B { int nb; };
template <class T> struct C : B<T> { int f() { nb = 1; return g(nb) + this->nb; } };
int main() { return 0; }
