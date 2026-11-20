// M8.2 2f: a plain class's private member reached through T in a body made from the read is refused as the replay refuses it.
extern "C" int printf(const char *, ...);

struct Hid { int open; Hid(); private: int x; };
Hid::Hid() : open(1), x(2) {}
template <class T> int peek(const T &t) { return t.x; }

int main() {
    Hid h;
    printf("%d\n", peek(h));
    return 0;
}
