// [except.spec]/3: every declaration of a function has the same list.
void f() throw(int);
void f() throw(char) {}
int main() { f(); return 0; }
