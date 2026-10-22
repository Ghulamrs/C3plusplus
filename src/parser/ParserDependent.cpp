// Expression SFINAE: an expression over a template's parameters, read as a tree in a pattern.
// docs/mend-1.14/expression-sfinae-design.md is the design, every production measured with clang.

#include "Parser.h"
#include "../Source.h"

#include <cstring>

// **Whether the operand ahead mentions a parameter of the pattern being read**, scanning to the
// first `,` `>` `>>` (with `angle`) or unbalanced `)` at depth 0: clang's "instantiation-dependent".
bool Parser::dependentAhead(bool angle) const {
    for (std::size_t k = 0, depth = 0; peekAt(k).kind != TokenKind::End; k++) {
        const Token &t = peekAt(k);
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return false;
            depth--;
        } else if (depth == 0 && (t.is(";") || (angle && (t.is(",") || t.is(">") || t.is(">>")))))
            return false;
        else if (t.kind == TokenKind::Ident) {
            if (k > 0 && (peekAt(k - 1).is(".") || peekAt(k - 1).is("->"))) continue;
            if (nonTypePatternParams_.count(t.text) != 0 || packs_.count(t.text) != 0) return true;
            if (boundTypeParams_.count(t.text) != 0) {
                const Type *bound = findTypedef(t.text);
                if (bound != nullptr && dependentType(bound)) return true;
            }
            if (const Local *l = findLocal(t.text))
                if (l->trailingParam >= 0 && dependentType(l->type)) return true;
        }
    }
    return false;
}

const DepExpr *Parser::depNode(DepExpr e) { return types_.intern(std::move(e)); }

void Parser::depRefuse(std::size_t pos, const std::string &what) {
    src_.fail(pos, what + " is not supported yet in an expression over a template's parameters in "
                          "a signature - the Itanium name would have to spell it, and that form was "
                          "not measured");
}

// expression := assignment [, assignment]... - the comma only where `comma` lets it in.
const DepExpr *Parser::dependentExpression(bool angle, bool comma) {
    const DepExpr *e = depAssignment(angle);
    while (comma && peek().is(",")) {
        at_++;
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = "cm";
        n.kids = { e, depAssignment(angle) };
        e = depNode(n);
    }
    return e;
}

const DepExpr *Parser::depAssignment(bool angle) {
    const DepExpr *lhs = depBinary(1, angle);
    if (peek().is("?")) {
        at_++;
        const DepExpr *a = dependentExpression(false, true);
        expect(":");
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = "qu";
        n.kids = { lhs, a, depAssignment(angle) };
        return depNode(n);
    }
    if (peek().is("=")) {
        at_++;
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = "aS";
        n.kids = { lhs, depAssignment(angle) };
        return depNode(n);
    }
    static const char *const compound[] = { "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                                            "<<=", ">>=" };
    for (const char *c : compound)
        if (peek().is(c)) depRefuse(peek().pos, "a compound assignment '" + std::string(c) + "'");
    return lhs;
}

// Left-associative, the tree clang builds: `sizeof(T) - 1 - 2` is `mimistT_Li1ELi2E`.
const DepExpr *Parser::depBinary(int least, bool angle) {
    struct Op { const char *token; const char *code; int prec; };
    static const Op ops[] = {
        { "||", "oo", 1 }, { "&&", "aa", 2 }, { "|", "or", 3 }, { "^", "eo", 4 },
        { "&", "an", 5 }, { "==", "eq", 6 }, { "!=", "ne", 6 }, { "<", "lt", 7 },
        { ">", "gt", 7 }, { "<=", "le", 7 }, { ">=", "ge", 7 }, { "<<", "ls", 8 },
        { ">>", "rs", 8 }, { "+", "pl", 9 }, { "-", "mi", 9 }, { "*", "ml", 10 },
        { "/", "dv", 10 }, { "%", "rm", 10 },
    };
    const DepExpr *lhs = depUnary(angle);
    for (;;) {
        const Op *found = nullptr;
        for (const Op &o : ops)
            if (peek().is(o.token)) { found = &o; break; }
        if (found == nullptr || found->prec < least) return lhs;
        if (angle && (std::strcmp(found->token, ">") == 0 || std::strcmp(found->token, ">>") == 0))
            return lhs;                           // the argument list's own `>`
        at_++;
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = found->code;
        n.kids = { lhs, depBinary(found->prec + 1, angle) };
        lhs = depNode(n);
    }
}

const DepExpr *Parser::depUnary(bool angle) {
    const std::size_t pos = peek().pos;
    static const char *const prefix[][2] = { { "-", "ng" }, { "!", "nt" }, { "~", "co" },
                                             { "&", "ad" }, { "*", "de" }, { "+", "ps" },
                                             { "++", "pp" }, { "--", "mm" } };
    for (const auto &p : prefix)
        if (peek().is(p[0])) {
            at_++;
            DepExpr n;
            n.op = DepExpr::Operator;
            n.code = p[1];
            n.kids = { depUnary(angle) };
            return depNode(n);
        }
    if (peek().is("sizeof")) {
        at_++;
        if (peek().is("...")) depRefuse(pos, "'sizeof...'");
        DepExpr n;
        if (peek().is("(") && parenHoldsTypeId()) {
            at_++;
            StorageClass sc;
            const Type *t = declarator(specifiers(&sc), true).type;
            expect(")");
            // [mangle]: an operand that depends on nothing is spelled as the size it has.
            if (!dependentType(t)) {
                if (!t->isComplete()) src_.fail(pos, "sizeof needs a complete type");
                n.op = DepExpr::Literal;
                n.type = types_.get(target_.sizeType());
                n.value = t->size(target_);
                return depNode(n);
            }
            n.op = DepExpr::SizeofType;
            n.type = t;
            return depNode(n);
        }
        n.op = DepExpr::SizeofExpr;
        n.kids = { depUnary(angle) };
        return depNode(n);
    }
    if (peek().is("alignof") || peek().is("noexcept") || peek().is("new") ||
        peek().is("delete") || peek().is("throw"))
        depRefuse(pos, "'" + peek().text + "'");
    // A C-style cast is spelled as a one-argument functional cast: `(int)t` is `cvifp_`.
    if (peek().is("(") && parenHoldsTypeId()) {
        at_++;
        StorageClass sc;
        const Type *t = declarator(specifiers(&sc), true).type;
        expect(")");
        DepExpr n;
        n.op = DepExpr::Cast;
        n.type = t;
        n.kids = { depUnary(angle) };
        return depNode(n);
    }
    return depPostfix(angle);
}

const DepExpr *Parser::depPostfix(bool angle) {
    const DepExpr *e = depPrimary(angle);
    for (;;) {
        const std::size_t pos = peek().pos;
        if (peek().is(".") || peek().is("->")) {
            const bool arrow = peek().is("->");
            at_++;
            if (peek().kind != TokenKind::Ident)
                depRefuse(pos, "a member named by '" + peek().text + "'");
            if (peekAt(1).is("::")) depRefuse(pos, "a qualified member name");
            if (peekAt(1).is("<")) depRefuse(pos, "a member template named with arguments");
            DepExpr n;
            n.op = arrow ? DepExpr::Arrow : DepExpr::Member;
            n.name = peek().text;
            n.kids = { e };
            at_++;
            e = depNode(n);
        } else if (peek().is("(")) {
            at_++;
            DepExpr n;
            n.op = DepExpr::Call;
            n.kids = { e };
            while (!consume(")")) {
                n.kids.push_back(dependentExpression(false, false));
                if (!peek().is(")")) expect(",");
            }
            e = depNode(n);
        } else if (peek().is("[")) {
            at_++;
            DepExpr n;
            n.op = DepExpr::Operator;
            n.code = "ix";
            n.kids = { e, dependentExpression(false, true) };
            expect("]");
            e = depNode(n);
        } else if (peek().is("++") || peek().is("--")) {
            depRefuse(pos, "a postfix '" + peek().text + "'");
        } else if (peek().is(".*") || peek().is("->*")) {
            depRefuse(pos, "'" + peek().text + "'");
        } else {
            return e;
        }
    }
}

// `T(x)` and `T(1, 2)` and `T()`: `cv <type> <e>` for one argument, `cv <type> _ <e>... E` else.
const DepExpr *Parser::depCast(const Type *to) {
    const std::size_t pos = peek().pos;
    if (peek().is("{")) depRefuse(pos, "a braced functional cast");
    expect("(");
    DepExpr n;
    n.type = to;
    while (!consume(")")) {
        n.kids.push_back(dependentExpression(false, false));
        if (!peek().is(")")) expect(",");
    }
    n.op = n.kids.size() == 1 ? DepExpr::Cast : DepExpr::CastList;
    return depNode(n);
}

const DepExpr *Parser::depPrimary(bool angle) {
    (void)angle;
    const Token &tok = peek();
    const std::size_t pos = tok.pos;
    DepExpr n;
    if (tok.kind == TokenKind::Num || tok.is("true") || tok.is("false")) {
        Discarded held(this);
        Unevaluated unevaluated(this);
        ExprPtr lit = primary(current_);
        const Num *num = dynamic_cast<const Num *>(lit.get());
        if (num == nullptr || lit->type() == nullptr) depRefuse(pos, "this literal");
        n.op = DepExpr::Literal;
        n.type = lit->type();
        if (n.type->kind() == Kind::Float || n.type->kind() == Kind::Double) {
            n.floating = true;
            if (n.type->kind() == Kind::Float) {
                const float f = static_cast<float>(num->dvalue());
                unsigned int bits;
                std::memcpy(&bits, &f, sizeof bits);
                n.value = static_cast<long long>(bits);
            } else {
                const double d = static_cast<double>(num->dvalue());
                unsigned long long bits;
                std::memcpy(&bits, &d, sizeof bits);
                n.value = static_cast<long long>(bits);
            }
        } else if (n.type->isFloating()) {
            depRefuse(pos, "a 'long double' literal");
        } else {
            n.value = num->value();
        }
        return depNode(n);
    }
    if (tok.kind == TokenKind::Str) depRefuse(pos, "a string literal");
    if (tok.is("(")) {
        at_++;
        const DepExpr *e = dependentExpression(false, true);
        expect(")");
        return e;
    }
    if (tok.is("static_cast")) {
        at_++;
        expect("<");
        const bool wasArgs = inTemplateArgs_;
        inTemplateArgs_ = true;
        StorageClass sc;
        const Type *t = declarator(specifiers(&sc), true).type;
        inTemplateArgs_ = wasArgs;
        takeClosingAngle();
        expect("(");
        n.op = DepExpr::StaticCast;
        n.type = t;
        n.kids = { dependentExpression(false, true) };
        expect(")");
        return depNode(n);
    }
    if (tok.is("this") || tok.is("dynamic_cast") || tok.is("reinterpret_cast") ||
        tok.is("const_cast") || tok.is("typeid") || tok.is("[") || tok.is("::") ||
        tok.is("nullptr") || tok.is("operator"))
        depRefuse(pos, "'" + tok.text + "'");
    if (tok.is("typename")) {
        StorageClass sc;
        return depCast(specifiers(&sc));
    }
    if (tok.kind == TokenKind::Ident) {
        const std::string &name = tok.text;
        if (packs_.count(name) != 0) depRefuse(pos, "a parameter pack");
        // A type parameter: `T(x)` is a cast, `T::count` the unresolved `sr T_ 5count`.
        if (boundTypeParams_.count(name) != 0) {
            const Type *t = findTypedef(name);
            if (t != nullptr && t->kind() == Kind::TemplateParam) {
                if (peekAt(1).is("::")) {
                    if (peekAt(2).kind != TokenKind::Ident || peekAt(3).is("::") ||
                        peekAt(3).is("<"))
                        depRefuse(pos, "a qualified name other than 'T::name'");
                    n.op = DepExpr::QualifiedParamName;
                    n.type = t;
                    n.name = peekAt(2).text;
                    at_ += 3;
                    return depNode(n);
                }
                at_++;
                return depCast(t);
            }
        }
        auto np = nonTypePatternParams_.find(name);
        if (np != nonTypePatternParams_.end()) {
            at_++;
            n.op = DepExpr::TemplateParam;
            n.index = np->second;
            return depNode(n);
        }
        // An enclosing template's non-type parameter is bound to its value here, and clang spells
        // that value: `V<N + K>` in `V<3>` is `XplLm3ET_E`, measured.
        if (boundValueParams_.count(name) != 0) {
            const EnumConst *e = findEnum(name);
            if (e != nullptr && e->paramType != nullptr && e->paramType->isInteger()) {
                at_++;
                n.op = DepExpr::Literal;
                n.type = e->paramType;
                n.value = e->value;
                return depNode(n);
            }
        }
        if (const Local *l = findLocal(name)) {
            if (l->trailingParam < 0) depRefuse(pos, "a local name");
            at_++;
            n.op = DepExpr::FunctionParam;
            n.index = l->trailingParam;
            return depNode(n);
        }
    }
    if (atTypeName()) {
        StorageClass sc;
        return depCast(specifiers(&sc));
    }
    if (tok.kind == TokenKind::Ident) {
        if (peekAt(1).is("<") && findTemplate(tok.text) != templates_.end())
            depRefuse(pos, "a template-id");
        if (peekAt(1).is("::")) depRefuse(pos, "a qualified name");
        if (peekAt(1).is("(")) {
            at_++;
            n.op = DepExpr::UnresolvedName;
            n.name = tok.text;
            return depNode(n);
        }
        depRefuse(pos, "a name declared outside the template ('" + tok.text + "')");
    }
    depRefuse(pos, "'" + tok.text + "'");
    return nullptr;
}

// The tokens of [from, to), as a diagnostic shows the expression.
std::string Parser::depText(std::size_t from, std::size_t to) const {
    std::string text;
    for (std::size_t i = from; i < to; i++) {
        if (!text.empty()) text += ' ';
        text += tokens_[i].text;
    }
    return text;
}
