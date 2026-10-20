// [except.unexpected]/2: a list that allows std::bad_exception has a disallowed exception from the
// unexpected handler replaced by one. The C6000 header has no bad_exception: its runtime keeps the
// class to itself.
#include <exception>
extern "C" int printf(const char *, ...);

void other() { printf("handler throws char\n"); throw 'c'; }

void sub() throw(int, std::bad_exception) { throw 1.5; }

int main() {
    std::set_unexpected(other);
    try { sub(); }
    catch (std::bad_exception &) { printf("bad_exception\n"); }
    catch (...) { printf("other\n"); }
    return 0;
}
