// A nested class's static member used, another not; the names suite would show the unused one.
extern "C" int printf(const char *, ...);
template <class T> struct O { struct In { static int used; static int unused; int get() const { return used; } }; };
template <class T> int O<T>::In::used = 3;
template <class T> int O<T>::In::unused = 4;
int main() { O<int>::In in; printf("%d %d\n", in.get(), O<char>::In::used); return 0; }
