// [temp.arg.template]/1: the argument for a template template parameter is the
// name of a class template or an alias template, and `int` is neither. clang:
// "template argument for template template parameter must be a class template
// or type alias template", at the argument.
template <template <class> class C> struct H { C<int> c; };
H<int> a;
