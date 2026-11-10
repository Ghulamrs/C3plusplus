// **cl's shapes** (M3b.3, lane E): the atexit helper `??__F<object>` a Microsoft static object with a destructor is
// registered by - the destructor the parser chose ("~object") called on the object it recorded ("$object") - and a jump
// out of nested handlers, each a funclet handing the runtime an exit in the funclet around it.

#include "Builder.h"

namespace ir {

FuncId Builder::atexitHelper(const FunctionFacts &facts, std::size_t index) {
    Lowering &l = l_;
    l.beginFunction(facts);
    std::string object, dtor;
    TypeId objectType, sig;
    bool defaulted = false;
    if (l.this_.valid() || facts.paramCount != 0 || !l.T(l.returns_).isVoid() || !l.memberBuilt_ ||
        !l.memberBuilt_(l.key_, "$object", 0, object, objectType, defaulted) || !l.memberBuilt_(l.key_, "~object", 0, dtor, sig, defaulted))
        l.outside("an atexit helper the parser recorded no object or destructor for");
    openBody();
    // A thread_local's is the thread's own object, reached directly as objectAt reaches it (the helper is __tlregdtor's).
    sema::Resolution r;
    r.isThreadLocal = l.threadLocalGlobal(object);
    l.directCall(dtor, sig, std::vector<ValueId>(1, l.objectAddr(object, l.tid(*l.T(sig).params()[0]), r)));
    l.br(l.returnBlock_);
    l.finishFunction(facts.landingPads);
    return FuncId(static_cast<std::uint32_t>(index));
}

// The caller's result pointer where it is read: in a Microsoft funclet, which has none of the body's registers, from a
// slot the entry fills, as a handler that builds the result reads it.
ValueId Lowering::resultPtr() {
    if (!funclet_.valid() || !target_.microsoftNames() || !sret_.valid()) return sret_;
    const TypeId pt = F().value(sret_).type;
    if (!sretSlot_.valid()) {
        sretSlot_ = entryAlloca(pt, "$sret");
        store(sretSlot_, sret_);
        std::vector<InstId> &here = F().blockArena[cur_.v].insts, &entry = F().blockArena[F().entry.v].insts;
        entry.push_back(here.back());
        here.pop_back();
    }
    return load(sretSlot_, pt);
}

// Leaving the n innermost handlers (leaveHandlers, msGotoExit): the innermost one's objects past `upper`'s floor destroyed
// in its funclet, a catchret to an exit its try's pad lists, in the funclet that try stands in; there the next handler is
// left the same way, and the last exit destroys down to `mark` and branches on.
void Lowering::msLeave(std::size_t n, std::size_t mark, BlockId target, const std::vector<ValueId> &args, std::size_t upper) {
    const std::size_t own = handlerLive_.back();
    for (std::size_t k = upper; k-- > own; ) if (live_[k].slot.v != elidedSlot_.v) destroy(live_[k]);
    const std::pair<BlockId, BlockId> h = msHandlers_.back();
    const BlockId was = funclet_;
    funclet_ = h.second;
    const BlockId exit = newBlock();
    funclet_ = was;
    Function &f = F();
    std::vector<ValueId> params;
    for (std::size_t i = 0; i < args.size(); i++) params.push_back(param(exit, f.value(args[i]).type));
    at(f.block(h.first).insts[0]).cases.push_back(exit.v);
    Succ s;
    s.block = exit;
    s.first = static_cast<std::uint32_t>(f.operands.size());
    s.count = static_cast<std::uint32_t>(args.size());
    f.operands.insert(f.operands.end(), args.begin(), args.end());
    at(add(Op::CatchRet, TypeId(), std::vector<ValueId>())).succ.push_back(s);
    const BlockId back = cur_;
    place(exit);
    if (n == 1) {
        for (std::size_t k = own; k-- > mark; ) if (live_[k].slot.v != elidedSlot_.v) destroy(live_[k]);
        br(target, params);
    } else {
        handlerLive_.pop_back();
        msHandlers_.pop_back();
        msLeave(n - 1, mark, target, params, own);
        handlerLive_.push_back(own);
        msHandlers_.push_back(h);
    }
    place(back);
}

}   // namespace ir
