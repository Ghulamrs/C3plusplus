// [temp.arg.template]/3: each parameter of the list wants one of its own kind
// from the argument template - a non-type `template <int> class C` is not
// given Box, whose parameter is a type. clang: "template argument for template
// type parameter must be a type", at the argument.
template <class T> struct Box { T v; };
template <template <int> class C> struct HN { C<3> c; };
HN<Box> b;
