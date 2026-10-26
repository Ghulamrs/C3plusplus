/* Implicit int on a definition, valid C90 (6.5.2); c90 1.4 refuses it and so does this. */
#include <stdio.h>
main() { printf("%d\n", 3); return 0; }
