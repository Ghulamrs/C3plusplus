// Software pipelining of the innermost counted loops of one function's C6000 text at -O2: a loop that is one block
// ending in `[P] B head`, counted by `ADD 1, i, i` against a bound, is modulo-scheduled at the least II its dependences
// and units allow, renamed per iteration where a value outlives II, and emitted as prologue, kernel and epilogue.

#include "C6xMir.h"
#include "C6xModel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace c6x {
namespace {

int tracing() {
    static const int t = std::getenv("CPP11_PIPE") ? std::atoi(std::getenv("CPP11_PIPE")) : 0;
    return t;
}

// ----- addresses as linear forms: term -> coefficient -----

// A form's term: the constant, the induction variable, the value a register came in with, or an object's address -
// a symbol's by its name, the frame's as A15.
struct Term {
    enum Kind { Const, Iv, Reg, Object };
    Kind kind; int reg; std::string name;
    bool operator<(const Term &o) const { return kind != o.kind ? kind < o.kind : reg != o.reg ? reg < o.reg : name < o.name; }
    bool operator==(const Term &o) const { return kind == o.kind && reg == o.reg && name == o.name; }
};
Term termOf(Term::Kind k, int reg = kNoReg, const std::string &name = std::string()) { Term t = { k, reg, name }; return t; }
Term onEntry(int r) { return r == 15 ? termOf(Term::Object, 15) : termOf(Term::Reg, r); }
std::uint64_t bitR(int r) { return r == kNoReg ? 0 : 1ull << r; }

typedef std::map<Term, long> Form;

Form formOf(const Term &term, long k = 1) { Form f; f[term] = k; return f; }
Form constForm(long k) { return formOf(termOf(Term::Const), k); }
Form combine(const Form &a, const Form &b, long sign) {
    Form r = a;
    for (Form::const_iterator it = b.begin(); it != b.end(); ++it) r[it->first] += sign * it->second;
    for (Form::iterator it = r.begin(); it != r.end();) { if (it->second == 0) r.erase(it++); else ++it; }
    return r;
}
Form scaled(const Form &a, long k) {
    Form r;
    if (k != 0) for (Form::const_iterator it = a.begin(); it != a.end(); ++it) r[it->first] = it->second * k;
    return r;
}
bool isConstForm(const Form &f, long &k) {
    k = 0;
    if (f.empty()) return true;
    if (f.size() == 1 && f.begin()->first.kind == Term::Const) { k = f.begin()->second; return true; }
    return false;
}
// The one object a form is based on - a global symbol or the frame, with coefficient one; false if none or several.
bool objectOf(const Form &f, Term &o) {
    bool found = false;
    for (Form::const_iterator it = f.begin(); it != f.end(); ++it) {
        if (it->first.kind != Term::Object) continue;
        if (found || it->second != 1) return false;
        o = it->first; found = true;
    }
    return found;
}

struct Access { bool known = false; Form addr; };

// **What the address walk reads of an instruction** (M6c round 11): its parts, its registers and its access - the base,
// the offset or index and the step - by number: from the entry's operands on the stream, from a line's text off it.
struct Ins {
    std::string mnem, name[2];              // the first two operands as spelled, where one is a symbol
    int reg[2] = { kNoReg, kNoReg };        // and as a register, where it is one
    bool pred = false, cst[2] = { false, false }, access = false, indexed = false;
    std::size_t ops = 0;
    long num[2] = { 0, 0 };
    std::vector<int> reads, writes;
    int stepReg = kNoReg, base = kNoReg, index = kNoReg;    // `steppedRegister`; the access's base; indexed: its index, none a constant
    long by = 0, off = 0, sign = 1;
};

// The access `m` as its spelling says: stepped, `*+R(n)`, `*+R[x]` or `*R`.
void accessOf(const std::string &m, Ins &x) {
    if (x.stepReg != kNoReg) { x.base = x.stepReg; if (m.compare(0, 3, "*++") == 0 || m.compare(0, 3, "*--") == 0) x.off = x.by; }
    else if (m.size() > 1 && m[0] == '*' && (m[1] == '+' || m[1] == '-') && m.find('(') != std::string::npos) {
        x.sign = m[1] == '-' ? -1 : 1;
        x.base = regNumber(m.substr(2, m.find('(') - 2));
        x.off = std::atol(m.substr(m.find('(') + 1).c_str());
    } else if (m.size() > 1 && m[0] == '*' && (m[1] == '+' || m[1] == '-') && m.find('[') != std::string::npos) {
        x.sign = m[1] == '-' ? -1 : 1;
        x.base = regNumber(m.substr(2, m.find('[') - 2));
        x.indexed = true;
        x.index = regNumber(m.substr(m.find('[') + 1, m.find(']') - m.find('[') - 1));
    } else if (m.size() > 1 && m[0] == '*' && sideOf(m.substr(1))) x.base = regNumber(m.substr(1));
}

Ins insOf(const Line &l) {
    Ins x;
    x.mnem = l.mnem; x.pred = !l.pred.empty(); x.ops = l.ops.size();
    for (std::size_t k = 0; k < 2 && k < l.ops.size(); k++) { x.name[k] = l.ops[k]; x.reg[k] = regNumber(l.ops[k]); x.cst[k] = isNumber(l.ops[k]); x.num[k] = std::atol(l.ops[k].c_str()); }
    std::vector<std::string> reads, writes;
    readsAndWrites(l, reads, writes);
    for (std::size_t k = 0; k < reads.size(); k++) x.reads.push_back(regNumber(reads[k]));
    for (std::size_t k = 0; k < writes.size(); k++) x.writes.push_back(regNumber(writes[k]));
    x.stepReg = regNumber(steppedRegister(l, x.by));
    x.access = isLoad(l.mnem) || isStore(l.mnem);
    if (x.access) accessOf(isStore(l.mnem) ? l.ops[1] : l.ops[0], x);
    return x;
}

Ins insOf(const MInst &e) {
    Ins x;
    x.mnem = e.m.str(); x.pred = e.pred != kNoReg; x.ops = e.operands;
    for (int k = 0; k < 2 && k < e.operands; k++) {
        x.cst[k] = e.op[k].kind == MOperand::Immediate;
        x.num[k] = static_cast<long>(e.op[k].value);
        if (e.op[k].kind == MOperand::Register) x.reg[k] = e.op[k].reg;
        if (!x.cst[k]) x.name[k] = e.op[k].str();
    }
    readsAndWrites(e, x.reads, x.writes);
    // The first stepped access names the step: by its `(n)` bytes, one where none is written, none where it has an index.
    for (int k = 0; k < e.operands; k++) {
        const MOperand &o = e.op[k];
        if (o.kind != MOperand::Memory || o.mode < MOperand::PreInc) continue;
        if (o.index != kNoReg) break;
        x.stepReg = o.reg;
        x.by = o.hasOffset && !o.scaled ? static_cast<long>(o.value) : 1;
        if (o.mode == MOperand::PreDec || o.mode == MOperand::PostDec) x.by = -x.by;
        break;
    }
    x.access = isLoad(x.mnem) || isStore(x.mnem);
    if (!x.access) return x;
    const MOperand &o = e.op[isStore(x.mnem) ? 1 : 0];
    if (o.kind != MOperand::Memory) { accessOf(o.str(), x); return x; }
    const bool signedBase = o.mode == MOperand::Plus || o.mode == MOperand::Minus;
    if (x.stepReg != kNoReg) { x.base = x.stepReg; if (o.mode == MOperand::PreInc || o.mode == MOperand::PreDec) x.off = x.by; }
    else if (signedBase && o.hasOffset) {
        x.sign = o.mode == MOperand::Minus ? -1 : 1;
        x.base = o.reg;
        if (!o.scaled) x.off = o.index != kNoReg ? 0 : static_cast<long>(o.value);
        else { x.indexed = true; x.index = o.index; }
    } else if (o.mode == MOperand::Base && !o.hasOffset) x.base = o.reg;
    return x;
}

// What one instruction makes of its destination - a form, or nothing known - entered in val, the rest of its writes lost.
void evalLine(const Ins &l, std::map<int, Form> &val, std::set<int> &lost) {
    const std::vector<int> &writes = l.writes;
    if (writes.empty()) return;
    const int d = writes.back();
    Form nv; bool knownV = false; long k = 0, k2 = 0;
    const int *p = l.reg;
    bool cst0 = l.ops > 1 && l.cst[0], cst1 = l.ops > 2 && l.cst[1];
    Form v0 = l.ops > 1 && !l.cst[0] && p[0] != kNoReg && val.count(p[0]) ? val[p[0]] : Form(), v1 = l.ops > 2 && !l.cst[1] && p[1] != kNoReg && val.count(p[1]) ? val[p[1]] : Form();
    bool k0 = cst0 || (l.ops > 1 && !v0.empty()), k1 = cst1 || (l.ops > 2 && !v1.empty());
    if (cst0) v0 = constForm(l.num[0]);
    if (cst1) v1 = constForm(l.num[1]);
    const Form symbol = formOf(termOf(Term::Object, kNoReg, l.name[0]));
    if (!l.pred && writes.size() == 1) {
        if (l.mnem == "MVK" && cst0) { nv = v0; knownV = true; }
        else if (l.mnem == "MVKL" && l.ops == 2) { nv = cst0 ? v0 : symbol; knownV = true; }
        else if (l.mnem == "MVKH" && l.ops == 2 && val.count(d) && val[d] == (cst0 ? v0 : symbol)) { nv = val[d]; knownV = true; }
        else if (l.mnem == "MV" && k0) { nv = v0; knownV = true; }
        else if (l.mnem == "ADD" && k0 && k1) { nv = combine(v0, v1, 1); knownV = true; }
        else if (l.mnem == "SUB" && k0 && k1) { nv = combine(v0, v1, -1); knownV = true; }
        else if (l.mnem == "SUBAW" && k0 && cst1) { nv = combine(v0, scaled(v1, 4), -1); knownV = true; }
        else if (l.mnem == "ADDK" && cst0 && val.count(d)) { nv = combine(val[d], v0, 1); knownV = true; }
        else if (l.mnem == "SHL" && k0 && cst1 && isConstForm(v1, k) && k >= 0 && k < 31) { nv = scaled(v0, 1L << k); knownV = true; }
        else if (l.mnem == "MPY32" && k0 && k1 && (isConstForm(v1, k2) || isConstForm(v0, k))) { nv = isConstForm(v1, k2) ? scaled(v0, k2) : scaled(v1, k); knownV = true; }
    }
    for (std::size_t w = 0; w < writes.size(); w++) { val.erase(writes[w]); lost.insert(writes[w]); }
    if (knownV && !nv.empty()) { val[d] = nv; lost.erase(d); }
}

// The address forms of the body's memory operands, from a walk over the body with the induction variable as
// `iv`, a register the loop never writes as its own term, and a register written before its first read unknown.
std::vector<Access> addressForms(const std::vector<Ins> &body, const std::vector<Ins> &pre, int iv, std::uint64_t written) {
    std::map<int, Form> val, before;
    std::set<int> lost;
    for (std::size_t o = 0; o < pre.size(); o++) {           // what the preheader computed, by the same rules
        const std::vector<int> &reads = pre[o].reads;
        for (std::size_t r = 0; r < reads.size(); r++) if (!before.count(reads[r]) && !lost.count(reads[r])) before[reads[r]] = formOf(onEntry(reads[r]));
        evalLine(pre[o], before, lost);
    }
    lost.clear();
    // A pointer the accesses alone step: on entry to a turn it is the preheader's value plus its stride per turn gone.
    std::map<int, long> stride, stepWrites, allWrites;
    for (std::size_t o = 0; o < body.size(); o++) {
        for (std::size_t w = 0; w < body[o].writes.size(); w++) allWrites[body[o].writes[w]]++;
        const int r = body[o].stepReg;
        if (r != kNoReg) { stride[r] += body[o].by; stepWrites[r]++; }
    }
    const Form turns = combine(formOf(termOf(Term::Iv)), formOf(termOf(Term::Reg, iv)), -1);
    std::vector<Access> out(body.size());
    for (std::size_t o = 0; o < body.size(); o++) {
        const Ins &l = body[o];
        const std::vector<int> &reads = l.reads;
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (val.count(reads[r]) || lost.count(reads[r])) continue;
            const int x = reads[r];
            if (x == iv) val[x] = formOf(termOf(Term::Iv));
            else if (stepWrites.count(x) && stepWrites[x] == allWrites[x] && before.count(x) && x != iv) val[x] = combine(before[x], scaled(turns, stride[x]), 1);
            else val[x] = (written & bitR(x)) ? Form() : before.count(x) ? before[x] : formOf(onEntry(x));
        }
        const int stepReg = l.stepReg;
        Form stepFrom; bool stepKnown = stepReg != kNoReg && val.count(stepReg) && !val[stepReg].empty();
        if (stepKnown) stepFrom = val[stepReg];
        if (l.access) {
            bool known = l.base != kNoReg && val.count(l.base) && !val[l.base].empty();
            if (known && l.indexed) known = l.index != kNoReg && val.count(l.index) && !val[l.index].empty();
            if (known) {
                Form a = val[l.base];
                if (l.indexed) a = combine(a, scaled(val[l.index], accessSize(l.mnem)), l.sign);
                else if (l.off) a = combine(a, constForm(l.off), l.sign);
                out[o].known = true; out[o].addr = a;
            }
        }
        evalLine(l, val, lost);
        if (stepKnown) { val[stepReg] = combine(stepFrom, constForm(l.by), 1); lost.erase(stepReg); }
    }
    return out;
}

// Whether access a, in iteration j, and b, in iteration j + d, may touch the same bytes.
bool mayAliasAt(const Access &a, int sa, const Access &b, int sb, int d) {
    if (!a.known || !b.known) return true;
    Form diff = combine(b.addr, a.addr, -1);
    Form::const_iterator iv = b.addr.find(termOf(Term::Iv));
    if (iv != b.addr.end()) diff = combine(diff, constForm(d * iv->second), 1);
    long c;
    if (isConstForm(diff, c)) return c > -sb && c < sa;
    Term oa, ob;
    return !objectOf(a.addr, oa) || !objectOf(b.addr, ob) || oa == ob;
}

// **The recognisers' reader** (M6c round 4): what an entry is and its parts - kind, mnemonic, predicate, operands,
// reads and writes, units - from the stream's typed operands where it has them (`modelled`), else from the line.
struct Src {
    const MStream *s;
    const std::vector<std::uint64_t> *live;     // the stream's live-in, entry by entry
    mutable std::vector<Line> made;
    mutable std::vector<char> have;
    Src(const MStream *stream, const std::vector<std::uint64_t> *in) : s(stream), live(in) {}
    std::size_t size() const { return s->size(); }
    // **An entry read as a line** (M6c round 14): made where a reader still wants text, once.
    const Line &line(std::size_t i) const {
        if (made.empty()) { made.resize(s->size()); have.assign(s->size(), 0); }
        if (!have[i]) { made[i] = viewOf((*s)[i]); have[i] = 1; }
        return made[i];
    }
    // Entry i as it stands on the stream.
    MInst entry(std::size_t i) const { return (*s)[i]; }
    std::uint64_t liveIn(std::size_t i) const { return (*live)[i]; }
    std::uint64_t liveAt(std::size_t i) const { return (*live)[i]; }
    bool typed(std::size_t i) const { return modelled((*s)[i]); }
    bool instr(std::size_t i) const { return (*s)[i].kind == MInst::Instruction && !(*s)[i].packed; }
    bool label(std::size_t i) const {
        if ((*s)[i].kind == MInst::Label) return true;
        return ((*s)[i].kind != MInst::Instruction || (*s)[i].packed) && isLabel(line(i));
    }
    std::string name(std::size_t i) const { return (*s)[i].kind == MInst::Label ? (*s)[i].m.str() : labelName(line(i)); }
    std::string mnem(std::size_t i) const { return instr(i) ? (*s)[i].m.str() : line(i).mnem; }
    std::string pred(std::size_t i) const {
        if (!typed(i)) return line(i).pred;
        const MInst &e = (*s)[i];
        return e.pred == kNoReg ? std::string() : (e.predNot ? "!" : "") + regName(e.pred);
    }
    std::size_t ops(std::size_t i) const { return typed(i) ? (*s)[i].operands : line(i).ops.size(); }
    std::string op(std::size_t i, std::size_t k) const { return typed(i) ? (*s)[i].op[k].str() : line(i).ops[k]; }
    bool reg(std::size_t i, std::size_t k) const { return typed(i) ? (*s)[i].op[k].isReg() : sideOf(line(i).ops[k]) != 0; }
    bool number(std::size_t i, std::size_t k, long &n) const {
        if (typed(i)) { n = static_cast<long>((*s)[i].op[k].value); return (*s)[i].op[k].kind == MOperand::Immediate; }
        n = std::atol(line(i).ops[k].c_str());
        return isNumber(line(i).ops[k]);
    }
    bool pair(std::size_t i, std::size_t k) const { return typed(i) ? (*s)[i].op[k].kind == MOperand::Pair : line(i).ops[k].find(':') != std::string::npos; }
    void rw(std::size_t i, std::vector<std::string> &reads, std::vector<std::string> &writes) const {
        if (typed(i)) readsAndWrites((*s)[i], reads, writes); else readsAndWrites(line(i), reads, writes);
    }
    // The same by number: the entry's own registers where it is typed, the line's names numbered where not.
    void regs(std::size_t i, std::vector<int> &reads, std::vector<int> &writes) const {
        if (typed(i)) { readsAndWrites((*s)[i], reads, writes); return; }
        std::vector<std::string> r, w;
        readsAndWrites(line(i), r, w);
        for (std::size_t k = 0; k < r.size(); k++) reads.push_back(regNumber(r[k]));
        for (std::size_t k = 0; k < w.size(); k++) writes.push_back(regNumber(w[k]));
    }
    unsigned units(std::size_t i) const { return typed(i) ? unitsFor((*s)[i]) : unitsFor(line(i)); }
    // The same as masks, and a register operand or the predicate as a number: the entry's own where it is typed.
    void masks(std::size_t i, std::uint64_t &reads, std::uint64_t &writes) const {
        if (typed(i)) { readsAndWrites((*s)[i], reads, writes); return; }
        std::vector<std::string> r, w;
        readsAndWrites(line(i), r, w);
        reads |= maskOf(r); writes |= maskOf(w);
    }
    int regAt(std::size_t i, std::size_t k) const { return typed(i) ? ((*s)[i].op[k].kind == MOperand::Register ? (*s)[i].op[k].reg : kNoReg) : regNumber(line(i).ops[k]); }
    int predReg(std::size_t i) const { return typed(i) ? (*s)[i].pred : regNumber(line(i).pred.substr(line(i).pred.compare(0, 1, "!") == 0 ? 1 : 0)); }
    char side(std::size_t i, std::size_t k) const {
        if (!typed(i)) return sideOf(line(i).ops[k]);
        const MOperand &o = (*s)[i].op[k];
        if (o.kind != MOperand::Register && o.kind != MOperand::Pair) return 0;
        return sideOfReg(o.kind == MOperand::Pair ? o.hi : o.reg) ? 'B' : 'A';
    }
    bool predicated(std::size_t i) const { return typed(i) ? (*s)[i].pred != kNoReg : !line(i).pred.empty(); }
    bool predNot(std::size_t i) const { return typed(i) ? (*s)[i].predNot : line(i).pred.compare(0, 1, "!") == 0; }
    void regsIn(std::size_t i, std::size_t k, std::vector<int> &out) const;
    bool byteAddress(std::size_t i, std::size_t k, int &base, long &off, bool &post) const;
    // The operands that name `x`: the label a branch reaches, counted over the function's instructions.
    int naming(const std::string &x) const {
        int n = 0;
        for (std::size_t i = 0; i < size(); i++) if (instr(i)) for (std::size_t o = 0; o < ops(i); o++) if (op(i, o) == x) n++;
        return n;
    }
};

// ----- the loop as recognised -----

// An operand from a register's number, a pair's two, or a constant; an entry of its parts; and the line an entry spells.
MOperand R(int r) { MOperand o; o.kind = MOperand::Register; o.reg = static_cast<std::uint8_t>(r); return o; }
MOperand RR(int hi, int lo) { MOperand o = R(lo); o.kind = MOperand::Pair; o.hi = static_cast<std::uint8_t>(hi); return o; }
MOperand K(long long v) { return MOperand(v); }
char sideOfR(int r) { return r == kNoReg ? 0 : sideOfReg(r) ? 'B' : 'A'; }
// Registers in their names' order (A10 before A2): the order a map of names walked, which the schedule's ties keep.
struct ByName {
    static std::string key(int r) { return r == kNoReg ? std::string() : regName(r); }
    bool operator()(int a, int b) const { return key(a) < key(b); }
};
// What no pass renames: A15, B3, B14 and B15.
const std::uint64_t kKeptM = (1ull << 15) | (1ull << 35) | (1ull << 46) | (1ull << 47);
MInst entry(const std::string &mnem, const MOperand &a, const MOperand &b = MOperand(), const MOperand &c = MOperand(), int pred = kNoReg, bool no = false) {
    MInst e(mnem.c_str(), a, b, c);
    e.pred = static_cast<std::uint8_t>(pred); e.predNot = no;
    return e;
}
Line lineOf(const MInst &e) {
    std::vector<std::string> ops;
    for (int k = 0; k < e.operands; k++) ops.push_back(e.op[k].str());
    return rebuilt(e.m.str(), ops, e.pred == kNoReg ? std::string() : (e.predNot ? "!" : "") + regName(e.pred));
}

struct Edge { int from, to, delay, dist; const char *why; };

struct Loop {
    std::size_t head = 0, back = 0;
    std::string label, exitName;
    bool negated = false, boundConst = false, plusOne = false;
    long boundK = 0;
    std::size_t cmp = 0;
    std::vector<std::size_t> bodyAt;    // the body's instruction lines, the compare and the branch left out
    std::vector<Line> body, pre;          // the body, and the block that falls into the head
    std::vector<MInst> preM;              // `pre` as entries, where every one is `modelled`
    bool preTyped = true;
    // The body as the stream's entries (M6c round 5), kept equal to `body` through every edit; empty off the stream or
    // where an entry is not `modelled`. What the body hands on - reads, writes, pairs, nodes - is read from it there.
    std::vector<MInst> bodyM;
    bool fromLines = false;
    std::vector<std::size_t> taken;       // where each entry of bodyM stands on the stream
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::uint64_t liveExit = 0;
    // A counter stepped by more than one: the test stays in the body and the kernel's branch reads it against a bound
    // moved back by the turns in flight, so no turn the loop would not have run is begun. Low word first throughout.
    bool stepped = false, wide = false;
    std::vector<int> ivsR, boundsR, stepsR;     // the counter's words, the bound's and the step's: a constant step is kNoReg, its value stepK
    long stepK = 0;
    std::vector<std::size_t> testBody;          // the body's compares that read a bound
    // **The loop's registers as numbers and masks** (M6c rounds 12 and 13), read from the entries' operands: the counter,
    // the bound, the predicate and the count, kNoReg where a constant stands; `pinnedM` is a stepped counter's words, bounds and steps.
    int ivR = kNoReg, boundR = kNoReg, PR = kNoReg, CR = kNoReg;
    std::uint64_t usedM = 0, writtenM = 0, predicatedM = 0, rmwFirstM = 0, pinnedM = 0;
    void count(std::uint64_t reads, std::uint64_t writes, bool plain) { usedM |= reads | writes; writtenM |= writes; if (!plain) predicatedM |= writes; }
    void masks(std::size_t o, std::uint64_t &reads, std::uint64_t &writes) const {
        if (!bodyM.empty()) { readsAndWrites(bodyM[o], reads, writes); return; }
        std::vector<std::string> r, w;
        readsAndWrites(body[o], r, w);
        reads |= maskOf(r); writes |= maskOf(w);
    }
    static std::uint64_t bit(int r) { return bitR(r); }
    // **The body read without its lines** (M6c round 14): on the stream the entries alone; a line is beside one only
    // where an entry or the preheader is not modelled. Text off the stream, as it was.
    std::size_t size() const { return bodyM.empty() ? body.size() : bodyM.size(); }
    std::string mnem(std::size_t o) const { return bodyM.empty() ? body[o].mnem : bodyM[o].m.str(); }
    std::size_t ops(std::size_t o) const { return bodyM.empty() ? body[o].ops.size() : bodyM[o].operands; }
    std::string op(std::size_t o, std::size_t k) const { return bodyM.empty() ? body[o].ops[k] : bodyM[o].op[k].str(); }
    std::string raw(std::size_t o) const { return body.empty() ? lineOf(bodyM[o]).raw : body[o].raw; }
    // What a pass may not rename or fold away: the counter, a register bound, the predicate, and with `count` the count.
    std::uint64_t fixedM(bool count) const { return bit(ivR) | bit(boundR) | bit(PR) | (count ? bit(CR) : 0); }
    bool plainAt(std::size_t o) const { return bodyM.empty() ? body[o].pred.empty() : bodyM[o].pred == kNoReg; }
    void rw(std::size_t o, std::vector<std::string> &reads, std::vector<std::string> &writes) const {
        if (!bodyM.empty()) readsAndWrites(bodyM[o], reads, writes); else readsAndWrites(body[o], reads, writes);
    }
    // The same by number, in the order `rw` lists them.
    void regs(std::size_t o, std::vector<int> &reads, std::vector<int> &writes) const {
        if (!bodyM.empty()) { readsAndWrites(bodyM[o], reads, writes); return; }
        std::vector<std::string> r, w;
        rw(o, r, w);
        for (std::size_t k = 0; k < r.size(); k++) reads.push_back(regNumber(r[k]));
        for (std::size_t k = 0; k < w.size(); k++) writes.push_back(regNumber(w[k]));
    }
    // Operand k of body line o as a pair `hi:lo` of two registers.
    bool pairIn(std::size_t o, std::size_t k, int &hi, int &lo) const {
        if (!bodyM.empty()) {
            const MOperand &x = bodyM[o].op[k];
            if (x.kind != MOperand::Pair) return false;
            hi = x.hi; lo = x.reg;
            return true;
        }
        std::vector<std::string> regs;
        registersIn(body[o].ops[k], regs);
        if (body[o].ops[k].find(':') == std::string::npos || regs.size() != 2) return false;
        hi = regNumber(regs[0]); lo = regNumber(regs[1]);
        return true;
    }
    // `steppedRegister` of body line o by number: the base its first stepped access writes, kNoReg where it has an index.
    int steppedR(std::size_t o) const {
        long by;
        if (bodyM.empty()) return regNumber(steppedRegister(body[o], by));
        for (int k = 0; k < bodyM[o].operands; k++) {
            const MOperand &x = bodyM[o].op[k];
            if (x.kind == MOperand::Memory && x.mode >= MOperand::PreInc) return x.index == kNoReg ? x.reg : kNoReg;
        }
        return kNoReg;
    }
    // The address forms of the body's accesses: read from the entries on the stream, from the lines off it.
    std::vector<Access> addresses() const {
        std::vector<Ins> b, p;
        const bool typed = !bodyM.empty() && preTyped;
        for (std::size_t o = 0; o < size(); o++) b.push_back(typed ? insOf(bodyM[o]) : insOf(body[o]));
        for (std::size_t o = 0; o < (typed ? preM.size() : pre.size()); o++) p.push_back(typed ? insOf(preM[o]) : insOf(pre[o]));
        const std::vector<Access> got = addressForms(b, p, ivR, writtenM);
        return got;
    }
    void erase(std::size_t o) {
        if (!body.empty()) body.erase(body.begin() + static_cast<long>(o));
        if (!bodyM.empty()) bodyM.erase(bodyM.begin() + static_cast<long>(o));
    }
    // `renameWebs`'s edit on entry o: `from` named `to` in its sources (a write: its destination, and MVKH's and ADDK's
    // always), as `renamed` spells it - a register, a pair's half, an address's base or index - and a read in its predicate.
    void rename(std::size_t o, int f, int t, bool isWrite) {
        if (bodyM.empty()) return;
        MInst &e = bodyM[o];
        const std::string m = e.m.str();
        const bool hasDest = e.operands && !isStore(m) && m != "B" && e.op[e.operands - 1].kind != MOperand::Memory;
        for (int k = 0; k < e.operands; k++) {
            const bool dest = hasDest && k + 1 == e.operands;
            if (dest != isWrite && !(dest && (m == "MVKH" || m == "ADDK"))) continue;
            MOperand &x = e.op[k];
            if (x.kind != MOperand::Register && x.kind != MOperand::Pair && x.kind != MOperand::Memory) continue;
            if (x.reg == f) x.reg = static_cast<std::uint8_t>(t);
            if (x.kind == MOperand::Pair && x.hi == f) x.hi = static_cast<std::uint8_t>(t);
            if (x.kind == MOperand::Memory && x.index == f) x.index = static_cast<std::uint8_t>(t);
        }
        if (!isWrite && e.pred == f) e.pred = static_cast<std::uint8_t>(t);
    }
    void take(const Src &src, std::size_t i) {
        const bool beside = !preTyped;
        if (!fromLines && !src.typed(i)) {              // one entry not modelled: the whole body from its lines
            fromLines = true;
            for (std::size_t k = 0; k < taken.size(); k++) if (!beside) body.push_back(src.line(taken[k]));
            bodyM.clear();
        }
        if (fromLines) { body.push_back(src.line(i)); return; }
        bodyM.push_back((*src.s)[i]);
        taken.push_back(i);
        if (beside) body.push_back(src.line(i));
    }
};

const int kCondR[] = { 0, 1, 2, 32, 33, 34 };

// ----- the body read as algebra: what each register holds at its end, as a term over the values it came in with -----

// **The algebra's terms by number** (M6c round 15): each made once in a table, so two are equal where their numbers
// are. The value R held at entry, a constant, a flat sorted sum, `lt(a,b)` and `ltu(a,b)`, eq, and, or of a sorted
// pair, and `op` for anything else; -1 is no term. `str` spells one for a trace.
struct Terms {
    enum Kind { Entry, Symbol, Const, Op, Sum, Lt, Ltu, Eq, And, Or };
    struct T { Kind kind; long k; int reg; std::string text; std::vector<int> args; };
    std::vector<T> all;
    std::map<std::pair<std::pair<int, long>, std::pair<std::pair<int, std::string>, std::vector<int> > >, int> made;
    int make(Kind kind, long k, int reg, const std::string &text, const std::vector<int> &args) {
        const std::pair<std::pair<int, long>, std::pair<std::pair<int, std::string>, std::vector<int> > > key(std::make_pair(static_cast<int>(kind), k), std::make_pair(std::make_pair(reg, text), args));
        if (made.count(key)) return made[key];
        T t; t.kind = kind; t.k = k; t.reg = reg; t.text = text; t.args = args;
        all.push_back(t);
        return made[key] = static_cast<int>(all.size()) - 1;
    }
    const T &at(int t) const { return all[static_cast<std::size_t>(t)]; }
    int entry(int r) { return make(Entry, 0, r, std::string(), std::vector<int>()); }           // a register's value at the turn's start
    int symbol(const std::string &x) { return make(Symbol, 0, kNoReg, x, std::vector<int>()); }
    int constant(long k) { return make(Const, k, kNoReg, std::string(), std::vector<int>()); }
    int op(long k, int r) { return make(Op, k, r, std::string(), std::vector<int>()); }         // what body line k leaves in r
    int ordered(Kind kind, int a, int b) { std::vector<int> v; v.push_back(a); v.push_back(b); return make(kind, 0, kNoReg, std::string(), v); }
    int sorted(Kind kind, std::vector<int> v) { std::sort(v.begin(), v.end()); return make(kind, 0, kNoReg, std::string(), v); }
    int pair(Kind kind, int a, int b) { std::vector<int> v; v.push_back(a); v.push_back(b); return sorted(kind, v); }
    int sum(int a, int b) {
        std::vector<int> v;
        const int parts[2] = { a, b };
        for (int i = 0; i < 2; i++) {
            if (at(parts[i]).kind == Sum) v.insert(v.end(), at(parts[i]).args.begin(), at(parts[i]).args.end());
            else v.push_back(parts[i]);
        }
        return sorted(Sum, v);
    }
    bool is(int t, Kind kind) const { return t >= 0 && at(t).kind == kind; }
    // Whether `sub` is t or a term inside it; no term is inside every one, as an empty spelling was.
    bool contains(int t, int sub) const {
        if (sub < 0 || t == sub) return true;
        if (t < 0) return false;
        for (std::size_t a = 0; a < at(t).args.size(); a++) if (contains(at(t).args[a], sub)) return true;
        return false;
    }
    std::string str(int t) const {
        if (t < 0) return std::string();
        const T &x = at(t);
        if (x.kind == Entry) return "s:" + regName(x.reg);
        if (x.kind == Symbol) return "s:" + x.text;
        if (x.kind == Const) return "k:" + std::to_string(x.k);
        if (x.kind == Op) return "op:" + std::to_string(x.k) + ":" + regName(x.reg);
        std::vector<std::string> ts;
        for (std::size_t a = 0; a < x.args.size(); a++) ts.push_back(str(x.args[a]));
        if (x.kind == Lt || x.kind == Ltu) return (x.kind == Lt ? "lt(" : "ltu(") + ts[0] + "," + ts[1] + ")";
        std::sort(ts.begin(), ts.end());
        std::string s = x.kind == Sum ? "sum{" : x.kind == Eq ? "eq{" : x.kind == And ? "and{" : "or{";
        for (std::size_t i = 0; i < ts.size(); i++) s += (i ? "," : "") + ts[i];
        return s + "}";
    }
};

bool in(const std::vector<int> &v, int r) { return std::find(v.begin(), v.end(), r) != v.end(); }

// The terms of every register after the body, and the term P is computed from, by one pass in program order: keyed by
// the register's number and walked in the names' order.
typedef std::map<int, int, ByName> Env;
Env evaluateBody(const Src &src, const Loop &L, Terms &T, std::vector<int> &lineTerms) {
    Env env;
    auto name = [&](int r) -> int {
        if (!env.count(r)) env[r] = T.entry(r);
        return env[r];
    };
    std::size_t i = 0;
    auto term = [&](std::size_t k) -> int {
        long n;
        if (src.number(i, k, n)) return T.constant(n);
        return src.regAt(i, k) != kNoReg ? name(src.regAt(i, k)) : T.symbol(src.op(i, k));
    };
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        i = L.bodyAt[k];
        const std::string m = src.mnem(i);
        std::vector<int> reads, writes;
        src.regs(i, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) name(reads[r]);
        int t = -1;
        const bool unpred = src.pred(i).empty(), plain = unpred && src.ops(i) == 3 && writes.size() == 1 && src.regAt(i, 2) == writes[0];
        const int t0 = (plain || m == "MV") && src.ops(i) > 0 ? term(0) : -1, t1 = plain ? term(1) : -1;
        if (m == "MV" && unpred && src.ops(i) == 2 && writes.size() == 1) t = t0;
        else if (plain && m == "ADD") t = T.sum(t0, t1);
        else if (plain && m == "CMPLT") t = T.ordered(Terms::Lt, t0, t1);
        else if (plain && m == "CMPGT") t = T.ordered(Terms::Lt, t1, t0);
        else if (plain && m == "CMPLTU") t = T.ordered(Terms::Ltu, t0, t1);
        else if (plain && m == "CMPGTU") t = T.ordered(Terms::Ltu, t1, t0);
        else if (plain && (m == "CMPEQ" || m == "AND" || m == "OR")) t = T.pair(m == "CMPEQ" ? Terms::Eq : m == "AND" ? Terms::And : Terms::Or, t0, t1);
        for (std::size_t w = 0; w < writes.size(); w++) env[writes[w]] = t < 0 || writes.size() > 1 ? T.op(static_cast<long>(k), writes[w]) : t;
        lineTerms.push_back(t < 0 || writes.size() != 1 ? -1 : env[writes[0]]);
    }
    return env;
}

// **A counter stepped by more than one, or sixty-four bits wide**: the body, read as algebra, must leave registers lo (and
// hi) as `lo + s` (and `hi + shi + carry`) for s the loop never writes, and compute P as `lo' < n` or `n < lo'` - the two-word
// signed form `hi'<nhi | (hi'==nhi & lo' <u nlo)` for a wide one - so the loop runs while iv < n or iv <= n by its own test.
bool recogniseStepped(const Src &src, Loop &L, std::string &why, const std::string &fallback) {
    why = fallback;
    auto no = [&](const char *r) { if (tracing() > 1) why += std::string(" [stepped: ") + r + "]"; return false; };
    L.liveExit = src.liveIn(L.back + 1);
    if (L.liveExit & Loop::bit(L.PR)) return no("the predicate lives past the loop");
    std::map<int, int> writers;
    std::map<int, std::size_t> writerAt;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        std::vector<int> reads, writes;
        src.regs(L.bodyAt[k], reads, writes);
        for (std::size_t w = 0; w < writes.size(); w++) { writers[writes[w]]++; writerAt[writes[w]] = k; }
    }
    if (writers[L.PR] != 1) return no("the predicate is written more than once");
    Terms T;
    std::vector<int> lineTerms;
    Env env = evaluateBody(src, L, T, lineTerms);
    auto termOf = [&](int r) -> int { Env::const_iterator it = env.find(r); return it == env.end() ? -1 : it->second; };
    // The steps: a register whose end term is itself plus one invariant (lo), and one that adds a carry of lo's sum (hi).
    // An invariant term: a constant (reg kNoReg, its value k) or the entry value of a register the loop never writes.
    auto invariant = [&](int t, int &reg, long &k) -> bool {
        reg = kNoReg; k = 0;
        if (T.is(t, Terms::Const)) { k = T.at(t).k; return true; }
        if (!T.is(t, Terms::Entry)) return false;
        reg = T.at(t).reg;
        return !writers.count(reg);
    };
    int lo = kNoReg, slo = kNoReg, hi = kNoReg, shi = kNoReg;
    long sloK = 0;
    int sloTerm = -1;
    const int pTerm = termOf(L.PR);
    for (Env::const_iterator it = env.begin(); it != env.end() && lo == kNoReg; ++it) {
        const int r = it->first, t = it->second;
        if (!writers.count(r) || !T.is(t, Terms::Sum)) continue;
        const int a = T.entry(r);
        if (t == T.sum(a, T.constant(0)) || !T.contains(pTerm, t)) continue;            // the counter the test reads
        const std::vector<int> ts = T.at(t).args;
        if (ts.size() != 2) continue;
        const int other = ts[0] == a ? ts[1] : ts[1] == a ? ts[0] : -1;
        int s = kNoReg; long k = 0;
        if (other < 0 || !invariant(other, s, k) || (s == kNoReg && k == 0)) continue;
        lo = r; slo = s; sloK = k; sloTerm = other;
    }
    if (lo == kNoReg) return no("no counter stepped by an invariant");
    const int loTerm = termOf(lo), carry = T.ordered(Terms::Ltu, loTerm, sloTerm);
    for (Env::const_iterator it = env.begin(); it != env.end() && hi == kNoReg; ++it) {
        const int r = it->first, t = it->second;
        if (r == lo || !writers.count(r) || !T.is(t, Terms::Sum) || !T.contains(t, carry)) continue;
        const int self = T.entry(r), rest = T.sum(self, carry);
        // t is sum{s:r, carry, step}: the step is what remains when those two are taken out.
        int cand = -1;
        const std::vector<int> parts = T.at(t).args;
        if (parts.size() != 3) continue;
        for (std::size_t p = 0; p < parts.size(); p++) if (parts[p] != self && parts[p] != carry) cand = parts[p];
        int s = kNoReg; long k = 0;
        if (cand < 0 || !invariant(cand, s, k) || T.sum(rest, cand) != t) continue;
        hi = r; shi = s;
    }
    L.wide = hi != kNoReg;
    if (L.wide && ((slo == kNoReg) != (shi == kNoReg))) return no("the two steps are not both registers or both constants");
    // The predicate: the stepped value against a bound the loop never writes, one or two words, signed.
    const int p = pTerm, hiTerm = L.wide ? termOf(hi) : -1;
    if (tracing() > 2) std::fprintf(stderr, "  stepped: lo %s + %s, hi %s, %s = %s\n", regName(lo).c_str(), T.str(sloTerm).c_str(), T.str(hiTerm).c_str(), regName(L.PR).c_str(), T.str(p).c_str());
    int nlo = kNoReg, nhi = kNoReg;
    bool less = false;                    // the loop runs while iv < n; else while iv <= n
    auto boundOf = [&](int t, int &reg) -> bool { long k; return T.is(t, Terms::Entry) && invariant(t, reg, k) && reg != lo && reg != hi; };
    if (!L.wide) {
        if (!T.is(p, Terms::Lt)) return no("the predicate is not a compare");
        const int x = T.at(p).args[0], y = T.at(p).args[1];
        if (x == loTerm && boundOf(y, nlo)) less = true;
        else if (y == loTerm && boundOf(x, nlo)) less = false;
        else return no("the compare is not of the stepped counter against an invariant");
    } else {
        // or{and{eq{hi',nhi},ltu(lo',nlo)},lt(hi',nhi)} for iv < n; the operands the other way round for n < iv.
        bool found = false;
        for (int dir = 0; dir < 2 && !found; dir++) {
            for (Env::const_iterator a = env.begin(); a != env.end() && !found; ++a) {
                int nl = kNoReg, nh = kNoReg;
                if (!boundOf(T.entry(a->first), nl) || a->first == lo || a->first == hi) continue;
                for (Env::const_iterator b = env.begin(); b != env.end() && !found; ++b) {
                    if (!boundOf(T.entry(b->first), nh) || b->first == a->first) continue;
                    const int NL = T.entry(nl), NH = T.entry(nh);
                    const int ltu = dir == 0 ? T.ordered(Terms::Ltu, loTerm, NL) : T.ordered(Terms::Ltu, NL, loTerm);
                    const int lt = dir == 0 ? T.ordered(Terms::Lt, hiTerm, NH) : T.ordered(Terms::Lt, NH, hiTerm);
                    if (p == T.pair(Terms::Or, T.pair(Terms::And, T.pair(Terms::Eq, hiTerm, NH), ltu), lt)) { nlo = nl; nhi = nh; less = dir == 0; found = true; }
                }
            }
        }
        if (!found) return no("the two-word compare is not of the stepped counter against an invariant");
    }
    // `[P] B` with P = iv < n runs while iv < n; `[!P] B` with P = n < iv runs while iv <= n; the other two count down.
    if (less == L.negated) return no("the branch sense runs the loop while the test fails");
    L.plusOne = !less;
    // The lines that feed P, by the last writer before each read; the compares reading a bound must be among them,
    // must compare the stepped counter's term, and must be read by nothing else - they are the lines the kernel's
    // moved bound is substituted into.
    auto lastWriterBefore = [&](int x, std::size_t k) -> long {
        for (std::size_t j = k; j-- > 0;) {
            std::vector<int> reads, writes;
            src.regs(L.bodyAt[j], reads, writes);
            if (in(writes, x)) return static_cast<long>(j);
        }
        return -1;
    };
    std::set<std::size_t> feeding;
    std::vector<std::size_t> work(1, writerAt[L.PR]);
    while (!work.empty()) {
        std::size_t k = work.back(); work.pop_back();
        if (!feeding.insert(k).second) continue;
        std::vector<int> reads, writes;
        src.regs(L.bodyAt[k], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) { long w = lastWriterBefore(reads[r], k); if (w >= 0) work.push_back(static_cast<std::size_t>(w)); }
    }
    std::vector<std::size_t> tree;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        std::vector<int> reads, writes;
        src.regs(i, reads, writes);
        const bool rlo = in(reads, nlo), rhi = L.wide && in(reads, nhi);
        if (!rlo && !rhi) continue;
        if (!feeding.count(k) || src.ops(i) != 3 || !src.pred(i).empty() || src.mnem(i).compare(0, 3, "CMP") != 0 || writes.size() != 1) return no("a bound is read outside the test");
        const int dst = src.regAt(i, 2);
        if (dst != kNoReg && (dst == nlo || dst == nhi)) return no("the test writes a bound");
        const int lt = lineTerms[k], want = rlo ? loTerm : hiTerm, bound = T.entry(rlo ? nlo : nhi);
        if (lt < 0 || !T.contains(lt, want) || !T.contains(lt, bound)) return no("the test reads the counter before its step");
        for (std::size_t k2 = k + 1; k2 < L.bodyAt.size(); k2++) {
            std::vector<int> r2, w2;
            src.regs(L.bodyAt[k2], r2, w2);
            if (in(r2, writes[0]) && !feeding.count(k2)) return no("a bound's compare is read outside the test");
            if (in(w2, writes[0])) break;
        }
        tree.push_back(k);
    }
    if (tree.empty()) return no("no compare reads the bound");
    if (slo == kNoReg && L.wide) return no("a constant step on a two-word counter");
    if (slo == kNoReg && sloK <= 0) return no("a step that is not positive");
    L.stepped = true;
    L.cmp = L.bodyAt[writerAt[L.PR]];
    L.ivR = lo; L.boundR = nlo; L.CR = kNoReg;
    L.ivsR.push_back(lo); L.boundsR.push_back(nlo); L.stepsR.push_back(slo);
    if (L.wide) { L.ivsR.push_back(hi); L.boundsR.push_back(nhi); L.stepsR.push_back(shi); }
    L.stepK = sloK;
    L.usedM = L.writtenM = L.predicatedM = L.pinnedM = 0;
    for (std::size_t w = 0; w < L.ivsR.size(); w++) L.pinnedM |= Loop::bit(L.ivsR[w]) | Loop::bit(L.boundsR[w]) | Loop::bit(L.stepsR[w]);
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        std::uint64_t rm = 0, wm = 0;
        src.masks(i, rm, wm);
        L.count(rm, wm, src.pred(i).empty());
        if (src.mnem(i) == "MV" && src.ops(i) == 2 && src.op(i, 0) == src.op(i, 1)) continue;
        if (std::find(tree.begin(), tree.end(), k) != tree.end()) L.testBody.push_back(L.size());
        L.take(src, i);
    }
    why.clear();
    return true;
}

bool recognise(const Src &src, std::size_t back, const std::set<std::string> &labels, const std::set<std::string> &named, Loop &L, std::string &why) {
    const std::string bpred = src.pred(back);
    if (bpred.empty() || src.ops(back) == 0 || labels.count(src.op(back, 0)) == 0) { why = "no conditional branch to a label"; return false; }
    L.label = src.op(back, 0);
    if (L.label.size() > 5 && L.label.compare(L.label.size() - 5, 5, "$fill") == 0) { why = "a fill loop, memory-bound, kept as written"; return false; }
    L.PR = src.predReg(back);
    L.negated = bpred[0] == '!';
    std::size_t h = 0; bool found = false;
    for (std::size_t i = 0; i < back; i++) if (src.label(i) && src.name(i) == L.label) { h = i; found = true; }
    if (!found) { why = "the head is not above"; return false; }
    if (back + 1 >= src.size() || !src.label(back + 1)) { why = "no exit label after the branch"; return false; }
    L.head = h; L.back = back; L.exitName = src.name(back + 1);
    std::size_t first = h;
    for (std::size_t k = h; k-- > 0 && src.instr(k) && !isBranch(src.mnem(k)); first = k) {
        if (src.typed(k)) L.preM.insert(L.preM.begin(), (*src.s)[k]); else L.preTyped = false;
    }
    // The preheader's lines: off the stream, and where an entry of it is not modelled.
    if (!L.preTyped) for (std::size_t k = first; k < h; k++) L.pre.push_back(src.line(k));
    if (src.naming(L.label) != 1) { why = "the head is entered from elsewhere"; return false; }
    for (std::size_t i = h + 1; i < back; i++) {
        if (src.label(i)) { if (!passThroughName(src.name(i), named)) { why = "a label inside"; return false; } continue; }
        if (!src.instr(i)) { why = "a directive inside"; return false; }
        if (isBranch(src.mnem(i))) { why = "a branch inside"; return false; }
        if (src.units(i) == 0) { why = "an instruction of no known unit: " + src.mnem(i); return false; }
        std::vector<int> reads, writes;
        src.regs(i, reads, writes);
        if (in(writes, 15) || in(writes, 47)) { why = "the stack pointer is written"; return false; }
        L.bodyAt.push_back(i);
    }
    if (L.bodyAt.size() < 2 || L.bodyAt.size() > 64) { why = "too small or too large"; return false; }
    // The compare: the last writer of P, of a form the count can be read from.
    bool cmpFound = false;
    for (std::size_t k = L.bodyAt.size(); k-- > 0;) {
        std::vector<int> reads, writes;
        src.regs(L.bodyAt[k], reads, writes);
        if (in(writes, L.PR)) { L.cmp = L.bodyAt[k]; cmpFound = true; break; }
    }
    if (!cmpFound) { why = "the predicate is not written in the loop"; return false; }
    const std::size_t c = L.cmp;
    const std::string cm = src.mnem(c);
    bool lt = cm == "CMPLT" || cm == "CMPLTU", gt = cm == "CMPGT" || cm == "CMPGTU";
    if ((!lt && !gt) || src.ops(c) != 3 || !src.pred(c).empty() || src.regAt(c, 2) != L.PR) return recogniseStepped(src, L, why, "the test is not a compare of two");
    // Loops while i < n: [P] with i < n or n > i; while i <= n: [!P] with i > n or n < i.
    const std::size_t ivAt = lt == !L.negated ? 0 : 1, boundAt = 1 - ivAt;
    L.ivR = src.regAt(c, ivAt); L.boundR = src.regAt(c, boundAt);
    L.plusOne = L.negated;
    long k0 = 0;
    const bool boundNumber = src.number(c, boundAt, k0);
    if (boundNumber) { L.boundConst = true; L.boundK = k0; }
    else if (!src.reg(c, boundAt)) { why = "the bound is not a register"; return false; }
    if (!src.reg(c, ivAt) || (L.boundConst && L.ivR != src.regAt(c, 1))) { why = "the counter is not a register"; return false; }
    // P is read by the branch alone and is dead at the exit; i is stepped once, by one, before the test.
    L.liveExit = src.liveIn(back + 1);
    if (L.liveExit & Loop::bit(L.PR)) { why = "the predicate lives past the loop"; return false; }
    int ivWrites = 0, boundWrites = 0, pWrites = 0;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        const bool plain = src.pred(i).empty();
        std::vector<int> reads, writes;
        src.regs(i, reads, writes);
        if (in(reads, L.PR) && pWrites == 0) { why = "the predicate's test is read in the loop"; return false; }
        if (in(writes, L.ivR)) {
            ivWrites++;
            long one = 0;
            bool step = src.mnem(i) == "ADD" && plain && src.ops(i) == 3 && src.regAt(i, 2) == L.ivR &&
                        ((src.number(i, 0, one) && one == 1 && src.regAt(i, 1) == L.ivR) || (src.number(i, 1, one) && one == 1 && src.regAt(i, 0) == L.ivR));
            if (!step || i > L.cmp) return recogniseStepped(src, L, why, "the counter is not stepped by one before the test");
        }
        long kb = 0;
        if (!boundNumber && in(writes, L.boundR) && i < L.cmp) {
            boundWrites++;
            if (src.mnem(i) == "MVK" && plain && src.ops(i) == 2 && src.number(i, 0, kb)) { L.boundConst = true; L.boundK = kb; }
            else L.boundConst = false;
        }
        if (in(writes, L.PR) && i != L.cmp) pWrites++;
        std::uint64_t rm = 0, wm = 0;
        src.masks(i, rm, wm);
        L.count(rm, wm, plain);
    }
    if (ivWrites != 1) { why = "the counter is stepped " + std::to_string(ivWrites) + " times"; return false; }
    if (boundWrites > 0 && !L.boundConst) { why = "the bound is written in the loop"; return false; }
    // The kernel's counter: P itself where the compare alone writes it, else a condition register the loop leaves alone.
    if (pWrites == 0) L.CR = L.PR;
    else for (std::size_t k = 0; k < 6 && L.CR == kNoReg; k++)
        if (!((L.usedM | L.liveExit) & Loop::bit(kCondR[k])) && kCondR[k] != L.PR) L.CR = kCondR[k];
    if (L.CR == kNoReg) { why = "no condition register for the count"; return false; }
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        if (i != L.cmp && !(src.mnem(i) == "MV" && src.ops(i) == 2 && src.op(i, 0) == src.op(i, 1))) L.take(src, i);
    }
    return true;
}

int antiDelay(const Node &r, const Node &w) { return std::max(r.store ? 1 : 0, r.late - w.lat + 1); }

// The dependences of one iteration and the next: register flow, anti and output orders in program order, the
// same wrapped round the back edge - the anti and output ones only for a register kept under one name - and memory.
void buildEdges(Loop &L, std::uint64_t renameable) {
    std::vector<Node> &n = L.nodes;
    L.edges.clear();
    std::map<int, std::vector<std::pair<int, bool> >, ByName> acc;      // register -> (op, isWrite) in order
    for (std::size_t o = 0; o < n.size(); o++) {
        std::vector<int> reads, writes;
        L.regs(o, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) acc[reads[r]].push_back(std::make_pair(static_cast<int>(o), false));
        for (std::size_t w = 0; w < writes.size(); w++) acc[writes[w]].push_back(std::make_pair(static_cast<int>(o), true));
    }
    for (std::map<int, std::vector<std::pair<int, bool> >, ByName>::iterator it = acc.begin(); it != acc.end(); ++it) {
        const std::vector<std::pair<int, bool> > &a = it->second;
        const bool kept = !(renameable & Loop::bit(it->first));         // under one name every turn
        // A writer's result lands at its latency - but an address it steps lands in E1, a cycle after issue.
        auto latOf = [&](int w) { return (n[static_cast<std::size_t>(w)].stepped & Loop::bit(it->first)) ? 1 : n[static_cast<std::size_t>(w)].lat; };
        int firstW = -1, lastW = -1;
        for (std::size_t k = 0; k < a.size(); k++) if (a[k].second) { if (firstW < 0) firstW = static_cast<int>(k); lastW = static_cast<int>(k); }
        if (firstW < 0) continue;
        for (std::size_t k = 0; k < a.size(); k++) {
            int prevW = -1, nextW = -1;
            for (int m = static_cast<int>(k) - 1; m >= 0; m--) if (a[static_cast<std::size_t>(m)].second) { prevW = m; break; }
            for (std::size_t m = k + 1; m < a.size(); m++) if (a[m].second) { nextW = static_cast<int>(m); break; }
            const int op = a[k].first;
            if (!a[k].second) {
                if (prevW >= 0 && a[static_cast<std::size_t>(prevW)].first != op) { Edge e = { a[static_cast<std::size_t>(prevW)].first, op, latOf(a[static_cast<std::size_t>(prevW)].first), 0, "flow" }; L.edges.push_back(e); }
                if (prevW < 0) { Edge e = { a[static_cast<std::size_t>(lastW)].first, op, latOf(a[static_cast<std::size_t>(lastW)].first), 1, "flow1" }; L.edges.push_back(e); }
                if (nextW >= 0 && a[static_cast<std::size_t>(nextW)].first != op && (prevW >= 0 || kept)) { const Node &w = n[static_cast<std::size_t>(a[static_cast<std::size_t>(nextW)].first)]; Edge e = { op, a[static_cast<std::size_t>(nextW)].first, antiDelay(n[static_cast<std::size_t>(op)], w), 0, "anti" }; L.edges.push_back(e); }
                if (nextW < 0 && kept) { const Node &w = n[static_cast<std::size_t>(a[static_cast<std::size_t>(firstW)].first)]; Edge e = { op, a[static_cast<std::size_t>(firstW)].first, antiDelay(n[static_cast<std::size_t>(op)], w), 1, "anti1" }; L.edges.push_back(e); }
            } else if (nextW >= 0) {
                const Node &w1 = n[static_cast<std::size_t>(op)], &w2 = n[static_cast<std::size_t>(a[static_cast<std::size_t>(nextW)].first)];
                Edge e = { op, a[static_cast<std::size_t>(nextW)].first, std::max(1, w1.lat - w2.lat + 1), 0, "out" }; L.edges.push_back(e);
            } else if (kept) {
                const Node &w1 = n[static_cast<std::size_t>(op)], &w2 = n[static_cast<std::size_t>(a[static_cast<std::size_t>(firstW)].first)];
                Edge e = { op, a[static_cast<std::size_t>(firstW)].first, std::max(1, w1.lat - w2.lat + 1), 1, "out1" }; L.edges.push_back(e);
            }
        }
    }
    if (tracing() > 3) for (std::size_t e = 0; e < L.edges.size(); e++) std::fprintf(stderr, "  edge %s %d->%d (%d,%d)\n", L.edges[e].why, L.edges[e].from, L.edges[e].to, L.edges[e].delay, L.edges[e].dist);
    std::vector<Access> addr = L.addresses();
    for (std::size_t a = 0; a < n.size(); a++) {
        if (!n[a].mem) continue;
        for (std::size_t b = a; b < n.size(); b++) {
            if (!n[b].mem || (!n[a].store && !n[b].store)) continue;
            if (a != b && mayAliasAt(addr[a], n[a].memSize, addr[b], n[b].memSize, 0)) { Edge e = { static_cast<int>(a), static_cast<int>(b), 1, 0, "mem" }; L.edges.push_back(e); }
            if (mayAliasAt(addr[a], n[a].memSize, addr[b], n[b].memSize, 1)) { Edge e = { static_cast<int>(a), static_cast<int>(b), 1, 1, "mem1" }; L.edges.push_back(e); }
            if (a != b && mayAliasAt(addr[b], n[b].memSize, addr[a], n[a].memSize, 1)) { Edge e = { static_cast<int>(b), static_cast<int>(a), 1, 1, "mem1r" }; L.edges.push_back(e); }
        }
    }
}

// The longest path through one iteration, as a list schedule would take at least - what pipelining is measured against.
int criticalPath(const Loop &L) {
    std::vector<int> t(L.nodes.size(), 0);
    int len = 0;
    for (std::size_t o = 0; o < L.nodes.size(); o++) {
        for (std::size_t e = 0; e < L.edges.size(); e++)
            if (L.edges[e].dist == 0 && L.edges[e].to == static_cast<int>(o)) t[o] = std::max(t[o], t[static_cast<std::size_t>(L.edges[e].from)] + L.edges[e].delay);
        len = std::max(len, t[o] + L.nodes[o].lat);
        // The sequential loop's branch waits for its predicate - the body's own, or the compare of the stepped counter - and
        // the next turn starts six cycles after it, which the overlapped kernel never pays.
        const std::uint64_t gate = Loop::bit(L.stepped ? L.PR : L.ivR);
        if (L.nodes[o].writes & gate) len = std::max(len, t[o] + L.nodes[o].lat + (L.stepped ? 0 : 1) + 6);
    }
    return std::max(len, 6);
}

// One try at an initiation interval: each instruction in program order at the first cycle in the window its
// scheduled neighbours leave it whose modulo slot has room - the branch and the count's step reserved first.
bool scheduleAt(Loop &L, int II, std::vector<int> &t, std::uint64_t renameMask, std::map<int, int> &minQ, std::vector<int> &push) {
    const std::size_t n = L.nodes.size();
    t.assign(n, -1);
    minQ.clear();
    std::vector<Packet> mrt(static_cast<std::size_t>(II));
    std::vector<Node> extra;
    extra.reserve(2 + n * 5);
    std::set<std::string> lab;
    lab.insert(L.label + "$pipe");
    // The kernel's branch, on the count where there is one and on the loop's own test where the counter is stepped.
    extra.push_back(makeNode(lineOf(entry("B", MOperand(L.label + "$pipe"), MOperand(), MOperand(), L.stepped ? L.PR : L.CR, L.stepped && L.negated)), lab));
    if (!L.stepped) extra.push_back(makeNode(lineOf(entry("ADD", K(-1), R(L.CR), R(L.CR))), std::set<std::string>()));
    const int sb = ((6 % II) == 0 ? 0 : II - 6 % II);
    for (std::size_t k = 0; k < extra.size(); k++) { if (!fits(mrt[static_cast<std::size_t>(sb)], extra[k])) return false; add(mrt[static_cast<std::size_t>(sb)], extra[k]); }
    for (std::size_t o = 0; o < n; o++) {
        const Node &x = L.nodes[o];
        if (x.late + 1 > II) return false;
        int lo = push[o], hi = 1 << 30, hiBy = -1;
        for (std::size_t e = 0; e < L.edges.size(); e++) {
            const Edge &d = L.edges[e];
            if (d.to == static_cast<int>(o) && d.from == static_cast<int>(o)) { if (d.delay > d.dist * II) return false; continue; }
            if (d.to == static_cast<int>(o) && t[static_cast<std::size_t>(d.from)] >= 0) lo = std::max(lo, t[static_cast<std::size_t>(d.from)] + d.delay - d.dist * II);
            if (d.from == static_cast<int>(o) && t[static_cast<std::size_t>(d.to)] >= 0) {
                if (tracing() > 2 && t[static_cast<std::size_t>(d.to)] - d.delay + d.dist * II < hi) std::fprintf(stderr, "    hi by%s (%s delay %d dist %d)\n", L.raw(static_cast<std::size_t>(d.to)).c_str(), d.why, d.delay, d.dist);
                if (t[static_cast<std::size_t>(d.to)] - d.delay + d.dist * II < hi) { hi = t[static_cast<std::size_t>(d.to)] - d.delay + d.dist * II; hiBy = d.dist ? d.to : -1; }
            }
        }
        bool placed = false;
        // A renamed register may be written twice in one slot: the two are different iterations' copies, so q says so.
        extra.push_back(x);
        Node &xv = extra.back();
        xv.writes &= ~renameMask;
        for (int c = lo; c <= hi && c < lo + II; c++) {
            const std::size_t s = static_cast<std::size_t>(c % II);
            if (!fits(mrt[s], xv)) continue;
            bool held = true;
            for (int k = 1; k <= x.late && held; k++) {
                Node ph; ph.units = x.units; ph.side = x.side; ph.cross = x.cross;
                held = fits(mrt[static_cast<std::size_t>((c + k) % II)], ph);
            }
            if (!held) continue;
            t[o] = c;
            add(mrt[s], xv);
            for (int k = 1; k <= x.late; k++) {
                Node ph; ph.units = x.units; ph.side = x.side; ph.cross = x.cross;
                extra.push_back(ph);
                add(mrt[static_cast<std::size_t>((c + k) % II)], extra.back());
            }
            placed = true;
            break;
        }
        if (!placed) {
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: no slot for%s in [%d,%d]\n", II, L.raw(o).c_str(), lo, hi);
            if (hi < lo && hiBy >= 0) push[static_cast<std::size_t>(hiBy)] += lo - hi;
            return false;
        }
    }
    // Two writes of one renamed register in one slot are different iterations' copies: q must tell them apart.
    for (std::size_t o = 0; o < n; o++)
        for (std::size_t p = 0; p < o; p++) {
            const std::uint64_t both = L.nodes[p].writes & L.nodes[o].writes & renameMask;
            if (!both || t[p] % II != t[o] % II) continue;
            for (int r = 0; r < 64; r++) if (both & (1ull << r)) minQ[r] = std::max(minQ[r], std::abs(t[o] - t[p]) / II + 1);
        }
    return true;
}

// A register's rename unit: itself, or the even:odd pair it is half of.
struct Unit { std::vector<int> regs; int q = 1; std::vector<std::vector<int> > copies; bool carried = false, exits = false; };
typedef std::map<int, std::size_t, ByName> UnitOf;        // a register's unit, walked in the names' order

int pow2At(int x) { int p = 1; while (p < x) p *= 2; return p; }
long modq(long a, int q) { return ((a % q) + q) % q; }

// Body instruction o as turn j writes it: each register of a unit renamed to that turn's copy - a source to the copy
// it reads, the destination to the one it writes - on the entry where the body is the stream's, else on its line.
std::string spelledLine(const Loop &L, const std::vector<Unit> &units, const UnitOf &unitOf,
                        const std::vector<int> &firstWrite, std::size_t o, long j) {
    const Line made = L.body.empty() ? lineOf(L.bodyM[o]) : Line();
    const Line &l = L.body.empty() ? made : L.body[o];
    std::vector<std::string> ops = l.ops;
    std::string pred = l.pred;
    bool hasDest = !ops.empty() && !isStore(l.mnem) && l.mnem != "B" && ops.back()[0] != '*';
    std::size_t k0 = 0;
    for (UnitOf::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k0++) {
        const Unit &u = units[it->second];
        if (u.q < 2) continue;
        const std::string r = regName(it->first);
        std::size_t half = 0;
        while (u.regs[half] != it->first) half++;
        const long readJ = j + (firstWrite[k0] >= static_cast<int>(o) ? -1 : 0);
        const std::string rd = regName(u.copies[half][static_cast<std::size_t>(modq(readJ, u.q))]), wr = regName(u.copies[half][static_cast<std::size_t>(modq(j, u.q))]);
        for (std::size_t k = 0; k < ops.size(); k++) {
            bool dest = hasDest && k + 1 == ops.size();
            if (dest && (l.mnem == "MVKH" || l.mnem == "ADDK")) { ops[k] = renamed(ops[k], r, wr); continue; }
            ops[k] = renamed(ops[k], r, dest ? wr : rd);
        }
        if (!pred.empty()) pred = renamed(pred, r, rd);
    }
    return rebuilt(l.mnem, ops, pred).raw;
}

// One instruction of a kernel's packet: its spelling, and on the stream the entry itself (typed).
struct Turn { std::string raw; MInst e; bool typed = false; };

Turn spelled(const Loop &L, const std::vector<Unit> &units, const UnitOf &unitOf,
             const std::vector<int> &firstWrite, std::size_t o, long j) {
    Turn turn;
    if (L.bodyM.empty() || L.bodyM[o].unit != 0 || L.bodyM[o].par || L.bodyM[o].packed) { turn.raw = spelledLine(L, units, unitOf, firstWrite, o, j); return turn; }
    MInst x = L.bodyM[o];
    const std::string m = x.m.str();
    const bool hasDest = x.operands && !isStore(m) && m != "B" && x.op[x.operands - 1].kind != MOperand::Memory;
    std::size_t k0 = 0;
    for (UnitOf::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k0++) {
        const Unit &u = units[it->second];
        if (u.q < 2) continue;
        std::size_t half = 0;
        while (u.regs[half] != it->first) half++;
        const long readJ = j + (firstWrite[k0] >= static_cast<int>(o) ? -1 : 0);
        const int r = it->first, rd = u.copies[half][static_cast<std::size_t>(modq(readJ, u.q))], wr = u.copies[half][static_cast<std::size_t>(modq(j, u.q))];
        for (int k = 0; k < x.operands; k++) {
            MOperand &y = x.op[k];
            const std::uint8_t to = static_cast<std::uint8_t>(hasDest && k + 1 == x.operands ? wr : rd);
            if (y.kind != MOperand::Register && y.kind != MOperand::Pair && y.kind != MOperand::Memory) continue;
            if (y.reg == r) y.reg = to;
            if (y.kind == MOperand::Pair && y.hi == r) y.hi = to;
            if (y.kind == MOperand::Memory && y.index == r) y.index = to;
        }
        if (x.pred == r) x.pred = static_cast<std::uint8_t>(rd);
    }
    turn.raw = x.str();
    turn.raw.erase(turn.raw.size() - 1);
    turn.e = x;
    turn.typed = true;
    return turn;
}

Line verbatim(const std::string &raw) { Line l; l.raw = raw; l.verbatim = true; return l; }

// **What the emitters write** (M6c round 11): entries - an instruction built from its parts, a turn the body's
// renamed entry, placed as it is put. `ok` false: one the stream cannot hold.
struct Code {
    MStream m;
    const MStream *from = nullptr;          // the stream being rewritten
    bool ok = true;
    // An entry made whole (the window's).
    void put(const MInst &e) { if (ok) m.push_back(e); }
    // An instruction of its operands - registers by number, constants, a label - under `[pred]` or `[!pred]`.
    void add(const std::string &mnem, const MOperand &a, const MOperand &b = MOperand(), const MOperand &c = MOperand(), int pred = kNoReg, bool no = false) {
        put(entry(mnem, a, b, c, pred, no));
    }
    void branch(const std::string &to, int pred, bool no) { add("B", MOperand(to), MOperand(), MOperand(), pred, no); }
    // `legalForm` and the instruction: commuted where both sources cross, false with the reason where it has no form.
    bool legal(std::string &why, const std::string &mnem, const MOperand &a, const MOperand &b = MOperand(), const MOperand &c = MOperand()) {
        MInst e = entry(mnem, a, b, c);
        if (!ok || !modelled(e)) {
            Line l = lineOf(e);
            if (!legalForm(l)) { why = "the stepped guard has no legal form for" + l.raw; return false; }
            ok = false;
            return true;
        }
        const bool commutes = mnem == "ADD" || mnem == "AND" || mnem == "OR" || mnem == "XOR" || mnem == "MPYDP" || mnem == "ADDDP" || mnem == "MPYSP" ||
                              mnem == "ADDSP" || mnem == "MPY32" || mnem == "CMPEQ";
        if (crossings(e) > 1 && commutes && e.operands == 3 && e.op[0].kind != MOperand::Immediate && e.op[1].kind != MOperand::Immediate) std::swap(e.op[0], e.op[1]);
        const bool has = crossings(e) <= 1 && (unitsFor(e) != 0 || isLoad(mnem) || isStore(mnem));
        std::string raw = e.str();
        raw.erase(raw.size() - 1);
        if (!has) { why = "the stepped guard has no legal form for" + raw; return false; }
        m.push_back(e);
        return true;
    }
    // A placed line - a turn, idle cycles, the kernel's label: the entry given, else the line read as placed.
    void placed(const std::string &raw, bool par, const MInst *e) {
        if (!ok) return;
        if (!e) { ok = streamOf(std::vector<Line>(1, verbatim((par ? "||" : "") + raw)), m); return; }
        m.push_back(*e);
        m.back().par = par;
        m.back().packed = true;
    }
    void nop(int n) { const MInst e("NOP", n); placed("\tNOP\t" + std::to_string(n), false, &e); }
    void label(const std::string &name) { MInst e; e.kind = MInst::Label; e.m = opt::StrId(name); placed(name + ":", false, &e); }
    // Line k of the function as it stands: the stream's own entry.
    void same(std::size_t k) { if (ok) m.push_back((*from)[k]); }
};

Turn turnOf(const MInst &e) {
    Turn t;
    t.raw = lineOf(e).raw;
    t.e = e;
    t.typed = true;
    return t;
}

void emitCycles(Code &out, const std::vector<std::vector<Turn> > &cycles) {
    int nops = 0;
    for (std::size_t c = 0; c < cycles.size(); c++) {
        if (cycles[c].empty()) { nops++; continue; }
        while (nops > 0) { out.nop(std::min(nops, 9)); nops -= std::min(nops, 9); }
        for (std::size_t k = 0; k < cycles[c].size(); k++) out.placed(cycles[c][k].raw, k != 0, cycles[c][k].typed ? &cycles[c][k].e : nullptr);
    }
    while (nops > 0) { out.nop(std::min(nops, 9)); nops -= std::min(nops, 9); }
}

// A register web: one value, from its write to its last read; linked to its partner's where it is half a pair.
struct Web { int reg = kNoReg, name = kNoReg; int link = -1; bool keep = false; };

// Each write of a temporary starts a web of its own, and every web but the first and the one a later iteration or
// the exit reads takes a fresh name from the pool - so a name the backend recycles through an iteration becomes
// several short-lived ones, each renamed per iteration on its own account.
void renameWebs(Loop &L, std::vector<int> &pool) {
    std::vector<Web> webs;
    std::map<int, int, ByName> cur;
    std::uint64_t fixed = 0;                                // registers no web of which may be renamed
    std::vector<std::vector<std::pair<int, int> > > opWebs(L.size());   // (web, isWrite) per op
    for (std::size_t o = 0; o < L.size(); o++) {
        const std::string m = L.mnem(o);
        const std::size_t nops = L.ops(o);
        std::vector<int> reads, writes;
        L.regs(o, reads, writes);
        bool rmw = m == "MVKH" || m == "ADDK" || !L.plainAt(o) || L.steppedR(o) != kNoReg;
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (!cur.count(reads[r])) { Web w; w.reg = reads[r]; w.keep = true; cur[reads[r]] = static_cast<int>(webs.size()); webs.push_back(w); }
            opWebs[o].push_back(std::make_pair(cur[reads[r]], 0));
        }
        // A pair operand links its halves' current webs - the sources before the writes are seen, the
        // destination after; a half linked twice over keeps its names.
        bool hasDest = nops != 0 && !isStore(m) && m != "B" && L.op(o, nops - 1)[0] != '*';
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) for (std::size_t w = 0; w < writes.size(); w++) {
                if (!rmw || !cur.count(writes[w])) { Web nw; nw.reg = writes[w]; cur[writes[w]] = static_cast<int>(webs.size()); webs.push_back(nw); }
                opWebs[o].push_back(std::make_pair(cur[writes[w]], 1));
            }
            for (std::size_t k = 0; k < nops; k++) {
                const bool dest = hasDest && k + 1 == nops;
                if (dest != (pass == 1)) continue;
                int h = kNoReg, w = kNoReg;
                if (!L.pairIn(o, k, h, w) || !cur.count(h) || !cur.count(w)) continue;
                Web &hi = webs[static_cast<std::size_t>(cur[h])], &lo = webs[static_cast<std::size_t>(cur[w])];
                if ((hi.link >= 0 && hi.link != cur[w]) || (lo.link >= 0 && lo.link != cur[h])) fixed |= Loop::bit(h) | Loop::bit(w);
                hi.link = cur[w]; lo.link = cur[h];
            }
        }
    }
    for (std::map<int, int, ByName>::const_iterator it = cur.begin(); it != cur.end(); ++it) {
        const std::uint64_t bit = Loop::bit(it->first);
        bool carried = false;
        for (std::size_t w = 0; w < webs.size(); w++) if (webs[w].reg == it->first && webs[w].keep) carried = true;
        if (carried || (L.liveExit & bit) || ((L.fixedM(true) | L.pinnedM) & bit)) webs[static_cast<std::size_t>(it->second)].keep = true;
    }
    for (std::size_t w = 0; w < webs.size(); w++) {
        Web &x = webs[w];
        if (x.name == kNoReg) x.name = x.reg;
        if (x.keep || ((fixed | kKeptM) & Loop::bit(x.reg))) continue;
        if (x.link >= 0 && (webs[static_cast<std::size_t>(x.link)].keep || (fixed & Loop::bit(webs[static_cast<std::size_t>(x.link)].reg)))) continue;
        if (x.name != x.reg) continue;                      // named with its partner already
        for (std::size_t f = 0; f < pool.size(); f++) {
            if (sideOfReg(pool[f]) != sideOfReg(x.reg)) continue;
            if (x.link < 0) { x.name = pool[f]; pool.erase(pool.begin() + static_cast<long>(f)); break; }
            if ((pool[f] & 31) % 2) continue;
            const int lo = pool[f], hi = lo + 1;
            std::vector<int>::iterator ih = std::find(pool.begin(), pool.end(), hi);
            if (ih == pool.end()) continue;
            Web &y = webs[static_cast<std::size_t>(x.link)];
            const bool xLow = (x.reg & 31) % 2 == 0;
            x.name = xLow ? lo : hi; y.name = xLow ? hi : lo;
            pool.erase(ih); pool.erase(pool.begin() + static_cast<long>(f));
            break;
        }
    }
    for (std::size_t o = 0; o < L.size(); o++) {
        const bool text = !L.body.empty();                  // a line beside the entry: renamed as the entry is
        const std::string m = L.mnem(o);
        std::vector<std::string> ops;
        std::string pred;
        if (text) { ops = L.body[o].ops; pred = L.body[o].pred; }
        bool hasDest = !ops.empty() && !isStore(m) && m != "B" && ops.back()[0] != '*';
        for (std::size_t k = 0; k < opWebs[o].size(); k++) {
            const Web &w = webs[static_cast<std::size_t>(opWebs[o][k].first)];
            if (w.name == w.reg) continue;
            const bool isWrite = opWebs[o][k].second == 1;
            const std::string was = regName(w.reg), now = regName(w.name);     // the line's spelling, kept beside the entry
            for (std::size_t p = 0; p < ops.size(); p++) {
                bool dest = hasDest && p + 1 == ops.size();
                if (dest == isWrite || (dest && (m == "MVKH" || m == "ADDK"))) ops[p] = renamed(ops[p], was, now);
            }
            if (text && !isWrite) pred = renamed(pred, was, now);
            L.rename(o, w.reg, w.name, isWrite);
        }
        if (text) L.body[o] = rebuilt(m, ops, pred);
    }
    L.usedM = L.writtenM = L.predicatedM = 0;
    for (std::size_t o = 0; o < L.size(); o++) {
        std::uint64_t rm = 0, wm = 0;
        L.masks(o, rm, wm);
        L.count(rm, wm, L.plainAt(o));
    }
    if (L.PR != kNoReg) L.usedM |= 1ull << L.PR;
    std::uint64_t seenM = 0;
    for (std::size_t o = 0; o < L.size(); o++) {
        std::uint64_t rm = 0, wm = 0;
        L.masks(o, rm, wm);
        if (L.mnem(o) == "MVKH" || L.mnem(o) == "ADDK") L.rmwFirstM |= wm & ~seenM;
        L.rmwFirstM |= wm & bitR(L.steppedR(o));
        seenM |= wm;
    }
}

// Whether register r is read before it is written on the path from line k round the back edge to line o - the
// value r holds after line o is wanted - or is live at the exit.
bool wantedAfter(const Loop &L, std::size_t o, const std::string &r) {
    const std::size_t size = L.size();
    for (std::size_t n = 0, k = o + 1; n < size; n++, k = (k + 1) % size) {
        std::vector<std::string> reads, writes;
        L.rw(k, reads, writes);
        if (has(reads, r)) return true;
        if (has(writes, r)) return false;
    }
    return true;
}

// **The body's own copies**: `MV S, D` copied straight back by `MV D, S` with neither written between is one copy, and
// a copy nothing reads before the register is written again - round the back edge too, the exit not reading it - is none.
// The backend saves a result across the address it computes next, and the save is a web the schedule would carry.
void foldBodyCopies(Loop &L) {
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t o = 0; o < L.size() && !changed; o++) {
            if (L.mnem(o) != "MV" || L.ops(o) != 2 || !L.plainAt(o) || !sideOf(L.op(o, 0)) || !sideOf(L.op(o, 1))) continue;
            const std::string S = L.op(o, 0), D = L.op(o, 1);
            if (D == "A15" || D == "B15" || D == "B3" || D == "B14" || (L.fixedM(false) & bitOf(D))) continue;
            for (std::size_t k = o + 1; k < L.size(); k++) {
                std::vector<std::string> reads, writes;
                L.rw(k, reads, writes);
                if (L.mnem(k) == "MV" && L.ops(k) == 2 && L.plainAt(k) && L.op(k, 0) == D && L.op(k, 1) == S) { L.erase(k); changed = true; break; }
                if (has(writes, S) || has(writes, D)) break;
            }
            if (changed) break;
            if ((L.liveExit & bitOf(D)) || wantedAfter(L, o, D)) continue;
            L.erase(o);
            changed = true;
        }
    }
}


// A register of free's on the side asked for, or any; kNoReg for none. With pair, an aligned even:odd pair's even half.
int takeFree(std::vector<int> &free, char side, bool pair) {
    for (int pass = 0; pass < 2; pass++)
        for (std::size_t f = 0; f < free.size(); f++) {
            if (pass == 0 && sideOfR(free[f]) != side) continue;
            const int r = free[f];
            if (!pair) { free.erase(free.begin() + static_cast<long>(f)); return r; }
            if ((r & 31) % 2) continue;
            std::vector<int>::iterator ih = std::find(free.begin(), free.end(), r + 1);
            if (ih == free.end()) continue;
            free.erase(ih); free.erase(free.begin() + static_cast<long>(f));
            return r;
        }
    return kNoReg;
}

// The loop's own test in one canonical shape: P = iv < n where the loop runs while iv < n, P = n < iv where it runs
// while iv <= n; a two-word counter signed through the high word and unsigned through the low, over two scratch registers.
bool steppedTest(const Loop &L, Code &out, int nlo, int nhi, int t, int tb, std::string &why) {
    const bool less = !L.plusOne;
    const int lo = L.ivsR[0];
    const bool across = sideOfReg(L.PR) != sideOfReg(lo);    // the result is made beside the counter and copied to P
    if (!L.wide) {
        if (!across) return out.legal(why, less ? "CMPLT" : "CMPGT", R(lo), R(nlo), R(L.PR));
        if (t == kNoReg) { why = "the stepped test has no scratch register beside the counter"; return false; }
        return out.legal(why, less ? "CMPLT" : "CMPGT", R(lo), R(nlo), R(t)) && out.legal(why, "MV", R(t), R(L.PR));
    }
    const int hi = L.ivsR[1], x = across ? tb : L.PR;
    return out.legal(why, "CMPEQ", R(hi), R(nhi), R(t)) && out.legal(why, less ? "CMPLTU" : "CMPGTU", R(lo), R(nlo), R(tb)) &&
           out.legal(why, "AND", R(t), R(tb), R(t)) && out.legal(why, less ? "CMPLT" : "CMPGT", R(hi), R(nhi), R(tb)) &&
           out.legal(why, "OR", R(t), R(tb), R(x)) && (!across || out.legal(why, "MV", R(x), R(L.PR)));
}

// **The stepped loop's entry**: the step must not be negative, K = m*s and Kg = g*s must fit, n-K (the kernel's bound)
// and n-Kg must not overflow, and turn g must exist - `iv REL n-Kg` by the loop's own test - or the loop runs as written.
// Every check branches to the head, and the test is the loop's own over the bound n - Kg; one or two words.
bool steppedGuard(const Loop &L, Code &out, const std::vector<int> &moved, int xt, int xtb, std::vector<int> free, int m, int g, std::string &why) {
    const int slo = L.stepsR[0], nlo = L.boundsR[0], shi = L.wide ? L.stepsR[1] : kNoReg, nhi = L.wide ? L.boundsR[1] : kNoReg, P = L.PR;
    const bool constant = slo == kNoReg;
    const char stepSide = sideOfR(constant ? nlo : slo);
    const int klo = takeFree(free, stepSide, true), tm = takeFree(free, stepSide, false), t2 = takeFree(free, stepSide, false), tb = takeFree(free, sideOfR(nlo), false);
    if (klo == kNoReg || tm == kNoReg || t2 == kNoReg || tb == kNoReg) { why = "no free registers for the stepped guard"; return false; }
    const int khi = klo + 1;
    // Each branch past the pipelined copy is to the head: taken where the loop would not go on, or a check fails.
    if (!constant && !out.legal(why, "CMPGT", K(0), R(L.wide ? shi : slo), R(P))) return false;
    if (!constant) out.branch(L.label, P, false);
    // K = k*s into khi:klo, checked to fit: a 31-bit value for one word, a 47-bit step for two.
    auto times = [&](int k) -> bool {
        if (constant) {
            long kk = k * L.stepK;
            if (kk < 0 || kk > 0x7fffffff) { why = "the stepped guard's constant does not fit"; return false; }
            if (kk <= 32767) out.add("MVK", K(kk), R(klo));
            else { out.add("MVKL", K(kk), R(klo)); out.add("MVKH", K(kk), R(klo)); }
            return true;
        }
        out.add("MVK", K(k), R(tm));
        if (!out.legal(why, "MPY32U", R(slo), R(tm), RR(khi, klo))) return false;
        if (!L.wide) {
            if (!out.legal(why, "CMPGT", K(0), R(klo), R(tb)) || !out.legal(why, "XOR", K(1), R(tb), R(tb))) return false;
            if (!out.legal(why, "CMPEQ", K(0), R(khi), R(P)) || !out.legal(why, "AND", R(P), R(tb), R(P))) return false;
            out.branch(L.label, P, true);
            return true;
        }
        if (!out.legal(why, "MPY32", R(shi), R(tm), R(t2)) || !out.legal(why, "ADD", R(t2), R(khi), R(khi))) return false;
        if (!out.legal(why, "SHRU", R(shi), K(15), R(t2)) || !out.legal(why, "CMPEQ", K(0), R(t2), R(P))) return false;
        out.branch(L.label, P, true);
        return true;
    };
    // d = n - (khi:klo), into dlo (and dhi), then the overflow check: n negative and the difference not.
    auto minus = [&](int dlo, int dhi) -> bool {
        if (!L.wide) {
            if (!out.legal(why, "SUB", R(nlo), R(klo), R(dlo))) return false;
            if (!out.legal(why, "CMPGT", K(0), R(nlo), R(P)) || !out.legal(why, "CMPGT", K(0), R(dlo), R(tb))) return false;
        } else {
            if (!out.legal(why, "CMPLTU", R(nlo), R(klo), R(tb)) || !out.legal(why, "SUB", R(nlo), R(klo), R(dlo))) return false;
            if (!out.legal(why, "SUB", R(nhi), R(khi), R(dhi)) || !out.legal(why, "SUB", R(dhi), R(tb), R(dhi))) return false;
            if (!out.legal(why, "CMPGT", K(0), R(nhi), R(P)) || !out.legal(why, "CMPGT", K(0), R(dhi), R(tb))) return false;
        }
        if (!out.legal(why, "XOR", K(1), R(tb), R(tb)) || !out.legal(why, "AND", R(P), R(tb), R(P))) return false;
        out.branch(L.label, P, false);
        return true;
    };
    if (!times(m) || !minus(moved[0], L.wide ? moved[1] : kNoReg)) return false;
    if (g != m && !times(g)) return false;
    if (!minus(klo, khi)) return false;
    // The entry test over the bound n - Kg, then the branch past the copy where it fails.
    if (!steppedTest(L, out, klo, khi, xt, xtb, why)) return false;
    out.branch(L.label, P, !L.negated);
    return true;
}

// The whole rewrite of one recognised loop; false, with the reason, where it is not worth it or cannot be done.
bool pipeline(Loop &L, Code &out, std::string &why) {
    // The pool of names the loop may take: caller-saved, unused in it, dead at its exit.
    std::vector<int> pool;
    for (int side = 0; side < 2; side++)
        for (int k = 3; k < 32; k++) {
            if ((k >= 10 && k <= 15) || (side == 1 && k == 3)) continue;
            const int r = side * 32 + k;
            if (!((L.usedM | L.liveExit) & Loop::bit(r)) && r != L.CR && r != L.PR) pool.push_back(r);
        }
    int X = kNoReg;
    if (!L.stepped) {
        for (std::size_t k = 0; k < pool.size() && X == kNoReg; k++) if (sideOfR(pool[k]) == sideOfR(L.CR)) X = pool[k];
        if (X == kNoReg) { why = "no free register for the count"; return false; }
        pool.erase(std::find(pool.begin(), pool.end(), X));
    }
    // A stepped counter's kernel tests the moved bound: a fresh register per word, taking the bound's own side.
    std::vector<int> moved;
    for (std::size_t w = 0; w < L.boundsR.size(); w++) {
        int f = kNoReg;
        for (int pass = 0; pass < 2 && f == kNoReg; pass++)
            for (std::size_t k = 0; k < pool.size() && f == kNoReg; k++)
                if (pass == 1 || sideOfR(pool[k]) == sideOfR(L.boundsR[w])) { f = pool[k]; pool.erase(pool.begin() + static_cast<long>(k)); }
        if (f == kNoReg) { why = "no free register for the moved bound"; return false; }
        moved.push_back(f);
    }
    // The test's scratch registers, beside the counter so that only the bound crosses: taken before the renaming can.
    int xt = kNoReg, xtb = kNoReg;
    if (L.stepped && (L.wide || sideOfReg(L.PR) != sideOfReg(L.ivsR[0]))) {
        const char beside = sideOfR(L.ivsR[0]);
        xt = takeFree(pool, beside, false);
        if (L.wide) xtb = takeFree(pool, beside, false);
        if (xt == kNoReg || (L.wide && xtb == kNoReg) || sideOfR(xt) != beside || (L.wide && sideOfR(xtb) != beside)) {
            why = "no free register beside the counter for the stepped test"; return false;
        }
    }
    for (std::size_t k = 0; k < L.testBody.size(); k++) {
        const std::size_t at = L.testBody[k];
        std::vector<std::string> ops;
        if (!L.body.empty()) ops = L.body[at].ops;
        for (std::size_t o = 0; o + 1 < L.ops(at); o++)
            for (std::size_t w = 0; w < L.boundsR.size(); w++) if (regNumber(L.op(at, o)) == L.boundsR[w]) {
                if (!L.body.empty()) { ops[o] = regName(moved[w]); L.body[at].ops[o] = ops[o]; }
                if (!L.bodyM.empty()) L.bodyM[at].op[o] = R(moved[w]);
            }
        if (!L.body.empty()) L.body[at] = rebuilt(L.mnem(at), ops, L.body[at].pred);
    }
    foldBodyCopies(L);
    if (tracing() > 4) for (std::size_t o = 0; o < L.size(); o++) std::fprintf(stderr, "  as written%s\n", L.raw(o).c_str());
    renameWebs(L, pool);
    std::set<std::string> labels;
    labels.insert(L.label + "$pipe");
    for (std::size_t o = 0; o < L.size(); o++) {
        if (L.bodyM.empty()) { L.nodes.push_back(makeNode(L.body[o], labels)); continue; }
        L.nodes.push_back(makeNode(L.bodyM[o], labels));
    }
    // The rename units, and which of them may take a name per iteration.
    std::vector<Unit> units;
    UnitOf unitOf;
    for (std::size_t o = 0; o < L.size(); o++)
        for (std::size_t k = 0; k < L.ops(o); k++) {
            int hi = kNoReg, lo = kNoReg;
            if (!L.pairIn(o, k, hi, lo)) continue;
            if (!unitOf.count(hi) && !unitOf.count(lo)) { Unit u; u.regs.push_back(lo); u.regs.push_back(hi); unitOf[hi] = unitOf[lo] = units.size(); units.push_back(u); }
        }
    std::vector<int> used;                              // the body's registers in their names' order, which is the units'
    for (int r = 0; r < 64; r++) if (L.usedM >> r & 1) used.push_back(r);
    std::sort(used.begin(), used.end(), ByName());
    for (std::vector<int>::const_iterator it = used.begin(); it != used.end(); ++it)
        if (!unitOf.count(*it)) { Unit u; u.regs.push_back(*it); unitOf[*it] = units.size(); units.push_back(u); }
    std::vector<int> firstWrite(unitOf.size(), 1 << 30);
    {
        std::size_t k = 0;
        for (UnitOf::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k++)
            for (std::size_t o = 0; o < L.nodes.size(); o++) if (L.nodes[o].writes & Loop::bit(it->first)) { firstWrite[k] = static_cast<int>(o); break; }
    }
    std::uint64_t renameable = 0;
    for (std::size_t u = 0; u < units.size(); u++) {
        bool ok = true;                                     // every half written: a pair's names move together
        std::uint64_t halves = 0;
        for (std::size_t h = 0; h < units[u].regs.size(); h++) {
            const std::uint64_t bit = Loop::bit(units[u].regs[h]);
            halves |= bit;
            if (!(L.writtenM & bit)) ok = false;
            if ((L.predicatedM | L.rmwFirstM | Loop::bit(L.CR) | Loop::bit(L.PR) | kKeptM) & bit) { ok = false; break; }
        }
        if (ok) renameable |= halves;
    }
    if (tracing() > 3) for (std::size_t o = 0; o < L.size(); o++) std::fprintf(stderr, "  body%s\n", L.raw(o).c_str());
    if (tracing() > 2) { std::fprintf(stderr, "  renameable:"); for (UnitOf::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it) if (renameable & Loop::bit(it->first)) std::fprintf(stderr, " %s", regName(it->first).c_str()); std::fprintf(stderr, "\n"); }
    std::vector<int> t;
    std::map<int, int> minQ;
    std::vector<int> free;                  // the pool less the copies: what a guard may still take
    int II = 1, S = 0, u = 1, seq = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        buildEdges(L, renameable);
        seq = criticalPath(L);
        int lateMax = 0;
        for (std::size_t o = 0; o < L.nodes.size(); o++) lateMax = std::max(lateMax, L.nodes[o].late);
        // Each II in turn: a schedule, then the copies its spans need; an II whose copies the pool cannot
        // give is passed over for the next, and only past the bound are the units without them left unrenamed.
        bool any = false, done = false;
        std::vector<int> failed;
        for (II = lateMax + 1; II <= 64 && 5 * II <= 4 * seq; II++) {
            bool ok = false;
            std::vector<int> push(L.nodes.size(), 0), was;
            for (int retry = 0; retry < 8 && !ok; retry++) { was = push; ok = scheduleAt(L, II, t, renameable, minQ, push); if (push == was) break; }
            if (!ok) continue;
            any = true;
            failed.clear();
            int len = 0;
            for (std::size_t o = 0; o < t.size(); o++) len = std::max(len, t[o] + 1);
            S = (len + II - 1) / II;
            // Each unit's span from its first write's landing to its last use, the reads of the old value a turn on.
            int qMax = 1;
            std::size_t k = 0;
            for (std::size_t x = 0; x < units.size(); x++) { units[x].q = 1; units[x].carried = units[x].exits = false; }
            for (UnitOf::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k++) {
                Unit &un = units[it->second];
                const std::uint64_t bit = Loop::bit(it->first);
                if (!(renameable & bit) || !(L.writtenM & bit)) continue;
                int start = 1 << 30, end = 0;
                for (std::size_t o = 0; o < L.nodes.size(); o++) {
                    if (L.nodes[o].writes & bit) { start = std::min(start, t[o] + L.nodes[o].lat); end = std::max(end, t[o] + L.nodes[o].lat); }
                    if (L.nodes[o].reads & bit) end = std::max(end, t[o] + L.nodes[o].late + (firstWrite[k] >= static_cast<int>(o) ? II : 0));
                }
                int q = std::max((end - start) / II + 1, minQ.count(it->first) ? minQ[it->first] : 1);
                un.q = std::max(un.q, pow2At(q));
                if (firstWrite[k] < (1 << 30)) for (std::size_t o = 0; o <= static_cast<std::size_t>(firstWrite[k]); o++) if (L.nodes[o].reads & bit) un.carried = true;
                if ((L.liveExit & bit) || ((Loop::bit(L.ivR) | Loop::bit(L.boundR) | L.pinnedM) & bit)) un.exits = true;
            }
            for (std::size_t x = 0; x < units.size(); x++) qMax = std::max(qMax, units[x].q);
            u = pow2At(std::max((6 + II - 1) / II, qMax));
            // The copies: the register itself, then q-1 more of its side - a pair's two halves aligned.
            free = pool;
            for (std::size_t x = 0; x < units.size(); x++) {
                Unit &un = units[x];
                un.copies.assign(un.regs.size(), std::vector<int>());
                for (std::size_t h = 0; h < un.regs.size(); h++) un.copies[h].push_back(un.regs[h]);
                for (int c = 1; c < un.q; c++) {
                    bool got = false;
                    for (std::size_t f = 0; f < free.size() && !got; f++) {
                        if (sideOfReg(free[f]) != sideOfReg(un.regs[0])) continue;
                        if (un.regs.size() == 1) { un.copies[0].push_back(free[f]); free.erase(free.begin() + static_cast<long>(f)); got = true; break; }
                        if ((free[f] & 31) % 2) continue;
                        const int hi = free[f] + 1;
                        std::vector<int>::iterator ih = std::find(free.begin(), free.end(), hi);
                        if (ih == free.end()) continue;
                        un.copies[0].push_back(free[f]); un.copies[1].push_back(hi);
                        free.erase(ih); free.erase(free.begin() + static_cast<long>(f)); got = true;
                    }
                    if (!got) { failed.push_back(un.regs[0]); break; }
                }
            }
            if (failed.empty()) { done = true; break; }
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: S=%d, no %d copies of %s - trying II+1\n", II, S, units[unitOf[failed[0]]].q, regName(failed[0]).c_str());
        }
        if (!any) { why = "no schedule under four fifths of the " + std::to_string(seq) + "-cycle iteration"; return false; }
        if (done) break;
        for (std::size_t f = 0; f < failed.size(); f++) {
            const Unit &un = units[unitOf[failed[f]]];
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: no %d copies of %s\n", II, un.q, regName(failed[f]).c_str());
            for (std::size_t h = 0; h < un.regs.size(); h++) renameable &= ~Loop::bit(un.regs[h]);
        }
        if (attempt == 7) { why = "no registers for the renaming"; return false; }
    }
    // A stepped counter: the kernel's branch, at cycle u*II-6 of a round, reads the test iteration k_b made of the counter
    // one turn on, so the bound it tests is moved back by m = 2u-2-d steps (d the turns between that test and the branch);
    // a round is entered only when its last-started turn exists, and the loop is entered when turns 0..u+S-2 do.
    int m = 0, g = 0;
    if (L.stepped) {
        int tP = 0;
        for (std::size_t o = 0; o < L.nodes.size(); o++) if (L.nodes[o].writes & Loop::bit(L.PR)) tP = t[o];
        auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        int d = floorDiv(u * II - 7 - tP, II);
        while (S - 1 + d < 0) { u *= 2; d = floorDiv(u * II - 7 - tP, II); }   // the first round's branch must see a test
        m = 2 * u - 2 - d; g = u + S - 2;
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: II=%d S=%d u=%d seq=%d ops=%zu%s\n", L.label.c_str(), II, S, u, seq, L.nodes.size(), L.stepped ? (L.wide ? " stepped wide" : " stepped") : "");
    if (L.stepped) { if (!steppedGuard(L, out, moved, xt, xtb, free, m, g, why)) return false; }
    // The guard: X = (n - i [+1] - (S-1)) >> log2 u, the unrolled rounds; none, and the loop is left as written.
    const bool crossBoth = !L.stepped && !L.boundConst && sideOfReg(L.boundR) != sideOfReg(X) && sideOfReg(L.ivR) != sideOfReg(X);
    if (L.stepped) { }
    else if (L.boundConst) {
        if (L.boundK >= -32768 && L.boundK <= 32767) out.add("MVK", K(L.boundK), R(X));
        else { out.add("MVKL", K(L.boundK), R(X)); out.add("MVKH", K(L.boundK), R(X)); }
        out.add("SUB", R(X), R(L.ivR), R(X));
    } else if (crossBoth) { out.add("MV", R(L.boundR), R(X)); out.add("SUB", R(X), R(L.ivR), R(X)); }
    else out.add("SUB", R(L.boundR), R(L.ivR), R(X));
    if (!L.stepped) {
        if (L.plusOne) out.add("ADD", K(1), R(X), R(X));
        if (S > 1) { if (S - 1 <= 31) out.add("SUB", R(X), K(S - 1), R(X)); else out.add("ADDK", K(1 - S), R(X)); }
        int log2u = 0;
        while ((1 << log2u) < u) log2u++;
        if (log2u) out.add("SHR", R(X), K(log2u), R(X));
        out.add("CMPGT", K(1), R(X), R(L.PR));
        out.branch(L.label, L.PR, false);
        out.add("SUB", R(X), K(1), R(L.CR));
    }
    for (std::size_t x = 0; x < units.size(); x++)
        if (units[x].q > 1 && units[x].carried) for (std::size_t h = 0; h < units[x].regs.size(); h++) out.add("MV", R(units[x].regs[h]), R(units[x].copies[h][static_cast<std::size_t>(units[x].q - 1)]));
    // The prologue, the kernel of u rounds with its branch, and the epilogue - every op of every iteration once.
    const std::size_t n = L.nodes.size();
    std::vector<std::vector<Turn> > cyc;
    for (int a = 0; a < (S - 1) * II; a++) {
        std::vector<Turn> p;
        for (std::size_t o = 0; o < n; o++) if (t[o] <= a && (a - t[o]) % II == 0) p.push_back(spelled(L, units, unitOf, firstWrite, o, (a - t[o]) / II));
        cyc.push_back(p);
    }
    emitCycles(out, cyc);
    cyc.clear();
    out.label(L.label + "$pipe");
    for (int c = 0; c < u; c++)
        for (int s = 0; s < II; s++) {
            std::vector<Turn> p;
            for (std::size_t o = 0; o < n; o++) if (t[o] % II == s) p.push_back(spelled(L, units, unitOf, firstWrite, o, S - 1 + c - t[o] / II));
            if (c * II + s == u * II - 6) {
                p.push_back(turnOf(entry("B", MOperand(L.label + "$pipe"), MOperand(), MOperand(), L.stepped ? L.PR : L.CR, L.stepped && L.negated)));
                if (!L.stepped) p.push_back(turnOf(entry("ADD", K(-1), R(L.CR), R(L.CR))));
            }
            cyc.push_back(p);
        }
    emitCycles(out, cyc);
    cyc.clear();
    int epi = (S - 1) * II;
    for (std::size_t o = 0; o < n; o++) epi = std::max(epi, t[o] + L.nodes[o].lat - II);
    for (int e = 0; e < epi; e++) {
        std::vector<Turn> p;
        for (std::size_t o = 0; o < n; o++) if (t[o] - e >= II && (t[o] - e) % II == 0) p.push_back(spelled(L, units, unitOf, firstWrite, o, S - 1 - (t[o] - e) / II));
        cyc.push_back(p);
    }
    emitCycles(out, cyc);
    // The last pipelined iteration is u*q'+S-2, so its names are fixed: copied back, then the test the loop's own way.
    for (std::size_t x = 0; x < units.size(); x++)
        if (units[x].q > 1 && units[x].exits && modq(S - 2, units[x].q) != 0) for (std::size_t h = 0; h < units[x].regs.size(); h++) out.add("MV", R(units[x].copies[h][static_cast<std::size_t>(modq(S - 2, units[x].q))]), R(units[x].regs[h]));
    if (L.stepped) { if (!steppedTest(L, out, L.boundsR[0], L.wide ? L.boundsR[1] : kNoReg, xt, xtb, why)) return false; }
    else out.same(L.cmp);
    out.branch(L.exitName, L.PR, !L.negated);
    return true;
}


// ----- a loop that leaves on a byte it reads: eight turns over one aligned 8-byte block -----

// **A loop whose exit hangs on what it loads** - `while (*p) p++`, strchr, strcmp, a string hash - is unrolled eight
// turns: each turn's arithmetic runs ahead under fresh names and only a value the loop leaves behind is copied back,
// under R, the mask of the turns still alive. No turn the loop would not have run writes anything it can see.
struct Window {
    struct Ptr {
        int reg = kNoReg;
        std::map<std::size_t, long> loads;      // body index -> the byte it reads, past the turn's own value of reg
        long offmin = 0, offmax = 0;
        bool cond = false;                      // the first byte read only after an exit: the block is read under G
        std::size_t anchor = 0;
    };
    // An exit: target "" for the fall-through after the back branch; taken where `pred` is not zero, `no` where it is.
    struct Exit {
        std::string target;
        int pred = kNoReg; bool no = false;
        std::uint64_t live = 0;
    };
    std::size_t head = 0, back = 0;
    std::string label;
    int PR = kNoReg; bool backNot = false;      // the back branch's predicate, `[!P]` backNot; kNoReg: unconditional
    MStream bodyM;                              // the instructions in order, the back branch left out
    std::string mnem(std::size_t k) const { return bodyM[k].m.str(); }
    std::vector<std::size_t> at;                // where each stands in the text
    std::vector<int> cls;                       // a branch's exit, an instruction under one's predicate that exit, else kPlain or kCont
    std::vector<Exit> exits;                    // the exits inside, in order, then the back branch's fall-through
    std::uint64_t liveHead = 0, liveOut = 0;    // live at the head; live where any exit goes
    std::vector<Ptr> ptrs;
};
const int kPlain = -1, kCont = -2;

// A byte load's address as base and constant: `*R`, `*+R(c)`, `*-R(c)`, `*+R[c]`, `*R++`, `*R++(1)`; post for the two last.
bool byteAddress(const std::string &op, std::string &base, long &off, bool &post) {
    post = false; off = 0;
    if (op.size() < 3 || op[0] != '*') return false;
    std::string s = op.substr(1);
    char sign = 0;
    if (s[0] == '+' || s[0] == '-') { if (s.size() > 1 && (s[1] == '+' || s[1] == '-')) return false; sign = s[0]; s = s.substr(1); }
    std::size_t i = 0;
    while (i < s.size() && std::isalnum(static_cast<unsigned char>(s[i]))) i++;
    base = s.substr(0, i);
    if (!sideOf(base)) return false;
    const std::string rest = s.substr(i);
    if (rest.empty()) return sign == 0;
    if (rest == "++" || rest == "++(1)") { post = true; return sign == 0; }
    if (!sign || rest.size() < 3 || !((rest[0] == '(' && rest.back() == ')') || (rest[0] == '[' && rest.back() == ']'))) return false;
    const std::string k = rest.substr(1, rest.size() - 2);
    if (!isNumber(k)) return false;
    off = std::atol(k.c_str()) * (sign == '-' ? -1 : 1);
    return true;
}

// A byte load's address on an entry's operand, as the spelling's reader above gives it.
bool byteAddress(const MOperand &o, int &base, long &off, bool &post) {
    post = false; off = 0;
    if (o.kind != MOperand::Memory || o.index != kNoReg) return false;
    base = o.reg;
    if (o.mode == MOperand::Base) return !o.hasOffset;
    if (o.mode == MOperand::PostInc) { post = true; return !o.hasOffset || (!o.scaled && o.value == 1); }
    if (o.mode != MOperand::Plus && o.mode != MOperand::Minus) return false;
    off = static_cast<long>(o.value) * (o.mode == MOperand::Minus ? -1 : 1);
    return o.hasOffset;
}

bool isByteLoad(const std::string &m) { return m == "LDB" || m == "LDBU"; }

// An operand's registers in its spelling's order, and a byte load's address as `byteAddress` reads it, on the entry.
void Src::regsIn(std::size_t i, std::size_t k, std::vector<int> &out) const {
    if (!typed(i)) {
        std::vector<std::string> names;
        registersIn(line(i).ops[k], names);
        for (std::size_t g = 0; g < names.size(); g++) out.push_back(regNumber(names[g]));
        return;
    }
    const MOperand &o = (*s)[i].op[k];
    if (o.kind == MOperand::Register) out.push_back(o.reg);
    if (o.kind == MOperand::Pair) { out.push_back(o.hi); out.push_back(o.reg); }
    if (o.kind != MOperand::Memory) return;
    out.push_back(o.reg);
    if (o.index != kNoReg) out.push_back(o.index);
}
bool Src::byteAddress(std::size_t i, std::size_t k, int &base, long &off, bool &post) const {
    if (!typed(i)) {
        std::string name;
        const bool is = c6x::byteAddress(line(i).ops[k], name, off, post);
        base = regNumber(name);
        return is;
    }
    return c6x::byteAddress((*s)[i].op[k], base, off, post);
}

// Pointer r followed through one turn: registers holding r plus a constant, through copies, constant adds and the
// steps of byte loads; true where r ends the turn one past where it began, with each byte load's offset in loads.
bool trackPointer(const Src &src, const Window &W, int r, std::map<std::size_t, long> &loads) {
    std::map<int, long> sym;
    sym[r] = 0;
    for (std::size_t k = 0; k < W.at.size(); k++) {
        const std::size_t i = W.at[k];
        const std::string m = src.mnem(i);
        if (isBranch(m)) continue;
        std::vector<int> reads, writes;
        src.regs(i, reads, writes);
        if (W.cls[k] >= 0) {                    // done only on the way out: what stays on sees none of it
            std::vector<int> regs;
            if (isLoad(m)) src.regsIn(i, 0, regs);
            for (std::size_t g = 0; g < regs.size(); g++) if (sym.count(regs[g])) return false;
            continue;
        }
        if (isLoad(m)) {
            int base = kNoReg; long off; bool post;
            std::vector<int> regs;
            src.regsIn(i, 0, regs);
            bool touches = false;
            for (std::size_t g = 0; g < regs.size(); g++) if (sym.count(regs[g])) touches = true;
            if (touches) {
                if (!isByteLoad(m) || !src.byteAddress(i, 0, base, off, post) || !sym.count(base)) return false;
                loads[k] = sym[base] + off;
                if (post) sym[base] += 1;
            }
            sym.erase(src.regAt(i, src.ops(i) - 1));
            continue;
        }
        if (writes.empty()) continue;
        const int d = writes[0];
        long v = 0, n = 0; bool known = false;
        if (m == "MV" && src.ops(i) == 2 && sym.count(src.regAt(i, 0))) { v = sym[src.regAt(i, 0)]; known = true; }
        else if ((m == "ADD" || m == "SUB") && src.ops(i) == 3) {
            const bool sub = m == "SUB";
            if (src.number(i, 0, n) && sym.count(src.regAt(i, 1)) && !sub) { v = sym[src.regAt(i, 1)] + n; known = true; }
            else if (src.number(i, 1, n) && sym.count(src.regAt(i, 0))) { v = sym[src.regAt(i, 0)] + (sub ? -1 : 1) * n; known = true; }
        }
        if (known) sym[d] = v; else sym.erase(d);
    }
    return sym.count(r) && sym[r] == 1;
}

bool recogniseWindow(const Src &src, std::size_t back, const std::set<std::string> &labels, const std::set<std::string> &named, Window &W, std::string &why) {
    if (src.mnem(back) != "B" || src.ops(back) == 0 || labels.count(src.op(back, 0)) == 0) { why = "no branch back to a label"; return false; }
    W.label = src.op(back, 0); W.back = back;
    if (src.predicated(back)) { W.PR = src.predReg(back); W.backNot = src.predNot(back); }
    if (src.predicated(back) && W.PR == kNoReg) { why = "a predicate that names no register"; return false; }
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < src.size(); i++) if (src.label(i)) at[src.name(i)] = i;
    bool found = false;
    for (std::size_t i = 0; i < back; i++) if (src.label(i) && src.name(i) == W.label) { W.head = i; found = true; }
    if (!found) { why = "the head is not above"; return false; }
    if (src.naming(W.label) != 1) { why = "the head is entered from elsewhere"; return false; }
    W.liveHead = src.liveAt(W.head);
    for (std::size_t i = W.head + 1; i < back; i++) {
        if (src.label(i)) { if (!passThroughName(src.name(i), named)) { why = "a label inside"; return false; } continue; }
        if (!src.instr(i)) { why = "a directive inside"; return false; }
        const std::string m = src.mnem(i);
        if (isBranch(m)) {
            const std::string to = src.ops(i) ? src.op(i, 0) : std::string();
            if (m != "B" || !src.predicated(i) || to.empty() || !at.count(to) || to == W.label) { why = "a branch inside that is not a way out"; return false; }
            Window::Exit e; e.target = to; e.pred = src.predReg(i); e.no = src.predNot(i);
            if (e.pred == kNoReg) { why = "a predicate that names no register"; return false; }
            if (W.PR != kNoReg && back + 1 < src.size() && src.label(back + 1) && src.name(back + 1) == e.target) e.target = "";
            e.live = src.liveAt(at[to]);
            W.cls.push_back(static_cast<int>(W.exits.size()));
            W.exits.push_back(e);
            W.liveOut |= e.live;
            W.bodyM.push_back(src.entry(i));
            W.at.push_back(i);
            continue;
        }
        if (isStore(m)) { why = "a store, which a byte read early might meet"; return false; }
        if (m == "ADDK" || m == "MVKH" || m == "MVKL") { why = "an instruction that reads its destination"; return false; }
        for (std::size_t o = 0; o < src.ops(i); o++) if (src.pair(i, o)) { why = "a register pair"; return false; }
        std::vector<int> reads, writes;
        src.regs(i, reads, writes);
        if (in(writes, 15) || in(writes, 47) || in(writes, 35)) { why = "the stack pointer or return address is written"; return false; }
        if (!isLoad(m) && (src.units(i) == 0 || writes.size() != 1)) { why = "an instruction of no known form: " + m; return false; }
        W.cls.push_back(kPlain);
        W.bodyM.push_back(src.entry(i));
        if (!modelled(W.bodyM.back())) { why = "an instruction an entry does not model"; return false; }
        W.at.push_back(i);
    }
    if (W.PR != kNoReg) {
        Window::Exit e; e.pred = W.PR; e.no = !W.backNot;
        e.live = back + 1 < src.size() ? src.liveAt(back + 1) : ~0ull;
        W.exits.push_back(e);
        W.liveOut |= e.live;
    }
    if (W.exits.empty()) { why = "no way out"; return false; }
    if (W.bodyM.empty() || W.bodyM.size() > 20) { why = "too small or too large to unroll"; return false; }
    // An instruction under a predicate belongs to the next branch, under the same one: done on the way out there,
    // or, before the back branch, only where the turn goes on. Its predicate must not change on the way.
    const std::size_t n = W.at.size();
    for (std::size_t k = 0; k < n; k++) {
        if (!src.predicated(W.at[k]) || isBranch(src.mnem(W.at[k]))) continue;
        const int q = src.predReg(W.at[k]);
        const bool no = src.predNot(W.at[k]);
        if (q == kNoReg) { why = "a predicate that names no register"; return false; }
        std::size_t j = k + 1;
        for (; j < n; j++) {
            if (isBranch(src.mnem(W.at[j]))) break;
            std::vector<int> reads, writes;
            src.regs(W.at[j], reads, writes);
            if (in(writes, q)) { why = "a predicate written under its own instructions"; return false; }
        }
        if (j < n && src.predicated(W.at[j]) && src.predReg(W.at[j]) == q && src.predNot(W.at[j]) == no) W.cls[k] = W.cls[j];
        else if (j == n && q == W.PR && no == W.backNot) W.cls[k] = kCont;
        else { why = "an instruction under a predicate no branch after it takes"; return false; }
    }
    // The pointers stepped by one a turn, followed through copies and constant offsets to the byte loads that read them.
    std::set<int> tried;
    for (std::size_t k = 0; k < n; k++) {
        std::vector<int> reads, writes;
        src.regs(W.at[k], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (!(W.liveHead & bitR(reads[r])) || !tried.insert(reads[r]).second) continue;
            Window::Ptr p;
            p.reg = reads[r];
            if (!trackPointer(src, W, p.reg, p.loads) || p.loads.empty()) continue;
            bool first = true, exited = false;
            for (std::size_t b = 0; b < n; b++) {
                if (isBranch(src.mnem(W.at[b]))) { exited = true; continue; }
                std::map<std::size_t, long>::const_iterator it = p.loads.find(b);
                if (it == p.loads.end()) continue;
                if (src.side(W.at[b], src.ops(W.at[b]) - 1) != sideOfR(p.reg)) { first = false; p.loads.clear(); break; }
                if (first || it->second < p.offmin) { p.offmin = it->second; p.anchor = b; p.cond = exited; }
                if (first || it->second > p.offmax) p.offmax = it->second;
                first = false;
            }
            if (p.loads.empty()) continue;
            if (p.offmax - p.offmin > 4) { why = "the bytes of one turn lie too far apart"; return false; }
            W.ptrs.push_back(p);
        }
    }
    if (W.ptrs.empty()) { why = "no byte load through a pointer stepped by one"; return false; }
    if (W.ptrs.size() > 3) { why = "more pointers than the block reads can carry"; return false; }
    // A load through any other register is read where it stands, under R: it must not step its register.
    for (std::size_t k = 0; k < n; k++) {
        if (!isLoad(src.mnem(W.at[k]))) continue;
        std::vector<int> reads, writes;
        src.regs(W.at[k], reads, writes);
        bool stepped = false;
        for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].loads.count(k)) stepped = true;
        if (!stepped && writes.size() != 1) { why = "a load that steps its own register"; return false; }
    }
    why.clear();
    return true;
}

// **The window's code before its registers are given** (M6c round 15): each operand an entry's, its registers numbers
// beside it - a physical one its own, a virtual one kVirt and up - so no instruction is spelled before `giveRegisters`.
const int kVirt = 256;
struct WinCode {
    struct Arg { MOperand o; int reg = kNoReg, index = kNoReg; };
    struct Op { std::string mnem; int pred = kNoReg; std::vector<Arg> ops; };
    std::vector<Op> ops;
    std::vector<char> side;
    std::set<int> boolean;                  // made by a compare: 0 or 1
    std::map<int, std::pair<int, long> > affine;    // a register as another plus a constant
    int fresh(char s) { side.push_back(s); return kVirt + static_cast<int>(side.size()) - 1; }
    void put(const std::string &m, const std::vector<Arg> &o, int pred = kNoReg) { Op x; x.mnem = m; x.ops = o; x.pred = pred; ops.push_back(x); }
    bool isBool(int r) const { return boolean.count(r) != 0; }
    char sideOf(int r) const { return r >= kVirt ? side[static_cast<std::size_t>(r - kVirt)] : sideOfR(r); }
    std::pair<int, long> base(int r) const {
        std::map<int, std::pair<int, long> >::const_iterator it = affine.find(r);
        return it == affine.end() ? std::make_pair(r, 0L) : it->second;
    }
};
typedef WinCode::Arg Arg;
typedef std::set<int, ByName> RegSet;

// The operands the emitters make: a register, a constant, `*base` or `*+base(off)`, and `*+base[index]`.
Arg V(int r) { Arg a; a.o = R(0); a.reg = r; return a; }
Arg N(long k) { Arg a; a.o = K(k); return a; }
Arg byteAt(int base, long off) {
    Arg a;
    a.o.kind = MOperand::Memory; a.o.reg = 0; a.reg = base;
    if (off == 0) return a;
    a.o.mode = off > 0 ? MOperand::Plus : MOperand::Minus;
    a.o.hasOffset = true; a.o.value = off < 0 ? -off : off;
    return a;
}
Arg indexed(int base, int index) {
    Arg a = byteAt(base, 0);
    a.o.mode = MOperand::Plus; a.o.hasOffset = true; a.o.scaled = true; a.o.index = 0; a.index = index;
    return a;
}
bool isConst(const Arg &a) { return a.o.kind == MOperand::Immediate; }

// An operand of the body with every register renamed by cur, one after the other as its spelling lists them.
Arg renamedBy(const MOperand &o, const std::map<int, int> &cur) {
    Arg a;
    a.o = o;
    if (o.kind != MOperand::Register && o.kind != MOperand::Memory) return a;
    a.reg = o.reg;
    if (o.kind == MOperand::Memory && o.index != kNoReg) a.index = o.index;
    const int regs[2] = { a.reg, a.index };
    for (int r = 0; r < 2; r++) {
        std::map<int, int>::const_iterator it = cur.find(regs[r]);
        if (regs[r] == kNoReg || it == cur.end() || it->second == regs[r]) continue;
        if (a.reg == regs[r]) a.reg = it->second;
        if (a.index == regs[r]) a.index = it->second;
    }
    return a;
}

MInst branchEntry(const std::string &to, int pred = kNoReg, bool no = false) { return entry("B", MOperand(to), MOperand(), MOperand(), pred, no); }

// The window's code with registers given: an instruction whose result nothing reads is dropped first, then each
// virtual register takes a physical one of its side in the order the code reads, freed after its last reader.
bool giveRegisters(WinCode &c, std::vector<int> pool[2], MStream &code, std::string &why) {
    auto each = [&](const Arg &a, const std::function<void(int)> &f) {
        if (a.reg != kNoReg && a.reg >= kVirt) f(a.reg - kVirt);
        if (a.index != kNoReg && a.index >= kVirt) f(a.index - kVirt);
    };
    for (bool changed = true; changed;) {
        changed = false;
        std::set<int> read;
        for (std::size_t i = 0; i < c.ops.size(); i++)
            for (std::size_t o = 0; o < c.ops[i].ops.size(); o++)
                if (o + 1 < c.ops[i].ops.size() || c.ops[i].ops[o].o.kind == MOperand::Memory) each(c.ops[i].ops[o], [&](int n) { read.insert(n); });
        for (std::size_t i = c.ops.size(); i-- > 0;) {
            const Arg &d = c.ops[i].ops.back();
            if (d.o.kind == MOperand::Register && d.reg >= kVirt && !read.count(d.reg - kVirt)) {
                c.ops.erase(c.ops.begin() + static_cast<long>(i));
                changed = true;
            }
        }
    }
    // Registers given in the order the code reads, each freed after its last reader, oldest freed first.
    std::vector<int> last(c.side.size(), -1);
    for (std::size_t i = 0; i < c.ops.size(); i++) for (std::size_t o = 0; o < c.ops[i].ops.size(); o++) each(c.ops[i].ops[o], [&](int n) { last[static_cast<std::size_t>(n)] = static_cast<int>(i); });
    std::vector<int> phys(c.side.size(), kNoReg);
    std::vector<bool> given(c.side.size(), false);
    std::deque<int> freeList[2];
    for (int s = 0; s < 2; s++) for (std::size_t k = 0; k < pool[s].size(); k++) freeList[s].push_back(pool[s][k]);
    std::vector<int> live;
    for (std::size_t i = 0; i < c.ops.size(); i++) {
        for (std::size_t a = 0; a < live.size();) {
            if (last[static_cast<std::size_t>(live[a])] < static_cast<int>(i)) { const int r = phys[static_cast<std::size_t>(live[a])]; freeList[sideOfReg(r)].push_back(r); live.erase(live.begin() + static_cast<long>(a)); }
            else a++;
        }
        bool ok = true;
        for (std::size_t o = 0; o < c.ops[i].ops.size(); o++) each(c.ops[i].ops[o], [&](int n) {
            if (given[static_cast<std::size_t>(n)]) return;
            const int s = c.side[static_cast<std::size_t>(n)] == 'B' ? 1 : 0;
            if (freeList[s].empty()) { ok = false; return; }
            phys[static_cast<std::size_t>(n)] = freeList[s].front(); freeList[s].pop_front();
            given[static_cast<std::size_t>(n)] = true; live.push_back(n);
        });
        if (!ok) { why = "no registers for the turns"; return false; }
    }
    auto given1 = [&](int r) { return static_cast<std::uint8_t>(r >= kVirt ? phys[static_cast<std::size_t>(r - kVirt)] : r); };
    for (std::size_t i = 0; i < c.ops.size(); i++) {
        const std::string &m = c.ops[i].mnem;
        if (c.ops[i].ops.size() > 4) { why = "an instruction of more operands than an entry holds"; return false; }
        MInst e(m.c_str());
        for (std::size_t o = 0; o < c.ops[i].ops.size(); o++) {
            MOperand x = c.ops[i].ops[o].o;
            if (c.ops[i].ops[o].reg != kNoReg) x.reg = given1(c.ops[i].ops[o].reg);
            if (c.ops[i].ops[o].index != kNoReg) x.index = given1(c.ops[i].ops[o].index);
            e.op[e.operands++] = x;
        }
        e.pred = static_cast<std::uint8_t>(c.ops[i].pred);
        // `legalForm` on the entry: commuted where both sources cross; a load stands as it is made.
        const bool commutes = m == "ADD" || m == "AND" || m == "OR" || m == "XOR" || m == "MPYDP" || m == "ADDDP" || m == "MPYSP" ||
                              m == "ADDSP" || m == "MPY32" || m == "CMPEQ";
        if (!isLoad(m) && crossings(e) > 1 && commutes && e.operands == 3 && e.op[0].kind != MOperand::Immediate && e.op[1].kind != MOperand::Immediate) std::swap(e.op[0], e.op[1]);
        if (!isLoad(m) && !(crossings(e) <= 1 && (unitsFor(e) != 0 || isStore(m)))) { why = "no legal form for" + lineOf(e).raw; return false; }
        code.push_back(e);
    }
    return true;
}

// ----- the closed form: where all a loop leaves behind is its registers plus a constant step a turn -----

// A value within one turn as a register's value at the turn's start plus a constant; base kNoReg for a constant alone.
struct Aff { int base = kNoReg; long c = 0; bool ok = false; };

// **One turn read as affine values**: each value the loop leaves - at each exit where it is live, and at the head - must
// be the start value of a register that steps by -1, 0 or 1 a turn, plus a constant; then the turn a window stops in
// says them all, and no turn writes anything. at[j] is exit j's state, step a register's per-turn step.
bool affineTurn(const Window &W, const RegSet &committed, std::vector<std::map<int, Aff> > &at, std::map<int, long> &step) {
    std::map<int, Aff> st, away;
    auto get = [&](int r) { if (away.count(r)) return away[r]; if (st.count(r)) return st[r]; Aff a; a.base = r; a.ok = true; return a; };
    auto value = [&](const MInst &l) {
        Aff a;
        const std::string m = l.m.str();
        const auto num = [&](int k) { return l.op[k].kind == MOperand::Immediate; };
        if (m == "MV" && l.operands == 2) return l.op[0].isReg() ? get(l.op[0].reg) : a;
        if (m == "MVK" && l.operands == 2 && num(0)) { a.ok = true; a.c = static_cast<long>(l.op[0].value); return a; }
        if ((m == "ADD" || m == "SUB") && l.operands == 3) {
            const int ki = num(0) && m == "ADD" ? 0 : num(1) ? 1 : -1;
            if (ki >= 0 && l.op[1 - ki].isReg()) {
                a = get(l.op[1 - ki].reg);
                a.c += static_cast<long>(l.op[ki].value) * (m == "SUB" ? -1 : 1);
                return a;
            }
        }
        return a;
    };
    at.assign(W.exits.size(), std::map<int, Aff>());
    bool finalTaken = false;
    for (std::size_t i = 0; i < W.bodyM.size(); i++) {
        const MInst &l = W.bodyM[i];
        if (isBranch(l.m.str())) {
            for (RegSet::const_iterator r = committed.begin(); r != committed.end(); ++r) at[static_cast<std::size_t>(W.cls[i])][*r] = get(*r);
            away.clear();
            continue;
        }
        if (W.cls[i] == kCont && !finalTaken && W.PR != kNoReg) {
            for (RegSet::const_iterator r = committed.begin(); r != committed.end(); ++r) at.back()[*r] = get(*r);
            finalTaken = true;
        }
        std::vector<int> reads, writes;
        readsAndWrites(l, reads, writes);
        if (writes.empty()) continue;
        const Aff a = isLoad(l.m.str()) ? Aff() : value(l);
        if (W.cls[i] >= 0) away[writes[0]] = a; else st[writes[0]] = a;
    }
    if (W.PR != kNoReg && !finalTaken)
        for (RegSet::const_iterator r = committed.begin(); r != committed.end(); ++r) at.back()[*r] = get(*r);
    // The steps: a register live at the head ends the turn as itself plus its step.
    for (RegSet::const_iterator r = committed.begin(); r != committed.end(); ++r) {
        if (!(W.liveHead & bitR(*r))) continue;
        const Aff e = get(*r);
        if (!e.ok || e.base != *r || e.c < -1 || e.c > 1) return false;
        step[*r] = e.c;
    }
    // Every value an exit needs, over a register of known step or none; a register the loop never writes steps by 0.
    for (std::size_t j = 0; j < W.exits.size(); j++)
        for (RegSet::const_iterator r = committed.begin(); r != committed.end(); ++r) {
            if (!(W.exits[j].live & bitR(*r))) continue;
            const Aff &a = at[j][*r];
            if (!a.ok) return false;
            if (a.base != kNoReg && !step.count(a.base)) { if (committed.count(a.base)) return false; step[a.base] = 0; }
        }
    return true;
}

bool emitClosed(Window &W, const RegSet &committed, std::vector<int> pool[2], MStream &out, std::string &why) {
    std::vector<std::map<int, Aff> > at;
    std::map<int, long> step;
    const std::size_t E = W.exits.size();
    if (E > 2) { why = "more than two ways out of a turn"; return false; }
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].offmin != W.ptrs[p].offmax) { why = "bytes of a turn at two places"; return false; }
    for (std::size_t k = 0; k < W.bodyM.size(); k++) {
        bool stepped = false;
        for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].loads.count(k)) stepped = true;
        if (isLoad(W.mnem(k)) && !stepped) { why = "a load that cannot be read ahead"; return false; }
    }
    if (!affineTurn(W, committed, at, step)) { why = "a value left behind that is not a step a turn"; return false; }
    for (std::size_t p = 0; p < W.ptrs.size(); p++)
        if (W.ptrs[p].cond) {
            int before = 0;
            for (std::size_t i = 0; i < W.ptrs[p].anchor; i++) if (isBranch(W.mnem(i))) before++;
            if (before != 1) { why = "a block read behind more than one exit"; return false; }
        }
    auto isFree = [&](int s, int k) { return std::find(pool[s].begin(), pool[s].end(), s * 32 + k) != pool[s].end(); };
    auto takePred = [&]() -> int {
        for (int s = 0; s < 2; s++) for (int k = 0; k < 3; k++) if (isFree(s, k)) { pool[s].erase(std::find(pool[s].begin(), pool[s].end(), s * 32 + k)); return s * 32 + k; }
        return kNoReg;
    };
    const int Pc = takePred(), Pj = E > 1 ? takePred() : kNoReg;
    std::vector<int> G(W.ptrs.size(), kNoReg);
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond) G[p] = takePred();
    bool ok = Pc != kNoReg && (E == 1 || Pj != kNoReg);
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond && G[p] == kNoReg) ok = false;
    if (!ok) { why = "no condition registers for the closed form"; return false; }
    WinCode c;
    std::map<int, int> cur;
    // Each pointer's block and offset in it; the turns run to the end of the nearest block, w = 8 - offset.
    std::vector<int> B(W.ptrs.size()), O(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        const char s = sideOfR(W.ptrs[p].reg);
        int u = W.ptrs[p].reg;
        if (W.ptrs[p].offmin != 0) { u = c.fresh(s); c.put("ADD", { N(W.ptrs[p].offmin), V(W.ptrs[p].reg), V(u) }); }
        O[p] = c.fresh(s); c.put("AND", { N(7), V(u), V(O[p]) });
        B[p] = c.fresh(s); c.put("AND", { N(-8), V(u), V(B[p]) });
    }
    std::map<std::string, int> loaded;
    auto loadWrapped = [&](std::size_t p) {
        const char s = sideOfR(W.ptrs[p].reg);
        std::set<std::string> mn;
        for (std::map<std::size_t, long>::const_iterator it = W.ptrs[p].loads.begin(); it != W.ptrs[p].loads.end(); ++it) mn.insert(W.mnem(it->first));
        // With two pointers, the odd turns' bytes through a copy of the block on the other side: both .D units read.
        const char o2 = W.ptrs.size() < 2 ? s : s == 'A' ? 'B' : 'A';
        int B2 = B[p], O2 = O[p];
        if (o2 != s) { B2 = c.fresh(o2); O2 = c.fresh(o2); c.put("MV", { V(B[p]), V(B2) }); c.put("MV", { V(O[p]), V(O2) }); }
        for (int k = 1; k < 8; k++)
            for (std::set<std::string>::const_iterator m = mn.begin(); m != mn.end(); ++m) {
                const bool odd = k % 2 != 0;
                const int t = c.fresh(odd ? o2 : s);
                const int x = c.fresh(odd ? o2 : s);
                c.put("ADD", { N(k), V(odd ? O2 : O[p]), V(t) });
                c.put("AND", { N(7), V(t), V(t) });
                c.put(*m, { indexed(odd ? B2 : B[p], t), V(x) }, G[p]);
                loaded[std::to_string(p) + "/" + std::to_string(k) + "/" + *m] = x;
            }
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (!W.ptrs[p].cond) loadWrapped(p);
    auto valueOf = [&](int r) { std::map<int, int>::const_iterator it = cur.find(r); return it == cur.end() ? r : it->second; };
    // Whether a turn stops at a test, as 0 or 1, from Q and the sense it leaves in: a compare's own result where it can be.
    auto stopOf = [&](int q, bool leavesWhenNonzero) {
        if (c.isBool(q) && leavesWhenNonzero) return q;
        const int x = c.fresh(c.sideOf(q));
        if (c.isBool(q)) c.put("XOR", { N(1), V(q), V(x) });
        else if (leavesWhenNonzero) c.put("CMPLTU", { N(0), V(q), V(x) });
        else c.put("CMPEQ", { N(0), V(q), V(x) });
        c.boolean.insert(x);
        return x;
    };
    std::vector<int> dead;                              // d_p: 1 where some test up to place p stopped
    int d = kNoReg;
    auto pass = [&](int j, int k) {
        const Window::Exit &x = W.exits[static_cast<std::size_t>(j)];
        const int s = stopOf(valueOf(x.pred), !x.no);
        if (d == kNoReg) d = s;
        else { const int n = c.fresh(c.sideOf(d)); c.put("OR", { V(d), V(s), V(n) }); d = n; }
        dead.push_back(d);
        if (k == 0 && j == 0)
            for (std::size_t p = 0; p < W.ptrs.size(); p++)
                if (W.ptrs[p].cond) { c.put("CMPEQ", { N(0), V(s), V(G[p]) }); loadWrapped(p); }
    };
    for (int k = 0; k < 8; k++) {
        for (std::size_t i = 0; i < W.bodyM.size(); i++) {
            const MInst &l = W.bodyM[i];
            const std::string m = l.m.str();
            if (isBranch(m)) { pass(W.cls[i], k); continue; }
            if (W.cls[i] >= 0) continue;            // done on the way out: its value is read from the closed form
            const int dst = l.op[l.operands - 1].reg;
            std::size_t ptr = W.ptrs.size();
            for (std::size_t p = 0; p < W.ptrs.size() && ptr == W.ptrs.size(); p++) if (W.ptrs[p].loads.count(i)) ptr = p;
            if (ptr < W.ptrs.size()) {
                int base = kNoReg; long off = 0; bool post = false;
                byteAddress(l.op[0], base, off, post);
                const std::string key = std::to_string(ptr) + "/" + std::to_string(k) + "/" + m;
                if (!loaded.count(key)) {
                    const int x = c.fresh(sideOfR(dst));
                    c.put(m, { byteAt(valueOf(base), post ? 0 : off), V(x) }, W.ptrs[ptr].cond ? G[ptr] : kNoReg);
                    loaded[key] = x;
                }
                cur[dst] = loaded[key];
                if (post) { const int nv = c.fresh(sideOfR(base)); c.put("ADD", { N(1), V(valueOf(base)), V(nv) }); cur[base] = nv; }
                continue;
            }
            if (m == "MV" && l.operands == 2 && l.op[0].isReg() && c.sideOf(valueOf(l.op[0].reg)) == sideOfR(dst)) { const int v = valueOf(l.op[0].reg); cur[dst] = v; continue; }
            std::vector<Arg> ops;
            for (int o = 0; o + 1 < l.operands; o++) ops.push_back(renamedBy(l.op[o], cur));
            const int x = c.fresh(sideOfR(dst));
            ops.push_back(V(x));
            c.put(m, ops);
            if (startsWith(m, "CMP")) c.boolean.insert(x);
            cur[dst] = x;
        }
        if (W.PR != kNoReg) pass(static_cast<int>(E) - 1, k);
    }
    // t, the places passed: their count less the sum of the d_p, no more than the window's w*E places.
    std::vector<int> sum = dead;
    while (sum.size() > 1) {
        std::vector<int> next;
        for (std::size_t i = 0; i + 1 < sum.size(); i += 2) { const int n = c.fresh(c.sideOf(sum[i])); c.put("ADD", { V(sum[i]), V(sum[i + 1]), V(n) }); next.push_back(n); }
        if (sum.size() % 2) next.push_back(sum.back());
        sum.swap(next);
    }
    const int passed = c.fresh('A');
    if (dead.size() <= 15) c.put("SUB", { N(static_cast<long>(dead.size())), V(sum[0]), V(passed) });
    else { const int n = c.fresh('A'); c.put("MVK", { N(static_cast<long>(dead.size())), V(n) }); c.put("SUB", { V(n), V(sum[0]), V(passed) }); }
    // The window's bound, the least of the pointers' (8 - offset) * E, and t no more than it: t = b + min(t - b, 0).
    int bound = kNoReg;
    auto least = [&](int x, int y) {
        const int df = c.fresh('A');
        const int m = c.fresh('A');
        const int n = c.fresh('A');
        c.put("SUB", { V(x), V(y), V(df) }); c.put("SHR", { V(df), N(31), V(m) }); c.put("AND", { V(df), V(m), V(m) }); c.put("ADD", { V(y), V(m), V(n) });
        return n;
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        int w = c.fresh('A');
        c.put("SUB", { N(8), V(O[p]), V(w) });
        if (E > 1) { const int w2 = c.fresh('A'); c.put("ADD", { V(w), V(w), V(w2) }); w = w2; }
        bound = bound == kNoReg ? w : least(w, bound);
    }
    const int t = least(passed, bound);
    c.put("CMPEQ", { V(t), V(bound), V(Pc) });
    int Kt = t;
    if (E > 1) { Kt = c.fresh('A'); c.put("SHR", { V(t), N(1), V(Kt) }); c.put("AND", { N(1), V(t), V(Pj) }); }
    // Each value left behind, made from K: the exit-0 one first, the exit-1 one under Pj, the head's under Pc.
    auto make = [&](const Aff &v, char s) {
        const int x = c.fresh(s);
        if (v.base == kNoReg) { c.put("MVK", { N(v.c), V(x) }); return x; }
        const long sb = step.count(v.base) ? step[v.base] : 0;
        int y = v.base;
        if (sb == 1) { const int z = c.fresh(s); c.put("ADD", { V(Kt), V(v.base), V(z) }); y = z; }
        else if (sb == -1) { const int z = c.fresh(s); c.put("SUB", { V(v.base), V(Kt), V(z) }); y = z; }
        if (v.c >= -16 && v.c <= 15) c.put("ADD", { N(v.c), V(y), V(x) });
        else { const int k2 = c.fresh(s); c.put("MVK", { N(v.c), V(k2) }); c.put("ADD", { V(y), V(k2), V(x) }); }
        return x;
    };
    struct Move { int reg, value, pred; };
    std::vector<Move> moves;
    auto move = [&](int reg, int value, int pred) { Move mv; mv.reg = reg; mv.value = value; mv.pred = pred; moves.push_back(mv); };
    for (RegSet::const_iterator r = committed.begin(); r != committed.end(); ++r) {
        const char s = sideOfR(*r);
        std::vector<int> xs(E, kNoReg);
        for (std::size_t j = 0; j < E; j++) if (W.exits[j].live & bitR(*r)) xs[j] = make(at[j][*r], s);
        int head = kNoReg;
        bool same = true;                               // the head's value is the exits' own: one move does
        for (std::size_t j = 0; j < E; j++) if (!(W.exits[j].live & bitR(*r)) || at[j][*r].base != *r || at[j][*r].c != 0) same = false;
        if ((W.liveHead & bitR(*r)) && !same) { Aff h; h.base = *r; h.ok = true; head = make(h, s); }
        if (xs[0] != kNoReg) move(*r, xs[0], kNoReg);
        if (E > 1 && xs[1] != kNoReg) move(*r, xs[1], xs[0] == kNoReg ? kNoReg : Pj);
        if (head != kNoReg) move(*r, head, Pc);
    }
    for (std::size_t m = 0; m < moves.size(); m++) c.put("MV", { V(moves[m].value), V(moves[m].reg) }, moves[m].pred);
    MStream code;
    if (!giveRegisters(c, pool, code, why)) return false;
    code.push_back(branchEntry(W.label, Pc));
    if (E == 1 || W.exits[0].target == W.exits[1].target) { if (!W.exits[0].target.empty()) code.push_back(branchEntry(W.exits[0].target)); }
    else {
        if (!W.exits[1].target.empty()) code.push_back(branchEntry(W.exits[1].target, Pj));
        else code.push_back(branchEntry(W.exits[0].target.empty() ? W.label : W.exits[0].target, Pj, true));
        if (!W.exits[1].target.empty() && !W.exits[0].target.empty()) code.push_back(branchEntry(W.exits[0].target));
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: window, closed form, %zu pointers, %zu instructions for eight turns\n", W.label.c_str(), W.ptrs.size(), code.size());
    out.swap(code);
    return true;
}

bool emitWindow(Window &W, MStream &out, std::string &why) {
    // What the window may not take: every register live at the head or an exit, and the fixed ones.
    const std::uint64_t fixed = W.liveHead | W.liveOut | kKeptM;
    RegSet written;
    for (std::size_t k = 0; k < W.bodyM.size(); k++) {
        std::vector<int> reads, writes;
        readsAndWrites(W.bodyM[k], reads, writes);
        for (std::size_t w = 0; w < writes.size(); w++) written.insert(writes[w]);
    }
    // The registers whose value the loop leaves behind: written in it and live at the head or an exit.
    RegSet committed;
    for (RegSet::const_iterator it = written.begin(); it != written.end(); ++it)
        if ((W.liveHead | W.liveOut) & bitR(*it)) committed.insert(*it);
    std::vector<int> pool[2];
    for (int s = 0; s < 2; s++)
        for (int k = 0; k < 32; k++) {
            if (k >= 10 && k <= 15) continue;
            const int r = s * 32 + k;
            if (!(fixed & bitR(r)) && !committed.count(r)) pool[s].push_back(r);
        }
    {
        std::vector<int> spare[2] = { pool[0], pool[1] };
        std::string cwhy;
        if (!std::getenv("CPP11_NOCLOSED") && emitClosed(W, committed, spare, out, cwhy)) return true;
        if (tracing() > 1) std::fprintf(stderr, "pipe %s: no closed form: %s\n", W.label.c_str(), cwhy.c_str());
    }
    // The exits by where they go; more than one place, and a selector says which was taken.
    std::vector<std::string> targets;
    for (std::size_t j = 0; j < W.exits.size(); j++) if (!has(targets, W.exits[j].target)) targets.push_back(W.exits[j].target);
    const bool multi = targets.size() > 1;
    bool needK = false, needZ = multi;
    for (std::size_t k = 0; k < W.bodyM.size(); k++) {
        if (W.cls[k] == kCont) needK = true;
        if (W.cls[k] >= 0 && !isBranch(W.mnem(k))) needZ = true;
    }
    // The predicates: R, K and Z on one side, beside the arithmetic that makes them; Cr and the anchors' G anywhere.
    auto isFree = [&](int s, int k) { return std::find(pool[s].begin(), pool[s].end(), s * 32 + k) != pool[s].end(); };
    auto take = [&](int s, int k) { pool[s].erase(std::find(pool[s].begin(), pool[s].end(), s * 32 + k)); return s * 32 + k; };
    auto takePred = [&](int s) -> int { for (int k = 0; k < 3; k++) if (isFree(s, k)) return take(s, k); return kNoReg; };
    auto preds = [&](int s) { int n = 0; for (int k = 0; k < 3; k++) if (isFree(s, k)) n++; return n; };
    const int cs = preds(0) >= preds(1) ? 0 : 1;
    const char ctl = cs ? 'B' : 'A';
    const int Rr = takePred(cs);
    const int Z = needZ ? takePred(cs) : kNoReg;
    std::vector<int> Ks;
    if (needK) { Ks.push_back(takePred(cs)); if (preds(cs) > 0 && preds(1 - cs) > 0) Ks.push_back(takePred(cs)); }
    int Cr = takePred(1 - cs); if (Cr == kNoReg) Cr = takePred(cs);
    std::vector<int> G(W.ptrs.size(), kNoReg);
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond) { G[p] = takePred(1 - cs); if (G[p] == kNoReg) G[p] = takePred(cs); }
    bool predsOk = Rr != kNoReg && (!needZ || Z != kNoReg) && (!needK || Ks[0] != kNoReg) && Cr != kNoReg;
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond && G[p] == kNoReg) predsOk = false;
    if (!predsOk) { why = "no condition registers for the turns"; return false; }
    int Sel = kNoReg;
    if (multi) { if (pool[cs].size() > 4) { Sel = pool[cs].back(); pool[cs].pop_back(); } else { why = "no register for the way out"; return false; } }
    WinCode c;
    std::map<int, int> cur;
    // Setup: each pointer's block and offset in it, and R = 0xFF >> (offset + span) for each, or-ed with one.
    std::vector<int> B(W.ptrs.size()), O(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        const Window::Ptr &P = W.ptrs[p];
        const char s = sideOfR(P.reg);
        int u = P.reg;
        if (P.offmin != 0) { u = c.fresh(s); c.put("ADD", { N(P.offmin), V(P.reg), V(u) }); }
        O[p] = c.fresh(s); c.put("AND", { N(7), V(u), V(O[p]) });
        B[p] = c.fresh(s); c.put("AND", { N(-8), V(u), V(B[p]) });
        int sh = O[p];
        if (P.offmax != P.offmin) { sh = c.fresh(s); c.put("ADD", { N(P.offmax - P.offmin), V(O[p]), V(sh) }); }
        const int ff = c.fresh(s);
        const int m = c.fresh(s);
        c.put("MVK", { N(255), V(ff) });
        c.put("SHRU", { V(ff), V(sh), V(m) });
        if (p == 0) c.put("MV", { V(m), V(Rr) }); else c.put("AND", { V(Rr), V(m), V(Rr) });
    }
    c.put("OR", { N(1), V(Rr), V(Rr) });            // the first turn is always the loop's own
    // A committed register is read from a copy, its own name only ever written by the turns.
    for (RegSet::const_iterator it = committed.begin(); it != committed.end(); ++it) {
        const int x = c.fresh(sideOfR(*it));
        c.put("MV", { V(*it), V(x) });
        cur[*it] = x;
    }
    // Which (n, mnem) a turn reaches, n the byte's place past the anchor's in the first turn.
    std::vector<std::vector<std::pair<long, std::string> > > reach(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++)
        for (std::map<std::size_t, long>::const_iterator it = W.ptrs[p].loads.begin(); it != W.ptrs[p].loads.end(); ++it)
            reach[p].push_back(std::make_pair(it->second - W.ptrs[p].offmin, W.mnem(it->first)));
    // The bytes of the block the later turns read, each where it wraps within the block, under the anchor's G.
    std::map<std::string, int> loaded;                  // "p/n/mnem" -> register
    auto loadWrapped = [&](std::size_t p) {
        const char s = sideOfR(W.ptrs[p].reg);
        for (int k = 1; k < 8; k++)
            for (std::size_t e = 0; e < reach[p].size(); e++) {
                const long n = reach[p][e].first + k;
                const std::string key = std::to_string(p) + "/" + std::to_string(n) + "/" + reach[p][e].second;
                bool atZero = false;
                for (std::size_t f = 0; f < reach[p].size(); f++) if (reach[p][f].first == n && reach[p][f].second == reach[p][e].second) atZero = true;
                if (atZero || loaded.count(key)) continue;
                const int t = c.fresh(s);
                const int x = c.fresh(s);
                c.put("ADD", { N(n), V(O[p]), V(t) });
                c.put("AND", { N(7), V(t), V(t) });
                c.put(reach[p][e].second, { indexed(B[p], t), V(x) }, G[p]);
                loaded[key] = x;
            }
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (!W.ptrs[p].cond) loadWrapped(p);
    auto commit = [&](int reg, int val, int pr) {
        if (committed.count(reg)) c.put("MV", { V(val), V(reg) }, pr);
    };
    auto valueOf = [&](int r) { std::map<int, int>::const_iterator it = cur.find(r); return it == cur.end() ? r : it->second; };
    // The test's mask: Y = -1 where the turn goes on and 0 where it leaves, from Q and the sense it goes on in.
    auto maskOf = [&](int q, bool goesOnWhenNonzero) {
        const int y = c.fresh(ctl);
        if (c.isBool(q)) { if (goesOnWhenNonzero) c.put("NEG", { V(q), V(y) }); else c.put("ADD", { N(-1), V(q), V(y) }); return y; }
        const int t = c.fresh(ctl);
        c.put("CMPEQ", { N(0), V(q), V(t) });           // 1 where Q is zero
        if (goesOnWhenNonzero) c.put("ADD", { N(-1), V(t), V(y) }); else c.put("NEG", { V(t), V(y) });
        return y;
    };
    for (int k = 0; k < 8; k++) {
        bool exited = false;
        std::map<int, int> yOf;                         // exit -> its mask this turn
        int zFor = -1;                                  // the exit Z is made for, this turn
        bool kMade = false;
        const int Kk = needK ? Ks[static_cast<std::size_t>(k) % Ks.size()] : kNoReg;
        auto mask = [&](int j) {
            if (!yOf.count(j)) {
                const Window::Exit &x = W.exits[static_cast<std::size_t>(j)];      // the predicate it leaves on
                const int y = maskOf(valueOf(x.pred), x.no);
                yOf[j] = y;
            }
            return yOf[j];
        };
        auto leaving = [&](int j) {                     // Z: alive here and leaving by exit j
            if (zFor == j) return;
            const int n = c.fresh(ctl);
            const int y = mask(j);
            c.put("NOT", { V(y), V(n) });
            c.put("AND", { V(Rr), V(n), V(Z) });
            zFor = j;
        };
        auto leaveBy = [&](int j) {                     // the exit itself: Cr, the selector, and R
            const int y = mask(j);
            if (multi) {
                leaving(j);
                const std::size_t id = static_cast<std::size_t>(std::find(targets.begin(), targets.end(), W.exits[static_cast<std::size_t>(j)].target) - targets.begin());
                c.put("MVK", { N(static_cast<long>(id)), V(Sel) }, Z);
            }
            c.put("MV", { V(y), V(Cr) }, Rr);
            return y;
        };
        for (std::size_t i = 0; i < W.bodyM.size(); i++) {
            const MInst &l = W.bodyM[i];
            const std::string m = l.m.str();
            const int pr = k == 0 && !exited ? kNoReg : Rr;
            if (isBranch(m)) {                  // an exit inside the turn: R loses the turns from here on where it leaves
                const int y = leaveBy(W.cls[i]);
                c.put("AND", { V(Rr), V(y), V(Rr) });
                exited = true;
                continue;
            }
            const int cl = W.cls[i];
            int cpr = pr;
            if (cl == kCont) {
                if (!kMade) { const int y = mask(static_cast<int>(W.exits.size()) - 1); c.put("AND", { V(Rr), V(y), V(Kk) }); kMade = true; }
                cpr = Kk;
            } else if (cl >= 0) { leaving(cl); cpr = Z; }
            const bool away = cl >= 0;          // done only on the way out: what stays on keeps the old value
            const int dst = l.op[l.operands - 1].reg;
            std::size_t ptr = W.ptrs.size();
            for (std::size_t p = 0; p < W.ptrs.size() && ptr == W.ptrs.size(); p++) if (W.ptrs[p].loads.count(i)) ptr = p;
            if (ptr < W.ptrs.size()) {
                const Window::Ptr &P = W.ptrs[ptr];
                int base = kNoReg; long off = 0; bool post = false;
                byteAddress(l.op[0], base, off, post);
                const long n = P.loads.at(i) - P.offmin + k;
                const std::string key = std::to_string(ptr) + "/" + std::to_string(n) + "/" + m;
                if (k == 0 && P.cond && i == P.anchor) { c.put("MV", { V(Rr), V(G[ptr]) }); loadWrapped(ptr); }
                if (!loaded.count(key)) {
                    if (k != 0) { why = "a byte no turn's block read was asked for"; return false; }
                    const int x = c.fresh(sideOfR(dst));
                    c.put(m, { byteAt(valueOf(base), post ? 0 : off), V(x) }, pr);
                    loaded[key] = x;
                }
                cur[dst] = loaded[key];
                c.affine.erase(dst);
                commit(dst, loaded[key], cpr);
                if (post) {
                    const std::pair<int, long> a = c.base(valueOf(base));
                    const int nv = c.fresh(sideOfR(base));
                    c.put("ADD", { N(a.second + 1), V(a.first), V(nv) });
                    c.affine[nv] = std::make_pair(a.first, a.second + 1);
                    cur[base] = nv;
                    commit(base, nv, cpr);
                }
                continue;
            }
            if (isLoad(m)) {                    // read where it stands, under the turn's mask
                const int x = c.fresh(sideOfR(dst));
                c.put(m, { renamedBy(l.op[0], cur), V(x) }, cpr);
                if (!away) cur[dst] = x;
                commit(dst, x, cpr);
                continue;
            }
            if (m == "MV" && l.operands == 2 && l.op[0].isReg()) {
                const int s = valueOf(l.op[0].reg);
                if (c.sideOf(s) == sideOfR(dst)) { if (!away) cur[dst] = s; commit(dst, s, cpr); continue; }
            }
            std::vector<Arg> ops;
            for (int o = 0; o + 1 < l.operands; o++) ops.push_back(renamedBy(l.op[o], cur));
            const int x = c.fresh(sideOfR(dst));
            // A constant added to a register that is itself another plus a constant: one add from that other.
            int ki = -1;
            if (m == "ADD" && ops.size() == 2) ki = isConst(ops[0]) ? 0 : isConst(ops[1]) ? 1 : -1;
            if (m == "SUB" && ops.size() == 2 && isConst(ops[1])) ki = 1;
            if (ki >= 0 && !isConst(ops[static_cast<std::size_t>(1 - ki)])) {
                const Arg &other = ops[static_cast<std::size_t>(1 - ki)];
                const long kc = static_cast<long>(ops[static_cast<std::size_t>(ki)].o.value) * (m == "SUB" ? -1 : 1);
                const std::pair<int, long> a = other.o.isReg() ? c.base(other.reg) : std::make_pair(static_cast<int>(kNoReg), 0L);
                if (other.o.isReg() && a.second + kc >= -16 && a.second + kc <= 15 && c.sideOf(a.first) == sideOfR(dst)) {
                    c.put("ADD", { N(a.second + kc), V(a.first), V(x) });
                    c.affine[x] = std::make_pair(a.first, a.second + kc);
                } else c.put(m, { ops[0], ops[1], V(x) });
            } else {
                ops.push_back(V(x));
                c.put(m, ops);
            }
            if (startsWith(m, "CMP")) c.boolean.insert(x);
            if (!away) { cur[dst] = x; }
            commit(dst, x, cpr);
        }
        // The turn's end: Y with bit k cleared, so that R keeps only the turns after this one in the block.
        if (W.PR != kNoReg) {
            const int y = leaveBy(static_cast<int>(W.exits.size()) - 1);
            const int x = c.fresh(ctl);
            c.put("CLR", { V(y), N(k), N(k), V(x) });
            c.put("AND", { V(Rr), V(x), V(Rr) });
        } else {
            c.put("MVK", { N(1), V(Cr) }, Rr);
            c.put("CLR", { V(Rr), N(k), N(k), V(Rr) });
        }
    }
    MStream code;
    if (!giveRegisters(c, pool, code, why)) return false;
    // Round again while the last turn went on; else to where the turn that stopped was going.
    code.push_back(branchEntry(W.label, Cr));
    if (!multi) { if (!targets[0].empty()) code.push_back(branchEntry(targets[0])); }
    else {
        bool falls = has(targets, "");
        std::vector<std::string> branches;
        for (std::size_t t = 0; t < targets.size(); t++) if (!targets[t].empty()) branches.push_back(targets[t]);
        for (std::size_t t = 0; t < branches.size(); t++) {
            const std::size_t id = static_cast<std::size_t>(std::find(targets.begin(), targets.end(), branches[t]) - targets.begin());
            if (!falls && t + 1 == branches.size()) { code.push_back(branchEntry(branches[t])); break; }
            code.push_back(entry("CMPEQ", K(static_cast<long long>(id)), R(Sel), R(Rr)));
            code.push_back(branchEntry(branches[t], Rr));
        }
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: window, %zu pointers, %zu instructions for eight turns\n", W.label.c_str(), W.ptrs.size(), code.size());
    out.swap(code);
    return true;
}

// One back branch tried: the code that replaces lines [from, to) of the text where the pipelining pays.
bool pipelineAt(const Src &src, std::size_t i, const std::set<std::string> &labels, const std::set<std::string> &named,
                std::size_t copied, Code &code, std::size_t &from, std::size_t &to) {
    if (!src.instr(i) || src.mnem(i) != "B" || isCallTo("B", src.ops(i) ? src.op(i, 0) : std::string(), labels)) return false;
    const bool plain = src.pred(i).empty();
    if (plain) {                                // an unconditional branch is a back branch only to a label above
        bool above = false;
        for (std::size_t k = 0; k < i && !above; k++) above = src.label(k) && src.name(k) == src.op(i, 0);
        if (!above) return false;
    }
    Loop L;
    std::string why;
    if (plain || !recognise(src, i, labels, named, L, why)) {
        if (plain) why = "an unconditional back branch";
        // Not counted: a loop that leaves on a byte it reads takes eight turns of one aligned block at a time.
        Window W;
        std::string wwhy;
        MStream turns;
        if (recogniseWindow(src, i, labels, named, W, wwhy) && W.head >= copied && emitWindow(W, turns, wwhy)) {
            for (std::size_t k = 0; k < turns.size(); k++) code.put(turns[k]);
            from = W.head + 1;
            to = W.back + 1;
            return true;
        }
        if (tracing()) std::fprintf(stderr, "pipe %s: %s [window: %s]\n", src.op(i, 0).c_str(), why.c_str(), wwhy.c_str());
        return false;
    }
    if (L.head < copied) return false;
    if (!pipeline(L, code, why)) { if (tracing()) std::fprintf(stderr, "pipe %s: %s\n", L.label.c_str(), why.c_str()); return false; }
    from = to = L.head;
    return true;
}

}   // namespace

// **The pipeliner on the stream** (M6c rounds 2 and 14): the same candidates read from the entries, their liveness the
// stream's; a kernel is written as entries (`Code`) - `par` its packets, `NOP n` its idle cycles. False: one not held.
bool pipelineLoops(MStream &s) {
    std::vector<std::uint64_t> in, liveOut;
    liveness(s, in, liveOut);
    const Src src(&s, &in);
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (tracing() > 5) std::fprintf(stderr, "| %s", s[i].str().c_str());
        if (src.label(i)) labels.insert(src.name(i));
        if (s[i].kind == MInst::Label || s[i].packed) continue;             // a line of no operands
        for (std::size_t o = 0; o < src.ops(i); o++) named.insert(src.op(i, o));
    }
    MStream out;
    std::size_t copied = 0, from = 0, to = 0;
    for (std::size_t i = 0; i < s.size(); i++) {
        Code code;
        code.from = &s;
        if (!pipelineAt(src, i, labels, named, copied, code, from, to)) continue;
        if (!code.ok) return false;
        out.insert(out.end(), s.begin() + static_cast<long>(copied), s.begin() + static_cast<long>(from));
        out.insert(out.end(), code.m.begin(), code.m.end());
        copied = to;
    }
    out.insert(out.end(), s.begin() + static_cast<long>(copied), s.end());
    s.swap(out);
    return true;
}

}   // namespace c6x
