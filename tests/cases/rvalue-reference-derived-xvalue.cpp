// [dcl.init.ref]/5: an rvalue reference to a base binds an xvalue of a derived class to its base
// subobject, as an lvalue reference binds an lvalue. Calling the base's move assignment by name -
// `B::operator=(std::move(o))` - moved out of a sliced byte copy, which shared the vector's buffer
// with o, and both were then destroyed (found building the C6000 scheduler with cxx3, M1).
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

struct Fields {
    std::string name;
    std::vector<std::string> ops;
};

struct Line : Fields {
    int edits = 0;
    Line() = default;
    Line(Line &&) = default;
    Line(const Line &o) : Fields(o), edits(o.edits + 1) {}
    Line &operator=(Line &&o) { Fields::operator=(std::move(o)); edits++; return *this; }
};

static int peek(Fields &&f) { return (int)f.ops.size(); }

static Fields steal(Fields &&f) { return std::move(f); }

int main() {
    Line a;
    a.name = "load";
    for (int i = 0; i < 5; i++) a.ops.push_back(std::string("operand number ") + char('0' + i));
    Line b;
    b = std::move(a);
    std::printf("%d %d %s %d\n", (int)a.ops.size(), (int)b.ops.size(), b.ops[4].c_str(), b.edits);

    Line c;
    c.ops.push_back("one");
    std::printf("%d\n", peek(std::move(c)));
    Fields d = steal(std::move(c));
    std::printf("%d %d %s\n", (int)c.ops.size(), (int)d.ops.size(), d.ops[0].c_str());

    std::vector<Line> v(3);
    v[0].ops.push_back("x");
    v[1] = std::move(v[0]);
    std::printf("%d %d\n", (int)v[0].ops.size(), (int)v[1].ops.size());
    return 0;
}
