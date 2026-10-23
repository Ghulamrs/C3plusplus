// [stmt.for]/1: a for's condition must convert to bool, and a class with no conversion function does not.
struct S { int v; };
int main() {
    S s = {1};
    for (; s;) {}
}
