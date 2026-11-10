// The SSA IR as text (-dump=ssa) and the verifier (-check=ssa) - M3-IR 3.3, 3.4, 3.10.

#include "Ir.h"
#include "opt/SsaOpt.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace ir {

namespace {

const char kOpNames[][12] = {
    "iconst", "fconst", "null", "addr", "tlsaddr", "undef",
    "alloca", "load", "store", "memcpy", "memset", "gep",
    "add", "sub", "mul", "sdiv", "udiv", "srem", "urem", "smulh", "umulh", "and", "or", "xor", "shl", "lshr", "ashr", "neg", "not", "icmp",
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
    case Op::FConst: {
        char b[40];
        if (i.k != 0) std::snprintf(b, sizeof b, " 0x%04llx%016llx", static_cast<unsigned long long>(i.k), i.bits);   // x87
        else std::snprintf(b, sizeof b, " 0x%llx", i.bits);
        out += b;
        break;
    }
    case Op::Addr: out += " @" + i.sym; if (i.k != 0) out += " +" + std::to_string(i.k); break;
    case Op::TlsAddr: out += " @" + i.sym; if (i.k != 0) out += " wrapper"; break;
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

std::string dump(const Function &f, const TypeTable &types) {
    std::string out = "function @" + f.symbol + "(";
    for (std::size_t p = 0; p < f.params.size(); p++)
        out += (p ? ", " : "") + valueName(f.params[p]) + ": " + typeName(types, f.value(f.params[p]).type);
    if (f.sret.valid()) out += std::string(f.params.empty() ? "" : ", ") + valueName(f.sret) + " sret: " + typeName(types, f.value(f.sret).type);
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
    return out + "}\n";
}

std::string dump(const Module &m, const TypeTable &types) {
    std::string out;
    for (std::size_t fi = 1; fi < m.functions.size(); fi++) out += dump(m.functions[fi], types);
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
    Dominators dom;

    Verifier(const Function &fn, const TypeTable &t, std::string &o) : f(fn), types(t), out(o), faults(0), dom(fn) {}

    void fault(BlockId b, const std::string &what) {
        out += "  " + f.symbol + " " + blockName(b) + ": " + what + "\n";
        faults++;
    }

    void run() {
        // The blocks of the body: one a pass removed or merged away stays in the arena, out of `blocks`.
        defBlock.assign(f.values.size(), 0);
        std::vector<char> inBody(f.blockArena.size(), 0);
        for (BlockId blk : f.blocks) {
            const std::uint32_t bi = blk.v;
            inBody[bi] = 1;
            const Block &b = f.blockArena[bi];
            for (ValueId p : b.params) defBlock[p.v] = bi;
            for (InstId id : b.insts) if (f.inst(id).result.valid()) defBlock[f.inst(id).result.v] = bi;
            if (b.term.valid() && f.inst(b.term).result.valid()) defBlock[f.inst(b.term).result.v] = bi;
        }
        for (ValueId p : f.params) defBlock[p.v] = f.entry.v;
        if (f.sret.valid()) defBlock[f.sret.v] = f.entry.v;
        if (!f.entry.valid()) { fault(BlockId(), "no entry block"); return; }
        for (BlockId bid : f.blocks) {
            const std::uint32_t bi = bid.v;
            const Block &b = f.blockArena[bi];
            if (!b.term.valid()) { fault(bid, "no terminator"); continue; }
            if (!isTerminator(f.inst(b.term).op)) fault(bid, "ends in a '" + opName(f.inst(b.term).op) + "', which is no terminator");
            for (const Succ &s : f.inst(b.term).succ)
                if (s.block.v < inBody.size() && !inBody[s.block.v]) fault(bid, "a branch to " + blockName(s.block) + ", which is not in the body");
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
        if (!dom.reachable(here)) return;   // unreachable: nothing dominates anything there, and nothing is read
        for (std::uint32_t n = 0; n < i.count; n++) {
            const ValueId v = f.operand(i, n);
            if (!v.valid() || v.v >= f.values.size()) { fault(bid, "an operand that is no value"); continue; }
            const std::uint32_t db = defBlock[v.v];
            if (db == 0) { fault(bid, valueName(v) + " is defined nowhere"); continue; }
            if (db == here) {
                // Within one block, the definition must come first; a block parameter counts as first.
                if (f.value(v).def.valid() && !before(bid, f.value(v).def, i)) fault(bid, valueName(v) + " is used before its definition");
            } else if (!dom.dominates(db, here)) {
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
                if (db != 0 && db != here && !dom.dominates(db, here)) fault(bid, valueName(v) + " is passed where its definition does not dominate");
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

int verify(const Function &f, const TypeTable &types, std::string &out) {
    Verifier v(f, types, out);
    v.run();
    return v.faults;
}

int verify(const Module &m, const TypeTable &types, std::string &out) {
    int failed = 0;
    for (std::size_t fi = 1; fi < m.functions.size(); fi++) if (verify(m.functions[fi], types, out) != 0) failed++;
    return failed;
}

}   // namespace ir
