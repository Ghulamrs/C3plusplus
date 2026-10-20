// The parser: what is evaluated where it is written.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// [dcl.typedef]/2: `using X = T;` names T as `typedef T X;` would, in whichever scope it stands -
// `prefix` is what the key takes there (a namespace's, a class's tag and "::", or nothing in a block).
bool Parser::aliasDeclaration(const std::string &prefix) {
    if (!peek().is("using")) return false;
    if (peekAt(1).kind != TokenKind::Ident || !(peekAt(2).is("=") || peekAt(2).is("["))) return false;
    at_++;
    const std::size_t pos = peek().pos;
    const std::string name = peek().text;
    at_++;
    skipAttributes();
    expect("=");
    const std::size_t typeAt = peek().pos;
    StorageClass sc;
    const Type *t = specifiers(&sc);
    if (sc != StorageNone)
        src_.fail(typeAt, "an alias declaration names a type, and a storage class is not part of one");
    if (t->unqualified()->kind() == Kind::Deduced)
        src_.fail(typeAt, "an alias declaration names a type, and 'auto' deduces one from an "
                          "initialiser it does not have");
    // `using F = int(int);` - a function type, whose parameter list no declarator reads.
    Declared d{ "", t, typeAt, 0, "" };
    if (peek().is("(") && !atParenInitialiser()) typedefFunctionSuffix(d);
    else d = declarator(t, true);
    if (!d.name.empty())
        src_.fail(d.pos, "an alias declaration names a type, and '" + d.name + "' would declare something "
                         "of that type - write 'using " + name + " = T;' with T a type alone");
    expect(";");
    const std::string key = prefix + name;
    std::unordered_map<std::string, std::size_t>::const_iterator had = typedefIndex_.find(key);
    if (had != typedefIndex_.end() && typedefs_[had->second].type != d.type &&
        typedefs_[had->second].name == key)
        src_.fail(pos, "'" + name + "' is declared twice, and not as the same type: it was '" +
                       typedefs_[had->second].type->describe() + "' and is now '" + d.type->describe() + "'");
    typedefIndex_[key] = typedefs_.size();
    typedefs_.push_back(TypedefName{ key, d.type });
    return true;
}

// [temp.explicit]/2: `extern template` promises a definition elsewhere, and every specialization here is
// mergeable (weak, COMDAT), so the one it would suppress folds with that one - read and dropped.
bool Parser::externTemplateDeclaration() {
    if (!peek().is("extern") || !peekAt(1).is("template")) return false;
    const std::size_t pos = peek().pos;
    at_ += 2;
    int depth = 0;
    for (;; at_++) {
        if (peek().kind == TokenKind::End) src_.fail(pos, "this 'extern template' declaration never ends");
        if (peek().is("(") || peek().is("[") || peek().is("{")) depth++;
        else if (peek().is(")") || peek().is("]") || peek().is("}")) depth--;
        else if (depth == 0 && peek().is(";")) break;
    }
    at_++;
    return true;
}

// **`= default` and `= delete` sit exactly where `= 0` does**, after the virt-specifiers, and every
// declare* that follows reads which one was written. Outside a class only `= delete` has a meaning.
void Parser::readDefaultedOrDeleted() {
    pendingSpecial_ = SpecialDef::None;
    if (!peek().is("=")) return;
    const bool def = peekAt(1).is("default");
    if (!def && !peekAt(1).is("delete")) return;
    pendingSpecialAt_ = peek().pos;
    pendingSpecial_ = def ? SpecialDef::Defaulted : SpecialDef::Deleted;
    at_ += 2;
    if (def && classStack_.empty())
        src_.fail(pendingSpecialAt_, "'= default' outside the class is not supported yet: a special "
                                     "member defaulted on a later declaration is user-provided, where "
                                     "one defaulted inside its class is the implicit one - write it there");
}

// [dcl.fct.def.delete]/2: a deleted function may not be used, and this is said where the use is.
void Parser::refuseDeleted(const Signature &f, std::size_t pos) const {
    if (!f.deleted) return;
    const std::string name = f.owner.empty() ? f.name : f.owner + "::" + f.name;
    if (f.implicit)
        src_.fail(pos, "'" + name + "' is deleted - the compiler would write it, and [class.copy] deletes "
                       "it: a move written '= default', or a base or member whose own is deleted");
    src_.fail(pos, "'" + name + "' is deleted - it was declared '= delete', and a deleted function "
                   "cannot be called, copied from or taken the address of");
}

// [dcl.fct.def.default]/1: only a special member may be defaulted, and once per class.
void Parser::defaultedMember(DefaultedMember &m, Access access, std::size_t pos, const std::string &what) {
    if (m.is) src_.fail(pos, what + " is defaulted twice");
    m.is = true;
    m.access = access;
}

// What the class wrote `= default` on, or nothing.
Parser::Defaulted Parser::defaultedFor(const std::string &tag) const {
    std::map<std::string, Defaulted>::const_iterator it = defaulted_.find(tag);
    return it != defaulted_.end() ? it->second : Defaulted();
}

// [dcl.constexpr]/4: a C++11 constexpr constructor's body is empty; statements in it are C++14.
void Parser::refuseConstexprConstructorBody(std::size_t parenAt) const {
    std::size_t k = parenAt;
    int depth = 0;
    for (;; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End || (depth == 0 && t.is(";"))) return;
        if (t.is("(") || t.is("[")) depth++;
        else if (t.is(")") || t.is("]")) depth--;
        else if (t.is("{")) {
            const Token &before = peekAt(k - 1);
            if (depth == 0 && before.kind != TokenKind::Ident && !before.is(">")) break;
            depth++;
        } else if (t.is("}")) depth--;
    }
    if (peekAt(k + 1).is("}")) return;
    src_.fail(peekAt(k + 1).pos, "a 'constexpr' constructor with statements in its body is C++14, "
                                 "and this compiler is C++11 - [dcl.constexpr]/4 asks for an empty "
                                 "body and the work in the initialiser list");
}


// `friend class X;`, or `friend X;` for a class already declared - [class.friend]/2: every member
// of X reaches what `owner` keeps private. An X not yet declared is declared by it in the namespace
// around `owner`, [namespace.memdef]/3.
void Parser::befriendClass(const std::string &owner, bool keyed) {
    if (keyed) at_++;
    const std::string name = expectIdent("the class to befriend");
    std::string key = name;
    if (const Type *t = findTypedef(name)) key = t->unqualified()->tag();
    else if (!namespaceStack_.empty()) key = namespacePrefix() + name;
    friendClasses_[owner].push_back(key);
    expect(";");
}

// Is the code being read inside a member of a class `cls` befriended - or of a class nested in one?
bool Parser::isFriendClassOf(const Type *cls) const {
    if (cls == nullptr) return false;
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        friendClasses_.find(cls->unqualified()->tag());
    if (it == friendClasses_.end()) return false;
    const Type *here[3] = { currentClass_, classStack_.empty() ? nullptr : classStack_.back(), lambdaScope() };
    for (const Type *h : here)
        for (const Type *c = h; c != nullptr; c = c->enclosing())
            for (std::size_t i = 0; i < it->second.size(); i++)
                if (c->unqualified()->tag() == it->second[i]) return true;
    return false;
}

// The implicit member a `= default` stands for was declared public; it takes the access it was written under.
void Parser::applyDefaultedAccess(const std::string &tag, const Type *type) {
    std::map<std::string, Defaulted>::const_iterator it = defaulted_.find(tag);
    if (it == defaulted_.end()) return;
    const Defaulted &df = it->second;
    if (const Signature *f = defaultConstructorOf(type))
        if (df.defaultCtor.is) functions_[static_cast<std::size_t>(f - &functions_[0])].access = df.defaultCtor.access;
    if (const Signature *f = copyConstructorOf(type))
        if (df.copyCtor.is) functions_[static_cast<std::size_t>(f - &functions_[0])].access = df.copyCtor.access;
    if (const Signature *f = moveConstructorOf(type))
        if (df.moveCtor.is) functions_[static_cast<std::size_t>(f - &functions_[0])].access = df.moveCtor.access;
    if (const Signature *f = copyAssignOf(type))
        if (df.copyAssign.is) functions_[static_cast<std::size_t>(f - &functions_[0])].access = df.copyAssign.access;
    if (const Signature *f = destructorOf(type))
        if (df.dtor.is) functions_[static_cast<std::size_t>(f - &functions_[0])].access = df.dtor.access;
}

bool Parser::staticAssertion() {
    if (!peek().is("static_assert")) return false;
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");

    const std::size_t condAt = peek().pos;
    ExprPtr cond = decay(conditional());
    long long value;
    if (!fold(*cond, &value, condAt))
        src_.fail(condAt, "the condition of a 'static_assert' has to be a "
                          "constant the compiler can work out here, and this "
                          "is not one");

    if (peek().is(")"))
        src_.fail(peek().pos, "a 'static_assert' with no message is C++17 - "
                              "write the message this one would have printed, "
                              "'static_assert(cond, \"why\")'");
    expect(",");

    if (peek().kind != TokenKind::Str)
        src_.fail(peek().pos, "the message of a 'static_assert' has to be "
                              "written out as a string literal - it is printed "
                              "by the compiler, so there is no program running "
                              "to read a variable");
    std::string message = peek().text;
    at_++;
    while (peek().kind == TokenKind::Str) {   // "a" "b" is one literal
        message += peek().text;
        at_++;
    }

    expect(")");
    expect(";");

    if (value == 0) src_.fail(pos, "static assertion failed: " + message);
    return true;
}

// **The exception specification, and the one thing it does not touch.** In C++11 it is
// *not* part of the function's type - measured, both spellings mangle alike - so what
// it buys is `noexcept(e)`, or `throw(T...)`'s list, kept in pendingSpec_ for the definition.
bool Parser::exceptionSpecification() {
    skipAttributes();
    pendingSpec_ = DynamicSpec();
    bool noexc = false;
    if (consume("noexcept")) {
        noexc = true;
        if (consume("(")) {
            const long long value = constantExpression("a constant in 'noexcept('");
            expect(")");
            noexc = value != 0;
        }
    } else if (peek().is("throw") && peekAt(1).is("(")) {
        const std::size_t at = peek().pos;
        at_ += 2;
        if (consume(")")) {
            noexc = true;
        } else {
            pendingSpec_.written = true;
            pendingSpec_.pos = at;
            do {
                const std::size_t tpos = peek().pos;
                StorageClass sc;
                const Type *t = declarator(specifiers(&sc), true).type;
                if (peek().is("..."))
                    src_.fail(peek().pos, "a pack expansion in a dynamic exception specification is not "
                                          "supported yet - write the types out");
                pendingSpec_.types.push_back(adjustedSpecType(t, tpos));
            } while (consume(","));
            expect(")");
        }
    }
    readVirtSpecifiers();
    readDefaultedOrDeleted();
    return noexc;
}

// [except.spec]/2: a reference names its referent, an array or a function decays, and the
// top-level qualifiers go - what is left must be complete, or point at a complete type or void.
const Type *Parser::adjustedSpecType(const Type *t, std::size_t pos) {
    if (t->isRValueReference())
        src_.fail(pos, "an rvalue reference type is not allowed in an exception specification - [except.spec]/2");
    if (t->isReference()) t = t->referent();
    if (t->isArray()) t = types_.pointerTo(t->pointee());
    else if (t->isFunction()) t = types_.pointerTo(t);
    t = t->unqualified();
    if (patternOnly_) return t;
    const Type *named = (t->isPointer() ? t->pointee() : t)->unqualified();
    if (named->kind() == Kind::TemplateParam || named->kind() == Kind::DependentMember) return t;
    if (!named->isVoid() && !named->isComplete())
        src_.fail(pos, "'" + named->describe() + "' is incomplete, and an exception specification may "
                       "only name a complete type, or a pointer or reference to one - [except.spec]/2");
    return t;
}

// **`override` and `final` are contextual and follow the exception specification** - [class.mem]. Inside a
// class body they are recorded for the declaration being read; a held body's replay has already checked
// them and steps over; anywhere else - a free function, an out-of-line definition - they are ill-formed.
void Parser::readVirtSpecifiers() {
    pendingVirt_ = VirtSpecifiers();
    while (peek().is("override") || peek().is("final")) {
        const bool ov = peek().is("override");
        VirtSpecifiers &v = pendingVirt_;
        if ((ov && v.isOverride) || (!ov && v.isFinal))
            src_.fail(peek().pos, std::string("'") + peek().text + "' is written twice");
        if (ov) { v.isOverride = true; v.overrideAt = peek().pos; }
        else    { v.isFinal = true;    v.finalAt = peek().pos; }
        if (classStack_.empty() && !replayingInline_)
            src_.fail(peek().pos, std::string("'") + peek().text + "' can only be written on a "
                                  "member function's declaration inside its class - "
                                  "[class.mem] - and this declaration is outside one");
        at_++;
    }
    if (!classStack_.empty()) return;
    pendingVirt_ = VirtSpecifiers();
}

// A constructor, a static member or a data member may carry neither word.
void Parser::refuseVirtSpecifiers(const char *what) {
    const VirtSpecifiers v = takeVirtSpecifiers();
    if (v.isOverride)
        src_.fail(v.overrideAt, std::string("'override' marks a virtual member function, and ") + what +
                                " cannot be virtual");
    if (v.isFinal)
        src_.fail(v.finalAt, std::string("'final' marks a virtual member function, and ") + what +
                             " cannot be virtual");
}

long long Parser::constantExpression(const char *what) {
    std::size_t pos = peek().pos;
    ExprPtr e = decay(conditional());
    long long v;
    if (!fold(*e, &v, pos))
        src_.fail(pos, std::string("expected ") + what +
                       ", and this is not an integer constant expression");
    return v;
}

// The frame an object fold outside any call stands in, so a temporary it builds has a slot to live in.
static const int kScratchFrame = INT_MIN;

const Parser::ConstexprSlot *Parser::constexprSlot(const Var &v) const {
    if (constexprFrames_.empty()) return nullptr;
    const std::vector<ConstexprSlot> &frame = constexprFrames_.back();
    for (std::size_t i = 0; i < frame.size(); i++) {
        if (frame[i].offset == kScratchFrame) continue;
        if (v.isLocal() ? frame[i].global.empty() && frame[i].offset == v.offset()
                        : frame[i].global == v.symbol() || frame[i].global == v.name()) return &frame[i];
    }
    return nullptr;
}

bool Parser::enterConstexprCall(const Call &c, const ConstexprFn **out,
                                std::size_t pos) const {
    auto it = constexprFns_.find(c.symbol());
    if (it == constexprFns_.end() && instantiateForConstant(c.symbol())) it = constexprFns_.find(c.symbol());
    if (it == constexprFns_.end()) return false;
    const ConstexprFn &fn = it->second;
    if (fn.value == nullptr || c.args().size() != fn.slots.size())
        return false;

    // A recursion that does not end is a compiler that does not either.
    // The standard lets an implementation set a limit and say so; this is
    // that limit, and it is said where it is reached.
    if (constexprFrames_.size() >= 256)
        src_.fail(pos, "this constant expression is more than 256 calls "
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
    constexprFrames_.push_back(frame);
    *out = &fn;
    return true;
}

// **An address built on a constant pointer is a constant**, `(size_t)&((S *)0)->m` - offsetof,
// [support.types]/4: a cast of an integer to a pointer, `*` of it, members of that, and `&`.
bool Parser::foldPointerConstant(const Expr &e, long long *out, std::size_t pos) const {
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

bool Parser::fold(const Expr &e, long long *out, std::size_t pos) const {
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
            if (const Local *l = findLocal(v->name()))
                if (l->isConstantValue) { *out = l->constantValue; return true; }
            return false;
        }
        if (const GlobalSym *g = findGlobal(v->name()))
            if (g->isConstantValue) { *out = g->constantValue; return true; }
        // A static member is not in globals_ - it is kept apart from the
        // member list so nothing walking a layout has to skip it - so the
        // read-back asks the table keyed by its symbol.
        if (const StaticConst *sc = findStaticConst(v->symbol()))
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
        constexprFrames_.pop_back();
        if (!ok) return false;
        *out = result;
        return true;
    }
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) return false;
        *out = n->value();
        return true;
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
            v = e.type()->isSigned(target_)
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
        bool uns = t->isInteger() && !t->isSigned(target_);
        unsigned long long ul = static_cast<unsigned long long>(l);
        unsigned long long ur = static_cast<unsigned long long>(r);

        switch (b->op()) {
        case BinOp::Add: *out = static_cast<long long>(ul + ur); return true;
        case BinOp::Sub: *out = static_cast<long long>(ul - ur); return true;
        case BinOp::Mul: *out = static_cast<long long>(ul * ur); return true;
        case BinOp::Div:
        case BinOp::Mod:
            if (r == 0)
                src_.fail(pos, "division by zero in a constant expression");
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
                src_.fail(pos, "shift count out of range in a constant expression");
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
const Expr *Parser::singleReturnValue(const Stmt &body) const {
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

bool Parser::constantInitialiser(const Type *t, const Init &in,
                                 long long *out) const {
    if (t == nullptr || !t->isConst() || !t->isInteger()) return false;
    if (in.isList || in.value == nullptr) return false;
    return fold(*in.value, out, in.pos);
}

// The floating twin: a const floating object with a foldable initialiser is
// read back by foldDouble, an integer one being widened on the way in.
bool Parser::constantFloatingInitialiser(const Type *t, const Init &in,
                                         long double *out) const {
    if (t == nullptr || !t->isConst() || !t->isFloating()) return false;
    if (in.isList || in.value == nullptr) return false;
    return foldFloating(*in.value, out);
}

// foldDouble with its two host-precision flags folded away: an answer is an
// answer here, as it is for the integer lane.
bool Parser::foldFloating(const Expr &e, long double *out) const {
    bool past53 = false, x87 = false;
    return foldDouble(e, target_, out, &past53, &x87);
}

long long Parser::narrowTo(long long v, const Type *t) const {
    int bits = t->size(target_) * 8;
    if (bits >= 64) return v;
    unsigned long long mask = (1ULL << bits) - 1;
    unsigned long long kept = static_cast<unsigned long long>(v) & mask;
    if (t->isSigned(target_) && (kept & (1ULL << (bits - 1)))) kept |= ~mask;
    return static_cast<long long>(kept);
}

// The same types, in any order and however often each is written.
bool Parser::DynamicSpec::sameSet(const DynamicSpec &o) const {
    auto within = [](const std::vector<const Type *> &a, const std::vector<const Type *> &b) {
        for (const Type *t : a) {
            bool found = false;
            for (const Type *u : b) found = found || t == u || t->describe() == u->describe();
            if (!found) return false;
        }
        return true;
    };
    return within(types, o.types) && within(o.types, types);
}

// ---------------------------------------------------------------- constant objects
// **[expr.const]/2 lets a literal class's object be worked out while compiling**: a constexpr
// constructor's lowered body runs over its scalar leaves, a pointer being object and offset. A
// bit-field, an address, a union read through another member: none folds, and the need is refused.

// The leaves of `from` in [at, at + size), moved to start at 0 - a member or a base taken whole.
Parser::ConstObject Parser::subObject(const ConstObject &from, int at, int size) {
    ConstObject out;
    for (ConstObject::const_iterator it = from.lower_bound(at);
         it != from.end() && it->first < at + size; ++it)
        out[it->first - at] = it->second;
    return out;
}

void Parser::storeSubObject(ConstObject &into, int at, int size, const ConstObject &what) {
    into.erase(into.lower_bound(at), into.lower_bound(at + size));
    for (const auto &leaf : what) into[at + leaf.first] = leaf.second;
}

std::string Parser::constObjectKey(const Var &v) const {
    return v.isLocal() ? currentFunction_ + "@" + std::to_string(v.offset()) : v.symbol();
}

// What a call's argument is worth: a number, an object of class type, or a pointer into one.
bool Parser::foldArgument(const Expr &a, ConstexprSlot *slot, std::size_t pos) const {
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
bool Parser::objectAddress(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->obj == nullptr || slot->pointer) return false;
            *obj = slot->obj;
            *at = 0;
            return true;
        }
        // A named object outside any call: inside one, every name is a parameter or a temporary.
        if (!constexprFrames_.empty() && (constexprFrames_.back().empty() ||
                                          constexprFrames_.back()[0].offset != kScratchFrame))
            return false;
        std::map<std::string, ConstObject>::const_iterator it = constObjects_.find(constObjectKey(*v));
        if (it == constObjects_.end() && !v->isLocal()) it = constObjects_.find(v->name());
        if (it == constObjects_.end()) return false;
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
bool Parser::foldObjectPointer(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const {
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
        const long long step = pt->pointee()->isVoid() ? 1 : pt->pointee()->size(target_);
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
bool Parser::instantiateForConstant(const std::string &symbol) const {
    Parser &self = const_cast<Parser &>(*this);
    std::string complete = symbol;
    const std::size_t c2 = complete.find("C2E");
    if (c2 != std::string::npos) complete.replace(c2, 3, "C1E");
    for (std::size_t i = 0; i < specializations_.size(); i++) {
        if (!specializations_[i].isClass) continue;
        std::vector<PendingBody> &bodies = self.specializations_[i].bodies;
        for (std::size_t b = 0; b < bodies.size(); b++) {
            if (bodies[b].which >= functions_.size() || functions_[bodies[b].which].symbol != complete) continue;
            const PendingBody body = bodies[b];
            bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(b));
            self.functions_[body.which].used = true;
            const Specialization sp = specializations_[i];
            std::vector<Shadow> undo;
            self.bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            std::vector<std::vector<ConstexprSlot> > frames;
            frames.swap(constexprFrames_);
            const std::string wasKey = instantiationKey_, wasOf = instantiationOf_;
            self.instantiationKey_.clear();
            self.instantiationOf_.clear();
            const bool wasInline = replayingInline_;
            const std::size_t resume = at_;
            self.replayInlineBodies(std::vector<PendingBody>(1, body));
            self.at_ = resume;
            self.replayingInline_ = wasInline;
            self.instantiationKey_ = wasKey;
            self.instantiationOf_ = wasOf;
            frames.swap(constexprFrames_);
            self.unbindTemplateParameters(undo);
            return true;
        }
    }
    return false;
}

// **The value of an expression of class type.**
bool Parser::foldObject(const Expr &e, ConstObject *out, std::size_t pos) const {
    const Type *t = e.type();
    if (t == nullptr || !t->unqualified()->isStructOrUnion()) return false;
    const bool scratch = constexprFrames_.empty();
    if (scratch) constexprFrames_.push_back(std::vector<ConstexprSlot>(1, ConstexprSlot()));
    if (scratch) constexprFrames_.back()[0].offset = kScratchFrame;
    bool ok = false;
    ConstObjectPtr obj;
    int at = 0;
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        const ConstexprFn *fn = nullptr;
        if (enterConstexprCall(*c, &fn, pos)) {
            ok = foldObject(*fn->value, out, pos);
            constexprFrames_.pop_back();
        }
    } else if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        ok = c->value().type() != nullptr && c->value().type()->unqualified() == t->unqualified() &&
             foldObject(c->value(), out, pos);
    } else if (objectAddress(e, &obj, &at, pos)) {
        *out = subObject(*obj, at, t->size(target_));
        ok = true;
    } else if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        // A member of an object that is itself a value - `make().inner`.
        ConstObject whole;
        ok = !m->isBitField() && foldObject(m->object(), &whole, pos);
        if (ok) *out = subObject(whole, m->offset(), t->size(target_));
    }
    if (scratch) constexprFrames_.pop_back();
    return ok;
}

// **One scalar of a constant object**, the type the read asks for and the leaf agreeing on it.
bool Parser::foldLeaf(const Expr &e, ConstLeaf *out, std::size_t pos) const {
    const Type *t = e.type();
    if (t == nullptr || !t->isArithmetic()) return false;
    const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e);
    if (m != nullptr && m->isBitField()) return false;
    const bool scratch = constexprFrames_.empty();
    if (scratch) constexprFrames_.push_back(std::vector<ConstexprSlot>(1, ConstexprSlot()));
    if (scratch) constexprFrames_.back()[0].offset = kScratchFrame;
    ConstObjectPtr obj;
    int at = 0;
    bool ok = objectAddress(e, &obj, &at, pos);
    if (!ok && m != nullptr) {
        ConstObject whole;
        ok = foldObject(m->object(), &whole, pos);
        if (ok) { obj = std::make_shared<ConstObject>(std::move(whole)); at = m->offset(); }
    }
    if (scratch) constexprFrames_.pop_back();
    if (!ok) return false;
    ConstObject::const_iterator it = obj->find(at);
    if (it == obj->end() || it->second.type->isFloating() != t->isFloating() ||
        it->second.type->size(target_) != t->size(target_))
        return false;
    *out = it->second;
    return true;
}

// **A constexpr constructor run on the object its first argument points at.**
bool Parser::constructConst(const Call &c, std::size_t pos) const {
    std::map<std::string, ConstexprCtor>::const_iterator it = constexprCtors_.find(c.symbol());
    if (it == constexprCtors_.end() && instantiateForConstant(c.symbol())) it = constexprCtors_.find(c.symbol());
    if (it == constexprCtors_.end() || c.args().empty()) return false;
    const ConstexprCtor &ctor = it->second;
    if (ctor.slots.size() + 1 != c.args().size()) return false;
    if (constexprFrames_.size() >= 256)
        src_.fail(pos, "this constant expression is more than 256 calls deep - a 'constexpr' "
                       "function that never stops recursing cannot be worked out while compiling");
    // A temporary the comma around this call reads back is made here, in the caller's frame.
    ConstexprSlot self;
    self.offset = ctor.thisSlot;
    self.pointer = true;
    int at = 0;
    if (!foldObjectPointer(*c.args()[0], &self.obj, &at, pos)) {
        const Unary *u = dynamic_cast<const Unary *>(c.args()[0].get());
        const Var *v = u != nullptr && u->op() == '&' ? dynamic_cast<const Var *>(&u->operand()) : nullptr;
        if (v == nullptr || !v->isLocal() || constexprFrames_.empty()) return false;
        ConstexprSlot made;
        made.offset = v->offset();
        made.obj = std::make_shared<ConstObject>();
        constexprFrames_.back().push_back(made);
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
    constexprFrames_.push_back(frame);
    const bool ok = runConstexpr(*ctor.body, pos);
    constexprFrames_.pop_back();
    return ok;
}

// The statements a constructor's body and a declaration's initialisation are lowered to.
bool Parser::runConstexpr(const Stmt &s, std::size_t pos) const {
    if (const Block *b = dynamic_cast<const Block *>(&s)) {
        for (const StmtPtr &x : b->body())
            if (!runConstexpr(*x, pos)) return false;
        return true;
    }
    if (const ExprStmt *x = dynamic_cast<const ExprStmt *>(&s)) return runConstexprEffect(x->expr(), pos);
    return false;
}

bool Parser::runConstexprEffect(const Expr &e, std::size_t pos) const {
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return runConstexprEffect(c->left(), pos) && runConstexprEffect(c->right(), pos);
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        // A function returning an object into a slot of the frame - the slot holds its value from here.
        if (c->resultSlot() != 0 && c->type() != nullptr && c->type()->unqualified()->isStructOrUnion() &&
            !constexprFrames_.empty()) {
            ConstexprSlot made;
            made.offset = c->resultSlot();
            made.obj = std::make_shared<ConstObject>();
            if (!foldObject(*c, made.obj.get(), pos)) return false;
            constexprFrames_.back().push_back(made);
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
        if (v == nullptr || !v->isLocal() || constexprFrames_.empty()) return false;
        ConstexprSlot slot;
        slot.offset = v->offset();
        int at = 0;
        if (!foldObjectPointer(a->value(), &slot.obj, &at, pos)) return false;
        slot.pointer = true;
        slot.i = at;
        std::vector<ConstexprSlot> &frame = constexprFrames_.back();
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
        storeSubObject(*obj, at, t->size(target_), value);
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
    storeSubObject(*obj, at, t->size(target_), ConstObject());
    (*obj)[at] = leaf;
    return true;
}

// **The object a declaration builds**, from the statements it was lowered to.
bool Parser::foldDeclaredObject(const std::vector<const Stmt *> &init, const Var &target, ConstObject *out,
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
    constexprFrames_.push_back(frame);
    bool ok = true;
    for (const Stmt *s : init) ok = ok && runConstexpr(*s, pos);
    constexprFrames_.pop_back();
    if (ok) *out = *self.obj;
    return ok;
}

// An aggregate's braced list, leaf by leaf, as flattenInit lays it down.
bool Parser::constObjectFromInit(const Type *t, const Init &in, int base, ConstObject *out) const {
    t = t->unqualified();
    if (in.isList) {
        if (t->isArray()) {
            const int w = t->pointee()->size(target_);
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
            storeSubObject(*out, base, t->size(target_), whole);
            return true;
        }
        // Left out of the list: zero, leaf by leaf.
        if (t->isArray()) {
            for (long long i = 0; i < t->length(); i++)
                if (!constObjectFromInit(t->pointee(), Init(), base + static_cast<int>(i * t->pointee()->size(target_)), out))
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
void Parser::constObjectPieces(const ConstObject &o, std::vector<GlobalPiece> &out) {
    for (const auto &leaf : o) {
        Init in;
        Num *n = leaf.second.type->isFloating() ? new Num(leaf.second.d) : new Num(leaf.second.i);
        n->setType(leaf.second.type);
        in.value.reset(n);
        flattenScalar(leaf.second.type, in, leaf.first, out);
    }
}

// **The one answer when it does not fold**: [dcl.constexpr]/9 wants a constant, and what this
// compiler can fold of a class is a constexpr constructor's member initialisers and an aggregate's list.
void Parser::refuseConstexprClassObject(const std::string &name, std::size_t pos) const {
    src_.fail(pos, "'" + name + "' is 'constexpr', and its value could not be worked out while compiling - "
                   "either its initialiser is not a constant expression, or it needs what folding an object "
                   "of class type does not support yet: a pointer, reference, union or bit-field member, an "
                   "array of class objects, or a constructor that is not 'constexpr'");
}
