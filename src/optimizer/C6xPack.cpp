// **The packetiser on the C6000's MIR** (M6c, doc-cxx3/M6-PLAN.md): a selected function's stream placed in execute
// packets - `par` joins an entry to the cycle before it, a `NOP n` entry is n idle cycles - by C6xSched's list scheduler.

#include "C6xMir.h"
#include "C6xModel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
