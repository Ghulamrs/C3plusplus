// sema::Constexpr - see Constexpr.h.

#include "Constexpr.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>

namespace sema {

// The frame an object fold outside any call stands in, so a temporary it builds has a slot to live in.
static const int kScratchFrame = INT_MIN;

static long double inType(const Type *t, const Target &target, long double v) {
    if (t->kind() == Kind::Float) return static_cast<float>(v);
    if (t->kind() == Kind::Double ||
        (t->kind() == Kind::LongDouble && !t->isX87(target)))
        return static_cast<double>(v);
    return v;
}

const ConstexprSlot *Constexpr::constexprSlot(const Var &v) const {
    if (p_.constexprFrames_.empty()) return nullptr;
    const std::vector<ConstexprSlot> &frame = p_.constexprFrames_.back();
    for (std::size_t i = 0; i < frame.size(); i++) {
        if (frame[i].offset == kScratchFrame) continue;
        if (v.isLocal() ? frame[i].global.empty() && frame[i].offset == v.offset()
                        : frame[i].global == v.symbol() || frame[i].global == v.name()) return &frame[i];
    }
    return nullptr;
}

bool Constexpr::enterConstexprCall(const Call &c, const ConstexprFn **out,
                                std::size_t pos) const {
    auto it = p_.constexprFns_.find(c.symbol());
    if (it == p_.constexprFns_.end() && instantiateForConstant(c.symbol())) it = p_.constexprFns_.find(c.symbol());
    if (it == p_.constexprFns_.end()) return false;
    const ConstexprFn &fn = it->second;
    if (fn.value == nullptr || c.args().size() != fn.slots.size())
        return false;

    // A recursion that does not end is a compiler that does not either.
    // The standard lets an implementation set a limit and say so; this is
    // that limit, and it is said where it is reached.
    if (p_.constexprFrames_.size() >= 256)
        p_.src_.fail(pos, "this constant expression is more than 256 calls "
                       "deep - a 'constexpr' function that never stops "
                           "recursing cannot be worked out while compiling");

    std::vector<ConstexprSlot> frame;
    for (std::size_t i = 0; i < c.args().size(); i++) {
        ConstexprSlot slot;
        slot.offset = fn.slots[i];
        const Expr &a = *c.args()[i];
        // **Folded outside the new frame, in the caller's.** An argument is an
        // expression where the call is written, so `fact(n - 1)` reads the
        // caller's n; folding it after the push would read the callee's slot.
        if (!foldArgument(a, &slot, pos)) return false;
        frame.push_back(slot);
    }
    p_.constexprFrames_.push_back(frame);
    *out = &fn;
    return true;
}

// **An address built on a constant pointer is a constant**, `(size_t)&((S *)0)->m` - offsetof,
// [support.types]/4: a cast of an integer to a pointer, `*` of it, members of that, and `&`.
bool Constexpr::foldPointerConstant(const Expr &e, long long *out, std::size_t pos) const {
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        if (c->value().type() != nullptr && c->value().type()->isPointer())
            return foldPointerConstant(c->value(), out, pos);
        return c->value().type() != nullptr && c->value().type()->isInteger() && fold(c->value(), out, pos);
    }
    const Unary *u = dynamic_cast<const Unary *>(&e);
    if (u == nullptr || u->op() != '&') return false;
    const Expr *at = &u->operand();
    long long offset = 0;
    while (const MemberAccess *m = dynamic_cast<const MemberAccess *>(at)) {
        if (m->isBitField()) return false;
        offset += m->offset();
        at = &m->object();
    }
    const Unary *deref = dynamic_cast<const Unary *>(at);
    if (deref == nullptr || deref->op() != '*' || !foldPointerConstant(deref->operand(), out, pos))
        return false;
    *out += offset;
    return true;
}

bool Constexpr::fold(const Expr &e, long long *out, std::size_t pos) const {
    // **A name, when it names a constant.**
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        // **Inside a constexpr call, a local name is a parameter.** The body being
        // folded belongs to another function, so its Vars name slots in a frame that
        // does not exist; what they are worth is on the top of this stack.
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->floating || slot->obj != nullptr) return false;   // Cast/Binary fold it as floating
            *out = slot->i;
            return true;
        }
        if (v->isLocal()) {
            if (const Local *l = p_.lookup_.findLocal(v->name()))
                if (l->isConstantValue) { *out = l->constantValue; return true; }
            return false;
        }
        if (const GlobalSym *g = p_.lookup_.findGlobal(v->name()))
            if (g->isConstantValue) { *out = g->constantValue; return true; }
        // A static member is not in globals_ - it is kept apart from the
        // member list so nothing walking a layout has to skip it - so the
        // read-back asks the table keyed by its symbol.
        if (const Parser::StaticConst *sc = p_.findStaticConst(v->symbol()))
            if (sc->known) { *out = sc->value; return true; }
        return false;
    }
    // **A call to a constexpr function.** C++11 lets its body be one return statement,
    // so running it is folding that expression with the parameters standing for the
    // arguments. A call to anything else simply does not fold, which is the answer.
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        const ConstexprFn *fn = nullptr;
        if (!enterConstexprCall(*c, &fn, pos)) return false;
        long long result = 0;
        const bool ok = fold(*fn->value, &result, pos);
        p_.constexprFrames_.pop_back();
        if (!ok) return false;
        *out = result;
        return true;
    }
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) return false;
        *out = n->value();
        return true;
    }
    // [expr.comma]: the right operand is the value; the left is a constant too or an effect a
    // constexpr body is allowed, so `int a[(1, 3)]` has 3 elements (1.27, 2026-10-21, review of
    // 1.25, item 3 - it was "not an integer constant expression").
    if (const Comma *c = dynamic_cast<const Comma *>(&e)) {
        long long dropped = 0;
        if (!fold(c->left(), &dropped, pos) && !runConstexprEffect(c->left(), pos)) return false;
        return fold(c->right(), out, pos);
    }
    // **A member of a constant object**, or what a pointer into one reaches.
    if (dynamic_cast<const MemberAccess *>(&e) != nullptr ||
        (dynamic_cast<const Unary *>(&e) != nullptr && static_cast<const Unary &>(e).op() == '*')) {
        ConstLeaf leaf;
        if (!foldLeaf(e, &leaf, pos) || leaf.type->isFloating()) return false;
        *out = leaf.i;
        return true;
    }

    // **[expr.const] has a floating lane, and this fold answers in integers**,
    // so a floating operand goes to foldDouble where an integer comes out of
    // it - a cast, a comparison, `!` - and a floating result stays its own.
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        long long v;
        if (!e.type()->isInteger()) return false;
        if (c->value().type() != nullptr && c->value().type()->isFloating()) {
            long double d = 0;
            if (!foldFloating(c->value(), &d)) return false;
            v = e.type()->isSigned(p_.target_)
              ? static_cast<long long>(d)
              : static_cast<long long>(static_cast<unsigned long long>(d));
        } else if (c->value().type() != nullptr && c->value().type()->isPointer()) {
            if (!foldPointerConstant(c->value(), &v, pos)) return false;
        } else if (!fold(c->value(), &v, pos)) {
            return false;
        }
        *out = narrowTo(v, e.type());
        return true;
    }

    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        long long v;
        if (u->op() == '!' && u->operand().type() != nullptr &&
            u->operand().type()->isFloating()) {
            long double d = 0;
            if (!foldFloating(u->operand(), &d)) return false;
            *out = d == 0;
            return true;
        }
        if (!fold(u->operand(), &v, pos)) return false;
        switch (u->op()) {
        case '-': *out = static_cast<long long>(0ULL - static_cast<unsigned long long>(v)); return true;
        case '+': *out = v; return true;
        case '!': *out = !v; return true;
        case '~': *out = ~v; return true;
        default: return false;
        }
    }

    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        long long t;
        if (!fold(c->cond(), &t, pos)) return false;
        return fold(t ? c->thenArm() : c->elseArm(), out, pos);
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long long l, r;
        const Type *lt = b->lhs().type();
        const Type *rt = b->rhs().type();
        if ((lt != nullptr && lt->isFloating()) ||
            (rt != nullptr && rt->isFloating())) {
            long double dl = 0, dr = 0;
            if (!foldFloating(b->lhs(), &dl) || !foldFloating(b->rhs(), &dr))
                return false;
            switch (b->op()) {
            case BinOp::Eq: *out = (dl == dr); return true;
            case BinOp::Ne: *out = (dl != dr); return true;
            case BinOp::Lt: *out = (dl <  dr); return true;
            case BinOp::Le: *out = (dl <= dr); return true;
            case BinOp::Gt: *out = (dl >  dr); return true;
            case BinOp::Ge: *out = (dl >= dr); return true;
            case BinOp::LAnd: *out = (dl != 0 && dr != 0); return true;
            case BinOp::LOr:  *out = (dl != 0 || dr != 0); return true;
            default: return false;
            }
        }
        if (!fold(b->lhs(), &l, pos) || !fold(b->rhs(), &r, pos)) return false;

        const Type *t = b->lhs().type();
        bool uns = t->isInteger() && !t->isSigned(p_.target_);
        unsigned long long ul = static_cast<unsigned long long>(l);
        unsigned long long ur = static_cast<unsigned long long>(r);

        switch (b->op()) {
        case BinOp::Add: *out = static_cast<long long>(ul + ur); return true;
        case BinOp::Sub: *out = static_cast<long long>(ul - ur); return true;
        case BinOp::Mul: *out = static_cast<long long>(ul * ur); return true;
        case BinOp::Div:
        case BinOp::Mod:
            if (r == 0)
                p_.src_.fail(pos, "division by zero in a constant expression");
            if (!uns && ul == (1ULL << 63) && r == -1) {
                *out = (b->op() == BinOp::Div) ? l : 0;
                return true;
            }
            if (b->op() == BinOp::Div)
                *out = uns ? static_cast<long long>(ul / ur) : l / r;
            else
                *out = uns ? static_cast<long long>(ul % ur) : l % r;
            return true;
        case BinOp::Shl:
        case BinOp::Shr:
            if (r < 0 || r >= 64)
                p_.src_.fail(pos, "shift count out of range in a constant expression");
            if (b->op() == BinOp::Shl) *out = static_cast<long long>(ul << r);
            else *out = uns ? static_cast<long long>(ul >> r) : (l >> r);
            return true;
        case BinOp::BitAnd: *out = l & r; return true;
        case BinOp::BitOr:  *out = l | r; return true;
        case BinOp::BitXor: *out = l ^ r; return true;
        case BinOp::Eq: *out = (l == r); return true;
        case BinOp::Ne: *out = (l != r); return true;
        case BinOp::Lt: *out = uns ? (ul <  ur) : (l <  r); return true;
        case BinOp::Le: *out = uns ? (ul <= ur) : (l <= r); return true;
        case BinOp::Gt: *out = uns ? (ul >  ur) : (l >  r); return true;
        case BinOp::Ge: *out = uns ? (ul >= ur) : (l >= r); return true;
        case BinOp::LAnd: *out = (l && r); return true;
        case BinOp::LOr:  *out = (l || r); return true;
        }
        return false;
    }

    return false;
}

// **[expr.const]/3: a named integral constant is a constant expression**, which is the
// rule that lets C++ write `const int n = 4; int a[n];`. Only integral, because that
// is what fold() answers in. Below it: the one expression a constexpr body may be.
const Expr *Constexpr::singleReturnValue(const Stmt &body) const {
    const Stmt *at = &body;
    for (;;) {
        const Block *b = dynamic_cast<const Block *>(at);
        if (b == nullptr) break;
        if (b->body().size() != 1) return nullptr;
        at = b->body()[0].get();
    }
    const Return *r = dynamic_cast<const Return *>(at);
    if (r == nullptr || !r->hasValue()) return nullptr;
    return &r->value();
}

bool Constexpr::constantInitialiser(const Type *t, const Init &in,
                                 long long *out) const {
    if (t == nullptr || !t->isConst() || !t->isInteger()) return false;
    if (in.isList || in.value == nullptr) return false;
    return fold(*in.value, out, in.pos);
}

// The floating twin: a const floating object with a foldable initialiser is
// read back by foldDouble, an integer one being widened on the way in.
bool Constexpr::constantFloatingInitialiser(const Type *t, const Init &in,
                                         long double *out) const {
    if (t == nullptr || !t->isConst() || !t->isFloating()) return false;
    if (in.isList || in.value == nullptr) return false;
    return foldFloating(*in.value, out);
}

// foldDouble with its two host-precision flags folded away: an answer is an
// answer here, as it is for the integer lane.
bool Constexpr::foldFloating(const Expr &e, long double *out) const {
    bool past53 = false, x87 = false;
    return foldDouble(e, p_.target_, out, &past53, &x87);
}

long long Constexpr::narrowTo(long long v, const Type *t) const {
    int bits = t->size(p_.target_) * 8;
    if (bits >= 64) return v;
    unsigned long long mask = (1ULL << bits) - 1;
    unsigned long long kept = static_cast<unsigned long long>(v) & mask;
    if (t->isSigned(p_.target_) && (kept & (1ULL << (bits - 1)))) kept |= ~mask;
    return static_cast<long long>(kept);
}

// The leaves of `from` in [at, at + size), moved to start at 0 - a member or a base taken whole.
ConstObject Constexpr::subObject(const ConstObject &from, int at, int size) {
    ConstObject out;
    for (ConstObject::const_iterator it = from.lower_bound(at);
         it != from.end() && it->first < at + size; ++it)
        out[it->first - at] = it->second;
    return out;
}

void Constexpr::storeSubObject(ConstObject &into, int at, int size, const ConstObject &what) {
    into.erase(into.lower_bound(at), into.lower_bound(at + size));
    for (const auto &leaf : what) into[at + leaf.first] = leaf.second;
}

std::string Constexpr::constObjectKey(const Var &v) const {
    return v.isLocal() ? p_.currentFunction_ + "@" + std::to_string(v.offset()) : v.symbol();
}

// What a call's argument is worth: a number, an object of class type, or a pointer into one.
bool Constexpr::foldArgument(const Expr &a, ConstexprSlot *slot, std::size_t pos) const {
    const Type *t = a.type();
    if (t != nullptr && t->unqualified()->isStructOrUnion()) {
        slot->obj = std::make_shared<ConstObject>();
        return foldObject(a, slot->obj.get(), pos);
    }
    if (t != nullptr && t->isPointer() && t->pointee()->unqualified()->isStructOrUnion()) {
        int at = 0;
        if (!foldObjectPointer(a, &slot->obj, &at, pos)) return false;
        slot->pointer = true;
        slot->i = at;
        return true;
    }
    slot->floating = t != nullptr && t->isFloating();
    return slot->floating ? foldFloating(a, &slot->d) : fold(a, &slot->i, pos);
}

// **Where an lvalue of a constant object lies**: a named one, a slot of the frame, a member of one,
// or what a pointer into one reaches.
bool Constexpr::objectAddress(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->obj == nullptr || slot->pointer) return false;
            *obj = slot->obj;
            *at = 0;
            return true;
        }
        // A named object outside any call: inside one, every name is a parameter or a temporary.
        if (!p_.constexprFrames_.empty() && (p_.constexprFrames_.back().empty() ||
                                          p_.constexprFrames_.back()[0].offset != kScratchFrame))
            return false;
        std::map<std::string, ConstObject>::const_iterator it = p_.constObjects_.find(constObjectKey(*v));
        if (it == p_.constObjects_.end() && !v->isLocal()) it = p_.constObjects_.find(v->name());
        if (it == p_.constObjects_.end()) return false;
        *obj = std::make_shared<ConstObject>(it->second);
        *at = 0;
        return true;
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        if (m->isBitField() || !objectAddress(m->object(), obj, at, pos)) return false;
        *at += m->offset();
        return true;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e))
        if (u->op() == '*') return foldObjectPointer(u->operand(), obj, at, pos);
    return false;
}

// **A pointer into a constant object**, as the lowering computes one: `&x`, `this`, a cast, the
// arithmetic a base's address is reached by, and the comma that builds a temporary first.
bool Constexpr::foldObjectPointer(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        const ConstexprSlot *slot = constexprSlot(*v);
        if (slot == nullptr || !slot->pointer) return false;
        *obj = slot->obj;
        *at = static_cast<int>(slot->i);
        return true;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() != '&') return false;
        if (objectAddress(u->operand(), obj, at, pos)) return true;
        // The address of a value - `make().get()` calls get on the object make returned.
        ConstObject value;
        if (!foldObject(u->operand(), &value, pos)) return false;
        *obj = std::make_shared<ConstObject>(std::move(value));
        *at = 0;
        return true;
    }
    if (const Cast *c = dynamic_cast<const Cast *>(&e))
        return c->value().type() != nullptr && c->value().type()->isPointer() &&
               foldObjectPointer(c->value(), obj, at, pos);
    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long long n = 0;
        const Type *pt = b->lhs().type();
        if ((b->op() != BinOp::Add && b->op() != BinOp::Sub) || pt == nullptr || !pt->isPointer() ||
            !foldObjectPointer(b->lhs(), obj, at, pos) || !fold(b->rhs(), &n, pos))
            return false;
        const long long step = pt->pointee()->isVoid() ? 1 : pt->pointee()->size(p_.target_);
        *at += static_cast<int>((b->op() == BinOp::Add ? n : -n) * step);
        return true;
    }
    // `p ? p + k : 0` - a base's address, null-checked; an object here is never null.
    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        ConstObjectPtr tested;
        int where = 0;
        long long t = 0;
        if (c->cond().type() != nullptr && c->cond().type()->isPointer()) {
            if (!foldObjectPointer(c->cond(), &tested, &where, pos)) return false;
            t = 1;
        } else if (!fold(c->cond(), &t, pos)) {
            return false;
        }
        return foldObjectPointer(t ? c->thenArm() : c->elseArm(), obj, at, pos);
    }
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return runConstexprEffect(c->left(), pos) && foldObjectPointer(c->right(), obj, at, pos);
    return false;
}

// **A class template's constexpr member is compiled where a constant needs it**, not after the
// file as instantiatePending would: that one body is replayed now, its parameters bound.
bool Constexpr::instantiateForConstant(const std::string &symbol) const {
    Parser &self = p_;
    std::string complete = symbol;
    const std::size_t c2 = complete.find("C2E");
    if (c2 != std::string::npos) complete.replace(c2, 3, "C1E");
    for (std::size_t i = 0; i < p_.specializations_.size(); i++) {
        if (!p_.specializations_[i].isClass) continue;
        std::vector<Parser::PendingBody> &bodies = self.specializations_[i].bodies;
        for (std::size_t b = 0; b < bodies.size(); b++) {
            if (bodies[b].which >= p_.functions_.size() || p_.functions_[bodies[b].which].symbol != complete) continue;
            const Parser::PendingBody body = bodies[b];
            bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(b));
            self.functions_[body.which].used = true;
            const Parser::Specialization sp = p_.specializations_[i];
            std::vector<Shadow> undo;
            self.tmpl_.bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            std::vector<std::vector<ConstexprSlot> > frames;
            frames.swap(p_.constexprFrames_);
            const std::string wasKey = p_.instantiationKey_, wasOf = p_.instantiationOf_;
            self.instantiationKey_.clear();
            self.instantiationOf_.clear();
            const bool wasInline = p_.replayingInline_;
            const std::size_t resume = p_.at_;
            self.replayInlineBodies(std::vector<Parser::PendingBody>(1, body));
            self.at_ = resume;
            self.replayingInline_ = wasInline;
            self.instantiationKey_ = wasKey;
            self.instantiationOf_ = wasOf;
            frames.swap(p_.constexprFrames_);
            self.tmpl_.unbindTemplateParameters(undo);
            return true;
        }
    }
    return false;
}

// **The value of an expression of class type.**
bool Constexpr::foldObject(const Expr &e, ConstObject *out, std::size_t pos) const {
    const Type *t = e.type();
    if (t == nullptr || !t->unqualified()->isStructOrUnion()) return false;
    const bool scratch = p_.constexprFrames_.empty();
    if (scratch) p_.constexprFrames_.push_back(std::vector<ConstexprSlot>(1, ConstexprSlot()));
    if (scratch) p_.constexprFrames_.back()[0].offset = kScratchFrame;
    bool ok = false;
    ConstObjectPtr obj;
    int at = 0;
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        const ConstexprFn *fn = nullptr;
        if (enterConstexprCall(*c, &fn, pos)) {
            ok = foldObject(*fn->value, out, pos);
            p_.constexprFrames_.pop_back();
        }
    } else if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        ok = c->value().type() != nullptr && c->value().type()->unqualified() == t->unqualified() &&
             foldObject(c->value(), out, pos);
    } else if (objectAddress(e, &obj, &at, pos)) {
        *out = subObject(*obj, at, t->size(p_.target_));
        ok = true;
    } else if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        // A member of an object that is itself a value - `make().inner`.
        ConstObject whole;
        ok = !m->isBitField() && foldObject(m->object(), &whole, pos);
        if (ok) *out = subObject(whole, m->offset(), t->size(p_.target_));
    }
    if (scratch) p_.constexprFrames_.pop_back();
    return ok;
}

// **One scalar of a constant object**, the type the read asks for and the leaf agreeing on it.
bool Constexpr::foldLeaf(const Expr &e, ConstLeaf *out, std::size_t pos) const {
    const Type *t = e.type();
    if (t == nullptr || !t->isArithmetic()) return false;
    const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e);
    if (m != nullptr && m->isBitField()) return false;
    const bool scratch = p_.constexprFrames_.empty();
    if (scratch) p_.constexprFrames_.push_back(std::vector<ConstexprSlot>(1, ConstexprSlot()));
    if (scratch) p_.constexprFrames_.back()[0].offset = kScratchFrame;
    ConstObjectPtr obj;
    int at = 0;
    bool ok = objectAddress(e, &obj, &at, pos);
    if (!ok && m != nullptr) {
        ConstObject whole;
        ok = foldObject(m->object(), &whole, pos);
        if (ok) { obj = std::make_shared<ConstObject>(std::move(whole)); at = m->offset(); }
    }
    if (scratch) p_.constexprFrames_.pop_back();
    if (!ok) return false;
    ConstObject::const_iterator it = obj->find(at);
    if (it == obj->end() || it->second.type->isFloating() != t->isFloating() ||
        it->second.type->size(p_.target_) != t->size(p_.target_))
        return false;
    *out = it->second;
    return true;
}

// **A constexpr constructor run on the object its first argument points at.**
bool Constexpr::constructConst(const Call &c, std::size_t pos) const {
    std::map<std::string, ConstexprCtor>::const_iterator it = p_.constexprCtors_.find(c.symbol());
    if (it == p_.constexprCtors_.end() && instantiateForConstant(c.symbol())) it = p_.constexprCtors_.find(c.symbol());
    if (it == p_.constexprCtors_.end() || c.args().empty()) return false;
    const ConstexprCtor &ctor = it->second;
    if (ctor.slots.size() + 1 != c.args().size()) return false;
    if (p_.constexprFrames_.size() >= 256)
        p_.src_.fail(pos, "this constant expression is more than 256 calls deep - a 'constexpr' "
                       "function that never stops recursing cannot be worked out while compiling");
    // A temporary the comma around this call reads back is made here, in the caller's frame.
    ConstexprSlot self;
    self.offset = ctor.thisSlot;
    self.pointer = true;
    int at = 0;
    if (!foldObjectPointer(*c.args()[0], &self.obj, &at, pos)) {
        const Unary *u = dynamic_cast<const Unary *>(c.args()[0].get());
        const Var *v = u != nullptr && u->op() == '&' ? dynamic_cast<const Var *>(&u->operand()) : nullptr;
        if (v == nullptr || !v->isLocal() || p_.constexprFrames_.empty()) return false;
        ConstexprSlot made;
        made.offset = v->offset();
        made.obj = std::make_shared<ConstObject>();
        p_.constexprFrames_.back().push_back(made);
        self.obj = made.obj;
        at = 0;
    }
    self.i = at;
    std::vector<ConstexprSlot> frame(1, self);
    for (std::size_t i = 1; i < c.args().size(); i++) {
        ConstexprSlot slot;
        slot.offset = ctor.slots[i - 1];
        if (!foldArgument(*c.args()[i], &slot, pos)) return false;
        frame.push_back(slot);
    }
    p_.constexprFrames_.push_back(frame);
    const bool ok = runConstexpr(*ctor.body, pos);
    p_.constexprFrames_.pop_back();
    return ok;
}

// The statements a constructor's body and a declaration's initialisation are lowered to.
bool Constexpr::runConstexpr(const Stmt &s, std::size_t pos) const {
    if (const Block *b = dynamic_cast<const Block *>(&s)) {
        for (const StmtPtr &x : b->body())
            if (!runConstexpr(*x, pos)) return false;
        return true;
    }
    if (const ExprStmt *x = dynamic_cast<const ExprStmt *>(&s)) return runConstexprEffect(x->expr(), pos);
    return false;
}

bool Constexpr::runConstexprEffect(const Expr &e, std::size_t pos) const {
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return runConstexprEffect(c->left(), pos) && runConstexprEffect(c->right(), pos);
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        // A function returning an object into a slot of the frame - the slot holds its value from here.
        if (c->resultSlot() != 0 && c->type() != nullptr && c->type()->unqualified()->isStructOrUnion() &&
            !p_.constexprFrames_.empty()) {
            ConstexprSlot made;
            made.offset = c->resultSlot();
            made.obj = std::make_shared<ConstObject>();
            if (!foldObject(*c, made.obj.get(), pos)) return false;
            p_.constexprFrames_.back().push_back(made);
            return true;
        }
        return constructConst(*c, pos);
    }
    const Assign *a = dynamic_cast<const Assign *>(&e);
    if (a == nullptr || a->type() == nullptr) return false;
    const Type *t = a->target().type()->unqualified();
    // A pointer the lowering keeps in a temporary of its own - a base's address on its way.
    if (t->isPointer()) {
        const Var *v = dynamic_cast<const Var *>(&a->target());
        if (v == nullptr || !v->isLocal() || p_.constexprFrames_.empty()) return false;
        ConstexprSlot slot;
        slot.offset = v->offset();
        int at = 0;
        if (!foldObjectPointer(a->value(), &slot.obj, &at, pos)) return false;
        slot.pointer = true;
        slot.i = at;
        std::vector<ConstexprSlot> &frame = p_.constexprFrames_.back();
        for (std::size_t i = 0; i < frame.size(); i++)
            if (frame[i].offset == slot.offset && frame[i].global.empty()) { frame[i] = slot; return true; }
        frame.push_back(slot);
        return true;
    }
    ConstObjectPtr obj;
    int at = 0;
    if (!objectAddress(a->target(), &obj, &at, pos)) return false;
    if (t->isStructOrUnion()) {
        ConstObject value;
        if (!foldObject(a->value(), &value, pos)) return false;
        storeSubObject(*obj, at, t->size(p_.target_), value);
        return true;
    }
    if (!t->isArithmetic()) return false;
    ConstLeaf leaf;
    leaf.type = t;
    if (t->isFloating()) {
        if (!foldFloating(a->value(), &leaf.d)) return false;
    } else {
        if (!fold(a->value(), &leaf.i, pos)) return false;
        leaf.i = t->isBool() ? leaf.i != 0 : narrowTo(leaf.i, t);
    }
    storeSubObject(*obj, at, t->size(p_.target_), ConstObject());
    (*obj)[at] = leaf;
    return true;
}

// **The object a declaration builds**, from the statements it was lowered to.
bool Constexpr::foldDeclaredObject(const std::vector<const Stmt *> &init, const Var &target, ConstObject *out,
                                std::size_t pos) {
    ConstexprSlot self;
    self.offset = target.isLocal() ? target.offset() : 0;
    if (!target.isLocal()) self.global = target.symbol();
    self.obj = std::make_shared<ConstObject>();
    ConstexprSlot scratch;
    scratch.offset = kScratchFrame;
    std::vector<ConstexprSlot> frame;
    frame.push_back(scratch);
    frame.push_back(self);
    p_.constexprFrames_.push_back(frame);
    bool ok = true;
    for (const Stmt *s : init) ok = ok && runConstexpr(*s, pos);
    p_.constexprFrames_.pop_back();
    if (ok) *out = *self.obj;
    return ok;
}

// An aggregate's braced list, leaf by leaf, as flattenInit lays it down.
bool Constexpr::constObjectFromInit(const Type *t, const Init &in, int base, ConstObject *out) const {
    t = t->unqualified();
    if (in.isList) {
        if (t->isArray()) {
            const int w = t->pointee()->size(p_.target_);
            for (std::size_t i = 0; i < in.items.size(); i++)
                if (!constObjectFromInit(t->pointee(), in.items[i], base + static_cast<int>(i) * w, out))
                    return false;
            for (long long i = static_cast<long long>(in.items.size()); i < t->length(); i++)
                if (!constObjectFromInit(t->pointee(), Init(), base + static_cast<int>(i) * w, out)) return false;
            return true;
        }
        if (!t->isStructOrUnion() || t->kind() == Kind::Union) return false;
        static const Init none;
        std::size_t k = 0;
        for (const Member &m : t->members()) {
            if (m.declaredIn != nullptr || m.isBitField()) return false;
            if (!constObjectFromInit(m.type, k < in.items.size() ? in.items[k] : none, base + m.offset, out))
                return false;
            k++;
        }
        return k >= in.items.size();
    }
    if (t->isArray() || t->isStructOrUnion()) {
        if (in.value != nullptr) {
            ConstObject whole;
            if (!foldObject(*in.value, &whole, in.pos)) return false;
            storeSubObject(*out, base, t->size(p_.target_), whole);
            return true;
        }
        // Left out of the list: zero, leaf by leaf.
        if (t->isArray()) {
            for (long long i = 0; i < t->length(); i++)
                if (!constObjectFromInit(t->pointee(), Init(), base + static_cast<int>(i * t->pointee()->size(p_.target_)), out))
                    return false;
            return true;
        }
        for (const Member &m : t->members())
            if (m.declaredIn == nullptr && !constObjectFromInit(m.type, Init(), base + m.offset, out)) return false;
        return true;
    }
    if (!t->isArithmetic()) return false;
    ConstLeaf leaf;
    leaf.type = t;
    if (in.value != nullptr && (t->isFloating() ? !foldFloating(*in.value, &leaf.d) : !fold(*in.value, &leaf.i, in.pos)))
        return false;
    if (!t->isFloating()) leaf.i = t->isBool() ? leaf.i != 0 : narrowTo(leaf.i, t);
    (*out)[base] = leaf;
    return true;
}

// The bytes a constant object is laid down as, each leaf the way flattenInit writes a scalar.
void Constexpr::constObjectPieces(const ConstObject &o, std::vector<GlobalPiece> &out) {
    for (const auto &leaf : o) {
        Init in;
        Num *n = leaf.second.type->isFloating() ? new Num(leaf.second.d) : new Num(leaf.second.i);
        n->setType(leaf.second.type);
        in.value.reset(n);
        p_.flattenScalar(leaf.second.type, in, leaf.first, out);
    }
}

// **The one answer when it does not fold**: [dcl.constexpr]/9 wants a constant, and what this
// compiler can fold of a class is a constexpr constructor's member initialisers and an aggregate's list.
void Constexpr::refuseConstexprClassObject(const std::string &name, std::size_t pos) const {
    p_.src_.fail(pos, "'" + name + "' is 'constexpr', and its value could not be worked out while compiling - "
                   "either its initialiser is not a constant expression, or it needs what folding an object "
                   "of class type does not support yet: a pointer, reference, union or bit-field member, an "
                   "array of class objects, or a constructor that is not 'constexpr'");
}

// **Every value below is carried as a `double`, exactly, or the fold says so.**
// A host `long double` is 64 bits of significand on the Linux box and 53 on the
// Mac, so the fold works in double and flags `past53` and `x87Rounded`.
bool Constexpr::foldDouble(const Expr &e, const Target &target, long double *out,
                        bool *past53, bool *x87Rounded) const {
    // **A const floating object read back**, the twin of the integral read-back fold() already
    // makes: `const double b = (1 - f) * a;` is dynamic initialisation by the letter of
    // [basic.start.init], and folding it is what every compiler does instead.
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->obj != nullptr) return false;
            *out = slot->floating ? slot->d : static_cast<long double>(slot->i);
            return true;
        }
        if (v->isLocal()) {
            if (const Local *l = p_.lookup_.findLocal(v->name()))
                if (l->isConstantDouble) { *out = l->constantDouble; return true; }
            return false;
        }
        if (const GlobalSym *g = p_.lookup_.findGlobal(v->name()))
            if (g->isConstantDouble) { *out = g->constantDouble; return true; }
        // As in fold(): a static member lives under its symbol, not in globals_.
        if (const Parser::StaticConst *sc = p_.findStaticConst(v->symbol()))
            if (sc->dknown) { *out = sc->dvalue; return true; }
        return false;
    }
    // A member of a constant object, as fold() reads one.
    if (dynamic_cast<const MemberAccess *>(&e) != nullptr ||
        (dynamic_cast<const Unary *>(&e) != nullptr && static_cast<const Unary &>(e).op() == '*')) {
        ConstLeaf leaf;
        if (!foldLeaf(e, &leaf, 0)) return false;
        *out = leaf.type->isFloating() ? leaf.d
             : leaf.type->isSigned(target) ? static_cast<long double>(leaf.i)
             : static_cast<long double>(static_cast<unsigned long long>(leaf.i));
        return true;
    }
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) {
            *out = inType(n->type(), target, n->dvalue());
        } else {
            unsigned long long a = n->value() < 0
                ? 0ULL - static_cast<unsigned long long>(n->value())
                : static_cast<unsigned long long>(n->value());
            while (a != 0 && (a & 1) == 0) a >>= 1;
            if (a >= (1ULL << 53)) *past53 = true;
            *out = static_cast<double>(n->value());
        }
        return true;
    }
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        if (!foldDouble(c->value(), target, out, past53, x87Rounded))
            return false;

        const Type *ct = c->type();
        if (ct->kind() == Kind::Float)       *out = static_cast<float>(*out);
        else if (ct->kind() == Kind::Double) *out = static_cast<double>(*out);
        else if (ct->kind() == Kind::LongDouble && !ct->isX87(target))
                                             *out = static_cast<double>(*out);
        else if (!ct->isFloating()) {
            if (ct->isSigned(target))
                *out = static_cast<long double>(
                           static_cast<long long>(*out));
            else
                *out = static_cast<long double>(
                           static_cast<unsigned long long>(*out));
        }
        return true;
    }
    // **A call to a constexpr function, as fold() runs one** (A11).
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        // No expression carries a position, so the depth limit is reported at
        // the constexpr function's own definition.
        auto it = p_.constexprFns_.find(c->symbol());
        const ConstexprFn *fn = nullptr;
        if (!enterConstexprCall(*c, &fn, it == p_.constexprFns_.end() ? 0 : it->second.pos))
            return false;
        const bool ok = foldDouble(*fn->value, target, out, past53, x87Rounded);
        p_.constexprFrames_.pop_back();
        if (ok && !e.type()->isFloating())
            *out = static_cast<long double>(static_cast<long long>(*out));
        return ok;
    }
    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        long long t;
        if (!fold(c->cond(), &t, 0)) return false;
        return foldDouble(t ? c->thenArm() : c->elseArm(), target, out, past53, x87Rounded);
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '-' &&
            foldDouble(u->operand(), target, out, past53, x87Rounded)) {
            *out = -*out;
            return true;
        }
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long double l, r;
        if (!foldDouble(b->lhs(), target, &l, past53, x87Rounded) ||
            !foldDouble(b->rhs(), target, &r, past53, x87Rounded))
            return false;

        Kind bk = b->type()->kind();
        bool asDouble = bk == Kind::Double ||
                        (bk == Kind::LongDouble && !b->type()->isX87(target));
        if (bk == Kind::Float) {
            float fl = static_cast<float>(l), fr = static_cast<float>(r);
            switch (b->op()) {
            case BinOp::Add: *out = fl + fr; return true;
            case BinOp::Sub: *out = fl - fr; return true;
            case BinOp::Mul: *out = fl * fr; return true;
            case BinOp::Div: if (fr == 0) return false;
                             *out = fl / fr; return true;
            default: return false;
            }
        }
        if (asDouble) {
            double dl = static_cast<double>(l), dr = static_cast<double>(r);
            switch (b->op()) {
            case BinOp::Add: *out = dl + dr; return true;
            case BinOp::Sub: *out = dl - dr; return true;
            case BinOp::Mul: *out = dl * dr; return true;
            case BinOp::Div: if (dr == 0) return false;
                             *out = dl / dr; return true;
            default: return false;
            }
        }
        // The x87 lane. The operands are exact doubles by construction, so the
        // one question is whether this result still fits one - unless an integer
        // past 53 bits fed in, where the only honest answer is the flag.
        if (*past53) *x87Rounded = true;
        const double dl = static_cast<double>(l);
        const double dr = static_cast<double>(r);
        double sum;
        bool fits;
        switch (b->op()) {
        case BinOp::Add: {
            sum = dl + dr;
            const double bb = sum - dl;
            fits = (dl - (sum - bb)) + (dr - bb) == 0.0;
            break;
        }
        case BinOp::Sub: {
            sum = dl - dr;
            const double bb = sum - dl;
            fits = (dl - (sum - bb)) + (-dr - bb) == 0.0;
            break;
        }
        case BinOp::Mul:
            sum = dl * dr;
            fits = std::fma(dl, dr, -sum) == 0.0;
            break;
        case BinOp::Div:
            if (dr == 0) return false;
            sum = dl / dr;
            fits = std::fma(sum, dr, -dl) == 0.0;
            break;
        default: return false;
        }
        if (sum != 0.0 && sum > -2.2250738585072014e-308 &&
                          sum <  2.2250738585072014e-308)
            fits = false;
        if (!fits) *x87Rounded = true;
        *out = sum;
        return true;
    }
    return false;
}

}   // namespace sema
