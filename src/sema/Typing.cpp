// sema::Typer - see Typing.h.

#include "Typing.h"
#include "../parser/Parser.h"
#include "../Source.h"

#include <cstdio>

namespace sema {

TypedId TypedTree::add(syntax::NodeId syn, const Type *type, const std::vector<TypedId> &children) {
    TypedNode n;
    n.syntax = syn;
    n.type = type;
    n.first = static_cast<std::uint32_t>(children_.size());
    n.count = static_cast<std::uint32_t>(children.size());
    children_.insert(children_.end(), children.begin(), children.end());
    nodes_.push_back(n);
    return TypedId(static_cast<std::uint32_t>(nodes_.size() - 1));
}

TypedId Typer::typeFunction(syntax::NodeId fn) {
    scopes_.clear();
    localOnly_ = p_.scopedFunctions_.count(fn.v) != 0;
    return node(fn);
}

static bool isExpression(syntax::Kind k) {
    return k < syntax::Kind::Block;     // the expression kinds come first in syntax::Kind
}

static bool opensScope(syntax::Kind k) {
    return k == syntax::Kind::Function || k == syntax::Kind::Block || k == syntax::Kind::For ||
           k == syntax::Kind::If || k == syntax::Kind::While || k == syntax::Kind::Switch;
}

TypedId Typer::node(syntax::NodeId id) {
    const syntax::Tree &st = p_.syntaxTree_;
    const syntax::Node &n = st.node(id);
    const bool scoped = opensScope(n.kind);
    if (scoped) scopes_.push_back(std::unordered_map<std::string, const Type *>());
    // A variable is in scope from its declarator on, its own initialiser included - [basic.scope.pdecl]/1.
    if (n.kind == syntax::Kind::Var && !scopes_.empty()) {
        std::unordered_map<std::uint32_t, const Type *>::const_iterator t = p_.declaredTypes_.find(id.v);
        const std::string &text = st.text(id);
        const std::string::size_type space = text.rfind(' ');
        if (t != p_.declaredTypes_.end() && space != std::string::npos) scopes_.back()[text.substr(space + 1)] = t->second;
    }
    std::vector<TypedId> kids;
    std::vector<const Type *> parts;
    for (std::uint32_t i = 0; i < n.count; i++) {
        const TypedId k = node(st.child(id, i));
        kids.push_back(k);
        parts.push_back(tree_.node(k).type);
    }
    const Type *type = isExpression(n.kind) ? expression(id, parts) : nullptr;
    if (scoped) scopes_.pop_back();
    return tree_.add(id, type, kids);
}

// [lex.icon] table 6 and [lex.fcon]: a literal's type from its token, the decimal ladder and the hex or octal one.
const Type *Typer::literal(syntax::NodeId id) {
    std::unordered_map<std::uint32_t, std::size_t>::const_iterator at = p_.literalTokens_.find(id.v);
    if (at == p_.literalTokens_.end()) return nullptr;
    const Token &t = p_.tokens_[at->second];
    TypeTable &types = p_.types_;
    if (t.kind == TokenKind::Str) {
        const bool alone = at->second + 1 >= p_.tokens_.size() || p_.tokens_[at->second + 1].kind != TokenKind::Str;
        if (!alone || t.wide || t.prefix != 0) return nullptr;     // adjacent pieces and wide units: not yet
        return types.arrayOf(types.withConst(types.get(Kind::Char)), static_cast<long long>(t.text.size()) + 1);
    }
    if (t.isFloat) return types.get(t.suffixF ? Kind::Float : t.suffixL ? Kind::LongDouble : Kind::Double);
    if (t.prefix == 'u') return types.get(Kind::Char16);
    if (t.prefix == 'U') return types.get(Kind::Char32);
    if (t.wide) return types.get(Kind::WChar);
    if (t.isChar && !t.suffixU && !t.suffixL && !t.suffixLL) return types.get(Kind::Char);
    const unsigned long long u = static_cast<unsigned long long>(t.value);
    const Target &target = p_.target_;
    struct Fits {
        TypeTable &types; const Target &target; unsigned long long u;
        bool operator()(Kind k) const {
            const Type *c = types.get(k);
            const int bits = c->size(target) * 8;
            const unsigned long long limit = c->isSigned(target) ? (1ULL << (bits - 1)) - 1
                                           : bits >= 64 ? ~0ULL : (1ULL << bits) - 1;
            return u <= limit;
        }
    } fits = { types, target, u };
    std::vector<Kind> ladder;
    if (t.suffixU && t.suffixLL) ladder.push_back(Kind::ULongLong);
    else if (t.suffixLL) { ladder.push_back(Kind::LongLong); ladder.push_back(Kind::ULongLong); }
    else if (t.suffixU && t.suffixL) { ladder.push_back(Kind::ULong); ladder.push_back(Kind::ULongLong); }
    else if (t.suffixU) { ladder.push_back(Kind::UInt); ladder.push_back(Kind::ULong); ladder.push_back(Kind::ULongLong); }
    else if (t.suffixL) {
        ladder.push_back(Kind::Long);
        if (!t.decimal) ladder.push_back(Kind::ULong);
        ladder.push_back(Kind::LongLong);
        ladder.push_back(Kind::ULongLong);
    } else {
        ladder.push_back(Kind::Int);
        if (!t.decimal) ladder.push_back(Kind::UInt);
        ladder.push_back(Kind::Long);
        if (!t.decimal) ladder.push_back(Kind::ULong);
        ladder.push_back(Kind::LongLong);
        ladder.push_back(Kind::ULongLong);
    }
    for (std::size_t i = 0; i + 1 < ladder.size(); i++)
        if (fits(ladder[i])) return types.get(ladder[i]);
    return types.get(ladder.back());
}

// A name, innermost scope first, then the namespace's objects and enumerators. A reference reads as what it refers to.
const Type *Typer::named(const std::string &name) {
    const Type *t = nullptr;
    for (std::size_t i = scopes_.size(); t == nullptr && i > 0; i--) {
        std::unordered_map<std::string, const Type *>::const_iterator it = scopes_[i - 1].find(name);
        if (it != scopes_[i - 1].end()) t = it->second;
    }
    if (t == nullptr && localOnly_) return nullptr;
    if (t == nullptr)
        if (const GlobalSym *g = p_.lookup_.findGlobal(name)) t = g->type;
    if (t == nullptr)
        if (const EnumConst *e = p_.lookup_.findEnum(name)) t = e->type != nullptr ? e->type : p_.types_.intType();
    if (t != nullptr && t->isReference()) t = t->referent();
    return t;
}

static bool isClass(const Type *t) { return t->unqualified()->isStructOrUnion(); }

static bool scalarOrArray(const Type *t) { return t != nullptr && (t->isArithmetic() || t->isPointer() || t->isArray()); }

const Type *Typer::expression(syntax::NodeId id, const std::vector<const Type *> &parts) {
    const syntax::Tree &st = p_.syntaxTree_;
    const syntax::Kind kind = st.node(id).kind;
    const std::string &op = st.text(id);
    TypeTable &types = p_.types_;
    const Type *a = parts.size() > 0 ? parts[0] : nullptr;
    const Type *b = parts.size() > 1 ? parts[1] : nullptr;
    // An array operand of arithmetic is its first element's address.
    const Type *da = a != nullptr && a->isArray() ? types.pointerTo(a->pointee()) : a;
    const Type *db = b != nullptr && b->isArray() ? types.pointerTo(b->pointee()) : b;
    switch (kind) {
        case syntax::Kind::IntLit: case syntax::Kind::FloatLit: case syntax::Kind::CharLit: case syntax::Kind::StringLit:
            return literal(id);
        case syntax::Kind::Name:
            return op.find("::") == std::string::npos ? named(op) : nullptr;
        case syntax::Kind::Unary:
            if (a == nullptr || isClass(a)) return nullptr;     // a class's operator is overload resolution's
            if (op == "!") return types.get(Kind::Bool);
            if ((op == "+" || op == "-") && a->isArithmetic()) return p_.promote(a);
            if (op == "~" && a->isInteger()) return p_.promote(a);
            if (op == "*" && da->isPointer() && !da->pointee()->isFunction()) return da->pointee();
            if (op == "&" && !a->isFunction()) return types.pointerTo(a);
            if ((op == "++" || op == "--") && (a->isArithmetic() || a->isPointer())) return a;
            return nullptr;
        case syntax::Kind::Postfix:
            return a != nullptr && (a->isArithmetic() || a->isPointer()) ? a->unqualified() : nullptr;
        case syntax::Kind::Binary:
            if (a == nullptr || b == nullptr || isClass(a) || isClass(b)) return nullptr;
            if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
                op == "&&" || op == "||")
                return scalarOrArray(a) && scalarOrArray(b) ? types.get(Kind::Bool) : nullptr;
            if (op == "<<" || op == ">>") return a->isInteger() && b->isInteger() ? p_.promote(a) : nullptr;
            if (a->isArithmetic() && b->isArithmetic()) return p_.usualArithmetic(a, b);
            if ((op == "+" || op == "-") && da->isPointer() && b->isInteger()) return da;
            if (op == "+" && a->isInteger() && db->isPointer()) return db;
            if (op == "-" && da->isPointer() && db->isPointer()) return p_.ptrdiffType();
            return nullptr;
        case syntax::Kind::Assign:
            return a != nullptr && (a->isArithmetic() || a->isPointer()) ? a : nullptr;
        case syntax::Kind::Comma:
            return b;
        case syntax::Kind::Conditional: {
            const Type *x = parts.size() > 1 ? parts[1] : nullptr, *y = parts.size() > 2 ? parts[2] : nullptr;
            if (x == nullptr || y == nullptr || isClass(x) || isClass(y)) return nullptr;
            if (x->unqualified() == y->unqualified()) return x->unqualified();     // [expr.cond]/6: one type
            if (x->isArithmetic() && y->isArithmetic()) return p_.usualArithmetic(x, y);
            return nullptr;
        }
        case syntax::Kind::Subscript:
            if (da != nullptr && da->isPointer() && b != nullptr && b->isInteger()) return da->pointee();
            if (db != nullptr && db->isPointer() && a != nullptr && a->isInteger()) return db->pointee();
            return nullptr;
        case syntax::Kind::Call: {
            // A call of a name with one function under it, not a template's: its return type.
            const syntax::NodeId callee = st.node(id).count > 0 ? st.child(id, 0) : syntax::NodeId();
            if (localOnly_ || !callee.valid() || st.node(callee).kind != syntax::Kind::Name) return nullptr;
            const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(st.text(callee));
            if (set == nullptr || set->size() != 1) return nullptr;
            const Signature &f = p_.lookup_.functions_[(*set)[0]];
            if (f.fromTemplate || f.returns == nullptr) return nullptr;
            return f.returns->isReference() ? f.returns->referent() : f.returns;
        }
        case syntax::Kind::Cast: {
            std::unordered_map<std::uint32_t, const Type *>::const_iterator to = p_.castTypes_.find(id.v);
            if (to == p_.castTypes_.end()) return nullptr;
            return to->second->isReference() ? to->second->referent() : to->second;
        }
        case syntax::Kind::Sizeof:
            if (op.compare(0, 6, "sizeof") == 0 || op.compare(0, 7, "alignof") == 0)
                return types.get(p_.target_.sizeType());
            if (op.compare(0, 8, "noexcept") == 0) return types.get(Kind::Bool);
            return nullptr;
        default:
            return nullptr;
    }
}

namespace {

// The type the parser gave each syntax node: the innermost Ast.h expression carrying it, which a conversion that wrapped
// it - and so carries it too - sits outside of.
struct Collect final : Visitor {
    std::unordered_map<std::uint32_t, const Type *> types;
    void note(const Expr &e) { if (e.syntax().valid() && e.type() != nullptr) types[e.syntax().v] = e.type(); }
    void visit(const Num &n) override { note(n); }
    void visit(const Var &n) override { note(n); }
    void visit(const Assign &n) override { note(n); n.target().accept(*this); n.value().accept(*this); }
    void visit(const Unary &n) override { note(n); n.operand().accept(*this); }
    void visit(const Binary &n) override { note(n); n.lhs().accept(*this); n.rhs().accept(*this); }
    void visit(const Postfix &n) override { note(n); n.target().accept(*this); }
    void visit(const Call &n) override {
        note(n);
        if (n.callee() != nullptr) n.callee()->accept(*this);
        for (std::size_t i = 0; i < n.args().size(); i++) n.args()[i]->accept(*this);
    }
    void visit(const Cast &n) override { note(n); n.value().accept(*this); }
    void visit(const StrLit &n) override { note(n); }
    void visit(const VaStart &n) override { note(n); n.list().accept(*this); }
    void visit(const VaArg &n) override { note(n); n.list().accept(*this); }
    void visit(const MemberAccess &n) override { note(n); n.object().accept(*this); }
    void visit(const ExprStmt &n) override { n.expr().accept(*this); }
    void visit(const Return &n) override { if (n.hasValue()) n.value().accept(*this); }
    void visit(const Block &n) override { for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(*this); }
    void visit(const If &n) override {
        n.cond().accept(*this);
        n.thenArm().accept(*this);
        if (n.elseArm() != nullptr) n.elseArm()->accept(*this);
    }
    void visit(const While &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const For &n) override {
        if (n.init() != nullptr) n.init()->accept(*this);
        if (n.cond() != nullptr) n.cond()->accept(*this);
        if (n.step() != nullptr) n.step()->accept(*this);
        n.body().accept(*this);
    }
    void visit(const DoWhile &n) override { n.body().accept(*this); n.cond().accept(*this); }
    void visit(const Switch &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const Case &n) override { n.body().accept(*this); }
    void visit(const Goto &) override {}
    void visit(const Label &n) override { n.body().accept(*this); }
    void visit(const Conditional &n) override {
        note(n);
        n.cond().accept(*this);
        n.thenArm().accept(*this);
        n.elseArm().accept(*this);
    }
    void visit(const Comma &n) override { note(n); n.left().accept(*this); n.right().accept(*this); }
    void visit(const Break &) override {}
    void visit(const Continue &) override {}
    void visit(const FuncletLeave &) override {}
    void visit(const Try &n) override {
        for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(*this);
        if (n.hasPad()) n.pad().accept(*this);
    }
};

}   // namespace

std::string Typer::check(Parser &p, const Program &program, const Source &src, int *differ) {
    Collect collect;
    for (std::size_t i = 0; i < program.functions.size(); i++) program.functions[i].body().accept(collect);
    Typer typer(p);
    const syntax::Tree &st = p.syntaxTree_;
    const std::vector<syntax::NodeId> roots = st.roots();
    for (std::size_t i = 0; i < roots.size(); i++)
        if (st.node(roots[i]).kind == syntax::Kind::Function) typer.typeFunction(roots[i]);
    std::string out;
    int typed = 0, agree = 0, absent = 0, cv = 0, lowered = 0;
    *differ = 0;
    for (std::uint32_t i = 1; i <= typer.tree().size(); i++) {
        const TypedNode &n = typer.tree().node(TypedId(i));
        if (n.type == nullptr) continue;
        typed++;
        std::unordered_map<std::uint32_t, const Type *>::const_iterator theirs = collect.types.find(n.syntax.v);
        if (theirs == collect.types.end()) { absent++; continue; }
        if (theirs->second == n.type) { agree++; continue; }
        // Two lowerings the parser makes, named rather than counted as differences: top-level cv, which its
        // Ast.h keeps on some prvalues and drops through a reference, and a conditional's arms promoted or decayed.
        if (theirs->second->unqualified() == n.type->unqualified()) { cv++; continue; }
        if (st.node(n.syntax).kind == syntax::Kind::Conditional &&
            (theirs->second->unqualified() == (n.type->isArithmetic() ? p.promote(n.type)->unqualified() : nullptr) ||
             (n.type->isArray() && theirs->second == p.types_.pointerTo(n.type->pointee())))) {
            lowered++;
            continue;
        }
        (*differ)++;
        out += src.where(st.node(n.syntax).loc) + ": " + syntax::kindName(st.node(n.syntax).kind) + " '" +
               st.text(n.syntax) + "' typed '" + n.type->describe() + "', the parser says '" +
               theirs->second->describe() + "'\n";
    }
    char line[240];
    std::snprintf(line, sizeof line, "-check=typed: %d typed, %d agree, %d differ, %d with no Ast.h node to hold against"
                  " (%d in top-level cv alone, %d conditionals the parser promotes or decays)\n",
                  typed, agree, *differ, absent, cv, lowered);
    return out + line;
}

}   // namespace sema
