template <class T, T *p> struct P { };
int g;
int main() { P<int, &g> x; return 0; }
