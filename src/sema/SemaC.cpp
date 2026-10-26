// sema::SemaC and sema::SemaCxx - see SemaC.h.

#include "SemaC.h"
#include "../Ast.h"
#include "../Source.h"

namespace sema {

bool SemaC::fits(const Token &t, Kind k) const {
    const unsigned long long u = static_cast<unsigned long long>(t.value);
    const Type *c = types_.get(k);
    const int bits = c->size(target_) * 8;
    const unsigned long long limit =
        c->isSigned(target_) ? (1ULL << (bits - 1)) - 1 : (bits >= 64 ? ~0ULL : (1ULL << bits) - 1);
    return u <= limit;
}

// **C90 6.1.3.2**: an unsuffixed decimal climbs int, long, unsigned long; octal and hex have an unsigned
// rung above each; a suffix starts its ladder where it says. `long long` and its unsigned stand above as
// the extension c90 takes (clang -std=c89 gives `sizeof(0xFFFFFFFF)` 4 where c90 1.4 gave 8: clang's answer).
const Type *SemaC::integerLiteralType(const Token &t) const {
    if (t.suffixU && t.suffixLL) return types_.get(Kind::ULongLong);
    if (t.suffixLL)              return fits(t, Kind::LongLong) ? types_.get(Kind::LongLong) : types_.get(Kind::ULongLong);
    if (t.suffixU && t.suffixL)  return fits(t, Kind::ULong) ? types_.get(Kind::ULong) : types_.get(Kind::ULongLong);
    if (t.suffixU)               return fits(t, Kind::UInt) ? types_.get(Kind::UInt)
                                      : fits(t, Kind::ULong) ? types_.get(Kind::ULong) : types_.get(Kind::ULongLong);
    if (t.suffixL)               return fits(t, Kind::Long) ? types_.get(Kind::Long)
                                      : fits(t, Kind::ULong) ? types_.get(Kind::ULong)
                                      : fits(t, Kind::LongLong) ? types_.get(Kind::LongLong) : types_.get(Kind::ULongLong);
    if (t.isChar)                return charLiteralType();
    if (t.prefix == 'u')         return types_.get(Kind::Char16);
    if (t.prefix == 'U')         return types_.get(Kind::Char32);
    if (t.wide)                  return wideElement();
    if (fits(t, Kind::Int))                     return types_.intType();
    if (!t.decimal && fits(t, Kind::UInt))      return types_.get(Kind::UInt);
    if (fits(t, Kind::Long))                    return types_.get(Kind::Long);
    if (fits(t, Kind::ULong))                   return types_.get(Kind::ULong);
    if (fits(t, Kind::LongLong))                return types_.get(Kind::LongLong);
    return types_.get(Kind::ULongLong);
}

// **[lex.icon] table 6, and the two ladders it holds.** A decimal literal climbs int, long, long long
// and never reaches unsigned; a hex or octal one has an unsigned rung above each. One ladder served both, wrongly.
const Type *SemaCxx::integerLiteralType(const Token &t) const {
    if (t.suffixU && t.suffixLL) return types_.get(Kind::ULongLong);
    if (t.suffixLL)              return fits(t, Kind::LongLong) ? types_.get(Kind::LongLong) : types_.get(Kind::ULongLong);
    if (t.suffixU && t.suffixL)  return fits(t, Kind::ULong) ? types_.get(Kind::ULong) : types_.get(Kind::ULongLong);
    if (t.suffixU)               return fits(t, Kind::UInt) ? types_.get(Kind::UInt)
                                      : fits(t, Kind::ULong) ? types_.get(Kind::ULong) : types_.get(Kind::ULongLong);
    if (t.suffixL)               return fits(t, Kind::Long) ? types_.get(Kind::Long)
                                      : (!t.decimal && fits(t, Kind::ULong)) ? types_.get(Kind::ULong)
                                      : fits(t, Kind::LongLong) ? types_.get(Kind::LongLong) : types_.get(Kind::ULongLong);
    if (t.prefix == 'u')         return types_.get(Kind::Char16);
    if (t.prefix == 'U')         return types_.get(Kind::Char32);
    if (t.wide)                  return types_.get(Kind::WChar);
    if (t.isChar)                return types_.get(Kind::Char);
    if (fits(t, Kind::Int))                     return types_.intType();
    if (!t.decimal && fits(t, Kind::UInt))      return types_.get(Kind::UInt);
    if (fits(t, Kind::Long))                    return types_.get(Kind::Long);
    if (!t.decimal && fits(t, Kind::ULong))     return types_.get(Kind::ULong);
    if (fits(t, Kind::LongLong))                return types_.get(Kind::LongLong);
    return types_.get(Kind::ULongLong);
}

namespace {
// The constructs a C90 token stream can reach and C++ alone allows: the construct as the message names it, and the hint.
const struct { const char *construct; const char *hint; } kCxxOnly[] = {
    { "a reference",                       "C has pointers: write 'T *' and take the address" },
    { "a function declared in a struct",   "C's struct holds data; declare the function outside it and pass the struct" },
    { "a member initialiser",              "initialise the member where the object is defined, or in a function" },
    { "a default argument",                "every call must pass every argument in C" },
    { "a second declaration of one name with another parameter list",
                                           "C has no overloading, and two functions named alike would be one symbol - give the second another name" },
    { "a functional cast",                 "write the cast as '(T)e'" },
    { "a function try block",              "C has no exceptions" },
};
}

void SemaC::refuseCxxOnly(const Source &src, std::size_t pos, const char *construct) const {
    std::string hint;
    for (const auto &row : kCxxOnly)
        if (std::string(row.construct) == construct) hint = row.hint;
    src.fail(pos, std::string(construct) + " is C++, and this file is being compiled as C90 (-std=c90)" +
                  (hint.empty() ? std::string() : " - " + hint));
}

std::unique_ptr<SemaC> rulesFor(Dialect d, TypeTable &types, const Target &target) {
    if (d == Dialect::C90) return make_owned<SemaC>(types, target);
    return make_owned<SemaCxx>(types, target);
}

}  // namespace sema
