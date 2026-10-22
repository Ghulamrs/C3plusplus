// [class.access.base]/4: slicing through a private base is refused outside the class, as clang refuses it.
struct T { int i; };
struct P : private T { int j; };
int main() {
    P p;
    T t = p;
    return t.i;
}
