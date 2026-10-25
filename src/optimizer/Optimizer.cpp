#include "Optimizer.h"

#include "OptDeclines.h"
#include "OptPipeline.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <utility>

using opt::Entry;

Optimizer::Optimizer(Spelling &under, const Abi &abi, int level)
    : under_(under), passes_(level > 0), costs_(opt::Costs::forLevel(level)), fn_(*costs_), pipeline_(opt::pipelineFor()) {
    fn_.convention = opt::conventionOf(abi);
}

void Optimizer::hold(Entry e) {
    current().stream.push_back(std::move(e));
    held_++;
}

void Optimizer::instruction(const std::string &m, int operands, const Op *a, const Op *b) {
    if (!inFunction_) {
        if (operands == 0) under_.ins(m);
        else if (operands == 1) under_.ins(m, *a);
        else under_.ins(m, *a, *b);
        return;
    }
    Entry e;
    e.ins.m = m;
    // CPP11_MIR_STRICT=1 names every mnemonic held that the table does not list: the measure of its completeness,
    // which is also M4's list of what the encoder must take (M3-IR.md 2.6).
    static const bool strict = std::getenv("CPP11_MIR_STRICT") != nullptr;
    if (strict && !e.ins.m.listed())
        std::fprintf(stderr, "mir-strict: '%s' is not in the opcode table, in %s\n", m.c_str(), fn_.name.c_str());
    e.ins.operands = operands;
    if (a) e.ins.a = opt::Operand::from(*a);
    if (b) e.ins.b = opt::Operand::from(*b);
    if (argsPending_ && m == "call") { e.ins.args = args_; e.ins.exactArgs = true; }
    argsPending_ = false;
    if (inlining_)
        for (opt::Operand *o : {&e.ins.a, &e.ins.b})
            if (o->isMem() && o->reg.id == opt::RBP && o->disp > -kTempBase) {
                assert(o->disp < 0 && "an inlined callee reads only its own frame");
                o->disp -= inlineBase_;
                o->hasDisp = true;
            }
    hold(std::move(e));
}

void Optimizer::event(opt::Directive d) {
    if (!inFunction_) { d.replay(under_); return; }
    Entry e;
    e.kind = Entry::Event;
    e.event = std::move(d);
    hold(std::move(e));
}

void Optimizer::ins(const std::string &m) { instruction(m, 0, nullptr, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a) { instruction(m, 1, &a, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a, const Op &b) { instruction(m, 2, &a, &b); }

void Optimizer::defLabel(const std::string &l) {
    if (!inFunction_) { under_.defLabel(l); return; }
    Entry e;
    e.kind = Entry::Label;
    e.label = l;
    hold(std::move(e));
}

// **Held as a label with a mark**: whether a call ends on it is known only
// once the passes are done, and the spelling judges that from what it is given.
void Optimizer::stateLabel(const std::string &l) {
    if (!inFunction_) { under_.stateLabel(l); return; }
    defLabel(l);
    current().stream.back().state = true;     // a funclet's own stream, where a handler holds a try (M1)
}

void Optimizer::functionBegin(const std::string &name, bool exported, bool mergeable) {
    flush();
    under_.functionBegin(name, exported, mergeable);
    inFunction_ = true;
    fn_.name = name;
    fn_.whole = true;
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

// **A funclet is a function of its own to the passes** - the runtime calls it, with the parent's
// frame under rbp - and a piece, never whole: no pass takes a register for it that its prologue does
// not save. What it reads or writes of the parent's frame is shared with the parent, whose passes then leave those slots alone.
void Optimizer::funcletBegin() {
    assert(inFunction_ && "a funclet outside a function");
    funclet_.emplace_back(new opt::Function(*costs_));
    opt::Function &f = *funclet_.back();
    f.name = fn_.name;
    f.convention = fn_.convention;
    f.whole = false;
    if (passes_) opt::noteDecline("funclet-no-frame-passes", fn_.name);
}

void Optimizer::funcletEnd() {
    assert(!funclet_.empty() && "no funclet is held");
    opt::Function &f = *funclet_.back();
    improve(f);
    fn_.shared.addAccessesOf(f.stream);
    funclets_.push_back(std::move(f.stream));
    funclet_.pop_back();
}

void Optimizer::defer(std::function<void()> call) {
    opt::Directive d(opt::Directive::Deferred);
    d.deferred = std::move(call);
    event(std::move(d));
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
    for (Entry &e : fn_.stream) {
        if (e.kind != Entry::Ins) continue;
        for (opt::Operand *o : {&e.ins.a, &e.ins.b})
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

void Optimizer::returnsPair(bool pair) {
    fn_.convention.returned = opt::bit(opt::RAX) | opt::bit(opt::kXmm0);
    if (pair) fn_.convention.returned |= opt::bit(opt::RDX) | opt::bit(opt::kXmm0 + 1);
}

// **The passes, through the manager.** What ran here as hand-written loops is the pipeline in OptPipeline.cpp; the manager runs it, checks what each pass requires, and dumps after any pass CPP11_DUMP_MIR names.
void Optimizer::improve(opt::Function &fn) {
    if (!passes_) return;
    // The passes write a large immediate signed, as -O1 and -O2 always have; -O0 keeps the walker's spelling.
    for (Entry &e : fn.stream)
        if (e.kind == Entry::Ins) e.ins.a.unsignedSpelling = e.ins.b.unsignedSpelling = false;
    manager_.run(*pipeline_, fn);
}

// **The function, then its funclets**, the frame's final size having been
// written with the function's prologue - a funclet's operands are rendered
// against it.
void Optimizer::flush() {
    if (fn_.stream.empty()) return;
    if (fn_.prologueAt < 0 && passes_) opt::noteDecline("no-prologue-no-frame-passes", fn_.name);
    improve(fn_);
    replay(fn_.stream);
    for (const opt::Stream &f : funclets_) replay(f);
    frameSize_ = fn_.size;
    funclets_.clear();
    fn_.stream.clear();
    fn_.regions.clear();       // written out with the labels they name
    fn_.props = opt::kPropPhysical;   // a new stream: the flow describes nothing yet
    fn_.prologueAt = -1;       // written out: from here on the frame is what it is
}

void Optimizer::replay(const opt::Stream &s) {
    for (const Entry &e : s) {
        if (e.dead) continue;
        switch (e.kind) {
        case Entry::Ins: {
            const opt::Instr &i = e.ins;
            if (i.operands == 0) under_.ins(i.m);
            else if (i.operands == 1) under_.ins(i.m, i.a.op());
            else under_.ins(i.m, i.a.op(), i.b.op());
            break;
        }
        case Entry::Label:
            if (e.state) under_.stateLabel(e.label);
            else under_.defLabel(e.label);
            break;
        case Entry::Event: e.event.replay(under_); break;
        }
    }
}

// Everything else is a directive: held where it stood, with its arguments copied.
namespace {
opt::Directive dir(opt::Directive::Kind k, long long a = 0, long long b = 0, long long c = 0,
                   const std::string &s = std::string()) {
    opt::Directive d(k);
    d.n[0] = a; d.n[1] = b; d.n[2] = c;
    d.s = s;
    return d;
}
}
using D = opt::Directive;
void Optimizer::prologue(int frameSize, const std::string &lsda, int outgoing, int realign) {
    assert((realign == 0 || !passes_) && "a realigned frame is written past the passes");
    fn_.prologueAt = static_cast<int>(fn_.stream.size());
    fn_.frameSize = frameSize;
    fn_.lsda = lsda;
    fn_.outgoing = outgoing;
    event(dir(D::Prologue, frameSize, outgoing, realign, lsda));
}
void Optimizer::fileEntry(int n, const std::string &name) { event(dir(D::FileEntry, n, 0, 0, name)); }
void Optimizer::location(int file, int line, int column) { event(dir(D::Location, file, line, column)); }
void Optimizer::funcletLinesBegin() { event(dir(D::FuncletLinesBegin)); }
void Optimizer::funcletLinesEnd() { event(dir(D::FuncletLinesEnd)); }
void Optimizer::globl(const std::string &name) { event(dir(D::Globl, 0, 0, 0, name)); }
void Optimizer::weakDefinition(const std::string &name) { event(dir(D::WeakDefinition, 0, 0, 0, name)); }
void Optimizer::textSection() { event(dir(D::TextSection)); }
void Optimizer::rodataSection() { event(dir(D::RodataSection)); }
void Optimizer::relroSection() { event(dir(D::RelroSection)); }
void Optimizer::dataSection() { event(dir(D::DataSection)); }
void Optimizer::bssSection() { event(dir(D::BssSection)); }
void Optimizer::tlsDataSection() { event(dir(D::TlsDataSection)); }
void Optimizer::tlsBssSection() { event(dir(D::TlsBssSection)); }
void Optimizer::tlsInitialiserEntry(const std::string &fn) { event(dir(D::TlsInitialiserEntry, 0, 0, 0, fn)); }
void Optimizer::objectType(const std::string &name) { event(dir(D::ObjectType, 0, 0, 0, name)); }
void Optimizer::objectSize(const std::string &name, int size) { event(dir(D::ObjectSize, size, 0, 0, name)); }
void Optimizer::align(int n) { event(dir(D::Align, n)); }
void Optimizer::loopAlign(int bytes) { event(dir(D::LoopAlign, bytes)); }
void Optimizer::zero(int n) { event(dir(D::Zero, n)); }
void Optimizer::dataInt(int size, long long v) { event(dir(D::DataInt, size, v)); }
void Optimizer::dataSym(const std::string &sym, long long off) { event(dir(D::DataSym, off, 0, 0, sym)); }
void Optimizer::noteHasEh(bool yes) { event(dir(D::NoteHasEh, yes ? 1 : 0)); }
void Optimizer::initialiserEntry(const std::string &fn, bool dsoHandle) {
    event(dir(D::InitialiserEntry, dsoHandle ? 1 : 0, 0, 0, fn));
}
void Optimizer::dataBytes(const std::string &bytes) { event(dir(D::DataBytes, 0, 0, 0, bytes)); }
