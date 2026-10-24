#pragma once

// **sema::Overload, out of the parser in place** - M2 step 1, doc-cxx3/M2-PLAN.md. Its functions moved here
// whole; they still read the parser's state through p_, and the state follows in a later commit.

#include "Entities.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Type.h"

#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Parser;

namespace sema {

class Overload {
public:
    explicit Overload(Parser &p) : p_(p) {}

    // UserDefined rank for class `from` to scalar `to` through its one conversion function; plain for a class.
    Rank userDefinedRank(const Type *from, const Type *to);
    // For `given` at one rank, [over.ics.rank]/3.2 and /4: +1 if pa's target is nearer, -1 if pb's, 0 if neither.
    int closerTarget(const Type *pa, const Type *pb, const Type *given) const;
    // `ranksObjectA`/`B` say whether that candidate's rank vector opens with an implicit object
    // parameter, which is how a rank position is mapped back to the parameter it came from - a
    // member and a non-member operator are ranked side by side and do not open the same way.
    bool betterCandidate(const std::vector<Rank> &a, const std::vector<Rank> &b,
                         const Signature &fa, const Signature &fb,
                         bool ranksObjectA, bool ranksObjectB) const;
    // [temp.func.order]: a's template beats b's when b's patterns match a's, not the reverse.
    bool moreSpecializedFunction(const Signature &a, const Signature &b) const;
    // The parameter a rank position came from, or null for an implicit object
    // parameter and for anything the ellipsis swallowed.
    static const Type *rankedParameter(const Signature &f, std::size_t i,
                                       bool ranksObject);
    // Whether two parameters are the same match either way round.
    static bool sameMatchEitherWay(const Type *pa, const Type *pb);
    static std::string describeSignature(const Signature &f);
    Rank rankArgument(const Expr &arg, const Type *param);
    // `object` is the type the call is made on for a member function and null for a free
    // one: **the implicit object parameter is ranked like any other**, which is what
    // tells `get()` from `get() const`. **By value** - `functions_` can move under you.
    Signature resolveOverload(const std::string &name,
                                     const std::vector<ExprPtr> &args,
                                     std::size_t pos,
                                     const Type *object = nullptr,
                                     bool objectLvalue = true);
    // [over.match.funcs]/4: whether a member with this ref-qualifier and const takes the object.
    static bool refQualifierViable(RefQual ref, bool constThis, bool objectLvalue);
    static std::string refQualifierRefusal(const std::string &name, bool objectLvalue);
    static std::string refQualifierMixRefusal(RefQual written, RefQual other);
    // The lowest number of arguments a call may give this function.
    std::size_t leastArguments(const Signature &f) const;
    // `right` is null for a unary operator, where the operand is `left` and
    // a member candidate writes no parameter at all.
    OperatorChoice resolveOperator(const std::string &name, const Expr &left,
                                   const Expr *right, std::size_t pos);

private:
    Parser &p_;
};

}   // namespace sema
