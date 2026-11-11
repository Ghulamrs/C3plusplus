// **The inliner on the SSA IR** (M3b.5 round 2, SsaOpt.h): `Inliner`'s budgets decide over `ir::Function`s measured
// here, and a site taken has its callee's blocks cloned into the caller before promote, so its slots promote too.

#include "SsaOpt.h"
#include "../../optimizer/Inliner.h"
#include "../../optimizer/OptDeclines.h"

#include <memory>
#include <utility>

namespace ir {

namespace {

// A direct call of a function of the module: its instruction, its block and the loops around it.
struct CallAt { InstId inst; std::uint32_t block; int depth; std::uint32_t callee; };

// One function as the inliner measures it: an instruction a node, less what the Ast.h tree has no node for - an
// alloca, a jump, a parameter's store into its slot.
struct Body {
    int size;
    bool loops, calls, pads, throws, variadic, entryTarget;
    std::vector<CallAt> sites;
    Body() : size(0), loops(false), calls(false), pads(false), throws(false), variadic(false), entryTarget(false) {}
};

// By block: how many natural loops hold it - a back edge is one into a block that dominates its source.
std::vector<int> loopDepths(const Function &f, const Dominators &dom) {
    const std::size_t n = f.blockArena.size();
    std::vector<int> depth(n, 0);
    for (std::uint32_t h = 1; h < n; h++) {
        if (!dom.reachable(h)) continue;
        std::vector<char> in(n, 0);
        std::vector<std::uint32_t> work;
        for (std::uint32_t t : dom.preds[h])
            if (dom.dominates(h, t) && !in[t]) { in[t] = 1; if (t != h) work.push_back(t); }
        if (work.empty() && !in[h]) continue;
        in[h] = 1;
        while (!work.empty()) {
            const std::uint32_t b = work.back();
            work.pop_back();
            for (std::uint32_t p : dom.preds[b]) if (!in[p]) { in[p] = 1; work.push_back(p); }
        }
        for (std::size_t b = 1; b < n; b++) depth[b] += in[b];
    }
    return depth;
}

bool isParameter(const Function &f, ValueId v) { return !f.value(v).def.valid() && f.value(v).param.v == f.entry.v; }

Body measure(const Function &f, const std::map<std::string, std::uint32_t> &bySymbol) {
    Body body;
    if (!f.entry.valid()) return body;
    const Dominators dom(f);
    const std::vector<int> depth = loopDepths(f, dom);
    body.pads = f.landingPads;
    for (BlockId b : f.blocks) {
        const Block &blk = f.block(b);
        if (blk.pad || blk.funclet != 0) body.pads = true;
        if (depth[b.v] != 0) body.loops = true;
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const Inst &i = f.inst(id);
            for (const Succ &s : i.succ) if (s.block.v == f.entry.v) body.entryTarget = true;
            if (i.op == Op::Alloca || i.op == Op::Br) continue;
            if (i.op == Op::Store && i.count == 2 && isParameter(f, f.operand(i, 1))) continue;
            body.size++;
            if (i.op == Op::VaStart || i.op == Op::VaArg) body.variadic = true;
            if (i.op == Op::Rethrow) body.throws = true;
            if (i.op == Op::Invoke || (i.op >= Op::CatchSwitch && i.op <= Op::CleanupRet)) body.pads = true;
            if (i.op != Op::Call && i.op != Op::Invoke) continue;
            body.calls = true;
            const CallInfo &c = f.calls[i.call];
            if (c.symbol == "_CxxThrowException") body.throws = true;
            const std::map<std::string, std::uint32_t>::const_iterator g = bySymbol.find(c.symbol);
            if (g != bySymbol.end()) body.sites.push_back(CallAt{id, b.v, depth[b.v], g->second});
        }
    }
    return body;
}

// Why no site of the callee is taken, as Inliner.cpp names the Ast.h path's reasons; "" where one may be.
std::string ineligible(const Function &g, const Body &b, const TypeTable &types) {
    if (b.pads) return "inline-callee-landing-pad";
    if (g.variadic || b.variadic) return "inline-callee-variadic";
    if (b.throws) return "inline-callee-throws-in-its-frame";
    if (g.sret.valid() || (g.returns.valid() && types.type(g.returns)->isStructOrUnion())) return "inline-callee-returns-a-class";
    if (g.frameAlign > 16) return "inline-callee-realigned-frame";
    if (b.entryTarget) return "inline-callee-entry-is-a-target";
    return "";
}

bool sameOrPointers(const TypeTable &types, TypeId a, TypeId b) {
    return a.v == b.v || (types.type(a)->isPointer() && types.type(b)->isPointer());
}

bool hasResult(const Function &f, const Inst &call, const TypeTable &types) {
    return call.result.valid() && f.value(call.result).type.valid() && !types.type(f.value(call.result).type)->isVoid();
}

// Why this site is not taken whatever the budgets say; "" where the clone below can write it.
std::string refusedSite(const Function &f, const CallAt &at, const Function &g, const Body &callee, const TypeTable &types) {
    const Inst &i = f.inst(at.inst);
    const CallInfo &c = f.calls[i.call];
    if (f.block(BlockId(at.block)).funclet != 0 || f.block(BlockId(at.block)).pad) return "inline-site-in-a-funclet";
    if (i.op == Op::Invoke && callee.calls) return "inline-site-invoke-of-a-callee-that-calls";
    if (c.sret || c.resultAlloca >= 0 || c.variadic) return "inline-site-class-result";
    if (i.count != g.params.size()) return "inline-site-argument-count";
    const Type &sig = *types.type(c.signature);
    for (std::uint32_t k = 0; k < i.count; k++) {
        if (k < sig.params().size() && sig.params()[k]->isStructOrUnion()) return "inline-site-class-argument";
        if (!sameOrPointers(types, f.value(f.operand(i, k)).type, g.value(g.params[k]).type)) return "inline-site-argument-type";
    }
    if (!hasResult(f, i, types)) return "";
    for (BlockId b : g.blocks) {
        const Inst &t = g.inst(g.block(b).term);
        if (t.op != Op::Ret) continue;
        if (t.count != 1 || !sameOrPointers(types, g.value(g.operand(t, 0)).type, f.value(i.result).type)) return "inline-site-result-type";
    }
    return "";
}

// **One site written**: g's blocks cloned into f - values, blocks, calls and frame objects renumbered - the call's
// block cut at the call, g's parameters the call's operands, each `ret` a branch to the block holding what followed
// the call, whose parameter is the call's own value. An invoke of g, which calls nothing, is a jump to its normal edge.
struct Cloner {
    Function &f;
    const Function &g;
    const TypeTable &types;
    std::vector<ValueId> vmap;
    std::vector<std::uint32_t> bmap;

    Cloner(Function &fn, const Function &callee, const TypeTable &t) : f(fn), g(callee), types(t), vmap(callee.values.size()),
        bmap(callee.blockArena.size(), 0) {}

    BlockId newBlock() {
        f.blockArena.push_back(Block());
        return BlockId(static_cast<std::uint32_t>(f.blockArena.size() - 1));
    }
    ValueId newValue(TypeId type, InstId def, BlockId param, std::size_t index) {
        Value v;
        v.type = type;
        v.def = def;
        v.param = param;
        v.index = static_cast<std::uint16_t>(index);
        f.values.push_back(v);
        return ValueId(static_cast<std::uint32_t>(f.values.size() - 1));
    }
    InstId newInst(const Inst &i) {
        f.insts.push_back(i);
        return InstId(static_cast<std::uint32_t>(f.insts.size() - 1));
    }
    // v as type `to`: itself, or a bitcast of it appended to `list` (two pointer types, refusedSite's rule).
    ValueId as(ValueId v, TypeId to, std::vector<InstId> &list, const Inst &like) {
        if (f.value(v).type.v == to.v) return v;
        Inst i;
        i.op = Op::Bitcast;
        i.type = to;
        i.at = like.at;
        i.pos = like.pos;
        i.first = static_cast<std::uint32_t>(f.operands.size());
        i.count = 1;
        f.operands.push_back(v);
        const InstId id = newInst(i);
        f.insts[id.v].result = newValue(to, id, BlockId(), 0);
        list.push_back(id);
        return f.insts[id.v].result;
    }
    Succ edge(BlockId to, const std::vector<ValueId> &args) {
        Succ s;
        s.block = to;
        s.first = static_cast<std::uint32_t>(f.operands.size());
        s.count = static_cast<std::uint32_t>(args.size());
        f.operands.insert(f.operands.end(), args.begin(), args.end());
        return s;
    }
    InstId jump(BlockId to, const std::vector<ValueId> &args, const Inst &like) {
        Inst i;
        i.op = Op::Br;
        i.at = like.at;
        i.pos = like.pos;
        i.succ.push_back(edge(to, args));
        return newInst(i);
    }

    void run(InstId site) {
        // Where the call is: block B, place `at` among its instructions, or its terminator (an invoke).
        std::size_t place = 0, at = 0;
        bool found = false;
        for (std::size_t p = 0; p < f.blocks.size() && !found; p++) {
            const Block &blk = f.block(f.blocks[p]);
            for (std::size_t k = 0; k <= blk.insts.size() && !found; k++)
                if ((k < blk.insts.size() ? blk.insts[k] : blk.term).v == site.v) { place = p; at = k; found = true; }
        }
        if (!found) return;
        const BlockId B = f.blocks[place];
        const Inst call = f.insts[site.v];
        const bool result = hasResult(f, call, types);
        std::vector<InstId> head(f.block(B).insts.begin(), f.block(B).insts.begin() + static_cast<std::ptrdiff_t>(at));
        std::vector<InstId> tail;
        if (at < f.block(B).insts.size()) tail.assign(f.block(B).insts.begin() + static_cast<std::ptrdiff_t>(at) + 1, f.block(B).insts.end());
        // The frame objects and the calls, appended; the parameters, the call's operands.
        const long long frameBase = static_cast<long long>(f.frame.size());
        const std::uint32_t callBase = static_cast<std::uint32_t>(f.calls.size()) - 1;
        f.frame.insert(f.frame.end(), g.frame.begin(), g.frame.end());
        for (std::size_t c = 1; c < g.calls.size(); c++) {
            f.calls.push_back(g.calls[c]);
            if (f.calls.back().resultAlloca >= 0) f.calls.back().resultAlloca += static_cast<int>(frameBase);
        }
        for (std::size_t p = 0; p < g.params.size(); p++)
            vmap[g.params[p].v] = as(f.operands[call.first + p], g.value(g.params[p]).type, head, call);
        // The blocks and their parameters; then C, which holds what followed the call.
        std::vector<BlockId> made;
        for (BlockId gb : g.blocks) {
            const BlockId nb = newBlock();
            bmap[gb.v] = nb.v;
            made.push_back(nb);
            if (gb.v == g.entry.v) continue;
            for (std::size_t p = 0; p < g.block(gb).params.size(); p++) {
                const ValueId v = newValue(g.value(g.block(gb).params[p]).type, InstId(), nb, p);
                vmap[g.block(gb).params[p].v] = v;
                f.blockArena[nb.v].params.push_back(v);
            }
        }
        const BlockId C = newBlock();
        made.push_back(C);
        if (result) {
            Value &v = f.values[call.result.v];
            v.def = InstId();
            v.param = C;
            v.index = 0;
            f.blockArena[C.v].params.push_back(call.result);
        } else if (call.result.valid()) {
            f.values[call.result.v].type = TypeId();
        }
        // Every instruction copied with a value of its own, then - every value known - its operands and edges.
        std::vector<std::pair<InstId, InstId> > copies;
        std::vector<InstId> allocas;
        for (BlockId gb : g.blocks) {
            const Block &blk = g.block(gb);
            for (std::size_t k = 0; k <= blk.insts.size(); k++) {
                const InstId gi = k < blk.insts.size() ? blk.insts[k] : blk.term;
                if (!gi.valid()) continue;
                const InstId ni = newInst(g.inst(gi));
                copies.push_back(std::make_pair(gi, ni));
                Inst &i = f.insts[ni.v];
                if (i.result.valid()) { i.result = newValue(g.value(g.inst(gi).result).type, ni, BlockId(), 0); vmap[g.inst(gi).result.v] = i.result; }
                if (i.op == Op::Alloca) { i.k += frameBase; allocas.push_back(ni); }
                else if (k < blk.insts.size()) f.blockArena[bmap[gb.v]].insts.push_back(ni);
                else f.blockArena[bmap[gb.v]].term = ni;
                if (i.op == Op::Call || i.op == Op::Invoke) i.call += callBase;
            }
        }
        for (std::size_t n = 0; n < copies.size(); n++) {
            const Inst &from = g.inst(copies[n].first);
            const InstId ni = copies[n].second;
            const std::uint32_t first = static_cast<std::uint32_t>(f.operands.size());
            for (std::uint32_t k = 0; k < from.count; k++) f.operands.push_back(vmap[g.operand(from, k).v]);
            f.insts[ni.v].first = first;
            for (std::size_t s = 0; s < from.succ.size(); s++) {
                std::vector<ValueId> args;
                for (std::uint32_t a = 0; a < from.succ[s].count; a++) args.push_back(vmap[g.arg(from.succ[s], a).v]);
                const Succ e = edge(BlockId(bmap[from.succ[s].block.v]), args);
                f.insts[ni.v].succ[s] = e;
            }
            if (from.op != Op::Ret) continue;
            // A `ret`: a branch to C, carrying the value where the call had one.
            std::uint32_t in = 0;
            for (BlockId gb : g.blocks) if (g.block(gb).term.v == copies[n].first.v) in = bmap[gb.v];
            std::vector<ValueId> args;
            if (result) args.push_back(as(f.operands[first], f.value(call.result).type, f.blockArena[in].insts, from));
            Inst &i = f.insts[ni.v];
            i.op = Op::Br;
            i.type = TypeId();
            i.count = 0;
            const Succ e = edge(C, args);
            f.insts[ni.v].succ.assign(1, e);
        }
        // B ends in a jump to the callee's entry; C takes the rest - an invoke's normal edge as a jump.
        f.blockArena[C.v].insts = tail;
        if (call.op == Op::Invoke) {
            Inst i;
            i.op = Op::Br;
            i.at = call.at;
            i.pos = call.pos;
            i.succ.push_back(call.succ[0]);
            f.blockArena[C.v].term = newInst(i);
        } else {
            f.blockArena[C.v].term = f.block(B).term;
        }
        f.blockArena[B.v].insts = head;
        f.blockArena[B.v].term = jump(BlockId(bmap[g.entry.v]), std::vector<ValueId>(), call);
        Inst &gone = f.insts[site.v];
        gone = Inst();
        std::vector<InstId> &entry = f.blockArena[f.entry.v].insts;
        entry.insert(entry.begin(), allocas.begin(), allocas.end());
        f.blocks.insert(f.blocks.begin() + static_cast<std::ptrdiff_t>(place) + 1, made.begin(), made.end());
    }
};

}   // namespace

void inlineCalls(Module &m, int level, const TypeTable &types, PassLog &log) {
    const std::unique_ptr<opt::Costs> costs = opt::Costs::forLevel(level);
    if (level >= 1 && costs->inlines()) {
        // Every function measured, by FuncId; every site the clone can write listed in program order.
        std::vector<Body> bodies(1);
        std::vector<Inliner::Measure> measures(1);
        std::vector<std::string> whys(1);      // each measure's reason, kept here while the inliner reads it
        std::map<std::string, std::uint32_t> bySymbol;      // the first of a symbol, as Module::find answers
        for (std::uint32_t fi = 1; fi < m.functions.size(); fi++) bySymbol.insert(std::make_pair(m.functions[fi].symbol, fi));
        for (std::size_t fi = 1; fi < m.functions.size(); fi++) {
            const Function &f = m.functions[fi];
            bodies.push_back(measure(f, bySymbol));
            Inliner::Measure me;
            me.symbol = f.symbol;
            me.size = bodies.back().size;
            me.leaf = !bodies.back().calls && !bodies.back().loops;
            whys.push_back(ineligible(f, bodies.back(), types));
            measures.push_back(me);
        }
        for (std::size_t fi = 1; fi < measures.size(); fi++) measures[fi].why = whys[fi].c_str();
        std::vector<Inliner::Site> sites;
        std::vector<InstId> insts;
        for (std::uint32_t fi = 1; fi < m.functions.size(); fi++) {
            const Function &f = m.functions[fi];
            for (const CallAt &at : bodies[fi].sites) {
                const std::uint32_t gi = at.callee;
                log.inlineSites++;
                const std::string why = measures[gi].eligible() ? refusedSite(f, at, m.functions[gi], bodies[gi], types) : std::string();
                if (!why.empty()) {
                    opt::noteDecline(why.c_str(), m.functions[gi].symbol + " in " + f.symbol);
                    log.inlineRefused[why]++;
                    continue;
                }
                sites.push_back(Inliner::Site{fi, gi, at.depth, 1 + static_cast<int>(f.inst(at.inst).count), 0, false, ""});
                insts.push_back(at.inst);
            }
        }
        Inliner inliner(*costs);
        inliner.decide(measures, sites);
        // Each caller rewritten in a copy, so that a callee cloned is the body as lowered, whatever its own sites took.
        std::vector<Function> rewritten(m.functions.size());
        std::vector<char> changed(m.functions.size(), 0);
        for (std::size_t k = 0; k < sites.size(); k++) {
            const Inliner::Site &s = sites[k];
            if (!s.taken) { log.inlineRefused[s.why]++; continue; }
            if (!changed[s.caller]) { rewritten[s.caller] = m.functions[s.caller]; changed[s.caller] = 1; }
            Cloner(rewritten[s.caller], m.functions[s.callee], types).run(insts[k]);
            log.inlined++;
        }
        for (std::size_t fi = 1; fi < m.functions.size(); fi++) if (changed[fi]) std::swap(m.functions[fi], rewritten[fi]);
    }
    if (log.check) {
        std::string faults;
        if (verify(m, types, faults) != 0) { log.failed++; log.faults += "  after inline:\n" + faults; }
    }
    if (log.dumpAfter == "inline") log.dumped = dump(m, types);
}

}   // namespace ir
