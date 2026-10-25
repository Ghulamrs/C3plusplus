// dumpFunction, dumpStmt - see AstDump.h.

#include "AstDump.h"

#include <cstdio>

namespace {

const char *binOpName(BinOp op) {
    switch (op) {
        case BinOp::Add: return "+";
        case BinOp::Sub: return "-";
        case BinOp::Mul: return "*";
        case BinOp::Div: return "/";
        case BinOp::Mod: return "%";
        case BinOp::Shl: return "<<";
        case BinOp::Shr: return ">>";
        case BinOp::BitAnd: return "&";
        case BinOp::BitOr: return "|";
        case BinOp::BitXor: return "^";
        case BinOp::Eq: return "==";
        case BinOp::Ne: return "!=";
        case BinOp::Lt: return "<";
        case BinOp::Le: return "<=";
        case BinOp::Gt: return ">";
        case BinOp::Ge: return ">=";
        case BinOp::LAnd: return "&&";
        case BinOp::LOr: return "||";
    }
    return "?";
}

std::string typeText(const Type *t) { return t == nullptr ? std::string("-") : t->describe(); }

// A node a line, indented by depth; an expression's line ends with its type in brackets.
struct Dump final : Visitor {
    std::string &out;
    int depth = 0;
    explicit Dump(std::string &o) : out(o) {}
    void line(const std::string &s) { out.append(static_cast<std::size_t>(depth) * 2, ' '); out += s; out += '\n'; }
    void expr(const Expr &e, const std::string &s) {
        std::string t = s + " [" + typeText(e.type()) + "]";
        if (e.isXvalue()) t += " xvalue";
        line(t);
    }
    void sub(const Node &n) { depth++; n.accept(*this); depth--; }
    void stmtLine(const Stmt &s, const std::string &text) {
        char at[32];
        std::snprintf(at, sizeof at, " @%zu", s.pos());
        line(text + at);
    }

    void visit(const Num &n) override {
        char b[96];
        std::snprintf(b, sizeof b, "num %lld %.21Lg", n.value(), n.dvalue());
        expr(n, b);
    }
    void visit(const Var &n) override {
        char b[64];
        std::snprintf(b, sizeof b, " off=%d%s%s%s%s", n.offset(), n.isLocal() ? " local" : "",
                      n.readOnly() ? " ro" : "", n.noAddress() ? " noaddr" : "",
                      n.isThreadLocal() ? (n.viaWrapper() ? " tls-wrapper" : " tls") : "");
        expr(n, "var " + n.name() + " sym=" + n.symbol() + b);
    }
    void visit(const Assign &n) override { expr(n, "assign"); sub(n.target()); sub(n.value()); }
    void visit(const Unary &n) override { expr(n, std::string("unary ") + n.op()); sub(n.operand()); }
    void visit(const Binary &n) override { expr(n, std::string("binary ") + binOpName(n.op())); sub(n.lhs()); sub(n.rhs()); }
    void visit(const Postfix &n) override {
        char b[64];
        std::snprintf(b, sizeof b, "postfix %s %lld", n.increment() ? "++" : "--", n.step());
        expr(n, b);
        sub(n.target());
    }
    void visit(const Call &n) override {
        char b[128];
        std::snprintf(b, sizeof b, " variadic=%d result=%d%s named=%d this=%d", n.isVariadic() ? 1 : 0, n.resultSlot(),
                      n.resultIndirect() ? " indirect" : "", n.namedArgs(), n.hasThis() ? 1 : 0);
        std::string slots;
        for (std::size_t i = 0; i < n.args().size(); i++) {
            char s[24];
            std::snprintf(s, sizeof s, " %d", n.argSlot(i));
            slots += s;
        }
        expr(n, "call " + n.name() + " sym=" + n.symbol() + b + " slots" + slots);
        if (n.callee() != nullptr) { depth++; line("callee"); sub(*n.callee()); depth--; }
        for (std::size_t i = 0; i < n.args().size(); i++) sub(*n.args()[i]);
    }
    void visit(const Cast &n) override { expr(n, "cast"); sub(n.value()); }
    void visit(const StrLit &n) override { expr(n, "string " + n.label()); }
    void visit(const VaStart &n) override { expr(n, "va_start"); sub(n.list()); }
    void visit(const VaArg &n) override { expr(n, "va_arg"); sub(n.list()); }
    void visit(const MemberAccess &n) override {
        char b[96];
        std::snprintf(b, sizeof b, " off=%d width=%d bit=%d", n.offset(), n.width(), n.bitOffset());
        expr(n, "member " + n.name() + b);
        sub(n.object());
    }
    void visit(const Conditional &n) override { expr(n, "?:"); sub(n.cond()); sub(n.thenArm()); sub(n.elseArm()); }
    void visit(const Comma &n) override { expr(n, "comma"); sub(n.left()); sub(n.right()); }
    void visit(const ExprStmt &n) override { stmtLine(n, "expr"); sub(n.expr()); }
    void visit(const Return &n) override { stmtLine(n, "return"); if (n.hasValue()) sub(n.value()); }
    void visit(const Block &n) override {
        char b[64];
        std::snprintf(b, sizeof b, "block scope=%d%s", n.scope(), n.unwindCleanup() ? " unwind" : "");
        stmtLine(n, b);
        for (std::size_t i = 0; i < n.body().size(); i++) sub(*n.body()[i]);
    }
    void visit(const If &n) override {
        stmtLine(n, "if");
        sub(n.cond());
        sub(n.thenArm());
        if (n.elseArm() != nullptr) { depth++; line("else"); depth--; sub(*n.elseArm()); }
    }
    void visit(const While &n) override { stmtLine(n, "while"); sub(n.cond()); sub(n.body()); }
    void visit(const For &n) override {
        char b[32];
        std::snprintf(b, sizeof b, "for scope=%d", n.scope());
        stmtLine(n, b);
        depth++;
        line(n.init() != nullptr ? "init" : "no init");
        if (n.init() != nullptr) sub(*n.init());
        line(n.cond() != nullptr ? "cond" : "no cond");
        if (n.cond() != nullptr) sub(*n.cond());
        line(n.step() != nullptr ? "step" : "no step");
        if (n.step() != nullptr) sub(*n.step());
        depth--;
        sub(n.body());
    }
    void visit(const DoWhile &n) override { stmtLine(n, "do"); sub(n.body()); sub(n.cond()); }
    void visit(const Switch &n) override {
        char b[48];
        std::snprintf(b, sizeof b, "switch cases=%zu default=%d", n.cases().size(), n.defaultCase() != nullptr ? 1 : 0);
        stmtLine(n, b);
        sub(n.cond());
        sub(n.body());
    }
    void visit(const Case &n) override {
        char b[64];
        std::snprintf(b, sizeof b, "%s %lld id=%d", n.isDefault() ? "default" : "case", n.value(), n.id());
        stmtLine(n, b);
        sub(n.body());
    }
    void visit(const Goto &n) override { stmtLine(n, "goto " + n.label()); }
    void visit(const Label &n) override { stmtLine(n, "label " + n.name()); sub(n.body()); }
    void visit(const Break &n) override { stmtLine(n, "break"); }
    void visit(const Continue &n) override { stmtLine(n, "continue"); }
    void visit(const FuncletLeave &n) override { stmtLine(n, "funclet-leave " + n.label()); }
    // Every field a backend reads (M2.14): two bodies differing in a type, an index, an exit or a slot must compare different.
    void visit(const Try &n) override {
        char b[128];
        std::snprintf(b, sizeof b, "try ptr=%d sel=%d handlers=%zu cleanup=%d help=%d", n.pointerSlot(), n.selectorSlot(),
                      n.handlers().size(), n.alsoCleanup() ? 1 : 0, n.unwindHelpSlot());
        std::string text = b;
        for (std::size_t i = 0; i < n.types().size(); i++) {
            text += i == 0 ? " types=" : ",";
            text += n.types()[i].empty() ? "..." : n.types()[i];
            if (i < n.typeIndices().size()) text += "#" + std::to_string(n.typeIndices()[i]);
        }
        for (std::size_t i = 0; i < n.msExits().size(); i++) text += (i == 0 ? " exits=" : ",") + n.msExits()[i];
        stmtLine(n, text);
        for (std::size_t i = 0; i < n.body().size(); i++) sub(*n.body()[i]);
        if (n.hasPad()) { depth++; line("pad"); depth--; sub(n.pad()); }
        // A Microsoft handler's every field, and the cleanup a region runs (M2 close, condition C1).
        for (const MsHandler &h : n.handlers()) {
            std::snprintf(b, sizeof b, "handler %s obj=%d size=%d ref=%d const=%d pos=%zu",
                          h.descriptor.empty() ? "..." : h.descriptor.c_str(), h.objectSlot, h.objectSize,
                          h.byReference ? 1 : 0, h.constPointer ? 1 : 0, h.pos);
            depth++; line(b); depth--;
            if (h.body) sub(*h.body);
        }
        if (n.cleanup() != nullptr) { depth++; line("cleanup"); depth--; sub(*n.cleanup()); }
    }
};

}   // namespace

void dumpStmt(const Stmt &s, std::string &out) {
    Dump d(out);
    s.accept(d);
}

void dumpFunction(const Function &f, std::string &out) {
    char b[160];
    std::snprintf(b, sizeof b, " frame=%d align=%d sret=%d static=%d variadic=%d this=%d inline=%d\n", f.frameSize(),
                  f.frameAlign(), f.sretSlot(), f.isStatic() ? 1 : 0, f.isVariadic() ? 1 : 0, f.hasThis() ? 1 : 0,
                  f.isInline() ? 1 : 0);
    out += "function " + f.symbol() + " returns " + typeText(f.returns()) + b;
    for (std::size_t i = 0; i < f.params().size(); i++) {
        char p[32];
        std::snprintf(p, sizeof p, " off=%d\n", f.params()[i].offset);
        out += "  param " + typeText(f.params()[i].type) + p;
    }
    dumpStmt(f.body(), out);
}
