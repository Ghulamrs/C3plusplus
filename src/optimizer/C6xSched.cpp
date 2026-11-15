// The C6000 passes over one function's text: the -O0 NOPs are dropped, the sequential program is rewritten
// by the peepholes under a liveness of its blocks, and each block is list-scheduled into execute packets,
// the delay slots of its branch filled, and padded to the hazards that remain.

#include "C6xSched.h"
#include "C6xMir.h"
#include "C6xModel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <vector>

bool skipped(const char *pass);   // CPP11_C6XSKIP, below

namespace c6x {

bool startsWith(const std::string &s, const char *p) { return s.compare(0, std::string(p).size(), p) == 0; }
bool endsWith(const std::string &s, const char *p) {
    std::string q(p);
    return s.size() >= q.size() && s.compare(s.size() - q.size(), q.size(), q) == 0;
}

// Delay slots by mnemonic, as the -O0 backend pads them: the unknown floating
// forms take the longest of their precision rather than none.
int delaySlots(const std::string &m) {
    if (m == "B" || m == "CALLP") return 5;
    if (startsWith(m, "LD")) return 4;
    if (m == "MPYDP") return 9;
    if (m == "ADDDP" || m == "SUBDP") return 6;
    if (m == "INTDP" || m == "INTDPU") return 4;
    if (startsWith(m, "CMP") && (endsWith(m, "DP") || endsWith(m, "SP"))) return 1;
    if (m == "SPDP") return 1;
    if (m == "MPYSP" || m == "ADDSP" || m == "SUBSP") return 3;
    if (m == "INTSP" || m == "INTSPU" || m == "SPTRUNC" || m == "DPTRUNC" || m == "SPINT" || m == "DPINT" || m == "DPSP") return 3;
    if (startsWith(m, "MPY")) return 3;
    if (endsWith(m, "DP")) return 9;
    if (endsWith(m, "SP")) return 3;
    return 0;
}

// The C67x's double-precision instructions read a pair's high word after issue - one cycle later for ADDDP, SUBDP, CMPxxDP, MPYSPDP and the DPxx
// conversions, up to three for MPYDP, MPYI and MPYID - and hold their unit that long (SPRUFE8). TI's cycle-accurate simulator: 1.0 + 2.0 by ADDDP
// with A7 zeroed in its packet is 1, 3.0 * 2.0 by MPYDP with A7 zeroed in the next is 0 (tools/c6x-dphazards finds both in a schedule).
int lateReads(const std::string &m) {
    if (m == "MPYDP" || m == "MPYI" || m == "MPYID") return 3;
    if (m == "ADDDP" || m == "SUBDP" || m == "MPYSPDP" || m == "DPSP" || m == "DPINT" || m == "DPTRUNC") return 1;
    return startsWith(m, "CMP") && endsWith(m, "DP") ? 1 : 0;
}

Line parse(const std::string &raw) {
    Line l;
    l.raw = raw;
    if (raw.empty() || raw[0] != '\t') return l;        // a label, or blank
    std::size_t i = 1;
    if (i < raw.size() && raw[i] == '[') {
        std::size_t close = raw.find(']', i);
        if (close == std::string::npos) return l;
        l.pred = raw.substr(i + 1, close - i - 1);
        i = close + 1;
        while (i < raw.size() && raw[i] == '\t') i++;
    }
    if (i >= raw.size() || !std::isupper(static_cast<unsigned char>(raw[i]))) return l;   // a directive
    std::size_t tab = raw.find('\t', i);
    l.mnem = raw.substr(i, tab == std::string::npos ? std::string::npos : tab - i);
    if (tab != std::string::npos) {
        std::string rest = raw.substr(tab + 1);
        std::size_t at = 0;
        while (at <= rest.size()) {
            std::size_t comma = rest.find(", ", at);
            l.ops.push_back(rest.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
            if (comma == std::string::npos) break;
            at = comma + 2;
        }
    }
    l.instr = true;
    return l;
}

Line make(const std::string &mnem, const std::string &a, const std::string &b, const std::string &c) {
    std::string raw = "\t" + mnem + "\t" + a;
    if (!b.empty()) raw += ", " + b;
    if (!c.empty()) raw += ", " + c;
    return parse(raw);
}

// An instruction rebuilt from its parts, every operand kept, and the predicate with it.
Line rebuilt(const std::string &mnem, const std::vector<std::string> &ops, const std::string &pred) {
    std::string raw = "\t" + (pred.empty() ? std::string() : "[" + pred + "]\t") + mnem;
    for (std::size_t o = 0; o < ops.size(); o++) raw += (o == 0 ? "\t" : ", ") + ops[o];
    return parse(raw);
}

bool isNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.'; }

// The registers an operand names: A0-A31 and B0-B31 as whole words, so a
// label or symbol that happens to contain one is not counted.
void registersIn(const std::string &op, std::vector<std::string> &out) {
    for (std::size_t i = 0; i < op.size(); i++) {
        if ((op[i] != 'A' && op[i] != 'B') || (i > 0 && isNameChar(op[i - 1]))) continue;
        std::size_t j = i + 1;
        while (j < op.size() && std::isdigit(static_cast<unsigned char>(op[j]))) j++;
        if (j == i + 1 || (j < op.size() && isNameChar(op[j]))) continue;
        out.push_back(op.substr(i, j - i));
        i = j - 1;
    }
}

bool isStore(const std::string &m) { return startsWith(m, "ST"); }
bool isLoad(const std::string &m) { return startsWith(m, "LD"); }

// A register's file: 'A' or 'B', or 0 for anything else.
char sideOf(const std::string &r) { return r.size() >= 2 && (r[0] == 'A' || r[0] == 'B') && std::isdigit(static_cast<unsigned char>(r[1])) ? r[0] : 0; }
int sideIndex(const std::string &r) { return sideOf(r) == 'B' ? 1 : 0; }

// A register as one bit of a 64-bit set: A0-A31 low, B0-B31 high.
std::uint64_t bitOf(const std::string &r) {
    if (!sideOf(r)) return 0;
    int n = std::atoi(r.c_str() + 1);
    if (n < 0 || n > 31) return 0;
    return 1ull << (n + (r[0] == 'B' ? 32 : 0));
}
std::uint64_t maskOf(const std::vector<std::string> &regs) {
    std::uint64_t m = 0;
    for (std::size_t i = 0; i < regs.size(); i++) m |= bitOf(regs[i]);
    return m;
}
std::uint64_t maskOf(const char *const *names, std::size_t n) {
    std::uint64_t m = 0;
    for (std::size_t i = 0; i < n; i++) m |= bitOf(names[i]);
    return m;
}

// What the instruction reads and writes: the last operand is the destination
// unless it is memory (a store) or a branch target, and MVKH keeps its low half.
void readsAndWrites(const Line &l, std::vector<std::string> &reads, std::vector<std::string> &writes) {
    if (!l.pred.empty()) reads.push_back(l.pred[0] == '!' ? l.pred.substr(1) : l.pred);
    if (l.ops.empty()) return;
    bool hasDest = !isStore(l.mnem) && l.mnem != "B" && l.mnem != "NOP" && l.ops.back()[0] != '*';
    std::size_t n = hasDest ? l.ops.size() - 1 : l.ops.size();
    for (std::size_t i = 0; i < n; i++) registersIn(l.ops[i], reads);
    for (std::size_t i = 0; i < l.ops.size(); i++)      // `*R++`, `*R--`: the access writes its address register too
        if (l.ops[i][0] == '*' && (l.ops[i].find("++") != std::string::npos || l.ops[i].find("--") != std::string::npos)) registersIn(l.ops[i], writes);
    if (!hasDest) return;
    registersIn(l.ops.back(), writes);
    if (l.mnem == "MVKH" || l.mnem == "ADDK") registersIn(l.ops.back(), reads);
}

// The address register a `*R++(k)` or `*R--(k)` operand steps, and by how many bytes (signed), or "" for none.
std::string steppedRegister(const Line &l, long &by) {
    by = 0;
    for (std::size_t i = 0; i < l.ops.size(); i++) {
        const std::string &m = l.ops[i];
        std::size_t p = m.find("++");
        if (m.empty() || m[0] != '*' || (p == std::string::npos && (p = m.find("--")) == std::string::npos)) continue;
        std::vector<std::string> regs;
        registersIn(m, regs);
        if (regs.size() != 1) return "";
        std::size_t open = m.find('(', p);
        by = open == std::string::npos ? 1 : std::atol(m.substr(open + 1).c_str());
        if (m[p] == '-') by = -by;
        return regs[0];
    }
    return "";
}

bool has(const std::vector<std::string> &v, const std::string &r) {
    for (std::size_t i = 0; i < v.size(); i++) if (v[i] == r) return true;
    return false;
}

bool isNumber(const std::string &s) {
    std::size_t i = s.size() > 1 && s[0] == '-' ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); i++) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

bool isLabel(const Line &l) { return !l.instr && !l.raw.empty() && l.raw[0] != '\t'; }
std::string labelName(const Line &l) { return l.raw.substr(0, l.raw.find(':')); }
// CALLP is a branch that ends its block too, and a call: A4's rule that no push/pop pair spans a call rests on it.
bool isBranch(const std::string &m) { return m == "B" || m == "CALLP"; }
bool blockEnd(const Line &l) { return !l.instr || isBranch(l.mnem); }

// ----- the calls, the returns and what they read and write -----

const char *const kArgRegs[] = { "A3", "A4", "A5", "B4", "B5", "A6", "A7", "B6", "B7", "A8", "A9", "B8", "B9",
                                 "A10", "A11", "B10", "B11", "A12", "A13", "B12", "B13", "B3", "A15", "B15", "B14" };
const char *const kExitRegs[] = { "A4", "A5", "A10", "A11", "A12", "A13", "A14", "A15", "B3", "B10", "B11", "B12", "B13", "B14", "B15" };

// The registers a call reads (the EABI's argument registers, A3 the hidden result pointer, B3, the stack pointers) and clobbers.
std::uint64_t callReads() { return maskOf(kArgRegs, sizeof kArgRegs / sizeof kArgRegs[0]); }
std::uint64_t callWrites() {
    std::uint64_t m = 0;
    for (int n = 0; n < 32; n++) if (n <= 9 || n >= 16) m |= (1ull << n) | (1ull << (n + 32));
    return m;
}
std::uint64_t exitLive() { return maskOf(kExitRegs, sizeof kExitRegs / sizeof kExitRegs[0]); }

// A branch to a symbol that is no label of this text, or through a register other than B3, is a call.
bool isCallTo(const std::string &m, const std::string &t, const std::set<std::string> &labels) {
    if (m == "CALLP") return true;
    if (m != "B" || t.empty()) return false;
    if (sideOf(t)) return t != "B3";
    return labels.count(t) == 0 && !startsWith(t, "L$return$") && !startsWith(t, "L.return.");
}
bool isReturnTo(const std::string &m, const std::string &t, const std::set<std::string> &labels) {
    if (m != "B" || t.empty()) return false;
    if (sideOf(t)) return t == "B3";
    return labels.count(t) == 0 && (startsWith(t, "L$return$") || startsWith(t, "L.return."));
}
bool isCall(const Line &l, const std::set<std::string> &labels) { return isCallTo(l.mnem, l.ops.empty() ? "" : l.ops[0], labels); }

// ----- liveness over the blocks -----

// Every line takes the registers live at the end of its block: a label or a
// directive opens a block, a branch closes one, a call reads its arguments
// and clobbers the caller-saved registers, and a return leaves the exit set.
void liveness(const std::vector<LiveFacts> &v, std::vector<std::uint64_t> &liveIn, std::vector<std::uint64_t> &liveOut) {
    std::set<std::string> labels;
    for (std::size_t i = 0; i < v.size(); i++) if (v[i].label) labels.insert(v[i].name);
    std::vector<std::size_t> starts;                       // the first line of each block
    for (std::size_t i = 0; i < v.size(); i++)
        if (i == 0 || v[i].label || !v[i].instr || !v[i - 1].instr || isBranch(v[i - 1].mnem)) starts.push_back(i);
    std::size_t nb = starts.size();
    std::map<std::string, std::size_t> blockOfLabel;
    for (std::size_t b = 0; b < nb; b++) if (v[starts[b]].label) blockOfLabel[v[starts[b]].name] = b;
    std::vector<std::uint64_t> use(nb, 0), def(nb, 0), in(nb, 0), out(nb, 0);
    std::vector<std::vector<std::size_t> > succ(nb);
    std::vector<bool> exits(nb, false), unknown(nb, false);
    for (std::size_t b = 0; b < nb; b++) {
        std::size_t end = b + 1 < nb ? starts[b + 1] : v.size();
        bool fall = true;
        for (std::size_t i = starts[b]; i < end; i++) {
            const LiveFacts &l = v[i];
            if (!l.instr) continue;
            std::uint64_t r = l.reads, w = l.writes;
            if (isBranch(l.mnem)) {
                if (isCallTo(l.mnem, l.target, labels)) { r |= callReads(); w = callWrites(); if (l.mnem == "CALLP") { r &= ~bitOf("B3"); w |= bitOf("B3"); } }
                else if (isReturnTo(l.mnem, l.target, labels)) { exits[b] = true; fall = false; }
                else if (labels.count(l.target)) { succ[b].push_back(blockOfLabel[l.target]); if (!l.predicated) fall = false; }
                else { unknown[b] = true; if (!l.predicated) fall = false; }
            }
            use[b] |= r & ~def[b];
            if (!l.predicated) def[b] |= w;
        }
        if (fall && b + 1 < nb) succ[b].push_back(b + 1);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t b = nb; b-- > 0;) {
            std::uint64_t o = exits[b] ? exitLive() : 0;
            if (unknown[b] || v[starts[b]].kept) o = ~0ull;
            for (std::size_t k = 0; k < succ[b].size(); k++) o |= in[succ[b][k]];
            std::uint64_t n = v[starts[b]].kept ? ~0ull : use[b] | (o & ~def[b]);
            if (o != out[b] || n != in[b]) { out[b] = o; in[b] = n; changed = true; }
        }
    }
    liveIn.assign(v.size(), 0);
    liveOut.assign(v.size(), 0);
    for (std::size_t b = 0; b < nb; b++) {
        std::size_t end = b + 1 < nb ? starts[b + 1] : v.size();
        for (std::size_t i = starts[b]; i < end; i++) { liveOut[i] = out[b]; liveIn[i] = in[b]; }
    }
}

LiveFacts liveFacts(const Line &l) {
    LiveFacts f;
    f.instr = l.instr;
    f.label = isLabel(l);
    f.kept = l.verbatim;
    f.predicated = !l.pred.empty();
    if (f.label) f.name = labelName(l);
    if (!l.instr) return f;
    f.mnem = l.mnem;
    if (!l.ops.empty()) f.target = l.ops[0];
    std::vector<std::string> reads, writes;
    readsAndWrites(l, reads, writes);
    f.reads = maskOf(reads);
    f.writes = maskOf(writes);
    return f;
}

int accessSize(const std::string &m) {
    if (m == "LDDW" || m == "STDW") return 8;   // LDW ends in DW too
    if (endsWith(m, "W")) return 4;
    if (endsWith(m, "H") || endsWith(m, "HU")) return 2;
    return 1;
}

std::string renamed(const std::string &op, const std::string &from, const std::string &to) {
    std::string out;
    for (std::size_t i = 0; i < op.size();) {
        std::size_t j = i;
        if ((op[i] == 'A' || op[i] == 'B') && (i == 0 || !isNameChar(op[i - 1]))) {
            j = i + 1;
            while (j < op.size() && std::isdigit(static_cast<unsigned char>(op[j]))) j++;
            if (j > i + 1 && (j >= op.size() || !isNameChar(op[j])) && op.substr(i, j - i) == from) {
                out += to;
                i = j;
                continue;
            }
        }
        out += op[i++];
    }
    return out;
}

// Whether operand o of this instruction may come over the cross path: TI's src2 - the second source of the
// arithmetic, either of a commutative one or a compare (asm6x turns them about), the value of a shift or EXT
// (written first) - and never a count, a field instruction's source (EXTU, CLR), or a conversion's.
bool mayCross(const std::string &m, std::size_t o) {
    if (m == "ADD" || m == "AND" || m == "OR" || m == "XOR" || m == "SUB" || startsWith(m, "CMP")) return true;
    if (m == "MV" || m == "NEG" || m == "NOT" || m == "SPDP" || m == "INTSP" || m == "INTDP" || m == "SPTRUNC") return true;
    if (m == "SHL" || m == "SHR" || m == "SHRU") return o == 0;
    if (startsWith(m, "MPY") || m == "ADDSP" || m == "SUBSP" || m == "ADDDP" || m == "SUBDP") return o == 1;
    return false;
}

unsigned unitsFor(const Line &l);

// A pair crosses only as the second source of MPYDP, ADDDP or SUBDP - the xdp forms ASM6x's table has (ADDDP and SUBDP on .S alone).
bool pairMayCross(const std::string &m) { return m == "MPYDP" || m == "ADDDP" || m == "SUBDP"; }

// How many of an instruction's source registers sit on the other side from
// its destination - at most one may, over the cross path, and only in a
// position that has a crossed form; 2 answers for anything illegal.
int crossings(const Line &l) {
    if (l.ops.empty() || isStore(l.mnem) || isLoad(l.mnem) || isBranch(l.mnem)) return 0;
    std::vector<std::string> dst;
    registersIn(l.ops.back(), dst);
    if (dst.empty()) return 0;
    int n = 0;
    for (std::size_t o = 0; o + 1 < l.ops.size(); o++) {
        std::vector<std::string> regs;
        registersIn(l.ops[o], regs);
        bool crossed = false;
        for (std::size_t k = 0; k < regs.size(); k++) if (sideOf(regs[k]) != sideOf(dst[0])) crossed = true;
        if (crossed && (!mayCross(l.mnem, o) || (regs.size() > 1 && !pairMayCross(l.mnem)))) return 2;
        if (crossed) n++;
    }
    return n;
}

// The aligned pair `odd:even` the two registers make, lo the even one, or "" where they are not one pair of one side.
std::string pairOf(const std::string &lo, const std::string &hi) {
    if (!sideOf(lo) || sideOf(lo) != sideOf(hi)) return "";
    int a = std::atoi(lo.c_str() + 1), b = std::atoi(hi.c_str() + 1);
    return a % 2 == 0 && b == a + 1 ? hi + ":" + lo : "";
}

// A rebuilt instruction with one legal crossing at most: a commutative one turns its sources about where the first crosses.
bool legalForm(Line &r) {
    const std::string &m = r.mnem;
    const bool commutes = m == "ADD" || m == "AND" || m == "OR" || m == "XOR" || m == "MPYDP" || m == "ADDDP" || m == "MPYSP" || m == "ADDSP" || m == "MPY32" || m == "CMPEQ";
    if (crossings(r) > 1 && commutes && r.ops.size() == 3 && !isNumber(r.ops[0]) && !isNumber(r.ops[1])) {
        std::vector<std::string> ops = r.ops;
        std::swap(ops[0], ops[1]);
        r = rebuilt(m, ops, r.pred);
    }
    return crossings(r) <= 1 && (unitsFor(r) != 0 || isLoad(m) || isStore(m));
}

// A register-to-register instruction whose operands may be renamed: no
// memory operand, and no pair - a half of A5:A4 cannot be renamed alone.
bool renameable(const Line &l) {
    if (!l.instr || isBranch(l.mnem) || isStore(l.mnem) || isLoad(l.mnem) || l.mnem == "ADDK") return false;   // ADDK reads its destination
    for (std::size_t o = 0; o < l.ops.size(); o++) if (l.ops[o].find(':') != std::string::npos || l.ops[o][0] == '*') return false;
    return true;
}

// The other half of an aligned pair, and the register it is copied from or to beside this copy: `MV S, L` wants `MV S^1, L^1`.
std::string partner(const std::string &r) {
    if (!sideOf(r)) return "";
    int n = std::atoi(r.c_str() + 1);
    return std::string(1, r[0]) + std::to_string(n ^ 1);
}

// Whether line j reads or writes any of the registers named, the predicate included.
bool touches(const Line &l, const std::vector<std::string> &regs) {
    std::vector<std::string> reads, writes;
    readsAndWrites(l, reads, writes);
    for (std::size_t k = 0; k < regs.size(); k++) if (has(reads, regs[k]) || has(writes, regs[k])) return true;
    return false;
}

bool passThrough(const Line &l, const std::set<std::string> &named);

// The one register a load's address reads as it stands (`*R`, or displaced forward by a constant), or "" for any other.
std::string plainBase(const MOperand &a) {
    if (a.kind == MOperand::Memory) return a.mode == MOperand::Base && a.index == kNoReg && !(a.hasOffset && a.value < 0) ? regName(a.reg) : std::string();
    const std::string s = a.str();
    std::vector<std::string> addr;
    if (!s.empty() && s[0] == '*') registersIn(s, addr);
    return addr.size() == 1 && s.find('+') == std::string::npos && s.find('-') == std::string::npos ? addr[0] : std::string();
}

// **A byte read for the test is not read again for the work**: a rotated loop whose block opens with `LDx *R, X`
// and ends - after R's last write - with the same `LDx *R, X` that its test reads, entered from a block whose
// last write of X is that load too, opens without the load: on either path X holds `*R` already. `for (; *p; ++p)`.
void reuseEntryLoads(LoopText &t) {
    const MStream &s = t.s;
    std::map<std::string, std::size_t> at;
    std::string name;
    for (std::size_t i = 0; i < s.size(); i++) if (t.label(i, name)) at[name] = i;
    for (std::size_t i = 0; i < s.size(); i++) {
        const MInst &b = s[i];
        if (!t.instr(i) || b.m.str() != "B" || b.pred == kNoReg || b.operands == 0 || !at.count(b.op[0].str())) continue;
        const std::size_t h = at[b.op[0].str()];
        if (h >= i || h + 1 >= s.size()) continue;
        const MInst &top = s[h + 1];
        if (!t.instr(h + 1) || !isLoad(top.m.str()) || top.operands != 2 || top.pred != kNoReg) continue;
        const std::string R = plainBase(top.op[0]), X = top.op[1].str(), load = top.str();
        if (R.empty() || X.find(':') != std::string::npos) continue;
        // One block: no label between; the same load again after R's last write, nothing after it touching R, X or memory.
        std::size_t last = 0;
        bool ok = true;
        for (std::size_t k = h + 2; k < i && ok; k++) {
            if (!t.instr(k) || isBranch(s[k].m.str())) { ok = false; break; }
            if (s[k].str() == load) { last = k; continue; }
            std::vector<std::string> r, w;
            t.rw(k, r, w);
            if (last != 0 && (has(w, R) || has(w, X) || isStore(s[k].m.str()))) last = 0;
        }
        if (!ok || last == 0) continue;
        // The entry: back from the head over one block's fall-through, the last write of X the same load, memory untouched.
        bool entry = false;
        for (std::size_t k = h; k-- > 0;) {
            if (!t.instr(k) || (isBranch(s[k].m.str()) && s[k].pred == kNoReg)) break;
            if (s[k].str() == load) { entry = true; break; }
            std::vector<std::string> r, w;
            t.rw(k, r, w);
            if (has(w, R) || has(w, X) || isStore(s[k].m.str()) || s[k].m.str() == "CALLP") break;
        }
        if (!entry) continue;
        t.blank(h + 1);
    }
    t.compact();
}

// **A constant made inside a loop nest is made before it**: `MVKL sym, d; MVKH sym, d`, the only writer of d
// anywhere in a loop's region - inner loops and all - with d not live into the head and no branch into
// the region but the back edge to its head, goes in front of the head. `comp` in sieve's outer loop.
void hoistConstantPairs(LoopText &t) {
    std::vector<Line> &v = t.v;
    std::map<std::string, std::size_t> at;
    std::map<std::string, int> named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) at[labelName(v[i])] = i;
        if (v[i].instr) for (std::size_t o = 0; o < v[i].ops.size(); o++) named[v[i].ops[o]]++;
    }
    std::set<std::string> hoistedHeads;                    // a region holding one of these has been rewritten already
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &b = v[i];
        if (!b.instr || b.mnem != "B" || !at.count(b.ops[0]) || named[b.ops[0]] != 1) continue;
        const std::size_t h = at[b.ops[0]];
        if (h >= i || h == 0 || !v[h - 1].instr || v[h - 1].mnem != "B" || v[h - 1].pred.empty()) continue;
        bool ok = true;                                     // every branch into the region starts inside it
        std::set<std::string> inside;
        for (std::size_t k = h; k <= i; k++) if (isLabel(v[k])) inside.insert(labelName(v[k]));
        for (std::size_t k = 0; k < v.size() && ok; k++)
            if ((k < h || k > i) && v[k].instr && v[k].mnem == "B" && inside.count(v[k].ops[0])) ok = false;
        for (std::size_t k = h + 1; k < i && ok; k++) if ((!v[k].instr && !isLabel(v[k])) || (isLabel(v[k]) && hoistedHeads.count(labelName(v[k])))) ok = false;
        if (!ok) continue;
        const std::uint64_t headIn = t.liveIn(h);
        std::map<std::string, int> writers;
        for (std::size_t k = h + 1; k < i; k++) {
            if (!v[k].instr) continue;
            std::vector<std::string> r, w;
            readsAndWrites(v[k], r, w);
            if (v[k].mnem == "CALLP" || (v[k].mnem == "B" && !inside.count(v[k].ops[0]) && !at.count(v[k].ops[0]))) { writers["*"]++; }
            for (std::size_t x = 0; x < w.size(); x++) writers[w[x]]++;
        }
        if (writers.count("*")) continue;                   // a call clobbers what a constant would sit in
        std::set<std::string> used;
        for (std::size_t k = h; k <= i; k++) if (v[k].instr) { std::vector<std::string> r, w; readsAndWrites(v[k], r, w); used.insert(r.begin(), r.end()); used.insert(w.begin(), w.end()); }
        std::vector<Line> hoisted;
        for (std::size_t k = h + 1; k + 1 < i; k++) {
            const Line &l = v[k], &m = v[k + 1];
            if (!l.instr || l.mnem != "MVKL" || l.ops.size() != 2 || !l.pred.empty() || !m.instr || m.mnem != "MVKH" || m.ops != l.ops || !m.pred.empty()) continue;
            const std::string d = l.ops[1];
            if (!sideOf(d) || d == "A15" || d == "B15" || d == "B3" || d == "B14") continue;
            std::string f = d;
            std::vector<std::pair<std::size_t, Line> > web;         // the readers renamed, where d is written elsewhere too
            if (writers[d] != 2 || (headIn & bitOf(d))) {
                // A fresh register the region never names and the head is not entered with: the pair's value in
                // it, its readers up to d's next write in this straight run renamed, or the pair stays.
                f.clear();
                for (int side = 0; side < 2 && f.empty(); side++)
                    for (int n = 16; n < 32 && f.empty(); n++) {
                        const std::string c = std::string(sideOf(d) == 'A' ? (side ? "B" : "A") : (side ? "A" : "B")) + std::to_string(n);
                        if (!used.count(c) && !(headIn & bitOf(c))) f = c;
                    }
                if (f.empty()) continue;
                bool ok = true, ended = false;
                for (std::size_t x = k + 2; x < i && ok && !ended; x++) {
                    const Line &u = v[x];
                    if (!u.instr || isBranch(u.mnem)) { ok = false; break; }
                    std::vector<std::string> r, w;
                    readsAndWrites(u, r, w);
                    if (has(w, d)) ended = true;
                    if (!has(r, d)) continue;
                    if (!u.pred.empty() || u.mnem == "MVKH" || u.mnem == "ADDK") { ok = false; break; }
                    std::vector<std::string> ops = u.ops;
                    for (std::size_t o = 0; o < ops.size(); o++) {
                        if (ops[o].find(':') != std::string::npos) { ok = false; break; }
                        const bool read = o + 1 < ops.size() || isStore(u.mnem) || ops[o][0] == '*';
                        if (read && !(o + 1 == ops.size() && !isStore(u.mnem) && ops[o][0] != '*')) {
                            if (ops[o][0] == '*' && sideOf(f) != sideOf(d) && renamed(ops[o], d, f) != ops[o]) { ok = false; break; }
                            ops[o] = renamed(ops[o], d, f);
                        }
                    }
                    if (!ok) break;
                    Line r2 = rebuilt(u.mnem, ops, u.pred);
                    if (crossings(r2) > 1 || (unitsFor(r2) == 0 && !isLoad(r2.mnem) && !isStore(r2.mnem))) { ok = false; break; }
                    web.push_back(std::make_pair(x, r2));
                }
                if (!ok || !ended) continue;
            }
            Line a = l, b2 = m;
            a.ops[1] = f; b2.ops[1] = f;
            hoisted.push_back(rebuilt(a.mnem, a.ops));
            hoisted.push_back(rebuilt(b2.mnem, b2.ops));
            for (std::size_t x = 0; x < web.size(); x++) t.set(web[x].first, web[x].second);
            used.insert(f);
            t.blank(k);
            t.blank(k + 1);
            k++;
        }
        if (hoisted.empty()) continue;
        hoistedHeads.insert(b.ops[0]);
        i = t.apply(h, hoisted, i);
        at.clear();
        for (std::size_t k = 0; k < v.size(); k++) if (isLabel(v[k])) at[labelName(v[k])] = k;
    }
}

// What the loop passes ask of operands, held by the model or as text: an address, a pair, two entries' spelled alike but the last `but`.
bool isAddress(const MOperand &o) { return o.kind == MOperand::Memory || (o.kind == MOperand::Symbol && o.text[0] == '*'); }
bool isPair(const MOperand &o) { return o.kind == MOperand::Pair || (o.kind == MOperand::Symbol && o.text.find(':') != std::string::npos); }
bool sameOperands(const MInst &a, const MInst &b, int but = 0) {
    if (a.operands != b.operands) return false;
    for (int o = 0; o + but < a.operands; o++) if (a.op[o].str() != b.op[o].str()) return false;
    return true;
}

// The stream's labels, and every operand an instruction spells (a label named there is no pass-through).
void namesOf(const LoopText &t, std::set<std::string> &labels, std::set<std::string> &named) {
    std::string name;
    for (std::size_t i = 0; i < t.s.size(); i++) {
        if (t.label(i, name)) labels.insert(name);
        if (t.instr(i)) for (int o = 0; o < t.s[i].operands; o++) named.insert(t.s[i].op[o].str());
    }
}

// The head a predicated back edge at i returns to - a label before it that this one branch names - or i for none.
std::size_t loopHead(const LoopText &t, std::size_t i, const std::set<std::string> &labels) {
    const MStream &s = t.s;
    if (!t.instr(i) || s[i].m != "B" || s[i].pred == kNoReg || s[i].operands == 0 || !labels.count(s[i].op[0].str())) return i;
    const std::string head = s[i].op[0].str();
    std::size_t h = i; int uses = 0;
    std::string name;
    for (std::size_t k = 0; k < i; k++) if (t.label(k, name) && name == head) h = k;
    for (std::size_t k = 0; k < s.size(); k++) if (t.instr(k)) for (int o = 0; o < s[k].operands; o++) if (s[k].op[o].kind == MOperand::Symbol && s[k].op[o].text == head) uses++;
    return uses == 1 && h != 0 ? h : i;
}

// The loop from head h to back edge i as one body: it may leave by a conditional branch to a label outside it - an
// exit - and by nothing else; its labels pass-through ones.
bool loopBody(const LoopText &t, std::size_t h, std::size_t i, const std::set<std::string> &labels, const std::set<std::string> &named,
              std::vector<std::size_t> &body, std::vector<std::size_t> &exits) {
    const MStream &s = t.s;
    std::set<std::string> inside;
    std::string name;
    for (std::size_t k = h; k <= i; k++) if (t.label(k, name)) inside.insert(name);
    for (std::size_t k = h + 1; k < i; k++) {
        if (t.label(k, name)) { if (!passThroughName(name, named)) return false; continue; }
        if (!t.instr(k)) return false;
        if (!isBranch(s[k].m.str())) { body.push_back(k); continue; }
        if (s[k].m != "B" || s[k].pred == kNoReg || s[k].operands == 0 || inside.count(s[k].op[0].str()) || !labels.count(s[k].op[0].str())) return false;
        exits.push_back(k);
    }
    return true;
}

// **A loop's invariants move to its preheader**: in a rotated loop - one block, entered by falling into its
// head from the test that guards it - an instruction whose sources the loop never writes is computed once
// before the head; a destination the loop also writes elsewhere takes a fresh register for that one value.
void hoistInvariants(LoopText &t) {
    const MStream &s = t.s;
    std::set<std::string> labels, named;
    namesOf(t, labels, named);
    for (std::size_t i = 0; i < s.size(); i++) {
        const std::size_t h = loopHead(t, i, labels);
        if (h == i || !t.instr(h - 1) || s[h - 1].m != "B" || s[h - 1].pred == kNoReg) continue;
        std::vector<std::size_t> body, exits;
        if (!loopBody(t, h, i, labels, named, body, exits)) continue;
        // The liveness of the loop as it is entered: its own edits rename what they hoist, never what is asked about after.
        const std::uint64_t backOut = t.liveOut(i);
        std::vector<std::uint64_t> exitOut(exits.size());
        std::uint64_t liveAtExits = backOut;
        for (std::size_t e = 0; e < exits.size(); e++) liveAtExits |= exitOut[e] = t.liveOut(exits[e]);
        std::set<std::string> used;
        for (std::size_t b = 0; b < body.size(); b++) {
            std::vector<std::string> reads, writes;
            t.rw(body[b], reads, writes);
            for (std::size_t r = 0; r < reads.size(); r++) used.insert(reads[r]);
            for (std::size_t w = 0; w < writes.size(); w++) used.insert(writes[w]);
        }
        std::vector<std::string> pool;
        for (int side = 0; side < 2; side++)
            for (int k = 3; k < 32; k++) {
                if ((k >= 10 && k <= 15) || (side == 1 && k == 3)) continue;
                std::string r = std::string(side ? "B" : "A") + std::to_string(k);
                if (!used.count(r) && !(liveAtExits & bitOf(r))) pool.push_back(r);
            }
        MStream hoisted;
        std::vector<bool> gone(body.size(), false);
        for (bool changed = true; changed;) {
            changed = false;
            std::map<std::string, int> writers;                 // the loop's writes per register, this round
            for (std::size_t b = 0; b < body.size(); b++) {
                if (gone[b]) continue;
                std::vector<std::string> reads, writes;
                t.rw(body[b], reads, writes);
                for (std::size_t w = 0; w < writes.size(); w++) writers[writes[w]]++;
            }
            for (std::size_t b = 0; b < body.size() && !changed; b++) {
                const MInst &l = s[body[b]];
                const std::string &lm = l.m.str();
                if (gone[b] || l.pred != kNoReg || isLoad(lm) || isStore(lm) || lm == "MVKH" || lm == "ADDK" || l.operands < 2) continue;
                std::vector<std::string> reads, writes;
                t.rw(body[b], reads, writes);
                if (writes.size() != 1) continue;
                const std::string d = writes[0];
                if (d == "A15" || d == "B15" || d == "B3" || d == "B14" || has(reads, d)) continue;
                bool pair = lm == "MVKL" && b + 1 < body.size() && !gone[b + 1] && s[body[b + 1]].m == "MVKH" && sameOperands(s[body[b + 1]], l) && s[body[b + 1]].pred == kNoReg;
                if (lm == "MVKL" && !pair) continue;
                bool invariant = true;
                for (std::size_t r = 0; r < reads.size(); r++) if (writers.count(reads[r])) invariant = false;
                if (!invariant) continue;
                // The value's web: its reads up to the next write of d; none may come before the loop's first write.
                std::size_t next = body.size(), first = b;
                for (std::size_t k = 0; k < body.size(); k++) {
                    if (gone[k] || k == b || (pair && k == b + 1)) continue;
                    std::vector<std::string> r2, w2;
                    t.rw(body[k], r2, w2);
                    if (has(w2, d)) { if (k < first) first = k; if (k > b && k < next) next = k; }
                }
                bool early = false;
                for (std::size_t k = 0; k < first; k++) { std::vector<std::string> r2, w2; t.rw(body[k], r2, w2); if (has(r2, d)) early = true; }
                if (early && next == body.size()) continue;                        // this value reaches the next turn
                if (next < body.size() && (s[body[next]].m == "MVKH" || s[body[next]].m == "ADDK")) continue;
                // The web renamed to f: every reader's operands, the next writer's sources too; false where a
                // reader cannot take f - a memory operand or a predicate, or a crossing the form lacks.
                auto renameWeb = [&](const std::string &f, bool apply) -> bool {
                    MInst own = l;                                             // the hoisted instruction's own form
                    own.op[own.operands - 1] = MOperand(f);
                    if (!legal(own)) return false;
                    const bool moves = f != d, crosses = sideOf(f) != sideOf(d);
                    // A branch in the web - an exit, or the back edge itself - may read d as its predicate, which keeps its register.
                    for (std::size_t x = body[b] + 1; x <= (next < body.size() ? body[next] : i); x++)
                        if (moves && t.instr(x) && isBranch(s[x].m.str()) && s[x].pred != kNoReg && regName(s[x].pred) == d) return false;
                    for (std::size_t k = b + 1; k <= next && k < body.size(); k++) {
                        if (gone[k] || (pair && k == b + 1)) continue;
                        MInst u = s[body[k]];
                        const std::string &um = u.m.str();
                        const int n = u.operands, last = n == 0 || isStore(um) || um == "B" || isAddress(u.op[n - 1]) ? n : n - 1;
                        for (int o = 0; o < n; o++) {
                            if (!(o < last || isAddress(u.op[o]) || (k != next && (um == "MVKH" || um == "ADDK")))) continue;
                            const std::string was = u.op[o].str(), now = renamed(was, d, f);
                            if (now == was) continue;
                            if ((crosses && isAddress(u.op[o])) || isPair(u.op[o])) return false;
                            u.op[o] = MOperand(now);
                        }
                        if (moves && u.pred != kNoReg && regName(u.pred) == d) return false;
                        if (crosses && !legal(u)) return false;
                        if (apply) t.set(body[k], u);
                    }
                    return true;
                };
                // The same value hoisted already - the same instruction over the same sources - is reused.
                std::string f;
                std::uint64_t liveAfter = backOut, liveBefore = 0;   // d at the exits past this write, and before it
                for (std::size_t e = 0; e < exits.size(); e++) (exits[e] > body[b] ? liveAfter : liveBefore) |= exitOut[e];
                if (liveBefore & bitOf(d)) continue;                       // an exit before the write reads the value coming in
                const bool leaves = next == body.size() && (liveAfter & bitOf(d));
                if (!leaves) for (std::size_t e = 0; e < hoisted.size() && f.empty(); e++) {
                    const MInst &m = hoisted[e];
                    if (m.m != l.m || !sameOperands(m, l, 1)) continue;
                    if (pair && !(e + 1 < hoisted.size() && hoisted[e + 1].m == "MVKH" && sameOperands(hoisted[e + 1], m))) continue;
                    const std::string to = m.op[m.operands - 1].str();
                    if (renameWeb(to, false)) f = to;
                }
                const bool reuse = !f.empty();
                if (!reuse) {
                    if (writers[d] <= (pair ? 2 : 1)) f = d;
                    else {
                        if (leaves) continue;                                      // the exit reads this value
                        for (int side = 1; side >= 0 && f.empty(); side--)
                            for (std::size_t k = 0; k < pool.size() && f.empty(); k++)
                                if (sideIndex(pool[k]) == side && renameWeb(pool[k], false)) { f = pool[k]; pool.erase(pool.begin() + static_cast<long>(k)); }
                        if (f.empty()) continue;
                    }
                }
                if (f != d) renameWeb(f, true);
                if (reuse) { for (std::size_t k = b; k < b + (pair ? 2u : 1u); k++) gone[k] = true; changed = true; continue; }
                for (std::size_t k = b; k < b + (pair ? 2u : 1u); k++) {
                    MInst m = s[body[k]];
                    m.op[m.operands - 1] = MOperand(f);
                    hoisted.push_back(m);
                    gone[k] = true;
                }
                changed = true;
            }
        }
        if (hoisted.empty()) continue;
        for (std::size_t b = 0; b < body.size(); b++) if (gone[b]) t.blank(body[b]);
        i = t.apply(h, hoisted, i);
    }
}

// **An address stepped with the counter is a pointer the access steps itself**: in a rotated loop whose counter iv is stepped
// once by `ADD 1, iv, iv`, `SHL iv, k, S; ADD base, S, T; LDx/STx *T` - or `ADD base, iv, T` - with base never written in the
// loop and S, T read nowhere else, is `*P++(1<<k)`, P made once in the preheader: T where nothing else writes or exits with it.
void inductionPointers(LoopText &t) {
    std::vector<Line> &v = t.v;
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || v[i].pred.empty() || !labels.count(v[i].ops[0])) continue;
        std::size_t h = i; int uses = 0;
        for (std::size_t k = 0; k < i; k++) if (isLabel(v[k]) && labelName(v[k]) == v[i].ops[0]) h = k;
        for (std::size_t k = 0; k < v.size(); k++) if (v[k].instr) for (std::size_t o = 0; o < v[k].ops.size(); o++) if (v[k].ops[o] == v[i].ops[0]) uses++;
        if (h == i || uses != 1 || h == 0) continue;
        // Entered by falling in from the guard's branch over a straight run - the preheader, where the pointers are made.
        std::size_t g = h;
        while (g > 0 && v[g - 1].instr && !isBranch(v[g - 1].mnem)) g--;
        if (g == 0 || !v[g - 1].instr || v[g - 1].mnem != "B" || v[g - 1].pred.empty()) continue;
        bool ok = true;
        std::vector<std::size_t> body, exits;
        std::set<std::string> inside;
        for (std::size_t k = h; k <= i; k++) if (isLabel(v[k])) inside.insert(labelName(v[k]));
        for (std::size_t k = h + 1; k < i && ok; k++) {
            if (isLabel(v[k])) { ok = passThrough(v[k], named); continue; }
            if (!v[k].instr) ok = false;
            else if (isBranch(v[k].mnem)) { if (v[k].mnem != "B" || v[k].pred.empty() || inside.count(v[k].ops[0]) || !labels.count(v[k].ops[0])) ok = false; else exits.push_back(k); }
            else body.push_back(k);
        }
        if (!ok) continue;
        // The liveness of the loop as it is entered: its own edits step pointers it chose from registers free here.
        const std::uint64_t headIn = t.liveIn(h);
        std::uint64_t liveAtExits = t.liveOut(i);
        for (std::size_t e = 0; e < exits.size(); e++) liveAtExits |= t.liveOut(exits[e]);
        std::map<std::string, int> writers, readers;
        std::vector<std::vector<std::string> > reads(body.size()), writes(body.size());
        std::set<std::string> used;
        for (std::size_t b = 0; b < body.size(); b++) {
            readsAndWrites(v[body[b]], reads[b], writes[b]);
            for (std::size_t r = 0; r < reads[b].size(); r++) { readers[reads[b][r]]++; used.insert(reads[b][r]); }
            for (std::size_t w = 0; w < writes[b].size(); w++) { writers[writes[b][w]]++; used.insert(writes[b][w]); }
        }
        // The counter: stepped once, by one.
        std::string iv; std::size_t stepAt = 0;
        for (std::size_t b = 0; b < body.size(); b++) {
            const Line &l = v[body[b]];
            if (l.mnem != "ADD" || !l.pred.empty() || l.ops.size() != 3 || writers[l.ops[2]] != 1) continue;
            if ((l.ops[0] == "1" && l.ops[1] == l.ops[2]) || (l.ops[1] == "1" && l.ops[0] == l.ops[2])) { if (!iv.empty()) { iv.clear(); break; } iv = l.ops[2]; stepAt = b; }
        }
        if (iv.empty()) continue;
        std::vector<std::string> pool;
        for (int side = 0; side < 2; side++)
            for (int k = 3; k < 32; k++) {
                if ((k >= 10 && k <= 15) || (side == 1 && k == 3)) continue;
                std::string r = std::string(side ? "B" : "A") + std::to_string(k);
                if (!used.count(r) && !(liveAtExits & bitOf(r)) && !(headIn & bitOf(r))) pool.push_back(r);
            }
        // The last write of r before b, with no read of r between it and b but at b itself; body.size() for none.
        auto reaches = [&](const std::string &r, std::size_t b) -> std::size_t {
            for (std::size_t k = b; k-- > 0;) {
                if (has(writes[k], r)) return k;
                if (has(reads[k], r)) return body.size();
            }
            return body.size();
        };
        // Whether the value r holds at b is read again after b, before the loop writes r anew.
        auto readAgain = [&](const std::string &r, std::size_t b) -> bool {
            for (std::size_t k = b + 1; k < body.size(); k++) {
                if (has(reads[k], r)) return true;
                if (has(writes[k], r)) return false;
            }
            return (liveAtExits & bitOf(r)) != 0 || (headIn & bitOf(r)) != 0;
        };
        std::vector<Line> entry;
        std::set<std::size_t> gone;
        for (std::size_t b = 0; b < body.size(); b++) {
            const Line &m = v[body[b]];
            if (!m.pred.empty() || m.ops.size() != 2 || (!isLoad(m.mnem) && !isStore(m.mnem))) continue;
            const std::string &addr = isStore(m.mnem) ? m.ops[1] : m.ops[0];
            if (addr.size() < 2 || addr[0] != '*' || !sideOf(addr.substr(1))) continue;
            const std::string T = addr.substr(1);
            std::vector<std::string> data;
            registersIn(m.ops[isStore(m.mnem) ? 0 : 1], data);
            if (has(data, T) || T == "A15" || T == "B15" || T == iv || readAgain(T, b)) continue;
            const std::size_t ta = reaches(T, b);
            if (ta >= body.size() || gone.count(ta)) continue;
            const Line &add = v[body[ta]];
            if (add.mnem != "ADD" || !add.pred.empty() || add.ops.size() != 3 || isNumber(add.ops[0]) || isNumber(add.ops[1])) continue;
            // One source is the counter, or a shift of it reaching here alone; the other the base, which the loop never writes.
            std::string S, base; long k = 0; std::size_t sa = ta; bool found = false;
            for (int side = 0; side < 2 && !found; side++) {
                const std::string &cand = add.ops[static_cast<std::size_t>(side)], &other = add.ops[static_cast<std::size_t>(1 - side)];
                if (!sideOf(cand) || !sideOf(other) || writers.count(other) || cand == other || other == iv) continue;
                if (cand == iv) { base = other; found = true; break; }
                const std::size_t sh = reaches(cand, ta);
                if (sh >= body.size() || gone.count(sh) || (cand != T && readAgain(cand, ta))) continue;   // `ADD base, S, S` reads S once more, as T
                const Line &shl = v[body[sh]];
                if (shl.mnem == "SHL" && shl.pred.empty() && shl.ops.size() == 3 && shl.ops[0] == iv && isNumber(shl.ops[1]) && cand != iv) {
                    S = cand; k = std::atol(shl.ops[1].c_str()); sa = sh; base = other; found = true;
                }
            }
            if (!found || k < 0 || k > 3 || base == T) continue;
            const long size = accessSize(m.mnem), stride = 1L << k;
            if (stride % size != 0 || stride / size > 31) continue;
            // The pointer: T where this is its only write and no exit reads it, else a register the loop never names.
            std::string P = T;
            if (writers[T] != 1 || (liveAtExits & bitOf(T)) || (headIn & bitOf(T))) {
                P.clear();
                for (std::size_t f = 0; f < pool.size() && P.empty(); f++) if (sideOf(pool[f]) == sideOf(T)) { P = pool[f]; pool.erase(pool.begin() + static_cast<long>(f)); }
                if (P.empty()) continue;
            }
            // The address the first turn uses: base + (iv << k), one more stride where the shift reads the stepped counter.
            const bool afterStep = stepAt < sa;
            Line first = S.empty() ? make("ADD", base, iv, P) : make("SHL", iv, std::to_string(k), P);
            if (!legalForm(first)) continue;
            Line second = make("ADD", base, P, P);
            if (!S.empty() && !legalForm(second)) continue;
            entry.push_back(first);
            if (!S.empty()) entry.push_back(second);
            if (afterStep) entry.push_back(make("ADD", std::to_string(stride), P, P));
            std::vector<std::string> ops = m.ops;
            ops[isStore(m.mnem) ? 1 : 0] = "*" + P + "++(" + std::to_string(stride) + ")";
            if (std::getenv("CPP11_INDUCT")) std::fprintf(stderr, "induct%s -> P=%s stride=%ld base=%s S=%s after=%d\n", m.raw.c_str(), P.c_str(), stride, base.c_str(), S.c_str(), afterStep ? 1 : 0);
            t.set(body[b], rebuilt(m.mnem, ops));
            gone.insert(ta);
            if (!S.empty()) gone.insert(sa);
        }
        if (entry.empty()) continue;
        for (std::set<std::size_t>::const_iterator it = gone.begin(); it != gone.end(); ++it) t.blank(body[*it]);
        i = t.apply(h, entry, i);
    }
}

// **A loop that fills memory with one value stores doublewords**: a rotated loop of `ADD 1, iv, iv`, `STB|STH|STW V,
// *P++(k)` and the compare of iv against a bound the loop never writes - K elements, known at entry - becomes, past 15
// elements, a loop up to the next 8-byte boundary, `STDW` of the value replicated, and the elements left over.
void widenFills(LoopText &t) {
    std::vector<Line> &v = t.v;
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || v[i].pred.empty() || !labels.count(v[i].ops[0]) || !isLabel(v[i + 1])) continue;
        std::size_t h = i; int uses = 0;
        for (std::size_t k = 0; k < i; k++) if (isLabel(v[k]) && labelName(v[k]) == v[i].ops[0]) h = k;
        for (std::size_t k = 0; k < v.size(); k++) if (v[k].instr) for (std::size_t o = 0; o < v[k].ops.size(); o++) if (v[k].ops[o] == v[i].ops[0]) uses++;
        if (h == i || uses != 1 || h == 0) continue;
        std::size_t g = h;
        while (g > 0 && v[g - 1].instr && !isBranch(v[g - 1].mnem)) g--;
        if (g == 0 || !v[g - 1].instr || v[g - 1].mnem != "B" || v[g - 1].pred.empty()) continue;
        std::vector<std::size_t> body;
        bool ok = true;
        for (std::size_t k = h + 1; k < i && ok; k++) {
            if (isLabel(v[k])) { ok = passThrough(v[k], named); continue; }
            if (!v[k].instr || !v[k].pred.empty()) ok = false; else body.push_back(k);
        }
        if (!ok || body.size() != 3) continue;
        // The three: the step, the store through the stepped pointer, and the compare the branch reads.
        std::string iv, V, P, n, C = v[i].pred[0] == '!' ? v[i].pred.substr(1) : v[i].pred;
        const bool negated = v[i].pred[0] == '!';
        long k = 0; std::size_t stepAt = 0, cmpAt = 0, storeAt = 0; bool plusOne = false;
        for (std::size_t b = 0; b < 3; b++) {
            const Line &l = v[body[b]];
            if (l.mnem == "ADD" && l.ops.size() == 3 && ((l.ops[0] == "1" && l.ops[1] == l.ops[2]) || (l.ops[1] == "1" && l.ops[0] == l.ops[2])) && iv.empty()) { iv = l.ops[2]; stepAt = b; }
            else if (isStore(l.mnem) && l.ops.size() == 2 && P.empty() && (l.mnem == "STB" || l.mnem == "STH" || l.mnem == "STW")) {
                long by = 0;
                P = steppedRegister(l, by); V = l.ops[0]; k = accessSize(l.mnem); storeAt = b;
                if (by != k || P.empty() || !sideOf(V)) P.clear();
            } else if ((l.mnem == "CMPLT" || l.mnem == "CMPGT") && l.ops.size() == 3 && l.ops[2] == C && cmpAt == 0 && b > 0) cmpAt = b;
        }
        if (iv.empty() || P.empty() || cmpAt == 0 || cmpAt < stepAt) continue;
        const Line &c = v[body[cmpAt]];
        const bool lt = c.mnem == "CMPLT";
        if (lt == !negated) { if (c.ops[0] != iv) continue; n = c.ops[1]; }
        else { if (c.ops[1] != iv) continue; n = c.ops[0]; }
        plusOne = negated;
        if (n == iv || n == P || n == V || P == iv || V == iv || V == P || P == "A15" || P == "B15" || (!isNumber(n) && !sideOf(n))) continue;
        if (storeAt == cmpAt || (t.liveOut(i) & bitOf(C)) || (t.liveIn(i + 1) & bitOf(C))) continue;
        const std::uint64_t liveAtExits = t.liveOut(i) | t.liveIn(i + 1), liveHead = t.liveIn(h);
        std::set<std::string> used;
        used.insert(iv); used.insert(P); used.insert(V); used.insert(C); if (!isNumber(n)) used.insert(n);
        const char S = sideOf(P), SV = sideOf(V);
        // Seven registers beside the pointer and a condition, from what the loop never names and nothing after it reads.
        auto takeReg = [&](char side, bool pair) -> std::string {
            for (int kk = 3; kk < 32; kk++) {
                if ((kk >= 10 && kk <= 15) || (side == 'B' && kk == 3) || (pair && kk % 2)) continue;
                std::string r = std::string(1, side) + std::to_string(kk), r2 = std::string(1, side) + std::to_string(kk + 1);
                if (used.count(r) || (liveAtExits & bitOf(r)) || (liveHead & bitOf(r))) continue;
                if (pair && (kk + 1 >= 32 || used.count(r2) || (liveAtExits & bitOf(r2)) || (liveHead & bitOf(r2)))) continue;
                used.insert(r); if (pair) used.insert(r2);
                return r;
            }
            return "";
        };
        std::string cond;
        for (int kk = 0; kk < 3 && cond.empty(); kk++) {
            std::string r = std::string(1, S) + std::to_string(kk);
            if (!used.count(r) && !(liveAtExits & bitOf(r)) && !(liveHead & bitOf(r))) { cond = r; used.insert(r); }
        }
        const std::string K = takeReg(S, false), A = takeReg(S, false), M = takeReg(S, false), T = takeReg(S, false), X = takeReg(S, false), lo = takeReg(SV, true);
        if (cond.empty() || K.empty() || A.empty() || M.empty() || T.empty() || X.empty() || lo.empty()) continue;
        const std::string hi = std::string(1, lo[0]) + std::to_string(std::atoi(lo.c_str() + 1) + 1);
        const int log2k = k == 1 ? 0 : k == 2 ? 1 : 2, per = 3 - log2k;
        std::vector<Line> out;
        bool legal = true;
        // A line with a form, a crossed first source brought over through X where the machine has none for it.
        auto put = [&](Line l) {
            if (legalForm(l)) { out.push_back(l); return; }
            if (l.ops.size() == 3 && sideOf(l.ops[0]) && sideOf(l.ops[0]) != sideOf(l.ops[2])) {
                Line mv = make("MV", l.ops[0], X), again = rebuilt(l.mnem, std::vector<std::string>{ X, l.ops[1], l.ops[2] }, l.pred);
                if (legalForm(mv) && legalForm(again)) { out.push_back(mv); out.push_back(again); return; }
            }
            legal = false;
        };
        // Each loop is named `$fill`: memory-bound on this part, so the pipeliner leaves them as written rather than unroll them.
        const std::string base = labelName(v[h]), Lh = base + "$fh$fill", Lw = base + "$fw", Lk = base + "$fw$fill", Lt = base + "$ft$fill", exit = labelName(v[i + 1]);
        auto branch = [&](const std::string &to, const std::string &pred) { out.push_back(rebuilt("B", std::vector<std::string>(1, to), pred)); };
        if (isNumber(n)) {
            const long nk = std::atol(n.c_str());
            if (nk >= -32768 && nk <= 32767) put(make("MVK", n, K)); else { put(make("MVKL", n, K)); put(make("MVKH", n, K)); }
            put(make("SUB", K, iv, K));
        } else put(make("SUB", n, iv, K));
        if (plusOne) put(make("ADD", "1", K, K));
        put(make("CMPGT", "15", K, cond)); branch(base, cond);
        if (k == 1) { put(rebuilt("EXTU", std::vector<std::string>{ V, "24", "24", lo })); put(make("SHL", lo, "8", hi)); put(make("OR", lo, hi, lo)); put(make("SHL", lo, "16", hi)); put(make("OR", lo, hi, lo)); put(make("MV", lo, hi)); }
        else if (k == 2) { put(rebuilt("EXTU", std::vector<std::string>{ V, "16", "16", lo })); put(make("SHL", lo, "16", hi)); put(make("OR", lo, hi, lo)); put(make("MV", lo, hi)); }
        else { put(make("MV", V, lo)); put(make("MV", V, hi)); }
        put(make("NEG", P, A)); put(make("AND", "7", A, A)); if (log2k) put(make("SHR", A, std::to_string(log2k), A));
        put(make("SUB", K, A, M)); put(make("SHR", M, std::to_string(per), M));
        put(make("SHL", M, std::to_string(per), T)); put(make("SUB", K, T, T)); put(make("SUB", T, A, T));
        put(make("ADD", K, iv, iv));
        put(make("CMPEQ", "0", A, cond)); branch(Lw, cond);
        out.push_back(parse(Lh + ":"));
        out.push_back(v[body[storeAt]]); put(make("SUB", A, "1", A)); put(make("CMPGT", A, "0", cond)); branch(Lh, cond);
        out.push_back(parse(Lh + "$x:"));
        out.push_back(parse(Lw + ":"));
        put(make("MVK", "0", X));
        out.push_back(parse(Lk + ":"));
        put(make("STDW", hi + ":" + lo, "*" + P + "++(8)")); put(make("ADD", "1", X, X)); put(make("CMPLT", X, M, cond)); branch(Lk, cond);
        out.push_back(parse(Lk + "$x:"));
        put(make("CMPEQ", "0", T, cond)); branch(exit, cond);
        out.push_back(parse(Lt + ":"));
        out.push_back(v[body[storeAt]]); put(make("SUB", T, "1", T)); put(make("CMPGT", T, "0", cond)); branch(Lt, cond);
        out.push_back(parse(Lt + "$x:"));
        branch(exit, "");
        if (!legal) continue;
        if (std::getenv("CPP11_FILL")) std::fprintf(stderr, "fill %s: %s of %s through %s, %ld-byte elements\n", base.c_str(), v[body[storeAt]].mnem.c_str(), V.c_str(), P.c_str(), k);
        i = t.apply(h, out, i);
    }
}

// ----- the list scheduler -----


// The units a mnemonic has a form on for these operands, from asm6x's table
// (ASM6x src/forms.h): the three-operand arithmetic on any of .L .S .D with a
// register, the small constants on .L and .S, the wider ones on .D alone.
unsigned unitsFor(const Line &l) {
    const std::string &m = l.mnem;
    if (isBranch(m) || m == "ADDK") return US;
    if (isLoad(m) || isStore(m)) return UD;
    if (m == "MPY32" || m == "MPY32U" || m == "MPYSP" || m == "MPYDP" || startsWith(m, "MPY")) return UM;
    bool pair = false;
    for (std::size_t o = 0; o < l.ops.size(); o++) if (l.ops[o].find(':') != std::string::npos) pair = true;
    long k = 0; int at = -1;
    if (l.ops.size() == 3) { if (isNumber(l.ops[0])) { at = 0; k = std::atol(l.ops[0].c_str()); } else if (isNumber(l.ops[1])) { at = 1; k = std::atol(l.ops[1].c_str()); } }
    bool small = at >= 0 && k >= -16 && k <= 15, ucst = at >= 0 && k >= 0 && k <= 31;
    if (m == "ADD" || m == "AND" || m == "OR" || m == "XOR") {
        if (pair) return UL;
        if (at < 0) return UL | US | UD;
        if (small) return UL | US | (ucst ? UD : 0);
        return ucst && m == "ADD" && crossings(l) == 0 ? UD : 0;
    }
    if (m == "SUBAW" || m == "ADDAW") return crossings(l) == 0 ? UD : 0;   // .D alone, the sources on its side
    if (m == "SUB") {
        if (pair) return UL;
        if (at < 0) {               // a crossed first source has the .S form and .L's crossed-first one, never .D's
            std::vector<std::string> first, dst;
            registersIn(l.ops[0], first); registersIn(l.ops[2], dst);
            bool firstCrosses = !first.empty() && !dst.empty() && sideOf(first[0]) != sideOf(dst[0]);
            return firstCrosses ? UL | US : UL | US | UD;
        }
        if (at == 0 && small) return UL | US;
        if (at == 1 && ucst && crossings(l) == 0) return UD;
        return 0;
    }
    if (m == "NOT") return UL | US | UD;
    if (m == "NEG") return UL | US;
    if (m == "MV") return pair ? UL : UL | US | UD;
    if (m == "ZERO") return UL;
    if (m == "CLR" || m == "EXT" || m == "EXTU" || m == "SHL" || m == "SHR" || m == "SHRU") return US;
    if (m == "MVK") return isNumber(l.ops[0]) && std::atol(l.ops[0].c_str()) >= -16 && std::atol(l.ops[0].c_str()) <= 15 ? UL | US : US;
    if (m == "MVKL" || m == "MVKH") return US;
    if (m == "CMPEQ" || m == "CMPGT" || m == "CMPLT" || m == "CMPGTU" || m == "CMPLTU") return UL;
    if (endsWith(m, "SP") || endsWith(m, "DP")) {
        if (startsWith(m, "CMP") || m == "SPDP") return US;
        if (m == "ADDDP" || m == "SUBDP") return crossings(l) > 0 ? US : UL | US;   // the xdp form is .S's alone
        if (m == "ADDSP" || m == "SUBSP") return UL | US;
        if (m == "DPSP" || m == "SPTRUNC" || m == "DPTRUNC" || startsWith(m, "INT")) return UL;
    }
    return 0;
}

// The frame slot a memory operand names, `*-A15(k)` or `*+A15(k)`, so two such accesses that do not overlap need no order.
bool frameSlot(const std::string &op, long &off) {
    if (op == "*A15") { off = 0; return true; }
    if (!startsWith(op, "*-A15(") && !startsWith(op, "*+A15(")) return false;
    std::string k = op.substr(6, op.size() - 7);
    if (!isNumber(k)) return false;
    off = std::atol(k.c_str()) * (op[1] == '-' ? -1 : 1);
    return true;
}

Node makeNode(const Line &l, const std::set<std::string> &labels) {
    Node n;
    n.callp = l.mnem == "CALLP";
    std::vector<std::string> reads, writes;
    readsAndWrites(l, reads, writes);
    n.reads = maskOf(reads);
    n.writes = maskOf(writes);
    n.lat = delaySlots(l.mnem) + 1;
    n.late = lateReads(l.mnem);
    n.units = unitsFor(l);
    if (isBranch(l.mnem)) {
        n.branch = true;
        n.call = isCall(l, labels);
        if (n.call) { n.reads |= callReads(); n.writes |= callWrites(); }
        if (l.mnem == "CALLP") { n.reads &= ~bitOf("B3"); n.side = 1; return n; }
        if (sideOf(l.ops[0])) { n.side = 1; if (sideOf(l.ops[0]) == 'A') n.cross = 1; }
        return n;
    }
    if (isLoad(l.mnem) || isStore(l.mnem)) {
        n.mem = true;
        n.store = isStore(l.mnem);
        const std::string &addr = n.store ? l.ops[1] : l.ops[0];
        const std::string &data = n.store ? l.ops[0] : l.ops[1];
        std::vector<std::string> a, d;
        registersIn(addr, a);
        registersIn(data, d);
        n.side = a.empty() ? 0 : sideIndex(a[0]);
        n.tpath = d.empty() ? 0 : sideIndex(d[0]);
        n.memSize = accessSize(l.mnem);
        n.memFrame = frameSlot(addr, n.memOff);
        long by;
        n.stepped = bitOf(steppedRegister(l, by));
        return n;
    }
    std::vector<std::string> dst;
    if (!l.ops.empty()) registersIn(l.ops.back(), dst);
    n.side = dst.empty() ? 0 : sideIndex(dst[0]);
    if (crossings(l) > 0) n.cross = n.side;
    return n;
}

// Two memory accesses are independent when both are frame slots that do not overlap.
bool mayAlias(const Node &a, const Node &b) {
    if (!a.memFrame || !b.memFrame) return true;
    return a.memOff < b.memOff + b.memSize && b.memOff < a.memOff + a.memSize;
}

// Whether every member, with the candidate added, can be given a unit: a
// depth-first assignment over the four letters of a side, the side of a
// branch to a label chosen too.
bool assignUnits(const std::vector<const Node *> &m, std::size_t k, bool taken[2][4]) {
    if (k == m.size()) return true;
    const Node *n = m[k];
    for (int side = 0; side < 2; side++) {
        if (n->side >= 0 && n->side != side) continue;
        for (int u = 0; u < 4; u++) {
            if (!(n->units & (1u << u)) || taken[side][u]) continue;
            taken[side][u] = true;
            if (assignUnits(m, k + 1, taken)) { taken[side][u] = false; return true; }
            taken[side][u] = false;
        }
    }
    return false;
}

bool fits(const Packet &p, const Node &n) {
    if (p.alone || p.count >= 8) return false;
    if (n.units == 0) return p.count == 0;
    if (n.branch && p.branch) return false;
    if (n.cross >= 0 && p.cross[n.cross]) return false;
    if (n.mem && p.tpath[n.tpath]) return false;
    if (n.writes & p.writes) return false;
    std::vector<const Node *> m = p.members;
    m.push_back(&n);
    bool taken[2][4];
    for (int s = 0; s < 2; s++) for (int u = 0; u < 4; u++) taken[s][u] = p.held[s][u];
    return assignUnits(m, 0, taken);
}

void add(Packet &p, const Node &n) {
    p.members.push_back(&n);
    p.count++;
    if (n.units == 0) p.alone = true;
    if (n.branch) p.branch = true;
    if (n.cross >= 0) p.cross[n.cross] = true;
    if (n.mem) p.tpath[n.tpath] = true;
    p.writes |= n.writes;
}

// Idle cycles as rows: a multi-cycle NOP holds nine at most.
void idle(std::vector<Row> &rows, int nops) {
    for (; nops > 0; nops -= std::min(nops, 9)) { Row r = { -1, std::min(nops, 9), false }; rows.push_back(r); }
}

// One block - the instructions between two labels, ending in its branch if it has one - scheduled by list scheduling: the edges between instructions
// are the register and memory orders with their distances in cycles, each cycle takes the ready instructions of greatest height that its packet has
// units for, and the branch goes as early as leaves every other instruction issued in its window and landed by the time its target runs.
std::vector<Pending> scheduleBlock(std::vector<Node> &nodes, std::vector<Row> &rows, const std::vector<Pending> &in, bool carry) {
    const std::size_t n = nodes.size();
    int branchAt = -1, landing = 0;
    std::vector<int> minIssue(n, 0);                     // what the block before still has in flight
    for (std::size_t p = 0; p < in.size(); p++) {
        landing = std::max(landing, in[p].lands);
        for (std::size_t i = 0; i < n; i++) {
            if (nodes[i].reads & in[p].writes) minIssue[i] = std::max(minIssue[i], in[p].lands);
            if (nodes[i].writes & in[p].writes) minIssue[i] = std::max(minIssue[i], in[p].lands - nodes[i].lat + 1);
        }
    }
    for (std::size_t i = 0; i < n; i++) if (nodes[i].branch) branchAt = static_cast<int>(i);
    for (std::size_t i = 0; i < n; i++) {
        Node &b = nodes[i];
        for (std::size_t j = 0; j < i; j++) {
            const Node &a = nodes[j];
            int d = -1;
            if (a.writes & b.reads) d = std::max(d, (a.writes & ~a.stepped & b.reads) ? a.lat : 1);
            if (a.reads & b.writes) d = std::max(d, std::max(a.store ? 1 : 0, a.late - b.lat + 1));
            if (a.writes & b.writes) d = std::max(d, std::max(1, a.lat - b.lat + 1));
            if (a.mem && b.mem && (a.store || b.store) && mayAlias(a, b)) d = std::max(d, 1);
            if (d >= 0) b.preds.push_back(std::make_pair(static_cast<int>(j), d));
        }
    }
    for (std::size_t i = n; i-- > 0;) {
        Node &a = nodes[i];
        a.height = a.lat;
        for (std::size_t k = i + 1; k < n; k++)
            for (std::size_t e = 0; e < nodes[k].preds.size(); e++)
                if (nodes[k].preds[e].first == static_cast<int>(i)) a.height = std::max(a.height, nodes[k].preds[e].second + nodes[k].height);
    }
    std::vector<Packet> cycles;
    std::size_t left = n - (branchAt >= 0 ? 1 : 0);
    for (int c = 0; left > 0; c++) {
        if (static_cast<int>(cycles.size()) <= c) cycles.push_back(Packet());
        for (;;) {
            Packet &p = cycles[static_cast<std::size_t>(c)];   // taken afresh: holding a unit may grow the vector
            int best = -1;
            for (std::size_t i = 0; i < n; i++) {
                Node &x = nodes[i];
                if (x.issue >= 0 || x.branch || c < minIssue[i]) continue;
                bool ready = true;
                for (std::size_t e = 0; e < x.preds.size() && ready; e++) {
                    const Node &y = nodes[static_cast<std::size_t>(x.preds[e].first)];
                    if (y.branch) continue;
                    if (y.issue < 0 || y.issue + x.preds[e].second > c) ready = false;
                }
                if (!ready || !fits(p, x)) continue;
                if (best < 0 || x.height > nodes[static_cast<std::size_t>(best)].height) best = static_cast<int>(i);
            }
            if (best < 0) break;
            Node &chosen = nodes[static_cast<std::size_t>(best)];
            chosen.issue = c;
            add(p, chosen);
            left--;
            // A DP instruction holds its unit on its side - each it might be given - for its late cycles, and a crossed pair the cross path.
            for (int k = 1; k <= chosen.late; k++) {
                while (static_cast<int>(cycles.size()) <= c + k) cycles.push_back(Packet());
                for (int side = 0; side < 2; side++) {
                    if (chosen.side >= 0 && chosen.side != side) continue;
                    for (int u = 0; u < 4; u++) if (chosen.units & (1u << u)) cycles[static_cast<std::size_t>(c + k)].held[side][u] = true;
                }
                if (chosen.cross >= 0) cycles[static_cast<std::size_t>(c + k)].cross[chosen.cross] = true;
            }
        }
    }
    int length = 0;
    for (std::size_t i = 0; i < n; i++) if (!nodes[i].branch) length = std::max(length, nodes[i].issue + nodes[i].lat);
    if (branchAt >= 0) {
        Node &b = nodes[static_cast<std::size_t>(branchAt)];
        int at = std::max(0, landing - 6);
        for (std::size_t e = 0; e < b.preds.size(); e++) {
            const Node &y = nodes[static_cast<std::size_t>(b.preds[e].first)];
            at = std::max(at, y.issue + b.preds[e].second);
        }
        for (std::size_t i = 0; i < n; i++) {
            if (nodes[i].branch) continue;
            at = std::max(at, nodes[i].issue - (b.callp ? 0 : 5));   // issued in the window, and CALLP's is NOPs
            at = std::max(at, nodes[i].issue + nodes[i].lat - 6);  // landed when the target runs
        }
        for (;;) {
            while (static_cast<int>(cycles.size()) <= at) cycles.push_back(Packet());
            if (fits(cycles[static_cast<std::size_t>(at)], b)) break;
            at++;
        }
        b.issue = at;
        add(cycles[static_cast<std::size_t>(at)], b);
        length = b.callp ? at + 1 : at + 6;
    }
    // Falling into a label: the block ends at its last issue, its late landings handed on; else it waits for them.
    std::vector<Pending> pend;
    if (carry && branchAt < 0) {
        length = 0;
        for (std::size_t i = 0; i < n; i++) length = std::max(length, nodes[i].issue + 1 + nodes[i].late);
        for (std::size_t i = 0; i < n; i++)
            if (nodes[i].issue + nodes[i].lat > length) { Pending p = { nodes[i].writes, nodes[i].issue + nodes[i].lat - length }; pend.push_back(p); }
        for (std::size_t p = 0; p < in.size(); p++)
            if (in[p].lands > length) { Pending q = { in[p].writes, in[p].lands - length }; pend.push_back(q); }
    } else length = std::max(length, landing);
    int nops = 0;
    for (int c = 0; c < length; c++) {
        const Packet *p = c < static_cast<int>(cycles.size()) ? &cycles[static_cast<std::size_t>(c)] : nullptr;
        if (!p || p->count == 0) { nops++; continue; }
        idle(rows, nops);
        nops = 0;
        for (std::size_t k = 0; k < p->members.size(); k++) { Row r = { static_cast<int>(p->members[k] - &nodes[0]), 0, k != 0 }; rows.push_back(r); }
    }
    idle(rows, nops);
    return pend;
}

// A block that is empty and not fallen out of: the writes still in flight are waited for.
void waitOut(std::vector<Pending> &pend, std::vector<Row> &rows) {
    int wait = 0;
    for (std::size_t p = 0; p < pend.size(); p++) wait = std::max(wait, pend[p].lands);
    idle(rows, wait);
    pend.clear();
}

// A label of the backend's own flow - the kinds only a branch of this text
// names - that no instruction names is no block boundary: the instructions
// on either side are scheduled together and the label is kept in front.
bool passThrough(const Line &l, const std::set<std::string> &named) { return passThroughName(labelName(l), named); }
bool passThroughName(const std::string &name, const std::set<std::string> &named) {
    static const char *const kinds[] = { ".begin", ".step", ".end", ".else", ".shortcut", ".wide", ".widend", ".noresult", ".case", ".default", ".inline", ".ssain" };
    if (named.count(name)) return false;     // spelled with dots here; the TI spelling makes them $ later
    std::size_t d = name.find_last_not_of("0123456789");
    if (d == std::string::npos || d + 1 >= name.size()) return false;
    std::size_t k = name.rfind('.', d);
    if (k == std::string::npos) return false;
    const std::string kind = name.substr(k, d + 1 - k);
    for (std::size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) if (kind == kinds[i]) return true;
    return false;
}

}   // namespace c6x

using namespace c6x;

// CPP11_C6XSKIP names passes (comma-separated) to leave out, for bisecting a wrong answer to its pass.
bool skipped(const char *pass) {
    const char *e = std::getenv("CPP11_C6XSKIP");
    return e != nullptr && (std::string(",") + e + ",").find(std::string(",") + pass + ",") != std::string::npos;
}

// The five folds (C6xPack.cpp) to a fixed point, on one stream held across the rounds.
void rounds(MStream &s) {
    LiveSpan live;
    for (int round = 0; round < 8; round++) {
        liveness(s, live.in, live.out);
        bool changed = c6x::forwardMoves(s, live);
        liveness(s, live.in, live.out);
        changed = (!skipped("pairmv") && c6x::forwardPairMoves(s, live)) || changed;
        liveness(s, live.in, live.out);
        changed = c6x::forwardConstants(s, live) || changed;
        liveness(s, live.in, live.out);
        changed = c6x::flipPredicates(s, live) || changed;
        liveness(s, live.in, live.out);
        changed = (!skipped("dead") && c6x::removeDead(s, live)) || changed;
        if (!changed) break;
    }
}

std::string c6xSchedule(const std::string &text, int level, bool &spelled) {
    spelled = true;
    if (level <= 0) return text;
    std::vector<Line> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        Line l = parse(line);
        if (l.mnem != "NOP") lines.push_back(l);    // the -O0 padding, re-derived by the scheduler
    }
    // A function is one stream from here to the packetiser, the selector's and the walker's alike; a line the stream
    // does not spell is refused by the caller, the text handed back as it came.
    MStream s;
    spelled = streamOf(lines, s);
    if (!spelled) return text;
    foldFront(s, !skipped("thread"), !skipped("labels"));
    rounds(s);
    if (!skipped("arms") && predicateArms(s)) rounds(s);
    tidyStream(s, !skipped("labels"));
    if (!skipped("entry")) loopPass(s, reuseEntryLoads);
    if (level >= 2) {
        loopPass(s, hoistInvariants);
        if (!skipped("induct")) loopPass(s, inductionPointers);
        if (!skipped("fill")) loopPass(s, widenFills);
        // The loops are pipelined on the stream; a kernel whose entries do not spell is refused as a line is.
        spelled = pipelineLoops(s);
        if (!spelled) return text;
        int placed = 0;
        for (std::size_t k = 0; k < s.size(); k++) placed += s[k].packed ? 1 : 0;
        if (placed != 0 && std::getenv("CXX1_C6X_MIR") != nullptr) std::fprintf(stderr, "c6x-kernel: %d placed entries\n", placed);
        if (!skipped("pairs")) loopPass(s, hoistConstantPairs);
    }
    // After the loops are made: a value numbered across a pipelined kernel would lengthen the spans it schedules.
    if (!skipped("vn")) numberValues(s);
    if (std::getenv("CPP11_C6XTRACE")) {
        std::fprintf(stderr, "--- after numberValues\n");
        for (std::size_t i = 0; i < s.size(); i++) std::fprintf(stderr, "%s", s[i].str().c_str());
    }
    rounds(s);
    foldAddressing(s, !skipped("index"));
    packetise(s);
    foldBranchNops(s, s.size() * 4 + 512 < 8192);
    std::ostringstream packed;
    write(s, packed);
    return packed.str();
}
