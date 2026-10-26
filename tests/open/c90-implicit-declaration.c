/* An implicit function declaration, valid C90 (6.3.2.2); c90 1.4 refuses it and so does this. */
#include <stdio.h>
int main(void) { printf("%d\n", g(2)); return 0; }
int g(int x) { return x + 1; }
