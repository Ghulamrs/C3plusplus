#include "OptPipeline.h"

#include "MirWebs.h"
#include "MirAlloc.h"
#include "MirLocals.h"
#include "OptPasses.h"

#include <cassert>

namespace opt {

// **The passes, each a Pass over the function.** Every one of these does what the function it calls did before the manager existed, and nothing more; the manager's rules replace the driver's hand-written loops.
namespace {

// A property set that says "the flow graph must describe the stream".
constexpr unsigned kFlow = kPropFlow;

struct ForwardValues : Pass {
    ForwardValues() : Pass(PassInfo{"forward-values", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return forwardValues(fn.stream, fn.flow, fn.convention, fn.tempFrom); }
};

struct RemoveUnreachable : Pass {
    RemoveUnreachable() : Pass(PassInfo{"remove-unreachable", 0, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return removeUnreachable(fn.stream); }
};

struct RemoveDead : Pass {
    RemoveDead() : Pass(PassInfo{"remove-dead", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return removeDead(fn.stream, fn.flow); }
};

struct CoalesceCopies : Pass {
    CoalesceCopies() : Pass(PassInfo{"coalesce-copies", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return coalesceCopies(fn.stream, fn.flow, fn.convention); }
};

struct FoldLoads : Pass {
    FoldLoads() : Pass(PassInfo{"fold-loads", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return foldLoads(fn.stream, fn.flow, fn.convention); }
};

struct FoldOffsets : Pass {
    FoldOffsets() : Pass(PassInfo{"fold-offsets", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return foldOffsets(fn.stream, fn.flow, fn.convention); }
};

struct ThreadJumps : Pass {
    ThreadJumps() : Pass(PassInfo{"thread-jumps", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return threadJumps(fn); }
};

// Ten instructions for three, so a speed level's alone; the flow is rebuilt before anything reads the entries this inserts,
// and before this reads any: thread-jumps runs just ahead of it and inserts labels and jumps without rebuilding.
// Off for a function selected from its SSA, whose `divide` did it (M3b.5 round 5: 0 changed there).
struct DivideByConstant : Pass {
    DivideByConstant() : Pass(PassInfo{"divide-by-constant", kFlow, 0, 0, kTodoBuildFlow}) {}
    bool gate(const MFunction &fn) const override { return !fn.costs().forSize() && !fn.fromSsa; }
    bool execute(MFunction &fn) override { return divideByConstant(fn.stream, fn.flow); }
};

// **Loop-invariant code motion**, after allocation, where a register the
// loop never touches can be seen to be free; a speed level's alone.
struct HoistInvariants : Pass {
    HoistInvariants() : Pass(PassInfo{"hoist-invariants", kFlow | kPropPhysical, 0, 0, kTodoBuildFlow}) {}
    bool gate(const MFunction &fn) const override { return !fn.costs().forSize(); }
    bool execute(MFunction &fn) override { return hoistInvariants(fn); }
};

// **A double the loop reads from an unwritten frame slot, held in a free xmm register**; a speed level's alone.
struct HoistXmmSlots : Pass {
    HoistXmmSlots() : Pass(PassInfo{"hoist-xmm-slots", kFlow | kPropPhysical, 0, 0, kTodoBuildFlow}) {}
    bool gate(const MFunction &fn) const override { return !fn.costs().forSize(); }
    bool execute(MFunction &fn) override { return hoistXmmSlots(fn); }
};

// **A loop counter that never goes negative read as its own 64-bit register**, its `movslq` then dead.
// Off for a function selected from its SSA, whose `loops` widened it (M3b.5 round 5).
struct WidenCounters : Pass {
    WidenCounters() : Pass(PassInfo{"widen-counters", kFlow | kPropPhysical, 0, 0, kTodoBuildFlow}) {}
    bool gate(const MFunction &fn) const override { return !fn.costs().forSize() && !fn.fromSsa; }
    bool execute(MFunction &fn) override { return widenCounters(fn); }
};

// **Scaled-index addressing**, after allocation so that webs and the
// allocator never meet an indexed operand; `imul $8` is still a multiply here.
struct FoldIndex : Pass {
    FoldIndex() : Pass(PassInfo{"fold-index", kFlow | kPropPhysical, 0, 0, kTodoBuildFlow}) {}
    bool execute(MFunction &fn) override { return foldIndex(fn.stream, fn.flow, fn.convention); }
};

// **The pinned occurrences split off with copies, every web a pseudo**, the
// register each was found in kept as its home for the allocator. The flow
// follows the inserted entries and the renaming; the stream names pseudos now.
struct Webs : Pass {
    Webs() : Pass(PassInfo{"webs", kFlow | kPropPhysical, 0, kPropPhysical, kTodoBuildFlow}) {}
    bool execute(MFunction &fn) override {
        mir::Webs webs(fn);
        const bool split = webs.splitPinned();
        webs.build();
        fn.pseudos.clear();
        for (int h : webs.homes()) fn.pseudos.push_back(PseudoInfo(mir::TargetDesc::x86_64().classOf(h), h));
        return split;
    }
};

// **Every scalar local whose slot a register could hold becomes a pseudo**,
// the slot kept for the allocator to give back.
struct LocalsPass : Pass {
    LocalsPass() : Pass(PassInfo{"locals", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return mir::Locals(fn).promote() > 0; }
};

// **Every pseudo given a register**, or its slot back, and the copies that
// then copy a register to itself dropped. The restores of the preserved
// registers it took leave the flow describing the stream no longer.
struct Allocate : Pass {
    Allocate() : Pass(PassInfo{"allocate", kFlow, kPropPhysical, kFlow, 0}) {}
    bool execute(MFunction &fn) override {
        mir::Allocator alloc(fn);
        const bool changed = alloc.run();
        fn.pseudos.clear();
        return changed;
    }
};

struct RemoveDeadStores : Pass {
    RemoveDeadStores() : Pass(PassInfo{"remove-dead-stores", 0, 0, 0, 0}) {}
    // A save's store is read by the epilogue, which is not in the stream: dse-late runs after the saves are taken.
    bool execute(MFunction &fn) override {
        SharedSlots shared = fn.shared;
        for (const SavedReg &sv : fn.saves) shared.add(sv.disp, 16);
        return removeDeadStores(fn.stream, shared);
    }
};

// **The temporaries' region shrinks to the slots still named**: the passes
// resolve most into registers, and a frame reserved for the rest moves every
// rendered displacement on Windows.
static void shrinkTemporaries(MFunction &fn) {
    if (fn.tempCount == 0) return;
    int used = 0;
    for (const MEntry &e : fn.stream) {
        if (e.kind != MEntry::Inst || e.dead) continue;
        for (const MOperand *o : {&e.ins.a, &e.ins.b}) {
            if (!o->isMem() || o->reg.id != RBP || o->scale != 0 || o->disp > fn.tempFrom) continue;
            const long long t = (-o->disp - fn.tempBase) / 8;
            if (t <= fn.tempCount && t > used) used = static_cast<int>(t);
        }
    }
    fn.inlineTop = static_cast<int>((fn.tempBase + 8 * used + 15) & ~15);
}

// **The values' slots close up** where nothing lies below them: no temporary or callee, no funclet, no LSDA.
static void closeValues(MFunction &fn) {
    if (fn.valueTo == 0 || fn.prologueAt < 0 || !fn.lsda.empty() || fn.inlineTop > fn.frameSize) return;
    if (fn.frameSize != ((fn.valueTo + 15) & ~15LL)) return;
    for (const MEntry &e : fn.stream) if (e.kind == MEntry::Directive && e.dir.beginsFunclet()) return;
    const long long end = closeValueSlots(fn.stream, fn.valueFrom, fn.valueTo, fn.shared);
    fn.frameSize = static_cast<int>((end + 15) & ~15LL);
    if (fn.inlineTop > fn.frameSize) fn.inlineTop = fn.frameSize;
}

// **The frame as the passes leave it**: the saves nothing needs any more
// dropped, and the prologue rewritten with the rest and the final size.
struct FinishFrame : Pass {
    FinishFrame() : Pass(PassInfo{"finish-frame", 0, 0, 0, 0}) {}
    bool execute(MFunction &fn) override {
        const bool had = !fn.saves.empty();
        const int walked = fn.frameSize;
        shrinkTemporaries(fn);
        closeValues(fn);
        dropUnusedSaves(fn.stream, fn.saves, fn.frameBase());
        fn.size = fn.frameBase() + ((8 * static_cast<int>(fn.saves.size()) + 15) & ~15);
        if (fn.size != walked) {
            assert(fn.prologueAt >= 0 && "a frame can grow only while its prologue is held");
            opt::MDirective &p = fn.stream[fn.prologueAt].dir;
            assert(p.kind == opt::MDirective::Prologue && "the prologue's entry holds the prologue");
            p.n[0] = fn.size;
            p.s = fn.lsda;
            p.n[1] = fn.outgoing;
            p.n[2] = 0;
            p.withSaves = true;
            p.saves = fn.saves;
        }
        return had;
    }
};

// **Loop-head alignment**, last of all: it adds entries that hold no code,
// and every pass after it would have to see past them.
struct AlignLoops : Pass {
    AlignLoops() : Pass(PassInfo{"align-loops", kFlow, 0, kFlow, kTodoBuildFlow}) {}
    bool gate(const MFunction &fn) const override { return fn.costs().loopLine() > 0; }
    bool execute(MFunction &fn) override { return alignLoops(fn); }
};

struct Shrink : Pass {
    Shrink() : Pass(PassInfo{"shrink", kFlow, 0, 0, 0}) {}
    bool execute(MFunction &fn) override { return shrink(fn.stream, fn.flow, fn.convention); }
};

// The frame passes run while the prologue is still held; a funclet's frame is its parent's, and its blocks are in the stream (M3a.4).
struct FrameGroup : Group {
    FrameGroup() : Group(PassInfo{"frame", 0, 0, 0, 0}, 1, WhenNoneChanged) {}
    bool gate(const MFunction &fn) const override { return fn.prologueAt >= 0; }
};

// Everything again once locals have registers - if any left its slot.
struct RoundsIfPromoted : Group {
    RoundsIfPromoted() : Group(PassInfo{"rounds", 0, 0, 0, kTodoDropLabels | kTodoBuildFlow}, 0, WhenNoneChanged) {}
    bool gate(const MFunction &fn) const override { return fn.promoted || !fn.saves.empty(); }
};

template <class G>
std::unique_ptr<Group> rounds() {
    std::unique_ptr<Group> g(new G());
    g->add(std::unique_ptr<Pass>(new ForwardValues()));
    g->add(std::unique_ptr<Pass>(new RemoveUnreachable()));
    g->add(std::unique_ptr<Pass>(new RemoveDead()));
    g->add(std::unique_ptr<Pass>(new CoalesceCopies()));
    g->add(std::unique_ptr<Pass>(new FoldLoads()));
    g->add(std::unique_ptr<Pass>(new FoldOffsets()));
    g->add(std::unique_ptr<Pass>(new ThreadJumps()));
    g->add(std::unique_ptr<Pass>(new DivideByConstant()));
    return g;
}

// **A round is every scalar pass once**; each leaves work for the others,
// so rounds run until one finds nothing, or the level's limit. Labels only
// jumps named are dropped and the flow rebuilt before each.
struct Rounds : Group {
    Rounds() : Group(PassInfo{"rounds", 0, 0, 0, kTodoDropLabels | kTodoBuildFlow}, 0, WhenNoneChanged) {}
};

}

// **The pipeline**, cxx1's passes.def, in the order the code below declares it: rounds; frame (webs, locals, allocate, rounds, dse-loop); fold-index; the hoists; dse-late; finish-frame; shrink-loop; align-loops (-O2).
// What each pass does, in one line apiece, is the table in CLAUDE.md, "The optimizer's design notes".
// The two loops rebuild the flow without dropping labels, where rounds does both: that is how the driver did it before the manager, and a label dropped there could change what is emitted - kept, and noted.
std::unique_ptr<Pass> pipelineFor() {
    std::unique_ptr<Group> top(new Group(PassInfo{"pipeline", 0, 0, 0, 0}, 1, Group::WhenNoneChanged));
    top->add(rounds<Rounds>());

    std::unique_ptr<Group> frame(new FrameGroup());
    frame->add(std::unique_ptr<Pass>(new Webs()));
    frame->add(std::unique_ptr<Pass>(new LocalsPass()));
    frame->add(std::unique_ptr<Pass>(new Allocate()));
    frame->add(rounds<RoundsIfPromoted>());
    std::unique_ptr<Group> dse(new Group(PassInfo{"dse-loop", 0, 0, 0, 0}, 3, Group::WhenFirstUnchanged));
    dse->add(std::unique_ptr<Pass>(new RemoveDeadStores()));
    dse->add(rounds<Rounds>());
    frame->add(std::move(dse));
    top->add(std::move(frame));

    top->add(std::unique_ptr<Pass>(new FoldIndex()));
    top->add(std::unique_ptr<Pass>(new HoistInvariants()));
    top->add(std::unique_ptr<Pass>(new HoistXmmSlots()));
    top->add(std::unique_ptr<Pass>(new WidenCounters()));
    // fold-index and the hoists leave stores whose reads they took: dse once more, and its rounds.
    std::unique_ptr<Group> dseLate(new Group(PassInfo{"dse-late", 0, 0, 0, 0}, 1, Group::WhenFirstUnchanged));
    dseLate->add(std::unique_ptr<Pass>(new RemoveDeadStores()));
    dseLate->add(rounds<Rounds>());
    top->add(std::move(dseLate));
    top->add(std::unique_ptr<Pass>(new FinishFrame()));

    std::unique_ptr<Group> shrinkLoop(new Group(PassInfo{"shrink-loop", 0, 0, 0, kTodoBuildFlow}, 3, Group::WhenFirstUnchanged));
    shrinkLoop->add(std::unique_ptr<Pass>(new Shrink()));
    shrinkLoop->add(rounds<Rounds>());
    top->add(std::move(shrinkLoop));
    top->add(std::unique_ptr<Pass>(new AlignLoops()));
    return std::unique_ptr<Pass>(top.release());
}

}
