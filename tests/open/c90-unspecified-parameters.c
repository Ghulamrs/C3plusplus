/* `int f();` declares a function with an unspecified parameter list (C90 6.5.4.3); c90 1.4 refuses the call too. */
#include <stdio.h>
int f();
int main(void) { printf("%d\n", f(1, 2)); return 0; }
int f(int a, int b) { return a + b; }
