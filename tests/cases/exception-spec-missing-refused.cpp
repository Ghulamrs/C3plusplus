// [except.spec]/3: a definition may not drop the list its declaration has.
void f() throw(int);
void f() {}
int main() { f(); return 0; }
