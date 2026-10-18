// A type pack deduced from another template's argument list - [temp.deduct.type]/9.
// `Tuple<Ts...>` as a function parameter is read as a pattern, where Ts stands for
// itself and the argument is its expansion (Itanium `JDpT_E`), and the argument's
// whole list is what Ts is deduced to be. Refused from rung 5.7c to 2026-10-18 as
// "needs the pack's members, and here it stands for itself"; the non-type pack
// round built the expansion for both kinds of pack. The .expected is clang's.
extern "C" int printf(const char *, ...);
template <class... Ts> struct Tuple { int n; };
template <class... Ts> int size(Tuple<Ts...> t) { t.n = 0; return sizeof...(Ts); }
template <class... Ts> int bytes(const Tuple<Ts...> &) { int a[] = { 0, (int)sizeof(Ts)... }; int s = 0; for (int i = 0; i <= (int)sizeof...(Ts); i++) s += a[i]; return s; }
int main() {
    Tuple<int> one;
    Tuple<char, double, short> three;
    Tuple<> none;
    printf("%d %d %d\n", size(one), size(three), size(none));
    printf("%d %d %d\n", bytes(one), bytes(three), bytes(none));
    return 0;
}
