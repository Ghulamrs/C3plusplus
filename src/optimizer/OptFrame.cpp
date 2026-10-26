#include "OptPasses.h"

#include <algorithm>

namespace opt {

namespace {

// A general register, or a pseudo standing for one.
bool gpr(const Operand &o) {
    return o.kind == Operand::Register && o.reg.id >= 0 && (o.reg.id < kGprs || o.reg.id >= kPhysical);
}
bool xmm(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= kXmm0 && o.reg.id < kPhysical; }
bool frameSlot(const Operand &o) { return o.kind == Operand::Memory && o.reg.id == RBP && o.scale == 0; }

// The instructions whose frame operand a register can take as it stands.
bool renamable(const OpId &m) { return m.row().has(Opcode::kRenamable); }

// **How many bytes an instruction reads or writes at its frame operand**; 16
// where this cannot tell, which only ever makes a slot look more shared.
int accessWidth(const Instr &i, const Operand &at) {
    const Operand &other = &at == &i.a ? i.b : i.a;
    if (i.m.is({mn::Movslq, mn::Movss})) return 4;
    if (isPush(i.m) || i.m == mn::Movsd) return 8;
    if (renamable(i.m) && gpr(other)) return other.reg.width;
    if (renamable(i.m) && i.m.row().width != 0) return i.m.row().width;
    return 16;
}

// **How many bytes a scalar SSE instruction reads or writes at a frame operand an xmm register could take**: a
// move to or from an xmm register, or the source of an arithmetic, compare or conversion; 0 for anything else.
int floatingAccessWidth(const Instr &i, const Operand &at, const std::function<bool(const Operand &)> &isXmm) {
    const Operand &other = &at == &i.a ? i.b : i.a;
    const OpId m = i.m;
    if (m.is({mn::Movsd, mn::Movq})) return isXmm(other) ? 8 : 0;
    if (m.is({mn::Movss, mn::Movd})) return isXmm(other) ? 4 : 0;
    if (&at != &i.a) return 0;
    if (m.is({mn::Addsd, mn::Subsd, mn::Mulsd, mn::Divsd, mn::Ucomisd, mn::Comisd, mn::Cvtsd2ss, mn::Cvttsd2si})) return 8;
    if (m.is({mn::Addss, mn::Subss, mn::Mulss, mn::Divss, mn::Ucomiss, mn::Comiss, mn::Cvtss2sd, mn::Cvttss2si})) return 4;
    return 0;
}

struct Candidate {
    Local local;
    bool ok;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    Candidate(Local local = Local(), bool ok = true) : local(local), ok(ok) {}
};

}

void SharedSlots::addAccessesOf(const Stream &s) {
    for (const Entry &e : s) addAccessOf(e);
}

void SharedSlots::addAccessOf(const Entry &e) {
    if (e.kind != Entry::Ins || e.dead) return;
    for (const Operand *o : {&e.ins.a, &e.ins.b}) {
        if (!frameSlot(*o)) continue;
        if (e.ins.m == mn::Lea) addressTaken(o->disp);
        else add(o->disp, accessWidth(e.ins, *o));
    }
}

std::vector<Local> promotableLocals(const Stream &s, const std::vector<Local> &locals, const SharedSlots &shared,
                                    RegSet &mentioned, const std::function<bool(int)> &xmmPseudo) {
    std::vector<Candidate> cands;
    for (const Local &l : locals)
        if ((l.size == 4 || l.size == 8) && !shared.overlaps(l.disp, l.size)) cands.push_back(Candidate{l});
    const auto isXmm = [&](const Operand &o) {
        return xmm(o) || (o.kind == Operand::Register && o.reg.id >= kPhysical && xmmPseudo(o.reg.id));
    };
    mentioned = 0;
    auto overlapping = [&](long long disp, int width, const std::function<void(Candidate &)> &f) {
        for (Candidate &cd : cands)
            if (disp < cd.local.disp + cd.local.size && cd.local.disp < disp + width) f(cd);
    };
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (s[k].kind != Entry::Ins || s[k].dead) continue;
        const Instr &i = s[k].ins;
        if (i.m == mn::Call && i.a.text.find("setjmp") != std::string::npos) return {};
        for (const Operand *o : {&i.a, &i.b}) {
            if ((o->kind == Operand::Register || o->basedOnReg() || o->kind == Operand::Indirect) &&
                o->reg.id >= 0 && o->reg.id < kPhysical)
                mentioned |= bit(o->reg.id);
            if (o->indexed()) mentioned |= bit(o->index.id);
            // rbp read as a value, not restored nor handed to rsp, is the frame escaping.
            if (o->isReg(RBP) && i.m != mn::Pop && !(i.m == mn::Mov && i.b.isReg(RSP))) return {};
        }
        for (const Operand *o : {&i.a, &i.b}) {
            if (!frameSlot(*o)) continue;
            if (i.m == mn::Lea) {
                if (!i.b.isReg(RSP)) overlapping(o->disp, 1, [](Candidate &cd) { cd.ok = false; });
                continue;
            }
            const int fw = floatingAccessWidth(i, *o, isXmm);
            const int w = fw ? fw : accessWidth(i, *o);
            overlapping(o->disp, w, [&](Candidate &cd) {
                if (cd.local.disp != o->disp || cd.local.size != w) cd.ok = false;
                else if (cd.local.floating ? fw == 0 : !renamable(i.m) || isXmm(i.a) || isXmm(i.b)) cd.ok = false;
            });
        }
    }
    std::vector<Local> out;
    for (const Candidate &cd : cands) if (cd.ok) out.push_back(cd.local);
    return out;
}

void insertRestores(Stream &s, const std::vector<SavedReg> &saves) {
    Stream out;
    out.reserve(s.size() + 4 * saves.size());
    for (std::size_t k = 0; k < s.size(); ++k) {
        const Instr &i = s[k].ins;
        const bool leaving = s[k].kind == Entry::Ins && !s[k].dead && gpr(i.b) && i.b.reg.id == RSP &&
                             i.m.is({mn::Lea, mn::Mov}) && (i.a.isReg(RBP) || frameSlot(i.a));
        if (leaving)
            for (const SavedReg &sv : saves) {
                Entry r;
                r.ins = Instr{mn::Mov, Operand::ofMem(RBP, sv.disp), Operand::ofReg(parseReg(sv.reg).id, 8), 2};
                out.push_back(r);
            }
        out.push_back(std::move(s[k]));
    }
    s.swap(out);
}

void dropUnusedSaves(Stream &s, std::vector<SavedReg> &saves, long long top) {
    RegSet named = 0;
    for (const Entry &e : s)
        if (e.kind == Entry::Ins && !e.dead && !(e.ins.m == mn::Mov && e.ins.a.isMem() && e.ins.a.reg.id == RBP))
            for (const Operand *o : {&e.ins.a, &e.ins.b}) {
                if ((o->kind == Operand::Register || o->basedOnReg()) && o->reg.id >= 0) named |= bit(o->reg.id);
                if (o->indexed()) named |= bit(o->index.id);
            }
    std::vector<SavedReg> kept;
    for (const SavedReg &sv : saves) {
        const int r = parseReg(sv.reg).id;
        if (named & bit(r)) { kept.push_back(sv); continue; }
        for (Entry &e : s)
            if (e.kind == Entry::Ins && !e.dead && e.ins.b.isReg(r) && e.ins.a.isMem() && e.ins.a.disp == sv.disp)
                e.dead = true;
    }
    // The slots close up, the kept saves renumbered from the top.
    for (std::size_t j = 0; j < kept.size(); ++j) {
        const long long want = -(top + 8 * static_cast<long long>(j + 1));
        for (Entry &e : s)
            if (e.kind == Entry::Ins && !e.dead && e.ins.a.isMem() && e.ins.a.reg.id == RBP && e.ins.a.disp == kept[j].disp &&
                e.ins.b.isReg(parseReg(kept[j].reg).id))
                e.ins.a.disp = want;
        kept[j].disp = want;
    }
    saves.swap(kept);
}

bool removeDeadStores(Stream &s, const SharedSlots &shared) {
    // An object runs upward from its address, so an address taken at L may
    // reach anything above it in the frame.
    long long escapesFrom = 0;
    struct Access { long long disp; int width; };
    std::vector<Access> reads;
    auto isStore = [](const Instr &i) {
        return frameSlot(i.b) && i.b.disp < 0 && i.operands == 2 &&
               isMovAny(i.m) && (gpr(i.a) || i.a.kind == Operand::Immediate);
    };
    for (const Entry &e : s) {
        if (e.kind != Entry::Ins || e.dead) continue;
        const Instr &i = e.ins;
        for (const Operand *o : {&i.a, &i.b}) {
            // An indexed access reaches an unknown way up from its displacement.
            if (o->indexed() && o->reg.id == RBP) { escapesFrom = std::min(escapesFrom, o->disp); continue; }
            if (!frameSlot(*o)) continue;
            if (i.m == mn::Lea) { if (!i.b.isReg(RSP)) escapesFrom = std::min(escapesFrom, o->disp); continue; }
            if (o == &i.b && isStore(i)) continue;
            reads.push_back(Access{o->disp, accessWidth(i, *o)});
        }
    }
    bool changed = false;
    for (Entry &e : s) {
        if (e.kind != Entry::Ins || e.dead || !isStore(e.ins)) continue;
        const long long d = e.ins.b.disp;
        const int w = accessWidth(e.ins, e.ins.b);
        if (escapesFrom < 0 && d + w > escapesFrom) continue;
        if (shared.mayReach(d, w)) continue;
        bool read = false;
        for (const Access &r : reads) read = read || (r.disp < d + w && d < r.disp + r.width);
        if (!read) { e.dead = true; changed = true; }
    }
    return changed;
}

}
