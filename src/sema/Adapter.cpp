// sema::Adapter - see Adapter.h.

#include "Adapter.h"
#include "../AstDump.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"

#include <cstdio>
#include <map>

namespace sema {

void Adapter::outside(const std::string &why) { throw Outside(why); }

const syntax::Node &Adapter::syn(TypedId id) const { return p_.syntaxTree_.node(typer_.tree().node(id).syntax); }

StmtPtr Adapter::body(TypedId typed, const Function &fn, std::string *why) {
    returns_ = fn.returns();
    try {
        const TypedNode &f = typer_.tree().node(typed);
        if (f.count == 0) outside("a function with no body");
        if (fn.isNoexcept() || fn.hasLandingPads()) outside("a function whose body is a region");
        for (std::uint32_t i = 0; i + 1 < f.count; i++)
            if (syn(typer_.tree().child(typed, i)).kind != syntax::Kind::Var) outside("a parameter that is not a var");
        const TypedId last = typer_.tree().child(typed, f.count - 1);
        if (syn(last).kind != syntax::Kind::Block) outside("a body that is not a block");
        return stmt(last);
    } catch (const Outside &o) {
        *why = o.why;
        return StmtPtr();
    }
}

namespace {

template <class T> StmtPtr placed(std::unique_ptr<T> s, const syntax::Node &n, syntax::NodeId id) {
    s->setPos(n.loc);
    s->setSyntax(id);
    return StmtPtr(std::move(s));
}

template <class T> ExprPtr typed(std::unique_ptr<T> e, const Type *t) {
    e->setType(t);
    return ExprPtr(std::move(e));
}

bool scalarArithmetic(const Type *t) { return t != nullptr && t->isArithmetic() && !t->isScopedEnumeration(); }

// What the adapter takes as a scalar: a number not of a scoped enumeration, or a pointer.
bool scalar(const Type *t) { return scalarArithmetic(t) || (t != nullptr && t->isPointer()); }

// An array of such scalars, any number of dimensions deep.
bool scalarArray(const Type *t) {
    if (t == nullptr || !t->isArray()) return false;
    while (t->isArray()) t = t->pointee();
    return scalar(t);
}

// What the parser can read twice for `a op= b` - clonePure's shapes.
bool pure(const Expr &e) {
    if (dynamic_cast<const Num *>(&e) || dynamic_cast<const Var *>(&e) || dynamic_cast<const StrLit *>(&e)) return true;
    if (const Cast *k = dynamic_cast<const Cast *>(&e)) return pure(k->value());
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) return pure(u->operand());
    if (const Binary *b = dynamic_cast<const Binary *>(&e)) return pure(b->lhs()) && pure(b->rhs());
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) return pure(m->object());
    return false;
}

// An object that may be written: a name, or what a pointer points at.
bool lvalueTarget(const Expr &e) {
    if (dynamic_cast<const Var *>(&e) || dynamic_cast<const MemberAccess *>(&e)) return !e.type()->isArray();
    const Unary *u = dynamic_cast<const Unary *>(&e);
    return u != nullptr && u->op() == '*';
}

}   // namespace

StmtPtr Adapter::stmt(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const syntax::Node &s = syn(id);
    switch (s.kind) {
        case syntax::Kind::Block: {
            std::unordered_map<std::uint32_t, int>::const_iterator scope = p_.syntaxScopes_.find(n.syntax.v);
            if (scope == p_.syntaxScopes_.end()) outside("a block whose scope was not recorded");
            std::vector<StmtPtr> body;
            for (std::uint32_t i = 0; i < n.count; i++) body.push_back(stmt(tree.child(id, i)));
            std::unique_ptr<Block> b = make_owned<Block>(std::move(body));
            b->setScope(scope->second);
            return placed(std::move(b), s, n.syntax);
        }
        case syntax::Kind::DeclStmt:
            return declaration(id);
        case syntax::Kind::ExprStmt:
            if (n.count != 1) outside("an expression statement of " + std::to_string(n.count) + " parts");
            return placed(make_owned<ExprStmt>(expr(tree.child(id, 0))), s, n.syntax);
        case syntax::Kind::Return:
            if (n.count == 0) {
                if (returns_ != nullptr && !returns_->isVoid()) outside("a return with no value");
                return placed(make_owned<Return>(ExprPtr()), s, n.syntax);
            }
            if (n.count != 1 || !scalar(returns_)) outside("a return of what is not a scalar");
            return placed(make_owned<Return>(convert(decay(expr(tree.child(id, 0))), returns_)), s, n.syntax);
        case syntax::Kind::If: {
            if (n.count != 2 && n.count != 3) outside("an if of " + std::to_string(n.count) + " parts");
            if (syn(tree.child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
            ExprPtr cond = condition(tree.child(id, 0));
            StmtPtr then = stmt(tree.child(id, 1));
            StmtPtr otherwise = n.count == 3 ? stmt(tree.child(id, 2)) : StmtPtr();
            return placed(make_owned<If>(std::move(cond), std::move(then), std::move(otherwise)), s, n.syntax);
        }
        case syntax::Kind::While: {
            if (n.count != 2) outside("a while of " + std::to_string(n.count) + " parts");
            if (syn(tree.child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
            ExprPtr cond = condition(tree.child(id, 0));
            return placed(make_owned<While>(std::move(cond), stmt(tree.child(id, 1))), s, n.syntax);
        }
        case syntax::Kind::Do: {
            if (n.count != 2) outside("a do of " + std::to_string(n.count) + " parts");
            StmtPtr body = stmt(tree.child(id, 0));
            ExprPtr cond = condition(tree.child(id, 1));
            return placed(make_owned<DoWhile>(std::move(body), std::move(cond)), s, n.syntax);
        }
        case syntax::Kind::For:
            return forStatement(id);
        case syntax::Kind::Break:
            return placed(make_owned<Break>(), s, n.syntax);
        case syntax::Kind::Continue:
            return placed(make_owned<Continue>(), s, n.syntax);
        default:
            outside(std::string("a '") + syntax::kindName(s.kind) + "' statement");
    }
    return StmtPtr();
}

// A local read or written by name, as Parser::localRef builds it.
static ExprPtr localVar(const Local &l) {
    Var *v = Var::local(l.name, l.offset);
    v->setReadOnly(l.isConst);
    v->setNoAddress(l.isRegister);
    return typed(std::unique_ptr<Var>(v), l.type);
}

// A struct the parser copies by its bytes and builds by doing nothing: no constructor, destructor, vptr or copy of its own.
bool Adapter::plainClass(Parser &p, const Type *t) {
    if (t == nullptr || !t->isStructOrUnion()) return false;
    const Type *u = t->unqualified();
    return u->isComplete() && u->kind() != Kind::Union && !p.hasConstructors(u->tag()) && !u->hasDestructor() && !u->hasVptr() &&
           p.classes_.copyAssignOf(u) == nullptr && p.classes_.copyConstructorOf(u) == nullptr &&
           p.classes_.moveConstructorOf(u) == nullptr;
}

void Adapter::plainLocal(Parser &p, const Local &l) {
    if (!l.staticName.empty()) throw std::string("a static local");
    if (l.isThreadLocal) throw std::string("a thread_local");
    if (l.constantOnly) throw std::string("a local read for its value only");
    if (!scalar(l.type) && !scalarArray(l.type) && !plainClass(p, l.type)) throw std::string("a local of type '" + l.type->describe() + "'");
}

// [stmt.dcl]: the declarators that have an initialiser, each an assignment in a block of their own.
StmtPtr Adapter::declaration(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    std::vector<StmtPtr> body;
    for (std::uint32_t i = 0; i < n.count; i++) {
        const TypedId var = tree.child(id, i);
        if (syn(var).kind != syntax::Kind::Var) outside("a declaration of what is not a var");
        std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(tree.node(var).syntax.v);
        if (l == p_.declaredLocals_.end()) outside("a var whose local was not recorded");
        try { plainLocal(p_, l->second); } catch (const std::string &why) { outside(why); }
        const TypedNode &v = tree.node(var);
        if (l->second.type->isArray()) {
            // An array's bounds are its var's first parts, so what follows the brackets says whether it is initialised.
            if (initialised(var)) outside("an array with an initialiser");
            continue;
        }
        if (v.count > 1) outside("a var of " + std::to_string(v.count) + " parts");
        if (v.count == 0) continue;
        ExprPtr init = decay(expr(tree.child(var, 0)));
        // A plain struct is initialised as `X q(p)` with no constructor is: by assignment, from one of its own type.
        if (plainClass(p_, l->second.type) && init->type()->unqualified() != l->second.type->unqualified())
            outside("a struct initialised from what is not one");
        init = classValue(std::move(init), l->second.type);
        ExprPtr store = typed(make_owned<Assign>(localVar(l->second), std::move(init)), l->second.type);
        body.push_back(make_owned<ExprStmt>(std::move(store)));
    }
    std::unique_ptr<Block> b = make_owned<Block>(std::move(body));
    b->setScope(-1);
    return placed(std::move(b), syn(id), n.syntax);
}

// Whether a declarator is followed by `=` or a brace, past its name and its array bounds.
bool Adapter::initialised(TypedId var) const {
    const std::size_t loc = syn(var).loc;
    std::size_t at = 0;
    while (at < p_.tokens_.size() && p_.tokens_[at].pos < loc) at++;
    if (at >= p_.tokens_.size() || p_.tokens_[at].pos != loc || p_.tokens_[at].kind != TokenKind::Ident)
        outside("a declarator not where it was");
    at++;
    while (at < p_.tokens_.size() && p_.tokens_[at].is("[")) {
        int depth = 0;
        for (; at < p_.tokens_.size(); at++) {
            if (p_.tokens_[at].is("[")) depth++;
            else if (p_.tokens_[at].is("]") && --depth == 0) { at++; break; }
        }
    }
    return at < p_.tokens_.size() && (p_.tokens_[at].is("=") || p_.tokens_[at].is("{") || p_.tokens_[at].is("("));
}

// A `for`'s parts are told apart by where they stand against its two semicolons, any of the first three being absent.
StmtPtr Adapter::forStatement(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const syntax::Node &s = syn(id);
    std::unordered_map<std::uint32_t, int>::const_iterator scope = p_.syntaxScopes_.find(n.syntax.v);
    if (scope == p_.syntaxScopes_.end()) outside("a for whose scope was not recorded");
    std::size_t at = 0;
    while (at < p_.tokens_.size() && p_.tokens_[at].pos < s.loc) at++;
    if (at + 1 >= p_.tokens_.size() || !p_.tokens_[at].is("for") || !p_.tokens_[at + 1].is("(")) outside("a for not where it was");
    std::size_t semi[2] = { 0, 0 }, close = 0;
    int depth = 0, found = 0;
    for (std::size_t k = at + 1; k < p_.tokens_.size(); k++) {
        const Token &t = p_.tokens_[k];
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) { if (--depth == 0) { close = t.pos; break; } }
        else if (depth == 1 && t.is(";") && found < 2) semi[found++] = t.pos;
        else if (depth == 1 && t.is(":")) outside("a range-based for");
    }
    if (found != 2) outside("a for whose semicolons were not found");
    StmtPtr init;
    ExprPtr cond, step;
    StmtPtr body;
    for (std::uint32_t i = 0; i < n.count; i++) {
        const TypedId part = tree.child(id, i);
        const std::size_t loc = syn(part).loc;
        if (loc < semi[0]) {
            if (syn(part).kind == syntax::Kind::DeclStmt) init = declaration(part);
            else { init = make_owned<ExprStmt>(expr(part)); }
        } else if (loc < semi[1]) {
            cond = condition(part);
        } else if (loc < close) {
            step = expr(part);
        } else {
            body = stmt(part);
        }
    }
    if (!body) outside("a for with no body");
    std::unique_ptr<For> f = make_owned<For>(std::move(init), std::move(cond), std::move(step), std::move(body));
    f->setScope(scope->second);
    return placed(std::move(f), s, n.syntax);
}

ExprPtr Adapter::expr(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const syntax::Node &s = syn(id);
    const std::string &text = p_.syntaxTree_.text(n.syntax);
    ExprPtr e;
    switch (s.kind) {
        case syntax::Kind::IntLit: case syntax::Kind::CharLit: case syntax::Kind::FloatLit: {
            std::unordered_map<std::uint32_t, std::size_t>::const_iterator at = p_.literalTokens_.find(n.syntax.v);
            if (at == p_.literalTokens_.end() || n.type == nullptr) outside("a literal not typed");
            const Token &t = p_.tokens_[at->second];
            if (!t.udSuffix.empty()) outside("a user-defined literal");
            e = t.isFloat ? typed(make_owned<Num>(static_cast<long double>(t.dvalue)), n.type) : typed(make_owned<Num>(t.value), n.type);
            break;
        }
        case syntax::Kind::StringLit: {
            std::unordered_map<std::uint32_t, std::pair<std::string, std::string> >::const_iterator lit =
                p_.stringLiterals_.find(n.syntax.v);
            if (lit == p_.stringLiterals_.end() || n.type == nullptr) outside("a string not typed");
            e = typed(make_owned<StrLit>(lit->second.first, lit->second.second), n.type);
            break;
        }
        case syntax::Kind::Name:
            e = name(id);
            break;
        case syntax::Kind::Unary:
            e = unary(id);
            break;
        case syntax::Kind::Postfix: {
            if (n.count != 1) outside("a postfix of " + std::to_string(n.count) + " parts");
            ExprPtr target = expr(tree.child(id, 0));
            const Type *t = target->type();
            if (!scalar(t) || t->kind() == Kind::Bool || t->isEnumeration() || !lvalueTarget(*target) ||
                (t->isPointer() && (t->pointee()->isFunction() || !t->pointee()->isComplete())))
                outside("a postfix step of what is not a scalar object");
            const long long step = t->isPointer() ? static_cast<long long>(t->pointee()->size(p_.target_)) : 1;
            e = typed(make_owned<Postfix>(std::move(target), text == "++", step), t);
            break;
        }
        case syntax::Kind::Binary:
            if (n.count != 2) outside("a binary of " + std::to_string(n.count) + " parts");
            e = binary(id, text, tree.child(id, 0), tree.child(id, 1));
            break;
        case syntax::Kind::Assign:
            e = assign(id);
            break;
        case syntax::Kind::Comma: {
            if (n.count != 2) outside("a comma of " + std::to_string(n.count) + " parts");
            ExprPtr l = expr(tree.child(id, 0)), r = expr(tree.child(id, 1));
            const Type *t = r->type();
            e = typed(make_owned<Comma>(std::move(l), std::move(r)), t);
            break;
        }
        case syntax::Kind::Conditional: {
            if (n.count != 3) outside("a conditional of " + std::to_string(n.count) + " parts");
            ExprPtr c = condition(tree.child(id, 0));
            ExprPtr x = decay(expr(tree.child(id, 1))), y = decay(expr(tree.child(id, 2)));
            const Type *tx = x->type(), *ty = y->type();
            if (!scalar(tx) || !scalar(ty)) outside("a conditional of what is not a scalar");
            if (tx != ty && tx->unqualified() == ty->unqualified() && isLvalue(*x) && isLvalue(*y))
                outside("a conditional of two lvalues apart in const");
            // [expr.cond]/4: two lvalues of one type make an lvalue, which the parser writes as `*(c ? &x : &y)`.
            if (tx == ty && isLvalue(*x) && isLvalue(*y)) {
                const Type *pt = p_.types_.pointerTo(tx);
                ExprPtr ax = typed(make_owned<Unary>('&', std::move(x)), pt), ay = typed(make_owned<Unary>('&', std::move(y)), pt);
                e = typed(make_owned<Unary>('*', typed(make_owned<Conditional>(std::move(c), std::move(ax), std::move(ay)), pt)), tx);
                break;
            }
            // [expr.cond]/6, in the parser's order: numbers, one type, one type but its cv, a null constant, two pointers.
            const Type *t = nullptr;
            if (tx->isArithmetic() && ty->isArithmetic()) {
                // Two arms of one enumeration give that enumeration, not its promotion.
                const bool oneEnum = tx->unqualified() == ty->unqualified() && tx->unqualified()->isEnumeration();
                t = oneEnum ? tx->unqualified() : p_.usualArithmetic(tx, ty);
                x = convert(std::move(x), t);
                y = convert(std::move(y), t);
            } else if (tx == ty) {
                t = tx;
            } else if (tx->unqualified() == ty->unqualified()) {
                t = tx->unqualified();
            } else if (tx->isPointer() && isNullConstant(*y)) {
                t = tx;
                y = convert(std::move(y), t);
            } else if (ty->isPointer() && isNullConstant(*x)) {
                t = ty;
                x = convert(std::move(x), t);
            } else if (tx->isPointer() && ty->isPointer() &&
                       !(tx->pointee()->unqualified()->isStructOrUnion() && ty->pointee()->unqualified()->isStructOrUnion() &&
                         tx->pointee()->unqualified() != ty->pointee()->unqualified())) {
                t = p_.compositePointer(tx, ty);
                if (t == nullptr) outside("a conditional of two pointers with no composite type");
                x = convert(std::move(x), t);
                y = convert(std::move(y), t);
            } else {
                outside("a conditional of '" + tx->describe() + "' and '" + ty->describe() + "'");
            }
            e = typed(make_owned<Conditional>(std::move(c), std::move(x), std::move(y)), t);
            break;
        }
        case syntax::Kind::Call:
            e = call(id);
            break;
        case syntax::Kind::Member:
            e = member(id);
            break;
        case syntax::Kind::Subscript: {
            // [expr.sub]: `a[i]` is `*(a + i)`, the sum the pointer arithmetic below builds, either way round.
            if (n.count != 2) outside("a subscript of " + std::to_string(n.count) + " parts");
            ExprPtr sum = arithmetic("+", expr(tree.child(id, 0)), expr(tree.child(id, 1)));
            if (!sum->type()->isPointer()) outside("a subscript of what is not a pointer");
            const Type *elem = sum->type()->pointee();
            e = typed(make_owned<Unary>('*', std::move(sum)), elem);
            break;
        }
        case syntax::Kind::Cast: {
            // [expr.cast]: the operand decayed, then to void as it stands, else the conversion a cast allows.
            std::unordered_map<std::uint32_t, const Type *>::const_iterator to = p_.castTypes_.find(n.syntax.v);
            if (to == p_.castTypes_.end() || n.count != 1) outside("a cast not recorded");
            ExprPtr v = decay(expr(tree.child(id, 0)));
            if (to->second->isVoid()) { e = typed(make_owned<Cast>(to->second, std::move(v)), to->second); break; }
            if (!scalar(to->second)) outside("a cast to '" + to->second->describe() + "'");
            if (!scalar(v->type())) outside("a cast of what is not a scalar");
            e = convert(std::move(v), to->second, true);
            break;
        }
        case syntax::Kind::Sizeof: {
            std::unordered_map<std::uint32_t, std::pair<long long, const Type *> >::const_iterator k =
                p_.keywordValues_.find(n.syntax.v);
            if (k == p_.keywordValues_.end()) outside("a sizeof not folded");
            e = typed(make_owned<Num>(k->second.first), k->second.second);
            break;
        }
        default:
            outside(std::string("a '") + syntax::kindName(s.kind) + "' expression");
    }
    if (n.type != nullptr && e->type() != n.type && e->type()->unqualified() != n.type->unqualified())
        outside(std::string("a '") + syntax::kindName(s.kind) + "' the typer and the adapter type apart");
    return e;
}

// A struct of one type given to another differing only in const: [conv.qual] leaves the object as it is.
ExprPtr Adapter::classValue(ExprPtr e, const Type *to) {
    if (!to->isStructOrUnion()) return convert(std::move(e), to);
    if (e->type() != to) e->setType(to);
    return e;
}

// [expr.ref]: `o.m` and `p->m` of a plain struct's data member, the object's const carried onto the member.
ExprPtr Adapter::member(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const std::string &text = p_.syntaxTree_.text(n.syntax);
    if (n.count != 1) outside("a member of " + std::to_string(n.count) + " parts");
    const bool arrow = text.compare(0, 2, "->") == 0;
    const std::string name = text.substr(arrow ? 2 : 1);
    if (name.empty() || name.find_first_of(":~<") != std::string::npos || name.compare(0, 8, "template") == 0)
        outside("a member named '" + name + "'");
    ExprPtr object = expr(tree.child(id, 0));
    if (arrow) {
        const Type *pt = object->type();
        if (!pt->isPointer() || !pt->pointee()->isStructOrUnion()) outside("a '->' of what is not a pointer to a struct");
        const Type *pointee = pt->pointee();
        object = typed(make_owned<Unary>('*', std::move(object)), pointee);
    }
    const Type *obj = object->type();
    if (!plainClass(p_, obj)) outside("a member of '" + obj->describe() + "'");
    if (!arrow && (object->isXvalue() || !isLvalue(*object))) outside("a member of what is not an lvalue");
    if (p_.lookup_.findMemberOwner(obj->unqualified(), name) != nullptr || obj->findStaticMember(name) != nullptr)
        outside("a member '" + name + "' that is not a data member");
    const Member *m = obj->findMember(name);
    if (m == nullptr || m->ambiguous || m->inVirtualBase != nullptr || m->anonUnion != nullptr || m->type->isReference())
        outside("a member '" + name + "' the adapter does not reach");
    const Type *t = obj->isConst() && !m->isMutable ? p_.types_.withConst(m->type) : m->type;
    if (!scalar(t) && !scalarArray(t) && !plainClass(p_, t)) outside("a member of type '" + t->describe() + "'");
    return typed(make_owned<MemberAccess>(std::move(object), name, m->offset, m->width, m->bitOffset), t);
}

// A name, as the typer found it: a local of this function, else an object or an enumerator at file scope.
ExprPtr Adapter::name(TypedId id) {
    const TypedNode &n = typer_.tree().node(id);
    const std::string &text = p_.syntaxTree_.text(n.syntax);
    if (n.decl.valid()) {
        std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(n.decl.v);
        if (l == p_.declaredLocals_.end()) outside("a name whose local was not recorded");
        try { plainLocal(p_, l->second); } catch (const std::string &why) { outside(why); }
        return localVar(l->second);
    }
    if (const GlobalSym *g = p_.lookup_.findGlobal(text)) {
        if (g->isThreadLocal) outside("a thread_local");
        if (!scalar(g->type) && !scalarArray(g->type) && !plainClass(p_, g->type)) outside("an object of type '" + g->type->describe() + "'");
        Var *v = Var::global(text);
        v->setSymbol(g->symbol);
        v->setReadOnly(g->isConst);
        return typed(std::unique_ptr<Var>(v), g->type);
    }
    if (const EnumConst *k = p_.lookup_.findEnum(text)) {
        const Type *t = k->type != nullptr ? k->type : p_.types_.intType();
        if (!scalarArithmetic(t)) outside("an enumerator of '" + t->describe() + "'");
        return typed(make_owned<Num>(k->value), t);
    }
    // [conv.func]: a function named as a value is its address.
    if (const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(text)) {
        if (set->size() != 1) outside("a function of an overload set named as a value");
        const Signature &f = p_.lookup_.functions_[(*set)[0]];
        if (f.fromTemplate || !f.owner.empty() || f.deleted) outside("a template's or a member named as a value");
        Var *v = Var::global(text);
        v->setSymbol(f.symbol);
        const Type *fn = p_.types_.functionType(f.returns, f.params, f.variadic);
        ExprPtr target = typed(std::unique_ptr<Var>(v), fn);
        return typed(make_owned<Unary>('&', std::move(target)), p_.types_.pointerTo(fn));
    }
    outside("the name '" + text + "'");
    return ExprPtr();
}

ExprPtr Adapter::unary(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const std::string &op = p_.syntaxTree_.text(n.syntax);
    if (n.count != 1) outside("a unary of " + std::to_string(n.count) + " parts");
    ExprPtr a = expr(tree.child(id, 0));
    if (op == "&") {
        // [expr.unary.op]/3 with [conv.func]: `&f` and `f` are one address.
        if (const Unary *fn = dynamic_cast<const Unary *>(a.get()))
            if (fn->op() == '&' && a->type()->isPointer() && a->type()->pointee()->isFunction()) return a;
        // [expr.unary.op]/3: the address of an object, never folded with a `*` under it.
        const MemberAccess *field = dynamic_cast<const MemberAccess *>(a.get());
        if (a->type()->isFunction() || (field != nullptr && field->width() != 0) ||
            !(lvalueTarget(*a) || (a->type()->isArray() && (dynamic_cast<const Var *>(a.get()) || field))))
            outside("an address of what is not an object");
        if (const Var *v = dynamic_cast<const Var *>(a.get()))
            if (v->noAddress()) outside("an address of a register object");
        const Type *pt = p_.types_.pointerTo(a->type());
        return typed(make_owned<Unary>('&', std::move(a)), pt);
    }
    if (op == "++" || op == "--") {
        const Type *t = a->type();
        if (!scalar(t) || t->kind() == Kind::Bool || t->isEnumeration() || !lvalueTarget(*a) || !pure(*a))
            outside("a prefix step of what is not a scalar object");
        ExprPtr again = expr(tree.child(id, 0));
        ExprPtr sum = arithmetic(op == "++" ? "+" : "-", std::move(again), typed(make_owned<Num>(1LL), p_.types_.intType()));
        return typed(make_owned<Assign>(std::move(a), convert(std::move(sum), t)), t);
    }
    a = decay(std::move(a));
    const Type *t = a->type();
    if (op == "*") {
        // [expr.unary.op]/1: what a pointer to an object points at.
        if (!scalar(t) || !t->isPointer() || t->pointee()->isVoid() || t->pointee()->isFunction())
            outside("a '*' of what is not an object pointer");
        const Type *elem = t->pointee();
        return typed(make_owned<Unary>('*', std::move(a)), elem);
    }
    if (op == "!") {
        if (!scalar(t)) outside("a '!' of what is not a scalar");
        return typed(make_owned<Unary>('!', std::move(a)), p_.types_.get(Kind::Bool));
    }
    if (!scalarArithmetic(t)) outside("a unary '" + op + "' of what is not a number");
    if (op == "+") return promote(std::move(a));
    if (op == "-") {
        a = promote(std::move(a));
        const Type *pt = a->type();
        return typed(make_owned<Unary>('-', std::move(a)), pt);
    }
    if (op == "~") {
        if (!t->isInteger()) outside("'~' of what is not an integer");
        a = promote(std::move(a));
        const Type *pt = a->type();
        return typed(make_owned<Binary>(BinOp::BitXor, std::move(a), typed(make_owned<Num>(-1LL), pt)), pt);
    }
    outside("a unary '" + op + "'");
    return ExprPtr();
}

// A condition: decayed, a scalar, and not converted - `if (p)` tests the pointer as it stands.
ExprPtr Adapter::condition(TypedId id) {
    if (syn(id).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
    ExprPtr c = decay(expr(id));
    if (!scalar(c->type())) outside("a condition that is not a scalar");
    return c;
}

static BinOp binOpOf(const std::string &op) {
    static const struct { const char *text; BinOp op; } kOps[] = {
        { "+", BinOp::Add }, { "-", BinOp::Sub }, { "*", BinOp::Mul }, { "/", BinOp::Div }, { "%", BinOp::Mod },
        { "<<", BinOp::Shl }, { ">>", BinOp::Shr }, { "&", BinOp::BitAnd }, { "|", BinOp::BitOr },
        { "^", BinOp::BitXor }, { "==", BinOp::Eq }, { "!=", BinOp::Ne }, { "<", BinOp::Lt }, { "<=", BinOp::Le },
        { ">", BinOp::Gt }, { ">=", BinOp::Ge }, { "&&", BinOp::LAnd }, { "||", BinOp::LOr },
    };
    for (const auto &k : kOps)
        if (op == k.text) return k.op;
    throw std::string("the operator '" + op + "'");
}

// [expr.add]/4: a pointer and an integer, the integer scaled to bytes in the pointer-width integer.
ExprPtr Adapter::pointerAdd(ExprPtr ptr, ExprPtr n) {
    const Type *pt = ptr->type(), *pd = p_.ptrdiffType();
    if (pt->pointee()->isFunction() || !pt->pointee()->isComplete() || pt->pointee()->isVoid()) outside("arithmetic on a pointer to an incomplete type");
    ExprPtr size = typed(make_owned<Num>(static_cast<long long>(pt->pointee()->size(p_.target_))), pd);
    ExprPtr scaled = typed(make_owned<Binary>(BinOp::Mul, convert(std::move(n), pd), std::move(size)), pd);
    return typed(make_owned<Binary>(BinOp::Add, std::move(ptr), std::move(scaled)), pt);
}

// [expr.add]/6: two pointers, their difference in bytes divided by the element's size.
ExprPtr Adapter::pointerSub(ExprPtr l, ExprPtr r) {
    const Type *pd = p_.ptrdiffType();
    if (l->type()->pointee()->unqualified() != r->type()->pointee()->unqualified() || !l->type()->pointee()->isComplete())
        outside("a difference of two pointers apart in type");
    const long long stride = static_cast<long long>(l->type()->pointee()->size(p_.target_));
    ExprPtr diff = typed(make_owned<Binary>(BinOp::Sub, std::move(l), std::move(r)), pd);
    return typed(make_owned<Binary>(BinOp::Div, std::move(diff), typed(make_owned<Num>(stride), pd)), pd);
}

// The parser's arithmetic, comparison and logical operators: the operands decayed, [expr.arith.conv] for two
// numbers, [expr.shift] promoting each alone, a pointer scaled, and a pointer compared as it stands.
ExprPtr Adapter::arithmetic(const std::string &op, ExprPtr a, ExprPtr b) {
    BinOp o = BinOp::Add;
    try { o = binOpOf(op); } catch (const std::string &why) { outside(why); }
    a = decay(std::move(a));
    b = decay(std::move(b));
    const Type *ta = a->type(), *tb = b->type();
    if (!scalar(ta) || !scalar(tb)) outside("'" + op + "' of what is not a scalar");
    if (o == BinOp::LAnd || o == BinOp::LOr)
        return typed(make_owned<Binary>(o, std::move(a), std::move(b)), p_.types_.get(Kind::Bool));
    const bool compare = o == BinOp::Eq || o == BinOp::Ne || o == BinOp::Lt || o == BinOp::Le || o == BinOp::Gt ||
                         o == BinOp::Ge;
    if (compare && (ta->isPointer() || tb->isPointer())) {
        if ((!ta->isPointer() && !isNullConstant(*a)) || (!tb->isPointer() && !isNullConstant(*b)))
            outside("a pointer compared with a number");
        return typed(make_owned<Binary>(o, std::move(a), std::move(b)), p_.types_.get(Kind::Bool));
    }
    if (o == BinOp::Add && ta->isPointer() && tb->isInteger()) return pointerAdd(std::move(a), std::move(b));
    if (o == BinOp::Add && ta->isInteger() && tb->isPointer()) return pointerAdd(std::move(b), std::move(a));
    if (o == BinOp::Sub && ta->isPointer() && tb->isInteger()) {
        const Type *lt = p_.promote(tb);
        ExprPtr neg = typed(make_owned<Unary>('-', convert(std::move(b), lt)), lt);
        return pointerAdd(std::move(a), std::move(neg));
    }
    if (o == BinOp::Sub && ta->isPointer() && tb->isPointer()) return pointerSub(std::move(a), std::move(b));
    if (!scalarArithmetic(ta) || !scalarArithmetic(tb)) outside("'" + op + "' of a pointer");
    if ((o == BinOp::Mod || o == BinOp::Shl || o == BinOp::Shr || o == BinOp::BitAnd || o == BinOp::BitOr ||
         o == BinOp::BitXor) && (!ta->isInteger() || !tb->isInteger()))
        outside("'" + op + "' of what is not an integer");
    if (o == BinOp::Shl || o == BinOp::Shr) {
        a = promote(std::move(a));
        b = promote(std::move(b));
        const Type *t = a->type();
        return typed(make_owned<Binary>(o, std::move(a), std::move(b)), t);
    }
    const Type *t = p_.usualArithmetic(ta, tb);
    a = convert(std::move(a), t);
    b = convert(std::move(b), t);
    return typed(make_owned<Binary>(o, std::move(a), std::move(b)), compare ? p_.types_.get(Kind::Bool) : t);
}

ExprPtr Adapter::binary(TypedId, const std::string &op, TypedId l, TypedId r) {
    return arithmetic(op, expr(l), expr(r));
}

// `a = b` converts b to a's type; `a op= b` is `a = a op b`, a read twice - the parser's lowering, for a target it
// can read twice.
ExprPtr Adapter::assign(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const std::string &op = p_.syntaxTree_.text(n.syntax);
    if (n.count != 2) outside("an assignment of " + std::to_string(n.count) + " parts");
    ExprPtr target = expr(tree.child(id, 0));
    const Type *t = target->type();
    if (!(scalar(t) || (op == "=" && plainClass(p_, t))) || !lvalueTarget(*target))
        outside("an assignment to what is not a scalar object");
    ExprPtr value = expr(tree.child(id, 1));
    if (t->isStructOrUnion()) {
        if (value->type()->unqualified() != t->unqualified()) outside("a struct assigned what is not one");
        return typed(make_owned<Assign>(std::move(target), classValue(std::move(value), t)), t);
    }
    if (op != "=") {
        if (t->kind() == Kind::Bool || t->isEnumeration()) outside("a compound assignment to a bool or an enumeration");
        if (!pure(*target)) outside("a compound assignment to what cannot be read twice");
        value = arithmetic(op.substr(0, op.size() - 1), expr(tree.child(id, 0)), std::move(value));
    } else {
        value = decay(std::move(value));
    }
    return typed(make_owned<Assign>(std::move(target), convert(std::move(value), t)), t);
}

// A call of a name with one function under it, the arguments converted to its parameters and an extra one promoted.
ExprPtr Adapter::call(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    if (n.count == 0 || syn(tree.child(id, 0)).kind != syntax::Kind::Name) outside("a call of what is not a name");
    const std::string &callee = p_.syntaxTree_.text(tree.node(tree.child(id, 0)).syntax);
    if (tree.node(tree.child(id, 0)).decl.valid()) outside("a call through a local");
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(callee);
    if (set == nullptr || set->size() != 1) outside("a call of an overload set");
    const Signature f = p_.lookup_.functions_[(*set)[0]];
    if (f.fromTemplate || !f.owner.empty() || f.deleted) outside("a call of a template's or a member");
    if (f.returns == nullptr || !(f.returns->isVoid() || scalar(f.returns))) outside("a call returning a class");
    const std::size_t given = n.count - 1;
    if (given < f.params.size() || (given > f.params.size() && !f.variadic)) outside("a call with defaults or too many");
    std::vector<ExprPtr> args;
    for (std::size_t i = 0; i < given; i++) {
        ExprPtr a = decay(expr(tree.child(id, static_cast<std::uint32_t>(i + 1))));
        if (i < f.params.size()) {
            const Type *p = f.params[i];
            if (!scalar(p)) outside("a parameter of type '" + p->describe() + "'");
            args.push_back(convert(std::move(a), p));
        } else {
            const Type *t = a->type();
            if (t->isPointer()) { args.push_back(std::move(a)); continue; }
            if (!scalarArithmetic(t)) outside("a variadic argument of type '" + t->describe() + "'");
            if (t->kind() == Kind::Float) args.push_back(convert(std::move(a), p_.types_.get(Kind::Double)));
            else args.push_back(promote(std::move(a)));
        }
    }
    std::unique_ptr<Call> c = make_owned<Call>(callee, ExprPtr(), std::move(args), f.variadic, 0, static_cast<int>(f.params.size()));
    c->setSymbol(f.symbol);
    return typed(std::move(c), f.returns);
}

// [conv]: a scalar to another - a Cast, but to bool a comparison with zero, as Parser::convertNode builds them.
ExprPtr Adapter::convert(ExprPtr e, const Type *to, bool cast) {
    const Type *from = e->type();
    if (from == to) return e;
    if (!scalar(from) || !scalar(to)) outside("a conversion of '" + from->describe() + "' to '" + to->describe() + "'");
    if (to->isBool() && !from->isBool()) {
        ExprPtr zero;
        if (from->isFloating()) zero = typed(make_owned<Num>(static_cast<long double>(0)), from);
        else zero = convert(typed(make_owned<Num>(0LL), p_.types_.intType()), from);
        return typed(make_owned<Binary>(BinOp::Ne, std::move(e), std::move(zero)), to);
    }
    if (!cast && from->isPointer() != to->isPointer() && !(to->isPointer() && isNullConstant(*e)))
        outside("a conversion between a pointer and a number");
    if (from->isPointer() && to->isPointer() && from->pointee()->unqualified()->isStructOrUnion() &&
        to->pointee()->unqualified()->isStructOrUnion() && from->pointee()->unqualified() != to->pointee()->unqualified())
        outside("a conversion of a pointer to a class to one to another class");
    return typed(make_owned<Cast>(to, std::move(e)), to);
}

ExprPtr Adapter::promote(ExprPtr e) {
    const Type *t = p_.promote(e->type());
    if (t == e->type()) return e;
    return typed(make_owned<Cast>(t, std::move(e)), t);
}

// An array is its first element's address, written as a cast to the pointer.
ExprPtr Adapter::decay(ExprPtr e) {
    const Type *t = e->type();
    if (t->isFunction()) outside("a function named as a value");
    if (!t->isArray()) return e;
    const Type *to = p_.types_.pointerTo(t->pointee());
    return typed(make_owned<Cast>(to, std::move(e)), to);
}

std::string Adapter::run(Parser &p, Program &program, bool replace, int *differ) {
    std::map<std::string, std::size_t> bySymbol;
    for (std::size_t i = 0; i < program.functions.size(); i++) bySymbol[program.functions[i].symbol()] = i;
    Typer typer(p);
    const syntax::Tree &st = p.syntaxTree_;
    const std::vector<syntax::NodeId> roots = st.roots();
    std::map<std::string, int> reasons;
    std::string out;
    int considered = 0, adapted = 0, same = 0;
    *differ = 0;
    for (std::size_t r = 0; r < roots.size(); r++) {
        if (st.node(roots[r]).kind != syntax::Kind::Function || p.scopedFunctions_.count(roots[r].v) != 0) continue;
        std::unordered_map<std::uint32_t, std::string>::const_iterator sym = p.syntaxFunctions_.find(roots[r].v);
        if (sym == p.syntaxFunctions_.end()) continue;
        std::map<std::string, std::size_t>::const_iterator at = bySymbol.find(sym->second);
        if (at == bySymbol.end()) continue;
        Function &fn = program.functions[at->second];
        considered++;
        const TypedId t = typer.typeFunction(roots[r]);
        Adapter adapter(p, typer);
        std::string why;
        StmtPtr b = adapter.body(t, fn, &why);
        if (!b) { reasons[why]++; continue; }
        adapted++;
        if (replace) { fn.setBody(std::move(b)); continue; }
        std::string theirs, ours;
        dumpStmt(fn.body(), theirs);
        dumpStmt(*b, ours);
        if (theirs == ours) { same++; continue; }
        (*differ)++;
        std::size_t a = 0, line = 1;
        while (a < theirs.size() && a < ours.size() && theirs[a] == ours[a]) { if (theirs[a] == '\n') line++; a++; }
        const std::size_t ta = theirs.rfind('\n', a == 0 ? 0 : a - 1), oa = ours.rfind('\n', a == 0 ? 0 : a - 1);
        const std::size_t ts = ta == std::string::npos ? 0 : ta + 1, os = oa == std::string::npos ? 0 : oa + 1;
        out += fn.symbol() + ": line " + std::to_string(line) + "\n  parser:  " +
               theirs.substr(ts, theirs.find('\n', ts) - ts) + "\n  adapter: " + ours.substr(os, ours.find('\n', os) - os) + "\n";
    }
    if (!replace) {
        for (std::map<std::string, int>::const_iterator i = reasons.begin(); i != reasons.end(); ++i)
            out += "  outside (" + std::to_string(i->second) + "): " + i->first + "\n";
        char line[200];
        std::snprintf(line, sizeof line, "-check=adapted: %d functions, %d adapted, %d the same, %d differ, %d outside\n",
                      considered, adapted, same, *differ, considered - adapted);
        out += line;
    }
    return out;
}

}   // namespace sema
