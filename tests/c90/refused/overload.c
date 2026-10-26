/* Two functions of one name: C has no overloading. */
int f(int x) { return x; }
int f(double x) { return (int)x; }
int main(void) { return f(1) + f(2.0); }
