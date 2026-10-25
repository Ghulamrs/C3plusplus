#pragma once

// **An operand's text as an index** (M3a.1, doc-cxx3/M3-IR.md 2.2): a symbol, a label, an immediate that is not a
// number, an x87 register's spelling. Interned once for the process and read with no lock, so an operand is a plain
// value a vector copies cheaply; it reads like the string it was through str(), and compares with one directly.

#include <cstddef>
#include <string>

namespace opt {

class StrId {
public:
    StrId() : id_(0) {}                // the empty text
    StrId(const std::string &s);       // interned where it is written
    StrId(const char *s);
    const std::string &str() const;
    unsigned index() const { return id_; }
    bool empty() const { return id_ == 0; }
    std::size_t size() const { return str().size(); }
    const char *c_str() const { return str().c_str(); }
    char operator[](std::size_t i) const { return str()[i]; }
    std::size_t find(const std::string &s, std::size_t pos = 0) const { return str().find(s, pos); }
    std::size_t find(char c, std::size_t pos = 0) const { return str().find(c, pos); }
    int compare(std::size_t pos, std::size_t n, const char *s) const { return str().compare(pos, n, s); }
    std::string substr(std::size_t pos, std::size_t n = std::string::npos) const { return str().substr(pos, n); }
private:
    unsigned id_;
};
inline bool operator==(const StrId &a, const StrId &b) { return a.index() == b.index(); }
inline bool operator!=(const StrId &a, const StrId &b) { return a.index() != b.index(); }
inline bool operator<(const StrId &a, const StrId &b) { return a.str() < b.str(); }
inline bool operator==(const StrId &a, const std::string &b) { return a.str() == b; }
inline bool operator!=(const StrId &a, const std::string &b) { return a.str() != b; }
inline bool operator==(const std::string &a, const StrId &b) { return b.str() == a; }
inline bool operator!=(const std::string &a, const StrId &b) { return b.str() != a; }
inline bool operator==(const StrId &a, const char *b) { return a.str() == b; }
inline bool operator!=(const StrId &a, const char *b) { return a.str() != b; }
inline std::string operator+(const std::string &a, const StrId &b) { return a + b.str(); }
inline std::string operator+(const StrId &a, const std::string &b) { return a.str() + b; }
inline std::string operator+(const char *a, const StrId &b) { return a + b.str(); }

}
