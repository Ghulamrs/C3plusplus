// The defining half of thread-local-extern: one object built by a
// constructor, two laid down as constants.
extern "C" int printf(const char *, ...);

struct Counted {
    int value;
    Counted(int v);
    ~Counted();
};

Counted::Counted(int v) : value(v) { printf("Counted(%d)\n", v); }
Counted::~Counted() { printf("~Counted(%d)\n", value); }

thread_local Counted counted(21);
thread_local int plain = 5;
namespace other { thread_local long wide = 7; }
