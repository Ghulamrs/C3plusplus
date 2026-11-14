// **The packetiser on the C6000's MIR** (M6c, doc-cxx3/M6-PLAN.md): a selected function's stream placed in execute
// packets - `par` joins an entry to the cycle before it, a `NOP n` entry is n idle cycles - by C6xSched's list scheduler.

#include "C6xMir.h"
#include "C6xModel.h"

#include <algorithm>
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

}   // namespace

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
        block.push_back(makeNode(lineOf(i), labels));
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
            for (std::size_t m = first; m < j; m++) nodes.push_back(makeNode(lineOf(s[m]), none));
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
