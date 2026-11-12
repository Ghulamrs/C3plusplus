// [namespace.udecl]/3: a using-declaration in a class may name a member of any base, direct or not -
// `using Regions::markLine;` where Regions is the base of a base (Walker.h, self-host stage 0).
#include <stdio.h>

struct Regions {
    int lines;
    Regions();
    void markLine(int pos);
};
Regions::Regions() : lines(0) {}
void Regions::markLine(int pos) { lines += pos; }

struct Middle : Regions {
    int extra;
    Middle();
};
Middle::Middle() : extra(7) {}

struct Walker : Middle {
    using Regions::markLine;
    void markLine(const char *why);
};
void Walker::markLine(const char *why) { printf("why %s\n", why); }

int main() {
    Walker w;
    w.markLine(3);
    w.markLine(4);
    w.markLine("text");
    printf("lines %d extra %d\n", w.lines, w.extra);
    return 0;
}
