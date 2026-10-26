#include "Optimizer.h"

#include "OptDeclines.h"
#include "OptPipeline.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <utility>

using opt::MEntry;

Optimizer::Optimizer(Spelling &under, const Abi &abi, int level)
    : under_(under), passes_(level > 0), costs_(opt::Costs::forLevel(level)), fn_(*costs_), pipeline_(opt::pipelineFor()) {
    fn_.convention = opt::conventionOf(abi);
}

void Optimizer::hold(MEntry e) {
    fn_.stream.push_back(std::move(e));
    held_++;
}

void Optimizer::instruction(const std::string &m, int operands, const Op *a, const Op *b) {
    if (!inFunction_) {
        if (operands == 0) under_.ins(m);
        else if (operands == 1) under_.ins(m, *a);
        else under_.ins(m, *a, *b);
        return;
    }
    MEntry e;
    e.ins.m = opt::OpId(m);
    // CPP11_MIR_STRICT=1 names every mnemonic held that the table does not list: the measure of its completeness,
    // which is also M4's list of what the encoder must take (M3-IR.md 2.6).
    static const bool strict = std::getenv("CPP11_MIR_STRICT") != nullptr;
    if (strict && !e.ins.m.listed())
        std::fprintf(stderr, "mir-strict: '%s' is not in the opcode table, in %s\n", m.c_str(), fn_.name.c_str());
    e.ins.operands = operands;
    if (a) e.ins.a = opt::MOperand::from(*a);
    if (b) e.ins.b = opt::MOperand::from(*b);
    if (argsPending_ && e.ins.m == opt::mn::Call) { e.ins.args = args_; e.ins.exactArgs = true; }
    if (retPending_ && e.ins.m == opt::mn::Ret) { e.ins.args = rets_; e.ins.exactArgs = true; }
    argsPending_ = false;
    retPending_ = false;
    if (inlining_)
        for (opt::MOperand *o : {&e.ins.a, &e.ins.b})
            if (o->isMem() && o->reg.id == opt::RBP && o->disp > -kTempBase) {
                assert(o->disp < 0 && "an inlined callee reads only its own frame");
                o->disp -= inlineBase_;
                o->hasDisp = true;
            }
    hold(std::move(e));
}

void Optimizer::directive(opt::MDirective d) {
    if (!inFunction_) { d.replay(under_); return; }
    MEntry e;
    e.kind = MEntry::Directive;
    e.dir = std::move(d);
    hold(std::move(e));
}

void Optimizer::ins(const std::string &m) { instruction(m, 0, nullptr, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a) { instruction(m, 1, &a, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a, const Op &b) { instruction(m, 2, &a, &b); }

void Optimizer::defLabel(const std::string &l) {
    if (!inFunction_) { under_.defLabel(l); return; }
    MEntry e;
    e.kind = MEntry::Label;
    e.label = l;
    hold(std::move(e));
}

// **Held as a label with a mark**: whether a call ends on it is known only
// once the passes are done, and the spelling judges that from what it is given.
void Optimizer::stateLabel(const std::string &l) {
    if (!inFunction_) { under_.stateLabel(l); return; }
    defLabel(l);
    fn_.stream.back().state = true;
}

void Optimizer::functionBegin(const std::string &name, bool exported, bool mergeable) {
    flush();
    under_.functionBegin(name, exported, mergeable);
    inFunction_ = true;
    fn_.name = name;
    fn_.prologueAt = -1;
    fn_.inlineTop = 0;
    fn_.saves.clear();
    fn_.promoted = false;
    fn_.tempFrom = 0;
    fn_.tempBase = fn_.tempCount = 0;
    fn_.shared = opt::SharedSlots();
    inlining_ = false;
    opaque_.clear();
}

void Optimizer::dumpTo(std::ostream *to, const std::string &afterPass) {
    if (afterPass.empty()) dump_ = to;
    else manager_.dumpAfter(afterPass, to);
}

void Optimizer::sharedSlot(long long disp, int size) { fn_.shared.add(disp, size); }

// **A callee walked in place keeps its frame below the caller's locals**, moved
// down by `base`, and its scalars are listed there once. The rest of a callee's
// frame is copied a word at a time, as a scalar is read: no scalar goes over it.
void Optimizer::inlineBegin(int base, int calleeFrame, const std::vector<opt::Local> &scalars) {
    inlining_ = true;
    inlineBase_ = base;
    fn_.inlineTop = std::max(fn_.inlineTop, base + ((calleeFrame + 15) & ~15));
    std::vector<opt::Local> moved;
    for (const opt::Local &l : scalars) moved.push_back(opt::Local{l.disp - base, l.size, l.floating});
    std::sort(moved.begin(), moved.end(),
              [](const opt::Local &x, const opt::Local &y) { return x.disp < y.disp; });
    long long at = -static_cast<long long>(base + calleeFrame);
    for (const opt::Local &m : moved) {
        if (m.disp > at) opaque_.push_back(opt::Local{at, static_cast<int>(m.disp - at)});
        at = std::max(at, m.disp + m.size);
    }
    if (-base > at) opaque_.push_back(opt::Local{at, static_cast<int>(-base - at)});
    auto opaque = [this](const opt::Local &l) {
        for (const opt::Local &o : opaque_)
            if (l.disp < o.disp + o.size && o.disp < l.disp + l.size) return true;
        return false;
    };
    fn_.locals.erase(std::remove_if(fn_.locals.begin(), fn_.locals.end(), opaque), fn_.locals.end());
    for (const opt::Local &m : moved) {
        bool listed = opaque(m);
        for (const opt::Local &have : fn_.locals)
            if (have.disp == m.disp && have.size == m.size && have.floating == m.floating) listed = true;
        if (!listed) fn_.locals.push_back(m);
    }
}

void Optimizer::inlineEnd() { inlining_ = false; }

void Optimizer::jumpOnly(const std::string &label) { fn_.jumpOnly.insert(label); }

void Optimizer::exceptionRegion(const std::string &begin, const std::string &end, const std::string &target) {
    fn_.regions.push_back(opt::Region{begin, end, target});
}

void Optimizer::functionEnd(const std::string &name) {
    flush();
    inFunction_ = false;
    under_.functionEnd(name);
}

void Optimizer::frame(std::vector<opt::Local> locals) { fn_.locals = std::move(locals); }

void Optimizer::temporaries(int count) {
    if (count == 0) return;
    const long long top = fn_.frameBase();
    for (MEntry &e : fn_.stream) {
        if (e.kind != MEntry::Inst) continue;
        for (opt::MOperand *o : {&e.ins.a, &e.ins.b})
            if (o->isMem() && o->reg.id == opt::RBP && o->disp <= -kTempBase) o->disp += kTempBase - top;
    }
    for (int t = 0; t < count; ++t) fn_.locals.push_back(opt::Local{-(top + 8 * (t + 1)), 8});
    fn_.tempFrom = -(top + 8);
    fn_.tempBase = top;
    fn_.tempCount = count;
    fn_.inlineTop = static_cast<int>((top + 8 * count + 15) & ~15);
}

void Optimizer::callArguments(opt::RegSet regs) {
    argsPending_ = true;
    args_ = regs;
}

void Optimizer::returnReads(opt::RegSet regs) {
    retPending_ = true;
    rets_ = regs;
}

void Optimizer::returnsPair(bool pair) {
    fn_.convention.returned = opt::bit(opt::RAX) | opt::bit(opt::kXmm0);
    if (pair) fn_.convention.returned |= opt::bit(opt::RDX) | opt::bit(opt::kXmm0 + 1);
}

// **The passes, through the manager.** What ran here as hand-written loops is the pipeline in OptPipeline.cpp; the manager runs it, checks what each pass requires, and dumps after the pass -dump=mir names.
void Optimizer::improve(opt::MFunction &fn) {
    if (!passes_) return;
    manager_.run(*pipeline_, fn);
}

// **The function with its funclets' blocks in place**, which the spelling lifts out as it meets their directives; a
// frame slot a funclet reads or writes is shared with the function first, so no register takes it (P1's decision:
// a value live into a funclet stays in its slot) and no store to it is dead.
void Optimizer::flush() {
    if (fn_.stream.empty()) return;
    if (fn_.prologueAt < 0 && passes_) opt::noteDecline("no-prologue-no-frame-passes", fn_.name);
    int depth = 0;
    for (const MEntry &e : fn_.stream) {
        if (e.kind == MEntry::Directive && e.dir.beginsFunclet()) ++depth;
        else if (e.kind == MEntry::Directive && e.dir.kind == opt::MDirective::FuncletEnd) --depth;
        else if (depth > 0 && e.kind == MEntry::Inst) fn_.shared.addAccessOf(e);
    }
    improve(fn_);
    if (dump_ != nullptr) { *dump_ << ";; " << fn_.name << "\n"; opt::dumpStream(*dump_, fn_); }
    replay(fn_.stream);
    frameSize_ = fn_.size;
    fn_.stream.clear();
    fn_.regions.clear();       // written out with the labels they name
    fn_.props = opt::kPropPhysical;   // a new stream: the flow describes nothing yet
    fn_.prologueAt = -1;       // written out: from here on the frame is what it is
}

void Optimizer::replay(const opt::MStream &s) {
    for (const MEntry &e : s) {
        if (e.dead) continue;
        switch (e.kind) {
        case MEntry::Inst: {
            const opt::MInst &i = e.ins;
            if (i.operands == 0) under_.ins(i.m.name());
            else if (i.operands == 1) under_.ins(i.m.name(), i.a.op());
            else under_.ins(i.m.name(), i.a.op(), i.b.op());
            break;
        }
        case MEntry::Label:
            if (e.state) under_.stateLabel(e.label);
            else under_.defLabel(e.label);
            break;
        case MEntry::Directive: e.dir.replay(under_); break;
        }
    }
}

// Everything else is a directive: held where it stood, with its arguments copied.
namespace {
opt::MDirective dir(opt::MDirective::Kind k, long long a = 0, long long b = 0, long long c = 0,
                   const std::string &s = std::string()) {
    opt::MDirective d(k);
    d.n[0] = a; d.n[1] = b; d.n[2] = c;
    d.s = s;
    return d;
}
}
using D = opt::MDirective;
void Optimizer::prologue(int frameSize, const std::string &lsda, int outgoing, int realign) {
    assert((realign == 0 || !passes_) && "a realigned frame is written past the passes");
    fn_.prologueAt = static_cast<int>(fn_.stream.size());
    fn_.frameSize = frameSize;
    fn_.lsda = lsda;
    fn_.outgoing = outgoing;
    directive(dir(D::Prologue, frameSize, outgoing, realign, lsda));
}
void Optimizer::fileEntry(int n, const std::string &name) { directive(dir(D::FileEntry, n, 0, 0, name)); }
void Optimizer::location(int file, int line, int column) { directive(dir(D::Location, file, line, column)); }
void Optimizer::funcletLinesBegin() { directive(dir(D::FuncletLinesBegin)); }
void Optimizer::funcletLinesEnd() { directive(dir(D::FuncletLinesEnd)); }
void Optimizer::globl(const std::string &name) { directive(dir(D::Globl, 0, 0, 0, name)); }
void Optimizer::weakDefinition(const std::string &name) { directive(dir(D::WeakDefinition, 0, 0, 0, name)); }
void Optimizer::textSection() { directive(dir(D::TextSection)); }
void Optimizer::rodataSection() { directive(dir(D::RodataSection)); }
void Optimizer::relroSection() { directive(dir(D::RelroSection)); }
void Optimizer::dataSection() { directive(dir(D::DataSection)); }
void Optimizer::bssSection() { directive(dir(D::BssSection)); }
void Optimizer::tlsDataSection() { directive(dir(D::TlsDataSection)); }
void Optimizer::tlsBssSection() { directive(dir(D::TlsBssSection)); }
void Optimizer::tlsInitialiserEntry(const std::string &fn) { directive(dir(D::TlsInitialiserEntry, 0, 0, 0, fn)); }
void Optimizer::objectType(const std::string &name) { directive(dir(D::ObjectType, 0, 0, 0, name)); }
void Optimizer::objectSize(const std::string &name, int size) { directive(dir(D::ObjectSize, size, 0, 0, name)); }
void Optimizer::align(int n) { directive(dir(D::Align, n)); }
void Optimizer::loopAlign(int bytes) { directive(dir(D::LoopAlign, bytes)); }
void Optimizer::zero(int n) { directive(dir(D::Zero, n)); }
void Optimizer::dataInt(int size, long long v) { directive(dir(D::DataInt, size, v)); }
void Optimizer::dataSym(const std::string &sym, long long off) { directive(dir(D::DataSym, off, 0, 0, sym)); }
void Optimizer::noteHasEh(bool yes) { directive(dir(D::NoteHasEh, yes ? 1 : 0)); }
void Optimizer::initialiserEntry(const std::string &fn, bool dsoHandle) {
    directive(dir(D::InitialiserEntry, dsoHandle ? 1 : 0, 0, 0, fn));
}
void Optimizer::dataBytes(const std::string &bytes) { directive(dir(D::DataBytes, 0, 0, 0, bytes)); }
void Optimizer::funcletBegin(const std::string &symbol, const std::string &entryLoc) {
    assert(inFunction_ && "a funclet outside a function");
    opt::MDirective d = dir(D::FuncletBegin, 0, 0, 0, symbol);
    d.t = entryLoc;
    directive(d);
}
void Optimizer::funcletLeave(const std::string &label) { directive(dir(D::FuncletLeave, 0, 0, 0, label)); }
void Optimizer::funcletEnd(const std::string &resume) { directive(dir(D::FuncletEnd, 0, 0, 0, resume)); }
