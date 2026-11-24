// M8.3 slice 3j: a static data member's initialiser naming a template, `Count<Rest...>::value`, `Size<T>::value`, made where
// the door evaluates it (Parser::declareStaticMember), after the ids made before the class, as the replay reads the body.
extern "C" int printf(const char *, ...);

template <class... Ts> struct Count;
template <> struct Count<> { static const int value = 0; };
template <class T, class... Rest> struct Count<T, Rest...> { static const int value = 1 + Count<Rest...>::value; };
template <class T> struct Size { static const int value = sizeof(T); };
template <class T> struct Twice { int held; static const int value = 2 * Size<T>::value; };
template <class T, class U> struct Same { static const bool value = false; };
template <class T> struct Same<T, T> { static const bool value = true; };
template <class T> struct IsInt { char c; static const bool value = Same<T, int>::value; };
template <class T> struct Late { static const int value = Size<T>::value; Size<T> after; };

int main() {
    printf("%d %d %d\n", Count<int, char, double>::value, Count<>::value, Count<short>::value);
    printf("%d %d\n", Twice<int>::value, Twice<char>::value);
    printf("%d %d\n", (int)IsInt<int>::value, (int)IsInt<char>::value);
    Twice<short> t;
    t.held = 3;
    Late<double> l;
    printf("%d %d %d %d\n", t.held, (int)sizeof(t), Late<double>::value, (int)sizeof(l));
    return 0;
}
