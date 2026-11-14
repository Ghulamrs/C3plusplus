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

// ----- addresses as linear forms: term -> coefficient, "" the constant, "iv" the induction variable -----

typedef std::map<std::string, long> Form;

Form formOf(const std::string &term, long k = 1) { Form f; f[term] = k; return f; }
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
    if (f.size() == 1 && f.begin()->first == "") { k = f.begin()->second; return true; }
    return false;
}
// The one object a form is based on - a global symbol or the frame, with coefficient one - or "" if none or several.
std::string objectOf(const Form &f) {
    std::string o;
    for (Form::const_iterator it = f.begin(); it != f.end(); ++it) {
        if (it->first.empty() || it->first == "iv" || it->first.compare(0, 4, "reg:") == 0) continue;
        if (!o.empty() || it->second != 1) return "";
        o = it->first;
    }
    return o;
}

struct Access { bool known = false; Form addr; };

// **What the address walk reads of an instruction** (M6c round 11): its parts, its registers and its access - the base,
// the offset or index and the step - from a line's text, or on the stream from the entry's operands.
struct Ins {
    std::string mnem, name[2];              // the first two operands as spelled: a register's name, a symbol
    bool pred = false, cst[2] = { false, false }, access = false;
    std::size_t ops = 0;
    long num[2] = { 0, 0 };
    std::vector<std::string> reads, writes;
    std::string stepReg, base, index;       // `steppedRegister`; the access's base, and its index where it has one
    long by = 0, off = 0, sign = 1;
};

// The access `m` as its spelling says: stepped, `*+R(n)`, `*+R[x]` or `*R`.
void accessOf(const std::string &m, Ins &x) {
    if (!x.stepReg.empty()) { x.base = x.stepReg; if (m.compare(0, 3, "*++") == 0 || m.compare(0, 3, "*--") == 0) x.off = x.by; }
    else if (m.size() > 1 && m[0] == '*' && (m[1] == '+' || m[1] == '-') && m.find('(') != std::string::npos) {
        x.sign = m[1] == '-' ? -1 : 1;
        x.base = m.substr(2, m.find('(') - 2);
        x.off = std::atol(m.substr(m.find('(') + 1).c_str());
    } else if (m.size() > 1 && m[0] == '*' && (m[1] == '+' || m[1] == '-') && m.find('[') != std::string::npos) {
        x.sign = m[1] == '-' ? -1 : 1;
        x.base = m.substr(2, m.find('[') - 2);
        x.index = m.substr(m.find('[') + 1, m.find(']') - m.find('[') - 1);
    } else if (m.size() > 1 && m[0] == '*' && sideOf(m.substr(1))) x.base = m.substr(1);
}

Ins insOf(const Line &l) {
    Ins x;
    x.mnem = l.mnem; x.pred = !l.pred.empty(); x.ops = l.ops.size();
    for (std::size_t k = 0; k < 2 && k < l.ops.size(); k++) { x.name[k] = l.ops[k]; x.cst[k] = isNumber(l.ops[k]); x.num[k] = std::atol(l.ops[k].c_str()); }
    readsAndWrites(l, x.reads, x.writes);
    x.stepReg = steppedRegister(l, x.by);
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
        if (!x.cst[k]) x.name[k] = e.op[k].str();
    }
    readsAndWrites(e, x.reads, x.writes);
    // The first stepped access names the step: by its `(n)` bytes, one where none is written, none where it has an index.
    for (int k = 0; k < e.operands; k++) {
        const MOperand &o = e.op[k];
        if (o.kind != MOperand::Memory || o.mode < MOperand::PreInc) continue;
        if (o.index != kNoReg) break;
        x.stepReg = regName(o.reg);
        x.by = o.hasOffset && !o.scaled ? static_cast<long>(o.value) : 1;
        if (o.mode == MOperand::PreDec || o.mode == MOperand::PostDec) x.by = -x.by;
        break;
    }
    x.access = isLoad(x.mnem) || isStore(x.mnem);
    if (!x.access) return x;
    const MOperand &o = e.op[isStore(x.mnem) ? 1 : 0];
    if (o.kind != MOperand::Memory) { accessOf(o.str(), x); return x; }
    const bool signedBase = o.mode == MOperand::Plus || o.mode == MOperand::Minus;
    if (!x.stepReg.empty()) { x.base = x.stepReg; if (o.mode == MOperand::PreInc || o.mode == MOperand::PreDec) x.off = x.by; }
    else if (signedBase && o.hasOffset) {
        x.sign = o.mode == MOperand::Minus ? -1 : 1;
        x.base = regName(o.reg);
        if (!o.scaled) x.off = o.index != kNoReg ? 0 : static_cast<long>(o.value);
        else x.index = o.index != kNoReg ? regName(o.index) : std::to_string(o.value);
    } else if (o.mode == MOperand::Base && !o.hasOffset) x.base = regName(o.reg);
    return x;
}

// What one instruction makes of its destination - a form, or nothing known - entered in val, the rest of its writes lost.
void evalLine(const Ins &l, std::map<std::string, Form> &val, std::set<std::string> &lost) {
    const std::vector<std::string> &writes = l.writes;
    if (writes.empty()) return;
    const std::string &d = writes.back();
    Form nv; bool knownV = false; long k = 0, k2 = 0;
    const std::string *p = l.name;
    bool cst0 = l.ops > 1 && l.cst[0], cst1 = l.ops > 2 && l.cst[1];
    Form v0 = l.ops > 1 && !l.cst[0] && val.count(p[0]) ? val[p[0]] : Form(), v1 = l.ops > 2 && !l.cst[1] && val.count(p[1]) ? val[p[1]] : Form();
    bool k0 = cst0 || (l.ops > 1 && !v0.empty()), k1 = cst1 || (l.ops > 2 && !v1.empty());
    if (cst0) v0 = formOf("", l.num[0]);
    if (cst1) v1 = formOf("", l.num[1]);
    if (!l.pred && writes.size() == 1) {
        if (l.mnem == "MVK" && cst0) { nv = v0; knownV = true; }
        else if (l.mnem == "MVKL" && l.ops == 2) { nv = cst0 ? v0 : formOf("@" + p[0]); knownV = true; }
        else if (l.mnem == "MVKH" && l.ops == 2 && val.count(d) && val[d] == (cst0 ? v0 : formOf("@" + p[0]))) { nv = val[d]; knownV = true; }
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
std::vector<Access> addressForms(const std::vector<Ins> &body, const std::vector<Ins> &pre, const std::string &iv, const std::set<std::string> &written) {
    std::map<std::string, Form> val, before;
    std::set<std::string> lost;
    for (std::size_t o = 0; o < pre.size(); o++) {           // what the preheader computed, by the same rules
        const std::vector<std::string> &reads = pre[o].reads;
        for (std::size_t r = 0; r < reads.size(); r++) if (!before.count(reads[r]) && !lost.count(reads[r])) before[reads[r]] = formOf(reads[r] == "A15" ? "A15" : "reg:" + reads[r]);
        evalLine(pre[o], before, lost);
    }
    lost.clear();
    // A pointer the accesses alone step: on entry to a turn it is the preheader's value plus its stride per turn gone.
    std::map<std::string, long> stride, stepWrites, allWrites;
    for (std::size_t o = 0; o < body.size(); o++) {
        for (std::size_t w = 0; w < body[o].writes.size(); w++) allWrites[body[o].writes[w]]++;
        const std::string &r = body[o].stepReg;
        if (!r.empty()) { stride[r] += body[o].by; stepWrites[r]++; }
    }
    const Form turns = combine(formOf("iv"), formOf("reg:" + iv), -1);
    std::vector<Access> out(body.size());
    for (std::size_t o = 0; o < body.size(); o++) {
        const Ins &l = body[o];
        const std::vector<std::string> &reads = l.reads;
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (val.count(reads[r]) || lost.count(reads[r])) continue;
            const std::string &x = reads[r];
            if (x == iv) val[x] = formOf("iv");
            else if (stepWrites.count(x) && stepWrites[x] == allWrites[x] && before.count(x) && x != iv) val[x] = combine(before[x], scaled(turns, stride[x]), 1);
            else val[x] = written.count(x) ? Form() : before.count(x) ? before[x] : formOf(x == "A15" ? "A15" : "reg:" + x);
        }
        const std::string &stepReg = l.stepReg;
        Form stepFrom; bool stepKnown = !stepReg.empty() && val.count(stepReg) && !val[stepReg].empty();
        if (stepKnown) stepFrom = val[stepReg];
        if (l.access) {
            bool known = !l.base.empty() && val.count(l.base) && !val[l.base].empty();
            if (known && !l.index.empty()) known = val.count(l.index) && !val[l.index].empty();
            if (known) {
                Form a = val[l.base];
                if (!l.index.empty()) a = combine(a, scaled(val[l.index], accessSize(l.mnem)), l.sign);
                else if (l.off) a = combine(a, formOf("", l.off), l.sign);
                out[o].known = true; out[o].addr = a;
            }
        }
        evalLine(l, val, lost);
        if (stepKnown) { val[stepReg] = combine(stepFrom, formOf("", l.by), 1); lost.erase(stepReg); }
    }
    return out;
}

// Whether access a, in iteration j, and b, in iteration j + d, may touch the same bytes.
bool mayAliasAt(const Access &a, int sa, const Access &b, int sb, int d) {
    if (!a.known || !b.known) return true;
    Form diff = combine(b.addr, a.addr, -1);
    Form::const_iterator iv = b.addr.find("iv");
    if (iv != b.addr.end()) diff = combine(diff, formOf("", d * iv->second), 1);
    long c;
    if (isConstForm(diff, c)) return c > -sb && c < sa;
    std::string oa = objectOf(a.addr), ob = objectOf(b.addr);
    return oa.empty() || ob.empty() || oa == ob;
}

// **The recognisers' reader** (M6c round 4): what an entry is and its parts - kind, mnemonic, predicate, operands,
// reads and writes, units - from the stream's typed operands where it has them (`modelled`), else from the line.
struct Src {
    const std::vector<Line> &v;
    const MStream *s;
    bool typed(std::size_t i) const { return s && modelled((*s)[i]); }
    bool instr(std::size_t i) const { return s ? (*s)[i].kind == MInst::Instruction && !(*s)[i].packed : v[i].instr; }
    bool label(std::size_t i) const {
        if (s && (*s)[i].kind == MInst::Label) return true;
        return (!s || (*s)[i].kind != MInst::Instruction || (*s)[i].packed) && isLabel(v[i]);
    }
    std::string name(std::size_t i) const { return s && (*s)[i].kind == MInst::Label ? (*s)[i].m.str() : labelName(v[i]); }
    std::string mnem(std::size_t i) const { return s && instr(i) ? (*s)[i].m.str() : v[i].mnem; }
    std::string pred(std::size_t i) const {
        if (!typed(i)) return v[i].pred;
        const MInst &e = (*s)[i];
        return e.pred == kNoReg ? std::string() : (e.predNot ? "!" : "") + regName(e.pred);
    }
    std::size_t ops(std::size_t i) const { return typed(i) ? (*s)[i].operands : v[i].ops.size(); }
    std::string op(std::size_t i, std::size_t k) const { return typed(i) ? (*s)[i].op[k].str() : v[i].ops[k]; }
    bool reg(std::size_t i, std::size_t k) const { return typed(i) ? (*s)[i].op[k].isReg() : sideOf(v[i].ops[k]) != 0; }
    bool number(std::size_t i, std::size_t k, long &n) const {
        if (typed(i)) { n = static_cast<long>((*s)[i].op[k].value); return (*s)[i].op[k].kind == MOperand::Immediate; }
        n = std::atol(v[i].ops[k].c_str());
        return isNumber(v[i].ops[k]);
    }
    bool pair(std::size_t i, std::size_t k) const { return typed(i) ? (*s)[i].op[k].kind == MOperand::Pair : v[i].ops[k].find(':') != std::string::npos; }
    void rw(std::size_t i, std::vector<std::string> &reads, std::vector<std::string> &writes) const {
        if (typed(i)) readsAndWrites((*s)[i], reads, writes); else readsAndWrites(v[i], reads, writes);
    }
    unsigned units(std::size_t i) const { return typed(i) ? unitsFor((*s)[i]) : unitsFor(v[i]); }
    char side(std::size_t i, std::size_t k) const {
        if (!typed(i)) return sideOf(v[i].ops[k]);
        const MOperand &o = (*s)[i].op[k];
        if (o.kind != MOperand::Register && o.kind != MOperand::Pair) return 0;
        return sideOfReg(o.kind == MOperand::Pair ? o.hi : o.reg) ? 'B' : 'A';
    }
    void regsIn(std::size_t i, std::size_t k, std::vector<std::string> &out) const;
    bool byteAddress(std::size_t i, std::size_t k, std::string &base, long &off, bool &post) const;
    // The operands that name `x`: the label a branch reaches, counted over the function's instructions.
    int naming(const std::string &x) const {
        int n = 0;
        for (std::size_t i = 0; i < v.size(); i++) if (instr(i)) for (std::size_t o = 0; o < ops(i); o++) if (op(i, o) == x) n++;
        return n;
    }
};

// ----- the loop as recognised -----

struct Edge { int from, to, delay, dist; const char *why; };

struct Loop {
    std::size_t head = 0, back = 0;
    std::string label, exitName, P, C, iv, bound;
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
    bool onStream = false, fromLines = false;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::set<std::string> used, written, predicated, rmwFirst;
    std::uint64_t liveExit = 0;
    // A counter stepped by more than one: the test stays in the body and the kernel's branch reads it against a bound
    // moved back by the turns in flight, so no turn the loop would not have run is begun. Low word first throughout.
    bool stepped = false, wide = false;
    std::vector<std::string> ivs, bounds, steps;
    std::vector<std::size_t> testBody;          // the body's compares that read a bound
    void rw(std::size_t o, std::vector<std::string> &reads, std::vector<std::string> &writes) const {
        if (!bodyM.empty()) readsAndWrites(bodyM[o], reads, writes); else readsAndWrites(body[o], reads, writes);
    }
    // Operand k of body line o as a pair `hi:lo` of two registers.
    bool pairIn(std::size_t o, std::size_t k, std::string &hi, std::string &lo) const {
        if (!bodyM.empty()) {
            const MOperand &x = bodyM[o].op[k];
            if (x.kind != MOperand::Pair) return false;
            hi = regName(x.hi); lo = regName(x.reg);
            return true;
        }
        std::vector<std::string> regs;
        registersIn(body[o].ops[k], regs);
        if (body[o].ops[k].find(':') == std::string::npos || regs.size() != 2) return false;
        hi = regs[0]; lo = regs[1];
        return true;
    }
    // `steppedRegister` of body line o: the base its first stepped access writes, empty where that access has an index.
    std::string steppedAt(std::size_t o) const {
        long by;
        if (bodyM.empty()) return steppedRegister(body[o], by);
        for (int k = 0; k < bodyM[o].operands; k++) {
            const MOperand &x = bodyM[o].op[k];
            if (x.kind == MOperand::Memory && x.mode >= MOperand::PreInc) return x.index == kNoReg ? regName(x.reg) : std::string();
        }
        return std::string();
    }
    // The address forms of the body's accesses: read from the entries on the stream, from the lines off it.
    std::vector<Access> addresses() const {
        std::vector<Ins> b, p;
        const bool typed = !bodyM.empty() && preTyped;
        for (std::size_t o = 0; o < body.size(); o++) b.push_back(typed ? insOf(bodyM[o]) : insOf(body[o]));
        for (std::size_t o = 0; o < pre.size(); o++) p.push_back(typed ? insOf(preM[o]) : insOf(pre[o]));
        const std::vector<Access> got = addressForms(b, p, iv, written);
        // CXX1_C6X_MIR=1: the lines walked too, and `c6x-addr` names an access whose form differs.
        if (typed && std::getenv("CXX1_C6X_MIR") != nullptr) {
            for (std::size_t o = 0; o < body.size(); o++) b[o] = insOf(body[o]);
            for (std::size_t o = 0; o < pre.size(); o++) p[o] = insOf(pre[o]);
            const std::vector<Access> ref = addressForms(b, p, iv, written);
            for (std::size_t o = 0; o < ref.size(); o++)
                if (ref[o].known != got[o].known || ref[o].addr != got[o].addr) std::fprintf(stderr, "c6x-addr:%s\n", body[o].raw.c_str());
        }
        return got;
    }
    void erase(std::size_t o) {
        body.erase(body.begin() + static_cast<long>(o));
        if (!bodyM.empty()) bodyM.erase(bodyM.begin() + static_cast<long>(o));
    }
    // `renameWebs`'s edit on entry o: `from` named `to` in its sources (a write: its destination, and MVKH's and ADDK's
    // always), as `renamed` spells it - a register, a pair's half, an address's base or index - and a read in its predicate.
    void rename(std::size_t o, const std::string &from, const std::string &to, bool isWrite) {
        if (bodyM.empty()) return;
        MInst &e = bodyM[o];
        const int f = regNumber(from), t = regNumber(to);
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
        body.push_back(src.v[i]);
        if (fromLines) return;
        if (src.typed(i)) bodyM.push_back((*src.s)[i]); else { fromLines = true; bodyM.clear(); }
    }
    // A counter's word, a bound or a step the kernel and the remainder both read under its own name.
    bool pinned(const std::string &r) const {
        return std::find(ivs.begin(), ivs.end(), r) != ivs.end() || std::find(bounds.begin(), bounds.end(), r) != bounds.end() ||
               std::find(steps.begin(), steps.end(), r) != steps.end();
    }
};

const char *const kCond[] = { "A0", "A1", "A2", "B0", "B1", "B2" };

// ----- the body read as algebra: what each register holds at its end, as a term over the values it came in with -----

// A term is a string: `s:R` the value R held at entry, `k:N` a constant, `sum{a,b,..}` a sorted flattened sum, `lt(a,b)`,
// `ltu(a,b)`, `eq{a,b}`, `and{a,b}`, `or{a,b}` the compares and logic (the braces sorted), and `op:N` anything else.
std::string joinSorted(std::vector<std::string> ts) {
    std::sort(ts.begin(), ts.end());
    std::string s;
    for (std::size_t i = 0; i < ts.size(); i++) s += (i ? "," : "") + ts[i];
    return s;
}
// The terms between the brackets of one, split at the commas of its own level.
std::vector<std::string> splitTop(const std::string &inner) {
    std::vector<std::string> parts;
    int depth = 0; std::size_t from = 0;
    for (std::size_t c = 0; c <= inner.size(); c++) {
        if (c == inner.size() || (inner[c] == ',' && depth == 0)) { parts.push_back(inner.substr(from, c - from)); from = c + 1; }
        else if (inner[c] == '(' || inner[c] == '{') depth++;
        else if (inner[c] == ')' || inner[c] == '}') depth--;
    }
    return parts;
}
std::string sumOf(const std::string &a, const std::string &b) {
    std::vector<std::string> ts;
    const std::string *parts[2] = { &a, &b };
    for (int i = 0; i < 2; i++) {
        const std::string &p = *parts[i];
        if (p.compare(0, 4, "sum{") == 0) { std::vector<std::string> in = splitTop(p.substr(4, p.size() - 5)); ts.insert(ts.end(), in.begin(), in.end()); }
        else ts.push_back(p);
    }
    return "sum{" + joinSorted(ts) + "}";
}

// The terms of every register after the body, and the term P is computed from, by one pass in program order.
std::map<std::string, std::string> evaluateBody(const Src &src, const Loop &L, std::vector<std::string> &lineTerms) {
    std::map<std::string, std::string> env;
    auto name = [&](const std::string &r) -> std::string {
        if (!env.count(r)) env[r] = "s:" + r;
        return env[r];
    };
    std::size_t i = 0;
    auto term = [&](std::size_t k) -> std::string { long n; return src.number(i, k, n) ? "k:" + src.op(i, k) : name(src.op(i, k)); };
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        i = L.bodyAt[k];
        const std::string m = src.mnem(i);
        std::vector<std::string> reads, writes;
        src.rw(i, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) name(reads[r]);
        std::string t;
        const bool unpred = src.pred(i).empty(), plain = unpred && src.ops(i) == 3 && writes.size() == 1 && src.op(i, 2) == writes[0];
        if (m == "MV" && unpred && src.ops(i) == 2 && writes.size() == 1) t = term(0);
        else if (plain && m == "ADD") t = sumOf(term(0), term(1));
        else if (plain && m == "CMPLT") t = "lt(" + term(0) + "," + term(1) + ")";
        else if (plain && m == "CMPGT") t = "lt(" + term(1) + "," + term(0) + ")";
        else if (plain && m == "CMPLTU") t = "ltu(" + term(0) + "," + term(1) + ")";
        else if (plain && m == "CMPGTU") t = "ltu(" + term(1) + "," + term(0) + ")";
        else if (plain && (m == "CMPEQ" || m == "AND" || m == "OR")) {
            std::vector<std::string> ts; ts.push_back(term(0)); ts.push_back(term(1));
            t = (m == "CMPEQ" ? "eq{" : m == "AND" ? "and{" : "or{") + joinSorted(ts) + "}";
        }
        for (std::size_t w = 0; w < writes.size(); w++) env[writes[w]] = t.empty() || writes.size() > 1 ? "op:" + std::to_string(k) + ":" + writes[w] : t;
        lineTerms.push_back(t.empty() || writes.size() != 1 ? "" : env[writes[0]]);
    }
    return env;
}

// **A counter stepped by more than one, or sixty-four bits wide**: the body, read as algebra, must leave registers lo (and
// hi) as `lo + s` (and `hi + shi + carry`) for s the loop never writes, and compute P as `lo' < n` or `n < lo'` - the two-word
// signed form `hi'<nhi | (hi'==nhi & lo' <u nlo)` for a wide one - so the loop runs while iv < n or iv <= n by its own test.
bool recogniseStepped(const Src &src, Loop &L, std::string &why, const std::string &fallback) {
    why = fallback;
    auto no = [&](const char *r) { if (tracing() > 1) why += std::string(" [stepped: ") + r + "]"; return false; };
    L.liveExit = src.v[L.back + 1].liveIn;
    if (L.liveExit & bitOf(L.P)) return no("the predicate lives past the loop");
    std::map<std::string, int> writers, readers;
    std::map<std::string, std::size_t> writerAt;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        std::vector<std::string> reads, writes;
        src.rw(L.bodyAt[k], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) readers[reads[r]]++;
        for (std::size_t w = 0; w < writes.size(); w++) { writers[writes[w]]++; writerAt[writes[w]] = k; }
    }
    if (writers[L.P] != 1) return no("the predicate is written more than once");
    std::vector<std::string> lineTerms;
    std::map<std::string, std::string> env = evaluateBody(src, L, lineTerms);
    // The steps: a register whose end term is itself plus one invariant (lo), and one that adds a carry of lo's sum (hi).
    auto invariant = [&](const std::string &t, std::string &reg) -> bool {
        if (t.compare(0, 2, "k:") == 0) { reg = t.substr(2); return true; }
        if (t.compare(0, 2, "s:") != 0) return false;
        reg = t.substr(2);
        return sideOf(reg) && !writers.count(reg);
    };
    std::string lo, slo, hi, shi;
    for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end() && lo.empty(); ++it) {
        const std::string &r = it->first, &t = it->second;
        if (!sideOf(r) || !writers.count(r) || t.compare(0, 4, "sum{") != 0) continue;
        const std::string a = "s:" + r;
        if (t == sumOf(a, "k:0") || env[L.P].find(t) == std::string::npos) continue;   // the counter the test reads
        std::vector<std::string> ts = splitTop(t.substr(4, t.size() - 5));
        if (ts.size() != 2) continue;
        std::string x = ts[0], y = ts[1], s;
        const std::string other = x == a ? y : y == a ? x : "";
        if (other.empty() || !invariant(other, s) || (s != r && s == "0")) continue;
        lo = r; slo = s;
    }
    if (lo.empty()) return no("no counter stepped by an invariant");
    const std::string loTerm = env[lo], carry = "ltu(" + loTerm + "," + (isNumber(slo) ? "k:" : "s:") + slo + ")";
    for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end() && hi.empty(); ++it) {
        const std::string &r = it->first, &t = it->second;
        if (!sideOf(r) || r == lo || !writers.count(r) || t.compare(0, 4, "sum{") != 0 || t.find(carry) == std::string::npos) continue;
        std::string rest = sumOf("s:" + r, carry), s;
        // t is sum{s:r, carry, step}: the step is what remains when those two are taken out.
        std::string cand;
        std::vector<std::string> parts = splitTop(t.substr(4, t.size() - 5));
        if (parts.size() != 3) continue;
        for (std::size_t p = 0; p < parts.size(); p++) if (parts[p] != "s:" + r && parts[p] != carry) cand = parts[p];
        if (cand.empty() || !invariant(cand, s) || sumOf(rest, cand) != t) continue;
        hi = r; shi = s;
    }
    L.wide = !hi.empty();
    if (L.wide && (isNumber(slo) != isNumber(shi))) return no("the two steps are not both registers or both constants");
    // The predicate: the stepped value against a bound the loop never writes, one or two words, signed.
    const std::string p = env[L.P], hiTerm = L.wide ? env[hi] : "";
    if (tracing() > 2) std::fprintf(stderr, "  stepped: lo %s + %s, hi %s + %s, %s = %s\n", lo.c_str(), slo.c_str(), hi.c_str(), shi.c_str(), L.P.c_str(), p.c_str());
    std::string nlo, nhi;
    bool less = false;                    // the loop runs while iv < n; else while iv <= n
    auto boundOf = [&](const std::string &t, std::string &reg) -> bool { return t.compare(0, 2, "s:") == 0 && invariant(t, reg) && reg != lo && reg != hi; };
    if (!L.wide) {
        if (p.compare(0, 3, "lt(") != 0) return no("the predicate is not a compare");
        std::vector<std::string> xy = splitTop(p.substr(3, p.size() - 4));
        if (xy.size() != 2) return no("the predicate is not a compare");
        const std::string x = xy[0], y = xy[1];
        if (x == loTerm && boundOf(y, nlo)) less = true;
        else if (y == loTerm && boundOf(x, nlo)) less = false;
        else return no("the compare is not of the stepped counter against an invariant");
    } else {
        // or{and{eq{hi',nhi},ltu(lo',nlo)},lt(hi',nhi)} for iv < n; the operands the other way round for n < iv.
        for (int dir = 0; dir < 2 && nlo.empty(); dir++) {
            for (std::map<std::string, std::string>::const_iterator a = env.begin(); a != env.end() && nlo.empty(); ++a) {
                std::string nl, nh;
                if (!boundOf("s:" + a->first, nl) || a->first == lo || a->first == hi) continue;
                for (std::map<std::string, std::string>::const_iterator b = env.begin(); b != env.end() && nlo.empty(); ++b) {
                    if (!boundOf("s:" + b->first, nh) || b->first == a->first) continue;
                    const std::string NL = "s:" + nl, NH = "s:" + nh;
                    std::vector<std::string> e; e.push_back(hiTerm); e.push_back(NH);
                    std::string ltu = dir == 0 ? "ltu(" + loTerm + "," + NL + ")" : "ltu(" + NL + "," + loTerm + ")";
                    std::string lt = dir == 0 ? "lt(" + hiTerm + "," + NH + ")" : "lt(" + NH + "," + hiTerm + ")";
                    std::vector<std::string> an; an.push_back("eq{" + joinSorted(e) + "}"); an.push_back(ltu);
                    std::vector<std::string> o; o.push_back("and{" + joinSorted(an) + "}"); o.push_back(lt);
                    if (p == "or{" + joinSorted(o) + "}") { nlo = nl; nhi = nh; less = dir == 0; }
                }
            }
        }
        if (nlo.empty()) return no("the two-word compare is not of the stepped counter against an invariant");
    }
    // `[P] B` with P = iv < n runs while iv < n; `[!P] B` with P = n < iv runs while iv <= n; the other two count down.
    if (less == L.negated) return no("the branch sense runs the loop while the test fails");
    L.plusOne = !less;
    // The lines that feed P, by the last writer before each read; the compares reading a bound must be among them,
    // must compare the stepped counter's term, and must be read by nothing else - they are the lines the kernel's
    // moved bound is substituted into.
    auto lastWriterBefore = [&](const std::string &x, std::size_t k) -> long {
        for (std::size_t j = k; j-- > 0;) {
            std::vector<std::string> reads, writes;
            src.rw(L.bodyAt[j], reads, writes);
            if (has(writes, x)) return static_cast<long>(j);
        }
        return -1;
    };
    std::set<std::size_t> feeding;
    std::vector<std::size_t> work(1, writerAt[L.P]);
    while (!work.empty()) {
        std::size_t k = work.back(); work.pop_back();
        if (!feeding.insert(k).second) continue;
        std::vector<std::string> reads, writes;
        src.rw(L.bodyAt[k], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) { long w = lastWriterBefore(reads[r], k); if (w >= 0) work.push_back(static_cast<std::size_t>(w)); }
    }
    std::vector<std::size_t> tree;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        std::vector<std::string> reads, writes;
        src.rw(i, reads, writes);
        const bool rlo = has(reads, nlo), rhi = L.wide && has(reads, nhi);
        if (!rlo && !rhi) continue;
        if (!feeding.count(k) || src.ops(i) != 3 || !src.pred(i).empty() || src.mnem(i).compare(0, 3, "CMP") != 0 || writes.size() != 1) return no("a bound is read outside the test");
        if (src.op(i, 2) == nlo || src.op(i, 2) == nhi) return no("the test writes a bound");
        const std::string &lt = lineTerms[k], want = rlo ? loTerm : hiTerm, bound = "s:" + (rlo ? nlo : nhi);
        if (lt.empty() || lt.find(want) == std::string::npos || lt.find(bound) == std::string::npos) return no("the test reads the counter before its step");
        for (std::size_t k2 = k + 1; k2 < L.bodyAt.size(); k2++) {
            std::vector<std::string> r2, w2;
            src.rw(L.bodyAt[k2], r2, w2);
            if (has(r2, writes[0]) && !feeding.count(k2)) return no("a bound's compare is read outside the test");
            if (has(w2, writes[0])) break;
        }
        tree.push_back(k);
    }
    if (tree.empty()) return no("no compare reads the bound");
    if (isNumber(nlo) || (L.wide && isNumber(nhi))) return no("a constant bound");
    if (isNumber(slo) && L.wide) return no("a constant step on a two-word counter");
    if (isNumber(slo) && std::atol(slo.c_str()) <= 0) return no("a step that is not positive");
    L.stepped = true;
    L.iv = lo; L.bound = nlo; L.cmp = L.bodyAt[writerAt[L.P]];
    L.ivs.push_back(lo); L.bounds.push_back(nlo); L.steps.push_back(slo);
    if (L.wide) { L.ivs.push_back(hi); L.bounds.push_back(nhi); L.steps.push_back(shi); }
    L.used.clear(); L.written.clear(); L.predicated.clear();
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        std::vector<std::string> reads, writes;
        src.rw(i, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) L.used.insert(reads[r]);
        for (std::size_t w = 0; w < writes.size(); w++) { L.used.insert(writes[w]); L.written.insert(writes[w]); if (!src.pred(i).empty()) L.predicated.insert(writes[w]); }
        if (src.mnem(i) == "MV" && src.ops(i) == 2 && src.op(i, 0) == src.op(i, 1)) continue;
        if (std::find(tree.begin(), tree.end(), k) != tree.end()) L.testBody.push_back(L.body.size());
        L.take(src, i);
    }
    why.clear();
    return true;
}

bool recognise(const Src &src, std::size_t back, const std::set<std::string> &labels, const std::set<std::string> &named, Loop &L, std::string &why) {
    const std::vector<Line> &v = src.v;
    const std::string bpred = src.pred(back);
    L.onStream = src.s != nullptr;
    if (bpred.empty() || src.ops(back) == 0 || labels.count(src.op(back, 0)) == 0) { why = "no conditional branch to a label"; return false; }
    L.label = src.op(back, 0);
    if (L.label.size() > 5 && L.label.compare(L.label.size() - 5, 5, "$fill") == 0) { why = "a fill loop, memory-bound, kept as written"; return false; }
    L.P = bpred[0] == '!' ? bpred.substr(1) : bpred;
    L.negated = bpred[0] == '!';
    std::size_t h = 0; bool found = false;
    for (std::size_t i = 0; i < back; i++) if (src.label(i) && src.name(i) == L.label) { h = i; found = true; }
    if (!found) { why = "the head is not above"; return false; }
    if (back + 1 >= v.size() || !src.label(back + 1)) { why = "no exit label after the branch"; return false; }
    L.head = h; L.back = back; L.exitName = src.name(back + 1);
    for (std::size_t k = h; k-- > 0 && src.instr(k) && !isBranch(src.mnem(k));) {
        L.pre.insert(L.pre.begin(), v[k]);
        if (src.typed(k)) L.preM.insert(L.preM.begin(), (*src.s)[k]); else L.preTyped = false;
    }
    if (src.naming(L.label) != 1) { why = "the head is entered from elsewhere"; return false; }
    for (std::size_t i = h + 1; i < back; i++) {
        if (src.label(i)) { if (!passThroughName(src.name(i), named)) { why = "a label inside"; return false; } continue; }
        if (!src.instr(i)) { why = "a directive inside"; return false; }
        if (isBranch(src.mnem(i))) { why = "a branch inside"; return false; }
        if (src.units(i) == 0) { why = "an instruction of no known unit: " + src.mnem(i); return false; }
        std::vector<std::string> reads, writes;
        src.rw(i, reads, writes);
        if (has(writes, "A15") || has(writes, "B15")) { why = "the stack pointer is written"; return false; }
        L.bodyAt.push_back(i);
    }
    if (L.bodyAt.size() < 2 || L.bodyAt.size() > 64) { why = "too small or too large"; return false; }
    // The compare: the last writer of P, of a form the count can be read from.
    bool cmpFound = false;
    for (std::size_t k = L.bodyAt.size(); k-- > 0;) {
        std::vector<std::string> reads, writes;
        src.rw(L.bodyAt[k], reads, writes);
        if (has(writes, L.P)) { L.cmp = L.bodyAt[k]; cmpFound = true; break; }
    }
    if (!cmpFound) { why = "the predicate is not written in the loop"; return false; }
    const std::size_t c = L.cmp;
    const std::string cm = src.mnem(c);
    bool lt = cm == "CMPLT" || cm == "CMPLTU", gt = cm == "CMPGT" || cm == "CMPGTU";
    if ((!lt && !gt) || src.ops(c) != 3 || !src.pred(c).empty() || src.op(c, 2) != L.P) return recogniseStepped(src, L, why, "the test is not a compare of two");
    // Loops while i < n: [P] with i < n or n > i; while i <= n: [!P] with i > n or n < i.
    const std::size_t ivAt = lt == !L.negated ? 0 : 1, boundAt = 1 - ivAt;
    L.iv = src.op(c, ivAt); L.bound = src.op(c, boundAt);
    L.plusOne = L.negated;
    long k0 = 0;
    const bool boundNumber = src.number(c, boundAt, k0);
    if (boundNumber) { L.boundConst = true; L.boundK = k0; }
    else if (!src.reg(c, boundAt)) { why = "the bound is not a register"; return false; }
    if (!src.reg(c, ivAt) || (L.boundConst && L.iv != src.op(c, 1))) { why = "the counter is not a register"; return false; }
    // P is read by the branch alone and is dead at the exit; i is stepped once, by one, before the test.
    L.liveExit = v[back + 1].liveIn;
    if (L.liveExit & bitOf(L.P)) { why = "the predicate lives past the loop"; return false; }
    int ivWrites = 0, boundWrites = 0, pWrites = 0;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        const bool plain = src.pred(i).empty();
        std::vector<std::string> reads, writes;
        src.rw(i, reads, writes);
        if (has(reads, L.P) && pWrites == 0) { why = "the predicate's test is read in the loop"; return false; }
        if (has(writes, L.iv)) {
            ivWrites++;
            long one = 0;
            bool step = src.mnem(i) == "ADD" && plain && src.ops(i) == 3 && src.op(i, 2) == L.iv &&
                        ((src.number(i, 0, one) && one == 1 && src.op(i, 1) == L.iv) || (src.number(i, 1, one) && one == 1 && src.op(i, 0) == L.iv));
            if (!step || i > L.cmp) return recogniseStepped(src, L, why, "the counter is not stepped by one before the test");
        }
        long kb = 0;
        if (!boundNumber && has(writes, L.bound) && i < L.cmp) {
            boundWrites++;
            if (src.mnem(i) == "MVK" && plain && src.ops(i) == 2 && src.number(i, 0, kb)) { L.boundConst = true; L.boundK = kb; }
            else L.boundConst = false;
        }
        if (has(writes, L.P) && i != L.cmp) pWrites++;
        for (std::size_t r = 0; r < reads.size(); r++) L.used.insert(reads[r]);
        for (std::size_t w = 0; w < writes.size(); w++) { L.used.insert(writes[w]); L.written.insert(writes[w]); if (!plain) L.predicated.insert(writes[w]); }
    }
    if (ivWrites != 1) { why = "the counter is stepped " + std::to_string(ivWrites) + " times"; return false; }
    if (boundWrites > 0 && !L.boundConst) { why = "the bound is written in the loop"; return false; }
    // The kernel's counter: P itself where the compare alone writes it, else a condition register the loop leaves alone.
    if (pWrites == 0) L.C = L.P;
    else for (std::size_t k = 0; k < 6 && L.C.empty(); k++)
        if (!L.used.count(kCond[k]) && !(L.liveExit & bitOf(kCond[k])) && kCond[k] != L.P) L.C = kCond[k];
    if (L.C.empty()) { why = "no condition register for the count"; return false; }
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const std::size_t i = L.bodyAt[k];
        if (i != L.cmp && !(src.mnem(i) == "MV" && src.ops(i) == 2 && src.op(i, 0) == src.op(i, 1))) L.take(src, i);
    }
    return true;
}

int antiDelay(const Node &r, const Node &w) { return std::max(r.store ? 1 : 0, r.late - w.lat + 1); }

// The dependences of one iteration and the next: register flow, anti and output orders in program order, the
// same wrapped round the back edge - the anti and output ones only for a register kept under one name - and memory.
void buildEdges(Loop &L, const std::set<std::string> &renameable) {
    std::vector<Node> &n = L.nodes;
    L.edges.clear();
    std::map<std::string, std::vector<std::pair<int, bool> > > acc;     // register -> (op, isWrite) in order
    for (std::size_t o = 0; o < n.size(); o++) {
        std::vector<std::string> reads, writes;
        L.rw(o, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) acc[reads[r]].push_back(std::make_pair(static_cast<int>(o), false));
        for (std::size_t w = 0; w < writes.size(); w++) acc[writes[w]].push_back(std::make_pair(static_cast<int>(o), true));
    }
    for (std::map<std::string, std::vector<std::pair<int, bool> > >::iterator it = acc.begin(); it != acc.end(); ++it) {
        const std::vector<std::pair<int, bool> > &a = it->second;
        // A writer's result lands at its latency - but an address it steps lands in E1, a cycle after issue.
        auto latOf = [&](int w) { return (n[static_cast<std::size_t>(w)].stepped & bitOf(it->first)) ? 1 : n[static_cast<std::size_t>(w)].lat; };
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
                if (nextW >= 0 && a[static_cast<std::size_t>(nextW)].first != op && (prevW >= 0 || !renameable.count(it->first))) { const Node &w = n[static_cast<std::size_t>(a[static_cast<std::size_t>(nextW)].first)]; Edge e = { op, a[static_cast<std::size_t>(nextW)].first, antiDelay(n[static_cast<std::size_t>(op)], w), 0, "anti" }; L.edges.push_back(e); }
                if (nextW < 0 && !renameable.count(it->first)) { const Node &w = n[static_cast<std::size_t>(a[static_cast<std::size_t>(firstW)].first)]; Edge e = { op, a[static_cast<std::size_t>(firstW)].first, antiDelay(n[static_cast<std::size_t>(op)], w), 1, "anti1" }; L.edges.push_back(e); }
            } else if (nextW >= 0) {
                const Node &w1 = n[static_cast<std::size_t>(op)], &w2 = n[static_cast<std::size_t>(a[static_cast<std::size_t>(nextW)].first)];
                Edge e = { op, a[static_cast<std::size_t>(nextW)].first, std::max(1, w1.lat - w2.lat + 1), 0, "out" }; L.edges.push_back(e);
            } else if (!renameable.count(it->first)) {
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
        const std::uint64_t gate = bitOf(L.stepped ? L.P : L.iv);
        if (L.nodes[o].writes & gate) len = std::max(len, t[o] + L.nodes[o].lat + (L.stepped ? 0 : 1) + 6);
    }
    return std::max(len, 6);
}

// One try at an initiation interval: each instruction in program order at the first cycle in the window its
// scheduled neighbours leave it whose modulo slot has room - the branch and the count's step reserved first.
bool scheduleAt(Loop &L, int II, std::vector<int> &t, const std::set<std::string> &renameable, std::map<std::string, int> &minQ, std::vector<int> &push) {
    const std::size_t n = L.nodes.size();
    t.assign(n, -1);
    minQ.clear();
    std::uint64_t renameMask = 0;
    for (std::set<std::string>::const_iterator it = renameable.begin(); it != renameable.end(); ++it) renameMask |= bitOf(*it);
    std::vector<Packet> mrt(static_cast<std::size_t>(II));
    std::vector<Node> extra;
    extra.reserve(2 + n * 5);
    std::set<std::string> lab;
    lab.insert(L.label + "$pipe");
    // The kernel's branch, on the count where there is one and on the loop's own test where the counter is stepped.
    extra.push_back(makeNode(rebuilt("B", std::vector<std::string>(1, L.label + "$pipe"), L.stepped ? (L.negated ? "!" + L.P : L.P) : L.C), lab));
    if (!L.stepped) extra.push_back(makeNode(make("ADD", "-1", L.C, L.C), std::set<std::string>()));
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
                if (tracing() > 2 && t[static_cast<std::size_t>(d.to)] - d.delay + d.dist * II < hi) std::fprintf(stderr, "    hi by%s (%s delay %d dist %d)\n", L.body[static_cast<std::size_t>(d.to)].raw.c_str(), d.why, d.delay, d.dist);
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
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: no slot for%s in [%d,%d]\n", II, L.body[o].raw.c_str(), lo, hi);
            if (hi < lo && hiBy >= 0) push[static_cast<std::size_t>(hiBy)] += lo - hi;
            return false;
        }
    }
    // Two writes of one renamed register in one slot are different iterations' copies: q must tell them apart.
    for (std::size_t o = 0; o < n; o++)
        for (std::size_t p = 0; p < o; p++) {
            const std::uint64_t both = L.nodes[p].writes & L.nodes[o].writes & renameMask;
            if (!both || t[p] % II != t[o] % II) continue;
            for (int r = 0; r < 64; r++) if (both & (1ull << r)) { std::string name = std::string(r < 32 ? "A" : "B") + std::to_string(r % 32); minQ[name] = std::max(minQ[name], std::abs(t[o] - t[p]) / II + 1); }
        }
    return true;
}

// A register's rename unit: itself, or the even:odd pair it is half of.
struct Unit { std::vector<std::string> regs; int q = 1; std::vector<std::vector<std::string> > copies; bool carried = false, exits = false; };

int pow2At(int x) { int p = 1; while (p < x) p *= 2; return p; }
long modq(long a, int q) { return ((a % q) + q) % q; }

// Body instruction o as turn j writes it: each register of a unit renamed to that turn's copy - a source to the copy
// it reads, the destination to the one it writes - on the entry where the body is the stream's, else on its line.
std::string spelledLine(const Loop &L, const std::vector<Unit> &units, const std::map<std::string, std::size_t> &unitOf,
                        const std::vector<int> &firstWrite, std::size_t o, long j) {
    const Line &l = L.body[o];
    std::vector<std::string> ops = l.ops;
    std::string pred = l.pred;
    bool hasDest = !ops.empty() && !isStore(l.mnem) && l.mnem != "B" && ops.back()[0] != '*';
    std::size_t k0 = 0;
    for (std::map<std::string, std::size_t>::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k0++) {
        const Unit &u = units[it->second];
        if (u.q < 2) continue;
        const std::string &r = it->first;
        std::size_t half = 0;
        while (u.regs[half] != r) half++;
        const long readJ = j + (firstWrite[k0] >= static_cast<int>(o) ? -1 : 0);
        const std::string rd = u.copies[half][static_cast<std::size_t>(modq(readJ, u.q))], wr = u.copies[half][static_cast<std::size_t>(modq(j, u.q))];
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

Turn spelled(const Loop &L, const std::vector<Unit> &units, const std::map<std::string, std::size_t> &unitOf,
             const std::vector<int> &firstWrite, std::size_t o, long j) {
    Turn turn;
    if (L.bodyM.empty() || L.bodyM[o].unit != 0 || L.bodyM[o].par || L.bodyM[o].packed) { turn.raw = spelledLine(L, units, unitOf, firstWrite, o, j); return turn; }
    MInst x = L.bodyM[o];
    const std::string m = x.m.str();
    const bool hasDest = x.operands && !isStore(m) && m != "B" && x.op[x.operands - 1].kind != MOperand::Memory;
    std::size_t k0 = 0;
    for (std::map<std::string, std::size_t>::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k0++) {
        const Unit &u = units[it->second];
        if (u.q < 2) continue;
        std::size_t half = 0;
        while (u.regs[half] != it->first) half++;
        const long readJ = j + (firstWrite[k0] >= static_cast<int>(o) ? -1 : 0);
        const int r = regNumber(it->first), rd = regNumber(u.copies[half][static_cast<std::size_t>(modq(readJ, u.q))]),
                  wr = regNumber(u.copies[half][static_cast<std::size_t>(modq(j, u.q))]);
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
    // Under CXX1_C6X_MIR=1 the line's spelling too: `c6x-spell` names a turn the entry writes otherwise.
    if (std::getenv("CXX1_C6X_MIR") != nullptr && turn.raw != spelledLine(L, units, unitOf, firstWrite, o, j))
        std::fprintf(stderr, "c6x-spell: %s against %s\n", turn.raw.c_str(), spelledLine(L, units, unitOf, firstWrite, o, j).c_str());
    turn.e = x;
    turn.typed = true;
    return turn;
}

Line verbatim(const std::string &raw) { Line l; l.raw = raw; l.verbatim = true; return l; }

// **What the emitters write** (M6c round 11): lines for a loop read from text; on the stream entries - an instruction
// built from its parts, a turn the body's renamed entry, placed as it is put. `ok` false: one the stream cannot hold.
struct Code {
    std::vector<Line> lines;
    MStream m;
    const MStream *from = nullptr;          // the stream being rewritten; none for the text
    bool ok = true;
    bool text() const { static const bool census = std::getenv("CXX1_C6X_MIR") != nullptr; return !from || census; }
    static std::vector<std::string> opsOf(const std::string &a, const std::string &b, const std::string &c) {
        std::vector<std::string> ops(1, a);
        if (!b.empty()) ops.push_back(b);
        if (!c.empty()) ops.push_back(c);
        return ops;
    }
    // The entry of these parts; false where the predicate names no register, which an entry cannot hold.
    static bool entryOf(const std::string &mnem, const std::vector<std::string> &ops, const std::string &pred, MInst &e) {
        if (ops.size() > 4) return false;
        e = MInst(mnem.c_str());
        for (std::size_t k = 0; k < ops.size(); k++) e.op[e.operands++] = MOperand(ops[k]);
        if (pred.empty()) return true;
        e.predNot = pred[0] == '!';
        e.pred = static_cast<std::uint8_t>(regNumber(pred.substr(e.predNot ? 1 : 0)));
        return e.pred != kNoReg;
    }
    // A composed line (the window's): its fields the entry's where it spells the line, else the line read.
    void push_back(const Line &l) {
        if (text()) lines.push_back(l);
        if (!from || !ok) return;
        MInst e;
        if (l.instr && !l.verbatim && entryOf(l.mnem, l.ops, l.pred, e) && e.str() == l.raw + "\n") { m.push_back(e); return; }
        ok = streamOf(std::vector<Line>(1, l), m);
    }
    void add(const std::string &mnem, const std::string &a, const std::string &b = "", const std::string &c = "", const std::string &pred = "") {
        const std::vector<std::string> ops = opsOf(a, b, c);
        if (text()) lines.push_back(rebuilt(mnem, ops, pred));
        if (!from || !ok) return;
        MInst e;
        ok = entryOf(mnem, ops, pred, e);
        if (ok) m.push_back(e);
    }
    // `legalForm` and the instruction: commuted where both sources cross, false with the reason where it has no form.
    bool legal(std::string &why, const std::string &mnem, const std::string &a, const std::string &b = "", const std::string &c = "") {
        const std::vector<std::string> ops = opsOf(a, b, c);
        MInst e;
        const bool typed = from && ok && entryOf(mnem, ops, "", e) && modelled(e);
        Line l;
        bool fine = false;
        if (!typed || text()) { l = rebuilt(mnem, ops); fine = legalForm(l); }
        if (!typed) {
            if (!fine) { why = "the stepped guard has no legal form for" + l.raw; return false; }
            push_back(l);
            return true;
        }
        const bool commutes = mnem == "ADD" || mnem == "AND" || mnem == "OR" || mnem == "XOR" || mnem == "MPYDP" || mnem == "ADDDP" || mnem == "MPYSP" ||
                              mnem == "ADDSP" || mnem == "MPY32" || mnem == "CMPEQ";
        if (crossings(e) > 1 && commutes && e.operands == 3 && e.op[0].kind != MOperand::Immediate && e.op[1].kind != MOperand::Immediate) std::swap(e.op[0], e.op[1]);
        const bool has = crossings(e) <= 1 && (unitsFor(e) != 0 || isLoad(mnem) || isStore(mnem));
        std::string raw = e.str();
        raw.erase(raw.size() - 1);
        if (text()) {
            if (has != fine || (has && raw != l.raw)) std::fprintf(stderr, "c6x-emit: legal%s against%s\n", raw.c_str(), l.raw.c_str());
            if (fine) lines.push_back(l);
        }
        if (!has) { why = "the stepped guard has no legal form for" + raw; return false; }
        m.push_back(e);
        return true;
    }
    // A placed line - a turn, idle cycles, the kernel's label: the entry given, else the line read as placed.
    void placed(const std::string &raw, bool par, const MInst *e) {
        if (text() || (!e && ok)) lines.push_back(verbatim((par ? "||" : "") + raw));
        if (!from || !ok) return;
        if (!e) { ok = streamOf(std::vector<Line>(1, lines.back()), m); if (!text()) lines.pop_back(); return; }
        m.push_back(*e);
        m.back().par = par;
        m.back().packed = true;
    }
    void nop(int n) { const MInst e("NOP", n); placed("\tNOP\t" + std::to_string(n), false, &e); }
    void label(const std::string &name) { MInst e; e.kind = MInst::Label; e.m = opt::StrId(name); placed(name + ":", false, &e); }
    // Line k of the function as it stands: the stream's own entry.
    void same(const std::vector<Line> &v, std::size_t k) {
        if (!from || !ok) { push_back(v[k]); return; }
        const MInst &e = (*from)[k];
        if (e.kind != MInst::Instruction || e.packed || e.par || e.unit != 0 || !modelled(e)) { push_back(v[k]); return; }
        if (text()) lines.push_back(v[k]);
        m.push_back(e);
    }
};

Turn turnOf(const std::string &mnem, const std::vector<std::string> &ops, const std::string &pred = "") {
    Turn t;
    t.raw = rebuilt(mnem, ops, pred).raw;
    t.typed = Code::entryOf(mnem, ops, pred, t.e);
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
struct Web { std::string reg, name; int link = -1; bool keep = false; };

// Each write of a temporary starts a web of its own, and every web but the first and the one a later iteration or
// the exit reads takes a fresh name from the pool - so a name the backend recycles through an iteration becomes
// several short-lived ones, each renamed per iteration on its own account.
void renameWebs(Loop &L, std::vector<std::string> &pool) {
    std::vector<Web> webs;
    std::map<std::string, int> cur;
    std::set<std::string> fixed;                            // registers no web of which may be renamed
    std::vector<std::vector<std::pair<int, int> > > opWebs(L.body.size());   // (web, isWrite) per op
    for (std::size_t o = 0; o < L.body.size(); o++) {
        const Line &l = L.body[o];
        std::vector<std::string> reads, writes;
        L.rw(o, reads, writes);
        bool rmw = l.mnem == "MVKH" || l.mnem == "ADDK" || !l.pred.empty() || !L.steppedAt(o).empty();
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (!cur.count(reads[r])) { Web w; w.reg = reads[r]; w.keep = true; cur[reads[r]] = static_cast<int>(webs.size()); webs.push_back(w); }
            opWebs[o].push_back(std::make_pair(cur[reads[r]], 0));
        }
        // A pair operand links its halves' current webs - the sources before the writes are seen, the
        // destination after; a half linked twice over keeps its names.
        bool hasDest = !l.ops.empty() && !isStore(l.mnem) && l.mnem != "B" && l.ops.back()[0] != '*';
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) for (std::size_t w = 0; w < writes.size(); w++) {
                if (!rmw || !cur.count(writes[w])) { Web nw; nw.reg = writes[w]; cur[writes[w]] = static_cast<int>(webs.size()); webs.push_back(nw); }
                opWebs[o].push_back(std::make_pair(cur[writes[w]], 1));
            }
            for (std::size_t k = 0; k < l.ops.size(); k++) {
                const bool dest = hasDest && k + 1 == l.ops.size();
                if (dest != (pass == 1)) continue;
                std::vector<std::string> regs(2);
                if (!L.pairIn(o, k, regs[0], regs[1]) || !cur.count(regs[0]) || !cur.count(regs[1])) continue;
                Web &hi = webs[static_cast<std::size_t>(cur[regs[0]])], &lo = webs[static_cast<std::size_t>(cur[regs[1]])];
                if ((hi.link >= 0 && hi.link != cur[regs[1]]) || (lo.link >= 0 && lo.link != cur[regs[0]])) { fixed.insert(regs[0]); fixed.insert(regs[1]); }
                hi.link = cur[regs[1]]; lo.link = cur[regs[0]];
            }
        }
    }
    for (std::map<std::string, int>::const_iterator it = cur.begin(); it != cur.end(); ++it) {
        const std::string &r = it->first;
        bool carried = false;
        for (std::size_t w = 0; w < webs.size(); w++) if (webs[w].reg == r && webs[w].keep) carried = true;
        if (carried || (L.liveExit & bitOf(r)) || r == L.iv || r == L.bound || r == L.C || r == L.P || L.pinned(r)) webs[static_cast<std::size_t>(it->second)].keep = true;
    }
    for (std::size_t w = 0; w < webs.size(); w++) {
        Web &x = webs[w];
        if (x.name.empty()) x.name = x.reg;
        if (x.keep || fixed.count(x.reg) || x.reg == "A15" || x.reg == "B15" || x.reg == "B3" || x.reg == "B14") continue;
        if (x.link >= 0 && (webs[static_cast<std::size_t>(x.link)].keep || fixed.count(webs[static_cast<std::size_t>(x.link)].reg))) continue;
        if (x.name != x.reg) continue;                      // named with its partner already
        for (std::size_t f = 0; f < pool.size(); f++) {
            if (sideOf(pool[f]) != sideOf(x.reg)) continue;
            if (x.link < 0) { x.name = pool[f]; pool.erase(pool.begin() + static_cast<long>(f)); break; }
            int num = std::atoi(pool[f].c_str() + 1);
            if (num % 2) continue;
            std::string hi = std::string(1, pool[f][0]) + std::to_string(num + 1);
            std::vector<std::string>::iterator ih = std::find(pool.begin(), pool.end(), hi);
            if (ih == pool.end()) continue;
            Web &y = webs[static_cast<std::size_t>(x.link)];
            const bool xLow = std::atoi(x.reg.c_str() + 1) % 2 == 0;
            x.name = xLow ? pool[f] : hi; y.name = xLow ? hi : pool[f];
            pool.erase(ih); pool.erase(pool.begin() + static_cast<long>(f));
            break;
        }
    }
    for (std::size_t o = 0; o < L.body.size(); o++) {
        Line &l = L.body[o];
        std::vector<std::string> ops = l.ops;
        std::string pred = l.pred;
        bool hasDest = !ops.empty() && !isStore(l.mnem) && l.mnem != "B" && ops.back()[0] != '*';
        for (std::size_t k = 0; k < opWebs[o].size(); k++) {
            const Web &w = webs[static_cast<std::size_t>(opWebs[o][k].first)];
            if (w.name == w.reg) continue;
            const bool isWrite = opWebs[o][k].second == 1;
            for (std::size_t p = 0; p < ops.size(); p++) {
                bool dest = hasDest && p + 1 == ops.size();
                if (dest == isWrite || (dest && (l.mnem == "MVKH" || l.mnem == "ADDK"))) ops[p] = renamed(ops[p], w.reg, w.name);
            }
            if (!isWrite) pred = renamed(pred, w.reg, w.name);
            L.rename(o, w.reg, w.name, isWrite);
        }
        l = rebuilt(l.mnem, ops, pred);
    }
    L.used.clear(); L.written.clear(); L.predicated.clear();
    for (std::size_t o = 0; o < L.body.size(); o++) {
        std::vector<std::string> reads, writes;
        L.rw(o, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) L.used.insert(reads[r]);
        for (std::size_t w = 0; w < writes.size(); w++) { L.used.insert(writes[w]); L.written.insert(writes[w]); if (!L.body[o].pred.empty()) L.predicated.insert(writes[w]); }
    }
    L.used.insert(L.P);
    std::set<std::string> seen;
    for (std::size_t o = 0; o < L.body.size(); o++) {
        std::vector<std::string> reads, writes;
        L.rw(o, reads, writes);
        const std::string stepped = L.steppedAt(o);
        for (std::size_t w = 0; w < writes.size(); w++) {
            if (!seen.count(writes[w]) && (L.body[o].mnem == "MVKH" || L.body[o].mnem == "ADDK")) L.rmwFirst.insert(writes[w]);
            if (writes[w] == stepped) L.rmwFirst.insert(writes[w]);
            seen.insert(writes[w]);
        }
    }
}

// Whether register r is read before it is written on the path from line k round the back edge to line o - the
// value r holds after line o is wanted - or is live at the exit.
bool wantedAfter(const Loop &L, std::size_t o, const std::string &r) {
    const std::vector<Line> &b = L.body;
    for (std::size_t n = 0, k = o + 1; n < b.size(); n++, k = (k + 1) % b.size()) {
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
    std::vector<Line> &b = L.body;
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t o = 0; o < b.size() && !changed; o++) {
            if (b[o].mnem != "MV" || b[o].ops.size() != 2 || !b[o].pred.empty() || !sideOf(b[o].ops[0]) || !sideOf(b[o].ops[1])) continue;
            const std::string S = b[o].ops[0], D = b[o].ops[1];
            if (D == "A15" || D == "B15" || D == "B3" || D == "B14" || D == L.iv || D == L.bound || D == L.P) continue;
            for (std::size_t k = o + 1; k < b.size(); k++) {
                std::vector<std::string> reads, writes;
                L.rw(k, reads, writes);
                if (b[k].mnem == "MV" && b[k].ops.size() == 2 && b[k].pred.empty() && b[k].ops[0] == D && b[k].ops[1] == S) { L.erase(k); changed = true; break; }
                if (has(writes, S) || has(writes, D)) break;
            }
            if (changed) break;
            if ((L.liveExit & bitOf(D)) || wantedAfter(L, o, D)) continue;
            L.erase(o);
            changed = true;
        }
    }
}

// CXX1_C6X_MIR=1: the body's entries against its lines after an edit (`c6x-body`); a loop on the stream whose body
// is not all modelled, and so is read from its lines, is named too (`c6x-loop-lines`).
void checkBody(const Loop &L, const char *at) {
    static const bool check = std::getenv("CXX1_C6X_MIR") != nullptr;
    if (!check || !L.onStream) return;
    if (L.bodyM.empty() && !L.body.empty()) { std::fprintf(stderr, "c6x-loop-lines: %s %s\n", at, L.label.c_str()); return; }
    if (L.bodyM.size() != L.body.size()) { std::fprintf(stderr, "c6x-body: %s %s sizes\n", at, L.label.c_str()); return; }
    for (std::size_t o = 0; o < L.body.size(); o++) {
        std::string t = L.bodyM[o].str();
        if (!t.empty() && t[t.size() - 1] == '\n') t.erase(t.size() - 1);
        const Line p = parse(t), &l = L.body[o];
        long by;
        if (p.mnem != l.mnem || p.ops != l.ops || p.pred != l.pred || L.steppedAt(o) != steppedRegister(l, by)) { std::fprintf(stderr, "c6x-body: %s %s|%s\n", at, l.raw.c_str(), t.c_str()); return; }
    }
}

// A register of free's on the side asked for, or any; "" for none. With pair, an aligned even:odd pair's even half.
std::string takeFree(std::vector<std::string> &free, char side, bool pair) {
    for (int pass = 0; pass < 2; pass++)
        for (std::size_t f = 0; f < free.size(); f++) {
            if (pass == 0 && sideOf(free[f]) != side) continue;
            if (!pair) { std::string r = free[f]; free.erase(free.begin() + static_cast<long>(f)); return r; }
            int num = std::atoi(free[f].c_str() + 1);
            if (num % 2) continue;
            std::string hi = std::string(1, free[f][0]) + std::to_string(num + 1);
            std::vector<std::string>::iterator ih = std::find(free.begin(), free.end(), hi);
            if (ih == free.end()) continue;
            std::string r = free[f];
            free.erase(ih); free.erase(free.begin() + static_cast<long>(f));
            return r;
        }
    return "";
}

// The loop's own test in one canonical shape: P = iv < n where the loop runs while iv < n, P = n < iv where it runs
// while iv <= n; a two-word counter signed through the high word and unsigned through the low, over two scratch registers.
bool steppedTest(const Loop &L, Code &out, const std::string &nlo, const std::string &nhi, const std::string &t, const std::string &tb, std::string &why) {
    const bool less = !L.plusOne;
    const std::string &lo = L.ivs[0];
    const bool across = sideOf(L.P) != sideOf(lo);    // the result is made beside the counter and copied to P
    if (!L.wide) {
        if (!across) return out.legal(why, less ? "CMPLT" : "CMPGT", lo, nlo, L.P);
        if (t.empty()) { why = "the stepped test has no scratch register beside the counter"; return false; }
        return out.legal(why, less ? "CMPLT" : "CMPGT", lo, nlo, t) && out.legal(why, "MV", t, L.P);
    }
    const std::string &hi = L.ivs[1], &x = across ? tb : L.P;
    return out.legal(why, "CMPEQ", hi, nhi, t) && out.legal(why, less ? "CMPLTU" : "CMPGTU", lo, nlo, tb) &&
           out.legal(why, "AND", t, tb, t) && out.legal(why, less ? "CMPLT" : "CMPGT", hi, nhi, tb) &&
           out.legal(why, "OR", t, tb, x) && (!across || out.legal(why, "MV", x, L.P));
}

// **The stepped loop's entry**: the step must not be negative, K = m*s and Kg = g*s must fit, n-K (the kernel's bound)
// and n-Kg must not overflow, and turn g must exist - `iv REL n-Kg` by the loop's own test - or the loop runs as written.
// Every check branches to the head, and the test is the loop's own over the bound n - Kg; one or two words.
bool steppedGuard(const Loop &L, Code &out, const std::vector<std::string> &moved, const std::string &xt, const std::string &xtb,
                  std::vector<std::string> free, int m, int g, std::string &why) {
    const std::string &slo = L.steps[0], &nlo = L.bounds[0];
    const std::string shi = L.wide ? L.steps[1] : "", nhi = L.wide ? L.bounds[1] : "";
    const bool constant = isNumber(slo);
    const char stepSide = constant ? sideOf(nlo) : sideOf(slo);
    const std::string klo = takeFree(free, stepSide, true), tm = takeFree(free, stepSide, false), t2 = takeFree(free, stepSide, false), tb = takeFree(free, sideOf(nlo), false);
    if (klo.empty() || tm.empty() || t2.empty() || tb.empty()) { why = "no free registers for the stepped guard"; return false; }
    const std::string khi = std::string(1, klo[0]) + std::to_string(std::atoi(klo.c_str() + 1) + 1);
    const std::string skip = L.negated ? L.P : "!" + L.P;    // the branch past the pipelined copy, taken where the loop would not go on
    if (!constant && !out.legal(why, "CMPGT", "0", L.wide ? shi : slo, L.P)) return false;
    if (!constant) out.add("B", L.label, "", "", L.P);
    // K = k*s into khi:klo, checked to fit: a 31-bit value for one word, a 47-bit step for two.
    auto times = [&](int k) -> bool {
        if (constant) {
            long kk = k * std::atol(slo.c_str());
            if (kk < 0 || kk > 0x7fffffff) { why = "the stepped guard's constant does not fit"; return false; }
            if (kk <= 32767) out.add("MVK", std::to_string(kk), klo);
            else { out.add("MVKL", std::to_string(kk), klo); out.add("MVKH", std::to_string(kk), klo); }
            return true;
        }
        out.add("MVK", std::to_string(k), tm);
        if (!out.legal(why, "MPY32U", slo, tm, khi + ":" + klo)) return false;
        if (!L.wide) {
            if (!out.legal(why, "CMPGT", "0", klo, tb) || !out.legal(why, "XOR", "1", tb, tb)) return false;
            if (!out.legal(why, "CMPEQ", "0", khi, L.P) || !out.legal(why, "AND", L.P, tb, L.P)) return false;
            out.add("B", L.label, "", "", "!" + L.P);
            return true;
        }
        if (!out.legal(why, "MPY32", shi, tm, t2) || !out.legal(why, "ADD", t2, khi, khi)) return false;
        if (!out.legal(why, "SHRU", shi, "15", t2) || !out.legal(why, "CMPEQ", "0", t2, L.P)) return false;
        out.add("B", L.label, "", "", "!" + L.P);
        return true;
    };
    // d = n - (khi:klo), into dlo (and dhi), then the overflow check: n negative and the difference not.
    auto minus = [&](const std::string &dlo, const std::string &dhi) -> bool {
        if (!L.wide) {
            if (!out.legal(why, "SUB", nlo, klo, dlo)) return false;
            if (!out.legal(why, "CMPGT", "0", nlo, L.P) || !out.legal(why, "CMPGT", "0", dlo, tb)) return false;
        } else {
            if (!out.legal(why, "CMPLTU", nlo, klo, tb) || !out.legal(why, "SUB", nlo, klo, dlo)) return false;
            if (!out.legal(why, "SUB", nhi, khi, dhi) || !out.legal(why, "SUB", dhi, tb, dhi)) return false;
            if (!out.legal(why, "CMPGT", "0", nhi, L.P) || !out.legal(why, "CMPGT", "0", dhi, tb)) return false;
        }
        if (!out.legal(why, "XOR", "1", tb, tb) || !out.legal(why, "AND", L.P, tb, L.P)) return false;
        out.add("B", L.label, "", "", L.P);
        return true;
    };
    if (!times(m) || !minus(moved[0], L.wide ? moved[1] : "")) return false;
    if (g != m && !times(g)) return false;
    if (!minus(klo, khi)) return false;
    // The entry test over the bound n - Kg, then the branch past the copy where it fails.
    if (!steppedTest(L, out, klo, khi, xt, xtb, why)) return false;
    out.add("B", L.label, "", "", skip);
    return true;
}

// The whole rewrite of one recognised loop; false, with the reason, where it is not worth it or cannot be done.
bool pipeline(const std::vector<Line> &v, Loop &L, Code &out, std::string &why) {
    // The pool of names the loop may take: caller-saved, unused in it, dead at its exit.
    std::vector<std::string> pool;
    for (int side = 0; side < 2; side++)
        for (int k = 3; k < 32; k++) {
            if ((k >= 10 && k <= 15) || (side == 1 && k == 3)) continue;
            std::string r = std::string(side ? "B" : "A") + std::to_string(k);
            if (!L.used.count(r) && !(L.liveExit & bitOf(r)) && r != L.C && r != L.P) pool.push_back(r);
        }
    std::string X;
    if (!L.stepped) {
        for (std::size_t k = 0; k < pool.size() && X.empty(); k++) if (sideOf(pool[k]) == sideOf(L.C)) X = pool[k];
        if (X.empty()) { why = "no free register for the count"; return false; }
        pool.erase(std::find(pool.begin(), pool.end(), X));
    }
    // A stepped counter's kernel tests the moved bound: a fresh register per word, taking the bound's own side.
    std::vector<std::string> moved;
    for (std::size_t w = 0; w < L.bounds.size(); w++) {
        std::string f;
        for (int pass = 0; pass < 2 && f.empty(); pass++)
            for (std::size_t k = 0; k < pool.size() && f.empty(); k++)
                if (pass == 1 || sideOf(pool[k]) == sideOf(L.bounds[w])) { f = pool[k]; pool.erase(pool.begin() + static_cast<long>(k)); }
        if (f.empty()) { why = "no free register for the moved bound"; return false; }
        moved.push_back(f);
    }
    // The test's scratch registers, beside the counter so that only the bound crosses: taken before the renaming can.
    std::string xt, xtb;
    if (L.stepped && (L.wide || sideOf(L.P) != sideOf(L.ivs[0]))) {
        xt = takeFree(pool, sideOf(L.ivs[0]), false);
        if (L.wide) xtb = takeFree(pool, sideOf(L.ivs[0]), false);
        if (xt.empty() || (L.wide && xtb.empty()) || sideOf(xt) != sideOf(L.ivs[0]) || (L.wide && sideOf(xtb) != sideOf(L.ivs[0]))) {
            why = "no free register beside the counter for the stepped test"; return false;
        }
    }
    for (std::size_t k = 0; k < L.testBody.size(); k++) {
        Line &l = L.body[L.testBody[k]];
        std::vector<std::string> ops = l.ops;
        for (std::size_t o = 0; o + 1 < ops.size(); o++)
            for (std::size_t w = 0; w < L.bounds.size(); w++) if (ops[o] == L.bounds[w]) {
                ops[o] = moved[w];
                if (!L.bodyM.empty()) L.bodyM[L.testBody[k]].op[o] = MOperand(moved[w]);
            }
        l = rebuilt(l.mnem, ops, l.pred);
    }
    checkBody(L, "recognised");
    foldBodyCopies(L);
    if (tracing() > 4) for (std::size_t o = 0; o < L.body.size(); o++) std::fprintf(stderr, "  as written%s\n", L.body[o].raw.c_str());
    renameWebs(L, pool);
    checkBody(L, "renamed");
    std::set<std::string> labels;
    labels.insert(L.label + "$pipe");
    for (std::size_t o = 0; o < L.body.size(); o++) {
        if (L.bodyM.empty()) { L.nodes.push_back(makeNode(L.body[o], labels)); continue; }
        L.nodes.push_back(makeNode(L.bodyM[o], labels));
    }
    // The rename units, and which of them may take a name per iteration.
    std::vector<Unit> units;
    std::map<std::string, std::size_t> unitOf;
    for (std::size_t o = 0; o < L.body.size(); o++)
        for (std::size_t k = 0; k < L.body[o].ops.size(); k++) {
            std::vector<std::string> regs(2);
            if (!L.pairIn(o, k, regs[0], regs[1])) continue;
            if (!unitOf.count(regs[0]) && !unitOf.count(regs[1])) { Unit u; u.regs.push_back(regs[1]); u.regs.push_back(regs[0]); unitOf[regs[0]] = unitOf[regs[1]] = units.size(); units.push_back(u); }
        }
    for (std::set<std::string>::const_iterator it = L.used.begin(); it != L.used.end(); ++it)
        if (!unitOf.count(*it)) { Unit u; u.regs.push_back(*it); unitOf[*it] = units.size(); units.push_back(u); }
    std::vector<int> firstWrite(unitOf.size(), 1 << 30);
    {
        std::size_t k = 0;
        for (std::map<std::string, std::size_t>::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k++)
            for (std::size_t o = 0; o < L.nodes.size(); o++) if (L.nodes[o].writes & bitOf(it->first)) { firstWrite[k] = static_cast<int>(o); break; }
    }
    std::set<std::string> renameable;
    for (std::size_t u = 0; u < units.size(); u++) {
        bool ok = true;                                     // every half written: a pair's names move together
        for (std::size_t h = 0; h < units[u].regs.size(); h++) {
            const std::string &r = units[u].regs[h];
            if (!L.written.count(r)) ok = false;
            if (L.predicated.count(r) || L.rmwFirst.count(r) || r == L.C || r == L.P || r == "A15" || r == "B15" || r == "B3" || r == "B14") { ok = false; break; }
        }
        if (ok) for (std::size_t h = 0; h < units[u].regs.size(); h++) renameable.insert(units[u].regs[h]);
    }
    if (tracing() > 3) for (std::size_t o = 0; o < L.body.size(); o++) std::fprintf(stderr, "  body%s\n", L.body[o].raw.c_str());
    if (tracing() > 2) { std::fprintf(stderr, "  renameable:"); for (std::set<std::string>::const_iterator it = renameable.begin(); it != renameable.end(); ++it) std::fprintf(stderr, " %s", it->c_str()); std::fprintf(stderr, "\n"); }
    std::vector<int> t;
    std::map<std::string, int> minQ;
    std::vector<std::string> free;          // the pool less the copies: what a guard may still take
    int II = 1, S = 0, u = 1, seq = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        buildEdges(L, renameable);
        seq = criticalPath(L);
        int lateMax = 0;
        for (std::size_t o = 0; o < L.nodes.size(); o++) lateMax = std::max(lateMax, L.nodes[o].late);
        // Each II in turn: a schedule, then the copies its spans need; an II whose copies the pool cannot
        // give is passed over for the next, and only past the bound are the units without them left unrenamed.
        bool any = false, done = false;
        std::vector<std::string> failed;
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
            for (std::map<std::string, std::size_t>::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k++) {
                Unit &un = units[it->second];
                const std::uint64_t bit = bitOf(it->first);
                if (!renameable.count(it->first) || !L.written.count(it->first)) continue;
                int start = 1 << 30, end = 0;
                for (std::size_t o = 0; o < L.nodes.size(); o++) {
                    if (L.nodes[o].writes & bit) { start = std::min(start, t[o] + L.nodes[o].lat); end = std::max(end, t[o] + L.nodes[o].lat); }
                    if (L.nodes[o].reads & bit) end = std::max(end, t[o] + L.nodes[o].late + (firstWrite[k] >= static_cast<int>(o) ? II : 0));
                }
                int q = std::max((end - start) / II + 1, minQ.count(it->first) ? minQ[it->first] : 1);
                un.q = std::max(un.q, pow2At(q));
                if (firstWrite[k] < (1 << 30)) for (std::size_t o = 0; o <= static_cast<std::size_t>(firstWrite[k]); o++) if (L.nodes[o].reads & bit) un.carried = true;
                if ((L.liveExit & bit) || it->first == L.iv || it->first == L.bound || L.pinned(it->first)) un.exits = true;
            }
            for (std::size_t x = 0; x < units.size(); x++) qMax = std::max(qMax, units[x].q);
            u = pow2At(std::max((6 + II - 1) / II, qMax));
            // The copies: the register itself, then q-1 more of its side - a pair's two halves aligned.
            free = pool;
            for (std::size_t x = 0; x < units.size(); x++) {
                Unit &un = units[x];
                un.copies.assign(un.regs.size(), std::vector<std::string>());
                for (std::size_t h = 0; h < un.regs.size(); h++) un.copies[h].push_back(un.regs[h]);
                for (int c = 1; c < un.q; c++) {
                    bool got = false;
                    for (std::size_t f = 0; f < free.size() && !got; f++) {
                        if (sideOf(free[f]) != sideOf(un.regs[0])) continue;
                        if (un.regs.size() == 1) { un.copies[0].push_back(free[f]); free.erase(free.begin() + static_cast<long>(f)); got = true; break; }
                        int num = std::atoi(free[f].c_str() + 1);
                        if (num % 2) continue;
                        std::string hi = std::string(1, free[f][0]) + std::to_string(num + 1);
                        std::vector<std::string>::iterator ih = std::find(free.begin(), free.end(), hi);
                        if (ih == free.end()) continue;
                        un.copies[0].push_back(free[f]); un.copies[1].push_back(hi);
                        free.erase(ih); free.erase(free.begin() + static_cast<long>(f)); got = true;
                    }
                    if (!got) { failed.push_back(un.regs[0]); break; }
                }
            }
            if (failed.empty()) { done = true; break; }
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: S=%d, no %d copies of %s - trying II+1\n", II, S, units[unitOf[failed[0]]].q, failed[0].c_str());
        }
        if (!any) { why = "no schedule under four fifths of the " + std::to_string(seq) + "-cycle iteration"; return false; }
        if (done) break;
        for (std::size_t f = 0; f < failed.size(); f++) {
            const Unit &un = units[unitOf[failed[f]]];
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: no %d copies of %s\n", II, un.q, failed[f].c_str());
            for (std::size_t h = 0; h < un.regs.size(); h++) renameable.erase(un.regs[h]);
        }
        if (attempt == 7) { why = "no registers for the renaming"; return false; }
    }
    // A stepped counter: the kernel's branch, at cycle u*II-6 of a round, reads the test iteration k_b made of the counter
    // one turn on, so the bound it tests is moved back by m = 2u-2-d steps (d the turns between that test and the branch);
    // a round is entered only when its last-started turn exists, and the loop is entered when turns 0..u+S-2 do.
    int m = 0, g = 0;
    if (L.stepped) {
        int tP = 0;
        for (std::size_t o = 0; o < L.nodes.size(); o++) if (L.nodes[o].writes & bitOf(L.P)) tP = t[o];
        auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        int d = floorDiv(u * II - 7 - tP, II);
        while (S - 1 + d < 0) { u *= 2; d = floorDiv(u * II - 7 - tP, II); }   // the first round's branch must see a test
        m = 2 * u - 2 - d; g = u + S - 2;
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: II=%d S=%d u=%d seq=%d ops=%zu%s\n", L.label.c_str(), II, S, u, seq, L.nodes.size(), L.stepped ? (L.wide ? " stepped wide" : " stepped") : "");
    if (L.stepped) { if (!steppedGuard(L, out, moved, xt, xtb, free, m, g, why)) return false; }
    // The guard: X = (n - i [+1] - (S-1)) >> log2 u, the unrolled rounds; none, and the loop is left as written.
    const bool crossBoth = !L.stepped && !L.boundConst && sideOf(L.bound) != sideOf(X) && sideOf(L.iv) != sideOf(X);
    if (L.stepped) { }
    else if (L.boundConst) {
        if (L.boundK >= -32768 && L.boundK <= 32767) out.add("MVK", std::to_string(L.boundK), X);
        else { out.add("MVKL", std::to_string(L.boundK), X); out.add("MVKH", std::to_string(L.boundK), X); }
        out.add("SUB", X, L.iv, X);
    } else if (crossBoth) { out.add("MV", L.bound, X); out.add("SUB", X, L.iv, X); }
    else out.add("SUB", L.bound, L.iv, X);
    if (!L.stepped) {
        if (L.plusOne) out.add("ADD", "1", X, X);
        if (S > 1) { if (S - 1 <= 31) out.add("SUB", X, std::to_string(S - 1), X); else out.add("ADDK", std::to_string(1 - S), X); }
        int log2u = 0;
        while ((1 << log2u) < u) log2u++;
        if (log2u) out.add("SHR", X, std::to_string(log2u), X);
        out.add("CMPGT", "1", X, L.P);
        out.add("B", L.label, "", "", L.P);
        out.add("SUB", X, "1", L.C);
    }
    for (std::size_t x = 0; x < units.size(); x++)
        if (units[x].q > 1 && units[x].carried) for (std::size_t h = 0; h < units[x].regs.size(); h++) out.add("MV", units[x].regs[h], units[x].copies[h][static_cast<std::size_t>(units[x].q - 1)]);
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
                p.push_back(turnOf("B", std::vector<std::string>(1, L.label + "$pipe"), L.stepped ? (L.negated ? "!" + L.P : L.P) : L.C));
                if (!L.stepped) p.push_back(turnOf("ADD", Code::opsOf("-1", L.C, L.C)));
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
        if (units[x].q > 1 && units[x].exits && modq(S - 2, units[x].q) != 0) for (std::size_t h = 0; h < units[x].regs.size(); h++) out.add("MV", units[x].copies[h][static_cast<std::size_t>(modq(S - 2, units[x].q))], units[x].regs[h]);
    if (L.stepped) { if (!steppedTest(L, out, L.bounds[0], L.wide ? L.bounds[1] : "", xt, xtb, why)) return false; }
    else out.same(v, L.cmp);
    out.add("B", L.exitName, "", "", L.negated ? L.P : "!" + L.P);
    return true;
}


// ----- a loop that leaves on a byte it reads: eight turns over one aligned 8-byte block -----

// **A loop whose exit hangs on what it loads** - `while (*p) p++`, strchr, strcmp, a string hash - is unrolled eight
// turns: each turn's arithmetic runs ahead under fresh names and only a value the loop leaves behind is copied back,
// under R, the mask of the turns still alive. No turn the loop would not have run writes anything it can see.
struct Window {
    struct Ptr {
        std::string reg;
        std::map<std::size_t, long> loads;      // body index -> the byte it reads, past the turn's own value of reg
        long offmin = 0, offmax = 0;
        bool cond = false;                      // the first byte read only after an exit: the block is read under G
        std::size_t anchor = 0;
    };
    struct Exit { std::string target, pred; std::uint64_t live = 0; };  // target "" for the fall-through after the back branch
    std::size_t head = 0, back = 0;
    std::string label, backPred, P;             // backPred "" for an unconditional back branch
    std::vector<Line> body;                     // the instructions in order, the back branch left out
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

bool isByteLoad(const std::string &m) { return m == "LDB" || m == "LDBU"; }

// An operand's registers in its spelling's order, and a byte load's address as `byteAddress` reads it, on the entry.
void Src::regsIn(std::size_t i, std::size_t k, std::vector<std::string> &out) const {
    if (!typed(i)) { registersIn(v[i].ops[k], out); return; }
    const MOperand &o = (*s)[i].op[k];
    if (o.kind == MOperand::Register) out.push_back(regName(o.reg));
    if (o.kind == MOperand::Pair) { out.push_back(regName(o.hi)); out.push_back(regName(o.reg)); }
    if (o.kind != MOperand::Memory) return;
    out.push_back(regName(o.reg));
    if (o.index != kNoReg) out.push_back(regName(o.index));
}
bool Src::byteAddress(std::size_t i, std::size_t k, std::string &base, long &off, bool &post) const {
    if (!typed(i)) return c6x::byteAddress(v[i].ops[k], base, off, post);
    const MOperand &o = (*s)[i].op[k];
    post = false; off = 0;
    if (o.kind != MOperand::Memory || o.index != kNoReg) return false;
    base = regName(o.reg);
    if (o.mode == MOperand::Base) return !o.hasOffset;
    if (o.mode == MOperand::PostInc) { post = true; return !o.hasOffset || (!o.scaled && o.value == 1); }
    if (o.mode != MOperand::Plus && o.mode != MOperand::Minus) return false;
    off = static_cast<long>(o.value) * (o.mode == MOperand::Minus ? -1 : 1);
    return o.hasOffset;
}

// Pointer r followed through one turn: registers holding r plus a constant, through copies, constant adds and the
// steps of byte loads; true where r ends the turn one past where it began, with each byte load's offset in loads.
bool trackPointer(const Src &src, const Window &W, const std::string &r, std::map<std::size_t, long> &loads) {
    std::map<std::string, long> sym;
    sym[r] = 0;
    for (std::size_t k = 0; k < W.at.size(); k++) {
        const std::size_t i = W.at[k];
        const std::string m = src.mnem(i);
        if (isBranch(m)) continue;
        std::vector<std::string> reads, writes;
        src.rw(i, reads, writes);
        if (W.cls[k] >= 0) {                    // done only on the way out: what stays on sees none of it
            std::vector<std::string> regs;
            if (isLoad(m)) src.regsIn(i, 0, regs);
            for (std::size_t g = 0; g < regs.size(); g++) if (sym.count(regs[g])) return false;
            continue;
        }
        if (isLoad(m)) {
            std::string base; long off; bool post;
            std::vector<std::string> regs;
            src.regsIn(i, 0, regs);
            bool touches = false;
            for (std::size_t g = 0; g < regs.size(); g++) if (sym.count(regs[g])) touches = true;
            if (touches) {
                if (!isByteLoad(m) || !src.byteAddress(i, 0, base, off, post) || !sym.count(base)) return false;
                loads[k] = sym[base] + off;
                if (post) sym[base] += 1;
            }
            sym.erase(src.op(i, src.ops(i) - 1));
            continue;
        }
        if (writes.empty()) continue;
        const std::string &d = writes[0];
        long v = 0, n = 0; bool known = false;
        if (m == "MV" && src.ops(i) == 2 && sym.count(src.op(i, 0))) { v = sym[src.op(i, 0)]; known = true; }
        else if ((m == "ADD" || m == "SUB") && src.ops(i) == 3) {
            const bool sub = m == "SUB";
            if (src.number(i, 0, n) && sym.count(src.op(i, 1)) && !sub) { v = sym[src.op(i, 1)] + n; known = true; }
            else if (src.number(i, 1, n) && sym.count(src.op(i, 0))) { v = sym[src.op(i, 0)] + (sub ? -1 : 1) * n; known = true; }
        }
        if (known) sym[d] = v; else sym.erase(d);
    }
    return sym.count(r) && sym[r] == 1;
}

bool recogniseWindow(const Src &src, std::size_t back, const std::set<std::string> &labels, const std::set<std::string> &named, Window &W, std::string &why) {
    const std::vector<Line> &v = src.v;
    if (src.mnem(back) != "B" || src.ops(back) == 0 || labels.count(src.op(back, 0)) == 0) { why = "no branch back to a label"; return false; }
    const std::string bpred = src.pred(back);
    W.label = src.op(back, 0); W.backPred = bpred; W.P = bpred.empty() ? "" : bpred[0] == '!' ? bpred.substr(1) : bpred; W.back = back;
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < v.size(); i++) if (src.label(i)) at[src.name(i)] = i;
    bool found = false;
    for (std::size_t i = 0; i < back; i++) if (src.label(i) && src.name(i) == W.label) { W.head = i; found = true; }
    if (!found) { why = "the head is not above"; return false; }
    if (src.naming(W.label) != 1) { why = "the head is entered from elsewhere"; return false; }
    W.liveHead = liveInAt(v, W.head);
    for (std::size_t i = W.head + 1; i < back; i++) {
        if (src.label(i)) { if (!passThroughName(src.name(i), named)) { why = "a label inside"; return false; } continue; }
        if (!src.instr(i)) { why = "a directive inside"; return false; }
        const std::string m = src.mnem(i), pred = src.pred(i);
        if (isBranch(m)) {
            const std::string to = src.ops(i) ? src.op(i, 0) : std::string();
            if (m != "B" || pred.empty() || to.empty() || !at.count(to) || to == W.label) { why = "a branch inside that is not a way out"; return false; }
            Window::Exit e; e.target = to; e.pred = pred;
            if (!W.backPred.empty() && back + 1 < v.size() && src.label(back + 1) && src.name(back + 1) == e.target) e.target = "";
            e.live = liveInAt(v, at[to]);
            W.cls.push_back(static_cast<int>(W.exits.size()));
            W.exits.push_back(e);
            W.liveOut |= e.live;
            W.body.push_back(v[i]);
            W.at.push_back(i);
            continue;
        }
        if (isStore(m)) { why = "a store, which a byte read early might meet"; return false; }
        if (m == "ADDK" || m == "MVKH" || m == "MVKL") { why = "an instruction that reads its destination"; return false; }
        for (std::size_t o = 0; o < src.ops(i); o++) if (src.pair(i, o)) { why = "a register pair"; return false; }
        std::vector<std::string> reads, writes;
        src.rw(i, reads, writes);
        if (has(writes, "A15") || has(writes, "B15") || has(writes, "B3")) { why = "the stack pointer or return address is written"; return false; }
        if (!isLoad(m) && (src.units(i) == 0 || writes.size() != 1)) { why = "an instruction of no known form: " + m; return false; }
        W.cls.push_back(kPlain);
        W.body.push_back(v[i]);
        W.at.push_back(i);
    }
    if (!W.backPred.empty()) {
        Window::Exit e; e.pred = W.backPred[0] == '!' ? W.P : "!" + W.P;
        e.live = back + 1 < v.size() ? liveInAt(v, back + 1) : ~0ull;
        W.exits.push_back(e);
        W.liveOut |= e.live;
    }
    if (W.exits.empty()) { why = "no way out"; return false; }
    if (W.body.empty() || W.body.size() > 20) { why = "too small or too large to unroll"; return false; }
    // An instruction under a predicate belongs to the next branch, under the same one: done on the way out there,
    // or, before the back branch, only where the turn goes on. Its predicate must not change on the way.
    const std::size_t n = W.at.size();
    for (std::size_t k = 0; k < n; k++) {
        const std::string pk = src.pred(W.at[k]);
        if (pk.empty() || isBranch(src.mnem(W.at[k]))) continue;
        const std::string q = pk[0] == '!' ? pk.substr(1) : pk;
        std::size_t j = k + 1;
        for (; j < n; j++) {
            if (isBranch(src.mnem(W.at[j]))) break;
            std::vector<std::string> reads, writes;
            src.rw(W.at[j], reads, writes);
            if (has(writes, q)) { why = "a predicate written under its own instructions"; return false; }
        }
        if (j < n && src.pred(W.at[j]) == pk) W.cls[k] = W.cls[j];
        else if (j == n && pk == W.backPred) W.cls[k] = kCont;
        else { why = "an instruction under a predicate no branch after it takes"; return false; }
    }
    // The pointers stepped by one a turn, followed through copies and constant offsets to the byte loads that read them.
    std::set<std::string> tried;
    for (std::size_t k = 0; k < n; k++) {
        std::vector<std::string> reads, writes;
        src.rw(W.at[k], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (!(W.liveHead & bitOf(reads[r])) || !tried.insert(reads[r]).second) continue;
            Window::Ptr p;
            p.reg = reads[r];
            if (!trackPointer(src, W, p.reg, p.loads) || p.loads.empty()) continue;
            bool first = true, exited = false;
            for (std::size_t b = 0; b < n; b++) {
                if (isBranch(src.mnem(W.at[b]))) { exited = true; continue; }
                std::map<std::size_t, long>::const_iterator it = p.loads.find(b);
                if (it == p.loads.end()) continue;
                if (src.side(W.at[b], src.ops(W.at[b]) - 1) != sideOf(p.reg)) { first = false; p.loads.clear(); break; }
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
        std::vector<std::string> reads, writes;
        src.rw(W.at[k], reads, writes);
        bool stepped = false;
        for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].loads.count(k)) stepped = true;
        if (!stepped && writes.size() != 1) { why = "a load that steps its own register"; return false; }
    }
    why.clear();
    return true;
}

// The window's code before its registers are given: a virtual register is `%N`, a physical one its own name.
struct WinCode {
    struct Op { std::string mnem, pred; std::vector<std::string> ops; };
    std::vector<Op> ops;
    std::vector<char> side;
    std::set<int> boolean;                  // made by a compare: 0 or 1
    std::map<std::string, std::pair<std::string, long> > affine;   // a register as another plus a constant
    std::string fresh(char s) { side.push_back(s); return "%" + std::to_string(side.size() - 1); }
    void put(const std::string &m, const std::vector<std::string> &o, const std::string &pred = "") { Op x; x.mnem = m; x.ops = o; x.pred = pred; ops.push_back(x); }
    bool isBool(const std::string &r) const { return r[0] == '%' && boolean.count(std::atoi(r.c_str() + 1)); }
    std::pair<std::string, long> base(const std::string &r) const {
        std::map<std::string, std::pair<std::string, long> >::const_iterator it = affine.find(r);
        return it == affine.end() ? std::make_pair(r, 0L) : it->second;
    }
};

char sideOfAny(const WinCode &c, const std::string &r) { return r[0] == '%' ? c.side[static_cast<std::size_t>(std::atoi(r.c_str() + 1))] : sideOf(r); }

// Every register of an operand renamed by cur, those not in it left as they are.
std::string renamedBy(const std::string &op, const std::map<std::string, std::string> &cur) {
    std::vector<std::string> regs;
    registersIn(op, regs);
    std::string s = op;
    for (std::size_t r = 0; r < regs.size(); r++) {
        std::map<std::string, std::string>::const_iterator it = cur.find(regs[r]);
        if (it != cur.end() && it->second != regs[r]) s = renamed(s, regs[r], it->second);
    }
    return s;
}

std::string byteOperand(const std::string &base, long off) {
    if (off == 0) return "*" + base;
    return std::string("*") + (off > 0 ? "+" : "-") + base + "(" + std::to_string(off < 0 ? -off : off) + ")";
}

// The window's code with registers given: an instruction whose result nothing reads is dropped first, then each
// virtual register takes a physical one of its side in the order the code reads, freed after its last reader.
bool giveRegisters(WinCode &c, std::vector<std::string> pool[2], std::vector<Line> &code, std::string &why) {
    auto each = [&](const std::string &op, const std::function<void(int)> &f) {
        for (std::size_t i = 0; i < op.size(); i++)
            if (op[i] == '%') { std::size_t j = i + 1; while (j < op.size() && std::isdigit(static_cast<unsigned char>(op[j]))) j++; f(std::atoi(op.c_str() + i + 1)); i = j - 1; }
    };
    for (bool changed = true; changed;) {
        changed = false;
        std::set<int> read;
        for (std::size_t i = 0; i < c.ops.size(); i++)
            for (std::size_t o = 0; o < c.ops[i].ops.size(); o++)
                if (o + 1 < c.ops[i].ops.size() || c.ops[i].ops[o][0] == '*') each(c.ops[i].ops[o], [&](int n) { read.insert(n); });
        for (std::size_t i = c.ops.size(); i-- > 0;) {
            const std::string &d = c.ops[i].ops.back();
            if (d[0] == '%' && d.find_first_not_of("0123456789", 1) == std::string::npos && !read.count(std::atoi(d.c_str() + 1))) {
                c.ops.erase(c.ops.begin() + static_cast<long>(i));
                changed = true;
            }
        }
    }
    // Registers given in the order the code reads, each freed after its last reader, oldest freed first.
    std::vector<int> last(c.side.size(), -1);
    for (std::size_t i = 0; i < c.ops.size(); i++) for (std::size_t o = 0; o < c.ops[i].ops.size(); o++) each(c.ops[i].ops[o], [&](int n) { last[static_cast<std::size_t>(n)] = static_cast<int>(i); });
    std::vector<std::string> phys(c.side.size());
    std::vector<bool> given(c.side.size(), false);
    std::deque<std::string> freeList[2];
    for (int s = 0; s < 2; s++) for (std::size_t k = 0; k < pool[s].size(); k++) freeList[s].push_back(pool[s][k]);
    std::vector<int> live;
    for (std::size_t i = 0; i < c.ops.size(); i++) {
        for (std::size_t a = 0; a < live.size();) {
            if (last[static_cast<std::size_t>(live[a])] < static_cast<int>(i)) { const std::string &r = phys[static_cast<std::size_t>(live[a])]; freeList[sideIndex(r)].push_back(r); live.erase(live.begin() + static_cast<long>(a)); }
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
    for (std::size_t i = 0; i < c.ops.size(); i++) {
        std::vector<std::string> ops = c.ops[i].ops;
        for (std::size_t o = 0; o < ops.size(); o++) {
            std::string s;
            for (std::size_t j = 0; j < ops[o].size(); j++) {
                if (ops[o][j] != '%') { s += ops[o][j]; continue; }
                std::size_t e = j + 1;
                while (e < ops[o].size() && std::isdigit(static_cast<unsigned char>(ops[o][e]))) e++;
                s += phys[static_cast<std::size_t>(std::atoi(ops[o].c_str() + j + 1))];
                j = e - 1;
            }
            ops[o] = s;
        }
        Line l = rebuilt(c.ops[i].mnem, ops, c.ops[i].pred);
        if (!isLoad(l.mnem) && !legalForm(l)) { why = "no legal form for" + l.raw; return false; }
        code.push_back(l);
    }
    return true;
}

// ----- the closed form: where all a loop leaves behind is its registers plus a constant step a turn -----

// A value within one turn as a register's value at the turn's start plus a constant; base "" for a constant alone.
struct Aff { std::string base; long c = 0; bool ok = false; };

// **One turn read as affine values**: each value the loop leaves - at each exit where it is live, and at the head - must
// be the start value of a register that steps by -1, 0 or 1 a turn, plus a constant; then the turn a window stops in
// says them all, and no turn writes anything. at[j] is exit j's state, step a register's per-turn step.
bool affineTurn(const Window &W, const std::set<std::string> &committed, std::vector<std::map<std::string, Aff> > &at, std::map<std::string, long> &step) {
    std::map<std::string, Aff> st, away;
    auto get = [&](const std::string &r) { if (away.count(r)) return away[r]; if (st.count(r)) return st[r]; Aff a; a.base = r; a.ok = true; return a; };
    auto value = [&](const Line &l) {
        Aff a;
        if (l.mnem == "MV" && l.ops.size() == 2) return get(l.ops[0]);
        if (l.mnem == "MVK" && l.ops.size() == 2 && isNumber(l.ops[0])) { a.ok = true; a.c = std::atol(l.ops[0].c_str()); return a; }
        if ((l.mnem == "ADD" || l.mnem == "SUB") && l.ops.size() == 3) {
            int ki = isNumber(l.ops[0]) && l.mnem == "ADD" ? 0 : isNumber(l.ops[1]) ? 1 : -1;
            if (ki >= 0 && !isNumber(l.ops[static_cast<std::size_t>(1 - ki)])) {
                a = get(l.ops[static_cast<std::size_t>(1 - ki)]);
                a.c += std::atol(l.ops[static_cast<std::size_t>(ki)].c_str()) * (l.mnem == "SUB" ? -1 : 1);
                return a;
            }
        }
        return a;
    };
    at.assign(W.exits.size(), std::map<std::string, Aff>());
    bool finalTaken = false;
    for (std::size_t i = 0; i < W.body.size(); i++) {
        const Line &l = W.body[i];
        if (isBranch(l.mnem)) {
            for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) at[static_cast<std::size_t>(W.cls[i])][*r] = get(*r);
            away.clear();
            continue;
        }
        if (W.cls[i] == kCont && !finalTaken && !W.backPred.empty()) {
            for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) at.back()[*r] = get(*r);
            finalTaken = true;
        }
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (writes.empty()) continue;
        const Aff a = isLoad(l.mnem) ? Aff() : value(l);
        if (W.cls[i] >= 0) away[writes[0]] = a; else st[writes[0]] = a;
    }
    if (!W.backPred.empty() && !finalTaken)
        for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) at.back()[*r] = get(*r);
    // The steps: a register live at the head ends the turn as itself plus its step.
    for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) {
        if (!(W.liveHead & bitOf(*r))) continue;
        const Aff e = get(*r);
        if (!e.ok || e.base != *r || e.c < -1 || e.c > 1) return false;
        step[*r] = e.c;
    }
    // Every value an exit needs, over a register of known step or none; a register the loop never writes steps by 0.
    for (std::size_t j = 0; j < W.exits.size(); j++)
        for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) {
            if (!(W.exits[j].live & bitOf(*r))) continue;
            const Aff &a = at[j][*r];
            if (!a.ok) return false;
            if (!a.base.empty() && !step.count(a.base)) { if (committed.count(a.base)) return false; step[a.base] = 0; }
        }
    return true;
}

bool emitClosed(Window &W, const std::set<std::string> &committed, std::vector<std::string> pool[2], std::vector<Line> &out, std::string &why) {
    std::vector<std::map<std::string, Aff> > at;
    std::map<std::string, long> step;
    const std::size_t E = W.exits.size();
    if (E > 2) { why = "more than two ways out of a turn"; return false; }
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].offmin != W.ptrs[p].offmax) { why = "bytes of a turn at two places"; return false; }
    for (std::size_t k = 0; k < W.body.size(); k++) {
        bool stepped = false;
        for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].loads.count(k)) stepped = true;
        if (isLoad(W.body[k].mnem) && !stepped) { why = "a load that cannot be read ahead"; return false; }
    }
    if (!affineTurn(W, committed, at, step)) { why = "a value left behind that is not a step a turn"; return false; }
    for (std::size_t p = 0; p < W.ptrs.size(); p++)
        if (W.ptrs[p].cond) {
            int before = 0;
            for (std::size_t i = 0; i < W.ptrs[p].anchor; i++) if (isBranch(W.body[i].mnem)) before++;
            if (before != 1) { why = "a block read behind more than one exit"; return false; }
        }
    auto isFree = [&](int s, int k) { return std::find(pool[s].begin(), pool[s].end(), std::string(s ? "B" : "A") + std::to_string(k)) != pool[s].end(); };
    auto takePred = [&]() -> std::string {
        for (int s = 0; s < 2; s++) for (int k = 0; k < 3; k++) if (isFree(s, k)) { std::string r = std::string(s ? "B" : "A") + std::to_string(k); pool[s].erase(std::find(pool[s].begin(), pool[s].end(), r)); return r; }
        return "";
    };
    const std::string Pc = takePred(), Pj = E > 1 ? takePred() : "";
    std::vector<std::string> G(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond) G[p] = takePred();
    bool ok = !Pc.empty() && (E == 1 || !Pj.empty());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond && G[p].empty()) ok = false;
    if (!ok) { why = "no condition registers for the closed form"; return false; }
    WinCode c;
    std::map<std::string, std::string> cur;
    // Each pointer's block and offset in it; the turns run to the end of the nearest block, w = 8 - offset.
    std::vector<std::string> B(W.ptrs.size()), O(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        const char s = sideOf(W.ptrs[p].reg);
        std::string u = W.ptrs[p].reg;
        if (W.ptrs[p].offmin != 0) { u = c.fresh(s); c.put("ADD", { std::to_string(W.ptrs[p].offmin), W.ptrs[p].reg, u }); }
        O[p] = c.fresh(s); c.put("AND", { "7", u, O[p] });
        B[p] = c.fresh(s); c.put("AND", { "-8", u, B[p] });
    }
    std::map<std::string, std::string> loaded;
    auto loadWrapped = [&](std::size_t p) {
        const char s = sideOf(W.ptrs[p].reg);
        std::set<std::string> mn;
        for (std::map<std::size_t, long>::const_iterator it = W.ptrs[p].loads.begin(); it != W.ptrs[p].loads.end(); ++it) mn.insert(W.body[it->first].mnem);
        // With two pointers, the odd turns' bytes through a copy of the block on the other side: both .D units read.
        const char o2 = W.ptrs.size() < 2 ? s : s == 'A' ? 'B' : 'A';
        std::string B2 = B[p], O2 = O[p];
        if (o2 != s) { B2 = c.fresh(o2); O2 = c.fresh(o2); c.put("MV", { B[p], B2 }); c.put("MV", { O[p], O2 }); }
        for (int k = 1; k < 8; k++)
            for (std::set<std::string>::const_iterator m = mn.begin(); m != mn.end(); ++m) {
                const bool odd = k % 2 != 0;
                std::string t = c.fresh(odd ? o2 : s), x = c.fresh(odd ? o2 : s);
                c.put("ADD", { std::to_string(k), odd ? O2 : O[p], t });
                c.put("AND", { "7", t, t });
                c.put(*m, { "*+" + (odd ? B2 : B[p]) + "[" + t + "]", x }, G[p]);
                loaded[std::to_string(p) + "/" + std::to_string(k) + "/" + *m] = x;
            }
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (!W.ptrs[p].cond) loadWrapped(p);
    auto valueOf = [&](const std::string &r) { return cur.count(r) ? cur[r] : r; };
    // Whether a turn stops at a test, as 0 or 1, from Q and the sense it leaves in: a compare's own result where it can be.
    auto stopOf = [&](const std::string &q, bool leavesWhenNonzero) {
        if (c.isBool(q) && leavesWhenNonzero) return q;
        std::string x = c.fresh(sideOfAny(c, q));
        if (c.isBool(q)) c.put("XOR", { "1", q, x });
        else if (leavesWhenNonzero) c.put("CMPLTU", { "0", q, x });
        else c.put("CMPEQ", { "0", q, x });
        c.boolean.insert(std::atoi(x.c_str() + 1));
        return x;
    };
    std::vector<std::string> dead;                      // d_p: 1 where some test up to place p stopped
    std::string d;
    auto pass = [&](int j, int k) {
        const std::string &pr = W.exits[static_cast<std::size_t>(j)].pred;
        const std::string q = pr[0] == '!' ? pr.substr(1) : pr;
        const std::string s = stopOf(valueOf(q), pr[0] != '!');
        if (d.empty()) d = s;
        else { std::string n = c.fresh(sideOfAny(c, d)); c.put("OR", { d, s, n }); d = n; }
        dead.push_back(d);
        if (k == 0 && j == 0)
            for (std::size_t p = 0; p < W.ptrs.size(); p++)
                if (W.ptrs[p].cond) { c.put("CMPEQ", { "0", s, G[p] }); loadWrapped(p); }
    };
    for (int k = 0; k < 8; k++) {
        for (std::size_t i = 0; i < W.body.size(); i++) {
            const Line &l = W.body[i];
            if (isBranch(l.mnem)) { pass(W.cls[i], k); continue; }
            if (W.cls[i] >= 0) continue;            // done on the way out: its value is read from the closed form
            const std::string &dst = l.ops.back();
            std::size_t ptr = W.ptrs.size();
            for (std::size_t p = 0; p < W.ptrs.size() && ptr == W.ptrs.size(); p++) if (W.ptrs[p].loads.count(i)) ptr = p;
            if (ptr < W.ptrs.size()) {
                std::string base; long off = 0; bool post = false;
                byteAddress(l.ops[0], base, off, post);
                const std::string key = std::to_string(ptr) + "/" + std::to_string(k) + "/" + l.mnem;
                if (!loaded.count(key)) {
                    std::string x = c.fresh(sideOf(dst));
                    c.put(l.mnem, { byteOperand(valueOf(base), post ? 0 : off), x }, W.ptrs[ptr].cond ? G[ptr] : "");
                    loaded[key] = x;
                }
                cur[dst] = loaded[key];
                if (post) { std::string nv = c.fresh(sideOf(base)); c.put("ADD", { "1", valueOf(base), nv }); cur[base] = nv; }
                continue;
            }
            if (l.mnem == "MV" && l.ops.size() == 2 && sideOf(l.ops[0]) && sideOfAny(c, valueOf(l.ops[0])) == sideOf(dst)) { cur[dst] = valueOf(l.ops[0]); continue; }
            std::vector<std::string> ops;
            for (std::size_t o = 0; o + 1 < l.ops.size(); o++) ops.push_back(renamedBy(l.ops[o], cur));
            std::string x = c.fresh(sideOf(dst));
            ops.push_back(x);
            c.put(l.mnem, ops);
            if (startsWith(l.mnem, "CMP")) c.boolean.insert(std::atoi(x.c_str() + 1));
            cur[dst] = x;
        }
        if (!W.backPred.empty()) pass(static_cast<int>(E) - 1, k);
    }
    // t, the places passed: their count less the sum of the d_p, no more than the window's w*E places.
    std::vector<std::string> sum = dead;
    while (sum.size() > 1) {
        std::vector<std::string> next;
        for (std::size_t i = 0; i + 1 < sum.size(); i += 2) { std::string n = c.fresh(sideOfAny(c, sum[i])); c.put("ADD", { sum[i], sum[i + 1], n }); next.push_back(n); }
        if (sum.size() % 2) next.push_back(sum.back());
        sum.swap(next);
    }
    std::string passed = c.fresh('A');
    if (dead.size() <= 15) c.put("SUB", { std::to_string(dead.size()), sum[0], passed });
    else { std::string n = c.fresh('A'); c.put("MVK", { std::to_string(dead.size()), n }); c.put("SUB", { n, sum[0], passed }); }
    // The window's bound, the least of the pointers' (8 - offset) * E, and t no more than it: t = b + min(t - b, 0).
    std::string bound;
    auto least = [&](const std::string &x, const std::string &y) {
        std::string d = c.fresh('A'), m = c.fresh('A'), n = c.fresh('A');
        c.put("SUB", { x, y, d }); c.put("SHR", { d, "31", m }); c.put("AND", { d, m, m }); c.put("ADD", { y, m, n });
        return n;
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        std::string w = c.fresh('A');
        c.put("SUB", { "8", O[p], w });
        if (E > 1) { std::string w2 = c.fresh('A'); c.put("ADD", { w, w, w2 }); w = w2; }
        bound = bound.empty() ? w : least(w, bound);
    }
    const std::string t = least(passed, bound);
    c.put("CMPEQ", { t, bound, Pc });
    std::string K = t;
    if (E > 1) { K = c.fresh('A'); c.put("SHR", { t, "1", K }); c.put("AND", { "1", t, Pj }); }
    // Each value left behind, made from K: the exit-0 one first, the exit-1 one under Pj, the head's under Pc.
    auto make = [&](const Aff &v, char s) {
        std::string x = c.fresh(s);
        if (v.base.empty()) { c.put("MVK", { std::to_string(v.c), x }); return x; }
        const long sb = step.count(v.base) ? step[v.base] : 0;
        std::string y = v.base;
        if (sb == 1) { std::string z = c.fresh(s); c.put("ADD", { K, v.base, z }); y = z; }
        else if (sb == -1) { std::string z = c.fresh(s); c.put("SUB", { v.base, K, z }); y = z; }
        if (v.c >= -16 && v.c <= 15) c.put("ADD", { std::to_string(v.c), y, x });
        else { std::string k2 = c.fresh(s); c.put("MVK", { std::to_string(v.c), k2 }); c.put("ADD", { y, k2, x }); }
        return x;
    };
    std::vector<std::pair<std::string, std::pair<std::string, std::string> > > moves;   // reg, value, predicate
    for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) {
        const char s = sideOf(*r);
        std::vector<std::string> xs(E);
        for (std::size_t j = 0; j < E; j++) if (W.exits[j].live & bitOf(*r)) xs[j] = make(at[j][*r], s);
        std::string head;
        bool same = true;                               // the head's value is the exits' own: one move does
        for (std::size_t j = 0; j < E; j++) if (!(W.exits[j].live & bitOf(*r)) || at[j][*r].base != *r || at[j][*r].c != 0) same = false;
        if ((W.liveHead & bitOf(*r)) && !same) { Aff h; h.base = *r; h.ok = true; head = make(h, s); }
        if (!xs[0].empty()) moves.push_back(std::make_pair(*r, std::make_pair(xs[0], std::string())));
        if (E > 1 && !xs[1].empty()) moves.push_back(std::make_pair(*r, std::make_pair(xs[1], xs[0].empty() ? std::string() : Pj)));
        if (!head.empty()) moves.push_back(std::make_pair(*r, std::make_pair(head, Pc)));
    }
    for (std::size_t m = 0; m < moves.size(); m++) c.put("MV", { moves[m].second.first, moves[m].first }, moves[m].second.second);
    std::vector<Line> code;
    if (!giveRegisters(c, pool, code, why)) return false;
    code.push_back(rebuilt("B", std::vector<std::string>(1, W.label), Pc));
    if (E == 1 || W.exits[0].target == W.exits[1].target) { if (!W.exits[0].target.empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[0].target))); }
    else {
        if (!W.exits[1].target.empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[1].target), Pj));
        else { code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[0].target.empty() ? W.label : W.exits[0].target), "!" + Pj)); }
        if (!W.exits[1].target.empty() && !W.exits[0].target.empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[0].target)));
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: window, closed form, %zu pointers, %zu instructions for eight turns\n", W.label.c_str(), W.ptrs.size(), code.size());
    out.swap(code);
    return true;
}

bool emitWindow(const std::vector<Line> &v, Window &W, std::vector<Line> &out, std::string &why) {
    (void)v;
    // What the window may not take: every register live at the head or an exit, and the fixed ones.
    const std::uint64_t fixed = W.liveHead | W.liveOut | bitOf("A15") | bitOf("B15") | bitOf("B14") | bitOf("B3");
    std::set<std::string> written;
    for (std::size_t k = 0; k < W.body.size(); k++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(W.body[k], reads, writes);
        for (std::size_t w = 0; w < writes.size(); w++) written.insert(writes[w]);
    }
    // The registers whose value the loop leaves behind: written in it and live at the head or an exit.
    std::set<std::string> committed;
    for (std::set<std::string>::const_iterator it = written.begin(); it != written.end(); ++it)
        if ((W.liveHead | W.liveOut) & bitOf(*it)) committed.insert(*it);
    std::vector<std::string> pool[2];
    for (int s = 0; s < 2; s++)
        for (int k = 0; k < 32; k++) {
            if (k >= 10 && k <= 15) continue;
            std::string r = std::string(s ? "B" : "A") + std::to_string(k);
            if (!(fixed & bitOf(r)) && !committed.count(r)) pool[s].push_back(r);
        }
    {
        std::vector<std::string> spare[2] = { pool[0], pool[1] };
        std::string cwhy;
        if (!std::getenv("CPP11_NOCLOSED") && emitClosed(W, committed, spare, out, cwhy)) return true;
        if (tracing() > 1) std::fprintf(stderr, "pipe %s: no closed form: %s\n", W.label.c_str(), cwhy.c_str());
    }
    // The exits by where they go; more than one place, and a selector says which was taken.
    std::vector<std::string> targets;
    for (std::size_t j = 0; j < W.exits.size(); j++) if (!has(targets, W.exits[j].target)) targets.push_back(W.exits[j].target);
    const bool multi = targets.size() > 1;
    bool needK = false, needZ = multi;
    for (std::size_t k = 0; k < W.body.size(); k++) {
        if (W.cls[k] == kCont) needK = true;
        if (W.cls[k] >= 0 && !isBranch(W.body[k].mnem)) needZ = true;
    }
    // The predicates: R, K and Z on one side, beside the arithmetic that makes them; Cr and the anchors' G anywhere.
    auto isFree = [&](int s, int k) { return std::find(pool[s].begin(), pool[s].end(), std::string(s ? "B" : "A") + std::to_string(k)) != pool[s].end(); };
    auto take = [&](int s, int k) { std::string r = std::string(s ? "B" : "A") + std::to_string(k); pool[s].erase(std::find(pool[s].begin(), pool[s].end(), r)); return r; };
    auto takePred = [&](int s) -> std::string { for (int k = 0; k < 3; k++) if (isFree(s, k)) return take(s, k); return ""; };
    auto preds = [&](int s) { int n = 0; for (int k = 0; k < 3; k++) if (isFree(s, k)) n++; return n; };
    const int cs = preds(0) >= preds(1) ? 0 : 1;
    const char ctl = cs ? 'B' : 'A';
    const std::string R = takePred(cs), Z = needZ ? takePred(cs) : "";
    std::vector<std::string> K;
    if (needK) { K.push_back(takePred(cs)); if (preds(cs) > 0 && preds(1 - cs) > 0) K.push_back(takePred(cs)); }
    std::string Cr = takePred(1 - cs); if (Cr.empty()) Cr = takePred(cs);
    std::vector<std::string> G(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond) { G[p] = takePred(1 - cs); if (G[p].empty()) G[p] = takePred(cs); }
    bool predsOk = !R.empty() && (!needZ || !Z.empty()) && (!needK || !K[0].empty()) && !Cr.empty();
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond && G[p].empty()) predsOk = false;
    if (!predsOk) { why = "no condition registers for the turns"; return false; }
    std::string Sel;
    if (multi) { if (pool[cs].size() > 4) { Sel = pool[cs].back(); pool[cs].pop_back(); } else { why = "no register for the way out"; return false; } }
    WinCode c;
    std::map<std::string, std::string> cur;
    // Setup: each pointer's block and offset in it, and R = 0xFF >> (offset + span) for each, or-ed with one.
    std::vector<std::string> B(W.ptrs.size()), O(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        const Window::Ptr &P = W.ptrs[p];
        const char s = sideOf(P.reg);
        std::string u = P.reg;
        if (P.offmin != 0) { u = c.fresh(s); c.put("ADD", { std::to_string(P.offmin), P.reg, u }); }
        O[p] = c.fresh(s); c.put("AND", { "7", u, O[p] });
        B[p] = c.fresh(s); c.put("AND", { "-8", u, B[p] });
        std::string sh = O[p];
        if (P.offmax != P.offmin) { sh = c.fresh(s); c.put("ADD", { std::to_string(P.offmax - P.offmin), O[p], sh }); }
        std::string ff = c.fresh(s), m = c.fresh(s);
        c.put("MVK", { "255", ff });
        c.put("SHRU", { ff, sh, m });
        if (p == 0) c.put("MV", { m, R }); else c.put("AND", { R, m, R });
    }
    c.put("OR", { "1", R, R });                     // the first turn is always the loop's own
    // A committed register is read from a copy, its own name only ever written by the turns.
    for (std::set<std::string>::const_iterator it = committed.begin(); it != committed.end(); ++it) {
        std::string x = c.fresh(sideOf(*it));
        c.put("MV", { *it, x });
        cur[*it] = x;
    }
    // Which (n, mnem) a turn reaches, n the byte's place past the anchor's in the first turn.
    std::vector<std::vector<std::pair<long, std::string> > > reach(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++)
        for (std::map<std::size_t, long>::const_iterator it = W.ptrs[p].loads.begin(); it != W.ptrs[p].loads.end(); ++it)
            reach[p].push_back(std::make_pair(it->second - W.ptrs[p].offmin, W.body[it->first].mnem));
    // The bytes of the block the later turns read, each where it wraps within the block, under the anchor's G.
    std::map<std::string, std::string> loaded;          // "p/n/mnem" -> register
    auto loadWrapped = [&](std::size_t p) {
        const char s = sideOf(W.ptrs[p].reg);
        for (int k = 1; k < 8; k++)
            for (std::size_t e = 0; e < reach[p].size(); e++) {
                const long n = reach[p][e].first + k;
                const std::string key = std::to_string(p) + "/" + std::to_string(n) + "/" + reach[p][e].second;
                bool atZero = false;
                for (std::size_t f = 0; f < reach[p].size(); f++) if (reach[p][f].first == n && reach[p][f].second == reach[p][e].second) atZero = true;
                if (atZero || loaded.count(key)) continue;
                std::string t = c.fresh(s), x = c.fresh(s);
                c.put("ADD", { std::to_string(n), O[p], t });
                c.put("AND", { "7", t, t });
                c.put(reach[p][e].second, { "*+" + B[p] + "[" + t + "]", x }, G[p]);
                loaded[key] = x;
            }
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (!W.ptrs[p].cond) loadWrapped(p);
    auto commit = [&](const std::string &reg, const std::string &val, const std::string &pr) {
        if (committed.count(reg)) c.put("MV", { val, reg }, pr);
    };
    auto valueOf = [&](const std::string &r) { return cur.count(r) ? cur[r] : r; };
    // The test's mask: Y = -1 where the turn goes on and 0 where it leaves, from Q and the sense it goes on in.
    auto maskOf = [&](const std::string &q, bool goesOnWhenNonzero) {
        std::string y = c.fresh(ctl);
        if (c.isBool(q)) { if (goesOnWhenNonzero) c.put("NEG", { q, y }); else c.put("ADD", { "-1", q, y }); return y; }
        std::string t = c.fresh(ctl);
        c.put("CMPEQ", { "0", q, t });                  // 1 where Q is zero
        if (goesOnWhenNonzero) c.put("ADD", { "-1", t, y }); else c.put("NEG", { t, y });
        return y;
    };
    for (int k = 0; k < 8; k++) {
        bool exited = false;
        std::map<int, std::string> yOf;                 // exit -> its mask this turn
        int zFor = -1;                                  // the exit Z is made for, this turn
        bool kMade = false;
        const std::string Kk = needK ? K[static_cast<std::size_t>(k) % K.size()] : "";
        auto mask = [&](int j) {
            if (!yOf.count(j)) {
                const std::string &pr = W.exits[static_cast<std::size_t>(j)].pred;     // the predicate it leaves on
                const std::string q = pr[0] == '!' ? pr.substr(1) : pr;
                yOf[j] = maskOf(valueOf(q), pr[0] == '!');
            }
            return yOf[j];
        };
        auto leaving = [&](int j) {                     // Z: alive here and leaving by exit j
            if (zFor == j) return;
            std::string n = c.fresh(ctl);
            c.put("NOT", { mask(j), n });
            c.put("AND", { R, n, Z });
            zFor = j;
        };
        auto leaveBy = [&](int j) {                     // the exit itself: Cr, the selector, and R
            const std::string y = mask(j);
            if (multi) {
                leaving(j);
                const std::size_t id = static_cast<std::size_t>(std::find(targets.begin(), targets.end(), W.exits[static_cast<std::size_t>(j)].target) - targets.begin());
                c.put("MVK", { std::to_string(id), Sel }, Z);
            }
            c.put("MV", { y, Cr }, R);
            return y;
        };
        for (std::size_t i = 0; i < W.body.size(); i++) {
            const Line &l = W.body[i];
            const std::string pr = k == 0 && !exited ? "" : R;
            if (isBranch(l.mnem)) {             // an exit inside the turn: R loses the turns from here on where it leaves
                const std::string y = leaveBy(W.cls[i]);
                c.put("AND", { R, y, R });
                exited = true;
                continue;
            }
            const int cl = W.cls[i];
            std::string cpr = pr;
            if (cl == kCont) {
                if (!kMade) { c.put("AND", { R, mask(static_cast<int>(W.exits.size()) - 1), Kk }); kMade = true; }
                cpr = Kk;
            } else if (cl >= 0) { leaving(cl); cpr = Z; }
            const bool away = cl >= 0;          // done only on the way out: what stays on keeps the old value
            std::size_t ptr = W.ptrs.size();
            for (std::size_t p = 0; p < W.ptrs.size() && ptr == W.ptrs.size(); p++) if (W.ptrs[p].loads.count(i)) ptr = p;
            if (ptr < W.ptrs.size()) {
                const Window::Ptr &P = W.ptrs[ptr];
                std::string base; long off = 0; bool post = false;
                byteAddress(l.ops[0], base, off, post);
                const long n = P.loads.at(i) - P.offmin + k;
                const std::string key = std::to_string(ptr) + "/" + std::to_string(n) + "/" + l.mnem;
                if (k == 0 && P.cond && i == P.anchor) { c.put("MV", { R, G[ptr] }); loadWrapped(ptr); }
                if (!loaded.count(key)) {
                    if (k != 0) { why = "a byte no turn's block read was asked for"; return false; }
                    std::string x = c.fresh(sideOf(l.ops.back()));
                    c.put(l.mnem, { byteOperand(valueOf(base), post ? 0 : off), x }, pr);
                    loaded[key] = x;
                }
                const std::string &dst = l.ops.back();
                cur[dst] = loaded[key];
                c.affine.erase(dst);
                commit(dst, loaded[key], cpr);
                if (post) {
                    const std::pair<std::string, long> a = c.base(valueOf(base));
                    std::string nv = c.fresh(sideOf(base));
                    c.put("ADD", { std::to_string(a.second + 1), a.first, nv });
                    c.affine[nv] = std::make_pair(a.first, a.second + 1);
                    cur[base] = nv;
                    commit(base, nv, cpr);
                }
                continue;
            }
            const std::string &dst = l.ops.back();
            if (isLoad(l.mnem)) {               // read where it stands, under the turn's mask
                std::string x = c.fresh(sideOf(dst));
                c.put(l.mnem, { renamedBy(l.ops[0], cur), x }, cpr.empty() ? "" : cpr);
                if (!away) cur[dst] = x;
                commit(dst, x, cpr);
                continue;
            }
            if (l.mnem == "MV" && l.ops.size() == 2 && sideOf(l.ops[0])) {
                const std::string s = valueOf(l.ops[0]);
                if (sideOfAny(c, s) == sideOf(dst)) { if (!away) cur[dst] = s; commit(dst, s, cpr); continue; }
            }
            std::vector<std::string> ops;
            for (std::size_t o = 0; o + 1 < l.ops.size(); o++) ops.push_back(renamedBy(l.ops[o], cur));
            std::string x = c.fresh(sideOf(dst));
            // A constant added to a register that is itself another plus a constant: one add from that other.
            int ki = -1;
            if (l.mnem == "ADD" && ops.size() == 2) ki = isNumber(ops[0]) ? 0 : isNumber(ops[1]) ? 1 : -1;
            if (l.mnem == "SUB" && ops.size() == 2 && isNumber(ops[1])) ki = 1;
            if (ki >= 0 && !isNumber(ops[1 - ki])) {
                const long kc = std::atol(ops[static_cast<std::size_t>(ki)].c_str()) * (l.mnem == "SUB" ? -1 : 1);
                const std::pair<std::string, long> a = c.base(ops[static_cast<std::size_t>(1 - ki)]);
                if (a.second + kc >= -16 && a.second + kc <= 15 && sideOfAny(c, a.first) == sideOf(dst)) {
                    c.put("ADD", { std::to_string(a.second + kc), a.first, x });
                    c.affine[x] = std::make_pair(a.first, a.second + kc);
                } else c.put(l.mnem, { ops[0], ops[1], x });
            } else {
                ops.push_back(x);
                c.put(l.mnem, ops);
            }
            if (startsWith(l.mnem, "CMP")) c.boolean.insert(std::atoi(x.c_str() + 1));
            if (!away) { cur[dst] = x; }
            commit(dst, x, cpr);
        }
        // The turn's end: Y with bit k cleared, so that R keeps only the turns after this one in the block.
        if (!W.backPred.empty()) {
            const std::string y = leaveBy(static_cast<int>(W.exits.size()) - 1);
            std::string x = c.fresh(ctl);
            c.put("CLR", { y, std::to_string(k), std::to_string(k), x });
            c.put("AND", { R, x, R });
        } else {
            c.put("MVK", { "1", Cr }, R);
            c.put("CLR", { R, std::to_string(k), std::to_string(k), R });
        }
    }
    std::vector<Line> code;
    if (!giveRegisters(c, pool, code, why)) return false;
    // Round again while the last turn went on; else to where the turn that stopped was going.
    code.push_back(rebuilt("B", std::vector<std::string>(1, W.label), Cr));
    if (!multi) { if (!targets[0].empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, targets[0]))); }
    else {
        bool falls = has(targets, "");
        std::vector<std::string> branches;
        for (std::size_t t = 0; t < targets.size(); t++) if (!targets[t].empty()) branches.push_back(targets[t]);
        for (std::size_t t = 0; t < branches.size(); t++) {
            const std::size_t id = static_cast<std::size_t>(std::find(targets.begin(), targets.end(), branches[t]) - targets.begin());
            if (!falls && t + 1 == branches.size()) { code.push_back(rebuilt("B", std::vector<std::string>(1, branches[t]))); break; }
            code.push_back(make("CMPEQ", std::to_string(id), Sel, R));
            code.push_back(rebuilt("B", std::vector<std::string>(1, branches[t]), R));
        }
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: window, %zu pointers, %zu instructions for eight turns\n", W.label.c_str(), W.ptrs.size(), code.size());
    out.swap(code);
    return true;
}

bool sameLines(const std::vector<Line> &a, const std::vector<Line> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); k++) if (a[k].raw != b[k].raw) return false;
    return true;
}
bool sameLoop(const Loop &a, const Loop &b) {
    return a.head == b.head && a.back == b.back && a.label == b.label && a.exitName == b.exitName && a.P == b.P && a.C == b.C &&
           a.iv == b.iv && a.bound == b.bound && a.negated == b.negated && a.boundConst == b.boundConst && a.plusOne == b.plusOne &&
           a.boundK == b.boundK && a.cmp == b.cmp && a.bodyAt == b.bodyAt && sameLines(a.body, b.body) && sameLines(a.pre, b.pre) &&
           a.used == b.used && a.written == b.written && a.predicated == b.predicated && a.liveExit == b.liveExit &&
           a.stepped == b.stepped && a.wide == b.wide && a.ivs == b.ivs && a.bounds == b.bounds && a.steps == b.steps && a.testBody == b.testBody;
}
bool sameWindow(const Window &a, const Window &b) {
    if (a.head != b.head || a.back != b.back || a.label != b.label || a.backPred != b.backPred || a.P != b.P || !sameLines(a.body, b.body) ||
        a.cls != b.cls || a.liveHead != b.liveHead || a.liveOut != b.liveOut || a.exits.size() != b.exits.size() || a.ptrs.size() != b.ptrs.size()) return false;
    for (std::size_t k = 0; k < a.exits.size(); k++)
        if (a.exits[k].target != b.exits[k].target || a.exits[k].pred != b.exits[k].pred || a.exits[k].live != b.exits[k].live) return false;
    for (std::size_t k = 0; k < a.ptrs.size(); k++)
        if (a.ptrs[k].reg != b.ptrs[k].reg || a.ptrs[k].loads != b.ptrs[k].loads || a.ptrs[k].offmin != b.ptrs[k].offmin ||
            a.ptrs[k].offmax != b.ptrs[k].offmax || a.ptrs[k].cond != b.ptrs[k].cond || a.ptrs[k].anchor != b.ptrs[k].anchor) return false;
    return true;
}

// CXX1_C6X_MIR=1: each recogniser asked on the stream is asked on the lines too, and a verdict that differs is named.
void checkRecognisers(const Src &src, std::size_t i, const std::set<std::string> &labels, const std::set<std::string> &named, bool counted) {
    static const bool check = std::getenv("CXX1_C6X_MIR") != nullptr;
    if (!check || !src.s) return;
    const Src text = { src.v, nullptr };
    std::string w1, w2;
    if (counted) {
        Loop a, b;
        const bool ra = recognise(src, i, labels, named, a, w1), rb = recognise(text, i, labels, named, b, w2);
        if (ra != rb || w1 != w2 || (ra && !sameLoop(a, b))) std::fprintf(stderr, "c6x-recog: loop %s\n", src.v[i].raw.c_str());
        return;
    }
    Window a, b;
    const bool ra = recogniseWindow(src, i, labels, named, a, w1), rb = recogniseWindow(text, i, labels, named, b, w2);
    if (ra != rb || w1 != w2 || (ra && !sameWindow(a, b))) std::fprintf(stderr, "c6x-recog: window %s\n", src.v[i].raw.c_str());
}

// One back branch tried: the code that replaces lines [from, to) of the text where the pipelining pays.
bool pipelineAt(const Src &src, std::size_t i, const std::set<std::string> &labels, const std::set<std::string> &named,
                std::size_t copied, Code &code, std::size_t &from, std::size_t &to) {
    const std::vector<Line> &v = src.v;
    if (!src.instr(i) || src.mnem(i) != "B" || isCall(v[i], labels)) return false;
    const bool plain = src.pred(i).empty();
    if (plain) {                                // an unconditional branch is a back branch only to a label above
        bool above = false;
        for (std::size_t k = 0; k < i && !above; k++) above = src.label(k) && src.name(k) == src.op(i, 0);
        if (!above) return false;
    }
    if (!plain) checkRecognisers(src, i, labels, named, true);
    Loop L;
    std::string why;
    if (plain || !recognise(src, i, labels, named, L, why)) {
        if (plain) why = "an unconditional back branch";
        // Not counted: a loop that leaves on a byte it reads takes eight turns of one aligned block at a time.
        checkRecognisers(src, i, labels, named, false);
        Window W;
        std::string wwhy;
        std::vector<Line> turns;
        if (recogniseWindow(src, i, labels, named, W, wwhy) && W.head >= copied && emitWindow(v, W, turns, wwhy)) {
            for (std::size_t k = 0; k < turns.size(); k++) code.push_back(turns[k]);
            from = W.head + 1;
            to = W.back + 1;
            return true;
        }
        if (tracing()) std::fprintf(stderr, "pipe %s: %s [window: %s]\n", v[i].ops[0].c_str(), why.c_str(), wwhy.c_str());
        return false;
    }
    if (L.head < copied) return false;
    if (!pipeline(v, L, code, why)) { if (tracing()) std::fprintf(stderr, "pipe %s: %s\n", L.label.c_str(), why.c_str()); return false; }
    from = to = L.head;
    return true;
}

// CXX1_C6X_MIR=1: the emitters' lines read as round 10 read them, and `c6x-emit` names a loop whose entries differ.
void checkEmitted(const Code &code, const Line &at) {
    MStream ref;
    bool same = streamOf(code.lines, ref) && ref.size() == code.m.size();
    for (std::size_t k = 0; same && k < ref.size(); k++) {
        const MInst &a = ref[k], &b = code.m[k];
        same = a.kind == b.kind && a.par == b.par && a.packed == b.packed && a.pred == b.pred && a.predNot == b.predNot && a.unit == b.unit &&
               a.operands == b.operands && a.str() == b.str();
        for (int o = 0; same && o < a.operands; o++) same = a.op[o].kind == b.op[o].kind;
    }
    if (!same) std::fprintf(stderr, "c6x-emit: %s\n", at.raw.c_str());
}

// The names the text defines and the operands it names, with its liveness computed: what every candidate is read against.
void namesOf(std::vector<Line> &v, std::set<std::string> &labels, std::set<std::string> &named) {
    computeLiveness(v);
    if (tracing() > 5) for (std::size_t i = 0; i < v.size(); i++) std::fprintf(stderr, "| %s\n", v[i].raw.c_str());
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
}

}   // namespace

// Every candidate loop of the text, innermost first as the text is walked: rewritten in place where the pipelining pays.
void pipelineLoops(std::vector<Line> &v) {
    std::set<std::string> labels, named;
    namesOf(v, labels, named);
    std::vector<Line> out;
    std::size_t copied = 0, from = 0, to = 0;
    const Src src = { v, nullptr };
    for (std::size_t i = 0; i < v.size(); i++) {
        Code code;
        if (!pipelineAt(src, i, labels, named, copied, code, from, to)) continue;
        for (std::size_t k = copied; k < from; k++) out.push_back(v[k]);
        for (std::size_t k = 0; k < code.lines.size(); k++) out.push_back(code.lines[k]);
        copied = to;
    }
    for (std::size_t k = copied; k < v.size(); k++) out.push_back(v[k]);
    v.swap(out);
}

// **The pipeliner on the stream** (M6c round 2): the same candidates, read through each entry's spelling; a kernel
// is written as entries (`Code`) - `par` its packets, `NOP n` its idle cycles, `packed` what is placed. False: one not held.
bool pipelineLoops(MStream &s) {
    std::vector<Line> v;
    linesOf(s, v);
    std::set<std::string> labels, named;
    namesOf(v, labels, named);
    MStream out;
    const Src src = { v, &s };
    std::size_t copied = 0, from = 0, to = 0;
    for (std::size_t i = 0; i < v.size(); i++) {
        Code code;
        code.from = &s;
        if (!pipelineAt(src, i, labels, named, copied, code, from, to)) continue;
        if (!code.ok) return false;
        if (code.text()) checkEmitted(code, v[i]);
        out.insert(out.end(), s.begin() + static_cast<long>(copied), s.begin() + static_cast<long>(from));
        out.insert(out.end(), code.m.begin(), code.m.end());
        copied = to;
    }
    out.insert(out.end(), s.begin() + static_cast<long>(copied), s.end());
    s.swap(out);
    return true;
}

}   // namespace c6x
