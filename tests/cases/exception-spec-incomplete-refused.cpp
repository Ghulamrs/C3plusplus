// [except.spec]/2: a list names complete types.
struct S;
void f() throw(S) {}
int main() { f(); return 0; }
