/* A K&R definition, valid C90 (6.7.1), refused here as c90 1.4 refuses it (M7-PLAN: C23 deleted it). */
#include <stdio.h>
int f(a, b) int a; int b; { return a + b; }
int main(void) { printf("%d\n", f(1, 2)); return 0; }
