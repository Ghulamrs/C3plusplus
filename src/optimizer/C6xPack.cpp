// **The packetiser on the C6000's MIR** (M6c, doc-cxx3/M6-PLAN.md): a selected function's stream placed in execute
// packets - `par` joins an entry to the cycle before it, a `NOP n` entry is n idle cycles - by C6xSched's list scheduler.

#include "C6xMir.h"
#include "C6xModel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace c6x {

namespace {

// The entry as the model's table reads one: its spelling without the packet mark, parsed (as `legal` asks).
Line lineOf(const MInst &i) {
    MInst x = i;
    x.par = false;
    std::string s = x.str();
    if (!s.empty() && s[s.size() - 1] == '\n') s.erase(s.size() - 1);
    return parse(s);
}

// An entry as the text passes read it: a placed entry a kept line, no instruction to them; any other its spelling.
Line viewOf(const MInst &i) {
    if (!i.packed) return lineOf(i);
    Line l;
    l.raw = i.str();
    l.raw.erase(l.raw.size() - 1);
    l.verbatim = true;
    return l;
}

// What the text pass takes for a label: a Label, or a kept line that starts in the first column; its name ends at `:`.
bool labelLike(const MInst &i, std::string &name) {
    if (i.kind == MInst::Instruction) return false;
    const Line l = lineOf(i);
    if (!isLabel(l)) return false;
    name = labelName(l);
    return true;
}

// The registers an operand names, in its spelling's order (a pair high first, a base before its index), as numbers.
int regsOf(const MOperand &o, int out[2]) {
    if (o.kind == MOperand::Register) { out[0] = o.reg; return 1; }
    if (o.kind == MOperand::Pair) { out[0] = o.hi; out[1] = o.reg; return 2; }
    if (o.kind != MOperand::Memory) return 0;
    out[0] = o.reg;
    if (o.index == kNoReg) return 1;
    out[1] = o.index;
    return 2;
}
std::uint64_t maskOf(const MOperand &o) {
    int r[2];
    std::uint64_t m = 0;
    for (int k = regsOf(o, r); k-- > 0; ) m |= 1ull << r[k];
    return m;
}
int sideOfFirst(const MOperand &o) { int r[2]; return regsOf(o, r) ? sideOfReg(r[0]) : -1; }
bool stepped(const MOperand &o) { return o.kind == MOperand::Memory && o.mode >= MOperand::PreInc; }
bool immediate(const MOperand &o, long &k) { if (o.kind != MOperand::Immediate) return false; k = static_cast<long>(o.value); return true; }

// `readsAndWrites`: the last operand is the destination unless it is memory (a store) or a branch target.
void readsAndWrites(const MInst &i, const std::string &m, std::uint64_t &reads, std::uint64_t &writes) {
    if (i.pred != kNoReg) reads |= 1ull << i.pred;
    if (i.operands == 0) return;
    const MOperand &last = i.op[i.operands - 1];
    const bool hasDest = !isStore(m) && m != "B" && m != "NOP" && last.kind != MOperand::Memory;
    for (int k = 0; k < i.operands - (hasDest ? 1 : 0); k++) reads |= maskOf(i.op[k]);
    for (int k = 0; k < i.operands; k++) if (stepped(i.op[k])) writes |= maskOf(i.op[k]);
    if (!hasDest) return;
    writes |= maskOf(last);
    if (m == "MVKH" || m == "ADDK") reads |= maskOf(last);
}

int crossings(const MInst &i, const std::string &m) {
    if (i.operands == 0 || isStore(m) || isLoad(m) || isBranch(m)) return 0;
    const int dst = sideOfFirst(i.op[i.operands - 1]);
    if (dst < 0) return 0;
    int n = 0;
    for (int o = 0; o + 1 < i.operands; o++) {
        int r[2];
        const int c = regsOf(i.op[o], r);
        bool crossed = false;
        for (int k = 0; k < c; k++) if (sideOfReg(r[k]) != dst) crossed = true;
        if (crossed && (!mayCross(m, static_cast<std::size_t>(o)) || (c > 1 && !pairMayCross(m)))) return 2;
        if (crossed) n++;
    }
    return n;
}

unsigned unitsFor(const MInst &i, const std::string &m) {
    if (isBranch(m) || m == "ADDK") return US;
    if (isLoad(m) || isStore(m)) return UD;
    if (startsWith(m, "MPY")) return UM;
    bool pair = false;
    for (int o = 0; o < i.operands; o++) if (i.op[o].kind == MOperand::Pair) pair = true;
    long k = 0; int at = -1;
    if (i.operands == 3) { if (immediate(i.op[0], k)) at = 0; else if (immediate(i.op[1], k)) at = 1; }
    const bool small = at >= 0 && k >= -16 && k <= 15, ucst = at >= 0 && k >= 0 && k <= 31;
    if (m == "ADD" || m == "AND" || m == "OR" || m == "XOR") {
        if (pair) return UL;
        if (at < 0) return UL | US | UD;
        if (small) return UL | US | (ucst ? UD : 0);
        return ucst && m == "ADD" && crossings(i, m) == 0 ? UD : 0;
    }
    if (m == "SUBAW" || m == "ADDAW") return crossings(i, m) == 0 ? UD : 0;
    if (m == "SUB") {
        if (pair) return UL;
        if (at < 0) {
            const int first = sideOfFirst(i.op[0]), dst = sideOfFirst(i.op[2]);
            return first >= 0 && dst >= 0 && first != dst ? UL | US : UL | US | UD;
        }
        if (at == 0 && small) return UL | US;
        if (at == 1 && ucst && crossings(i, m) == 0) return UD;
        return 0;
    }
    if (m == "NOT") return UL | US | UD;
    if (m == "NEG") return UL | US;
    if (m == "MV") return pair ? UL : UL | US | UD;
    if (m == "ZERO") return UL;
    if (m == "CLR" || m == "EXT" || m == "EXTU" || m == "SHL" || m == "SHR" || m == "SHRU") return US;
    if (m == "MVK") return immediate(i.op[0], k) && k >= -16 && k <= 15 ? UL | US : US;
    if (m == "MVKL" || m == "MVKH") return US;
    if (m == "CMPEQ" || m == "CMPGT" || m == "CMPLT" || m == "CMPGTU" || m == "CMPLTU") return UL;
    if (endsWith(m, "SP") || endsWith(m, "DP")) {
        if (startsWith(m, "CMP") || m == "SPDP") return US;
        if (m == "ADDDP" || m == "SUBDP") return crossings(i, m) > 0 ? US : UL | US;
        if (m == "ADDSP" || m == "SUBSP") return UL | US;
        if (m == "DPSP" || m == "SPTRUNC" || m == "DPTRUNC" || startsWith(m, "INT")) return UL;
    }
    return 0;
}

// `makeNode` on the entry's operands; a branch's target is read as the text pass reads it (`sideOf`, the design's rule).
Node nodeOf(const MInst &i, const std::set<std::string> &labels) {
    const std::string m = i.m.str();
    Node n;
    n.line.instr = true;
    n.line.mnem = m;
    readsAndWrites(i, m, n.reads, n.writes);
    n.lat = delaySlots(m) + 1;
    n.late = lateReads(m);
    n.units = unitsFor(i, m);
    if (isBranch(m)) {
        n.branch = true;
        const std::string target = i.operands ? i.op[0].str() : std::string();
        const char side = sideOf(target);
        n.call = m == "CALLP" || (i.operands != 0 && m == "B" && (side ? target != "B3" : labels.count(target) == 0 &&
                 !startsWith(target, "L$return$") && !startsWith(target, "L.return.")));
        if (n.call) { n.reads |= callReads(); n.writes |= callWrites(); }
        if (m == "CALLP") { n.reads &= ~bitOf("B3"); n.side = 1; return n; }
        if (side) { n.side = 1; if (side == 'A') n.cross = 1; }
        return n;
    }
    if (isLoad(m) || isStore(m)) {
        n.mem = true;
        n.store = isStore(m);
        const MOperand &addr = i.op[n.store ? 1 : 0], &data = i.op[n.store ? 0 : 1];
        n.side = std::max(sideOfFirst(addr), 0);
        n.tpath = std::max(sideOfFirst(data), 0);
        n.memSize = accessSize(m);
        // `frameSlot`: `*A15` itself, or `*+A15(k)` and `*-A15(k)` in bytes.
        n.memFrame = addr.kind == MOperand::Memory && addr.reg == 15 && addr.index == kNoReg && !addr.scaled &&
                     (addr.mode == MOperand::Base ? !addr.hasOffset : (addr.mode == MOperand::Plus || addr.mode == MOperand::Minus) && addr.hasOffset);
        if (n.memFrame) n.memOff = static_cast<long>(addr.value) * (addr.mode == MOperand::Minus ? -1 : 1);
        for (int k = 0; k < i.operands; k++) {
            if (!stepped(i.op[k])) continue;
            if (i.op[k].index == kNoReg) n.stepped = 1ull << i.op[k].reg;
            break;
        }
        return n;
    }
    n.side = i.operands ? std::max(sideOfFirst(i.op[i.operands - 1]), 0) : 0;
    if (crossings(i, m) > 0) n.cross = n.side;
    return n;
}

bool sameFacts(const Node &a, const Node &b) {
    return a.reads == b.reads && a.writes == b.writes && a.stepped == b.stepped && a.lat == b.lat && a.late == b.late &&
           a.units == b.units && a.side == b.side && a.cross == b.cross && a.mem == b.mem && a.store == b.store &&
           a.tpath == b.tpath && a.memOff == b.memOff && a.memSize == b.memSize && a.memFrame == b.memFrame &&
           a.branch == b.branch && a.call == b.call;
}

}   // namespace

// The node of an entry: on its operands where they are modelled, else by its spelling. CXX1_C6X_MIR=1 asks both.
Node makeNode(const MInst &i, const std::set<std::string> &labels) {
    if (!modelled(i)) return c6x::makeNode(lineOf(i), labels);
    Node n = nodeOf(i, labels);
    static const bool check = std::getenv("CXX1_C6X_MIR") != nullptr;
    if (check && !sameFacts(n, c6x::makeNode(lineOf(i), labels))) std::fprintf(stderr, "c6x-node: %s", i.str().c_str());
    return n;
}

// An operand held as text that names a register, a memory address or a number is read by its spelling; nothing else.
bool modelled(const MInst &i) {
    if (i.kind != MInst::Instruction || i.packed) return false;
    const std::string m = i.m.str();
    if (i.operands == 0 && !isBranch(m)) return true;
    if (i.operands < ((isLoad(m) || isStore(m)) ? 2 : 1)) return false;
    for (int k = 0; k < i.operands; k++) {
        if (i.op[k].kind != MOperand::Symbol) continue;
        const std::string s = i.op[k].text.str();
        std::vector<std::string> regs;
        registersIn(s, regs);
        if (s.empty() || !regs.empty() || s[0] == '*' || s.find(':') != std::string::npos || isNumber(s)) return false;
    }
    return true;
}
int crossings(const MInst &i) { return crossings(i, i.m.str()); }
unsigned unitsFor(const MInst &i) { return unitsFor(i, i.m.str()); }
void readsAndWrites(const MInst &i, std::vector<std::string> &reads, std::vector<std::string> &writes) {
    auto names = [](const MOperand &o, std::vector<std::string> &to) { int r[2]; const int n = regsOf(o, r); for (int k = 0; k < n; k++) to.push_back(regName(r[k])); };
    if (i.pred != kNoReg) reads.push_back(regName(i.pred));
    if (i.operands == 0) return;
    const std::string m = i.m.str();
    const MOperand &last = i.op[i.operands - 1];
    const bool hasDest = !isStore(m) && m != "B" && m != "NOP" && last.kind != MOperand::Memory;
    for (int k = 0; k < i.operands - (hasDest ? 1 : 0); k++) names(i.op[k], reads);
    for (int k = 0; k < i.operands; k++) if (stepped(i.op[k])) names(i.op[k], writes);
    if (!hasDest) return;
    names(last, writes);
    if (m == "MVKH" || m == "ADDK") names(last, reads);
}

// What liveness reads of an entry: its operands where `modelled`, else its spelling - the one reader either way.
LiveFacts liveFacts(const MInst &i) {
    if (i.packed) return liveFacts(viewOf(i));   // a kept line, `||` and all, as `linesOf` hands it to the text passes
    if (i.kind != MInst::Instruction || !modelled(i)) return liveFacts(lineOf(i));
    LiveFacts f;
    f.instr = true;
    f.predicated = i.pred != kNoReg;
    f.mnem = i.m.str();
    if (i.operands) f.target = i.op[0].str();
    readsAndWrites(i, f.mnem, f.reads, f.writes);
    return f;
}

void liveness(const MStream &s, std::vector<std::uint64_t> &liveIn, std::vector<std::uint64_t> &liveOut) {
    std::vector<LiveFacts> facts;
    facts.reserve(s.size());
    for (std::size_t k = 0; k < s.size(); k++) facts.push_back(liveFacts(s[k]));
    liveness(facts, liveIn, liveOut);
}

void checkLiveness(const std::vector<Line> &v) {
    MStream s;
    if (!streamOf(v, s)) { std::fprintf(stderr, "c6x-live-unread: a line the stream does not spell\n"); return; }
    std::vector<std::uint64_t> in, out;
    liveness(s, in, out);
    for (std::size_t k = 0; k < v.size(); k++) {
        if (in[k] == v[k].liveIn && out[k] == v[k].liveOut) continue;
        std::fprintf(stderr, "c6x-live: line %zu,%s\n", k, v[k].raw.c_str());
        return;
    }
}

namespace {

// `deadAfter` of C6xSched.cpp on entries, as liveness was computed: reg is written before it is read on the straight
// path from i, or is not live where the block ends; a directive ends the path and answers no.
bool deadAfter(const MStream &s, const std::vector<std::uint64_t> &out, std::size_t i, const std::string &reg) {
    const std::uint64_t bit = bitOf(reg);
    for (std::size_t j = i + 1; j < s.size(); j++) {
        const LiveFacts f = liveFacts(s[j]);
        if (!f.instr) return f.label ? (out[i] & bit) == 0 : false;
        if (f.reads & bit) return false;
        if (f.mnem == "CALLP") {
            if (callReads() & bit) return false;          // a call: its arguments
            return (callWrites() & bit) || reg == "B3" || (out[j] & bit) == 0;
        }
        if (f.mnem == "B") {
            if (sideOf(f.target) ? f.target != "B3" : !startsWith(f.target, "L$") && !startsWith(f.target, "L.")) {
                if (callReads() & bit) return false;      // a call: its arguments
                if (callWrites() & bit) return true;      // clobbered by the callee
            }
            return (out[j] & bit) == 0;
        }
        if ((f.writes & bit) && !f.predicated) return true;
    }
    return (out[i] & bit) == 0;
}

void eraseAt(MStream &s, LiveSpan &live, std::size_t i) {
    s.erase(s.begin() + static_cast<long>(i));
    live.in.erase(live.in.begin() + static_cast<long>(i));
    live.out.erase(live.out.begin() + static_cast<long>(i));
}

}   // namespace

// C6xSched's `removeDead` on entries: a result nothing reads is not made; a load, a store, a pair and a predicated entry stay.
bool removeDead(MStream &s, LiveSpan &live) {
    bool changed = false;
    for (std::size_t i = 0; i < s.size(); i++) {
        const MInst &x = s[i];
        if (x.kind != MInst::Instruction || x.packed || x.pred != kNoReg || x.operands == 0) continue;
        const std::string m = x.m.str();
        if (isBranch(m) || isStore(m) || isLoad(m)) continue;
        std::vector<std::string> reads, writes;
        if (modelled(x)) {
            const MOperand &last = x.op[x.operands - 1];
            if (last.kind == MOperand::Pair || last.kind == MOperand::Memory) continue;
            readsAndWrites(x, reads, writes);
        } else {
            const Line l = lineOf(x);
            if (l.ops.empty() || l.ops.back().find(':') != std::string::npos || l.ops.back()[0] == '*') continue;
            readsAndWrites(l, reads, writes);
        }
        if (writes.size() != 1) continue;
        const std::string &d = writes[0];
        if (d == "A15" || d == "B15" || d == "B14" || d == "B3" || !deadAfter(s, live.out, i, d)) continue;
        eraseAt(s, live, i);
        i--;
        changed = true;
    }
    return changed;
}

// C6xSched's `flipPredicates` on entries: `XOR 1, R, P; [P] B L` with P dead after is `MV R, P; [!P] B L`, or the
// branch alone where R is P. An entry made anew has no liveness (all live), as a line made anew has none.
bool flipPredicates(MStream &s, LiveSpan &live) {
    bool changed = false;
    for (std::size_t i = 0; i + 1 < s.size(); i++) {
        const MInst &x = s[i];
        if (x.kind != MInst::Instruction || x.packed || x.m.str() != "XOR" || x.operands != 3 || x.pred != kNoReg) continue;
        if (x.op[0].str() != "1" || !sideOf(x.op[1].str())) continue;
        const std::string R = x.op[1].str(), P = x.op[2].str();
        MInst &b = s[i + 1];
        if (b.kind != MInst::Instruction || b.packed || b.m.str() != "B" || b.pred == kNoReg || regName(b.pred) != P) continue;
        if (!deadAfter(s, live.out, i + 1, P)) continue;
        b.predNot = !b.predNot;
        live.in[i + 1] = live.out[i + 1] = ~0ull;
        if (R == P) eraseAt(s, live, i);
        else { s[i] = MInst("MV", MOperand(R), MOperand(P)); live.in[i] = live.out[i] = ~0ull; }
        changed = true;
    }
    return changed;
}

// C6xSched's `forwardConstants` on entries: `MVK k, R` goes into R's one reader on the straight path as its immediate,
// where the form takes one and R is dead after it (or written by it); a commutative operation takes it first.
bool forwardConstants(MStream &s, LiveSpan &live) {
    bool changed = false;
    for (std::size_t i = 0; i < s.size(); i++) {
        const MInst &mk = s[i];
        if (mk.kind != MInst::Instruction || mk.packed || mk.m.str() != "MVK" || mk.operands != 2 || mk.pred != kNoReg) continue;
        if (!isNumber(mk.op[0].str())) continue;
        const long k = std::atol(mk.op[0].str().c_str());
        const std::string R = mk.op[1].str();
        const std::uint64_t bit = bitOf(R);
        for (std::size_t j = i + 1; j < s.size(); j++) {
            const LiveFacts f = liveFacts(s[j]);
            if (!f.instr || isBranch(f.mnem)) break;
            if (f.reads & bit) {
                MInst &u = s[j];
                if (u.pred != kNoReg || !renameable(lineOf(u)) || u.operands != 3 || (!(f.writes & bit) && !deadAfter(s, live.out, j, R))) break;
                const std::string &m = f.mnem;
                int at = u.op[0].str() == R ? 0 : u.op[1].str() == R ? 1 : -1;
                if (at < 0 || u.op[0].str() == u.op[1].str() || isNumber(u.op[1 - at].str())) break;
                const bool small = k >= -16 && k <= 15, ucst = k >= 0 && k <= 31;
                const bool first = m == "ADD" || m == "AND" || m == "OR" || m == "XOR";
                bool ok = false;
                if (first && small) ok = true;
                else if ((m == "CMPEQ" || m == "CMPGT" || m == "CMPLT") && small && at == 0) ok = true;
                else if ((m == "CMPGTU" || m == "CMPLTU") && ucst && at == 0) ok = true;
                else if (m == "SUB" && ucst && at == 1 && crossings(lineOf(u)) == 0) ok = true;
                else if ((m == "SHL" || m == "SHR" || m == "SHRU") && ucst && at == 1) ok = true;
                if (!ok) break;
                if (first && at == 1) { u.op[1] = u.op[0]; at = 0; }
                u.op[at] = MOperand(static_cast<long long>(k));
                live.in[j] = live.out[j] = ~0ull;
                eraseAt(s, live, i);
                i--;
                changed = true;
                break;
            }
            if (f.writes & bit) break;
        }
    }
    return changed;
}

namespace {

int crossingsOf(const MInst &i) { return modelled(i) ? crossings(i) : crossings(lineOf(i)); }
unsigned unitsOf(const MInst &i) { return modelled(i) ? unitsFor(i) : unitsFor(lineOf(i)); }

// `renamed` on an operand: a register, an address's base or index, named `to` where it names `from`.
void renameOperand(MOperand &x, int from, int to) {
    if ((x.kind == MOperand::Register || x.kind == MOperand::Memory) && x.reg == from) x.reg = static_cast<std::uint8_t>(to);
    if (x.kind == MOperand::Memory && x.index == from) x.index = static_cast<std::uint8_t>(to);
}

// C6xSched's `legalForm` on an entry: a commutative one turns its sources about where the first crosses.
bool legalForm(MInst &r) {
    const std::string m = r.m.str();
    const bool commutes = m == "ADD" || m == "AND" || m == "OR" || m == "XOR" || m == "MPYDP" || m == "ADDDP" || m == "MPYSP" || m == "ADDSP" || m == "MPY32" || m == "CMPEQ";
    if (crossingsOf(r) > 1 && commutes && r.operands == 3 && !isNumber(r.op[0].str()) && !isNumber(r.op[1].str())) std::swap(r.op[0], r.op[1]);
    return crossingsOf(r) <= 1 && (unitsOf(r) != 0 || isLoad(m) || isStore(m));
}

// An address over both register files, which no unit reaches.
bool memSide(const MInst &e) {
    if (!modelled(e)) {
        const Line r = lineOf(e);
        for (std::size_t o = 0; o < r.ops.size(); o++)
            if (r.ops[o][0] == '*') { std::vector<std::string> m; registersIn(r.ops[o], m); if (m.size() > 1 && sideOf(m[0]) != sideOf(m[1])) return true; }
        return false;
    }
    for (int k = 0; k < e.operands; k++)
        if (e.op[k].kind == MOperand::Memory && e.op[k].index != kNoReg && sideOfReg(e.op[k].reg) != sideOfReg(e.op[k].index)) return true;
    return false;
}

// A fold's stream with each entry as the text reads it (`v`) and its liveness: an edit is made on the entry's operands
// where it is modelled, else by its spelling read back; CXX1_C6X_MIR=1 names an edit the text would spell otherwise.
struct Folding {
    MStream &s;
    LiveSpan &live;
    std::vector<Line> v;
    bool held = true, stale = false;     // held: liveness as computed; else recomputed after an edit, as `deadAfter` does
    Folding(MStream &stream, LiveSpan &l) : s(stream), live(l) { for (std::size_t k = 0; k < s.size(); k++) v.push_back(viewOf(s[k])); }
    void erase(std::size_t i) { eraseAt(s, live, i); v.erase(v.begin() + static_cast<long>(i)); stale = true; }
    void set(std::size_t i, const MInst &e) { s[i] = e; v[i] = viewOf(e); live.in[i] = live.out[i] = ~0ull; stale = true; }
    bool dead(std::size_t i, const std::string &reg) {
        if (!held && stale) { liveness(s, live.in, live.out); stale = false; }
        return deadAfter(s, live.out, i, reg);
    }
    void rw(std::size_t k, std::vector<std::string> &reads, std::vector<std::string> &writes) const {
        if (modelled(s[k])) readsAndWrites(s[k], reads, writes); else readsAndWrites(v[k], reads, writes);
    }
    bool touches(std::size_t k, const std::vector<std::string> &regs) const {
        std::vector<std::string> reads, writes;
        rw(k, reads, writes);
        for (std::size_t r = 0; r < regs.size(); r++) if (has(reads, regs[r]) || has(writes, regs[r])) return true;
        return false;
    }
    // Entry k edited: `edit` on its operands, or `text` (the text fold's rebuilt line) read back; false where it cannot be.
    template <typename Edit> bool edited(std::size_t k, const Line &text, Edit edit, MInst &e) const {
        if (modelled(s[k])) {
            e = s[k];
            edit(e);
            static const bool check = std::getenv("CXX1_C6X_MIR") != nullptr;
            if (check && viewOf(e).raw != text.raw) std::fprintf(stderr, "c6x-fold-edit: %s against %s\n", viewOf(e).raw.c_str(), text.raw.c_str());
            return true;
        }
        MStream one;
        read(text.raw + "\n", one);
        if (one.size() != 1) { std::fprintf(stderr, "c6x-fold-edit: unread %s\n", text.raw.c_str()); return false; }
        e = one[0];
        return true;
    }
};

}   // namespace

// C6xSched's `forwardMoves` on entries: a copy's one reader on the straight path takes the source itself, an instruction
// whose result is only copied writes the copy's destination, and a copy nothing reads is not made.
bool forwardMoves(MStream &s, LiveSpan &live) {
    Folding f(s, live);
    const std::vector<Line> &v = f.v;
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &mv = v[i];
        if (mv.mnem != "MV" || mv.ops.size() != 2 || !mv.pred.empty()) continue;
        const std::string S = mv.ops[0], D = mv.ops[1];
        if (sideOf(S) && S == D) { f.erase(i); i--; changed = true; continue; }   // a self-copy
        if (!sideOf(S) || !sideOf(D) || S == "A15" || D == "A15" || S == "B15" || D == "B15") continue;
        const int from = regNumber(D), to = regNumber(S);
        if (i > 0 && renameable(v[i - 1]) && v[i - 1].pred.empty() && v[i - 1].mnem != "MVKH" &&
            !v[i - 1].ops.empty() && v[i - 1].ops.back() == S && f.dead(i, S)) {
            std::vector<std::string> reads, writes;
            f.rw(i - 1, reads, writes);
            if (writes.size() == 1) {
                std::vector<std::string> ops = v[i - 1].ops;
                ops.back() = D;
                MInst e;
                if (f.edited(i - 1, rebuilt(v[i - 1].mnem, ops), [&](MInst &x) { x.op[x.operands - 1] = MOperand(D); }, e) &&
                    crossingsOf(e) <= 1 && unitsOf(e) != 0) {
                    f.set(i - 1, e);
                    f.erase(i);
                    i--;
                    changed = true;
                    continue;
                }
            }
        }
        for (std::size_t j = i + 1; j < v.size(); j++) {
            if (blockEnd(v[j])) break;
            std::vector<std::string> reads, writes;
            f.rw(j, reads, writes);
            if (has(reads, D)) {
                if (!v[j].pred.empty() || (!has(writes, D) && !f.dead(j, D))) break;
                if (v[j].mnem == "ADDK" || v[j].mnem == "MVKH") break;      // the destination is read too
                std::vector<std::string> ops = v[j].ops;
                bool pair = false;          // a register pair is odd:even, and a half cannot be renamed alone
                for (std::size_t o = 0; o < ops.size(); o++) if (ops[o].find(':') != std::string::npos) pair = true;
                if (pair) break;
                const std::string &m = v[j].mnem;
                const bool keepDest = !isStore(m) && m != "B" && ops.back()[0] != '*';
                for (std::size_t o = 0; o < ops.size(); o++) if (!(keepDest && o + 1 == ops.size())) ops[o] = renamed(ops[o], D, S);
                MInst e;
                auto rename = [&](MInst &x) { for (int k = 0; k < x.operands; k++) if (!(keepDest && k + 1 == x.operands)) renameOperand(x.op[k], from, to); };
                if (!f.edited(j, rebuilt(m, ops, v[j].pred), rename, e)) break;
                // A memory operand's registers stay on the unit's side; a branch register stays B.
                if (memSide(e) || crossingsOf(e) > 1 || unitsOf(e) == 0 || (e.m.str() == "B" && sideOf(e.op[0].str()) == 'A')) break;
                f.set(j, e);
                f.erase(i);
                i--;
                changed = true;
                break;
            }
            if (has(writes, D) || has(writes, S)) break;
        }
    }
    for (std::size_t i = 0; i < v.size(); i++)
        if (v[i].mnem == "MV" && v[i].ops.size() == 2 && v[i].pred.empty() && sideOf(v[i].ops[1]) &&
            v[i].ops[1] != "A15" && v[i].ops[1] != "B15" && f.dead(i, v[i].ops[1])) {
            f.erase(i);
            i--;
            changed = true;
        }
    return changed;
}

// C6xSched's `forwardPairMoves` on entries: a pair copied whole is made where the copies go, and a pair's two copies
// read whole by one reader are that reader reading the source pair.
bool forwardPairMoves(MStream &s, LiveSpan &live) {
    Folding f(s, live);
    const std::vector<Line> &v = f.v;
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &w = v[i];
        if (!w.instr || !w.pred.empty() || isStore(w.mnem) || isBranch(w.mnem) || w.mnem == "MVKH" || w.mnem == "ADDK" || w.ops.empty()) continue;
        if (w.ops.back().find(':') == std::string::npos) continue;
        std::vector<std::string> dst, reads, writes;
        registersIn(w.ops.back(), dst);
        f.rw(i, reads, writes);
        if (dst.size() != 2 || writes.size() != 2) continue;
        const std::string H = dst[0], L = dst[1];
        std::size_t mL = 0, mH = 0;
        std::string X, Y;
        std::vector<std::string> halves; halves.push_back(L); halves.push_back(H);
        for (std::size_t j = i + 1; j < v.size() && !blockEnd(v[j]) && (!mL || !mH); j++) {
            const Line &m = v[j];
            if (m.mnem == "MV" && m.ops.size() == 2 && m.pred.empty() && sideOf(m.ops[1])) {
                if (m.ops[0] == L && !mL) { mL = j; X = m.ops[1]; continue; }
                if (m.ops[0] == H && !mH) { mH = j; Y = m.ops[1]; continue; }
            }
            if (f.touches(j, halves)) break;
        }
        if (!mL || !mH) continue;
        const std::string pair = pairOf(X, Y);
        if (pair.empty() || X == L || X == H || Y == L || Y == H || X == "A15" || X == "B15") continue;
        const std::size_t last = std::max(mL, mH);
        std::vector<std::string> both; both.push_back(X); both.push_back(Y);
        bool ok = f.dead(last, L) && f.dead(last, H);
        for (std::size_t k = i + 1; k < last && ok; k++) if (k != mL && k != mH && f.touches(k, both)) ok = false;
        if (!ok) continue;
        std::vector<std::string> ops = w.ops;
        ops.back() = pair;
        MInst e;
        if (!f.edited(i, rebuilt(w.mnem, ops, w.pred), [&](MInst &x) { x.op[x.operands - 1] = MOperand(pair); }, e) || !legalForm(e)) continue;
        f.set(i, e);
        f.erase(last);
        f.erase(std::min(mL, mH));
        changed = true;
    }
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &a = v[i];
        if (!a.instr || a.mnem != "MV" || a.ops.size() != 2 || !a.pred.empty() || !sideOf(a.ops[0]) || !sideOf(a.ops[1])) continue;
        const std::string S = a.ops[0], L = a.ops[1], T = partner(S), H = partner(L);
        if (S == "A15" || S == "B15" || L == "A15" || L == "B15" || T == "A15" || T == "B15" || sideOf(S) != sideOf(T)) continue;
        std::vector<std::string> four; four.push_back(S); four.push_back(T); four.push_back(L); four.push_back(H);
        std::size_t j = i + 1;
        for (; j < v.size() && !blockEnd(v[j]); j++) {
            const Line &b = v[j];
            if (b.mnem == "MV" && b.ops.size() == 2 && b.pred.empty() && b.ops[0] == T && b.ops[1] == H) break;
            if (f.touches(j, four)) { j = v.size(); break; }
        }
        if (j >= v.size() || blockEnd(v[j])) continue;
        const std::string from = pairOf(std::min(L, H), std::max(L, H)), to = pairOf(std::min(S, T), std::max(S, T));
        if (from.empty() || to.empty()) continue;
        std::size_t r = j + 1;
        for (; r < v.size() && !blockEnd(v[r]); r++) {
            std::vector<std::string> reads, writes;
            f.rw(r, reads, writes);
            if (has(reads, L) || has(reads, H)) break;
            if (has(writes, L) || has(writes, H) || has(writes, S) || has(writes, T)) { r = v.size(); break; }
        }
        if (r >= v.size() || !v[r].instr || !v[r].pred.empty()) continue;
        std::vector<std::string> ops = v[r].ops, reads, writes;
        f.rw(r, reads, writes);
        const bool hasDest = !isStore(v[r].mnem) && ops.back()[0] != '*';
        bool ok = v[r].mnem != "MVKH" && v[r].mnem != "ADDK", seen = false;
        for (std::size_t o = 0; o + (hasDest ? 1 : 0) < ops.size() && ok; o++) {
            if (ops[o] == from) { ops[o] = to; seen = true; continue; }
            std::vector<std::string> regs;                                      // a half read on its own stays a copy
            registersIn(ops[o], regs);
            if (has(regs, L) || has(regs, H)) ok = false;
        }
        const bool overwrites = hasDest && ops.back() == from;
        if (!ok || !seen || (!overwrites && (has(writes, L) || has(writes, H)))) continue;
        if (!overwrites && !(f.dead(r, L) && f.dead(r, H))) continue;
        MInst e;
        auto repair = [&](MInst &x) { for (int k = 0; k + (hasDest ? 1 : 0) < x.operands; k++) if (x.op[k].str() == from) x.op[k] = MOperand(to); };
        if (!f.edited(r, rebuilt(v[r].mnem, ops, v[r].pred), repair, e) || !legalForm(e)) continue;
        f.set(r, e);
        f.erase(j);
        f.erase(i);
        i--;
        changed = true;
    }
    return changed;
}

// C6xSched's `numberValues` on entries: within a straight run each result numbered by its operation and its sources'
// numbers, a remake of a number a register holds a copy; liveness current, as the text's is, an entry gone a blank line.
void numberValues(MStream &s) {
    LiveSpan live;
    liveness(s, live.in, live.out);
    Folding f(s, live);
    f.held = false;
    const std::vector<Line> &v = f.v;
    MInst blank;
    blank.kind = MInst::Verbatim;
    blank.m = opt::StrId("\n");
    std::set<std::string> named;
    for (std::size_t i = 0; i < v.size(); i++)
        if (v[i].instr && v[i].mnem == "B") named.insert(v[i].ops[0]);
    std::map<std::string, long> val;            // register -> number
    std::map<std::string, long> made;           // key -> the number it makes
    std::map<long, std::size_t> defAt;          // number -> the entry that made it, this run
    long next = 1, stores = 0;
    std::size_t runStart = 0;
    auto reset = [&](std::size_t at) { val.clear(); made.clear(); defAt.clear(); next++; stores++; runStart = at; };
    // The number's register overwritten before the remake: its maker given a fresh register, its readers with it.
    auto renameFirst = [&](std::size_t p, std::size_t here, const std::string &D) -> std::string {
        std::size_t end = here;
        for (std::size_t k = p + 1; k < here; k++) {
            std::vector<std::string> r, w;
            f.rw(k, r, w);
            if (has(w, D) && v[k].pred.empty()) { end = k; break; }
        }
        const int from = regNumber(D);
        for (int n = 16; n < 32; n++) {
            const std::string F = "A" + std::to_string(n);
            bool ok = f.dead(p, F);
            for (std::size_t k = p + 1; k <= here && ok; k++) if (f.touches(k, std::vector<std::string>(1, F))) ok = false;
            if (!ok) continue;
            std::vector<MInst> changed;
            std::vector<std::size_t> where;
            std::vector<std::string> ops = v[p].ops;
            ops.back() = F;
            MInst d;
            if (!f.edited(p, rebuilt(v[p].mnem, ops, v[p].pred), [&](MInst &x) { x.op[x.operands - 1] = MOperand(F); }, d)) continue;
            if (crossingsOf(d) > 1 || (unitsOf(d) == 0 && !isLoad(d.m.str()))) continue;
            changed.push_back(d); where.push_back(p);
            for (std::size_t k = p + 1; k <= end && k < here && ok; k++) {   // the web's last entry may write D too
                std::vector<std::string> r, w;
                f.rw(k, r, w);
                if (!has(r, D)) continue;
                const Line &u = v[k];
                if (!u.pred.empty() || u.mnem == "MVKH" || u.mnem == "ADDK" || (has(w, D) && k != end)) { ok = false; break; }
                std::vector<std::string> uo = u.ops;
                std::size_t o = 0;
                for (; o + 1 < uo.size() || (o < uo.size() && (isStore(u.mnem) || uo[o][0] == '*')); o++) {
                    if (uo[o].find(':') != std::string::npos) { ok = false; break; }
                    if (uo[o][0] == '*' && sideOf(D) != 'A' && renamed(uo[o], D, F) != uo[o]) { ok = false; break; }
                    uo[o] = renamed(uo[o], D, F);
                }
                if (!ok) break;
                const std::size_t upTo = o;
                MInst r2;
                auto rename = [&](MInst &x) { for (std::size_t q = 0; q < upTo; q++) renameOperand(x.op[q], from, n); };
                if (!f.edited(k, rebuilt(u.mnem, uo, u.pred), rename, r2)) { ok = false; break; }
                const std::string m2 = r2.m.str();
                if (crossingsOf(r2) > 1 || (unitsOf(r2) == 0 && !isLoad(m2) && !isStore(m2))) { ok = false; break; }
                changed.push_back(r2); where.push_back(k);
            }
            if (!ok) continue;
            for (std::size_t c = 0; c < changed.size(); c++) f.set(where[c], changed[c]);
            return F;
        }
        return std::string();
    };
    auto numberOf = [&](const std::string &r) -> long { std::map<std::string, long>::iterator it = val.find(r); if (it == val.end()) { val[r] = next++; return val[r]; } return it->second; };
    auto keyOf = [&](const std::string &op) -> std::string {   // an operand with its registers' numbers
        std::string out;
        for (std::size_t k = 0; k < op.size();) {
            if ((op[k] == 'A' || op[k] == 'B') && k + 1 < op.size() && std::isdigit(static_cast<unsigned char>(op[k + 1])) && (k == 0 || !isNameChar(op[k - 1]))) {
                std::size_t e = k + 1;
                while (e < op.size() && std::isdigit(static_cast<unsigned char>(op[e]))) e++;
                out += "#" + std::to_string(numberOf(op.substr(k, e - k)));
                k = e;
            } else out += op[k++];
        }
        return out;
    };
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &l = v[i];
        if (isLabel(l)) { if (named.count(labelName(l))) reset(i); continue; }
        if (!l.instr) { reset(i); continue; }
        std::vector<std::string> reads, writes;
        f.rw(i, reads, writes);
        if (isBranch(l.mnem)) { if (l.pred.empty() || l.mnem == "CALLP" || sideOf(l.ops[0])) reset(i); continue; }
        bool pair = false, incr = false;
        for (std::size_t o = 0; o < l.ops.size(); o++) {
            if (l.ops[o].find(':') != std::string::npos) pair = true;
            if (l.ops[o][0] == '*' && (l.ops[o].find("++") != std::string::npos || l.ops[o].find("--") != std::string::npos)) incr = true;
        }
        const bool plain = l.pred.empty() && !pair && !incr && writes.size() == 1 && !isStore(l.mnem) && l.ops.size() >= 2 &&
                           writes[0] != "A15" && writes[0] != "B15" && writes[0] != "B14" && writes[0] != "B3";
        if (plain) {
            std::string key = l.mnem;
            for (std::size_t o = 0; o + 1 < l.ops.size(); o++) key += " " + keyOf(l.ops[o]);
            if (l.mnem == "MVKH" || l.mnem == "ADDK") key += " #" + std::to_string(numberOf(writes[0]));
            if (isLoad(l.mnem)) key += " @" + std::to_string(stores);
            const std::string dst = writes[0];
            if (l.mnem == "MV" && sideOf(l.ops[0])) {                           // a copy carries the number
                const long n = numberOf(l.ops[0]);
                if (val.count(dst) && val[dst] == n) { f.set(i, blank); continue; }   // a copy back of what it holds
                val[dst] = n;
                continue;
            }
            std::map<std::string, long>::iterator it = made.find(key);
            const long number = it != made.end() ? it->second : next++;
            if (it == made.end()) made[key] = number;
            std::string holder;                 // a register still holding that number, the destination itself first
            if (val.count(dst) && val[dst] == number) holder = dst;
            for (std::map<std::string, long>::const_iterator h = val.begin(); h != val.end() && holder.empty(); ++h)
                if (h->second == number && sideOf(h->first) && h->first != "A15" && h->first != "B15") holder = h->first;
            const bool store = isStore(l.mnem);
            if (holder.empty() && it != made.end() && defAt.count(number) && defAt[number] >= runStart && !store) {
                const std::size_t p = defAt[number];
                std::vector<std::string> r0, w0;
                f.rw(p, r0, w0);
                if (v[p].mnem != "MVKL" && v[p].mnem != "MVKH" && v[p].mnem != "ADDK" && sideOf(w0[0]) && w0[0] != "A15" && w0[0] != "B15") {
                    holder = renameFirst(p, i, w0[0]);
                    if (!holder.empty()) val[holder] = number;
                }
            }
            if (!holder.empty()) {
                if (holder == dst) { f.set(i, blank); continue; }   // gone: no operands left to read
                const MInst r("MV", MOperand(holder), MOperand(dst));
                if (crossingsOf(r) <= 1) { f.set(i, r); val[dst] = number; continue; }
            }
            val[dst] = number;
            defAt[number] = i;
            continue;
        }
        for (std::size_t w = 0; w < writes.size(); w++) val[writes[w]] = next++;
        if (isStore(l.mnem)) stores++;
    }
    MStream out;
    for (std::size_t i = 0; i < s.size(); i++) if (v[i].instr || !v[i].raw.empty()) out.push_back(s[i]);
    s.swap(out);
}

namespace {

// Entry k with operand `at` the address `addr`, by its operands or its spelling (`rebuilt` with no predicate).
bool readdressed(Folding &f, std::size_t k, std::size_t at, const std::string &addr) {
    std::vector<std::string> ops = f.v[k].ops;
    ops[at] = addr;
    MInst e;
    if (!f.edited(k, rebuilt(f.v[k].mnem, ops), [&](MInst &x) { x.op[at] = MOperand(addr); }, e)) return false;
    f.set(k, e);
    return true;
}

// C6xSched's `foldScaledIndex` on entries: `SHL I, log2(size), T` read by `ADD R, T, U`s whose U is an access's
// address becomes `*+R[I]` (or `*+R[T]` after `MV I, T`).
bool foldScaledIndex(Folding &f, std::size_t i, const std::set<std::string> &named) {
    const std::vector<Line> &v = f.v;
    const Line &l = v[i];
    if (l.mnem != "SHL" || l.ops.size() != 3 || !isNumber(l.ops[1]) || !sideOf(l.ops[0]) || !sideOf(l.ops[2]) || l.ops[0] == l.ops[2]) return false;
    const std::string I = l.ops[0], T = l.ops[2];
    const long shift = std::atol(l.ops[1].c_str());
    if (shift < 0 || shift > 3) return false;
    struct Use { std::size_t add, access; std::string R, U; bool iWritten; };
    std::vector<Use> uses;
    std::vector<std::size_t> unfolded;
    bool iWritten = false, tLive = true;
    std::size_t last = i;
    for (std::size_t j = i + 1; j < v.size(); j++) {
        if (isLabel(v[j]) && passThrough(v[j], named)) continue;
        if (blockEnd(v[j])) { tLive = !f.dead(j - 1, T); break; }
        std::vector<std::string> r2, w2;
        f.rw(j, r2, w2);
        if (has(w2, I)) iWritten = true;
        if (has(r2, T) && has(w2, T)) { unfolded.push_back(j); tLive = false; break; }
        if (has(w2, T)) { tLive = false; break; }
        if (!has(r2, T)) continue;
        const Line &a = v[j];
        const bool shape = a.pred.empty() && a.mnem == "ADD" && a.ops.size() == 3 && a.ops[2] != T && a.ops[2] != I &&
                           ((a.ops[0] == T && sideOf(a.ops[1]) && a.ops[1] != T) || (a.ops[1] == T && sideOf(a.ops[0]) && a.ops[0] != T));
        if (!shape) { unfolded.push_back(j); continue; }
        Use u{ j, 0, a.ops[0] == T ? a.ops[1] : a.ops[0], a.ops[2], iWritten };
        if (u.R == "A15" || u.R == "B15" || u.U == "A15" || u.U == "B15" || u.R == I) { unfolded.push_back(j); continue; }
        for (std::size_t k = j + 1; k < v.size() && u.access == 0; k++) {
            if (isLabel(v[k]) && passThrough(v[k], named)) continue;
            if (blockEnd(v[k])) break;
            std::vector<std::string> r3, w3;
            f.rw(k, r3, w3);
            const bool access = v[k].pred.empty() && v[k].ops.size() == 2 && (isLoad(v[k].mnem) || isStore(v[k].mnem));
            if (has(w3, u.R) || has(w3, T) || has(w3, I)) { if (!(access && isLoad(v[k].mnem) && v[k].ops[1] == u.R && !has(w3, T) && !has(w3, I))) break; }
            if (!has(r3, u.U) && !has(w3, u.U)) continue;
            if (!access) break;
            const std::size_t m = isStore(v[k].mnem) ? 1 : 0;
            if (v[k].ops[m] != "*" + u.U || accessSize(v[k].mnem) != (1L << shift)) break;
            std::vector<std::string> data;
            registersIn(v[k].ops[1 - m], data);
            if (has(data, u.R) || has(data, T) || has(data, I)) break;
            if (!(has(data, u.U) || f.dead(k, u.U))) break;
            u.access = k;
        }
        if (u.access == 0) { unfolded.push_back(j); continue; }
        uses.push_back(u);
        if (u.access > last) last = u.access;
    }
    if (uses.empty()) return false;
    bool direct = true;
    for (std::size_t k = 0; k < uses.size(); k++) direct = direct && !uses[k].iWritten && sideOf(I) == sideOf(uses[k].R);
    for (std::size_t j = i + 1; j <= last && direct; j++) {
        std::vector<std::string> r2, w2;
        if (v[j].instr) { f.rw(j, r2, w2); if (has(w2, I)) direct = false; }
    }
    if (!direct) {
        if (!unfolded.empty() || tLive) return false;
        for (std::size_t k = 0; k < uses.size(); k++) if (sideOf(T) != sideOf(uses[k].R)) return false;
    }
    const std::string X = direct ? I : T;
    std::vector<std::size_t> gone;
    for (std::size_t k = 0; k < uses.size(); k++) {
        readdressed(f, uses[k].access, isStore(v[uses[k].access].mnem) ? 1 : 0, "*+" + uses[k].R + "[" + X + "]");
        gone.push_back(uses[k].add);
    }
    if (direct && unfolded.empty() && !tLive) gone.push_back(i);
    else if (!direct) f.set(i, MInst("MV", MOperand(I), MOperand(T)));
    std::sort(gone.begin(), gone.end());
    for (std::size_t k = gone.size(); k-- > 0;) f.erase(gone[k]);
    return true;
}

}   // namespace

// C6xSched's `foldAddressing` on entries: an address made by ADD or SUB goes into its one access as `*+R(k)`, and a
// step after an access through `*R` into it as `*R++(k)`; liveness held as computed.
void foldAddressing(MStream &s, bool index) {
    LiveSpan live;
    liveness(s, live.in, live.out);
    Folding f(s, live);
    const std::vector<Line> &v = f.v;
    std::set<std::string> named;
    for (std::size_t i = 0; i < v.size(); i++) for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        const Line &l = v[i];
        if (!l.instr || !l.pred.empty()) continue;
        if (index && foldScaledIndex(f, i, named)) { i--; continue; }
        std::vector<std::string> reads, writes;
        f.rw(i, reads, writes);
        if ((l.mnem == "ADD" || l.mnem == "SUB") && l.ops.size() == 3 && writes.size() == 1 && l.ops[2] == writes[0] &&
            sideOf(writes[0]) && writes[0] != "A15" && writes[0] != "B15" && (isNumber(l.ops[0]) != isNumber(l.ops[1])) &&
            (isNumber(l.ops[0]) ? l.ops[1] : l.ops[0]) == writes[0] && !(l.mnem == "SUB" && isNumber(l.ops[0]))) {
            const std::string R = writes[0];
            long k = std::atol((isNumber(l.ops[0]) ? l.ops[0] : l.ops[1]).c_str());
            if (l.mnem == "SUB") k = -k;
            for (std::size_t j = i + 1; j < v.size(); j++) {
                if (isLabel(v[j]) && passThrough(v[j], named)) continue;
                if (blockEnd(v[j])) break;
                std::vector<std::string> r2, w2;
                f.rw(j, r2, w2);
                if (!has(r2, R) && !has(w2, R)) continue;
                const Line &u = v[j];
                if (u.pred.empty() && u.ops.size() == 2 && (isLoad(u.mnem) || isStore(u.mnem))) {
                    const std::size_t at = isStore(u.mnem) ? 1 : 0;
                    const long size = accessSize(u.mnem), mag = k < 0 ? -k : k;
                    std::vector<std::string> data;
                    registersIn(u.ops[1 - at], data);
                    if (u.ops[at] == "*" + R && mag != 0 && mag % size == 0 && mag <= 31 && !has(data, R) &&
                        readdressed(f, j, at, (k < 0 ? "*--" : "*++") + R + "(" + std::to_string(mag) + ")")) {
                        f.erase(i);
                        i--;
                    }
                }
                break;
            }
            continue;
        }
        if ((l.mnem == "ADD" || l.mnem == "SUB" || l.mnem == "SUBAW") && l.ops.size() == 3 && writes.size() == 1 && !has(reads, writes[0])) {
            const bool k0 = isNumber(l.ops[0]), k1 = isNumber(l.ops[1]);
            if (l.mnem != "ADD" && !k1) continue;
            if (k0 == k1) continue;
            const std::string R = k0 ? l.ops[1] : l.ops[0], T = writes[0];
            long k = std::atol((k0 ? l.ops[0] : l.ops[1]).c_str());
            if (l.mnem == "SUBAW") k *= 4;
            if (l.mnem != "ADD") k = -k;
            if (!sideOf(R) || R == T) continue;
            for (std::size_t j = i + 1; j < v.size(); j++) {
                if (isLabel(v[j]) && passThrough(v[j], named)) continue;
                if (blockEnd(v[j])) break;
                std::vector<std::string> r2, w2;
                f.rw(j, r2, w2);
                const bool access = v[j].pred.empty() && v[j].ops.size() == 2 && (isLoad(v[j].mnem) || isStore(v[j].mnem));
                if (has(w2, R) && !(access && isLoad(v[j].mnem) && v[j].ops[1] == R)) break;
                if (!has(r2, T) && !has(w2, T)) continue;
                const Line &u = v[j];
                if (access) {
                    const std::size_t at = isStore(u.mnem) ? 1 : 0;
                    const long size = accessSize(u.mnem), mag = k < 0 ? -k : k;
                    std::vector<std::string> data;
                    registersIn(u.ops[1 - at], data);
                    if (u.ops[at] == "*" + T && mag % size == 0 && mag / size <= 31 && (!has(data, R) || isLoad(u.mnem)) && (has(data, T) || f.dead(j, T)) &&
                        readdressed(f, j, at, mag == 0 ? "*" + R : (k < 0 ? "*-" : "*+") + R + "(" + std::to_string(mag) + ")")) {
                        f.erase(i);
                        i--;
                    }
                }
                break;
            }
            continue;
        }
        if ((isLoad(l.mnem) || isStore(l.mnem)) && l.ops.size() == 2) {
            const std::size_t at = isStore(l.mnem) ? 1 : 0;
            if (l.ops[at].size() < 2 || l.ops[at][0] != '*' || !sideOf(l.ops[at].substr(1))) continue;
            const std::string R = l.ops[at].substr(1);
            std::vector<std::string> data;
            registersIn(l.ops[1 - at], data);
            if (has(data, R) || R == "A15" || R == "B15") continue;
            for (std::size_t j = i + 1; j < v.size(); j++) {
                if (isLabel(v[j]) && passThrough(v[j], named)) continue;
                if (blockEnd(v[j])) break;
                std::vector<std::string> r2, w2;
                f.rw(j, r2, w2);
                if (!has(r2, R) && !has(w2, R)) continue;
                const Line &u = v[j];
                const bool k0 = u.ops.size() == 3 && isNumber(u.ops[0]), k1 = u.ops.size() == 3 && isNumber(u.ops[1]);
                if (u.pred.empty() && (u.mnem == "ADD" || u.mnem == "SUB") && u.ops.size() == 3 && u.ops[2] == R && (k0 != k1) &&
                    (k0 ? u.ops[1] : u.ops[0]) == R && !(u.mnem == "SUB" && k0)) {
                    long k = std::atol((k0 ? u.ops[0] : u.ops[1]).c_str());
                    if (u.mnem == "SUB") k = -k;
                    const long size = accessSize(l.mnem), mag = k < 0 ? -k : k;
                    if (mag != 0 && mag % size == 0 && mag / size <= 31 && readdressed(f, i, at, "*" + R + (k < 0 ? "--" : "++") + "(" + std::to_string(mag) + ")"))
                        f.erase(j);
                }
                break;
            }
        }
    }
}

namespace {

std::string opText(const MInst &x, int k) { return k < x.operands ? x.op[k].str() : std::string(); }
bool isInstr(const MInst &x, const char *m) { return x.kind == MInst::Instruction && x.m.str() == m; }
MInst made(const std::string &m, const MOperand &a, const MOperand &b = MOperand()) { return MInst(m.c_str(), a, b); }
void rwOf(const MInst &x, std::vector<std::string> &reads, std::vector<std::string> &writes) {
    if (modelled(x)) readsAndWrites(x, reads, writes); else readsAndWrites(lineOf(x), reads, writes);
}

// C6xSched's `foldCalls` on entries: `MVKL L, B3; MVKH L, B3; B f; L:` is `CALLP f, B3`.
void foldCalls(MStream &s) {
    MStream out;
    for (std::size_t i = 0; i < s.size(); i++) {
        std::string name;
        if (i + 3 < s.size() && isInstr(s[i], "MVKL") && isInstr(s[i + 1], "MVKH") && isInstr(s[i + 2], "B") &&
            s[i].pred == kNoReg && s[i + 1].pred == kNoReg && s[i + 2].pred == kNoReg &&
            s[i].operands == 2 && s[i + 1].operands == 2 && opText(s[i], 1) == "B3" && opText(s[i + 1], 1) == "B3" &&
            opText(s[i], 0) == opText(s[i + 1], 0) && labelLike(s[i + 3], name) && name == opText(s[i], 0) &&
            s[i + 2].operands != 0 && !sideOf(opText(s[i + 2], 0))) {
            out.push_back(made("CALLP", s[i + 2].op[0], MOperand("B3")));
            i += 2;
            continue;
        }
        out.push_back(s[i]);
    }
    s.swap(out);
}

// C6xSched's `foldMvk` on entries: MVKL then MVKH of a constant in sixteen signed bits is one MVK.
void foldMvk(MStream &s) {
    for (std::size_t i = 0; i + 1 < s.size(); i++) {
        const MInst &a = s[i], &b = s[i + 1];
        if (!isInstr(a, "MVKL") || !isInstr(b, "MVKH") || a.operands != 2 || b.operands != 2) continue;
        if (opText(a, 0) != opText(b, 0) || opText(a, 1) != opText(b, 1) || !isNumber(opText(a, 0))) continue;
        const long k = std::atol(opText(a, 0).c_str());
        if (k < -32768 || k > 32767) continue;
        s[i] = made("MVK", a.op[0], a.op[1]);
        s.erase(s.begin() + static_cast<long>(i) + 1);
    }
}

// C6xSched's `foldFrame` on entries, its liveness held as computed: a local's address built and used once is `*-A15(k)`.
void foldFrame(MStream &s, LiveSpan &live) {
    for (std::size_t i = 0; i + 1 < s.size(); i++) {
        const MInst &x = s[i];
        if (x.kind != MInst::Instruction || (x.m.str() != "SUB" && x.m.str() != "SUBAW") || x.operands != 3 || opText(x, 0) != "A15") continue;
        const std::string reg = opText(x, 2), k1 = opText(x, 1);
        long k;
        std::size_t first = i;
        if (x.m.str() == "SUBAW") { if (!isNumber(k1)) continue; k = 4 * std::atol(k1.c_str()); }
        else if (isNumber(k1)) k = std::atol(k1.c_str());
        else if (k1 == "A0" && i > 0 && isInstr(s[i - 1], "MVK") && s[i - 1].operands == 2 && opText(s[i - 1], 1) == "A0" &&
                 isNumber(opText(s[i - 1], 0)) && deadAfter(s, live.out, i, "A0")) {
            k = std::atol(opText(s[i - 1], 0).c_str());
            first = i - 1;
        } else continue;
        const MInst use = s[i + 1];
        if (use.kind != MInst::Instruction || use.operands != 2 || use.pred != kNoReg) continue;
        const std::string m = use.m.str();
        const bool load = startsWith(m, "LD") && opText(use, 0) == "*" + reg;
        const bool store = isStore(m) && opText(use, 1) == "*" + reg;
        if (!load && !store) continue;
        const int size = accessSize(m);
        if (k < 0 || k % size != 0) continue;
        std::vector<std::string> into;
        if (load) registersIn(opText(use, 1), into);
        if (!has(into, reg) && !deadAfter(s, live.out, i + 1, reg)) continue;
        const bool far = k / size > 31;
        if (far && (first == i || k / size > 32767)) continue;
        const MOperand mem(far ? std::string("*-A15[A0]") : "*-A15(" + std::to_string(k) + ")");
        s[i + 1] = load ? made(m, mem, use.op[1]) : made(m, use.op[0], mem);
        live.in[i + 1] = live.out[i + 1] = ~0ull;
        if (far) {
            s[first] = made("MVK", MOperand(static_cast<long long>(k / size)), MOperand("A0"));
            live.in[first] = live.out[first] = ~0ull;
            eraseAt(s, live, i);
        } else {
            for (std::size_t n = first; n <= i; n++) eraseAt(s, live, first);
            i = first;
        }
    }
}

bool isPush(const MStream &s, std::size_t i) {
    if (i + 1 >= s.size() || s[i].str() != "\tSUB\tB15, 8, B15\n" || s[i + 1].kind != MInst::Instruction || !isStore(s[i + 1].m.str())) return false;
    const std::string m = s[i + 1].m.str(), at = opText(s[i + 1], 1);
    return s[i + 1].operands == 2 && ((at == "*B15" && (m == "STW" || m == "STDW")) || (at == "*+B15(4)" && m == "STW"));
}
bool isPop(const MStream &s, std::size_t i) {
    if (i + 1 >= s.size() || !(isInstr(s[i], "LDW") || isInstr(s[i], "LDDW")) || s[i].operands != 2) return false;
    const std::string at = opText(s[i], 0);
    return (at == "*B15" || (at == "*+B15(4)" && s[i].m.str() == "LDW")) && s[i + 1].str() == "\tADD\tB15, 8, B15\n";
}

// C6xSched's `foldPushPop` on entries: a push popped in its block, B15 untouched between, keeps its value in A16-A31.
void foldPushPop(MStream &s, const LiveSpan &live) {
    std::vector<std::size_t> open;
    std::vector<std::pair<std::size_t, std::size_t> > pairs;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (s[i].kind != MInst::Instruction || isBranch(s[i].m.str())) { open.clear(); continue; }
        if (isPush(s, i)) { open.push_back(i); i++; continue; }
        if (isPop(s, i)) {
            if (!open.empty()) { pairs.push_back(std::make_pair(open.back(), i)); open.pop_back(); }
            i++;
            continue;
        }
        std::vector<std::string> reads, writes;
        rwOf(s[i], reads, writes);
        if (has(reads, "B15") || has(writes, "B15")) open.clear();
    }
    std::vector<bool> drop(s.size(), false);
    for (std::size_t p = 0; p < pairs.size(); p++) {
        const std::size_t push = pairs[p].first, pop = pairs[p].second;
        int depth = 0;
        for (std::size_t q = 0; q < pairs.size(); q++) if (pairs[q].first < push && pairs[q].second > pop) depth++;
        std::string lo, hi;
        for (; depth < 8 && lo.empty(); depth++) {
            const std::string l = "A" + std::to_string(16 + 2 * depth), h = "A" + std::to_string(17 + 2 * depth);
            bool ok = deadAfter(s, live.out, push, l) && deadAfter(s, live.out, push, h);
            for (std::size_t k = push + 1; k <= pop + 1 && ok; k++) {
                std::vector<std::string> r, w;
                rwOf(s[k], r, w);
                if (has(r, l) || has(w, l) || has(r, h) || has(w, h)) ok = false;
            }
            if (ok) { lo = l; hi = h; }
        }
        if (lo.empty()) continue;
        const std::string src = opText(s[push + 1], 0), dst = opText(s[pop], 1);
        if (s[push + 1].m.str() == "STDW") {      // the pair's halves take the two stack entries
            s[push] = made("MV", MOperand(src.substr(src.find(':') + 1)), MOperand(lo));
            s[push + 1] = made("MV", MOperand(src.substr(0, src.find(':'))), MOperand(hi));
            s[pop] = made("MV", MOperand(lo), MOperand(dst.substr(dst.find(':') + 1)));
            s[pop + 1] = made("MV", MOperand(hi), MOperand(dst.substr(0, dst.find(':'))));
            continue;
        }
        s[push + 1] = made("MV", s[push + 1].op[0], MOperand(lo));
        s[pop] = made("MV", MOperand(lo), s[pop].op[1]);
        drop[push] = drop[pop + 1] = true;
    }
    MStream out;
    for (std::size_t i = 0; i < s.size(); i++) if (!drop[i]) out.push_back(s[i]);
    s.swap(out);
}

// C6xSched's `threadPredicates` on entries: a branch to a test it has just made goes where that test goes.
void threadPredicates(MStream &s) {
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < s.size(); i++) { std::string name; if (labelLike(s[i], name)) at[name] = i; }
    for (std::size_t i = 0; i < s.size(); i++) {
        MInst &b = s[i];
        if (!isInstr(b, "B") || b.pred == kNoReg || b.operands == 0 || !at.count(opText(b, 0))) continue;
        const MInst *mv = i > 0 && isInstr(s[i - 1], "MV") && s[i - 1].operands == 2 && s[i - 1].pred == kNoReg &&
                          opText(s[i - 1], 1) == regName(b.pred) ? &s[i - 1] : nullptr;
        for (int hops = 0; hops < 8; hops++) {
            std::size_t j = at[opText(b, 0)] + 1;
            std::string name;
            while (j < s.size() && labelLike(s[j], name)) j++;
            if (j >= s.size() || s[j].kind != MInst::Instruction) break;
            if (mv != nullptr && isInstr(s[j], "MV") && s[j].operands == 2 && opText(s[j], 0) == opText(*mv, 0) &&
                opText(s[j], 1) == opText(*mv, 1) && s[j].pred == kNoReg) j++;
            if (j >= s.size() || !isInstr(s[j], "B") || s[j].pred != b.pred || s[j].predNot != b.predNot || s[j].operands == 0 ||
                !at.count(opText(s[j], 0)) || opText(s[j], 0) == opText(b, 0)) break;
            const MOperand to = s[j].op[0];
            for (int k = 1; k < b.operands; k++) b.op[k] = MOperand();
            b.op[0] = to;
            b.operands = 1;
        }
    }
}

// C6xSched's `dropUnnamedLabels` on entries: a label of the backend's own flow no branch names ends no block.
void dropUnnamedLabels(MStream &s) {
    std::set<std::string> named;
    for (std::size_t i = 0; i < s.size(); i++)
        if (s[i].kind == MInst::Instruction) for (int o = 0; o < s[i].operands; o++) named.insert(opText(s[i], o));
    MStream out;
    for (std::size_t i = 0; i < s.size(); i++) {
        std::string name;
        if (labelLike(s[i], name) && (passThroughName(name, named) || (startsWith(name, "L.return.") && !named.count(name)))) continue;
        out.push_back(s[i]);
    }
    s.swap(out);
}

}   // namespace

// The passes before the first `rounds` on a selected function's stream (C6xMir.h), in c6xSchedule's order.
void foldFront(MStream &s, bool thread, bool labels) {
    foldCalls(s);
    foldMvk(s);
    LiveSpan live;
    liveness(s, live.in, live.out);
    foldFrame(s, live);
    liveness(s, live.in, live.out);
    foldPushPop(s, live);
    if (thread) threadPredicates(s);
    if (labels) dropUnnamedLabels(s);
}

namespace {

void branchTo(MInst &b, const MOperand &to) {
    for (int k = 1; k < b.operands; k++) b.op[k] = MOperand();
    b.op[0] = to;
    b.operands = 1;
}
bool callOn(const MInst &x, const std::set<std::string> &labels) { return isCallTo(x.m.str(), opText(x, 0), labels); }
std::set<std::string> labelsOf(const MStream &s) {
    std::set<std::string> labels;
    for (std::size_t i = 0; i < s.size(); i++) { std::string name; if (labelLike(s[i], name)) labels.insert(name); }
    return labels;
}

// C6xSched's `rotateLoops` on entries: a jump to a loop's test is the test itself, inverted, to the body's label.
void rotateLoops(MStream &s) {
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < s.size(); i++) { std::string name; if (labelLike(s[i], name)) at[name] = i; }
    MStream out;
    std::set<std::string> rotated;
    for (std::size_t i = 0; i < s.size(); i++) {
        const MInst &b = s[i];
        std::string next;
        bool taken = false;
        if (isInstr(b, "B") && b.pred == kNoReg && at.count(opText(b, 0)) && i + 1 < s.size() && labelLike(s[i + 1], next)) {
            std::size_t h = at[opText(b, 0)], e = h + 1;
            while (e < s.size() && s[e].kind == MInst::Instruction && !isBranch(s[e].m.str())) e++;
            if (e < s.size() && e - h <= 17 && isInstr(s[e], "B") && s[e].pred != kNoReg && opText(s[e], 0) == next && (i < h || i > e)) {
                for (std::size_t k = h + 1; k < e; k++) out.push_back(s[k]);
                MInst r("B", MOperand(opText(b, 0) + "$rot"));
                r.pred = s[e].pred;
                r.predNot = !s[e].predNot;
                out.push_back(r);
                rotated.insert(opText(b, 0));
                taken = true;
            }
        }
        if (!taken) out.push_back(b);
    }
    s.swap(out);
    for (std::size_t i = 0; i < s.size(); i++) {
        std::string name;
        if (!labelLike(s[i], name) || !rotated.count(name)) continue;
        std::size_t e = i + 1;
        while (!isInstr(s[e], "B")) e++;
        MStream one;
        read(name + "$rot:\n", one);
        s.insert(s.begin() + static_cast<long>(e) + 1, one[0]);
    }
}

// C6xSched's `tidyJumps` on entries: what follows a jump unreached, a jump to a jump taken through, a jump to the next gone.
void tidyJumps(MStream &s) {
    const std::set<std::string> labels = labelsOf(s);
    MStream out;
    for (std::size_t i = 0; i < s.size(); i++) {
        out.push_back(s[i]);
        if (!isInstr(s[i], "B") || s[i].pred != kNoReg || callOn(s[i], labels)) continue;
        while (i + 1 < s.size() && s[i + 1].kind == MInst::Instruction) i++;
    }
    s.swap(out);
    std::map<std::string, std::string> onward;
    for (std::size_t i = 0; i < s.size(); i++) {
        std::string name, other;
        if (!labelLike(s[i], name)) continue;
        std::size_t j = i + 1;
        while (j < s.size() && labelLike(s[j], other)) j++;
        if (j < s.size() && isInstr(s[j], "B") && s[j].pred == kNoReg && !callOn(s[j], labels) && !sideOf(opText(s[j], 0)))
            onward[name] = opText(s[j], 0);
    }
    for (std::size_t i = 0; i < s.size(); i++) {
        if (!isInstr(s[i], "B") || sideOf(opText(s[i], 0)) || callOn(s[i], labels)) continue;
        std::string t = opText(s[i], 0);
        for (int hops = 0; hops < 8 && onward.count(t) && onward[t] != t; hops++) t = onward[t];
        if (t != opText(s[i], 0)) branchTo(s[i], MOperand(t));
    }
    out.clear();
    for (std::size_t i = 0; i < s.size(); i++) {
        if (isInstr(s[i], "B") && !sideOf(opText(s[i], 0)) && !callOn(s[i], labels)) {
            std::size_t j = i + 1;
            bool next = false;
            std::string name;
            for (; j < s.size() && s[j].kind != MInst::Instruction; j++) if (labelLike(s[j], name) && name == opText(s[i], 0)) next = true;
            if (!next && j == s.size() && isReturnTo(s[i].m.str(), opText(s[i], 0), labels)) next = true;
            if (next) continue;
        }
        out.push_back(s[i]);
    }
    s.swap(out);
}

}   // namespace

// C6xSched's `predicateArms` on entries: `[P] B L; i1..ik; B M; L:` is `[!P] i1..ik; [!P] B M; L:`, k at most four.
bool predicateArms(MStream &s) {
    const std::set<std::string> labels = labelsOf(s);
    bool changed = false;
    for (std::size_t i = 0; i < s.size(); i++) {
        const MInst b = s[i];
        if (!isInstr(b, "B") || b.pred == kNoReg || !labels.count(opText(b, 0))) continue;
        const std::string P = regName(b.pred);
        std::size_t j = i + 1;
        bool ok = true;
        for (; j < s.size() && s[j].kind == MInst::Instruction && !isBranch(s[j].m.str()) && ok; j++) {
            const MInst &l = s[j];
            const std::string m = l.m.str();
            if (j - i > 4 || l.pred != kNoReg || (!isLoad(m) && !isStore(m) && unitsOf(l) == 0)) ok = false;
            std::vector<std::string> reads, writes;
            rwOf(l, reads, writes);
            if (has(writes, P) || has(reads, "B15") || has(writes, "B15") || has(writes, "A15")) ok = false;
        }
        if (!ok || j >= s.size() || !isInstr(s[j], "B") || s[j].pred != kNoReg || !labels.count(opText(s[j], 0))) continue;
        bool target = false;
        std::string name;
        for (std::size_t m = j + 1; m < s.size() && labelLike(s[m], name); m++) if (name == opText(b, 0)) target = true;
        if (!target) continue;
        for (std::size_t k = i + 1; k <= j; k++) { s[k].pred = b.pred; s[k].predNot = !b.predNot; }
        s.erase(s.begin() + static_cast<long>(i));
        i--;
        changed = true;
    }
    return changed;
}

// The jumps tidied on a selected function's stream: rotateLoops, tidyJumps and (labels) dropUnnamedLabels, as c6xSchedule.
void tidyStream(MStream &s, bool labels) {
    rotateLoops(s);
    tidyJumps(s);
    if (labels) dropUnnamedLabels(s);
}

bool streamOf(const std::vector<Line> &v, MStream &s) {
    for (std::size_t k = 0; k < v.size(); k++) {
        const Line &l = v[k];
        // The pipeliner's lines are placed already, `||` and all: the mark is read off, the rest is an entry.
        const bool par = l.verbatim && startsWith(l.raw, "||");
        MStream one;
        read((par ? l.raw.substr(2) : l.raw) + "\n", one);
        if (one.size() != 1) return false;
        MInst &i = one[0];
        if (!l.verbatim) {
            // An instruction the stream spells otherwise, or a line whose fields are not its text's, stays with the text pass.
            if (l.instr != (i.kind == MInst::Instruction)) return false;
            const Line p = parse(l.raw);
            if (p.instr != l.instr || p.mnem != l.mnem || p.ops != l.ops || p.pred != l.pred) return false;
        }
        // A placed line the text's fold would read as an instruction and the stream cannot: the text pass too.
        if (l.verbatim && i.kind == MInst::Verbatim && lineOf(i).instr) return false;
        i.par = par;
        i.packed = l.verbatim;
        s.push_back(i);
    }
    return true;
}

// The stream as the text passes read it: a placed entry a kept line, `||` and all; any other its spelling, parsed.
void linesOf(const MStream &s, std::vector<Line> &v) {
    v.clear();
    for (std::size_t k = 0; k < s.size(); k++) {
        std::string raw = s[k].str();
        if (!raw.empty() && raw[raw.size() - 1] == '\n') raw.erase(raw.size() - 1);
        Line l;
        if (s[k].packed) { l.raw = raw; l.verbatim = true; }
        else l = parse(raw);
        v.push_back(l);
    }
}

// Each block scheduled on its own, every label and kept line where it stood: `schedule` of C6xSched.cpp, on entries.
void packetise(MStream &s) {
    std::set<std::string> labels, named;
    std::string name;
    for (std::size_t k = 0; k < s.size(); k++) {
        if (labelLike(s[k], name)) labels.insert(name);
        if (s[k].kind != MInst::Instruction || s[k].packed) continue;
        for (int o = 0; o < s[k].operands; o++) named.insert(s[k].op[o].str());
    }
    MStream out, members;
    std::vector<Node> block;
    std::vector<Pending> pend;
    // A label is fallen into with the block's late writes in flight; anything else waits for them to land.
    auto flush = [&](bool carry) {
        std::vector<Row> rows;
        if (!block.empty()) pend = scheduleBlock(block, rows, pend, carry);
        else if (!carry) waitOut(pend, rows);
        for (std::size_t r = 0; r < rows.size(); r++) {
            if (rows[r].node < 0) { out.push_back(MInst("NOP", rows[r].nops)); continue; }
            out.push_back(members[static_cast<std::size_t>(rows[r].node)]);
            out.back().par = rows[r].par;
        }
        block.clear();
        members.clear();
    };
    for (std::size_t k = 0; k < s.size(); k++) {
        const MInst &i = s[k];
        const bool label = !i.packed && labelLike(i, name);
        if (label && passThroughName(name, named)) { out.push_back(i); continue; }
        if (i.packed || i.kind != MInst::Instruction) { flush(label); out.push_back(i); continue; }
        block.push_back(makeNode(i, labels));
        members.push_back(i);
        if (isBranch(i.m.str())) flush(true);
    }
    flush(false);
    s.swap(out);
}

// **A branch and the NOPs after it are one BNOP**: C6xSched's fold, the unit a field and the count an operand. A label
// is folded only within BNOP's reach (`near`), a register only on the B side; a label branch in a packet names its unit.
void foldBranchNops(MStream &s, bool near) {
    for (std::size_t i = 0; i < s.size(); i++) {
        MInst &l = s[i];
        if (l.kind != MInst::Instruction || l.m.str() != "B" || l.operands != 1) continue;
        const char side = sideOf(l.op[0].str());           // the model's reading of a register, which the text's fold asks
        const bool reg = side != 0;
        if (reg && side != 'B') continue;
        std::size_t j = i + 1;
        while (j < s.size() && s[j].par) j++;
        // .S2 where the packet allows it: an unnamed BNOP asks for .S1 first, and the assembler moves one instruction at most.
        std::uint8_t unit = 2;
        if (!reg) {
            std::size_t first = i;
            while (first > 0 && s[first].par) first--;
            std::vector<Node> nodes;
            const std::set<std::string> none;
            for (std::size_t m = first; m < j; m++) nodes.push_back(makeNode(s[m], none));
            std::vector<const Node *> members;
            for (std::size_t m = 0; m < nodes.size(); m++) { if (m + first == i) nodes[m].side = 1; members.push_back(&nodes[m]); }
            bool taken[2][4] = { { false, false, false, false }, { false, false, false, false } };
            if (!assignUnits(members, 0, taken)) unit = 1;
        }
        const bool nop = j < s.size() && s[j].kind == MInst::Instruction && s[j].m.str() == "NOP" && s[j].operands == 1 &&
                         s[j].pred == kNoReg && s[j].op[0].kind == MOperand::Immediate;
        const int n = nop ? static_cast<int>(s[j].op[0].value) : 0, k = std::min(n, 5);
        if (k <= 0 || (!reg && !near)) {
            if (!reg && (l.par || (i + 1 < s.size() && s[i + 1].par))) l.unit = unit;
            continue;
        }
        l.m = opt::StrId("BNOP");
        l.unit = unit;
        l.op[l.operands++] = MOperand(k);
        if (n > k) s[j].op[0].value = n - k;
        else s.erase(s.begin() + static_cast<long>(j));
    }
}

}   // namespace c6x
