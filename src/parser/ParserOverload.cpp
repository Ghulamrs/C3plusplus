// The parser: conversions and overload resolution. The implicit conversion
// sequences of [conv] ranked as [over.ics] ranks them, the tournament that
// ranking decides, and the assignability check that asks the same questions.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <algorithm>
#include <climits>
#include <cstring>

const Type *Parser::unsignedVersion(const Type *t) const {
    switch (t->kind()) {
    case Kind::Int:      return types_.get(Kind::UInt);
    case Kind::Long:     return types_.get(Kind::ULong);
    case Kind::LongLong: return types_.get(Kind::ULongLong);
    default:             return t;
    }
}

const Type *Parser::promote(const Type *t) const {
    // [conv.prom]/3: an unscoped enumeration promotes as its underlying integer does; a scoped one not at all.
    if (t->isScopedEnumeration()) return t;
    if (t->isEnumeration()) t = types_.get(t->kind());
    if (t->isInteger() && t->rank() < types_.intType()->rank())
        return types_.intType();
    // [conv.prom]/2: char32_t goes to the first of int, unsigned int that holds it.
    if (t->kind() == Kind::Char32) return types_.get(Kind::UInt);
    return t;
}

const Type *Parser::usualArithmetic(const Type *a, const Type *b) const {
    // [expr]/10 starts from an unscoped enumeration: two of one scoped type compare, nothing else mixes.
    if (a->isScopedEnumeration() || b->isScopedEnumeration()) {
        if (a->unqualified() == b->unqualified()) return a->unqualified();
        refuseScopedConversion(a, b, peek().pos);
    }
    if (a->kind() == Kind::LongDouble || b->kind() == Kind::LongDouble)
        return types_.get(Kind::LongDouble);
    if (a->kind() == Kind::Double || b->kind() == Kind::Double)
        return types_.doubleType();
    if (a->kind() == Kind::Float || b->kind() == Kind::Float)
        return types_.get(Kind::Float);

    a = promote(a);
    b = promote(b);
    if (a == b) return a;

    bool as = a->isSigned(target_), bs = b->isSigned(target_);
    const Type *hi = a->rank() >= b->rank() ? a : b;
    if (as == bs) return hi;

    const Type *uns = as ? b : a;
    const Type *sig = as ? a : b;
    if (uns->rank() >= sig->rank()) return uns;
    if (sig->size(target_) > uns->size(target_)) return sig;
    return unsignedVersion(sig);
}

// Defined below, beside the conversion rules it belongs with.
int publicBaseOffset(const Type *derived, const Type *base);

static bool publiclyDerivedFrom(const Type *derived, const Type *base);
static int anyBaseOffset(const Type *derived, const Type *base);

// [dcl.enum]/10: what makes `enum class` worth writing, said where the conversion would be made.
void Parser::refuseScopedConversion(const Type *from, const Type *to, std::size_t pos) const {
    auto shown = [](const Type *t) {
        const Type *u = t->unqualified();
        if (u->isScopedEnumeration()) return "enum class " + u->enumTag();
        return u->isEnumeration() ? "enum " + u->enumTag() : t->describe();
    };
    src_.fail(pos, "'" + shown(from) + "' does not convert to '" + shown(to) + "' implicitly - a "
                   "scoped enumeration converts only with a cast, [dcl.enum]/10");
}

// The built-in arithmetic and bitwise operators take no scoped enumeration, [expr]/10.
void Parser::refuseScopedOperand(const Type *a, const Type *b, const char *op, std::size_t pos) const {
    const Type *t = a != nullptr && a->unqualified()->isScopedEnumeration() ? a
                  : b != nullptr && b->unqualified()->isScopedEnumeration() ? b : nullptr;
    if (t == nullptr) return;
    src_.fail(pos, "'" + std::string(op) + "' does not take 'enum class " + t->unqualified()->enumTag() +
                   "' - a scoped enumeration has no arithmetic; '==' and '<' compare two of one "
                   "type, and a cast reaches the integer");
}

ExprPtr Parser::baseSubobject(ExprPtr e, const Type *base) const {
    const Type *from = e->type();
    const Type *want = from->isConst() ? types_.withConst(base->unqualified()) : base->unqualified();
    ExprPtr at(new Unary('&', std::move(e)));
    at->setType(types_.pointerTo(from));
    at = convert(std::move(at), types_.pointerTo(want), true);
    ExprPtr sub(new Unary('*', std::move(at)));
    sub->setType(want);
    return sub;
}

// The node a conversion makes carries the syntax of the one it converts (M2 step 2): a wrapper is not a node written.
ExprPtr Parser::convert(ExprPtr e, const Type *to, bool allowExplicit) const {
    const syntax::NodeId written = e ? e->syntax() : syntax::NodeId();
    ExprPtr r = convertNode(std::move(e), to, allowExplicit);
    if (r && !r->syntax().valid()) r->setSyntax(written);
    return r;
}

ExprPtr Parser::convertNode(ExprPtr e, const Type *to, bool allowExplicit) const {
    if (e->type() == to) return e;
    // A scoped enumeration converts only by a cast, in either direction - [dcl.enum]/10, [expr.static.cast]/9.
    if (!allowExplicit && e->type() != nullptr && e->type()->unqualified() != to->unqualified() &&
        (e->type()->isScopedEnumeration() || to->isScopedEnumeration()) && !to->isReference())
        refuseScopedConversion(e->type(), to, peek().pos);

    // **A class converted by its own conversion function**, which is where a
    // class reaches a number, a pointer or a bool.
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion() &&
        !to->unqualified()->isStructOrUnion()) {
        Parser *self = const_cast<Parser *>(this);
        if (const Signature *how = self->conversionFunction(e->type(), to, allowExplicit)) {
            const Type *object = e->type();
            std::vector<ExprPtr> none;
            ExprPtr got = self->memberCallWith(std::move(e), object, how->name,
                                               0, std::move(none));
            return got->type() == to ? std::move(got)
                                     : convert(std::move(got), to);
        }
    }

    // **A class converted to another class by its own conversion function**,
    // [class.conv.fct] - the same one rule as above, only the target is a
    // class too: `operator B()` on an A makes a B.
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion() &&
        to->unqualified()->isStructOrUnion() &&
        e->type()->unqualified() != to->unqualified() &&
        anyBaseOffset(e->type()->unqualified(), to->unqualified()) < 0) {
        Parser *self = const_cast<Parser *>(this);
        if (const Signature *how = self->conversionFunction(e->type(), to, allowExplicit)) {
            const Type *object = e->type();
            std::vector<ExprPtr> none;
            return self->memberCallWith(std::move(e), object, how->name,
                                        0, std::move(none));
        }
    }

    // **A class made by one of its converting constructors**, [over.match.ctor]: `(CMatrix<3,3>)v`
    // and `static_cast<S>(2.5)` are direct-initialisations, so an explicit one is taken too.
    if (e->type() != nullptr && to->unqualified()->isStructOrUnion() &&
        e->type()->unqualified() != to->unqualified() &&
        !(e->type()->unqualified()->isStructOrUnion() &&
          anyBaseOffset(e->type()->unqualified(), to->unqualified()) >= 0)) {
        Parser *self = const_cast<Parser *>(this);
        if (const Signature *ctor = self->convertingConstructor(to, *e, 0, allowExplicit)) {
            self->markUsed(ctor);
            std::vector<ExprPtr> one;
            one.push_back(std::move(e));
            return self->constructTemporary(to->unqualified(), *ctor, std::move(one), false, 0);
        }
    }

    // **A class differing only in const-ness is the same object**, so there is
    // nothing here to convert: [conv.qual] changes the type and not the value,
    // and the object keeps its address and its bytes.
    if (to->unqualified() == e->type()->unqualified() &&
        to->unqualified()->isStructOrUnion()) {
        e->setType(to);
        return e;
    }

    // **A derived object copied to a base with no copy constructor of its own is its base subobject**,
    // [class.copy]/12 - slicing, `T t = u;` and `w = u`: the copy is of bytes, so it is a load of
    // the subobject. A prvalue is kept in a frame slot first, as a reference's temporary is.
    Parser *self = const_cast<Parser *>(this);
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion() &&
        to->unqualified()->isStructOrUnion() && e->type()->unqualified() != to->unqualified() &&
        anyBaseOffset(e->type()->unqualified(), to->unqualified()) >= 0 &&
        self->classes_.copyConstructorOf(to->unqualified()) == nullptr &&
        self->classes_.moveConstructorOf(to->unqualified()) == nullptr) {
        const Type *from = e->type();
        ExprPtr keep;
        if (!isGlvalue(*e)) {
            const int slot = self->allocateFrameSlot(from->unqualified());
            const std::string temp = ".sl" + std::to_string(self->refTemps_++);
            ExprPtr target(Var::local(temp, slot));
            target->setType(from->unqualified());
            keep.reset(new Assign(std::move(target), std::move(e)));
            keep->setType(from->unqualified());
            e.reset(Var::local(temp, slot));
            e->setType(from->unqualified());
            from = from->unqualified();
        }
        ExprPtr base = baseSubobject(std::move(e), to->unqualified());
        if (keep == nullptr) return base;
        const Type *baseType = base->type();
        ExprPtr both(new Comma(std::move(keep), std::move(base)));
        both->setType(baseType);
        return both;
    }
    // **A null data member pointer is -1 on both ABIs** (offset 0 is a member), {0, -1} in Microsoft's virtual
    // model; zero made a member at 0 null and a null from clang or cl not. `nullptr` or 0 gives a member function
    // pointer the pair with a null code word, [conv.mem]/1 (1.27, 2026-10-21, review of 1.25, item 3).
    const bool nullConstant = e->type()->isNullPtr() || (e->type()->isInteger() && isNullConstant(*e));
    if (nullConstant && to->unqualified()->isMemberFunctionPointer())
        return const_cast<Parser *>(this)->nullMemberFunctionPointer(to->unqualified());
    if (nullConstant && to->unqualified()->isMemberPointer()) {
        const bool virtualModel = target_.microsoftNames() && to->unqualified()->size(target_) == 8;
        ExprPtr n(new Num(virtualModel ? -4294967296LL : -1LL));
        n->setType(types_.get(Kind::LongLong));
        ExprPtr asTo(new Cast(to->unqualified(), std::move(n)));
        asTo->setType(to);
        return asTo;
    }
    {
        // A member pointer of a base becoming one of the derived class: the
        // value moves by the base's offset, or the pair is rebuilt where the
        // two classes' models differ in width; at 0 and one width, only the type.
        const int shift = memberPointerToDerived(e->type(), to);
        const Type *ft = e->type()->unqualified();
        if (shift >= 0 && ft->enclosing()->unqualified() != to->unqualified()->enclosing()->unqualified()) {
            if (shift > 0 || ft->size(target_) != to->unqualified()->size(target_))
                return convertMemberPointer(std::move(e), to->unqualified(), shift);
            e->setType(to);
            // The `&` node's record takes the type with it, as the retyped expression does (M3b.2).
            if (e->resolved() != 0 && resolvedTable_[e->resolved()].what == Resolved::Value) {
                Parser *self = const_cast<Parser *>(this);
                Resolved r = resolvedTable_[e->resolved()];
                r.type = to;
                if (e->syntax().valid() && self->resolutions_.count(e->syntax().v) != 0) self->resolutions_[e->syntax().v] = r;
                e = self->resolve(std::move(e), r);
            }
            return e;
        }
    }

    // **Derived * to Base * moves the value where the base is not the first one**
    // - B at 4 walks the pointer forward by four. The null check is the rule and
    // not caution: a null pointer converts to one, and `(char *)0 + 4` is not null.
    if (to->isPointer() && e->type()->isPointer() &&
        to->pointee()->isStructOrUnion() && e->type()->pointee()->isStructOrUnion()) {
        const int off = anyBaseOffset(e->type()->pointee(), to->pointee());
        // **A virtual base is not at a constant offset from the derived
        // pointer**, so the walk forward is a number read out of the vtable
        // rather than one written here.
        const Type *srcCls = e->type()->pointee()->unqualified();
        const Type *dstCls = to->pointee()->unqualified();
        const long long vbBack = target_.microsoftNames()
                               ? 0 : classes_.itaniumVbaseOffsetSlot(srcCls, dstCls);
        // **The same walk on the Microsoft ABI, through the vbtable.**
        if (target_.microsoftNames() && srcCls->vbptrOffset() >= 0 &&
            classes_.virtualBaseSlot(srcCls, dstCls, nullptr) > 0) {
            Parser *self = const_cast<Parser *>(this);
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            ExprPtr asChars(new Cast(chars, std::move(e)));
            asChars->setType(chars);
            const int slot = self->allocateFrameSlot(chars);
            const std::string temp = ".vp" + std::to_string(self->refTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(chars);
            ExprPtr save(new Assign(std::move(held), std::move(asChars)));
            save->setType(chars);

            ExprPtr step = classes_.microsoftVirtualBaseStep(srcCls, dstCls, temp, slot,
                                                    nullptr);
            ExprPtr from(Var::local(temp, slot));
            from->setType(chars);
            ExprPtr moved(new Binary(BinOp::Add, std::move(from),
                                     std::move(step)));
            moved->setType(chars);
            ExprPtr asTo(new Cast(to, std::move(moved)));
            asTo->setType(to);
            ExprPtr whole(new Comma(std::move(save), std::move(asTo)));
            whole->setType(to);
            return whole;
        }
        if (vbBack != 0 && !target_.microsoftNames()) {
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            const Type *offType = vtableOffsetType();
            ExprPtr asChars(new Cast(chars, std::move(e)));
            asChars->setType(chars);
            int slot = const_cast<Parser *>(this)->allocateFrameSlot(chars);
            std::string temp = ".vp" +
                std::to_string(const_cast<Parser *>(this)->refTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(chars);
            ExprPtr save(new Assign(std::move(held), std::move(asChars)));
            save->setType(chars);

            ExprPtr obj(Var::local(temp, slot));
            obj->setType(chars);
            ExprPtr asTable(new Cast(types_.pointerTo(chars), std::move(obj)));
            asTable->setType(types_.pointerTo(chars));
            ExprPtr vptr(new Unary('*', std::move(asTable)));
            vptr->setType(chars);
            ExprPtr backNum(new Num(vbBack));
            backNum->setType(offType);
            ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(backNum)));
            at->setType(chars);
            ExprPtr asOff(new Cast(types_.pointerTo(offType), std::move(at)));
            asOff->setType(types_.pointerTo(offType));
            ExprPtr delta(new Unary('*', std::move(asOff)));
            delta->setType(offType);

            ExprPtr from(Var::local(temp, slot));
            from->setType(chars);
            ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(delta)));
            moved->setType(chars);
            ExprPtr asTo(new Cast(to, std::move(moved)));
            asTo->setType(to);
            ExprPtr whole(new Comma(std::move(save), std::move(asTo)));
            whole->setType(to);
            return whole;
        }
        if (off > 0) {
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            ExprPtr asChars(new Cast(chars, std::move(e)));
            asChars->setType(chars);

            int slot = const_cast<Parser *>(this)->allocateFrameSlot(chars);
            std::string temp = ".bp" + std::to_string(const_cast<Parser *>(this)->refTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(chars);
            ExprPtr save(new Assign(std::move(held), std::move(asChars)));
            save->setType(chars);

            ExprPtr test(Var::local(temp, slot));
            test->setType(chars);
            ExprPtr shift(Var::local(temp, slot));
            shift->setType(chars);
            ExprPtr by(new Num(static_cast<long long>(off)));
            by->setType(types_.intType());
            ExprPtr moved(new Binary(BinOp::Add, std::move(shift), std::move(by)));
            moved->setType(chars);
            ExprPtr zero(new Num(0LL));
            zero->setType(chars);
            ExprPtr pick(new Conditional(std::move(test), std::move(moved),
                                         std::move(zero)));
            pick->setType(chars);

            ExprPtr both(new Comma(std::move(save), std::move(pick)));
            both->setType(chars);
            ExprPtr out(new Cast(to, std::move(both)));
            out->setType(to);
            return out;
        }
    }

    // A conversion to bool is not a narrowing: [conv.bool] makes every non-zero
    // value true, so (bool)256 is true where (char)256 is 0. Lowered here to a
    // comparison against zero rather than taught to three backends as a cast.
    if (to->isBool() && !e->type()->isBool() && e->type()->isScalar()) {
        const Type *from = e->type();
        ExprPtr zero;
        if (from->isFloating()) {
            zero.reset(new Num(static_cast<long double>(0)));
            zero->setType(from);
        } else {
            ExprPtr n(new Num(static_cast<long long>(0)));
            n->setType(types_.intType());
            zero = convert(std::move(n), from, true);
        }
        ExprPtr test(new Binary(BinOp::Ne, std::move(e), std::move(zero)));
        test->setType(to);
        return test;
    }

    return ExprPtr(new Cast(to, std::move(e)));
}

// **[expr.cast]/4: no cast converts between two unrelated classes on its own.**
// A conversion function is the one road and `convert` takes it; without one
// the bytes were reinterpreted in silence, which clang refuses.
void Parser::refuseUnrelatedClassCast(const Expr &v, const Type *to,
                                      std::size_t pos, const char *what) {
    const Type *from = v.type();
    if (from == nullptr || !to->unqualified()->isStructOrUnion()) return;
    // **A scalar cast to a class has one road, a converting constructor** - without one the Cast
    // that follows would reinterpret the bytes, which `(W)2.5` once did.
    if (!from->unqualified()->isStructOrUnion()) {
        if (convertingConstructor(to, v, pos, true) != nullptr) return;
        src_.fail(pos, std::string(what) + " from '" + from->describe() + "' to '" +
                       to->describe() + "' - no constructor of '" + to->unqualified()->describe() +
                       "' takes one, or two take one equally well");
    }
    if (from->unqualified() == to->unqualified()) return;
    if (publicBaseOffset(from, to) > -1 || publicBaseOffset(to, from) > -1) return;
    if (conversionFunction(from, to->unqualified(), true) != nullptr) return;
    if (convertingConstructor(to, v, pos, true) != nullptr) return;
    src_.fail(pos, std::string(what) + " from '" + from->describe() + "' to '" +
                   to->describe() + "' - the two classes are unrelated and '" +
                   from->unqualified()->describe() + "' has no conversion "
                   "function to '" + to->unqualified()->describe() + "', so "
                   "there is nothing for a cast to do but reinterpret the "
                   "bytes, which no cast does");
}

ExprPtr Parser::decay(ExprPtr e) {
    if (!e->type()->isArray()) return e;
    const Type *to = types_.pointerTo(e->type()->pointee());
    const syntax::NodeId written = e->syntax();
    ExprPtr r(new Cast(to, std::move(e)));
    r->setSyntax(written);
    return r;
}

void Parser::requireScalar(const Expr &e, std::size_t pos, const char *what) {
    // [dcl.enum]/10: a scoped enumeration is no truth value - but it may govern a switch, [stmt.switch]/2.
    if (e.type()->unqualified()->isScopedEnumeration() && std::string(what) == "unary '+'")
        refuseScopedOperand(e.type(), nullptr, "+", pos);
    if (e.type()->unqualified()->isScopedEnumeration() && !atSwitchCondition())
        refuseScopedConversion(e.type(), types_.get(Kind::Bool), pos);
    if (!e.type()->isScalar())
        src_.fail(pos, std::string(what) + " needs a number or a pointer, not '" +
                       e.type()->describe() + "'");
}

// At the `)` of `switch (...)`: the token before the matching `(` says which statement this is.
bool Parser::atSwitchCondition() const {
    if (!peek().is(")") || at_ == 0) return false;
    int depth = 0;
    for (std::size_t i = at_; i-- > 0; ) {
        if (tokens_[i].is(")")) depth++;
        else if (tokens_[i].is("(") && depth-- == 0) return i > 0 && tokens_[i - 1].is("switch");
    }
    return false;
}

// A string literal reaches here wrapped in the Cast that decayed it from an
// array, so the literal has to be looked for underneath.
static bool isStringLiteral(const Expr &e) {
    if (dynamic_cast<const StrLit *>(&e) != nullptr) return true;
    if (const Cast *c = dynamic_cast<const Cast *>(&e))
        return isStringLiteral(c->value());
    return false;
}

// [conv.qual]: a pointer may gain const and never lose it, and const below the
// first level counts only where every level above it is const too. Then whether
// `base` is a public base of `derived`, and at what offset - every base, not one.
int publicBaseOffset(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return -1;
    const Type *d = derived->unqualified();
    const Type *b = base->unqualified();
    if (d == b) return 0;
    const std::vector<Type::BaseSpec> &bases = d->bases();
    for (std::size_t i = 0; i < bases.size(); i++) {
        if (bases[i].access != Access::Public) continue;
        int deeper = publicBaseOffset(bases[i].type, b);
        if (deeper >= 0) return bases[i].offset + deeper;
    }
    return -1;
}

// Every base, whatever its access: where a conversion lands once it has been allowed -
// access is asked where it is checked, and a cast ignores it, [expr.cast]/4.
static int anyBaseOffset(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return -1;
    const Type *d = derived->unqualified();
    if (d == base->unqualified()) return 0;
    for (const Type::BaseSpec &b : d->bases()) {
        const int deeper = anyBaseOffset(b.type, base);
        if (deeper >= 0) return b.offset + deeper;
    }
    return -1;
}

static bool publiclyDerivedFrom(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return false;
    if (derived->unqualified() == base->unqualified()) return false;
    return publicBaseOffset(derived, base) >= 0;
}

static bool throughVirtualBase(const Type *derived, const Type *base) {
    const Type *d = derived->unqualified();
    const std::vector<Type::BaseSpec> &bases = d->bases();
    for (std::size_t i = 0; i < bases.size(); i++) {
        if (bases[i].type->unqualified() == base->unqualified()) return bases[i].isVirtual;
        if (publicBaseOffset(bases[i].type, base) >= 0)
            return bases[i].isVirtual || throughVirtualBase(bases[i].type, base);
    }
    return false;
}

// **[conv.mem]/2: `int B::*` converts to `int D::*` for a D derived from B**,
// the offset or the `this` adjustment growing by the base's. Through a
// virtual base it is ill-formed, which both oracles say; -1 here does too.
int Parser::memberPointerToDerived(const Type *from, const Type *to) const {
    const Type *f = from->unqualified(), *t = to->unqualified();
    const bool data = f->isMemberPointer() && t->isMemberPointer();
    const bool fn = f->isMemberFunctionPointer() && t->isMemberFunctionPointer();
    if (!data && !fn) return -1;
    if (f->enclosing() == nullptr || t->enclosing() == nullptr) return -1;
    if (data ? f->pointee()->unqualified() != t->pointee()->unqualified()
             : f->pointee() != t->pointee()) return -1;
    if (f->enclosing()->unqualified() == t->enclosing()->unqualified()) return 0;
    const int off = publicBaseOffset(t->enclosing(), f->enclosing());
    if (off < 0) return -1;
    if (throughVirtualBase(t->enclosing(), f->enclosing())) return -1;
    return off;
}

ExprPtr Parser::convertMemberPointer(ExprPtr e, const Type *to, int shift) const {
    Parser *self = const_cast<Parser *>(this);
    const Type *ft = e->type()->unqualified();
    if (ft->isMemberPointer()) {
        // Itanium's ptrdiff_t and Microsoft's int, held in the widest lane;
        // a null pointer stays the null of its destination.
        const Type *wide = types_.get(Kind::LongLong);
        const bool fourBytes = ft->size(target_) == 4;
        const Type *lane = fourBytes ? types_.get(Kind::Int) : wide;
        const int slot = self->allocateFrameSlot(ft);
        const std::string name = ".mq" + std::to_string(self->refTemps_++);
        auto held = [&]() { ExprPtr v(Var::local(name, slot)); v->setType(ft); return v; };
        auto asLane = [&]() { ExprPtr c(new Cast(lane, held())); c->setType(lane); return c; };
        auto num = [&](long long n, const Type *t) { ExprPtr v(new Num(n)); v->setType(t); return v; };
        ExprPtr keep(new Assign(held(), std::move(e)));
        keep->setType(ft);
        ExprPtr isNull(new Binary(BinOp::Eq, asLane(), num(-1, lane)));
        isNull->setType(types_.get(Kind::Bool));
        const bool virtualModel = target_.microsoftNames() && to->size(target_) == 8;
        ExprPtr nullTo = num(virtualModel ? -4294967296LL : -1LL, wide);
        ExprPtr widened(new Cast(wide, asLane()));
        widened->setType(wide);
        ExprPtr moved(new Binary(BinOp::Add, std::move(widened), num(shift, wide)));
        moved->setType(wide);
        ExprPtr chosen(new Conditional(std::move(isNull), std::move(nullTo), std::move(moved)));
        chosen->setType(wide);
        ExprPtr asTo(new Cast(to, std::move(chosen)));
        asTo->setType(to);
        ExprPtr whole(new Comma(std::move(keep), std::move(asTo)));
        whole->setType(to);
        return whole;
    }
    // The pair, rebuilt in the destination's shape: the code word copied, the
    // adjustment grown by the base's offset unless the pointer is null, and
    // Microsoft's vbtable index carried or zero.
    const int fromSlot = self->allocateFrameSlot(ft);
    const int toSlot = self->allocateFrameSlot(to);
    const std::string fromName = ".mq" + std::to_string(self->refTemps_++);
    const std::string toName = ".mq" + std::to_string(self->refTemps_++);
    auto src = [&](const char *m) {
        const Member *mm = ft->findMember(m);
        if (mm == nullptr) return ExprPtr();
        ExprPtr v(Var::local(fromName, fromSlot)); v->setType(ft);
        ExprPtr a(new MemberAccess(std::move(v), m, mm->offset, 0, 0)); a->setType(mm->type); return a;
    };
    auto dst = [&](const char *m) {
        const Member *mm = to->findMember(m);
        ExprPtr v(Var::local(toName, toSlot)); v->setType(to);
        ExprPtr a(new MemberAccess(std::move(v), m, mm->offset, 0, 0)); a->setType(mm->type); return a;
    };
    auto num = [&](long long n, const Type *t) { ExprPtr v(new Num(n)); v->setType(t); return v; };
    ExprPtr chain(new Assign(ExprPtr(Var::local(fromName, fromSlot)), std::move(e)));
    chain->setType(ft);
    static const char *const fields[] = { "$fn", "$adj", "$vbi" };
    for (std::size_t i = 0; i < 3; i++) {
        const Member *tm = to->findMember(fields[i]);
        if (tm == nullptr) continue;
        ExprPtr value = src(fields[i]);
        if (value == nullptr) value = num(0, tm->type);
        else if (i == 0 && value->type() != tm->type) value->setType(tm->type);
        if (i == 1 && shift != 0) {
            ExprPtr code = src("$fn");
            ExprPtr zero(new Cast(code->type(), num(0, types_.get(Kind::LongLong))));
            zero->setType(code->type());
            ExprPtr live(new Binary(BinOp::Ne, std::move(code), std::move(zero)));
            live->setType(types_.get(Kind::Bool));
            ExprPtr grow(new Conditional(std::move(live), num(shift, tm->type), num(0, tm->type)));
            grow->setType(tm->type);
            ExprPtr sum(new Binary(BinOp::Add, std::move(value), std::move(grow)));
            sum->setType(tm->type);
            value = std::move(sum);
        }
        ExprPtr put(new Assign(dst(fields[i]), std::move(value)));
        put->setType(tm->type);
        ExprPtr both(new Comma(std::move(chain), std::move(put)));
        both->setType(tm->type);
        chain = std::move(both);
    }
    ExprPtr whole(Var::local(toName, toSlot));
    whole->setType(to);
    ExprPtr at(new Unary('&', std::move(whole)));
    at->setType(types_.pointerTo(to));
    ExprPtr seq(new Comma(std::move(chain), std::move(at)));
    seq->setType(types_.pointerTo(to));
    ExprPtr made(new Unary('*', std::move(seq)));
    made->setType(to);
    return made;
}

// **An array's cv-qualification is its element's** ([conv.qual]/1), so
// `char (*)[3]` converts to `const char (*)[3]` as `char **` does to
// `const char *const *` (review A19); the step through an array keeps the extent.
static const Type *cvOwner(const Type *t) {
    while (t->isArray()) t = t->pointee();
    return t;
}

bool qualificationConvertible(const Type *from, const Type *to) {
    bool prefixConst = true;
    for (;;) {
        if (from->unqualified() == to->unqualified()) return true;
        if (from->isArray() && to->isArray()) {
            if (from->length() != to->length()) return false;
            from = from->pointee();
            to = to->pointee();
            continue;
        }
        if (!from->isPointer() || !to->isPointer()) return false;
        from = from->pointee();
        to = to->pointee();
        const bool fromConst = cvOwner(from)->isConst(), toConst = cvOwner(to)->isConst();
        if (fromConst && !toConst) return false;
        if (!fromConst && toConst && !prefixConst) return false;
        prefixConst = prefixConst && toConst;
    }
}

// ------------------------------------------------------------------ overloading
// [over.match] reduced to what rung 2 needs: a conversion sequence is ranked, the
// best viable function is the one no other beats, the unrankable is not viable.

const Type *Parser::decayedType(const Type *t) {
    if (t->isArray()) return types_.pointerTo(t->pointee());
    if (t->isFunction()) return types_.pointerTo(t);
    return t;
}

// The function an expression designates as an lvalue: one of function type (a reference to a
// function, read), or a function's name, which arrives as `&f`. Null for anything else.
const Type *Parser::functionDesignated(const Expr &e) const {
    if (e.type()->isFunction()) return e.type();
    if (const Unary *u = dynamic_cast<const Unary *>(&e))
        if (u->op() == '&' && u->operand().type()->isFunction()) return u->operand().type();
    return nullptr;
}

// **Each default is read again, here, at the call that left it out** -
// [dcl.fct.default]/9 evaluates it afresh every time. The caller's locals are put
// aside, the declaration's scope being what it is read in and not the call's.
void Parser::applyDefaults(Signature f, std::vector<ExprPtr> &args,
                           std::size_t pos) {
    if (args.size() >= f.params.size()) return;
    std::map<std::string, std::vector<std::size_t> >::const_iterator it =
        defaultArgs_.find(f.symbol);
    if (it == defaultArgs_.end()) return;

    const std::size_t resume = at_;
    const KeepCallArgs keepArgs(this);
    std::vector<Local> hidden;
    hidden.swap(locals_);
    std::vector<std::size_t> starts;
    starts.swap(scopeStarts_);
    // **The enclosing function is hidden for the same reason the locals are.**
    // [dcl.fct.default]/5 reads a default in the scope of the declaration, so what
    // the expression declares belongs there - a lambda's closure type included.
    const std::string outerFunction = currentFunction_;
    const std::string outerFunctionName = currentFunctionName_;
    currentFunction_.clear();
    currentFunctionName_.clear();
    // **And the rest of the declaration's scope**, which the locals were only
    // part of.
    const std::vector<std::string> outerNs = namespaceStack_;
    std::map<std::string, std::vector<std::string> >::const_iterator ns =
        defaultArgNamespace_.find(f.symbol);
    if (ns != defaultArgNamespace_.end()) namespaceStack_ = ns->second;
    const Type *outerClass = currentClass_;
    if (!f.owner.empty()) {
        if (const Type *owner = lookup_.findTypedef(f.owner)) currentClass_ = owner;
    }
    std::vector<syntax::NodeId> made;
    for (std::size_t i = args.size(); i < f.params.size(); i++) {
        if (i >= it->second.size() || it->second[i] == 0)
            src_.fail(pos, "'" + f.name + "' has no default for parameter " +
                           std::to_string(i + 1));
        at_ = it->second[i];
        // [dcl.fct.default]/1 takes a brace-or-equal-initializer: `= {}` and `= {1, 2}` copy-list-initialise
        // the parameter - a reference's referent through a temporary (M1 L3).
        if (peek().is("{")) {
            const Type *p = f.params[i]->isReference() ? f.params[i]->referent() : f.params[i];
            Init in = parseInitialiser();
            args.push_back(listTemporaryFrom(p->unqualified(), in, true, pos));
            made.push_back(args.back()->syntax());
            continue;
        }
        args.push_back(assign());
        made.push_back(args.back()->syntax());
    }
    // The defaults' syntax, for the call they complete to take as its last parts (M3b.2); a part with none, none of them.
    bool whole = !made.empty();
    for (std::size_t i = 0; i < made.size(); i++) whole = whole && made[i].valid();
    if (whole) defaultGroups_[made.back().v] = made;
    at_ = resume;
    locals_.swap(hidden);
    scopeStarts_.swap(starts);
    currentFunction_ = outerFunction;
    currentFunctionName_ = outerFunctionName;
    namespaceStack_ = outerNs;
    currentClass_ = outerClass;
}

// A call's syntax parts with the defaults it was completed by after them, where its arguments end in exactly those and the
// rest are the parts written; the parts as they were otherwise.
std::vector<syntax::NodeId> Parser::withDefaults(const Expr &e, const std::vector<syntax::NodeId> &children) {
    const Call *c = dynamic_cast<const Call *>(&e);
    if (c == nullptr || c->args().empty() || children.empty()) return children;
    const std::unordered_map<std::uint32_t, std::vector<syntax::NodeId> >::iterator g =
        defaultGroups_.find(c->args().back()->syntax().v);
    if (g == defaultGroups_.end()) return children;
    const std::vector<syntax::NodeId> group = g->second;
    defaultGroups_.erase(g);
    const std::size_t given = c->args().size() - (c->hasThis() ? 1 : 0);
    if (given != children.size() - 1 + group.size()) return children;
    for (std::size_t i = 0; i < group.size(); i++)
        if (c->args()[c->args().size() - group.size() + i]->syntax().v != group[i].v) return children;
    std::vector<syntax::NodeId> whole = children;
    whole.insert(whole.end(), group.begin(), group.end());
    for (std::size_t i = 0; i < group.size(); i++) defaultArgNodes_.insert(group[i].v);
    return whole;
}

void Parser::checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                             const std::string &what) const {
    const Type *ft = from.type();

    if (ft == to) return;

    // Copying ignores the const at the top.
    if (ft->unqualified() == to->unqualified()) return;

    if (ft->unqualified()->isScopedEnumeration()) refuseScopedConversion(ft, to, pos);
    if (ft->isArithmetic() && to->unqualified()->isEnumeration() && !rules().integerConvertsToEnumeration()) {
        // describe() spells an enumeration as its integer; this message is about the difference.
        auto shown = [](const Type *t) {
            if (t->unqualified()->isScopedEnumeration()) return "enum class " + t->unqualified()->enumTag();
            return t->unqualified()->isEnumeration() ? "enum " + t->unqualified()->enumTag()
                                                     : t->describe();
        };
        src_.fail(pos, what + " is '" + shown(to) + "' and this is '" + shown(ft) +
                       "' - C++ converts an integer to an enumeration only with a cast, "
                       "where C does it implicitly ([dcl.enum]/10)");
    }
    if (ft->isArithmetic() && to->isArithmetic()) return;
    if (memberPointerToDerived(ft, to) >= 0) return;

    // **A class converts by its own conversion function**, [class.conv.fct] - the mirror of the
    // converting constructor, and the same one rule: at most one user-defined conversion in a
    // sequence, so what the function answers is finished by a standard conversion and no further.
    if (ft->unqualified()->isStructOrUnion() &&
        ft->unqualified() != to->unqualified() &&
        !publiclyDerivedFrom(ft->unqualified(), to->unqualified()) &&
        const_cast<Parser *>(this)->conversionFunction(ft, to) != nullptr)
        return;
    // [class.conv.fct]/2: an `explicit` one is for direct-initialisation, a cast and a condition,
    // never for this copy - `bool b = f` (1.28, 2026-10-22, review of 1.25, neg07).
    if (ft->unqualified()->isStructOrUnion() && !to->unqualified()->isStructOrUnion() &&
        const_cast<Parser *>(this)->conversionFunction(ft, to, true) != nullptr)
        src_.fail(pos, what + " is '" + to->describe() + "' and this is '" + ft->describe() +
                       "', whose conversion function to it is 'explicit' - copy-initialisation "
                       "may not take it ([class.conv.fct]/2); write '" + to->describe() +
                       " x(v)', a static_cast, or use it as a condition");

    // Reached only by a lambda that captures: a captureless one has its conversion function (F22),
    // and [expr.prim.lambda]/6 gives one to no other.
    if (to->isPointer() && to->pointee()->isFunction() &&
        ft->unqualified()->isStructOrUnion() &&
        ft->unqualified()->tag().find("$_") != std::string::npos)
        src_.fail(pos, "a lambda converts to a function pointer only if it "
                       "captures nothing - call the closure directly, or give "
                       "the function a template parameter for it");

    // Slicing, [class.copy]/12: a derived object copies to an accessible base that has no copy
    // constructor of its own as its base subobject's bytes; convert() takes the subobject.
    if (ft->unqualified()->isStructOrUnion() && to->unqualified()->isStructOrUnion() &&
        accessibleBaseOffset(ft->unqualified(), to->unqualified()) >= 0 &&
        const_cast<Parser *>(this)->classes_.copyConstructorOf(to->unqualified()) == nullptr &&
        const_cast<Parser *>(this)->classes_.moveConstructorOf(to->unqualified()) == nullptr)
        return;
    if (ft->unqualified()->isStructOrUnion() && to->unqualified()->isStructOrUnion() &&
        ft->unqualified() != to->unqualified() &&
        anyBaseOffset(ft->unqualified(), to->unqualified()) >= 0)
        src_.fail(pos, what + " is '" + to->describe() + "', a base of '" + ft->describe() +
                       "' that is not accessible here ([class.access.base]/4)");

    auto refuse = [&](const char *tail) {
        src_.fail(pos, what + " is '" + to->describe() + "' and this is '" +
                       ft->describe() + "'" + tail);
    };

    if (to->isPointer() && ft->isPointer()) {
        if (qualificationConvertible(ft, to)) return;
        // Derived * converts to Base *, the base being at offset 0, so the
        // value is unchanged and only the type moves.
        if (ft->pointee()->unqualified() != to->pointee()->unqualified() &&
            accessibleBaseOffset(ft->pointee(), to->pointee()) >= 0 &&
            (to->pointee()->isConst() || !ft->pointee()->isConst()))
            return;
        // [conv.ptr]/2: an object pointer converts to void * implicitly, keeping its const;
        // void * converts back only by a cast, which is where C++ parts from C.
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            ft->pointee()->isConst())
            refuse(" - 'void *' would drop the const; 'const void *' keeps it");
        if (to->pointee()->isVoid()) return;
        if (ft->pointee()->isVoid() && rules().voidPointerConvertsToObjectPointer() &&
            (to->pointee()->isConst() || !ft->pointee()->isConst()))
            return;
        if (ft->pointee()->isVoid())
            refuse(" - 'void *' converts to another pointer only with a cast in C++, where C allows it");
        // The commonest way to meet this rule is a C program handing a
        // string literal to a 'char *', so it is worth saying which rule
        // stopped it rather than leaving the reader to work back from const.
        if (isStringLiteral(from))
            refuse(" - a string literal is an array of const char in C++11, "
                   "and does not convert to a writable pointer");
        if (ft->pointee()->unqualified() == to->pointee()->unqualified())
            refuse(" - the const would be dropped, and then the thing it "
                   "protects could be written through");
        if (anyBaseOffset(ft->pointee(), to->pointee()) > -1)
            refuse(" - the base is not accessible here, [class.access.base]/4: a private one only "
                   "inside the class and its friends");
        refuse(" - a cast says you meant it");
    }
    if (to->isPointer() && ft->isInteger()) {
        if (isNullConstant(from)) return;
        refuse(" - only the constant 0 becomes a pointer on its own");
    }
    // `nullptr` converts to any pointer and any pointer to member, and nothing is emitted: the value is already
    // a pointer-sized zero, the type only carrying the front end's knowledge that it is not the number 0. A member
    // function pointer is a pointer to a member too, [conv.mem]/1, and the constant 0 reaches both (1.27, item 3).
    if (ft->isNullPtr() &&
        (to->isPointer() || to->isMemberPointer() || to->isMemberFunctionPointer())) return;
    if ((to->isMemberPointer() || to->isMemberFunctionPointer()) && ft->isInteger() &&
        isNullConstant(from)) return;
    if (to->isNullPtr()) {
        if (isNullConstant(from)) return;
        refuse(" - the only values of that type are 'nullptr' and the "
               "constant 0");
    }
    if (to->isArithmetic() && ft->isPointer())
        refuse(" - a pointer is not a number here, though a cast makes it one");

    refuse("");
}

