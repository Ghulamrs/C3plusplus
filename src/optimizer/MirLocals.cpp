#include "MirLocals.h"

#include "OptPasses.h"

#include <map>

namespace mir {

using namespace opt;

// The candidates are promoteLocals' own - not shared, never addressed,
// every access whole and by an instruction that takes a register there.
int Locals::promote() {
    RegSet mentioned = 0;
    const auto xmmPseudo = [&](int id) { return fn_.pseudos[id - kFirstPseudo].cls == RegClass::Xmm; };
    const std::vector<Local> found = promotableLocals(fn_.stream, fn_.locals, fn_.shared, mentioned, xmmPseudo);
    if (found.empty()) return 0;
    std::map<long long, int> pseudoOf;
    for (const Local &l : found) {
        pseudoOf[l.disp] = kFirstPseudo + static_cast<int>(fn_.pseudos.size());
        fn_.pseudos.push_back(PseudoInfo(l.floating ? RegClass::Xmm : RegClass::Gpr, -1, l));
    }
    for (int k = 0; k < static_cast<int>(fn_.stream.size()); ++k) {
        Entry &e = fn_.stream[k];
        if (e.kind != Entry::Ins || e.dead) continue;
        bool renamed = false, floating = false;
        for (Operand *o : {&e.ins.a, &e.ins.b}) {
            if (o->kind != Operand::Memory || o->reg.id != RBP) continue;
            const auto p = pseudoOf.find(o->disp);
            if (p == pseudoOf.end()) continue;
            const Local &l = fn_.pseudos[p->second - kFirstPseudo].slot;
            *o = Operand::ofReg(p->second, l.floating ? 16 : l.size);
            renamed = true;
            floating = floating || l.floating;
        }
        // A scalar move between two xmm registers is a copy of the whole register, which the allocator can coalesce.
        const std::string m = e.ins.m.name();
        if (floating && (m == "movsd" || m == "movss" || m == "movq" || m == "movd")) {
            e.ins.m = "movapd";
            e.ins.a.reg.width = e.ins.b.reg.width = 16;
        }
        if (renamed) fn_.flow.effects[k] = effectsOf(e.ins, fn_.convention);
    }
    fn_.flow.touch();
    fn_.promoted = true;
    return static_cast<int>(found.size());
}

}
