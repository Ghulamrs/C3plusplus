// -g with -O on tms6747, the program it is refused on.
//
// **The refusal is the driver's, not the parser's**, so tools/exclusions cannot
// find it and docs/EXCLUSIONS.md cites this file. It runs as an ordinary case on
// every target; run.sh's tail compiles it with -arch tms6747 -g -O2 and wants the
// message, and with -g alone and wants it compiled.

extern "C" int printf(const char *, ...);

int twice(int x) { return 2 * x; }

int main() {
    printf("%d\n", twice(21));
    return 0;
}
