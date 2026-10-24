// A private nested class named outside its class by code that is neither a member nor a friend.
class P {
    struct In { int v; };
    friend int g();
};
int g() { P::In i; i.v = 4; return i.v; }
int h() { P::In i; i.v = 5; return i.v; }
int main() { return g() + h(); }
