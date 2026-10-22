// A pack expanded inside a nested template-id of a partial specialization's
// arguments, `K<B<R...> >`, is legal C++11: the pattern would bind R to the
// whole of B's list. Refused by name; `R...` as an argument of its own matches.
template <class... Ts> struct B { };
template <class... Ts> struct K { };
template <class... R> struct K<B<R...> > { static const int v = 1; };
int main() { return K<B<int> >::v; }
