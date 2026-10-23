// Three faults behind one crash of stage 2 at -O2 (M1), each in how a closure holding a class was made:
// - a by-copy capture of a class was assigned bytewise, where [expr.prim.lambda]/21 copy-constructs it;
// - the closure was never marked as copied by a call, so a function taking it by value moved out of a
//   byte copy the caller still owned, and both destroyed what it held;
// - a constructor's mem-initialiser made a guard flag outside its body's reckoning, and when the constructor
//   was instantiated in the middle of another function, that function cleared the flag in its own frame -
//   over the upper half of its `this`.
// The closure temporary is destroyed at the end of its full expression, and `auto f = [...]` is the object.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

static int built, gone;
struct Held {
    int v;
    Held(int n) : v(n) { built++; }
    Held(const Held &o) : v(o.v) { built++; }
    ~Held() { gone++; }
};

struct Sink {
    void take(int n, const std::string &name, int o) { std::printf("%d %s %d\n", n, name.c_str(), o); }
};

struct Recorder {
    std::vector<std::function<void(Sink &)> > events;
    void event(std::function<void(Sink &)> call) { events.push_back(std::move(call)); }
    void prologue(int size, const std::string &name, int outgoing) {
        event([=](Sink &s) { s.take(size, name, outgoing); });
    }
};

template <class F> int callTwice(F f) { return f() + f(); }

int main() {
    std::vector<std::string> words;
    words.push_back("a string long enough to live on the heap");
    words.push_back("and a second one, also on the heap");
    {
        auto count = [=]() { return (int)words.size(); };
        auto copy = count;
        std::printf("%d %d %d\n", count(), copy(), callTwice(count));
    }
    {
        Recorder r;
        r.prologue(48, std::string("a name long enough to live on the heap"), 16);
        r.prologue(32, "short", 8);
        Sink s;
        for (std::size_t i = 0; i < r.events.size(); i++) r.events[i](s);
    }
    {
        Held h(7);
        std::function<int()> f = [h]() { return h.v; };
        std::function<int()> g = f;
        std::printf("%d %d\n", f(), g());
    }
    std::printf("%d\n", built - gone);
    return 0;
}
