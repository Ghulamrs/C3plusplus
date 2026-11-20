// M8.2 2f: a class's private member function called through T in a body made from the read is refused as the replay refuses it.
extern "C" int printf(const char *, ...);

struct Sec { int open; Sec(); private: int hidden() const; };
Sec::Sec() : open(1) {}
int Sec::hidden() const { return 2; }
template <class T> int peek(const T &t) { return t.hidden(); }

int main() {
    Sec s;
    printf("%d\n", peek(s));
    return 0;
}
