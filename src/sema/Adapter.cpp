// sema::Adapter - see Adapter.h.

#include "Adapter.h"

#include "../AstDump.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace sema {

void Adapter::outside(const std::string &why) { throw Outside(why); }

const syntax::Node &Adapter::syn(TypedId id) const { return p_.syntaxTree_.node(typer_.tree().node(id).syntax); }

StmtPtr Adapter::body(TypedId typed, const Function &fn, std::string *why) {
    returns_ = fn.returns();
    fnSymbol_ = fn.symbol();
    alive_.clear();
    loopMarks_.clear();
    switches_.clear();
    tryDepth_ = 0;
    handlerLoops_.clear();
    handovers_.clear();
    pending_.clear();
    stmtTemps_.clear();
    std::unordered_map<std::uint32_t, const Type *>::const_iterator declared =
        p_.declaredReturns_.find(typer_.tree().node(typed).syntax.v);
    if (declared != p_.declaredReturns_.end() && declared->second != nullptr) returns_ = declared->second;
    try {
        const TypedNode &f = typer_.tree().node(typed);
        if (f.count == 0) outside("a function with no body");
        // A Microsoft cleanup is a funclet and a state, wrapMsCleanups' shape, which the adapter does not write.
        if (fn.hasLandingPads() && p_.target_.microsoftNames()) outside("a function with landing pads");
        noexcept_ = fn.isNoexcept();
        if (p_.wrappedBodies_.count(fn.symbol())) outside("a body wrapped in a try");
        // An array parameter's bound is recorded beside it, and [dcl.fct]/5 has adjusted it away.
        for (std::uint32_t i = 0; i + 1 < f.count; i++)
            if (syn(typer_.tree().child(typed, i)).kind == syntax::Kind::Block) outside("a parameter that is a block");
        const TypedId last = typer_.tree().child(typed, f.count - 1);
        if (syn(last).kind != syntax::Kind::Block) outside("a body that is not a block");
        StmtPtr b = stmt(last);
        if (!pending_.empty()) outside("a temporary left past the body");
        // Parser::topLevel's wrapUp: every guard the function made is cleared at its entry.
        std::map<std::string, std::vector<int> >::const_iterator g = p_.functionGuards_.find(fn.symbol());
        if (g == p_.functionGuards_.end()) return b;
        std::vector<StmtPtr> withClears;
        for (std::size_t i = 0; i < g->second.size(); i++) withClears.push_back(make_owned<ExprStmt>(setGuard(g->second[i], 0)));
        withClears.push_back(std::move(b));
        return make_owned<Block>(std::move(withClears));
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
            // Parser::block: each object built opens a region to the next, the '}' destroys them last first, and the
            // regions are wrapped in the slots the parser chose.
            const std::size_t aliveAtEntry = alive_.size();
            std::vector<std::pair<std::size_t, std::size_t> > built;
            std::vector<std::size_t> tryAt;
            std::vector<StmtPtr> body;
            // What this block's statements made and destroyed within themselves, which its pads release under guard.
            std::vector<Pending> temps, outer;
            outer.swap(stmtTemps_);
            for (std::uint32_t i = 0; i < n.count; i++) {
                const TypedId part = tree.child(id, i);
                const std::size_t before = alive_.size();
                body.push_back(syn(part).kind == syntax::Kind::DeclStmt ? declaration(part, true) : stmt(part));
                if (!pending_.empty()) outside("a temporary left past its statement");
                if (syn(part).kind == syntax::Kind::Try) tryAt.push_back(body.size() - 1);
                temps.insert(temps.end(), stmtTemps_.begin(), stmtTemps_.end());
                stmtTemps_.clear();
                if (alive_.size() > before) built.push_back(std::make_pair(body.size(), alive_.size()));
            }
            stmtTemps_.swap(outer);
            if (!temps.empty() && (built.empty() || built[0].first != 0))
                built.insert(built.begin(), std::make_pair(std::size_t(0), aliveAtEntry));
            destroy(body, aliveAtEntry, alive_.size());
            if (!built.empty()) {
                std::unordered_map<std::uint32_t, std::pair<int, int> >::const_iterator k = p_.blockCleanups_.find(n.syntax.v);
                if (k == p_.blockCleanups_.end()) outside("a block whose cleanup slots were not recorded");
                body = wrapCleanups(std::move(body), built, aliveAtEntry, k->second, tryAt, temps);
            }
            alive_.resize(aliveAtEntry);
            std::unique_ptr<Block> b = make_owned<Block>(std::move(body));
            b->setScope(scope->second);
            return placed(std::move(b), s, n.syntax);
        }
        case syntax::Kind::DeclStmt:
            return declaration(id);
        case syntax::Kind::ExprStmt:
            if (n.count != 1) outside("an expression statement of " + std::to_string(n.count) + " parts");
            // `throw e;` is the statements Parser::throwStatement writes, in a block of their own.
            if (syn(tree.child(id, 0)).kind == syntax::Kind::Throw && p_.syntaxTree_.text(tree.node(tree.child(id, 0)).syntax).empty() &&
                tree.node(tree.child(id, 0)).count == 1) {
                if (noexcept_ && tryDepth_ == 0 && handlerLoops_.empty()) outside("a throw a noexcept function ends in abort");
                std::unique_ptr<Block> b = make_owned<Block>(throwSteps(tree.child(id, 0)));
                b->setScope(-1);
                return placed(std::move(b), s, n.syntax);
            }
            return placed(make_owned<ExprStmt>(fullExpression(expr(tree.child(id, 0)), tree.node(tree.child(id, 0)).syntax)), s,
                          n.syntax);
        case syntax::Kind::Try:
            return tryStatement(id);
        case syntax::Kind::Return:
            if (!alive_.empty() || !handlerLoops_.empty() || dyingBytes(p_, returns_)) return unwindingReturn(id);
            if (n.count == 0) {
                if (returns_ != nullptr && !returns_->isVoid()) outside("a return with no value");
                return placed(make_owned<Return>(ExprPtr()), s, n.syntax);
            }
            if (n.count == 1 && returns_ != nullptr && returns_->isReference()) {
                ExprPtr bound = bind(returns_, expr(tree.child(id, 0)));
                noTemporarySince(0, "a reference returned");
                return placed(make_owned<Return>(std::move(bound)), s, n.syntax);
            }
            if (n.count == 1 && returns_ != nullptr && byteClass(p_, returns_)) {
                ExprPtr value = fullExpression(decay(expr(tree.child(id, 0))), tree.node(tree.child(id, 0)).syntax);
                if (value->type()->unqualified() != returns_->unqualified()) outside("a struct returned from what is not one");
                return placed(make_owned<Return>(classValue(std::move(value), returns_)), s, n.syntax);
            }
            if (n.count == 1 && returns_ != nullptr && returns_->isStructOrUnion())
                outside(p_.destructorOf(returns_->unqualified()) != nullptr ? "a return of a class with a destructor"
                                                                           : "a return of a class with a copy of its own");
            if (n.count != 1 || !scalar(returns_)) outside("a return of what is not a scalar");
            return placed(make_owned<Return>(convert(fullExpression(decay(expr(tree.child(id, 0))),
                                                                   tree.node(tree.child(id, 0)).syntax), returns_)), s, n.syntax);
        case syntax::Kind::If: {
            if (n.count != 2 && n.count != 3) outside("an if of " + std::to_string(n.count) + " parts");
            if (syn(tree.child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
            ExprPtr cond = fullExpression(condition(tree.child(id, 0)), tree.node(tree.child(id, 0)).syntax);
            StmtPtr then = stmt(tree.child(id, 1));
            StmtPtr otherwise = n.count == 3 ? stmt(tree.child(id, 2)) : StmtPtr();
            return placed(make_owned<If>(std::move(cond), std::move(then), std::move(otherwise)), s, n.syntax);
        }
        case syntax::Kind::While: {
            if (n.count != 2) outside("a while of " + std::to_string(n.count) + " parts");
            if (syn(tree.child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
            ExprPtr cond = fullExpression(condition(tree.child(id, 0)), tree.node(tree.child(id, 0)).syntax);
            loopMarks_.push_back(alive_.size());
            StmtPtr body = stmt(tree.child(id, 1));
            loopMarks_.pop_back();
            return placed(make_owned<While>(std::move(cond), std::move(body)), s, n.syntax);
        }
        case syntax::Kind::Do: {
            if (n.count != 2) outside("a do of " + std::to_string(n.count) + " parts");
            loopMarks_.push_back(alive_.size());
            StmtPtr body = stmt(tree.child(id, 0));
            loopMarks_.pop_back();
            ExprPtr cond = fullExpression(condition(tree.child(id, 1)), tree.node(tree.child(id, 1)).syntax);
            return placed(make_owned<DoWhile>(std::move(body), std::move(cond)), s, n.syntax);
        }
        case syntax::Kind::For:
            return forStatement(id);
        case syntax::Kind::Switch: {
            if (n.count != 2) outside("a switch of " + std::to_string(n.count) + " parts");
            if (syn(tree.child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a switch's condition");
            ExprPtr cond = decay(expr(tree.child(id, 0)));
            noTemporarySince(0, "a switch's condition");
            if (!cond->type()->isInteger()) outside("a switch on what is not an integer");
            const Type *governing = p_.promote(cond->type());
            cond = convert(std::move(cond), governing);
            switches_.push_back(SwitchCtx{ alive_.size(), loopMarks_.size(), {}, nullptr });
            StmtPtr body = stmt(tree.child(id, 1));
            SwitchCtx ctx = switches_.back();
            switches_.pop_back();
            std::sort(ctx.cases.begin(), ctx.cases.end(),
                      [](const std::pair<int, const Case *> &a, const std::pair<int, const Case *> &b) { return a.first < b.first; });
            std::vector<const Case *> cases;
            for (std::size_t i = 0; i < ctx.cases.size(); i++) cases.push_back(ctx.cases[i].second);
            return placed(make_owned<Switch>(std::move(cond), std::move(body), std::move(cases), ctx.deflt), s, n.syntax);
        }
        case syntax::Kind::Case: case syntax::Kind::Default: {
            std::unordered_map<std::uint32_t, Parser::CaseFacts>::const_iterator k = p_.caseFacts_.find(n.syntax.v);
            if (k == p_.caseFacts_.end() || switches_.empty() || n.count == 0) outside("a case label not recorded");
            StmtPtr body = stmt(tree.child(id, n.count - 1));
            std::unique_ptr<Case> c = make_owned<Case>(k->second.value, k->second.isDefault, k->second.id, std::move(body));
            if (k->second.isDefault) switches_.back().deflt = c.get();
            else switches_.back().cases.push_back(std::make_pair(k->second.id, c.get()));
            return placed(std::move(c), s, n.syntax);
        }
        case syntax::Kind::Label:
            if (n.count != 1) outside("a label of " + std::to_string(n.count) + " parts");
            return placed(make_owned<Label>(p_.syntaxTree_.text(n.syntax), stmt(tree.child(id, 0))), s, n.syntax);
        case syntax::Kind::Goto: {
            // Parser::gotoStatement's block, its Goto appended by resolveGotos: nothing alive here to destroy.
            if (!alive_.empty() || !handlerLoops_.empty() || tryDepth_ > 0) outside("a goto among objects or handlers");
            std::vector<StmtPtr> steps;
            steps.push_back(make_owned<Goto>(p_.syntaxTree_.text(n.syntax)));
            std::unique_ptr<Block> b = make_owned<Block>(std::move(steps));
            b->setScope(-1);
            return placed(std::move(b), s, n.syntax);
        }
        case syntax::Kind::Break:
            if (!switches_.empty() && switches_.back().loops == loopMarks_.size()) {
                // A break that leaves a switch, Parser::breakMarks_: what the switch's body built is destroyed.
                for (std::size_t h : handlerLoops_)
                    if (h >= loopMarks_.size()) outside("a break out of a switch that leaves a handler");
                if (alive_.size() > switches_.back().alive) outside("a break out of a switch among objects");
                std::unique_ptr<Break> b = make_owned<Break>();
                b->setPos(s.loc);
                b->setSyntax(n.syntax);
                return StmtPtr(std::move(b));
            }
            return leaving(id, make_owned<Break>());
        case syntax::Kind::Continue:
            return leaving(id, make_owned<Continue>());
        default:
            outside(std::string("a '") + syntax::kindName(s.kind) + "' statement");
    }
    return StmtPtr();
}

// A local read or written by name, as Parser::localRef builds it.
static ExprPtr localVar(const Local &l, TypeTable &types) {
    Var *v = l.staticName.empty() ? Var::local(l.name, l.offset) : Var::global(l.staticName);
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
    return byteClass(p, t) && !p.hasConstructors(t->unqualified()->tag());
}

// A struct the parser copies, passes and returns by its bytes, whatever constructors build it: no destructor, vptr,
// copy, move or copy assignment of its own (M2.12i).
bool Adapter::byteClass(Parser &p, const Type *t) {
    if (t == nullptr || !t->isStructOrUnion()) return false;
    const Type *u = t->unqualified();
    return u->isComplete() && u->kind() != Kind::Union && !u->hasDestructor() && !u->hasVptr() &&
           p.classes_.copyAssignOf(u) == nullptr && p.classes_.copyConstructorOf(u) == nullptr &&
           p.classes_.moveConstructorOf(u) == nullptr;
}

// A struct copied by its bytes that has a destructor: what byteClass takes, with the destructor allowed.
bool Adapter::dyingBytes(Parser &p, const Type *t) {
    if (t == nullptr || !t->isStructOrUnion()) return false;
    const Type *u = t->unqualified();
    return u->isComplete() && u->kind() != Kind::Union && u->hasDestructor() && !u->hasVptr() &&
           p.classes_.copyAssignOf(u) == nullptr && p.classes_.copyConstructorOf(u) == nullptr &&
           p.classes_.moveConstructorOf(u) == nullptr;
}

void Adapter::plainLocal(Parser &p, const Local &l) {
    if (l.isThreadLocal) throw std::string("a thread_local");
    if (!l.staticName.empty() && p.guardedStatics_.count(l.staticName)) throw std::string("a static local built at run time");
    if (l.constantOnly) throw std::string("a local read for its value only");
    if (l.type->isReference()) {
        const Type *r = l.type->referent();
        if (!scalar(r) && !byteClass(p, r)) throw std::string("a reference to '" + r->describe() + "'");
        return;
    }
    if (!scalar(l.type) && !scalarArray(l.type) && !plainClass(p, l.type) && !ownedClass(p, l.type))
        throw std::string("a local of type '" + l.type->describe() + "'");
}

static bool namespaceAssociated(const Type *t);

// A class the parser builds by a constructor and destroys at the end of a scope, with nothing virtual in it.
bool Adapter::ownedClass(Parser &p, const Type *t) {
    if (t == nullptr || !t->isStructOrUnion()) return false;
    const Type *u = t->unqualified();
    if (!u->isComplete() || u->kind() == Kind::Union || u->hasVptr() || u->tag().empty() || plainClass(p, u)) return false;
    return p.hasConstructors(u->tag()) || u->hasDestructor();
}

// One object of an owned class at a block's level: `X x;` or `X x(a, b)`, its constructor resolved over the arguments
// as Parser::constructObject resolves it, and alive from here if it has a destructor.
void Adapter::ownedLocal(TypedId var, const Local &l, std::vector<StmtPtr> &body) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &v = tree.node(var);
    const Type *cls = l.type->unqualified();
    const bool dies = p_.destructorOf(cls) != nullptr;
    if (dies && p_.target_.microsoftNames()) outside("an object a Microsoft funclet would destroy");
    // A named return value is built in the caller's storage, which only a class with a copy of its own gets (nrvoCandidate).
    if (returns_ != nullptr && returns_->isStructOrUnion() && returns_->unqualified() == cls && !byteClass(p_, cls) &&
        !dyingBytes(p_, cls))
        outside("a local of the class returned");
    int bounds = 0;
    const std::size_t end = declaratorEnd(var, &bounds);
    if (bounds != 0 || end >= p_.tokens_.size()) outside("a class object not where it was");
    const Token &t = p_.tokens_[end];
    std::vector<ExprPtr> args;
    if (t.is("(")) {
        // The syntax tree keeps no braces inside a list, so `X x({7})` would read as `X x(7)`.
        for (std::size_t k = end, depth = 0; k < p_.tokens_.size(); k++) {
            const Token &w = p_.tokens_[k];
            if (w.is("{")) outside("a braced constructor argument");
            if (w.is("(")) depth++;
            else if (w.is(")") && --depth == 0) break;
        }
        for (std::uint32_t i = 0; i < v.count; i++) args.push_back(expr(tree.child(var, i)));
    } else if (t.is("=")) {
        // Copy-initialisation, [dcl.init]/16: one argument, an explicit constructor being the parser's to refuse.
        if (end + 1 >= p_.tokens_.size() || p_.tokens_[end + 1].is("{") || v.count != 1)
            outside("a class object initialised by braces");
        // A call returning this class, destructor and all, builds its result here: the copy is elided.
        const bool claims = dies && dyingBytes(p_, cls) && syn(tree.child(var, 0)).kind == syntax::Kind::Call;
        if (claims) claimedSlot_ = l.offset;
        args.push_back(expr(tree.child(var, 0)));
        claimedSlot_ = 0;
        if (claims) {
            const Call *made = dynamic_cast<const Call *>(args[0].get());
            if (made == nullptr || made->resultSlot() != l.offset) outside("a class result not built where it was claimed");
            body.push_back(make_owned<ExprStmt>(std::move(args[0])));
            alive_.push_back(AliveObj{ l.name, l.offset, cls });
            return;
        }
    } else if (!t.is(";") && !t.is(",")) {
        outside("a class object initialised by '" + t.text + "'");
    } else if (v.count != 0) {
        outside("a class object of " + std::to_string(v.count) + " parts");
    }
    // One object of the class itself is a copy: its bytes where the class has no copy or move constructor (the
    // parser's trivialCopy), else its copy constructor, chosen below; a temporary is an elision, kept outside.
    if (args.size() == 1 && args[0]->type()->isStructOrUnion()) {
        if (args[0]->type()->unqualified() != cls) outside("a class object initialised from another class");
        if (p_.classes_.copyConstructorOf(cls) == nullptr && p_.classes_.moveConstructorOf(cls) == nullptr) {
            ExprPtr object = typed(std::unique_ptr<Var>(Var::local(l.name, l.offset)), l.type);
            body.push_back(make_owned<ExprStmt>(typed(make_owned<Assign>(std::move(object), std::move(args[0])), l.type)));
            if (dies) alive_.push_back(AliveObj{ l.name, l.offset, cls });
            return;
        }
        if (!isLvalue(*args[0])) outside("a class object initialised from a temporary");
    } else {
        // Any other class argument is a slice or a conversion function, which the parser decides before it resolves.
        for (std::size_t i = 0; i < args.size(); i++)
            if (!scalar(args[i]->type()) && !(args[i]->type()->isArray() && scalar(args[i]->type()->pointee())))
                outside("a constructor given what is not a scalar");
    }
    if (p_.hasConstructors(cls->tag())) {
        // The constructor the parser chose for this declaration, recorded on its `var` node (M2.16).
        std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(tree.node(var).syntax.v);
        const Signature *chosen = r != p_.resolutions_.end() && r->second.what == Parser::Resolved::Call
                                      ? signatureBySymbol(r->second.symbol) : nullptr;
        if (chosen == nullptr) outside("a constructor the parser did not record");
        const Signature ctor = *chosen;
        if (ctor.deleted || args.size() != ctor.params.size()) outside("a constructor with defaults, or deleted");
        const Type *thisType = p_.types_.pointerTo(cls);
        ExprPtr object = typed(std::unique_ptr<Var>(Var::local(l.name, l.offset)), l.type);
        std::vector<ExprPtr> all;
        all.push_back(typed(make_owned<Unary>('&', std::move(object)), thisType));
        for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));
        std::vector<const Type *> full(1, thisType);
        full.insert(full.end(), ctor.params.begin(), ctor.params.end());
        const Signature made(cls->tag(), ctor.symbol, p_.types_.get(Kind::Void), full, false);
        body.push_back(make_owned<ExprStmt>(finishCall(var, cls->tag(), made, full, std::move(all), false)));
    } else if (!args.empty()) {
        outside("a class with no constructor given arguments");
    }
    if (dies) alive_.push_back(AliveObj{ l.name, l.offset, cls });
}

// Parser::emitDestructors: the objects alive in [from, to) destroyed last first, each by its destructor's call.
void Adapter::destroy(std::vector<StmtPtr> &into, std::size_t from, std::size_t to, int except) const {
    for (std::size_t i = to; i > from; i--) {
        const AliveObj &a = alive_[i - 1];
        if (except >= 0 && a.offset == except) continue;
        into.push_back(make_owned<ExprStmt>(destruction(a.name, a.offset, a.cls)));
    }
}

// Parser::destructorCall on the object a frame slot holds: `~T(&o)`.
ExprPtr Adapter::destruction(const std::string &name, int offset, const Type *cls) const {
    const Signature *dtor = p_.destructorOf(cls);
    ExprPtr object = typed(std::unique_ptr<Var>(Var::local(name, offset)), cls);
    std::vector<ExprPtr> args;
    args.push_back(typed(make_owned<Unary>('&', std::move(object)), p_.types_.pointerTo(cls)));
    std::unique_ptr<Call> c = make_owned<Call>("~" + dtor->owner, ExprPtr(), std::move(args), false, 0, 1,
                                               std::vector<int>(1, 0));
    c->setSymbol(dtor->symbol);
    return typed(std::move(c), p_.types_.get(Kind::Void));
}

ExprPtr Adapter::setGuard(int flag, int value) const {
    return typed(make_owned<Assign>(typed(std::unique_ptr<Var>(Var::local("$guard", flag)), p_.types_.intType()),
                                    typed(make_owned<Num>(static_cast<long long>(value)), p_.types_.intType())),
                 p_.types_.intType());
}

// Parser::endFullExpression: every guard cleared in front, the value kept in the parser's slot, the temporaries destroyed
// last first - a guarded one only if it was built, its guard cleared with it - and the guarded ones left to the block.
ExprPtr Adapter::fullExpression(ExprPtr e, syntax::NodeId node) {
    if (pending_.empty()) return e;
    std::vector<Pending> mine;
    mine.swap(pending_);
    const Type *it = p_.types_.intType();
    for (std::size_t k = 0; k < mine.size(); k++) {
        if (mine[k].flag == 0) continue;
        const Type *et = e->type();
        e = typed(make_owned<Comma>(setGuard(mine[k].flag, 0), std::move(e)), et);
    }
    const Type *t = e->type();
    const bool hasValue = t != nullptr && !t->isVoid() && !t->isFunction();
    int keep = 0;
    if (hasValue) {
        std::unordered_map<std::uint32_t, int>::const_iterator k = p_.fullSlots_.find(node.v);
        if (!node.valid() || k == p_.fullSlots_.end()) outside("a full expression whose slot was not recorded");
        keep = k->second;
        e = typed(make_owned<Assign>(typed(std::unique_ptr<Var>(Var::local("$full", keep)), t), std::move(e)), t);
    }
    for (std::size_t k = mine.size(); k-- > 0; ) {
        ExprPtr gone = destruction("$copy", mine[k].slot, mine[k].cls);
        if (mine[k].flag != 0) {
            ExprPtr clear = typed(make_owned<Comma>(std::move(gone), setGuard(mine[k].flag, 0)), it);
            gone = typed(make_owned<Conditional>(typed(std::unique_ptr<Var>(Var::local("$guard", mine[k].flag)), it),
                                                 std::move(clear), typed(make_owned<Num>(0LL), it)), it);
        }
        e = typed(make_owned<Comma>(std::move(e), std::move(gone)), t);
    }
    if (hasValue) e = typed(make_owned<Comma>(std::move(e), typed(std::unique_ptr<Var>(Var::local("$full", keep)), t)), t);
    for (std::size_t k = 0; k < mine.size(); k++)
        if (mine[k].flag != 0) stmtTemps_.push_back(mine[k]);
    return e;
}

// Parser::flushTemporaries, a declaration's end: each temporary destroyed last first, a guarded one under its guard.
void Adapter::flushPending(std::vector<StmtPtr> &into) {
    std::vector<Pending> mine;
    mine.swap(pending_);
    for (std::size_t k = mine.size(); k-- > 0; ) {
        StmtPtr call = make_owned<ExprStmt>(destruction("$copy", mine[k].slot, mine[k].cls));
        if (mine[k].flag != 0) {
            std::vector<StmtPtr> both;
            both.push_back(std::move(call));
            both.push_back(make_owned<ExprStmt>(setGuard(mine[k].flag, 0)));
            call = make_owned<If>(typed(std::unique_ptr<Var>(Var::local("$guard", mine[k].flag)), p_.types_.intType()),
                                  make_owned<Block>(std::move(both)), StmtPtr());
        }
        into.push_back(std::move(call));
    }
}

// Parser::releaseTemporary: the pending temporary the value is, taken off the list - its slot through commas,
// dereferences and addresses, or a call's result.
bool Adapter::releasePending(const Expr &value) {
    const Expr *at = &value;
    for (;;) {
        if (const Comma *c = dynamic_cast<const Comma *>(at)) { at = &c->right(); continue; }
        if (const Unary *u = dynamic_cast<const Unary *>(at))
            if (u->op() == '*' || u->op() == '&') { at = &u->operand(); continue; }
        break;
    }
    int slot = -1;
    if (const Var *v = dynamic_cast<const Var *>(at)) {
        if (!v->isLocal()) return false;
        slot = v->offset();
    } else if (const Call *c = dynamic_cast<const Call *>(at)) {
        if (c->resultSlot() == 0) return false;
        slot = c->resultSlot();
    } else {
        return false;
    }
    for (std::size_t i = 0; i < pending_.size(); i++)
        if (pending_[i].slot == slot) {
            pending_.erase(pending_.begin() + static_cast<long>(i));
            return true;
        }
    return false;
}

// Parser::releaseGuarded: in a pad, one temporary of the block destroyed if it was built, its guard cleared.
void Adapter::releaseGuarded(std::vector<StmtPtr> &steps, const Pending &t) const {
    if (t.flag == 0) return;
    std::vector<StmtPtr> both;
    both.push_back(make_owned<ExprStmt>(destruction("$copy", t.slot, t.cls)));
    both.push_back(make_owned<ExprStmt>(setGuard(t.flag, 0)));
    steps.push_back(make_owned<If>(typed(std::unique_ptr<Var>(Var::local("$guard", t.flag)), p_.types_.intType()),
                                   make_owned<Block>(std::move(both)), StmtPtr()));
}

void Adapter::noTemporarySince(std::size_t mark, const char *where) const {
    if (pending_.size() > mark) outside(std::string("a temporary in ") + where);
}

// Parser::wrapCleanups, cleanupPad and unwindPad for a block with no `try` and no temporary: each stretch from one
// construction to the next a Try whose pad destroys what was built by then and resumes unwinding.
std::vector<StmtPtr> Adapter::wrapCleanups(std::vector<StmtPtr> body, const std::vector<std::pair<std::size_t, std::size_t> > &built,
                                           std::size_t aliveAtEntry, std::pair<int, int> slots,
                                           const std::vector<std::size_t> &tryAt, const std::vector<Pending> &temps) {
    Handover *into = handovers_.empty() ? nullptr : &handovers_.back();
    if (into != nullptr && (slots.first != into->ptr || slots.second != into->sel)) outside("a cleanup in slots not its handover's");
    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++) out.push_back(std::move(body[i]));
    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first, to = k + 1 < built.size() ? built[k + 1].first : body.size();
        // A region is split around every `try` in it, the `try` answering for itself.
        std::size_t cur = from;
        while (cur < to) {
            std::size_t stop = to;
            for (std::size_t t : tryAt)
                if (t >= cur && t < stop) stop = t;
            std::vector<StmtPtr> guarded;
            for (std::size_t i = cur; i < stop; i++) guarded.push_back(std::move(body[i]));
            if (!guarded.empty()) {
                std::vector<StmtPtr> steps;
                // The block's temporaries go first, each under its guard - Parser::cleanupPad.
                for (std::size_t t = temps.size(); t-- > 0; ) releaseGuarded(steps, temps[t]);
                destroy(steps, into != nullptr && into->chain ? into->aliveFrom : aliveAtEntry, built[k].second);
                if (into != nullptr) steps.push_back(make_owned<Goto>(into->label));
                else steps.push_back(p_.resumeUnwinding(slots.first));
                std::unique_ptr<Try> seg = make_owned<Try>(std::move(guarded), p_.unwindPad(std::move(steps)), slots.first,
                                                           slots.second, std::vector<std::string>());
                if (into != nullptr && into->chain) into->segments.push_back(seg.get());
                out.push_back(std::move(seg));
            }
            if (stop == to) break;
            out.push_back(std::move(body[stop]));
            cur = stop + 1;
        }
    }
    return out;
}

// A `return` with objects alive: the value saved in the slot the parser chose, everything destroyed, the slot returned.
StmtPtr Adapter::unwindingReturn(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    std::vector<StmtPtr> steps;
    if (n.count == 0) {
        if (returns_ != nullptr && !returns_->isVoid()) outside("a return with no value");
        destroy(steps, 0, alive_.size());
        endCatches(steps, static_cast<int>(handlerLoops_.size()));
        steps.push_back(make_owned<Return>(ExprPtr()));
        return placed(make_owned<Block>(std::move(steps)), syn(id), n.syntax);
    }
    if (n.count != 1 || returns_ == nullptr || !(scalar(returns_) || byteClass(p_, returns_) || dyingBytes(p_, returns_)))
        outside("a return of what is not a scalar, past objects alive");
    ExprPtr value = decay(expr(tree.child(id, 0)));
    // The temporary that is the returned value is the caller's from here, and not destroyed (Parser's ownedTemporary).
    const bool owned = returns_->isStructOrUnion() && releasePending(*value);
    value = fullExpression(std::move(value), tree.node(tree.child(id, 0)).syntax);
    // A local of a class with a destructor goes out as its bytes and is not destroyed here: the elision.
    int elided = -1;
    if (dyingBytes(p_, returns_)) {
        const Var *v = dynamic_cast<const Var *>(value.get());
        for (std::size_t i = 0; !owned && v != nullptr && v->isLocal() && i < alive_.size(); i++)
            if (alive_[i].offset == v->offset() && alive_[i].name == v->name()) elided = v->offset();
        if ((!owned && elided < 0) || value->type()->unqualified() != returns_->unqualified())
            outside("a class returned from what is not its local");
        value = classValue(std::move(value), returns_);
    } else if (byteClass(p_, returns_)) {
        if (value->type()->unqualified() != returns_->unqualified()) outside("a struct returned from what is not one");
        value = classValue(std::move(value), returns_);
    } else {
        value = convert(std::move(value), returns_);
    }
    if (alive_.empty() && handlerLoops_.empty()) return placed(make_owned<Return>(std::move(value)), syn(id), n.syntax);
    std::unordered_map<std::uint32_t, Parser::ReturnTemp>::const_iterator k = p_.returnTemps_.find(n.syntax.v);
    if (k == p_.returnTemps_.end()) outside("a return whose slot was not recorded");
    ExprPtr keep = typed(std::unique_ptr<Var>(Var::local(k->second.name, k->second.slot)), returns_);
    steps.push_back(make_owned<ExprStmt>(typed(make_owned<Assign>(std::move(keep), std::move(value)), returns_)));
    destroy(steps, 0, alive_.size(), elided);
    endCatches(steps, static_cast<int>(handlerLoops_.size()));
    steps.push_back(make_owned<Return>(typed(std::unique_ptr<Var>(Var::local(k->second.name, k->second.slot)), returns_)));
    return placed(make_owned<Block>(std::move(steps)), syn(id), n.syntax);
}

// Parser::jumpLeaving: a `break` or `continue` destroys what its loop's body built before it jumps.
StmtPtr Adapter::leaving(TypedId id, StmtPtr jump) {
    const std::size_t mark = loopMarks_.empty() ? alive_.size() : loopMarks_.back();
    // Parser::handlersLeftByBreak: each handler entered inside the loop the jump leaves is ended.
    int left = 0;
    for (std::size_t h : handlerLoops_)
        if (h >= loopMarks_.size()) left++;
    if (alive_.size() <= mark && left == 0) {
        jump->setPos(syn(id).loc);
        jump->setSyntax(typer_.tree().node(id).syntax);
        return jump;
    }
    std::vector<StmtPtr> steps;
    destroy(steps, mark, alive_.size());
    endCatches(steps, left);
    steps.push_back(std::move(jump));
    std::unique_ptr<Block> b = make_owned<Block>(std::move(steps));
    b->setScope(-1);
    return placed(std::move(b), syn(id), typer_.tree().node(id).syntax);
}

// [stmt.dcl]: the declarators that have an initialiser, each an assignment in a block of their own.
StmtPtr Adapter::declaration(TypedId id, bool owns) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    std::vector<StmtPtr> body;
    for (std::uint32_t i = 0; i < n.count; i++) {
        // Parser::flushTemporaries ends each declarator's full expression: what the last one made dies here.
        flushPending(body);
        const TypedId var = tree.child(id, i);
        // What else a declaration records is unevaluated - an alignas, a decltype, a static_assert - and builds nothing.
        if (syn(var).kind != syntax::Kind::Var) continue;
        std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(tree.node(var).syntax.v);
        if (l == p_.declaredLocals_.end()) outside("a var whose local was not recorded");
        try { plainLocal(p_, l->second); } catch (const std::string &why) { outside(why); }
        // A static local whose initialiser is a constant is data the parser laid down: nothing runs here.
        if (!l->second.staticName.empty()) continue;
        const TypedNode &v = tree.node(var);
        if (ownedClass(p_, l->second.type)) {
            if (!owns) outside("a class object declared where it is not a block's statement");
            ownedLocal(var, l->second, body);
            continue;
        }
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
        if (plainClass(p_, l->second.type) && aggregateList(var, l->second, body)) continue;
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
    flushPending(body);
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

// Parser::emitInit for a plain struct: its leaves in order, each from the next value of a list or zeroed where the
// list has ended (so brace elision is the parser's), through the member accesses targetFor builds. The syntax tree
// keeps no braces inside a list, so a list holding one stays the parser's; false where there is no list at all.
bool Adapter::aggregateList(TypedId var, const Local &l, std::vector<StmtPtr> &body) {
    const TypedTree &tree = typer_.tree();
    int bounds = 0;
    std::size_t at = declaratorEnd(var, &bounds);
    const std::vector<Token> &tk = p_.tokens_;
    if (at < tk.size() && tk[at].is("=")) at++;
    if (bounds != 0 || at >= tk.size() || !tk[at].is("{")) return false;
    for (std::size_t k = at, depth = 0; k < tk.size(); k++) {
        if (tk[k].is("...")) outside("a struct's list expanding a pack");
        if (tk[k].is("{")) { if (++depth > 1) outside("a struct's list holding braces"); }
        else if (tk[k].is("}") && --depth == 0) break;
    }
    struct Step { const Member *m; long long index; };
    struct Leaf { std::vector<Step> path; const Type *type; };
    std::vector<Leaf> leaves;
    std::vector<Step> path;
    struct Walk {
        Adapter &a; std::vector<Leaf> &leaves; std::vector<Step> &path;
        void operator()(const Type *t) {
            if (t->isArray()) {
                for (long long i = 0; i < t->length(); i++) {
                    path.push_back(Step{ nullptr, i });
                    (*this)(t->pointee());
                    path.pop_back();
                }
                return;
            }
            if (t->isStructOrUnion()) {
                const Type *u = t->unqualified();
                if (u->kind() == Kind::Union || !u->bases().empty() || !Adapter::plainClass(a.p_, u))
                    a.outside("a list into a member of type '" + t->describe() + "'");
                const std::vector<Member> &members = u->members();
                for (std::size_t i = 0; i < members.size(); i++) {
                    if (members[i].name.empty()) continue;
                    if (members[i].anonUnion != nullptr) a.outside("a list into an anonymous union");
                    path.push_back(Step{ &members[i], 0 });
                    (*this)(members[i].type);
                    path.pop_back();
                }
                return;
            }
            if (!scalar(t)) a.outside("a list into a leaf of type '" + t->describe() + "'");
            leaves.push_back(Leaf{ path, t });
        }
    } walk = { *this, leaves, path };
    walk(l.type);
    const TypedNode &v = tree.node(var);
    if (v.count > leaves.size()) outside("a struct's list longer than it");
    for (std::size_t i = 0; i < leaves.size(); i++) {
        ExprPtr to = localVar(l, p_.types_);
        for (const Step &s : leaves[i].path) {
            if (s.m != nullptr) {
                ExprPtr acc = make_owned<MemberAccess>(std::move(to), s.m->name, s.m->offset, s.m->width, s.m->bitOffset);
                to = typed(std::move(acc), s.m->type);
            } else {
                const Type *elem = to->type()->pointee();
                ExprPtr sum = pointerAdd(decay(std::move(to)), typed(make_owned<Num>(s.index), p_.types_.intType()));
                to = typed(make_owned<Unary>('*', std::move(sum)), elem);
            }
        }
        const Type *t = to->type();
        ExprPtr value;
        if (i < v.count) {
            const TypedId item = tree.child(var, static_cast<std::uint32_t>(i));
            if (syn(item).kind == syntax::Kind::StringLit && !t->isPointer()) outside("a string in a struct's list");
            value = decay(expr(item));
            if (value->type() == nullptr || value->type()->isStructOrUnion()) outside("a class in a struct's list");
        } else if (t->isFloating()) {
            value = typed(make_owned<Num>(static_cast<long double>(0)), p_.types_.doubleType());
        } else {
            value = typed(make_owned<Num>(0LL), t->unqualified()->isEnumeration() ? t->unqualified() : p_.types_.intType());
        }
        ExprPtr store = typed(make_owned<Assign>(std::move(to), convert(std::move(value), t)), t);
        body.push_back(make_owned<ExprStmt>(std::move(store)));
    }
    return true;
}

// A `for`'s parts are told apart by where they stand against its two semicolons, any of the first three being absent.
// Parser::rangeForStatement over an array: `__b = a; __e = __b + N;` and a loop whose body takes `*__b` first.
StmtPtr Adapter::rangeFor(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const Parser::RangeFacts &f = p_.rangeFacts_.find(n.syntax.v)->second;
    if (n.count != 3 || syn(tree.child(id, 1)).kind != syntax::Kind::Var) outside("a range-based for of other parts");
    ExprPtr range = expr(tree.child(id, 0));
    noTemporarySince(0, "a range-based for's range");
    const Type *rt = range->type();
    if (!rt->isArray() || rt->length() < 0) outside("a range-based for over what is not an array");
    const Type *elemPtr = p_.types_.pointerTo(rt->pointee());
    const Type *size = p_.types_.get(p_.target_.sizeType());
    auto iter = [&](const std::string &name, int slot) { return typed(std::unique_ptr<Var>(Var::local(name, slot)), elemPtr); };
    std::vector<StmtPtr> setup;
    setup.push_back(make_owned<ExprStmt>(typed(make_owned<Assign>(iter(f.bName, f.bSlot), decay(std::move(range))), elemPtr)));
    ExprPtr past = pointerAdd(iter(f.bName, f.bSlot), typed(make_owned<Num>(rt->length()), size));
    setup.push_back(make_owned<ExprStmt>(typed(make_owned<Assign>(iter(f.eName, f.eSlot), std::move(past)), elemPtr)));
    ExprPtr cond = typed(make_owned<Binary>(BinOp::Ne, iter(f.bName, f.bSlot), iter(f.eName, f.eSlot)), p_.types_.get(Kind::Bool));
    ExprPtr next = pointerAdd(iter(f.bName, f.bSlot), typed(make_owned<Num>(1LL), size));
    ExprPtr step = typed(make_owned<Assign>(iter(f.bName, f.bSlot), std::move(next)), elemPtr);
    const TypedId var = tree.child(id, 1);
    std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(tree.node(var).syntax.v);
    if (l == p_.declaredLocals_.end()) outside("a range-based for's variable not recorded");
    try { plainLocal(p_, l->second); } catch (const std::string &why) { outside(why); }
    if (!l->second.staticName.empty() || ownedClass(p_, l->second.type)) outside("a range-based for's variable of a class");
    ExprPtr take;
    if (l->second.type->isReference()) {
        const Type *slotType = p_.types_.pointerTo(l->second.type->referent());
        ExprPtr slot = typed(std::unique_ptr<Var>(Var::local(l->second.name, l->second.offset)), slotType);
        take = typed(make_owned<Assign>(std::move(slot), convert(iter(f.bName, f.bSlot), slotType)), slotType);
    } else {
        ExprPtr at = typed(make_owned<Unary>('*', iter(f.bName, f.bSlot)), elemPtr->pointee());
        take = typed(make_owned<Assign>(localVar(l->second, p_.types_), classValue(convert(std::move(at), l->second.type),
                                                                                    l->second.type)), l->second.type);
    }
    std::vector<StmtPtr> body;
    body.push_back(make_owned<ExprStmt>(std::move(take)));
    loopMarks_.push_back(alive_.size());
    body.push_back(stmt(tree.child(id, 2)));
    loopMarks_.pop_back();
    std::unique_ptr<Block> inside = make_owned<Block>(std::move(body));
    inside->setScope(f.inner);
    std::unique_ptr<For> loop = make_owned<For>(StmtPtr(), std::move(cond), std::move(step), StmtPtr(std::move(inside)));
    loop->setScope(f.scope);
    setup.push_back(std::move(loop));
    std::unique_ptr<Block> whole = make_owned<Block>(std::move(setup));
    whole->setScope(-1);
    return placed(std::move(whole), syn(id), n.syntax);
}

StmtPtr Adapter::forStatement(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    const syntax::Node &s = syn(id);
    if (p_.rangeFacts_.count(n.syntax.v) != 0) return rangeFor(id);
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
            else { init = make_owned<ExprStmt>(fullExpression(expr(part), tree.node(part).syntax)); }
        } else if (loc < semi[1]) {
            cond = fullExpression(condition(part), tree.node(part).syntax);
        } else if (loc < close) {
            step = fullExpression(decay(expr(part)), tree.node(part).syntax);
        } else {
            loopMarks_.push_back(alive_.size());
            body = stmt(part);
            loopMarks_.pop_back();
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
        case syntax::Kind::WordLit:
            if (n.type == nullptr) outside("a literal not typed");
            e = typed(make_owned<Num>(static_cast<long long>(text == "true" ? 1 : 0)), n.type);
            break;
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
            const std::size_t arms = pending_.size();
            ExprPtr x = decay(expr(tree.child(id, 1))), y = decay(expr(tree.child(id, 2)));
            noTemporarySince(arms, "a conditional's arm");
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
            // A reference cast names its operand: static_cast<T &&> marks it an xvalue, const_cast retypes it.
            std::unordered_map<std::uint32_t, std::pair<const Type *, bool> >::const_iterator ref =
                p_.refCastTypes_.find(n.syntax.v);
            if (ref != p_.refCastTypes_.end() && n.count == 1) {
                e = expr(tree.child(id, 0));
                if (!isLvalue(*e) || e->isXvalue()) outside("a reference cast of what is not an lvalue");
                if (e->type()->unqualified() != ref->second.first->unqualified()) outside("a reference cast to another type");
                if (text.compare(0, 11, "const_cast<") == 0) {
                    e->setType(ref->second.first);
                } else {
                    if (e->type() != ref->second.first) outside("a static_cast to a reference leaving another type");
                    if (ref->second.second) e->setXvalue();
                    e->setDeclCategory(Expr::Category::Natural);
                }
                break;
            }
            if (to == p_.castTypes_.end() || n.count != 1) outside("a cast not recorded");
            ExprPtr v = decay(expr(tree.child(id, 0)));
            if (to->second->isVoid()) { e = typed(make_owned<Cast>(to->second, std::move(v)), to->second); break; }
            if (!scalar(to->second)) outside("a cast to '" + to->second->describe() + "'");
            if (!scalar(v->type())) outside("a cast of what is not a scalar");
            e = convert(std::move(v), to->second, true);
            break;
        }
        case syntax::Kind::Throw: {
            if (n.count == 0) { e = p_.rethrowCall(); break; }
            if (noexcept_ && tryDepth_ == 0 && handlerLoops_.empty()) outside("a throw a noexcept function ends in abort");
            // statementsAsExpression: the statements chained by commas, each typed void.
            std::vector<StmtPtr> steps = throwSteps(id);
            for (StmtPtr &step : steps) {
                ExprStmt *one = dynamic_cast<ExprStmt *>(step.get());
                if (one == nullptr) outside("a throw that is not expressions");
                ExprPtr x = one->release();
                e = e ? typed(make_owned<Comma>(std::move(e), std::move(x)), p_.types_.get(Kind::Void)) : std::move(x);
            }
            e->setType(p_.types_.get(Kind::Void));
            break;
        }
        case syntax::Kind::Lambda:
            e = lambda(id);
            break;
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
    if (!e->syntax().valid()) e->setSyntax(n.syntax);
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
    if (!plainClass(p_, obj) && !ownedClass(p_, obj)) outside("a member of '" + obj->describe() + "'");
    // [expr.ref]/4: a member of an rvalue object - `make().x` - is an xvalue.
    const bool rvalue = object->isXvalue() || !isLvalue(*object);
    if (p_.lookup_.findMemberOwner(obj->unqualified(), name) != nullptr || obj->findStaticMember(name) != nullptr)
        outside("a member '" + name + "' that is not a data member");
    const Member *m = obj->findMember(name);
    if (m == nullptr || m->ambiguous || m->inVirtualBase != nullptr || m->anonUnion != nullptr || m->type->isReference())
        outside("a member '" + name + "' the adapter does not reach");
    const Type *t = obj->isConst() && !m->isMutable ? p_.types_.withConst(m->type) : m->type;
    if (!scalar(t) && !scalarArray(t) && !byteClass(p_, t)) outside("a member of type '" + t->describe() + "'");
    ExprPtr access = typed(make_owned<MemberAccess>(std::move(object), name, m->offset, m->width, m->bitOffset), t);
    if (rvalue) access->setXvalue();
    return access;
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
    // What the parser resolved the name to, recorded where it did (M2.16): no table is asked after the file.
    std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(n.syntax.v);
    if (r == p_.resolutions_.end()) outside("the name '" + text + "'");
    const Parser::Resolved &res = r->second;
    if (res.what == Parser::Resolved::Global) {
        if (res.isThreadLocal) outside("a thread_local");
        if (!scalar(res.type) && !scalarArray(res.type) && !byteClass(p_, res.type)) outside("an object of type '" + res.type->describe() + "'");
        Var *v = Var::global(res.name.empty() ? text : res.name);
        v->setSymbol(res.symbol);
        v->setReadOnly(res.isConst);
        return typed(std::unique_ptr<Var>(v), res.type);
    }
    if (res.what == Parser::Resolved::Enumerator) {
        if (!scalarArithmetic(res.type)) outside("an enumerator of '" + res.type->describe() + "'");
        return typed(make_owned<Num>(res.value), res.type);
    }
    // [conv.func]: a function named as a value is its address.
    if (res.what == Parser::Resolved::Function) {
        const Signature *f = signatureBySymbol(res.symbol);
        if (f == nullptr || f->fromTemplate || !f->owner.empty() || f->deleted) outside("a template's or a member named as a value");
        Var *v = Var::global(res.name.empty() ? text : res.name);
        v->setSymbol(f->symbol);
        const Type *fn = p_.types_.functionType(f->returns, f->params, f->variadic);
        ExprPtr target = typed(std::unique_ptr<Var>(v), fn);
        return typed(make_owned<Unary>('&', std::move(target)), p_.types_.pointerTo(fn));
    }
    outside("the name '" + text + "', " + (res.what == Parser::Resolved::Local ? "a local the typer did not see declared"
                                            : res.what == Parser::Resolved::Member ? "a member reached through this"
                                            : res.what == Parser::Resolved::StaticMember ? "a static member" : "of another kind"));
    return ExprPtr();
}

// The function the parser recorded a call node as reaching, by its symbol; null where it recorded none.
const Signature *Adapter::recordedFunction(TypedId id) const {
    std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(typer_.tree().node(id).syntax.v);
    if (r == p_.resolutions_.end() || r->second.what != Parser::Resolved::Call) return nullptr;
    return signatureBySymbol(r->second.symbol);
}

// The signature a recorded symbol names, from the one function table; null where the symbol is nobody's.
const Signature *Adapter::signatureBySymbol(const std::string &symbol) const {
    if (bySymbol_.empty())
        for (std::size_t i = 0; i < p_.lookup_.functions_.size(); i++) bySymbol_[p_.lookup_.functions_[i].symbol] = i;
    std::unordered_map<std::string, std::size_t>::const_iterator at = bySymbol_.find(symbol);
    return at == bySymbol_.end() ? nullptr : &p_.lookup_.functions_[at->second];
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
    ExprPtr a = expr(l);
    const std::size_t mark = pending_.size();
    ExprPtr b = expr(r);
    // Parser::markSkippableTemporaries guards what an operand that may be skipped builds.
    if (op == "&&" || op == "||") noTemporarySince(mark, "an operand that may be skipped");
    return arithmetic(op, std::move(a), std::move(b));
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
    if (!(scalar(t) || (op == "=" && byteClass(p_, t))) || !lvalueTarget(*target))
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
    if (syn(head).kind == syntax::Kind::Name && tree.node(head).decl.valid() && tree.node(head).type != nullptr &&
        tree.node(head).type->isStructOrUnion())
        return objectCall(id, head, std::move(args));
    if (syn(head).kind != syntax::Kind::Name || tree.node(head).decl.valid()) return pointerCall(id, head, std::move(args));
    std::string callee = p_.syntaxTree_.text(tree.node(head).syntax);
    if (callee.compare(0, 2, "::") == 0) callee = callee.substr(2);
    const std::string written = callee;
    // What the parser reached from this call, recorded on its node where it was resolved (M2.16): a function by its
    // symbol, a class temporary by its class, or - a call through a global holding a pointer - nothing.
    std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(n.syntax.v);
    if (r != p_.resolutions_.end() && r->second.what == Parser::Resolved::Temporary)
        return classTemporary(id, r->second.type->unqualified(), std::move(args));
    if (r == p_.resolutions_.end() || r->second.what != Parser::Resolved::Call || r->second.symbol.empty()) {
        std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator h = p_.resolutions_.find(tree.node(head).syntax.v);
        if (h != p_.resolutions_.end() && h->second.what == Parser::Resolved::Global) return pointerCall(id, head, std::move(args));
        outside("a call the parser recorded no function for");
    }
    const Signature *chosen = signatureBySymbol(r->second.symbol);
    if (chosen == nullptr) outside("a call of a function not in the table by its symbol");
    const Signature f = *chosen;
    if (p_.tmpl_.memberTemplates_.count(callee) != 0) outside("a call of a name a member template shares");
    if (f.fromTemplate) {
        if (f.deleted || !f.owner.empty()) outside("a call of a template's member or deleted function");
        return finishCall(id, f.name, f, f.params, std::move(args), false);
    }
    const bool qualified = callee.find("::") != std::string::npos;
    if (f.deleted) outside("a call of a deleted function");
    if (!f.owner.empty() && !(qualified && f.isStaticMember)) outside("a call of a member by its name");
    if (!f.owner.empty()) {
        const Type *cls = p_.lookup_.findTypedef(f.owner);
        if (cls == nullptr || !plainClass(p_, cls)) outside("a static member of what is not a plain struct");
    }
    if (callee.find_first_of("<~") != std::string::npos) outside("a call of '" + callee + "'");
    return finishCall(id, written, f, f.params, std::move(args), false);
}

// Parser::classTemporary for a class copied by its bytes: `T(a)` is `*(ctor(&$tmp, a), &$tmp)`, or a copy of one
// T into the slot where T has no copy of its own, an expiring object either way; the slot is the parser's, by node.
ExprPtr Adapter::classTemporary(TypedId id, const Type *cls, std::vector<ExprPtr> args) {
    const Type *plain = cls->unqualified();
    const bool dies = dyingBytes(p_, plain);
    if (!(byteClass(p_, plain) || dies) || !p_.hasConstructors(plain->tag())) outside("a temporary of what is not a class with constructors");
    std::unordered_map<std::uint32_t, int>::const_iterator k = p_.temporarySlots_.find(typer_.tree().node(id).syntax.v);
    if (k == p_.temporarySlots_.end()) outside("a temporary whose slot was not recorded");
    const int slot = k->second;
    const Type *ptr = p_.types_.pointerTo(plain);
    ExprPtr build;
    if (args.size() == 1 && args[0]->type()->unqualified() == plain) {
        if (dies) outside("a temporary copied from one of its class with a destructor");
        ExprPtr obj = typed(std::unique_ptr<Var>(Var::local("$tmp", slot)), plain);
        obj->setXvalue();
        obj->setDeclCategory(Expr::Category::Prvalue);
        build = typed(make_owned<Assign>(std::move(obj), std::move(args[0])), plain);
    } else {
        for (std::size_t i = 0; i < args.size(); i++)
            if (!scalar(args[i]->type())) outside("a temporary given what is not a scalar");
        if (args.empty()) outside("a temporary value-initialised");
        std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(typer_.tree().node(id).syntax.v);
        const Signature *chosen = r != p_.resolutions_.end() ? signatureBySymbol(r->second.symbol) : nullptr;
        if (chosen == nullptr) outside("a temporary whose constructor the parser did not record");
        const Signature ctor = *chosen;
        if (ctor.deleted || args.size() != ctor.params.size()) outside("a constructor with defaults, or deleted");
        std::vector<ExprPtr> all;
        all.push_back(typed(make_owned<Unary>('&', typed(std::unique_ptr<Var>(Var::local("$tmp", slot)), plain)), ptr));
        for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));
        std::vector<const Type *> full(1, ptr);
        full.insert(full.end(), ctor.params.begin(), ctor.params.end());
        const Signature made(plain->tag(), ctor.symbol, p_.types_.get(Kind::Void), full, false);
        build = finishCall(id, plain->tag(), made, full, std::move(all), false);
    }
    // Parser::constructTemporary: the object exists once its constructor returns, which its guard says.
    if (dies) {
        std::unordered_map<std::uint32_t, int>::const_iterator g = p_.temporaryGuards_.find(typer_.tree().node(id).syntax.v);
        if (g == p_.temporaryGuards_.end() || g->second == 0) outside("a temporary whose guard was not recorded");
        build = typed(make_owned<Comma>(std::move(build), setGuard(g->second, 1)), p_.types_.intType());
        pending_.push_back(Pending{ slot, plain, g->second });
    }
    ExprPtr at = typed(make_owned<Unary>('&', typed(std::unique_ptr<Var>(Var::local("$tmp", slot)), plain)), ptr);
    ExprPtr both = typed(make_owned<Comma>(std::move(build), std::move(at)), ptr);
    ExprPtr made = typed(make_owned<Unary>('*', std::move(both)), plain);
    made->setXvalue();
    made->setDeclCategory(Expr::Category::Prvalue);
    return made;
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

// [over.call]: `o(a)` for a local of a class is `o.operator()(a)` - a closure's call above all.
ExprPtr Adapter::objectCall(TypedId id, TypedId head, std::vector<ExprPtr> args) {
    ExprPtr object = expr(head);
    const Type *cls = object->type();
    if (!pure(*object) || !isLvalue(*object)) outside("a call of an object not named");
    const Type *plain = cls->unqualified();
    const std::string key = plain->tag() + "::operator()";
    if (p_.tmpl_.memberTemplates_.count(key) != 0) outside("a call of an object with no plain operator()");
    const Signature *chosen = recordedFunction(id);
    if (chosen == nullptr || chosen->fromTemplate) outside("a call of a template's operator(), or one not recorded");
    const Signature f = *chosen;
    if (f.deleted || f.isVirtual || f.refThis != RefQual::None || f.isStaticMember || f.owner != plain->tag())
        outside("an operator() that is not plain");
    const Type *thisType = p_.types_.pointerTo(f.constThis ? p_.types_.withConst(plain) : plain);
    std::vector<ExprPtr> all;
    all.push_back(typed(make_owned<Unary>('&', std::move(object)), thisType));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));
    std::vector<const Type *> full(1, thisType);
    full.insert(full.end(), f.params.begin(), f.params.end());
    return finishCall(id, "operator()", f, full, std::move(all), true);
}

// [expr.prim.lambda]: the closure in the slot the parser chose, each capture copied or bound into its member.
ExprPtr Adapter::lambda(TypedId id) {
    std::unordered_map<std::uint32_t, Parser::LambdaFacts>::const_iterator k =
        p_.lambdaFacts_.find(typer_.tree().node(id).syntax.v);
    if (k == p_.lambdaFacts_.end()) outside("a lambda whose closure was not recorded");
    const Parser::LambdaFacts &f = k->second;
    if (f.thisOffset >= 0) outside("a lambda capturing `this`");
    const Type *cls = f.type;
    ExprPtr e;
    if (f.captures.empty()) {
        e = typed(std::unique_ptr<Var>(Var::local(f.slotName, f.slot)), cls);
    } else {
        ExprPtr chain;
        for (std::size_t i = 0; i < f.captures.size(); i++) {
            const Parser::LambdaCapture &c = f.captures[i];
            Var *v = Var::local(c.src, c.srcOffset);
            v->setReadOnly(c.srcConst);
            ExprPtr src = typed(std::unique_ptr<Var>(v), c.srcType);
            const bool byRef = c.type->isReference();
            const Type *memberType = byRef ? p_.types_.pointerTo(c.type->referent()) : c.type;
            ExprPtr self = typed(std::unique_ptr<Var>(Var::local(f.slotName, f.slot)), cls);
            ExprPtr dst = typed(make_owned<MemberAccess>(std::move(self), c.member, c.offset, 0, 0), memberType);
            if (byRef) src = typed(make_owned<Unary>('&', std::move(src)), memberType);
            ExprPtr store = typed(make_owned<Assign>(std::move(dst), std::move(src)), memberType);
            chain = chain ? typed(make_owned<Comma>(std::move(chain), std::move(store)), memberType) : std::move(store);
        }
        ExprPtr whole = typed(std::unique_ptr<Var>(Var::local(f.slotName, f.slot)), cls);
        ExprPtr at = typed(make_owned<Unary>('&', std::move(whole)), p_.types_.pointerTo(cls));
        ExprPtr seq = typed(make_owned<Comma>(std::move(chain), std::move(at)), p_.types_.pointerTo(cls));
        e = typed(make_owned<Unary>('*', std::move(seq)), cls);
    }
    e->setXvalue();
    e->setDeclCategory(Expr::Category::Prvalue);
    return e;
}

// [expr.ref] with [class.mfct.non-static]: `o.f(a)` is f called with `&o` in front of its arguments.
ExprPtr Adapter::memberCall(TypedId id, TypedId head, std::vector<ExprPtr> args) {
    const TypedTree &tree = typer_.tree();
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
    if ((!plainClass(p_, cls) && !ownedClass(p_, cls)) || !pure(*object) || object->isXvalue() || !isLvalue(*object))
        outside("a member call on what is not a plain struct named");
    const Type *plain = cls->unqualified();
    const std::string key = plain->tag() + "::" + name;
    if (p_.tmpl_.memberTemplates_.count(key) != 0) outside("a member call of what is not a function");
    const Signature *chosen = recordedFunction(id);
    if (chosen == nullptr || chosen->fromTemplate) outside("a member call of a template's, or one not recorded");
    const Signature f = *chosen;
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
    const bool claimed = claimedSlot_ != 0 && dyingBytes(p_, gives);
    claimedSlot_ = 0;
    const bool dies = gives != nullptr && !f.returns->isReference() && dyingBytes(p_, gives) && !claimed;
    if (gives == nullptr || !(gives->isVoid() || scalar(gives) || byteClass(p_, gives) || claimed || dies))
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
        if (byteClass(p_, params[i])) {
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
    if (claimed && slots.result != 0 && f.returns->isReference()) outside("a reference returned where a class was claimed");
    std::unique_ptr<Call> c = make_owned<Call>(name, std::move(callee), std::move(args), f.variadic, slots.result,
                                               static_cast<int>(params.size()), slots.args);
    c->setSymbol(f.symbol);
    c->setHasThis(hasThis);
    c->setResultIndirect(slots.indirect);
    // Parser::completeCall: a result with a destructor dies at the end of the full expression, unless claimed.
    if (dies) pending_.push_back(Pending{ slots.result, gives->unqualified(), 0 });
    if (!f.returns->isReference()) return typed(std::move(c), f.returns);
    ExprPtr through = typed(std::move(c), p_.types_.pointerTo(gives));
    return typed(make_owned<Unary>('*', std::move(through)), gives);
}

// [dcl.init.ref]/5 as Parser::bindReference does it: a glvalue of the referent's type, its address taken; else a
// scalar a const or rvalue reference binds, copied into the frame slot the parser chose, `(.refN = v, &.refN)`.
ExprPtr Adapter::bind(const Type *ref, ExprPtr init) {
    const Type *referent = ref->referent(), *it = init->type();
    if (referent->isFunction() || referent->isArray()) outside("a binding of '" + ref->describe() + "'");
    const MemberAccess *field = dynamic_cast<const MemberAccess *>(init.get());
    const Var *var = dynamic_cast<const Var *>(init.get());
    const Unary *deref = dynamic_cast<const Unary *>(init.get());
    const bool glvalue = (var != nullptr && !var->noAddress()) || (field != nullptr && field->width() == 0) ||
                         (deref != nullptr && deref->op() == '*');
    if (glvalue && it->unqualified() == referent->unqualified()) {
        if (it->isConst() && !referent->isConst()) outside("a reference that can write bound to a const");
        return typed(make_owned<Unary>('&', std::move(init)), p_.types_.pointerTo(referent));
    }
    if (glvalue && !init->isXvalue() && ref->isRValueReference()) outside("an rvalue reference bound to an lvalue");
    if (!referent->isConst() && !ref->isRValueReference()) outside("a reference bound to what is not an lvalue of its type");
    if (!scalar(it) || !scalar(referent)) outside("a reference bound to a temporary of a class");
    std::unordered_map<std::uint32_t, std::pair<std::string, int> >::const_iterator k =
        p_.bindTemps_.find(init->syntax().v);
    if (!init->syntax().valid() || k == p_.bindTemps_.end()) outside("a reference's temporary not recorded");
    const Type *store = referent->unqualified();
    ExprPtr keep = typed(make_owned<Assign>(typed(std::unique_ptr<Var>(Var::local(k->second.first, k->second.second)), store),
                                            convert(std::move(init), store)), store);
    ExprPtr addr = typed(make_owned<Unary>('&', typed(std::unique_ptr<Var>(Var::local(k->second.first, k->second.second)),
                                                       store)), p_.types_.pointerTo(referent));
    return typed(make_owned<Comma>(std::move(keep), std::move(addr)), p_.types_.pointerTo(referent));
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
    Typer typer(p);
    const Census c = Typer::census(p, program);
    std::map<std::string, int> reasons;
    std::string out;
    int adapted = 0, same = 0;
    *differ = 0;
    for (std::size_t r = 0; r < c.bodies.size(); r++) {
        Function &fn = program.functions[c.bodies[r].second];
        const TypedId t = typer.typeFunction(c.bodies[r].first);
        Adapter adapter(p, typer);
        std::string why;
        // A member's, a namespace's or a template's body names things the adapter's subset never reached (M2.15).
        StmtPtr b = p.scopedFunctions_.count(c.bodies[r].first.v) != 0 ? StmtPtr() : adapter.body(t, fn, &why);
        if (!b && why.empty()) why = "a body whose names are a class's, a namespace's or a template's";
        if (!b) {
            reasons[why]++;
            // CXX1_ADAPT_WHY names the function each reason was met in, for finding the next slice.
            if (std::getenv("CXX1_ADAPT_WHY") != nullptr) std::fprintf(stderr, "outside %s: %s\n", fn.symbol().c_str(), why.c_str());
            continue;
        }
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
        char line[240];
        std::snprintf(line, sizeof line, "-check=adapted: %s adapted=%d same=%d differ=%d outside=%zu\n",
                      Typer::censusLine(c).c_str(), adapted, same, *differ, c.bodies.size() - adapted);
        out += line + Typer::censusKinds(c);
    }
    return out;
}

void Adapter::endCatches(std::vector<StmtPtr> &into, int count) const {
    for (int i = 0; i < count; i++)
        into.push_back(make_owned<ExprStmt>(p_.runtimeCall("__cxa_end_catch", p_.types_.get(Kind::Void), std::vector<ExprPtr>())));
}

// Parser::throwStatement for a scalar on the Itanium targets: the storage asked for, the value stored, the throw.
std::vector<StmtPtr> Adapter::throwSteps(TypedId id) {
    std::unordered_map<std::uint32_t, Parser::ThrowFacts>::const_iterator f = p_.throwFacts_.find(typer_.tree().node(id).syntax.v);
    if (f == p_.throwFacts_.end()) outside("a throw the adapter cannot rebuild");
    ExprPtr value = decay(expr(typer_.tree().child(id, 0)));
    noTemporarySince(0, "a throw");
    const Type *thrown = value->type()->unqualified();
    const bool bytes = thrown->isStructOrUnion() && byteClass(p_, thrown);
    if (!scalar(thrown) && !bytes) outside("a throw of what is not a scalar");
    const Type *voidPtr = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *thrownPtr = p_.types_.pointerTo(thrown);
    std::vector<ExprPtr> sizeArg;
    sizeArg.push_back(typed(make_owned<Num>(static_cast<long long>(thrown->size(p_.target_))), p_.types_.get(p_.target_.sizeType())));
    ExprPtr got = p_.runtimeCall("__cxa_allocate_exception", voidPtr, std::move(sizeArg));
    ExprPtr save = typed(make_owned<Assign>(typed(std::unique_ptr<Var>(Var::local(f->second.temp, f->second.slot)), voidPtr),
                                            std::move(got)), voidPtr);
    ExprPtr cast = typed(make_owned<Cast>(thrownPtr, typed(std::unique_ptr<Var>(Var::local(f->second.temp, f->second.slot)), voidPtr)),
                         thrownPtr);
    ExprPtr stored = bytes ? classValue(std::move(value), thrown) : convert(std::move(value), thrown);
    ExprPtr store = typed(make_owned<Assign>(typed(make_owned<Unary>('*', std::move(cast)), thrown), std::move(stored)), thrown);
    std::vector<ExprPtr> args;
    args.push_back(typed(std::unique_ptr<Var>(Var::local(f->second.temp, f->second.slot)), voidPtr));
    Var *ti = Var::global(f->second.info);
    ti->setSymbol(f->second.info);
    args.push_back(typed(make_owned<Unary>('&', typed(std::unique_ptr<Var>(ti), p_.types_.get(Kind::Char))), voidPtr));
    args.push_back(typed(make_owned<Num>(0LL), voidPtr));
    std::vector<StmtPtr> steps;
    steps.push_back(make_owned<ExprStmt>(typed(make_owned<Comma>(std::move(save), std::move(store)), p_.types_.get(Kind::Void))));
    steps.push_back(make_owned<ExprStmt>(p_.runtimeCall("__cxa_throw", p_.types_.get(Kind::Void), std::move(args))));
    return steps;
}

// Parser::tryStatement for a `try` with nothing alive round it and nothing round it: the body, then a chain on the
// selector whose arms begin the catch, bind the caught object, run the handler and end the catch.
StmtPtr Adapter::tryStatement(TypedId id) {
    const TypedTree &tree = typer_.tree();
    const TypedNode &n = tree.node(id);
    std::unordered_map<std::uint32_t, Parser::TryFacts>::const_iterator k = p_.tryFacts_.find(n.syntax.v);
    if (k == p_.tryFacts_.end()) outside("a try the adapter cannot rebuild");
    const Parser::TryFacts &facts = k->second;
    if (alive_.size() != facts.aliveOutside || tryDepth_ > 0 || !handlerLoops_.empty())
        outside("a try inside another, or with other objects alive");
    if (n.count != 1 + facts.handlers.size() || facts.types.size() != facts.handlers.size())
        outside("a try whose handlers were not recorded");
    const Type *voidPtr = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *voidType = p_.types_.get(Kind::Void);
    Handover chainTo;
    chainTo.label = facts.chain;
    chainTo.ptr = facts.ptr;
    chainTo.sel = facts.sel;
    chainTo.aliveFrom = facts.aliveOutside;
    chainTo.chain = true;
    handovers_.push_back(chainTo);
    tryDepth_++;
    StmtPtr body = stmt(tree.child(id, 0));
    tryDepth_--;
    std::vector<Try *> segments = handovers_.back().segments;
    handovers_.pop_back();
    std::vector<StmtPtr> handlers;
    for (std::size_t i = 0; i < facts.handlers.size(); i++) {
        const Parser::TryHandlerFacts &hf = facts.handlers[i];
        const TypedId c = tree.child(id, static_cast<std::uint32_t>(i + 1));
        const TypedNode &cn = tree.node(c);
        if (syn(c).kind != syntax::Kind::Catch || cn.count == 0 || cn.count > 2) outside("a handler not as recorded");
        std::vector<StmtPtr> steps;
        std::vector<ExprPtr> beginArgs;
        beginArgs.push_back(typed(std::unique_ptr<Var>(Var::local(".ex.ptr", facts.ptr)), voidPtr));
        ExprPtr began = p_.runtimeCall("__cxa_begin_catch", voidPtr, std::move(beginArgs));
        if (cn.count == 2) {
            const TypedId var = tree.child(c, 0);
            std::unordered_map<std::uint32_t, Local>::const_iterator l = p_.declaredLocals_.find(tree.node(var).syntax.v);
            if (syn(var).kind != syntax::Kind::Var || l == p_.declaredLocals_.end()) outside("a caught object not recorded");
            const Local &caught = l->second;
            ExprPtr from;
            const Type *slotType = caught.type->isReference() ? p_.types_.pointerTo(caught.type->referent())
                                                              : p_.types_.pointerTo(caught.type);
            ExprPtr cast = typed(make_owned<Cast>(slotType, std::move(began)), slotType);
            const Type *as = caught.type->isReference() ? slotType : caught.type;
            if (caught.type->isReference()) {
                from = std::move(cast);
            } else if (caught.type->isPointer()) {
                if (p_.target_.beginCatchReturnsObject())
                    from = typed(make_owned<Unary>('*', typed(make_owned<Cast>(slotType, std::move(cast)), slotType)), caught.type);
                else
                    from = typed(make_owned<Cast>(caught.type, std::move(cast)), caught.type);
            } else {
                from = typed(make_owned<Unary>('*', std::move(cast)), caught.type);
            }
            ExprPtr to = typed(std::unique_ptr<Var>(Var::local(caught.name, caught.offset)), as);
            steps.push_back(make_owned<ExprStmt>(typed(make_owned<Assign>(std::move(to), std::move(from)), as)));
        } else {
            steps.push_back(make_owned<ExprStmt>(std::move(began)));
        }
        Handover endTo;
        endTo.label = hf.endLabel;
        endTo.ptr = hf.padPtr;
        endTo.sel = hf.padSel;
        handovers_.push_back(endTo);
        handlerLoops_.push_back(loopMarks_.size());
        StmtPtr handlerBody = stmt(tree.child(c, cn.count - 1));
        handlerLoops_.pop_back();
        handovers_.pop_back();
        // An exception leaving the handler ends the catch in a pad of its own, and unwinds on.
        if (hf.canThrow) {
            std::vector<StmtPtr> guarded;
            guarded.push_back(std::move(handlerBody));
            std::vector<StmtPtr> padSteps;
            padSteps.push_back(make_owned<ExprStmt>(p_.runtimeCall("__cxa_end_catch", voidType, std::vector<ExprPtr>())));
            if (facts.aliveOutside > facts.owesFrom) destroy(padSteps, facts.owesFrom, facts.aliveOutside);
            padSteps.push_back(p_.resumeUnwinding(hf.padPtr));
            StmtPtr pad = make_owned<Label>(hf.endLabel, p_.unwindPad(std::move(padSteps)));
            steps.push_back(make_owned<Try>(std::move(guarded), std::move(pad), hf.padPtr, hf.padSel, std::vector<std::string>()));
        } else {
            steps.push_back(std::move(handlerBody));
        }
        steps.push_back(make_owned<ExprStmt>(p_.runtimeCall("__cxa_end_catch", voidType, std::vector<ExprPtr>())));
        std::unique_ptr<Block> b = make_owned<Block>(std::move(steps));
        b->setScope(hf.scope);
        handlers.push_back(std::move(b));
    }
    // What nothing matched unwinds on; each handler with a type tests the selector in front of what came after it.
    std::vector<StmtPtr> resume;
    if (facts.unwindsHere && facts.aliveOutside > facts.owesFrom) destroy(resume, facts.owesFrom, facts.aliveOutside);
    resume.push_back(p_.resumeUnwinding(facts.ptr));
    StmtPtr chain = p_.unwindPad(std::move(resume));
    for (std::size_t i = handlers.size(); i-- > 0; ) {
        if (facts.types[i].empty()) { chain = std::move(handlers[i]); continue; }
        ExprPtr sel = typed(std::unique_ptr<Var>(Var::local(".ex.sel", facts.sel)), p_.types_.intType());
        ExprPtr want = typed(make_owned<Num>(static_cast<long long>(facts.indices[i])), p_.types_.intType());
        ExprPtr test = typed(make_owned<Binary>(BinOp::Eq, std::move(sel), std::move(want)), p_.types_.intType());
        chain = make_owned<If>(std::move(test), std::move(handlers[i]), std::move(chain));
    }
    std::vector<StmtPtr> guarded;
    guarded.push_back(std::move(body));
    std::unique_ptr<Try> t = make_owned<Try>(std::move(guarded), make_owned<Label>(facts.chain, std::move(chain)), facts.ptr,
                                             facts.sel, facts.types);
    t->setTypeIndices(facts.indices);
    if (facts.unwindsHere) t->setAlsoCleanup();
    // Every segment of the body carries this `try`'s catch types, and hands its selector to the chain.
    for (Try *seg : segments) {
        seg->setTypes(facts.types);
        seg->setTypeIndices(facts.indices);
        seg->setAlsoCleanup();
    }
    return placed(std::move(t), syn(id), n.syntax);
}

}   // namespace sema
