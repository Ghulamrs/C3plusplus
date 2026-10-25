#include "AsmUnit.h"

namespace masm {

// Expressions are evaluated with explicit operand and operator stacks, not recursion, so a 10,000-deep parenthesis
// or minus chain costs heap, not the job thread's stack (ml64 takes both). Precedence follows MASM's table: unary +
// -, then * / MOD SHL SHR, then + -, then the comparisons, NOT, AND, then OR XOR.

enum OpKind { OP_PAREN, OP_PREFIX, OP_BINARY };

struct Op {
    int kind;
    int prec;   // higher binds tighter
    char c;   // the operator: + - * / % < > & | ^ ~ N L H O I E ... (see the tables)
};

struct ExprState {
    Unit *u;
    const std::vector<Token> *t;
    size_t pos;
    size_t end;
    std::string err;
    std::vector<Value> vals;
    std::vector<Op> ops;
};

static void clear(Value &v)
{
    v.v = 0;
    v.sym = -1;
    v.sub = -1;
    v.sec = -1;
    v.imagerel = false;
    v.offset = false;
    v.wide = false;
    v.secrel = false;
    v.secidx = false;
}

static bool is_const(const Value &v)
{
    return v.sym < 0 && v.sub < 0 && v.sec < 0 && !v.imagerel && !v.offset && !v.secrel && !v.secidx;
}

// a defined label, or $, as a section and an offset
bool located(const Unit &u, const Value &x, int &sec, long long &off)
{
    if (x.sub >= 0 || x.imagerel || x.offset || x.secrel || x.secidx)
        return false;
    if (x.sec >= 0) {
        sec = x.sec;
        off = x.v;
        return true;
    }
    if (x.sym >= 0) {
        const Symbol &s = u.symbols[x.sym];
        if (s.defined && s.bind != B_CONST && s.section >= 0) {
            sec = s.section;
            off = s.value + x.v;
            return true;
        }
    }
    return false;
}

static bool not_const(ExprState &e, const Value &v)
{
    if (is_const(v))
        return false;
    if (v.sym >= 0) e.err = "'" + e.u->symbols[v.sym].name + "' is not a constant";
    else e.err = "'$' is not a constant";
    return true;
}

// a + b: at most one address between them
static bool add(ExprState &e, Value &a, const Value &b)
{
    if (!is_const(a) && !is_const(b)) {
        e.err = "two addresses cannot be added";
        return false;
    }
    if (is_const(a)) {
        long long v = a.v;
        bool wide = a.wide;
        a = b;
        a.v += v;
        a.wide = a.wide || wide;
    } else {
        a.v += b.v;
        a.wide = a.wide || b.wide;
    }
    return true;
}

// a - b: a constant, a label difference folded once both are known, or kept for later
static bool sub(ExprState &e, Value &a, const Value &b)
{
    if (is_const(b)) {
        a.v -= b.v;
        a.wide = a.wide || b.wide;
        return true;
    }
    if (a.imagerel || a.offset || b.imagerel || b.offset || b.sub >= 0 || a.sub >= 0 ||
        a.secrel || a.secidx || b.secrel || b.secidx) {
        e.err = "bad address arithmetic";
        return false;
    }
    int sa, sb;
    long long oa, ob;
    if (located(*e.u, a, sa, oa) && located(*e.u, b, sb, ob)) {
        if (sa != sb) {
            e.err = "labels in different sections cannot be subtracted";
            return false;
        }
        clear(a);
        a.v = oa - ob;
        return true;
    }
    if (a.sym >= 0 && b.sym >= 0) {
        a.sub = b.sym;
        a.v -= b.v;
        return true;
    }
    e.err = "a label can only be subtracted from a label";
    return false;
}

static bool binary(ExprState &e, char op, Value &a, const Value &b)
{
    if (op == '+') return add(e, a, b);
    if (op == '-') return sub(e, a, b);
    if (not_const(e, a) || not_const(e, b)) return false;
    a.wide = a.wide || b.wide;
    long long x = a.v;
    long long y = b.v;
    switch (op) {
    case '*': a.v = x * y; return true;
    case '/':
    case '%':
        if (y == 0) { e.err = "division by zero"; return false; }
        a.v = op == '/' ? x / y : x % y;
        return true;
    case '<': a.v = y >= 64 ? 0 : (long long)((unsigned long long)x << y); return true;
    case '>': a.v = y >= 64 ? 0 : (long long)((unsigned long long)x >> y); return true;
    case '&': a.v = x & y; return true;
    case '|': a.v = x | y; return true;
    case '^': a.v = x ^ y; return true;
    case 'E': a.v = x == y ? -1 : 0; return true;
    case 'N': a.v = x != y ? -1 : 0; return true;
    case 'l': a.v = x < y ? -1 : 0; return true;
    case 'L': a.v = x <= y ? -1 : 0; return true;
    case 'g': a.v = x > y ? -1 : 0; return true;
    case 'G': a.v = x >= y ? -1 : 0; return true;
    }
    e.err = "bad operator";
    return false;
}

static bool prefix(ExprState &e, char op, Value &v)
{
    if (op == '+')
        return true;
    if (op == 'I' || op == 'O' || op == 'R' || op == 'X') {
        if (v.sym < 0 || v.sub >= 0 || v.imagerel || v.offset || v.secrel || v.secidx) {
            e.err = std::string(op == 'I' ? "IMAGEREL" : op == 'O' ? "OFFSET" : op == 'R' ? "SECTIONREL" : "SECTION") + " needs a label";
            return false;
        }
        if (op == 'I') v.imagerel = true;
        else if (op == 'O') v.offset = true;
        else if (op == 'R') v.secrel = true;
        else v.secidx = true;
        return true;
    }
    if (not_const(e, v)) return false;
    if (op == '-') v.v = -v.v;
    else if (op == '~') v.v = ~v.v;
    else if (op == 'w') v.v = v.v & 0xFF;
    else if (op == 'W') v.v = (v.v >> 8) & 0xFF;
    return true;
}

static bool reduce(ExprState &e)
{
    Op op = e.ops.back();
    e.ops.pop_back();
    if (op.kind == OP_PREFIX)
        return prefix(e, op.c, e.vals.back());
    Value b = e.vals.back();
    e.vals.pop_back();
    return binary(e, op.c, e.vals.back(), b);
}

// the binary operators: punctuation or a name, with MASM's precedence
static bool binary_op(const Token &k, Op &op)
{
    op.kind = OP_BINARY;
    if (k.kind == T_PUNCT) {
        char c = k.text[0];
        if (c == '*' || c == '/') { op.prec = 6; op.c = c; return true; }
        if (c == '+' || c == '-') { op.prec = 5; op.c = c; return true; }
        return false;
    }
    if (k.kind != T_NAME) return false;
    // MASM's word operators, at its levels: MOD SHL SHR with * and /, then + -, then the comparisons (true is -1),
    // then AND, then OR and XOR; NOT is a prefix below the comparisons
    std::string w = upper(k.text);
    if (w == "MOD") { op.prec = 6; op.c = '%'; return true; }
    if (w == "SHL") { op.prec = 6; op.c = '<'; return true; }
    if (w == "SHR") { op.prec = 6; op.c = '>'; return true; }
    if (w == "EQ") { op.prec = 4; op.c = 'E'; return true; }
    if (w == "NE") { op.prec = 4; op.c = 'N'; return true; }
    if (w == "LT") { op.prec = 4; op.c = 'l'; return true; }
    if (w == "LE") { op.prec = 4; op.c = 'L'; return true; }
    if (w == "GT") { op.prec = 4; op.c = 'g'; return true; }
    if (w == "GE") { op.prec = 4; op.c = 'G'; return true; }
    if (w == "AND") { op.prec = 2; op.c = '&'; return true; }
    if (w == "OR") { op.prec = 1; op.c = '|'; return true; }
    if (w == "XOR") { op.prec = 1; op.c = '^'; return true; }
    return false;
}

// the prefix operators
static bool prefix_op(const Token &k, Op &op)
{
    op.kind = OP_PREFIX;
    if (k.kind == T_PUNCT && (k.text[0] == '+' || k.text[0] == '-')) { op.prec = 7; op.c = k.text[0]; return true; }
    if (k.kind != T_NAME) return false;
    std::string w = upper(k.text);
    if (w == "OFFSET") { op.prec = 9; op.c = 'O'; return true; }
    if (w == "IMAGEREL") { op.prec = 9; op.c = 'I'; return true; }
    // SECTIONREL is ml64's; SECTION, a label's section number, is this assembler's own - the two halves CodeView
    // names an address by, which a compiler writing its own .debug$S needs
    if (w == "SECTIONREL") { op.prec = 9; op.c = 'R'; return true; }
    if (w == "SECTION") { op.prec = 9; op.c = 'X'; return true; }
    if (w == "NOT") { op.prec = 3; op.c = '~'; return true; }
    if (w == "LOW") { op.prec = 9; op.c = 'w'; return true; }
    if (w == "HIGH") { op.prec = 9; op.c = 'W'; return true; }
    return false;
}

static bool atom(ExprState &e, const Token &k, Value &v)
{
    clear(v);
    if (k.kind == T_NUM) {
        v.v = k.value;
        v.wide = k.wide;
        return true;
    }
    if (k.kind == T_STR) {
        // a character constant: up to eight characters, the first the most significant, as ml64
        if (k.text.empty() || k.text.size() > 8) {
            e.err = "a character constant needs one to eight characters";
            return false;
        }
        for (size_t i = 0; i < k.text.size(); i++)
            v.v = (long long)(((unsigned long long)v.v << 8) | (unsigned char)k.text[i]);
        return true;
    }
    if (k.kind == T_NAME) {
        if (k.text == "$") {
            if (e.u->current < 0) {
                e.err = "no section open (use .CODE or .DATA)";
                return false;
            }
            v.sec = e.u->current;
            v.v = (long long)e.u->here();
            return true;
        }
        int s = e.u->find(k.text);
        if (s >= 0 && e.u->symbols[s].bind == B_CONST)
            v.v = e.u->symbols[s].value;
        else
            v.sym = e.u->ref(k.text);
        return true;
    }
    if (k.kind == T_REAL) {
        e.err = "a real is only stored by DD, DQ, REAL4 or REAL8, not computed with";
        return false;
    }
    e.err = "unexpected '" + k.text + "'";
    return false;
}

bool eval(Unit &u, const std::vector<Token> &t, size_t from, size_t to, Value &v, std::string &err)
{
    ExprState e;
    e.u = &u;
    e.t = &t;
    e.pos = from;
    e.end = to;
    bool want_operand = true;
    while (e.pos < e.end) {
        const Token &k = t[e.pos];
        Op op;
        if (want_operand) {
            if (is_punct(t, e.pos, '(')) {
                op.kind = OP_PAREN;
                op.prec = 0;
                op.c = '(';
                e.ops.push_back(op);
            } else if (prefix_op(k, op)) {
                e.ops.push_back(op);
            } else {
                Value x;
                if (!atom(e, k, x)) { err = e.err; return false; }
                e.vals.push_back(x);
                want_operand = false;
            }
        } else if (is_punct(t, e.pos, ')')) {
            while (!e.ops.empty() && e.ops.back().kind != OP_PAREN)
                if (!reduce(e)) { err = e.err; return false; }
            if (e.ops.empty()) { err = "unexpected ')'"; return false; }
            e.ops.pop_back();
        } else if (binary_op(k, op)) {
            while (!e.ops.empty() && e.ops.back().kind != OP_PAREN && e.ops.back().prec >= op.prec)
                if (!reduce(e)) { err = e.err; return false; }
            e.ops.push_back(op);
            want_operand = true;
        } else {
            err = "unexpected '" + k.text + "'";
            return false;
        }
        e.pos++;
    }
    if (want_operand) {
        err = e.vals.empty() && e.ops.empty() ? "expression expected" : "expression expected";
        return false;
    }
    while (!e.ops.empty()) {
        if (e.ops.back().kind == OP_PAREN) { err = "')' expected"; return false; }
        if (!reduce(e)) { err = e.err; return false; }
    }
    v = e.vals.back();
    return true;
}

bool eval_const(Unit &u, const std::vector<Token> &t, size_t from, size_t to, long long &v, std::string &err)
{
    Value x;
    if (!eval(u, t, from, to, x, err))
        return false;
    ExprState e;
    e.u = &u;
    if (not_const(e, x)) {
        err = e.err;
        return false;
    }
    v = x.v;
    return true;
}

}
