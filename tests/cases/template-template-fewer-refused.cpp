// [temp.arg.template]/3: a template with fewer parameters than the parameter's
// list has cannot take the arguments that list forms. clang refuses it at the
// argument, "too many template arguments for class template 'Box'".
template <class T> struct Box { T v; };
template <template <class, class> class C> struct H2 { C<int, char> c; };
H2<Box> d;
