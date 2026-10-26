#pragma once

// **`SemaC`, the rules C90 and C++11 answer differently, and `SemaCxx` deriving from it** - design
// 3.2 (M7, doc-cxx3/M7-PLAN.md). C++ tightens C as well as extending it, so an override refuses as
// well as adds; every C++ answer is the one the parser's inline rule gave before it was lifted here.

#include "../Dialect.h"
#include "../Lexer.h"
#include "../Type.h"

#include <cstddef>
#include <memory>
#include <string>

class Source;

namespace sema {

class SemaC {
public:
    SemaC(TypeTable &types, const Target &target) : types_(types), target_(target) {}
    virtual ~SemaC() {}
    virtual Dialect dialect() const { return Dialect::C90; }

    // The type of a comparison, `!`, `&&` and `||`: int in C (6.3.8), bool in C++ ([expr.rel]/1).
    virtual const Type *truthType() const { return types_.intType(); }
    // A single-character literal: int in C (6.1.3.4), char in C++ ([lex.ccon]/2).
    virtual const Type *charLiteralType() const { return types_.intType(); }
    // A string literal's element: char in C (6.1.4), const char in C++ ([lex.string]/8).
    virtual const Type *stringLiteralElement(const Type *elem) const { return elem; }
    // An integer literal's type from its value and spelling - C90 6.1.3.2's ladders, [lex.icon] table 6 in C++.
    virtual const Type *integerLiteralType(const Token &t) const;
    // `void *` to an object pointer without a cast: C 6.3.16.1 yes, [conv.ptr]/2 no.
    virtual bool voidPointerConvertsToObjectPointer() const { return true; }
    // An integer to an enumeration without a cast: C 6.5.2.2 yes, [conv.integral] no.
    virtual bool integerConvertsToEnumeration() const { return true; }
    // `struct S {}; S s;` - the tag is a type name in C++ only; C keeps tags in a space of their own (6.1.2.3).
    virtual bool tagIsTypeName() const { return false; }
    // A jump into the scope of an initialised object: C allows it (6.6.6.4), [stmt.dcl]/3 refuses it.
    virtual bool jumpMayPassInitialisation() const { return true; }
    // `const int k = 1;` at file scope: external in C (6.1.2.2), internal in C++ ([basic.link]/3).
    virtual bool constObjectHasInternalLinkage() const { return false; }
    // `int v; int v;` - C's tentative definitions (6.7.2); [basic.def.odr]/1 has none.
    virtual bool tentativeDefinitions() const { return true; }
    // Whether a name is mangled; a C name is its own symbol, a static function's included.
    virtual bool mangles() const { return false; }
    // Two declarations of one name with different parameter lists: C++ overloads, C conflicts.
    virtual bool overloads() const { return false; }
    // `auto` deducing a type: C++11; in C it is a storage class and nothing else.
    virtual bool autoDeduces() const { return false; }
    // A wide character or string literal's element: wchar_t's own kind in C++, the typedef's in C.
    virtual const Type *wideElement() const { return types_.get(target_.wcharType()); }

    // **The one refusal table**: a C++-only construct met in a C90 file, refused by name with its hint.
    void refuseCxxOnly(const Source &src, std::size_t pos, const char *construct) const;

protected:
    TypeTable &types_;
    const Target &target_;
    // Whether the literal's value fits the kind on this target.
    bool fits(const Token &t, Kind k) const;
};

class SemaCxx : public SemaC {
public:
    SemaCxx(TypeTable &types, const Target &target) : SemaC(types, target) {}
    Dialect dialect() const override { return Dialect::Cxx11; }
    const Type *truthType() const override { return types_.get(Kind::Bool); }
    const Type *charLiteralType() const override { return types_.get(Kind::Char); }
    const Type *stringLiteralElement(const Type *elem) const override { return types_.withConst(elem); }
    const Type *integerLiteralType(const Token &t) const override;
    bool voidPointerConvertsToObjectPointer() const override { return false; }
    bool integerConvertsToEnumeration() const override { return false; }
    bool tagIsTypeName() const override { return true; }
    bool jumpMayPassInitialisation() const override { return false; }
    bool constObjectHasInternalLinkage() const override { return true; }
    bool tentativeDefinitions() const override { return false; }
    bool mangles() const override { return true; }
    bool overloads() const override { return true; }
    bool autoDeduces() const override { return true; }
    const Type *wideElement() const override { return types_.get(Kind::WChar); }
};

// The rules for a dialect, owned by the parser.
std::unique_ptr<SemaC> rulesFor(Dialect d, TypeTable &types, const Target &target);

}  // namespace sema
