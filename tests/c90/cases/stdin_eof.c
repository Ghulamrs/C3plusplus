// expect: 7
/* scanf and fgets on an empty stdin: each answers the end of the file and stores nothing. gets is
   left to cpp11's stdin-gets, macOS's libc writing a warning about it that the reference would carry. */
#include <stdio.h>

int main(void)
{
    int n = 7;
    char line[16];
    char *text;
    int got = scanf("%d", &n);
    printf("scanf %d, n still %d\n", got, n);
    text = fgets(line, sizeof line, stdin);
    printf("fgets %s\n", text ? "a line" : "null");
    return (got == EOF) + 2 * (n == 7) + 4 * (text == 0);
}
