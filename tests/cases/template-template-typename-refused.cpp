// `template <template <class> typename C>` - `typename` in place of `class`
// after a template template parameter's own list is C++17 (N4051), and this
// compiler is C++11. clang -std=c++11 -pedantic-errors: "template template
// parameter using 'typename' is a C++17 extension", at the keyword. The
// `typename` inside the inner list is C++98 and is not this.
template <class T> struct Box { T v; };
template <template <typename> typename C> struct H { C<int> c; };
H<Box> a;
