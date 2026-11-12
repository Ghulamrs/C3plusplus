#include "Walker.h"

#include <set>

void Walker::markLine(const Stmt &n) { markLine(n.pos()); }

void Walker::visit(const ExprStmt &n) { markLine(n); n.expr().accept(asVisitor()); }

void Walker::visit(const Block &n) {
    markLine(n);
    openBlock(n.scope());
    // A pad's own code, under a region of its own where the target wants
    // one - the enclosing region split around it, as around a `try` - so a
    // destructor that throws while the exception unwinds terminates.
    const bool scope = n.unwindCleanup() && terminateScopes();
    const int id = scope ? nextLabel() : 0;
    const int msStateAtEntry = msCurState_;
    if (scope) {
        defineLabel(label("cleanup", id));
        openRegion(label("cleanup", id), std::vector<std::string>(), std::vector<int>());
    }
    for (const StmtPtr &s : n.body()) s->accept(asVisitor());
    if (scope) {
        defineLabel(label("cleanupend", id));
        const std::vector<CallSite> pieces = closeRegion(label("cleanupend", id), label("cleanupend", id));
        const std::string pad = terminatePad(id);
        for (std::size_t i = 0; i < pieces.size(); i++) {
            CallSite row = pieces[i];
            row.pad = pad;
            row.terminate = true;
            addCallSite(row);
            exceptionRegion(row.begin, row.end, pad);
        }
    }
    // A cleanup's state outlives its stretch; the block that built the objects
    // destroys them at its '}', and the state in force goes back with them.
    if (msCurState_ != msStateAtEntry) {
        const std::string l = label("stateend", nextLabel());
        defineStateLabel(l);
        msRows().push_back(MsIpRow{ l, msStateAtEntry, false });
        msCurState_ = msStateAtEntry;
    }
    closeBlock(n.scope());
}

void Walker::visit(const If &n) {
    markLine(n);
    int id = nextLabel();
    genTruth(n.cond());
    if (n.elseArm()) {
        branchIfZero(label("else", id));
        n.thenArm().accept(asVisitor());
        jump(label("end", id));
        defineLabel(label("else", id));
        n.elseArm()->accept(asVisitor());
    } else {
        branchIfZero(label("end", id));
        n.thenArm().accept(asVisitor());
    }
    defineLabel(label("end", id));
}

void Walker::visit(const While &n) {
    markLine(n);
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("begin", id) });
    defineLabel(label("begin", id));
    genTruth(n.cond());
    branchIfZero(label("end", id));
    n.body().accept(asVisitor());
    jump(label("begin", id));
    defineLabel(label("end", id));
    jumps_.pop_back();
}

void Walker::visit(const For &n) {
    markLine(n);

    openBlock(n.scope());
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("step", id) });

    if (n.init()) n.init()->accept(asVisitor());
    defineLabel(label("begin", id));
    if (n.cond()) {

        markLine(n);
        genTruth(*n.cond());
        branchIfZero(label("end", id));
    }
    n.body().accept(asVisitor());
    defineLabel(label("step", id));
    if (n.step()) {
        markLine(n);
        n.step()->accept(asVisitor());
    }
    jump(label("begin", id));
    defineLabel(label("end", id));
    closeBlock(n.scope());

    jumps_.pop_back();
}

void Walker::visit(const DoWhile &n) {
    markLine(n);
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("step", id) });

    defineLabel(label("begin", id));
    n.body().accept(asVisitor());
    defineLabel(label("step", id));
    genTruth(n.cond());
    branchIfNotZero(label("begin", id));
    defineLabel(label("end", id));

    jumps_.pop_back();
}

void Walker::visit(const Switch &n) {
    markLine(n);
    int id = nextLabel();

    n.cond().accept(asVisitor());
    for (const Case *c : n.cases())
        caseBranch(c->value(), label("case", c->id()));
    jump(n.defaultCase() ? label("default", n.defaultCase()->id())
                         : label("end", id));

    jumps_.push_back({ label("end", id), "" });
    n.body().accept(asVisitor());
    jumps_.pop_back();
    defineLabel(label("end", id));
}

void Walker::visit(const Case &n) {
    markLine(n);
    defineLabel(label(n.isDefault() ? "default" : "case", n.id()));
    n.body().accept(asVisitor());
}

void Walker::visit(const Goto &n) { markLine(n); jump(userLabel(n.label())); }
void Walker::visit(const FuncletLeave &n) { markLine(n); funcletLeave(userLabel(n.label())); }

void Walker::visit(const Label &n) {
    markLine(n);
    defineLabel(userLabel(n.name()));
    n.body().accept(asVisitor());
}

void Walker::visit(const Conditional &n) {
    int id = nextLabel();
    genTruth(n.cond());
    branchIfZero(label("else", id));
    n.thenArm().accept(asVisitor());
    jump(label("end", id));
    defineLabel(label("else", id));
    n.elseArm().accept(asVisitor());
    defineLabel(label("end", id));
}

void Walker::visit(const Comma &n) {
    n.left().accept(asVisitor());
    n.right().accept(asVisitor());
}

void Walker::visit(const Break &n) { markLine(n); jump(jumps_.back().brk); }

// The gotos of a pad and the labels it defines - a pad goes on to another of this frame where one of
// its gotos names a label outside it, and resumes the unwind where none does.
static void padJumps(const Stmt &s, std::set<std::string> &to, std::set<std::string> &defined) {
    if (const Block *b = dynamic_cast<const Block *>(&s)) {
        for (const StmtPtr &x : b->body()) padJumps(*x, to, defined);
    } else if (const If *i = dynamic_cast<const If *>(&s)) {
        padJumps(i->thenArm(), to, defined);
        if (i->elseArm() != nullptr) padJumps(*i->elseArm(), to, defined);
    } else if (const Label *l = dynamic_cast<const Label *>(&s)) {
        defined.insert(l->name());
        padJumps(l->body(), to, defined);
    } else if (const Goto *g = dynamic_cast<const Goto *>(&s)) {
        to.insert(g->label());
    }
}

static std::set<std::string> padExits(const Stmt &pad, std::set<std::string> &defined) {
    std::set<std::string> to, out;
    padJumps(pad, to, defined);
    for (const std::string &l : to)
        if (defined.count(l) == 0) out.insert(l);
    return out;
}

// The first Microsoft region's scratch word, wherever in the body it is.
static int firstUnwindHelp(const Stmt &s) {
    if (const Try *t = dynamic_cast<const Try *>(&s)) {
        if (t->unwindHelpSlot() != 0) return t->unwindHelpSlot();
        for (const StmtPtr &x : t->body())
            if (int k = firstUnwindHelp(*x)) return k;
        return 0;
    }
    const Stmt *kids[2] = { nullptr, nullptr };
    if (const Block *b = dynamic_cast<const Block *>(&s)) {
        for (const StmtPtr &x : b->body())
            if (int k = firstUnwindHelp(*x)) return k;
    } else if (const If *i = dynamic_cast<const If *>(&s)) {
        kids[0] = &i->thenArm(); kids[1] = i->elseArm();
    } else if (const While *w = dynamic_cast<const While *>(&s)) {
        kids[0] = &w->body();
    } else if (const For *f = dynamic_cast<const For *>(&s)) {
        kids[0] = f->init(); kids[1] = &f->body();
    } else if (const DoWhile *d = dynamic_cast<const DoWhile *>(&s)) {
        kids[0] = &d->body();
    } else if (const Switch *w = dynamic_cast<const Switch *>(&s)) {
        kids[0] = &w->body();
    } else if (const Case *c = dynamic_cast<const Case *>(&s)) {
        kids[0] = &c->body();
    } else if (const Label *l = dynamic_cast<const Label *>(&s)) {
        kids[0] = &l->body();
    }
    for (const Stmt *k : kids)
        if (k != nullptr)
            if (int slot = firstUnwindHelp(*k)) return slot;
    return 0;
}

// **-2 goes into the scratch word on entry, as cl's prologue writes it**: a call that throws before
// the first region began - a local's constructor - found whatever the stack held there, and the
// runtime took it for a state and destroyed an object that was never built.
void Walker::storeUnwindHelpAtEntry(const Stmt &body) {
    if (!usesFunclets()) return;
    msUnwindHelpSlot_ = firstUnwindHelp(body);
    if (msUnwindHelpSlot_ != 0) storeUnwindHelp(msUnwindHelpSlot_);
}

void Walker::visit(const Try &n) {
    markLine(n);
    if (usesFunclets()) { msTryStatement(n); return; }
    const int id = nextLabel();
    const std::string begin = label("try", id);
    const std::string end = label("tryend", id);
    const std::string pad = label("pad", id);
    const std::string done = label("caught", id);

    defineLabel(begin);
    openRegion(begin, n.types(), n.typeIndices());
    for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(asVisitor());
    defineLabel(end);
    std::set<std::string> defines, exits;
    if (n.hasPad()) exits = padExits(n.pad(), defines);
    const std::vector<CallSite> pieces = closeRegion(end, end, exits, defines);
    jump(done);

    defineLabel(pad);
    landingPad(n.pointerSlot(), n.selectorSlot());
    n.pad().accept(asVisitor());
    defineLabel(done);

    // **Every piece names the same pad and the same types.** They are one
    // region as far as the program is concerned; they are several rows only
    // because something nested inside had to be cut out of the range.
    std::vector<std::string> chain = n.types();
    std::vector<int> chainIx = n.typeIndices();
    // A segment of a try's body carries the try's own types already, so a
    // handler the chain has - the same type under the same index - is not
    // added again: TI's personality reads every row it is given.
    for (std::size_t k = open_.size(); k-- > 0; ) {
        const OpenRegion &outer = open_[k];
        for (std::size_t t = 0; t < outer.types.size(); t++) {
            bool have = false;
            for (std::size_t c = 0; c < chain.size() && c < chainIx.size(); c++)
                if (t < outer.indices.size() && chain[c] == outer.types[t] && chainIx[c] == outer.indices[t]) have = true;
            if (have) continue;
            chain.push_back(outer.types[t]);
            if (t < outer.indices.size()) chainIx.push_back(outer.indices[t]);
        }
    }
    for (std::size_t i = 0; i < pieces.size(); i++) {
        callSite(pieces[i].begin, pieces[i].end, pad, chain, chainIx,
                 n.alsoCleanup(), pieces[i].at, pieces[i].handsOver);
        exceptionRegion(pieces[i].begin, pieces[i].end, pad);
    }
}

// **The Microsoft shape, and what is missing from it is the point.** No pad, no
// selector, no jump over a handler: each handler becomes a function of its own
// and a table says which to call. This frame contributes three labels.
void Walker::msTryStatement(const Try &n) {
    const int id = nextLabel();
    const std::string begin = label("try", id);
    const std::string end = label("tryend", id);
    const std::string resume = label("caught", id);
    // The FuncInfo names one scratch word per frame: the first region's, for all of them.
    if (msUnwindHelpSlot_ == 0) msUnwindHelpSlot_ = n.unwindHelpSlot();
    // Not inside a handler: a funclet runs with the frame's state already the runtime's (cl writes none there).
    if (msRowsInto_ == nullptr) storeUnwindHelp(msUnwindHelpSlot_);

    // This region's state, inside whatever encloses it; the body's own states
    // follow it in number, which is what tryLow..tryHigh relies on.
    const int parent = msCurState_;
    const int state = static_cast<int>(msStates_.size());
    MsState s;
    s.toState = parent;
    msStates_.push_back(s);
    defineStateLabel(begin);
    msRows().push_back(MsIpRow{ begin, state, false });
    msCurState_ = state;
    for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(asVisitor());
    defineStateLabel(end);
    defineLabel(resume);
    exceptionRegion(begin, end, resume);
    for (std::size_t i = 0; i < n.msExits().size(); i++)
        exceptionRegion(begin, end, userLabel(n.msExits()[i]));

    // **A cleanup's objects outlive its range** - the next stretch of the same
    // block chains to it, and the block's end is what reverts the state.
    if (n.cleanup() != nullptr) {
        const std::string funclet = openFunclet(n.cleanup()->pos(), true);
        n.cleanup()->accept(asVisitor());
        endCleanupFunclet();
        leaveFunclet();
        msStates_[state].action = funclet;
        msCurState_ = state;
        return;
    }
    msCurState_ = parent;
    msRows().push_back(MsIpRow{ end, parent, false });

    // The funclets come after the range they belong to is closed, so nothing
    // they emit lands between `begin` and `end` - those bound the addresses the
    // runtime matches a thrown object against, and a handler must not be inside.
    MsTryBlock tb;
    tb.tryLow = state;
    tb.tryHigh = static_cast<int>(msStates_.size()) - 1;
    for (std::size_t i = 0; i < n.handlers().size(); i++) {
        const MsHandler &h = n.handlers()[i];
        // A handler's state sits beside the try's, under the same parent: an
        // exception out of the handler has left the try.
        const int hstate = static_cast<int>(msStates_.size());
        MsState hs;
        hs.toState = parent;
        msStates_.push_back(hs);
        MsHandlerRow row;
        row.descriptor = h.descriptor;
        row.objectSlot = h.objectSlot;
        row.byReference = h.byReference;
        row.constPointer = h.constPointer;
        // Entered on the `catch` line, as cl's funclet is.
        row.funclet = openFunclet(h.pos, false);
        msCurState_ = hstate;
        std::vector<MsIpRow> inside;
        std::vector<MsIpRow> *const wasInto = msRowsInto_;
        msRowsInto_ = &inside;
        h.body->accept(asVisitor());
        msRowsInto_ = wasInto;
        msCurState_ = parent;
        endFunclet(resume);
        leaveFunclet();
        msFuncletRows_.push_back(MsIpRow{ row.funclet, hstate, true });
        msFuncletRows_.insert(msFuncletRows_.end(), inside.begin(), inside.end());
        tb.handlers.push_back(row);
    }
    tb.catchHigh = static_cast<int>(msStates_.size()) - 1;
    msTryBlocks_.push_back(tb);
}

void Walker::visit(const Continue &n) {
    markLine(n);
    for (std::size_t i = jumps_.size(); i-- > 0;) {
        if (!jumps_[i].cont.empty()) {
            jump(jumps_[i].cont);
            return;
        }
    }
}

