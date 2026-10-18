// [temp.arg.template]/1: the argument is a template's name alone - `Box<int>`
// is a type, and a type is not what a template template parameter takes. clang:
// "template argument for template template parameter must be a class template
// or type alias template", at the argument.
template <class T> struct Box { T v; };
template <template <class> class C> struct H { C<char> c; };
H<Box<int> > a;
