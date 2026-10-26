/* A default argument written on the definition. */
int add(int a, int b = 2) { return a + b; }
int main(void) { return add(1); }
