// sema::Overload - see Overload.h.

#include "Overload.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"
#include "../Source.h"

#include <algorithm>

namespace sema {

// The integral and floating promotions, [conv.prom] and [conv.fpprom], and only those - every other arithmetic pairing is a conversion, which ranks below; it is what makes f(int) beat f(double) for a char argument.
static bool isPromotion(const Type *from, const Type *to) {
    if (from->isScopedEnumeration()) return false;
    // [conv.prom]/3: an enumeration to the type its underlying integer promotes to.
    if (from->isEnumeration() && !to->isEnumeration()) {
        const Kind k = from->kind();
        const bool small = k == Kind::Bool || k == Kind::Char || k == Kind::SChar ||
                           k == Kind::UChar || k == Kind::Short || k == Kind::UShort;
        return to->kind() == (small ? Kind::Int : k);
    }
    if (to->kind() == Kind::Int) {
        switch (from->kind()) {
            case Kind::Bool: case Kind::Char: case Kind::SChar: case Kind::UChar:
            case Kind::Short: case Kind::UShort: case Kind::WChar: case Kind::Char16:
                return true;
            default:
                return false;
        }
    }
    if (to->kind() == Kind::UInt && from->kind() == Kind::Char32) return true;
    return to->kind() == Kind::Double && from->kind() == Kind::Float;
}

std::string Overload::refQualifierMixRefusal(RefQual written, RefQual other) {
    const RefQual with = written != RefQual::None ? written : other;
    const std::string spelt = std::string("with ref-qualifier '") +
                              (with == RefQual::LValue ? "&" : "&&") + "'";
    const std::string none = "without a ref-qualifier";
    return "cannot overload a member function " + (written != RefQual::None ? spelt : none) +
           " with a member function " + (written != RefQual::None ? none : spelt) + " - [over.load]/2";
}


Rank Overload::rankArgument(const Expr &arg, const Type *param) {
    const Type *given = arg.type();

    // A reference parameter binds or it does not; there is no conversion to
    // rank.
    if (param->isReference()) {
        const Type *want = param->pointee();

        // **A reference to a function binds a function lvalue of its type** - [dcl.init.ref]/5,
        // an exact match; a name arrives as its address, `&f`, and that is the same function.
        if (want->isFunction()) {
            const Type *fn = p_.functionDesignated(arg);
            return fn != nullptr && fn->unqualified() == want->unqualified() ? Rank::Identity
                                                                             : Rank::None;
        }

        // **A reference to an array binds to an array of the same length**,
        // without the decay a by-value parameter would apply.
        if (want->isArray() && given->isArray() && want->sameArrayShape(given)) {
            const Type *we = want->innermostElement();
            const Type *ge = given->innermostElement();
            if (we->unqualified() == ge->unqualified()) {
                if (!we->isConst() && ge->isConst()) return Rank::None;
                // The category as any reference asks it: `&&` takes no lvalue, a string literal being one (M1).
                const bool lvalue = markedLvalue(arg) || dynamic_cast<const StrLit *>(&arg) != nullptr;
                if (param->isRValueReference() ? lvalue : (!lvalue && !we->isConst())) return Rank::None;
                return we->isConst() && !ge->isConst() ? Rank::Qualification
                                                       : Rank::Identity;
            }
        }

        // **A reference to a base binds to a derived object** - [dcl.init.ref], a derived-to-base Conversion,
        // so `f(Base &)` loses to `f(Derived &)` for a Derived; /5 binds a const one to a derived class
        // temporary too, `f(D(3))` for `f(const B &)`, and a `B &&` a derived rvalue, `std::move(e)`.
        if (want->unqualified() != given->unqualified() &&
            p_.accessibleBaseOffset(given, want) > -1 &&
            (param->isRValueReference()
                 ? !markedLvalue(arg)
                 : (markedLvalue(arg) || (want->isConst() && given->unqualified()->isStructOrUnion())))) {
            if (!want->isConst() && given->isConst()) return Rank::None;
            return Rank::Conversion;
        }

        if (want->unqualified() != given->unqualified()) {
            // **A `const T &` takes a temporary, so a conversion** - [over.ics.user]: the converting constructor makes
            // an object and the reference binds to it; an rvalue reference does too, [dcl.init.ref]/5 (M1,
            // `v.push_back(nullptr)`) - not where the two are reference-related, /5.4.1.
            const bool related = given->unqualified()->isStructOrUnion() && want->unqualified()->isStructOrUnion() &&
                                 p_.accessibleBaseOffset(given, want) > -1;
            const bool takesTemporary = want->isConst() || (param->isRValueReference() && !related);
            if (takesTemporary && p_.rankingConversion_ == 0) {
                p_.rankingConversion_++;
                // A class target is reached by its converting constructor or by
                // the argument's conversion function, [over.ics.user]; the second
                // was missing, and `take(const B &)` refused an A with `operator B`.
                const bool usable =
                    want->unqualified()->isStructOrUnion()
                        ? (p_.convertingConstructor(want, arg) != nullptr ||
                           (given->unqualified()->isStructOrUnion() &&
                            p_.conversionFunction(given, want->unqualified()) != nullptr))
                        : (given->unqualified()->isStructOrUnion() &&
                           p_.conversionFunction(given, want) != nullptr);
                p_.rankingConversion_--;
                if (usable) return userDefinedRank(given, want);
            }
            // **And a standard conversion makes that temporary too.**
            if (takesTemporary) {
                const Rank direct = rankArgument(arg, want->unqualified());
                if (direct != Rank::None) return direct;
            }
            return Rank::None;
        }
        if (!want->isConst() && given->isConst()) return Rank::None;

        // **No rvalue reference for an lvalue** - `++x`, `x = 5` and `x += 2` among them (M1 L12).
        if (param->isRValueReference() && markedLvalue(arg)) return Rank::None;
        // `const T &&` for a `T` rvalue adds a qualifier, as `const T &` does below - [over.ics.rank]/3.2.6,
        // so that `fwd(std::move(x))` picks fwd<int> over the fwd<const int> an earlier call made (1.27).
        if (param->isRValueReference())
            return want->isConst() && !given->isConst() ? Rank::Qualification : Rank::Identity;

        // **A value has no lvalue to bind, so `T &` is not viable for one**.
        if (!markedLvalue(arg)) return want->isConst() ? Rank::Qualification : Rank::None;

        return want->isConst() && !given->isConst() ? Rank::Qualification
                                                    : Rank::Identity;
    }

    const Type *from = p_.decayedType(given);
    const Type *to = param;

    if (from == to) return Rank::Identity;
    // Top-level const on the parameter is not part of its type for this purpose.
    if (from->unqualified() == to->unqualified()) return Rank::Identity;

    // Nothing converts to an enumeration implicitly - [conv.integral]/1 starts from one - and a
    // scoped one converts to nothing, [dcl.enum]/10.
    if (from->isArithmetic() && to->unqualified()->isEnumeration()) return Rank::None;
    if (from->isScopedEnumeration()) return Rank::None;
    if (from->isArithmetic() && to->isArithmetic())
        return isPromotion(from, to) ? Rank::Promotion : Rank::Conversion;

    if (to->isPointer() && from->isPointer()) {
        // A qualification conversion - char * to const char *.
        if (qualificationConvertible(from, to)) return Rank::Qualification;
        // Derived * to Base * is a pointer conversion, which ranks below a
        // promotion - so f(Base *) loses to f(Derived *) for a Derived *,
        // which is what [over.ics.rank] asks for.
        if (from->pointee()->unqualified() != to->pointee()->unqualified() &&
            p_.accessibleBaseOffset(from->pointee(), to->pointee()) >= 0 &&
            (to->pointee()->isConst() || !from->pointee()->isConst()))
            return Rank::Conversion;
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            from->pointee()->isConst())
            return Rank::None;
        // [conv.ptr]/2: any object pointer to void *, and never back without a cast.
        if (to->pointee()->isVoid()) return Rank::Conversion;
        return Rank::None;
    }

    if (to->isPointer() && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;

    // **`nullptr` converts to any pointer and to any pointer to member**, both
    // pointer conversions - so `f(void *)` and `f(char *)` are ambiguous for it, as
    // clang reports. **And not to bool here**: that conversion is direct-init only.
    if (from->isNullPtr() && (to->isPointer() || to->isMemberPointer()))
        return Rank::Conversion;
    if (p_.memberPointerToDerived(from, to) > 0) return Rank::Conversion;

    // And the literal 0 converts *to* std::nullptr_t, at the same rank - which
    // is why `f(0)` against `f(decltype(nullptr))` and `f(char *)` is
    // ambiguous, measured.
    if (to->isNullPtr() && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;

    // [over.best.ics]/6: a derived object passed by value to a base is a derived-to-base Conversion.
    if (to->unqualified()->isStructOrUnion() && from->unqualified()->isStructOrUnion() &&
        to->unqualified() != from->unqualified() && p_.accessibleBaseOffset(from, to->unqualified()) >= 0)
        return Rank::Conversion;

    // **Last of all, a user-defined conversion** - [over.ics.rank]/2 puts one
    // below every standard conversion, so this is asked only where nothing
    // above it answered.
    if (p_.rankingConversion_ == 0) {
        p_.rankingConversion_++;
        const bool usable =
            to->unqualified()->isStructOrUnion()
                ? (p_.convertingConstructor(to, arg) != nullptr ||
                   (from->unqualified()->isStructOrUnion() &&
                    p_.conversionFunction(from, to->unqualified()) != nullptr))
                : (from->unqualified()->isStructOrUnion() &&
                   p_.conversionFunction(from, to) != nullptr);
        p_.rankingConversion_--;
        if (usable) return userDefinedRank(from, to);
    }

    return Rank::None;
}

// [over.ics.rank]/3.3: user-defined sequences through one conversion function are told apart by their second
// standard conversion: `lng(long)` beats `lng(int)` for `operator long()`. One scalar conversion function reaches
// every scalar target, sub-rank its distance from the result; a class target keeps the plain rank (1.27, item 3).
Rank Overload::userDefinedRank(const Type *from, const Type *to) {
    const Type *want = to->unqualified();
    if (want->isStructOrUnion()) return Rank::UserDefined;
    const Signature *via = p_.soleNumericConversion(from);
    if (via == nullptr) return Rank::UserDefined;
    const Type *gives = via->returns->unqualified();
    if (gives == want) return Rank::UserDefined;
    if (gives->isPointer() && want->isPointer() && qualificationConvertible(gives, want))
        return Rank::UserDefined;
    if (gives->isArithmetic() && want->isArithmetic() && isPromotion(gives, want))
        return Rank::UserDefinedPromotion;
    return Rank::UserDefinedConversion;
}

std::string Overload::describeSignature(const Signature &f) {
    std::string out = f.name + "(";
    for (std::size_t i = 0; i < f.params.size(); i++) {
        if (i > 0) out += ", ";
        out += f.params[i]->describe();
    }
    if (f.variadic) out += f.params.empty() ? "..." : ", ...";
    out += ")";
    if (f.constThis) out += " const";
    return out + (f.refThis == RefQual::LValue ? " &" : f.refThis == RefQual::RValue ? " &&" : "");
}

// [over.match.funcs]/4: `X &&` for `&&`, else `X &` - and an rvalue still binds an unqualified
// member's; `const &` binds an rvalue as any const lvalue reference does, [dcl.init.ref]/5.
bool Overload::refQualifierViable(RefQual ref, bool constThis, bool objectLvalue) {
    if (ref == RefQual::None) return true;
    if (ref == RefQual::RValue) return !objectLvalue;
    return objectLvalue || constThis;
}

std::string Overload::refQualifierRefusal(const std::string &name, bool objectLvalue) {
    return "'this' argument to member function '" + name + "' is an " +
           (objectLvalue ? "lvalue, but function has rvalue ref-qualifier"
                         : "rvalue, but function has non-const lvalue ref-qualifier");
}

// The best viable function, or a refusal naming every candidate: F beats G
// when it is no worse on every argument and better on one, and two that each
// win an argument are the ambiguity.
const Type *Overload::rankedParameter(const Signature &f, std::size_t i,
                                    bool ranksObject) {
    if (ranksObject) {
        if (i == 0) return nullptr;
        i--;
    }
    return i < f.params.size() ? f.params[i] : nullptr;
}

// **A by-value parameter and a reference to the same type are the same
// match**, and [over.ics.rank] gives neither a way to beat the other: one
// copies the argument, one binds it, and both are the identity conversion.
bool Overload::sameMatchEitherWay(const Type *pa, const Type *pb) {
    if (pa == nullptr || pb == nullptr) return false;
    const bool ra = pa->isReference(), rb = pb->isReference();
    if (ra == rb) return false;
    const Type *va = ra ? pa->pointee()->unqualified() : pa->unqualified();
    const Type *vb = rb ? pb->pointee()->unqualified() : pb->unqualified();
    return va == vb;
}

// **Two parameters that took one argument at the same rank** (1.27, review item 3), per [over.ics.rank]: /3.2.3 an
// rvalue reference binding an rvalue beats an lvalue one; /3.2.6, /4.3 the less cv-qualified reference or pointer
// target; /4.4 the nearer base (`H *` over `G *` for an `I *`), class over `void *`. /4 needs the argument.
int Overload::closerTarget(const Type *pa, const Type *pb, const Type *given) const {
    if (pa == nullptr || pb == nullptr || given == nullptr) return 0;
    struct Nearer {
        static int derived(const Parser &p, const Type *a, const Type *b, const Type *from) {
            if (from == nullptr || !from->isStructOrUnion()) return 0;
            if (a == b || !a->isStructOrUnion() || !b->isStructOrUnion()) return 0;
            if (p.accessibleBaseOffset(a, b) >= 0) return 1;
            if (p.accessibleBaseOffset(b, a) >= 0) return -1;
            return 0;
        }
    };
    if (pa->isReference() && pb->isReference()) {
        const Type *ta = pa->pointee(), *tb = pb->pointee();
        if (ta->unqualified() == tb->unqualified()) {
            if (ta->isConst() != tb->isConst()) return ta->isConst() ? -1 : 1;
            if (pa->isRValueReference() != pb->isRValueReference())
                return pa->isRValueReference() ? 1 : -1;
            return 0;
        }
        return Nearer::derived(p_, ta->unqualified(), tb->unqualified(), given->unqualified());
    }
    if (pa->isPointer() && pb->isPointer() && (given->isPointer() || given->isArray())) {
        const Type *ta = pa->pointee(), *tb = pb->pointee();
        const Type *object = given->pointee()->unqualified();   // an array decays to its element's
        if (ta->unqualified() == tb->unqualified())
            return ta->isConst() == tb->isConst() ? 0 : ta->isConst() ? -1 : 1;
        if (tb->isVoid() && ta->unqualified()->isStructOrUnion() && object->isStructOrUnion()) return 1;
        if (ta->isVoid() && tb->unqualified()->isStructOrUnion() && object->isStructOrUnion()) return -1;
        return Nearer::derived(p_, ta->unqualified(), tb->unqualified(), object);
    }
    if (!pa->isReference() && !pb->isReference())
        return Nearer::derived(p_, pa->unqualified(), pb->unqualified(), given->unqualified());
    return 0;
}

bool Overload::betterCandidate(const std::vector<Rank> &a,
                             const std::vector<Rank> &b,
                             const Signature &fa, const Signature &fb,
                             bool ranksObjectA, bool ranksObjectB) const {
    bool better = false, worse = false;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i++) {
        const Type *pa = rankedParameter(fa, i, ranksObjectA);
        const Type *pb = rankedParameter(fb, i, ranksObjectB);
        if (a[i] == b[i]) {
            // [over.ics.rank]/3.3 then /3.2.3: one conversion making a temporary for both, `&&` binds it as an rvalue.
            if (a[i] >= Rank::UserDefined && a[i] <= Rank::UserDefinedConversion && pa != nullptr && pb != nullptr &&
                pa->isReference() && pb->isReference() && pa->isRValueReference() != pb->isRValueReference() &&
                pa->referent()->unqualified() == pb->referent()->unqualified()) {
                if (pa->isRValueReference()) better = true;
                else worse = true;
                continue;
            }
            if (a[i] > Rank::Conversion) continue;
            const int closer =
                closerTarget(pa, pb, i < p_.rankingGiven_.size() ? p_.rankingGiven_[i] : nullptr);
            if (closer > 0) better = true;
            if (closer < 0) worse = true;
            continue;
        }
        if (sameMatchEitherWay(pa, pb) &&
            a[i] <= Rank::Qualification && b[i] <= Rank::Qualification)
            continue;
        // [over.ics.rank]/3.3: a user-defined sequence is compared with another only through the
        // same conversion function; a class target was reached by a constructor, so with one on
        // either side the two stay level.
        if (a[i] >= Rank::UserDefined && a[i] <= Rank::UserDefinedConversion &&
            b[i] >= Rank::UserDefined && b[i] <= Rank::UserDefinedConversion) {
            const Type *va = pa != nullptr ? (pa->isReference() ? pa->pointee() : pa) : nullptr;
            const Type *vb = pb != nullptr ? (pb->isReference() ? pb->pointee() : pb) : nullptr;
            if (va == nullptr || vb == nullptr || va->unqualified()->isStructOrUnion() ||
                vb->unqualified()->isStructOrUnion())
                continue;
        }
        if (a[i] < b[i]) better = true;
        if (a[i] > b[i]) worse = true;
    }
    if (better && worse) return false;
    if (better) return true;
    if (worse) return false;
    // [over.ics.rank]/3.2.3: two ref-qualified members both viable means an rvalue object,
    // which `&&` binds as an rvalue reference where `const &` binds an lvalue one.
    if (ranksObjectA && ranksObjectB && fa.refThis != fb.refThis &&
        fa.refThis != RefQual::None && fb.refThis != RefQual::None)
        return fa.refThis == RefQual::RValue;
    if (fa.fromTemplate && fb.fromTemplate && fa.pattern != nullptr &&
        fb.pattern != nullptr)
        return moreSpecializedFunction(fa, fb);
    return !fa.fromTemplate && fb.fromTemplate;
}

bool Overload::moreSpecializedFunction(const Signature &a, const Signature &b) const {
    struct Order {
        static bool atLeast(const Parser &p, const Type *a, const Type *b) {
            const std::vector<const Type *> &pa = a->params(), &pb = b->params();
            if (pa.size() != pb.size()) return false;
            // b's parameters bound afresh; a's stand as the opaque types they are.
            std::vector<const Type *> binding(pb.size() + pa.size());
            std::string why;
            for (std::size_t i = 0; i < pa.size(); i++)
                if (!p.tmpl_.matchPattern(pb[i], pa[i], &binding, &why)) return false;
            return true;
        }
    };
    return Order::atLeast(p_, a.pattern, b.pattern) &&
           !Order::atLeast(p_, b.pattern, a.pattern);
}


OperatorChoice Overload::resolveOperator(const std::string &name,
                                               const Expr &left,
                                               const Expr *right,
                                               std::size_t pos) {
    // **A non-member operator may be a function template.** Deduce and
    // instantiate every one that applies before ranking, so `a + b` for a
    // class template reaches `template <int N> V<N> operator+(V<N>, V<N>)`.
    {
        std::vector<const Type *> argTypes;
        argTypes.push_back(p_.tmpl_.deductionArgType(left));
        if (right != nullptr) argTypes.push_back(p_.tmpl_.deductionArgType(*right));
        p_.tmpl_.instantiateViableTemplates(name, argTypes, pos);
    }

    // **A member operator may be a template too**, `H * x` where operator* is
    // written `template <int VMAX> V<VMAX> operator*(const V<VMAX> &) const`.
    if (right != nullptr) {
        const Type *lp = left.type()->unqualified();
        if (lp->isStructOrUnion()) {
            std::vector<const Type *> one;
            one.push_back(p_.tmpl_.deductionArgType(*right));
            p_.tmpl_.instantiateViableMemberTemplates(p_.declaringClassOf(lp, name), name, one, pos);
        }
    }

    // A unary operator is the same question with one operand: a member takes it as
    // its implicit object and writes no parameter, a non-member writes one. One
    // rank each way, so the comparison is the same comparison.
    const std::size_t written = right != nullptr ? 1u : 0u;
    std::vector<std::vector<Rank> > ranks;
    std::vector<std::size_t> which;      // index into functions_
    std::vector<bool> member;

    const Type *lt = left.type();
    const Type *plain = lt->unqualified();
    if (plain->isStructOrUnion()) {
        const Type *owner = p_.lookup_.findMemberOwner(plain, name);
        if (owner != nullptr) {
            const std::string key = owner->tag() + "::" + name;
            if (const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(key))
                for (std::size_t k = 0; k < set->size(); k++) {
                    const Signature &f = p_.functions_[(*set)[k]];
                    if (f.params.size() != written) continue;
                    // The object parameter binds like any other reference: exact
                    // where the constness agrees, a qualification conversion for a
                    // const member on a non-const object, no match the other way.
                    if (lt->isConst() && !f.constThis) continue;
                    if (!refQualifierViable(f.refThis, f.constThis, objectIsLvalue(left))) continue;
                    std::vector<Rank> r;
                    r.push_back(lt->isConst() == f.constThis ? Rank::Identity
                                                             : Rank::Qualification);
                    if (right != nullptr) {
                        Rank second = rankArgument(*right, f.params[0]);
                        if (second == Rank::None) continue;
                        r.push_back(second);
                    }
                    ranks.push_back(r);
                    which.push_back((*set)[k]);
                    member.push_back(true);
                }
        }
    }

    const std::vector<std::string> keys =
        p_.lookup_.lookupKeys(name, lt, right != nullptr ? right->type() : nullptr);
    for (std::size_t ki = 0; ki < keys.size(); ki++)
    if (const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(keys[ki]))
        for (std::size_t k = 0; k < set->size(); k++) {
            // Two keys may reach one function - the bare name and its namespace's - and it is one candidate.
            if (std::find(which.begin(), which.end(), (*set)[k]) != which.end()) continue;
            const Signature &f = p_.functions_[(*set)[k]];
            if (f.params.size() != written + 1) continue;
            // [over.match.oper]/3.2: with no operand of class type, a non-member is a candidate only if a
            // parameter takes an operand's enumeration itself - `op == __fn_clone` is no call (M1).
            if (!plain->isStructOrUnion() && (right == nullptr || !right->type()->unqualified()->isStructOrUnion())) {
                auto takes = [](const Type *param, const Type *operand) {
                    const Type *e = operand->unqualified();
                    const Type *p = param->isReference() ? param->referent() : param;
                    return e->isEnumeration() && p->unqualified() == e;
                };
                if (!takes(f.params[0], lt) && !(right != nullptr && takes(f.params[1], right->type()))) continue;
            }
            Rank a = rankArgument(left, f.params[0]);
            if (a == Rank::None) continue;
            std::vector<Rank> r;
            r.push_back(a);
            if (right != nullptr) {
                Rank b = rankArgument(*right, f.params[1]);
                if (b == Rank::None) continue;
                r.push_back(b);
            }
            ranks.push_back(r);
            which.push_back((*set)[k]);
            member.push_back(false);
        }

    if (which.empty()) return OperatorChoice::None;

    p_.rankingGiven_.clear();
    p_.rankingGiven_.push_back(lt);
    if (right != nullptr) p_.rankingGiven_.push_back(right->type());
    std::size_t best = 0;
    for (std::size_t k = 1; k < which.size(); k++)
        if (betterCandidate(ranks[k], ranks[best],
                            p_.functions_[which[k]], p_.functions_[which[best]],
                            member[k], member[best]))
            best = k;
    for (std::size_t k = 0; k < which.size(); k++) {
        if (k == best) continue;
        if (!betterCandidate(ranks[best], ranks[k],
                             p_.functions_[which[best]], p_.functions_[which[k]],
                             member[best], member[k])) {
            std::string why = "this use of '" + name + "' is ambiguous";
            for (std::size_t j = 0; j < which.size(); j++)
                why += std::string("\n    candidate: ") +
                       (member[j] ? "member " : "") +
                       describeSignature(p_.functions_[which[j]]);
            p_.src_.fail(pos, why);
        }
    }
    return member[best] ? OperatorChoice::Member : OperatorChoice::NonMember;
}

// The fewest arguments a call may give: every parameter that has no default.
// [dcl.fct.default] requires the defaults to be a suffix, so this is a count
// and not a set.
std::size_t Overload::leastArguments(const Signature &f) const {
    std::map<std::string, std::vector<std::size_t> >::const_iterator it =
        p_.defaultArgs_.find(f.symbol);
    if (it == p_.defaultArgs_.end()) return f.params.size();
    std::size_t least = f.params.size();
    while (least > 0 && least <= it->second.size() && it->second[least - 1] != 0)
        least--;
    return least;
}


Signature Overload::resolveOverload(const std::string &written,
                                          const std::vector<ExprPtr> &args,
                                          std::size_t pos,
                                          const Type *object, bool objectLvalue) {
    p_.refuseOverloadArguments(args, written);
    // An unqualified name inside a namespace names that namespace's function
    // if it has one - the enclosing scopes are tried from the innermost out,
    // then whatever a `using namespace` has opened.
    std::string name = object != nullptr
                     ? written
                     : p_.lookup_.qualifyForLookup(written, &sema::Lookup::hasFunctionNamed);
    // An argument's own namespace is searched too - `twice(s)` finds
    // `N::twice` when `s` is an `N::S`. Only where nothing else answered, so a
    // name that is in scope is never quietly outranked by a far one.
    if (object == nullptr && p_.lookup_.overloadsOf(name) == nullptr && !args.empty()) {
        const std::vector<std::string> keys =
            p_.lookup_.lookupKeys(written, args[0]->type(),
                       args.size() > 1 ? args[1]->type() : nullptr);
        if (!keys.empty()) name = keys.back();
    }
    // **A constructor template is a candidate too**: every one under this key that
    // deduces from the arguments is instantiated before the set is read.
    if (p_.memberTemplates_.count(name) != 0) {
        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < args.size(); i++) argTypes.push_back(p_.tmpl_.deductionArgType(*args[i]));
        p_.tmpl_.instantiateViableMemberTemplatesUnder(name, argTypes, pos);
    }
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(name);

    // **For an operator the candidate set is the union, not the first scope
    // that answers.**
    std::vector<std::size_t> merged;
    if (object == nullptr && !args.empty() &&
        written.compare(0, 8, "operator") == 0) {
        const std::vector<std::string> keys =
            p_.lookup_.lookupKeys(written, args[0]->type(),
                       args.size() > 1 ? args[1]->type() : nullptr);
        for (std::size_t i = 0; i < keys.size(); i++)
            if (const std::vector<std::size_t> *s = p_.lookup_.overloadsOf(keys[i]))
                for (std::size_t k = 0; k < s->size(); k++) {
                    bool seen = false;
                    for (std::size_t j = 0; j < merged.size() && !seen; j++)
                        seen = merged[j] == (*s)[k];
                    if (!seen) merged.push_back((*s)[k]);
                }
        if (!merged.empty()) set = &merged;
    }

    if (set == nullptr) {
        // Only constructor templates, and none of them deduced from these arguments.
        if (p_.memberTemplates_.count(name) != 0)
            p_.src_.fail(pos, "no constructor template of '" + name.substr(0, name.rfind("::")) +
                           "' can be used with these " + std::to_string(args.size()) +
                           " argument(s)");
        // **`C(...)` where C is a class** is a temporary and not a call to a
        // function nobody declared; "no prototype" sends the reader after a
        // declaration never meant to exist. Live now a class is copied by value.
        if (const Type *cls = p_.lookup_.findTypedef(name))
            if (cls->isStructOrUnion())
                p_.src_.fail(pos, "'" + name + "(...)' makes a temporary of type '" +
                               cls->describe() + "', which is not supported yet - "
                               "name a variable of that type and use that");
        p_.src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
    }

    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;
    // Set when only the object's constness stopped a candidate, for the message.
    bool droppedForConst = false;
    bool droppedForRef = false;

    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = p_.functions_[(*set)[k]];
        // A default makes the parameter optional, so the count a call may
        // give is a range rather than one number.
        if (f.variadic ? args.size() < leastArguments(f)
                       : (args.size() > f.params.size() ||
                          args.size() < leastArguments(f))) continue;

        // **The implicit object parameter goes first**, and ranking it separates `get()` from `get() const`:
        // exact where the constness agrees, a qualification conversion where a const member takes a non-const
        // object. A static member's matches any object, [over.match.funcs]/4.
        std::vector<Rank> r;
        if (object != nullptr && f.isStaticMember) {
            r.push_back(Rank::Identity);
        } else if (object != nullptr) {
            if (object->isConst() && !f.constThis) { droppedForConst = true; continue; }
            if (!refQualifierViable(f.refThis, f.constThis, objectLvalue)) { droppedForRef = true; continue; }
            r.push_back(object->isConst() == f.constThis ? Rank::Identity
                                                         : Rank::Qualification);
        }

        const std::size_t first = r.size();
        r.resize(first + args.size(), Rank::Ellipsis);
        // Only the arguments written are ranked; the defaults are the same
        // expression for every candidate that has them and cannot separate
        // two of them.
        bool ok = true;
        for (std::size_t i = 0; i < args.size() && ok; i++) {
            if (i >= f.params.size()) continue;      // reached by the ellipsis
            r[first + i] = rankArgument(*args[i], f.params[i]);
            if (r[first + i] == Rank::None) ok = false;
        }
        if (!ok) continue;
        viable.push_back((*set)[k]);
        ranks.push_back(r);
    }

    if (viable.empty() && droppedForConst)
        p_.src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const - calling it could change what the "
                       "const promised not to");
    if (viable.empty() && droppedForRef) p_.src_.fail(pos, refQualifierRefusal(name, objectLvalue));

    if (viable.empty()) {
        std::string why = "no function called '" + name + "' takes these " +
                          std::to_string(args.size()) + " argument(s)";
        for (std::size_t k = 0; k < set->size(); k++)
            why += "\n    candidate: " + describeSignature(p_.functions_[(*set)[k]]);
        p_.src_.fail(pos, why);
    }
    if (viable.size() == 1) {
        p_.refuseDeleted(p_.functions_[viable[0]], pos);
        p_.noteUsed(viable[0]);
        if (!p_.functions_[viable[0]].isNoexcept) p_.mayThrow_++;
        return p_.functions_[viable[0]];
    }

    p_.rankingGiven_.clear();
    if (object != nullptr) p_.rankingGiven_.push_back(object);
    for (std::size_t i = 0; i < args.size(); i++) p_.rankingGiven_.push_back(args[i]->type());
    std::size_t best = 0;
    for (std::size_t k = 1; k < viable.size(); k++)
        if (betterCandidate(ranks[k], ranks[best],
                            p_.functions_[viable[k]], p_.functions_[viable[best]],
                            object != nullptr, object != nullptr))
            best = k;
    for (std::size_t k = 0; k < viable.size(); k++) {
        if (k == best) continue;
        if (!betterCandidate(ranks[best], ranks[k],
                             p_.functions_[viable[best]], p_.functions_[viable[k]],
                             object != nullptr, object != nullptr)) {
            std::string why = "this call to '" + name + "' is ambiguous";
            for (std::size_t j = 0; j < viable.size(); j++)
                why += "\n    candidate: " + describeSignature(p_.functions_[viable[j]]);
            p_.src_.fail(pos, why);
        }
    }
    // [dcl.fct.def.delete]: a deleted candidate is ranked like any other and refused only where it wins.
    p_.refuseDeleted(p_.functions_[viable[best]], pos);
    p_.noteUsed(viable[best]);
    // **Every call is potentially-throwing unless the function promised
    // otherwise**, and this is the one place a call finds out which function it
    // reached. `noexcept(e)` reads the count this leaves behind.
    if (!p_.functions_[viable[best]].isNoexcept) p_.mayThrow_++;
    return p_.functions_[viable[best]];
}

}   // namespace sema
