// sema::Adapter - see Adapter.h.

#include "Adapter.h"
#include "../AstDump.h"
#include "../parser/Parser.h"

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

bool scalarArithmetic(const Type *t) { return t != nullptr && t->isArithmetic() && !t->isEnumeration(); }

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
            if (n.count != 1 || !scalarArithmetic(returns_)) outside("a return of what is not a scalar");
            return placed(make_owned<Return>(convert(expr(tree.child(id, 0)), returns_)), s, n.syntax);
        case syntax::Kind::If: {
            if (n.count != 2 && n.count != 3) outside("an if of " + std::to_string(n.count) + " parts");
            if (syn(tree.child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
            ExprPtr cond = expr(tree.child(id, 0));
            if (!scalarArithmetic(cond->type())) outside("a condition that is not a scalar");
            StmtPtr then = stmt(tree.child(id, 1));
            StmtPtr otherwise = n.count == 3 ? stmt(tree.child(id, 2)) : StmtPtr();
            return placed(make_owned<If>(std::move(cond), std::move(then), std::move(otherwise)), s, n.syntax);
        }
        case syntax::Kind::While: {
            if (n.count != 2) outside("a while of " + std::to_string(n.count) + " parts");
            if (syn(tree.child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
            ExprPtr cond = expr(tree.child(id, 0));
            if (!scalarArithmetic(cond->type())) outside("a condition that is not a scalar");
            return placed(make_owned<While>(std::move(cond), stmt(tree.child(id, 1))), s, n.syntax);
        }
        case syntax::Kind::Do: {
            if (n.count != 2) outside("a do of " + std::to_string(n.count) + " parts");
            StmtPtr body = stmt(tree.child(id, 0));
            ExprPtr cond = expr(tree.child(id, 1));
            if (!scalarArithmetic(cond->type())) outside("a condition that is not a scalar");
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

static void plainLocal(const Local &l) {
    if (!l.staticName.empty()) throw std::string("a static local");
    if (l.isThreadLocal) throw std::string("a thread_local");
    if (l.constantOnly) throw std::string("a local read for its value only");
    if (!scalarArithmetic(l.type)) throw std::string("a local of type '" + l.type->describe() + "'");
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
        try { plainLocal(l->second); } catch (const std::string &why) { outside(why); }
        const TypedNode &v = tree.node(var);
        if (v.count > 1) outside("a var of " + std::to_string(v.count) + " parts");
        if (v.count == 0) continue;
        ExprPtr init = convert(expr(tree.child(var, 0)), l->second.type);
        ExprPtr store = typed(make_owned<Assign>(localVar(l->second), std::move(init)), l->second.type);
        body.push_back(make_owned<ExprStmt>(std::move(store)));
    }
    std::unique_ptr<Block> b = make_owned<Block>(std::move(body));
    b->setScope(-1);
    return placed(std::move(b), syn(id), n.syntax);
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
            cond = expr(part);
            if (!scalarArithmetic(cond->type())) outside("a condition that is not a scalar");
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
            if (!scalarArithmetic(t) || t->kind() == Kind::Bool || dynamic_cast<const Var *>(target.get()) == nullptr)
                outside("a postfix step of what is not a scalar variable");
            e = typed(make_owned<Postfix>(std::move(target), text == "++", 1), t);
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
            ExprPtr c = expr(tree.child(id, 0)), x = expr(tree.child(id, 1)), y = expr(tree.child(id, 2));
            if (!scalarArithmetic(c->type()) || !scalarArithmetic(x->type()) || !scalarArithmetic(y->type()))
                outside("a conditional of what is not a scalar");
            // [expr.cond]/4: two lvalues of one type make an lvalue, which the parser writes as `*(c ? &x : &y)`.
            if (x->type() == y->type() && dynamic_cast<const Var *>(x.get()) != nullptr &&
                dynamic_cast<const Var *>(y.get()) != nullptr) {
                const Type *t = x->type(), *pt = p_.types_.pointerTo(t);
                ExprPtr ax = typed(make_owned<Unary>('&', std::move(x)), pt), ay = typed(make_owned<Unary>('&', std::move(y)), pt);
                e = typed(make_owned<Unary>('*', typed(make_owned<Conditional>(std::move(c), std::move(ax), std::move(ay)), pt)), t);
                break;
            }
            const Type *t = p_.usualArithmetic(x->type(), y->type());
            x = convert(std::move(x), t);
            y = convert(std::move(y), t);
            e = typed(make_owned<Conditional>(std::move(c), std::move(x), std::move(y)), t);
            break;
        }
        case syntax::Kind::Call:
            e = call(id);
            break;
        case syntax::Kind::Cast: {
            std::unordered_map<std::uint32_t, const Type *>::const_iterator to = p_.castTypes_.find(n.syntax.v);
            if (to == p_.castTypes_.end() || n.count != 1) outside("a cast not recorded");
            if (!scalarArithmetic(to->second)) outside("a cast to '" + to->second->describe() + "'");
            ExprPtr v = expr(tree.child(id, 0));
            if (!scalarArithmetic(v->type())) outside("a cast of what is not a scalar");
            e = convert(std::move(v), to->second);
            break;
        }
        default:
            outside(std::string("a '") + syntax::kindName(s.kind) + "' expression");
    }
    if (n.type != nullptr && e->type() != n.type && e->type()->unqualified() != n.type->unqualified())
        outside(std::string("a '") + syntax::kindName(s.kind) + "' the typer and the adapter type apart");
    return e;
}

// A name, as the typer found it: a local of this function, else an object or an enumerator at file scope.
ExprPtr Adapter::name(TypedId id) {
    const TypedNode &n = typer_.tree().node(id);
    const std::string &text = p_.syntaxTree_.text(n.syntax);
    if (n.decl.valid()) {
        std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(n.decl.v);
        if (l == p_.declaredLocals_.end()) outside("a name whose local was not recorded");
        try { plainLocal(l->second); } catch (const std::string &why) { outside(why); }
        return localVar(l->second);
    }
    if (const GlobalSym *g = p_.lookup_.findGlobal(text)) {
        if (g->isThreadLocal) outside("a thread_local");
        if (!scalarArithmetic(g->type)) outside("an object of type '" + g->type->describe() + "'");
        Var *v = Var::global(text);
        v->setSymbol(g->symbol);
        v->setReadOnly(g->isConst);
        return typed(std::unique_ptr<Var>(v), g->type);
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
    const Type *t = a->type();
    if (!scalarArithmetic(t)) outside("a unary '" + op + "' of what is not a scalar");
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
    if (op == "!") return typed(make_owned<Unary>('!', std::move(a)), p_.types_.get(Kind::Bool));
    if (op == "++" || op == "--") {
        if (t->kind() == Kind::Bool || dynamic_cast<const Var *>(a.get()) == nullptr)
            outside("a prefix step of what is not a scalar variable");
        ExprPtr again = expr(tree.child(id, 0));
        ExprPtr sum = arithmetic(op == "++" ? "+" : "-", std::move(again), typed(make_owned<Num>(1LL), p_.types_.intType()));
        return typed(make_owned<Assign>(std::move(a), convert(std::move(sum), t)), t);
    }
    outside("a unary '" + op + "'");
    return ExprPtr();
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

// [expr.arith.conv] for + - * / % & | ^ and the six comparisons, [expr.shift] promoting each operand alone.
ExprPtr Adapter::arithmetic(const std::string &op, ExprPtr a, ExprPtr b) {
    BinOp o = BinOp::Add;
    try { o = binOpOf(op); } catch (const std::string &why) { outside(why); }
    if (!scalarArithmetic(a->type()) || !scalarArithmetic(b->type())) outside("'" + op + "' of what is not a scalar");
    if ((o == BinOp::Mod || o == BinOp::Shl || o == BinOp::Shr || o == BinOp::BitAnd || o == BinOp::BitOr ||
         o == BinOp::BitXor) && (!a->type()->isInteger() || !b->type()->isInteger()))
        outside("'" + op + "' of what is not an integer");
    if (o == BinOp::LAnd || o == BinOp::LOr)
        return typed(make_owned<Binary>(o, std::move(a), std::move(b)), p_.types_.get(Kind::Bool));
    if (o == BinOp::Shl || o == BinOp::Shr) {
        a = promote(std::move(a));
        b = promote(std::move(b));
        const Type *t = a->type();
        return typed(make_owned<Binary>(o, std::move(a), std::move(b)), t);
    }
    const Type *t = p_.usualArithmetic(a->type(), b->type());
    a = convert(std::move(a), t);
    b = convert(std::move(b), t);
    const bool compare = o == BinOp::Eq || o == BinOp::Ne || o == BinOp::Lt || o == BinOp::Le || o == BinOp::Gt ||
                         o == BinOp::Ge;
    return typed(make_owned<Binary>(o, std::move(a), std::move(b)), compare ? p_.types_.get(Kind::Bool) : t);
}

ExprPtr Adapter::binary(TypedId, const std::string &op, TypedId l, TypedId r) {
    return arithmetic(op, expr(l), expr(r));
}

// `a = b` converts b to a's type; `a op= b` is `a = a op b`, a read twice - the parser's lowering, for a scalar name.
ExprPtr Adapter::assign(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const std::string &op = p_.syntaxTree_.text(n.syntax);
    if (n.count != 2) outside("an assignment of " + std::to_string(n.count) + " parts");
    ExprPtr target = expr(tree.child(id, 0));
    const Type *t = target->type();
    if (!scalarArithmetic(t) || dynamic_cast<const Var *>(target.get()) == nullptr)
        outside("an assignment to what is not a scalar variable");
    ExprPtr value = expr(tree.child(id, 1));
    if (op != "=") {
        if (t->kind() == Kind::Bool) outside("a compound assignment to a bool");
        value = arithmetic(op.substr(0, op.size() - 1), expr(tree.child(id, 0)), std::move(value));
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
    if (f.returns == nullptr || !(f.returns->isVoid() || scalarArithmetic(f.returns))) outside("a call returning a class");
    const std::size_t given = n.count - 1;
    if (given < f.params.size() || (given > f.params.size() && !f.variadic)) outside("a call with defaults or too many");
    std::vector<ExprPtr> args;
    for (std::size_t i = 0; i < given; i++) {
        ExprPtr a = decay(expr(tree.child(id, static_cast<std::uint32_t>(i + 1))));
        if (i < f.params.size()) {
            const Type *p = f.params[i];
            if (!(scalarArithmetic(p) || (p->isPointer() && a->type() == p))) outside("a parameter of type '" + p->describe() + "'");
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

// [conv]: a value of one scalar type to another - a Cast, but to bool a comparison with zero.
ExprPtr Adapter::convert(ExprPtr e, const Type *to) {
    const Type *from = e->type();
    if (from == to) return e;
    if (to->kind() == Kind::Bool && to->unqualified() == to) {
        if (!from->isInteger() && !from->isPointer()) outside("a conversion of '" + from->describe() + "' to bool");
        return typed(make_owned<Binary>(BinOp::Ne, std::move(e), typed(make_owned<Num>(0LL), p_.types_.intType())), to);
    }
    if (from->isPointer() && to->isPointer() && from->pointee() == to->pointee()) return e;
    if (!scalarArithmetic(from) || !scalarArithmetic(to)) outside("a conversion of '" + from->describe() + "' to '" + to->describe() + "'");
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
