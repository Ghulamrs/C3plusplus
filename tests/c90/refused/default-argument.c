/* A default argument on a prototype. */
int add(int a, int b = 2);
int main(void) { return add(1); }
int add(int a, int b) { return a + b; }
