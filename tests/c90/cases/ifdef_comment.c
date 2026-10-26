// expect: 3
/* A comment after the name of #ifdef or #ifndef is whitespace, as on any directive line; it was
   read as part of the name, so every such #ifndef was taken. #if and #elif strip it too. */
#include <stdio.h>
#define KNOWN 1

int main(void)
{
    int r = 0;
#ifndef KNOWN /* a block comment */
    printf("wrong: #ifndef KNOWN with a block comment\n"); r += 8;
#endif
#ifdef NEVER_DEFINED /* a comment */
    printf("wrong: #ifdef of an undefined name\n"); r += 16;
#endif
#ifdef KNOWN /* the name alone */
    printf("#ifdef KNOWN\n"); r += 1;
#endif
#if KNOWN /* #if */
    printf("#if KNOWN\n"); r += 2;
#elif 1 /* #elif */
    printf("wrong: #elif after a true #if\n"); r += 32;
#endif
    return r;
}
