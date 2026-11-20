// M8.2 2f: a private member reached through T in a body made from the read is refused as the replay refuses it.
extern "C" int printf(const char *, ...);

template <class T> struct Hid { int open; Hid() : open(1), x(2) {} private: int x; };
template <class T> int peek(const T &t) { return t.x; }

int main() {
    Hid<int> h;
    printf("%d\n", peek(h));
    return 0;
}
