// A class local built by its initialiser's conversion function ([class.conv.fct], [class.copy]/31): in its own
// slot, and as a named return value through the result pointer - the cases of src/backend/Masm.cpp.
#include <cstdio>
#include <string>

struct Str {
    const char *p;
    unsigned n;
    Str sub(unsigned k) const;
    explicit operator std::string() const { return std::string(p, n); }
};

Str Str::sub(unsigned k) const {
    Str s;
    s.p = p + k;
    s.n = n - k;
    return s;
}

static std::string upper(Str r) {
    std::string s(r.sub(1));
    for (std::size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'a' && s[i] <= 'z') s[i] = static_cast<char>(s[i] - 'a' + 'A');
    return s;
}

static int digits(const Str &x) {
    const std::string t(x);
    int k = 0;
    for (std::size_t i = 0; i < t.size(); i++)
        if (t[i] >= '0' && t[i] <= '9') k++;
    return k;
}

static unsigned length(const std::string &s) { return static_cast<unsigned>(s.size()); }

int main() {
    Str a;
    a.p = "%rax";
    a.n = 4;
    Str b;
    b.p = "12x45";
    b.n = 5;
    std::printf("%s %d\n", upper(a).c_str(), digits(b));
    std::string c(a);
    std::printf("%s %u\n", c.c_str(), static_cast<unsigned>(c.size()));
    std::string d;
    d = std::string(b.sub(2));
    std::printf("%u %s %d\n", length(std::string(a)), d.c_str(), std::string(a.sub(1)) == "rax");
    return 0;
}
