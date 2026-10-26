#include "OptPass.h"
#include "Mir.h"

#include <cassert>
#include <iostream>
#include <set>

namespace opt {

bool PassManager::wanted(const Pass &pass) const {
    return out_ != nullptr && (dump_ == "all" || dump_ == pass.info().name);
}

bool PassManager::run(Pass &pass, MFunction &fn) {
    if (!pass.gate(fn)) return false;
    if (Group *g = dynamic_cast<Group *>(&pass)) return runGroup(*g, fn);
    return runLeaf(pass, fn);
}

void PassManager::before(const Pass &pass, MFunction &fn) {
    const PassInfo &info = pass.info();
    if (info.todoStart & kTodoDropLabels) dropUnnamedLabels(fn);
    if (info.todoStart & kTodoBuildFlow) fn.buildFlow();
    assert(fn.has(info.required) && "a pass ran without a property it requires");
}

// The properties as GCC updates them, one line; a change leaves the
// liveness to be solved again before anything reads it.
void PassManager::after(const Pass &pass, MFunction &fn, bool changed) {
    const PassInfo &info = pass.info();
    fn.props = (fn.props | info.provided) & ~info.destroyed;
    if (changed) fn.flow.touch();
    if (wanted(pass)) {
        std::ostream &o = *out_;
        o << ";; " << fn.name << " after " << info.name << (changed ? " (changed)" : "") << "\n";
        dumpStream(o, fn);
    }
}

bool PassManager::runLeaf(Pass &pass, MFunction &fn) {
    before(pass, fn);
    const bool changed = pass.execute(fn);
    after(pass, fn, changed);
    return changed;
}

bool PassManager::runGroup(Group &group, MFunction &fn) {
    bool any = false;
    const int limit = group.repeat(fn);
    for (int round = 0; round < limit; ++round) {
        before(group, fn);
        bool changed = false;
        bool first = true;
        for (const std::unique_ptr<Pass> &sub : group.subs()) {
            const bool c = run(*sub, fn);
            changed = changed || c;
            if (first && group.stop() == Group::WhenFirstUnchanged && !c) break;
            first = false;
        }
        any = any || changed;
        after(group, fn, changed);
        if (!changed) break;
    }
    return any;
}

void dropUnnamedLabels(MFunction &fn) {
    std::set<std::string> named;
    for (const MEntry &e : fn.stream)
        if (e.kind == MEntry::Inst && !e.dead && e.ins.a.kind == MOperand::Label) named.insert(e.ins.a.text.str());
    for (MEntry &e : fn.stream)
        if (e.kind == MEntry::Label && fn.jumpOnly.count(e.label) && !named.count(e.label)) e.dead = true;
}

namespace {

std::string regText(const Reg &r, const std::string &text) {
    if (r.id < 0) return text;
    if (mir::isPseudo(r.id)) return "%p" + std::to_string(r.id - mir::kFirstPseudo) + ":" + std::to_string(r.width);
    return regName(r.id, r.width);
}

std::string operandText(const MOperand &o) {
    switch (o.kind) {
    case MOperand::None: return "";
    case MOperand::Register: return regText(o.reg, o.text.str());
    case MOperand::Immediate: return "$" + (o.numeric ? std::to_string(o.value) : o.text.str());
    case MOperand::Memory:
        return (o.hasDisp ? std::to_string(o.disp) : std::string()) + "(" +
               (o.reg.id >= 0 ? regText(Reg{o.reg.id, 8}, o.text.str()) : o.text.str()) +
               (o.scale != 0 ? "," + regText(Reg{o.index.id, 8}, "") + "," + std::to_string(o.scale) : "") + ")";
    case MOperand::RipSymbol: return o.text.str() + "(%rip)";
    case MOperand::Indirect: return "*" + regText(o.reg, o.text.str());
    case MOperand::Label: return o.text.str();
    case MOperand::SegMemory: return o.text + ":" + std::to_string(o.disp);
    case MOperand::TlsSymbol: return o.text + "@TPOFF(" + regText(Reg{o.reg.id, 8}, "") + ")";
    }
    return "";
}

}

// **The stream, one entry a line**: its index, a `-` for a dead entry, the
// instruction, `label:` or a directive by its kind; a block boundary where the flow has
// one, with what is live in and out of it when that has been solved.
void dumpStream(std::ostream &o, MFunction &fn) {
    std::vector<int> blockAt(fn.stream.size() + 1, -1);
    if (fn.has(kPropFlow))
        for (std::size_t b = 0; b < fn.flow.blocks.size(); ++b) blockAt[fn.flow.blocks[b].begin] = static_cast<int>(b);
    for (std::size_t k = 0; k < fn.stream.size(); ++k) {
        if (blockAt[k] >= 0) {
            const Block &blk = fn.flow.blocks[blockAt[k]];
            o << ";;   block " << blockAt[k] << " [" << blk.begin << ", " << blk.end << ")";
            if (!fn.flow.solutionsDirty) o << " live in " << std::hex << blk.in.regs << " out " << blk.out.regs << std::dec;
            if (fn.loops().depthOf(blockAt[k]) > 0) o << " loop depth " << fn.loops().depthOf(blockAt[k]);
            for (int e : blk.succs) {
                static const char *const kinds[] = {"fall", "jump", "ret", "leave", "eh"};
                const Edge &edge = fn.flow.edges[e];
                o << (e == blk.succs.front() ? " -> " : ", ");
                if (edge.to == Edge::kExit) o << "exit"; else o << edge.to;
                o << " (" << kinds[edge.kind] << ")";
            }
            o << "\n";
        }
        const MEntry &e = fn.stream[k];
        o << (e.dead ? "  -" : "   ") << k << "\t";
        switch (e.kind) {
        case MEntry::Inst:
            o << e.ins.m.name();
            if (e.ins.operands >= 1) o << " " << operandText(e.ins.a);
            if (e.ins.operands >= 2) o << ", " << operandText(e.ins.b);
            break;
        case MEntry::Label: o << e.label << ":" << (e.state ? "  ; state" : ""); break;
        case MEntry::Directive: o << e.dir.describe(); break;
        }
        o << "\n";
    }
}

}
