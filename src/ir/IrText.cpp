// The SSA IR as text (-dump=ssa) and the verifier (-check=ssa) - M3-IR 3.3, 3.4, 3.10.

#include "Ir.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace ir {

namespace {

const char kOpNames[][12] = {
    "iconst", "fconst", "null", "addr", "tlsaddr", "undef",
    "alloca", "load", "store", "memcpy", "memset", "gep",
    "add", "sub", "mul", "sdiv", "udiv", "srem", "urem", "and", "or", "xor", "shl", "lshr", "ashr", "neg", "not", "icmp",
    "fadd", "fsub", "fmul", "fdiv", "fneg", "fcmp",
    "trunc", "sext", "zext", "fptosi", "fptoui", "sitofp", "uitofp", "fpext", "fptrunc", "bitcast", "ptrtoint", "inttoptr",
    "br", "condbr", "switch", "ret", "unreachable",
    "call", "invoke",
    "catchswitch", "catchpad", "cleanuppad", "catchret", "cleanupret", "rethrow",
    "select", "va_start", "va_arg", "va_end", "trap",
};
const char kCmpNames[][4] = { "eq", "ne", "slt", "sle", "sgt", "sge", "ult", "ule", "ugt", "uge", "oeq", "olt", "ole", "ogt", "oge", "une" };

std::string typeName(const TypeTable &types, TypeId t) {
    return t.valid() ? types.type(t)->describe() : std::string("none");
}

std::string valueName(ValueId v) { return "%" + std::to_string(v.v); }
std::string blockName(BlockId b) { return "^" + std::to_string(b.v); }

std::string succText(const Function &f, const Succ &s) {
    std::string out = blockName(s.block);
    if (s.count == 0) return out;
    out += "(";
    for (std::uint32_t i = 0; i < s.count; i++) out += (i ? ", " : "") + valueName(f.arg(s, i));
    return out + ")";
}

void dumpInst(const Function &f, const TypeTable &types, InstId id, std::string &out) {
    const Inst &i = f.inst(id);
    out += "  ";
    if (i.result.valid()) out += valueName(i.result) + " = ";
    out += opName(i.op);
    if (i.op == Op::ICmp || i.op == Op::FCmp) out += " " + cmpName(i.cc) + ",";
    if (i.type.valid() && i.op != Op::Store && !isTerminator(i.op) && i.op != Op::Call) out += " " + typeName(types, i.type) + ",";
    switch (i.op) {
    case Op::IConst: out += " " + std::to_string(i.k); break;
    case Op::FConst: { char b[24]; std::snprintf(b, sizeof b, " 0x%llx", i.bits); out += b; break; }
    case Op::Addr: case Op::TlsAddr: out += " @" + i.sym; if (i.k != 0) out += " +" + std::to_string(i.k); break;
    case Op::Alloca: out += " align " + std::to_string(i.align) + " ; " + f.frame[static_cast<std::size_t>(i.k)].name; break;
    case Op::CatchPad: case Op::CleanupPad:
        for (std::size_t n = 0; n < i.syms.size(); n++) out += (n ? ", " : " [") + (i.syms[n].empty() ? std::string("...") : "@" + i.syms[n]) + (n + 1 == i.syms.size() ? "]" : "");
        out += " own " + std::to_string(i.k);
        break;
    default: break;
    }
    if (i.op == Op::Call || i.op == Op::Invoke) {
        const CallInfo &c = f.calls[i.call];
        out += c.symbol.empty() ? " *" : " @" + c.symbol;
        out += "(";
        for (std::uint32_t n = c.symbol.empty() ? 1 : 0; n < i.count; n++) out += (n > (c.symbol.empty() ? 1u : 0u) ? ", " : "") + valueName(f.operand(i, n));
        out += ") : " + typeName(types, i.type);
        if (c.variadic) out += " variadic";
        if (c.hasThis) out += " this";
        if (c.sret) out += " sret";
    } else {
        for (std::uint32_t n = 0; n < i.count; n++) out += (n ? ", " : " ") + valueName(f.operand(i, n));
        if (i.op == Op::Gep && i.count == 1) out += ", " + std::to_string(i.k);
    }
    if (i.isVolatile) out += " volatile";
    for (std::size_t s = 0; s < i.succ.size(); s++) {
        if (i.op == Op::Switch && s > 0) out += ", " + std::to_string(i.cases[s - 1]) + " ->";
        else if (i.op == Op::Switch) out += ", default";
        else out += s ? "," : i.count ? "," : "";
        out += " " + succText(f, i.succ[s]);
    }
    out += "\n";
}

}   // namespace

std::string opName(Op op) { return kOpNames[static_cast<int>(op)]; }
std::string cmpName(Cmp cc) { return kCmpNames[static_cast<int>(cc)]; }

bool isTerminator(Op op) {
    return op == Op::Br || op == Op::CondBr || op == Op::Switch || op == Op::Ret || op == Op::Unreachable || op == Op::Invoke ||
           op == Op::CatchSwitch || op == Op::CatchRet || op == Op::CleanupRet || op == Op::Rethrow;
}

FuncId Module::find(const std::string &symbol) const {
    for (std::size_t i = 1; i < functions.size(); i++)
        if (functions[i].symbol == symbol) return FuncId(static_cast<std::uint32_t>(i));
    return FuncId();
}

std::string dump(const Module &m, const TypeTable &types) {
    std::string out;
    for (std::size_t fi = 1; fi < m.functions.size(); fi++) {
        const Function &f = m.functions[fi];
        out += "function @" + f.symbol + "(";
        for (std::size_t p = 0; p < f.params.size(); p++)
            out += (p ? ", " : "") + valueName(f.params[p]) + ": " + typeName(types, f.value(f.params[p]).type);
        out += ") -> " + typeName(types, f.returns);
        if (f.isStatic) out += " static";
        if (f.isInline) out += " inline";
        out += " {\n";
        for (std::size_t bi = 0; bi < f.blocks.size(); bi++) {
            const Block &b = f.block(f.blocks[bi]);
            out += blockName(f.blocks[bi]);
            if (!b.params.empty()) {
                out += "(";
                for (std::size_t p = 0; p < b.params.size(); p++)
                    out += (p ? ", " : "") + valueName(b.params[p]) + ": " + typeName(types, f.value(b.params[p]).type);
                out += ")";
            }
            out += ":\n";
            for (std::size_t k = 0; k < b.insts.size(); k++) dumpInst(f, types, b.insts[k], out);
            if (b.term.valid()) dumpInst(f, types, b.term, out);
        }
        out += "}\n";
    }
    return out;
}

namespace {

struct Verifier {
    const Function &f;
    const TypeTable &types;
    std::string &out;
    int faults;
    // Which block defines each value, and the dominator tree over the blocks reachable from the entry.
    std::vector<std::uint32_t> defBlock;
    std::vector<std::uint32_t> idom, order;
    std::vector<std::vector<std::uint32_t> > preds;

    Verifier(const Function &fn, const TypeTable &t, std::string &o) : f(fn), types(t), out(o), faults(0) {}

    void fault(BlockId b, const std::string &what) {
        out += "  " + f.symbol + " " + blockName(b) + ": " + what + "\n";
        faults++;
    }

    void dominators() {
        const std::size_t n = f.blockArena.size();
        preds.assign(n, std::vector<std::uint32_t>());
        std::vector<std::uint32_t> rpo;
        std::vector<char> seen(n, 0);
        // Reverse postorder by an explicit stack: the entry first, every reachable block once.
        std::vector<std::pair<std::uint32_t, std::size_t> > stack;
        stack.push_back(std::make_pair(f.entry.v, std::size_t(0)));
        seen[f.entry.v] = 1;
        while (!stack.empty()) {
            const std::uint32_t b = stack.back().first;
            std::vector<std::uint32_t> succs;
            const Block &blk = f.blockArena[b];
            if (blk.term.valid()) for (const Succ &s : f.inst(blk.term).succ) succs.push_back(s.block.v);
            if (stack.back().second < succs.size()) {
                const std::uint32_t s = succs[stack.back().second++];
                preds[s].push_back(b);
                if (!seen[s]) { seen[s] = 1; stack.push_back(std::make_pair(s, std::size_t(0))); }
            } else {
                rpo.push_back(b);
                stack.pop_back();
            }
        }
        std::reverse(rpo.begin(), rpo.end());
        order.assign(n, 0);
        for (std::size_t i = 0; i < rpo.size(); i++) order[rpo[i]] = static_cast<std::uint32_t>(i + 1);
        idom.assign(n, 0);
        idom[f.entry.v] = f.entry.v;
        // Cooper, Harvey and Kennedy's iteration, over reachable blocks in reverse postorder.
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 1; i < rpo.size(); i++) {
                const std::uint32_t b = rpo[i];
                std::uint32_t best = 0;
                for (std::uint32_t p : preds[b]) {
                    if (idom[p] == 0) continue;
                    best = best == 0 ? p : intersect(best, p);
                }
                if (best != 0 && idom[b] != best) { idom[b] = best; changed = true; }
            }
        }
    }
    std::uint32_t intersect(std::uint32_t a, std::uint32_t b) {
        while (a != b) {
            while (order[a] > order[b]) a = idom[a];
            while (order[b] > order[a]) b = idom[b];
        }
        return a;
    }
    bool dominates(std::uint32_t a, std::uint32_t b) {
        while (b != a && b != f.entry.v && idom[b] != 0) b = idom[b];
        return a == b;
    }

    void run() {
        const std::size_t n = f.blockArena.size();
        defBlock.assign(f.values.size(), 0);
        for (std::uint32_t bi = 1; bi < n; bi++) {
            const Block &b = f.blockArena[bi];
            for (ValueId p : b.params) defBlock[p.v] = bi;
            for (InstId id : b.insts) if (f.inst(id).result.valid()) defBlock[f.inst(id).result.v] = bi;
            if (b.term.valid() && f.inst(b.term).result.valid()) defBlock[f.inst(b.term).result.v] = bi;
        }
        for (ValueId p : f.params) defBlock[p.v] = f.entry.v;
        if (!f.entry.valid()) { fault(BlockId(), "no entry block"); return; }
        dominators();
        for (std::uint32_t bi = 1; bi < n; bi++) {
            const Block &b = f.blockArena[bi];
            const BlockId bid(bi);
            if (!b.term.valid()) { fault(bid, "no terminator"); continue; }
            if (!isTerminator(f.inst(b.term).op)) fault(bid, "ends in a '" + opName(f.inst(b.term).op) + "', which is no terminator");
            for (InstId id : b.insts) {
                const Inst &i = f.inst(id);
                if (isTerminator(i.op)) fault(bid, "a '" + opName(i.op) + "' inside the block");
                if (i.op == Op::Alloca && bi != f.entry.v) fault(bid, "an alloca outside the entry block");
                uses(bid, i, false);
            }
            uses(bid, f.inst(b.term), true);
            if (b.pad && !(b.insts.empty() ? false : f.inst(b.insts[0]).op == Op::CatchPad || f.inst(b.insts[0]).op == Op::CleanupPad))
                fault(bid, "a pad block that does not begin with its pad");
        }
    }

    void uses(BlockId bid, const Inst &i, bool term) {
        const std::uint32_t here = bid.v;
        if (order[here] == 0) return;   // unreachable: nothing dominates anything there, and nothing is read
        for (std::uint32_t n = 0; n < i.count; n++) {
            const ValueId v = f.operand(i, n);
            if (!v.valid() || v.v >= f.values.size()) { fault(bid, "an operand that is no value"); continue; }
            const std::uint32_t db = defBlock[v.v];
            if (db == 0) { fault(bid, valueName(v) + " is defined nowhere"); continue; }
            if (db == here) {
                // Within one block, the definition must come first; a block parameter counts as first.
                if (f.value(v).def.valid() && !before(bid, f.value(v).def, i)) fault(bid, valueName(v) + " is used before its definition");
            } else if (!dominates(db, here)) {
                fault(bid, valueName(v) + " is used where its definition does not dominate");
            }
        }
        for (std::size_t s = 0; s < i.succ.size(); s++) {
            const Succ &sc = i.succ[s];
            if (!sc.block.valid() || sc.block.v >= f.blockArena.size()) { fault(bid, "a branch to no block"); continue; }
            const Block &to = f.blockArena[sc.block.v];
            if (i.op == Op::Invoke && s == 1) continue;     // the unwind successor's parameters are the runtime's: the pointer and the selector
            if (to.params.size() != sc.count) { fault(bid, "a branch to " + blockName(sc.block) + " with " + std::to_string(sc.count) +
                                                            " arguments for " + std::to_string(to.params.size()) + " parameters"); continue; }
            for (std::uint32_t a = 0; a < sc.count; a++) {
                const ValueId v = f.arg(sc, a);
                if (!v.valid() || v.v >= f.values.size()) { fault(bid, "an argument that is no value"); continue; }
                if (f.value(v).type != f.value(to.params[a]).type)
                    fault(bid, "argument " + std::to_string(a) + " to " + blockName(sc.block) + " is '" + typeName(types, f.value(v).type) +
                               "' for a parameter '" + typeName(types, f.value(to.params[a]).type) + "'");
                const std::uint32_t db = defBlock[v.v];
                if (db != 0 && db != here && !dominates(db, here)) fault(bid, valueName(v) + " is passed where its definition does not dominate");
            }
        }
        (void)term;
    }
    bool before(BlockId bid, InstId def, const Inst &use) {
        const Block &b = f.blockArena[bid.v];
        for (InstId id : b.insts) {
            if (id.v == def.v) return true;
            if (&f.inst(id) == &use) return false;
        }
        return false;
    }
};

}   // namespace

int verify(const Module &m, const TypeTable &types, std::string &out) {
    int failed = 0;
    for (std::size_t fi = 1; fi < m.functions.size(); fi++) {
        Verifier v(m.functions[fi], types, out);
        v.run();
        if (v.faults != 0) failed++;
    }
    return failed;
}

}   // namespace ir
