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
    fnSymbol_ = fn.symbol();
    std::unordered_map<std::uint32_t, const Type *>::const_iterator declared =
        p_.declaredReturns_.find(typer_.tree().node(typed).syntax.v);
    if (declared != p_.declaredReturns_.end() && declared->second != nullptr) returns_ = declared->second;
    try {
        const TypedNode &f = typer_.tree().node(typed);
        if (f.count == 0) outside("a function with no body");
        if (fn.isNoexcept() || fn.hasLandingPads()) outside("a function whose body is a region");
        // An array parameter's bound is recorded beside it, and [dcl.fct]/5 has adjusted it away.
        for (std::uint32_t i = 0; i + 1 < f.count; i++)
            if (syn(typer_.tree().child(typed, i)).kind == syntax::Kind::Block) outside("a parameter that is a block");
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
            if (n.count == 1 && returns_ != nullptr && returns_->isReference())
                return placed(make_owned<Return>(bind(returns_, expr(tree.child(id, 0)))), s, n.syntax);
            if (n.count == 1 && returns_ != nullptr && plainClass(p_, returns_)) {
                ExprPtr value = decay(expr(tree.child(id, 0)));
                if (value->type()->unqualified() != returns_->unqualified()) outside("a struct returned from what is not one");
                return placed(make_owned<Return>(classValue(std::move(value), returns_)), s, n.syntax);
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
static ExprPtr localVar(const Local &l, TypeTable &types) {
    Var *v = Var::local(l.name, l.offset);
    v->setReadOnly(l.isConst);
    v->setNoAddress(l.isRegister);
    if (!l.type->isReference()) return typed(std::unique_ptr<Var>(v), l.type);
    // A reference's slot holds an address, and every mention reads through it - Parser::useReference.
    const Type *referent = l.type->referent();
    ExprPtr slot = typed(std::unique_ptr<Var>(v), types.pointerTo(referent));
    return typed(make_owned<Unary>('*', std::move(slot)), referent);
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
    if (l.type->isReference()) {
        const Type *r = l.type->referent();
        if (!scalar(r) && !plainClass(p, r)) throw std::string("a reference to '" + r->describe() + "'");
        return;
    }
    if (!scalar(l.type) && !scalarArray(l.type) && !plainClass(p, l.type)) throw std::string("a local of type '" + l.type->describe() + "'");
}

// [stmt.dcl]: the declarators that have an initialiser, each an assignment in a block of their own.
StmtPtr Adapter::declaration(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    std::vector<StmtPtr> body;
    for (std::uint32_t i = 0; i < n.count; i++) {
        const TypedId var = tree.child(id, i);
        // What else a declaration records is unevaluated - an alignas, a decltype, a static_assert - and builds nothing.
        if (syn(var).kind != syntax::Kind::Var) continue;
        std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(tree.node(var).syntax.v);
        if (l == p_.declaredLocals_.end()) outside("a var whose local was not recorded");
        try { plainLocal(p_, l->second); } catch (const std::string &why) { outside(why); }
        const TypedNode &v = tree.node(var);
        if (l->second.type->isReference()) {
            if (v.count != 1) outside("a reference of " + std::to_string(v.count) + " parts");
            const Type *slotType = p_.types_.pointerTo(l->second.type->referent());
            Var *slot = Var::local(l->second.name, l->second.offset);
            slot->setReadOnly(l->second.isConst);
            ExprPtr store = typed(make_owned<Assign>(typed(std::unique_ptr<Var>(slot), slotType),
                                                     bind(l->second.type, expr(tree.child(var, 0)))), slotType);
            body.push_back(make_owned<ExprStmt>(std::move(store)));
            continue;
        }
        if (l->second.type->isArray()) {
            arrayInitialiser(var, l->second, body);
            continue;
        }
        if (v.count > 1) outside("a var of " + std::to_string(v.count) + " parts");
        if (v.count == 0) continue;
        ExprPtr init = decay(expr(tree.child(var, 0)));
        // A plain struct is initialised as `X q(p)` with no constructor is: by assignment, from one of its own type.
        if (plainClass(p_, l->second.type) && init->type()->unqualified() != l->second.type->unqualified())
            outside("a struct initialised from what is not one");
        init = classValue(std::move(init), l->second.type);
        ExprPtr store = typed(make_owned<Assign>(localVar(l->second, p_.types_), std::move(init)), l->second.type);
        body.push_back(make_owned<ExprStmt>(std::move(store)));
    }
    std::unique_ptr<Block> b = make_owned<Block>(std::move(body));
    b->setScope(-1);
    return placed(std::move(b), syn(id), n.syntax);
}

// The token after a declarator's name and its array bounds; *bounds counts the bounds written, which are the var's
// first parts.
std::size_t Adapter::declaratorEnd(TypedId var, int *bounds) const {
    const std::size_t loc = syn(var).loc;
    std::size_t at = 0;
    while (at < p_.tokens_.size() && p_.tokens_[at].pos < loc) at++;
    if (at >= p_.tokens_.size() || p_.tokens_[at].pos != loc || p_.tokens_[at].kind != TokenKind::Ident)
        outside("a declarator not where it was");
    at++;
    *bounds = 0;
    while (at < p_.tokens_.size() && p_.tokens_[at].is("[")) {
        if (at + 1 < p_.tokens_.size() && !p_.tokens_[at + 1].is("]")) ++*bounds;
        int depth = 0;
        for (; at < p_.tokens_.size(); at++) {
            if (p_.tokens_[at].is("[")) depth++;
            else if (p_.tokens_[at].is("]") && --depth == 0) { at++; break; }
        }
    }
    return at;
}

// Parser::emitInit for an array of scalars: one store per element through `*(a + i)`, a list read in order into
// the leaves (so brace elision is the parser's), what it leaves zeroed, a string spelled out byte by byte. The
// syntax tree keeps no braces inside a list, so a list holding one stays the parser's.
void Adapter::arrayInitialiser(TypedId var, const Local &l, std::vector<StmtPtr> &body) {
    const TypedTree &tree = typer_.tree();
    int bounds = 0;
    std::size_t at = declaratorEnd(var, &bounds);
    const std::vector<Token> &tk = p_.tokens_;
    if (at >= tk.size() || !(tk[at].is("=") || tk[at].is("{"))) {
        if (at < tk.size() && tk[at].is("(")) outside("an array with an initialiser in parentheses");
        return;
    }
    if (tk[at].is("=")) at++;
    std::vector<TypedId> items;
    for (std::uint32_t i = static_cast<std::uint32_t>(bounds); i < tree.node(var).count; i++) items.push_back(tree.child(var, i));
    if (at < tk.size() && tk[at].is("{")) {
        int depth = 0;
        for (std::size_t k = at; k < tk.size(); k++) {
            if (tk[k].is("...")) outside("an array's list expanding a pack");
            if (tk[k].is("{")) { if (++depth > 1) outside("an array's list holding braces"); }
            else if (tk[k].is("}") && --depth == 0) break;
        }
    }
    const Type *leaf = l.type;
    std::vector<long long> dims;
    while (leaf->isArray()) { dims.push_back(leaf->length()); leaf = leaf->pointee(); }
    if (!scalar(leaf)) outside("an array of '" + leaf->describe() + "' with an initialiser");
    long long leaves = 1;
    for (std::size_t d = 0; d < dims.size(); d++) leaves *= dims[d];
    // The element at a flat index: `*(a + i)` for each dimension in turn, as targetFor builds it.
    struct Target {
        Adapter &a; const Local &l; const std::vector<long long> &dims;
        ExprPtr operator()(long long flat) {
            ExprPtr e = localVar(l, a.p_.types_);
            long long stride = 1;
            for (std::size_t d = 1; d < dims.size(); d++) stride *= dims[d];
            for (std::size_t d = 0; d < dims.size(); d++) {
                const Type *elem = e->type()->pointee();
                ExprPtr sum = a.pointerAdd(a.decay(std::move(e)), typed(make_owned<Num>(flat / stride), a.p_.types_.intType()));
                e = typed(make_owned<Unary>('*', std::move(sum)), elem);
                flat %= stride;
                if (d + 1 < dims.size()) stride /= dims[d + 1];
            }
            return e;
        }
    } target = { *this, l, dims };
    std::vector<ExprPtr> values;
    const Kind ek = leaf->unqualified()->kind();
    const bool narrow = ek == Kind::Char || ek == Kind::SChar || ek == Kind::UChar;
    if (items.size() == 1 && syn(items[0]).kind == syntax::Kind::StringLit && !leaf->isPointer()) {
        if (!narrow || dims.size() != 1) outside("a string into an array of '" + leaf->describe() + "'");
        const Type *st = tree.node(items[0]).type;
        if (st == nullptr || !st->isArray() || st->pointee()->unqualified()->kind() != Kind::Char) outside("a string not narrow");
        std::unordered_map<std::uint32_t, std::pair<std::string, std::string> >::const_iterator lit =
            p_.stringLiterals_.find(tree.node(items[0]).syntax.v);
        if (lit == p_.stringLiterals_.end()) outside("a string not recorded");
        const std::string &text = lit->second.second;
        for (long long i = 0; i < leaves; i++) {
            const long long ch = i < static_cast<long long>(text.size())
                                     ? static_cast<long long>(static_cast<unsigned char>(text[static_cast<std::size_t>(i)])) : 0;
            values.push_back(typed(make_owned<Num>(ch), p_.types_.intType()));
        }
    } else {
        if (static_cast<long long>(items.size()) > leaves) outside("an array's list longer than it");
        for (std::size_t i = 0; i < items.size(); i++) {
            if (syn(items[i]).kind == syntax::Kind::StringLit && !leaf->isPointer()) outside("a string in an array's list");
            values.push_back(decay(expr(items[i])));
        }
        for (long long i = static_cast<long long>(items.size()); i < leaves; i++) {
            const Type *z = leaf->isFloating() ? p_.types_.doubleType()
                          : leaf->unqualified()->isEnumeration() ? leaf->unqualified() : p_.types_.intType();
            values.push_back(leaf->isFloating() ? typed(make_owned<Num>(static_cast<long double>(0)), z)
                                                : typed(make_owned<Num>(0LL), z));
        }
    }
    for (long long i = 0; i < leaves; i++) {
        ExprPtr to = target(i);
        const Type *t = to->type();
        ExprPtr store = typed(make_owned<Assign>(std::move(to), convert(std::move(values[static_cast<std::size_t>(i)]), t)), t);
        body.push_back(make_owned<ExprStmt>(std::move(store)));
    }
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
    // [expr.ref]/4: a member of an rvalue object - `make().x` - is an xvalue.
    const bool rvalue = object->isXvalue() || !isLvalue(*object);
    if (p_.lookup_.findMemberOwner(obj->unqualified(), name) != nullptr || obj->findStaticMember(name) != nullptr)
        outside("a member '" + name + "' that is not a data member");
    const Member *m = obj->findMember(name);
    if (m == nullptr || m->ambiguous || m->inVirtualBase != nullptr || m->anonUnion != nullptr || m->type->isReference())
        outside("a member '" + name + "' the adapter does not reach");
    const Type *t = obj->isConst() && !m->isMutable ? p_.types_.withConst(m->type) : m->type;
    if (!scalar(t) && !scalarArray(t) && !plainClass(p_, t)) outside("a member of type '" + t->describe() + "'");
    ExprPtr access = typed(make_owned<MemberAccess>(std::move(object), name, m->offset, m->width, m->bitOffset), t);
    if (rvalue) access->setXvalue();
    return access;
}

// Whether a function, an object or an enumerator is declared under `name` inside some namespace or class: then an
// unqualified use of it in a function inside one may mean that one, and the parser's tables cannot say which.
bool Adapter::scopedName(const std::string &name) {
    if (!scopedBuilt_) {
        scopedBuilt_ = true;
        for (std::size_t i = 0; i < p_.lookup_.functions_.size(); i++) {
            const Signature &f = p_.lookup_.functions_[i];
            if (f.owner.empty() && f.name.find("::") == std::string::npos) globalFunctions_.insert(f.symbol);
        }
        struct Add {
            static void from(std::unordered_set<std::string> &out, const std::string &key) {
                const std::size_t at = key.rfind("::");
                if (at != std::string::npos) out.insert(key.substr(at + 2));
            }
        };
        for (std::unordered_map<std::string, std::vector<std::size_t> >::const_iterator i = p_.lookup_.functionIndex_.begin();
             i != p_.lookup_.functionIndex_.end(); ++i) Add::from(scoped_, i->first);
        for (std::unordered_map<std::string, std::size_t>::const_iterator i = p_.lookup_.globalIndex_.begin();
             i != p_.lookup_.globalIndex_.end(); ++i) Add::from(scoped_, i->first);
        for (std::unordered_map<std::string, std::size_t>::const_iterator i = p_.lookup_.enumIndex_.begin();
             i != p_.lookup_.enumIndex_.end(); ++i) Add::from(scoped_, i->first);
    }
    // A function at file scope with no using-directive open finds a name where the parser's tables, read now, do.
    if (globalFunctions_.count(fnSymbol_) != 0 && p_.lookup_.usingNamespaces_.empty()) return false;
    return scoped_.count(name) != 0;
}

// A name, as the typer found it: a local of this function, else an object or an enumerator at file scope.
ExprPtr Adapter::name(TypedId id) {
    const TypedNode &n = typer_.tree().node(id);
    const std::string &text = p_.syntaxTree_.text(n.syntax);
    if (n.decl.valid()) {
        std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(n.decl.v);
        if (l == p_.declaredLocals_.end()) outside("a name whose local was not recorded");
        try { plainLocal(p_, l->second); } catch (const std::string &why) { outside(why); }
        return localVar(l->second, p_.types_);
    }
    if (text.find("::") == std::string::npos && scopedName(text)) outside("a name that names something in a scope too");
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

// Whether a type names a class or an enumeration declared in a namespace, whose functions argument-dependent lookup
// would add to a call's candidates - which the adapter does not do, so such a call stays the parser's.
static bool namespaceAssociated(const Type *t) {
    while (t->isReference() || t->isPointer() || t->isArray()) t = t->isReference() ? t->referent() : t->pointee();
    t = t->unqualified();
    return (t->isStructOrUnion() || t->isEnumeration()) && t->tag().find("::") != std::string::npos;
}

// A call: of a name with one function under it or an overload set Sema resolves, of a static member by its
// qualified name, or of a member function of a plain struct through `.` or `->`.
ExprPtr Adapter::call(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    if (n.count == 0) outside("a call of nothing");
    const TypedId head = tree.child(id, 0);
    std::vector<ExprPtr> args;
    for (std::uint32_t i = 1; i < n.count; i++) args.push_back(expr(tree.child(id, i)));
    for (std::size_t i = 0; i < args.size(); i++)
        if (namespaceAssociated(args[i]->type())) outside("a call an argument's namespace may add to");
    if (syn(head).kind == syntax::Kind::Member) return memberCall(id, head, std::move(args));
    if (syn(head).kind != syntax::Kind::Name || tree.node(head).decl.valid()) return pointerCall(id, head, std::move(args));
    std::string callee = p_.syntaxTree_.text(tree.node(head).syntax);
    if (callee.compare(0, 2, "::") == 0) callee = callee.substr(2);
    // Which `f` an unqualified call means depends on the scope it is written in, which the parser has left by now.
    if (callee.find("::") == std::string::npos && scopedName(callee)) outside("a call of a name something in a scope shares");
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(callee);
    if (set == nullptr && p_.lookup_.findGlobal(callee) != nullptr) return pointerCall(id, head, std::move(args));
    if (set == nullptr) outside("a call of an undeclared name");
    for (std::size_t k = 0; k < set->size(); k++)
        if (p_.lookup_.functions_[(*set)[k]].fromTemplate) outside("a call of a template's");
    const std::string bare = Parser::localOf(callee);
    if (p_.tmpl_.templates_.count(bare) != 0 || p_.tmpl_.memberTemplates_.count(callee) != 0)
        outside("a call of a name a template shares");
    const bool qualified = callee.find("::") != std::string::npos;
    const Signature f = set->size() == 1 ? p_.lookup_.functions_[(*set)[0]]
                                         : p_.overload_.resolveOverload(callee, args, syn(id).loc);
    if (f.deleted) outside("a call of a deleted function");
    if (!f.owner.empty() && !(qualified && f.isStaticMember)) outside("a call of a member by its name");
    if (!f.owner.empty()) {
        const Type *cls = p_.lookup_.findTypedef(f.owner);
        if (cls == nullptr || !plainClass(p_, cls)) outside("a static member of what is not a plain struct");
    }
    if (callee.find_first_of("<~") != std::string::npos) outside("a call of '" + callee + "'");
    return finishCall(id, callee, f, f.params, std::move(args), false);
}

// [expr.call]/1 through a pointer to a function, `(*p)(a)` and `p(a)` alike: the call names the pointer's type.
ExprPtr Adapter::pointerCall(TypedId id, TypedId head, std::vector<ExprPtr> args) {
    const TypedTree &tree = typer_.tree();
    const syntax::Node &h = syn(head);
    const bool star = h.kind == syntax::Kind::Unary && p_.syntaxTree_.text(tree.node(head).syntax) == "*" &&
                      tree.node(head).count == 1;
    ExprPtr callee = expr(star ? tree.child(head, 0) : head);
    const Type *pt = callee->type();
    if (!pt->isFunctionPointer()) outside("a call through what is not a pointer to a function");
    const Type *fn = pt->pointee();
    if (fn->refQualifier() != RefQual::None || fn->isConstThisFn()) outside("a call through a qualified function type");
    Signature f(pt->describe(), std::string(), fn->returns(), fn->params(), fn->isVariadicFn());
    return finishCall(id, f.name, f, f.params, std::move(args), false, std::move(callee));
}

// [expr.ref] with [class.mfct.non-static]: `o.f(a)` is f called with `&o` in front of its arguments.
ExprPtr Adapter::memberCall(TypedId id, TypedId head, std::vector<ExprPtr> args) {
    const TypedTree &tree = typer_.tree();
    const std::size_t pos = syn(id).loc;
    const std::string &text = p_.syntaxTree_.text(tree.node(head).syntax);
    const bool arrow = text.compare(0, 2, "->") == 0;
    const std::string name = text.substr(arrow ? 2 : 1);
    if (name.empty() || name.find_first_of(":~<") != std::string::npos || name.compare(0, 8, "template") == 0 ||
        name.compare(0, 8, "operator") == 0)
        outside("a member call of '" + name + "'");
    if (tree.node(head).count != 1) outside("a member call of what is not one object");
    ExprPtr object = expr(tree.child(head, 0));
    if (arrow) {
        const Type *pt = object->type();
        if (!pt->isPointer() || !pt->pointee()->isStructOrUnion()) outside("a '->' of what is not a pointer to a struct");
        const Type *pointee = pt->pointee();
        object = typed(make_owned<Unary>('*', std::move(object)), pointee);
    }
    const Type *cls = object->type();
    if (!plainClass(p_, cls) || !pure(*object) || object->isXvalue() || !isLvalue(*object))
        outside("a member call on what is not a plain struct named");
    const Type *plain = cls->unqualified();
    if (p_.lookup_.findMemberOwner(plain, name) != plain) outside("a member call found in a base");
    const std::string key = plain->tag() + "::" + name;
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(key);
    if (set == nullptr || p_.tmpl_.memberTemplates_.count(key) != 0) outside("a member call of what is not a function");
    for (std::size_t k = 0; k < set->size(); k++)
        if (p_.lookup_.functions_[(*set)[k]].fromTemplate) outside("a member call of a template's");
    const Signature f = p_.overload_.resolveOverload(key, args, pos, cls, objectIsLvalue(*object));
    if (f.deleted || f.isVirtual || f.refThis != RefQual::None) outside("a member call of what is not plain");
    if (f.owner != plain->tag()) outside("a member call of a base's, brought in by a using-declaration");
    if (f.isStaticMember) return finishCall(id, key, f, f.params, std::move(args), false);
    const Type *thisType = p_.types_.pointerTo(f.constThis ? p_.types_.withConst(plain) : plain);
    std::vector<ExprPtr> all;
    all.push_back(typed(make_owned<Unary>('&', std::move(object)), thisType));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));
    std::vector<const Type *> full(1, thisType);
    full.insert(full.end(), f.params.begin(), f.params.end());
    return finishCall(id, name, f, full, std::move(all), true);
}

// Parser::completeCall for what the adapter takes: no default, a plain struct by value, a reference bound, a scalar
// converted, an argument past the parameters promoted; a reference returned is read through. The frame slots of a
// struct returned or passed are the parser's, read by node.
ExprPtr Adapter::finishCall(TypedId at, const std::string &name, const Signature &f, const std::vector<const Type *> &params,
                            std::vector<ExprPtr> args, bool hasThis, ExprPtr callee) {
    const Type *gives = f.returns != nullptr && f.returns->isReference() ? f.returns->referent() : f.returns;
    if (gives == nullptr || !(gives->isVoid() || scalar(gives) || plainClass(p_, gives)))
        outside("a call returning a class");
    if (args.size() < params.size() || (args.size() > params.size() && !f.variadic)) outside("a call with defaults or too many");
    for (std::size_t i = 0; i < args.size(); i++) {
        if (i >= params.size()) {
            ExprPtr a = decay(std::move(args[i]));
            const Type *t = a->type();
            if (t->isPointer()) { args[i] = std::move(a); continue; }
            if (!scalarArithmetic(t)) outside("a variadic argument of type '" + t->describe() + "'");
            args[i] = t->kind() == Kind::Float ? convert(std::move(a), p_.types_.get(Kind::Double)) : promote(std::move(a));
            continue;
        }
        if (params[i]->isReference()) { args[i] = bind(params[i], std::move(args[i])); continue; }
        if (plainClass(p_, params[i])) {
            args[i] = decay(std::move(args[i]));
            if (args[i]->type()->unqualified() != params[i]->unqualified()) outside("a struct passed from what is not one");
            args[i] = classValue(std::move(args[i]), params[i]);
            continue;
        }
        if (!scalar(params[i])) outside("a parameter of type '" + params[i]->describe() + "'");
        args[i] = convert(decay(std::move(args[i])), params[i]);
    }
    Parser::CallSlots slots;
    std::unordered_map<std::uint32_t, Parser::CallSlots>::const_iterator k = p_.callSlots_.find(typer_.tree().node(at).syntax.v);
    if (k != p_.callSlots_.end()) slots = k->second;
    else slots.args.assign(args.size(), 0);
    if (slots.args.size() != args.size()) outside("a call whose slots were not recorded");
    for (std::size_t i = 0; i < args.size(); i++)
        if (args[i]->type()->isStructOrUnion() && slots.args[i] == 0) outside("a struct argument with no slot");
    if (!f.returns->isReference() && gives->isStructOrUnion() && slots.result == 0) outside("a struct returned to no slot");
    std::unique_ptr<Call> c = make_owned<Call>(name, std::move(callee), std::move(args), f.variadic, slots.result,
                                               static_cast<int>(params.size()), slots.args);
    c->setSymbol(f.symbol);
    c->setHasThis(hasThis);
    c->setResultIndirect(slots.indirect);
    if (!f.returns->isReference()) return typed(std::move(c), f.returns);
    ExprPtr through = typed(std::move(c), p_.types_.pointerTo(gives));
    return typed(make_owned<Unary>('*', std::move(through)), gives);
}

// [dcl.init.ref]/5, the direct binding alone: an lvalue of the referent's type, its address taken.
ExprPtr Adapter::bind(const Type *ref, ExprPtr init) {
    const Type *referent = ref->referent(), *it = init->type();
    if (ref->isRValueReference() || referent->isFunction() || referent->isArray()) outside("a binding of '" + ref->describe() + "'");
    const MemberAccess *field = dynamic_cast<const MemberAccess *>(init.get());
    const Var *var = dynamic_cast<const Var *>(init.get());
    const Unary *deref = dynamic_cast<const Unary *>(init.get());
    const bool glvalue = (var != nullptr && !var->noAddress()) || (field != nullptr && field->width() == 0) ||
                         (deref != nullptr && deref->op() == '*');
    if (!glvalue || init->isXvalue() || it->unqualified() != referent->unqualified() || (it->isConst() && !referent->isConst()))
        outside("a reference bound to what is not an lvalue of its type");
    return typed(make_owned<Unary>('&', std::move(init)), p_.types_.pointerTo(referent));
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
