// [temp.arg.template]/3 as clang reads it: a template with a parameter the
// list does not reach matches only where that parameter has a default - Two
// in template-template-pack.cpp has one, this Two2 has none. clang: "too few
// template arguments for class template 'Two2'", at the argument.
template <class T, class U> struct Two2 { T t; U u; };
template <template <class> class C> struct H { C<int> c; };
H<Two2> b;
