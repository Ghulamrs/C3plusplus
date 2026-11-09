// **The complete object's functions** (M3b.3, lane B): the deleting destructor - Itanium's D0, cl's ??_G with its flag - and
// Itanium's C1 and D1 around the C2 or D2 of a class with virtual bases, cl's ??_D for the destructor; each built from the
// records its synthesiser leaves: the part called ("$C2", "$D2"), the VTT ("$vtt"), each virtual base ("^V", "~V"), what frees ("$delete").

#include "Builder.h"

namespace ir {

bool Builder::completeKind(const std::string &kind) {
    return kind == "deleting destructor" || kind == "complete destructor" || kind == "complete constructor";
}

bool Builder::recorded(const std::string &what, std::string &symbol, TypeId &sig) const {
    bool defaulted = false;
    return l_.memberBuilt_ && l_.memberBuilt_(l_.key_, what, 0, symbol, sig, defaulted);
}

// The own parameters after `this` and a VTT: an inheriting or copying constructor's, the deleting destructor's flag ([class.inhctor]/8,
// a reference arriving as its address, a scalar as itself; a class by value is not forwarded here); then cl's most-derived flag.
void Builder::ownParameters(const FunctionFacts &facts, const std::string &kind) {
    Lowering &l = l_;
    if (facts.paramCount != 1 + (facts.takesVtt ? 1 : 0) + facts.paramTypes.size() + (facts.msFlag ? 1 : 0))
        l.outside("a compiler-written " + kind + " with parameters of its own");
    for (TypeId pt : facts.paramTypes) {
        const Type &t = l.T(pt);
        if (t.isReference() ? !Lowering::referable(*t.referent()) : !Lowering::scalar(t)) l.outside("a compiler-written " + kind + " forwarding a '" + t.describe() + "'");
        l.forwarded_.push_back(l.param(l.F().entry, t.isReference() ? l.tid(*l.types_.pointerTo(t.referent())) : pt));
        l.F().params.push_back(l.forwarded_.back());
    }
    if (!facts.msFlag) return;
    l.msFlag_ = l.param(l.F().entry, l.types_.intType()->id());
    l.F().params.push_back(l.msFlag_);
}

// A C1 or D1 reads its class's VTT where a C2 or D2 is handed one: `_ZTT<cls>`, the symbol its synthesiser recorded.
void Builder::vttHeld() {
    std::string symbol;
    TypeId type;
    if (!recorded("$vtt", symbol, type)) return;
    const InstId a = l_.add(Op::Addr, type, std::vector<ValueId>());
    l_.at(a).sym = symbol;
    l_.vtt_ = l_.result(a);
}

FuncId Builder::completeObject(const FunctionFacts &facts, const std::string &kind, std::size_t index) {
    Lowering &l = l_;
    l.beginFunction(facts);
    if (!l.this_.valid()) l.outside("a compiler-written " + kind + " with no `this`");
    ownParameters(facts, kind);
    const Type &cls = *l.T(l.thisType_).pointee()->unqualified();
    l.returnBlock_ = l.newBlock();
    const BlockId first = l.newBlock();
    l.br(first);
    l.place(first);
    l.liveFloor_ = 0;
    ValueId carried;
    if (kind == "deleting destructor") carried = deleting(cls);
    else if (kind == "complete destructor") completeDtor(cls);
    else completeCtor(cls);
    // A constructor's virtual bases stay built on its normal way out; only its cleanups destroy them ([except.ctor]/3).
    l.closeScope(0, true);
    if (carried.valid()) l.br(l.returnBlock_, std::vector<ValueId>(1, carried));
    else l.br(l.returnBlock_);
    l.finishFunction(facts.landingPads);
    return FuncId(static_cast<std::uint32_t>(index));
}

// [class.dtor]: the destructor of the complete object - D1, or cl's ??_D for a class with virtual bases, as `delete p`
// names it - then the deallocation the parser chose; cl's ??_G frees only where its flag's low bit is set, and hands `this` back.
ValueId Builder::deleting(const Type &cls) {
    Lowering &l = l_;
    const std::string dtor = l.destructor_ ? l.destructor_(cls.id()) : std::string();
    if (!dtor.empty())
        l.directCall(dtor, l.types_.functionType(l.types_.voidType()->id(), std::vector<TypeId>(1, l.thisType_)), std::vector<ValueId>(1, l.this_));
    std::string frees;
    TypeId sig;
    if (!recorded("$delete", frees, sig)) l.outside("a deleting destructor whose deallocation the parser recorded none of");
    const TypeId voidPtr = l.types_.pointerTo(l.types_.voidType()->id());
    const ValueId raw = l.result(l.add(Op::Bitcast, voidPtr, std::vector<ValueId>(1, l.this_)));
    if (!l.target_.microsoftNames()) {
        l.directCall(frees, sig, std::vector<ValueId>(1, raw));
        return ValueId();
    }
    if (l.forwarded_.size() != 1) l.outside("a deleting destructor with no flag on the Microsoft ABI");
    const TypeId flagType = l.F().value(l.forwarded_[0]).type;
    const BlockId free = l.newBlock(), done = l.newBlock();
    l.condbr(l.truth(Lowering::Val(l.op2(Op::And, flagType, l.forwarded_[0], l.iconst(flagType, 1)), flagType)).v, free, done);
    l.place(free);
    l.directCall(frees, sig, std::vector<ValueId>(1, raw));
    l.br(done);
    l.place(done);
    return raw;
}

// The virtual base `tag` names among the class's - the synthesiser built or destroyed them in this order - or -1.
static int virtualBaseNamed(const Type &cls, const std::string &tag) {
    for (std::size_t b = 0; b < cls.bases().size(); b++)
        if (cls.bases()[b].isVirtual && cls.bases()[b].type->unqualified()->tag() == tag) return static_cast<int>(b);
    return -1;
}

// D1: D2 on the whole object, handed the VTT where it takes one, then each virtual base by its D2 (cl's ??1), last built first.
void Builder::completeDtor(const Type &cls) {
    Lowering &l = l_;
    std::string symbol;
    TypeId sig;
    if (!recorded("$D2", symbol, sig)) l.outside("a complete destructor whose base-object destructor the parser recorded none of");
    vttHeld();
    std::vector<ValueId> ops(1, l.this_);
    if (l.T(sig).params().size() == 2) ops.push_back(l.vttAt(0));
    if (l.T(sig).params().size() != ops.size()) l.outside("a complete destructor of another shape");
    l.directCall(symbol, sig, ops);
    std::vector<sema::VptrStore> none;
    std::vector<std::string> order;
    if (!l.msVbaseInit_ || !l.msVbaseInit_(l.key_, none, order)) l.outside("a complete destructor whose virtual bases the parser recorded no order for");
    for (const std::string &tag : order) {
        const int which = virtualBaseNamed(cls, tag);
        if (which < 0) l.outside("a virtual base the class does not lay out");
        const Type::BaseSpec &b = cls.bases()[static_cast<std::size_t>(which)];
        const Type &bt = *b.type->unqualified();
        if (!recorded("~" + tag, symbol, sig)) l.outside("a virtual base destroyed by no destructor the parser recorded");
        const Type &ft = l.T(sig);
        std::vector<ValueId> at(1, l.result(l.add(Op::Bitcast, l.tid(*ft.params()[0]), std::vector<ValueId>(1, l.baseAddress(b)))));
        if (ft.params().size() != l.baseVttArgument(bt, ft, at)) l.outside("a virtual base's destructor of another shape");
        l.directCall(symbol, sig, at);
    }
}

// C1: each virtual base in [class.base.init]/10's order by the constructor recorded for it - its default one, or a copy or move
// out of the source's own ([class.copy]/15, the one own parameter) - guarded for the parts after it; then C2, handed the VTT.
void Builder::completeCtor(const Type &cls) {
    Lowering &l = l_;
    vttHeld();
    std::vector<sema::VptrStore> none;
    std::vector<std::string> order;
    if (!l.msVbaseInit_ || !l.msVbaseInit_(l.key_, none, order)) l.outside("a complete constructor whose virtual bases the parser recorded no order for");
    const TypeId chars = l.types_.pointerTo(l.types_.charType()->id());
    for (const std::string &tag : order) {
        const int which = virtualBaseNamed(cls, tag);
        if (which < 0) l.outside("a virtual base the class does not lay out");
        const Type::BaseSpec &b = cls.bases()[static_cast<std::size_t>(which)];
        const Type &bt = *b.type->unqualified();
        std::string symbol;
        TypeId sig;
        if (!recorded("^" + tag, symbol, sig)) l.outside("a virtual base built by no constructor the parser recorded");
        const Type &ft = l.T(sig);
        std::vector<ValueId> ops(1, l.result(l.add(Op::Bitcast, l.tid(*ft.params()[0]), std::vector<ValueId>(1, l.baseAddress(b)))));
        const std::size_t skip = l.baseVttArgument(bt, ft, ops);
        if (ft.params().size() == skip + 1 && l.forwarded_.size() == 1 && ft.params()[skip]->isReference() &&
            ft.params()[skip]->referent()->unqualified() == &bt && l.T(l.F().value(l.forwarded_[0]).type).pointee()->unqualified() == &cls) {
            // The source's own V, at the same offset: `*(V *)((char *)that + offset)`.
            const InstId g = l.add(Op::Gep, chars, std::vector<ValueId>(1, l.result(l.add(Op::Bitcast, chars, std::vector<ValueId>(1, l.forwarded_[0])))));
            l.at(g).k = b.offset;
            ops.push_back(l.result(l.add(Op::Bitcast, l.tid(*l.types_.pointerTo(ft.params()[skip]->referent())), std::vector<ValueId>(1, l.result(g)))));
        } else if (ft.params().size() != skip) {
            l.outside("a complete constructor building a virtual base from a mem-initialiser's arguments");
        }
        l.directCall(symbol, sig, ops);
        if (!bt.hasDestructor()) continue;
        // [except.ctor]/3: built, it is destroyed by its D2 - handed the same virtual VTT - if a later part throws.
        if (!recorded("~" + tag, symbol, sig)) l.outside("a virtual base destroyed on unwinding by no destructor the parser recorded");
        std::vector<ValueId> at(1, l.result(l.add(Op::Bitcast, l.tid(*l.T(sig).params()[0]), std::vector<ValueId>(1, l.baseAddress(b)))));
        if (l.T(sig).params().size() != l.baseVttArgument(bt, l.T(sig), at)) l.outside("a virtual base's destructor of another shape");
        l.guard(at[0], bt, symbol);
        if (at.size() == 2) l.live_.back().vtt = at[1];
    }
    std::string symbol;
    TypeId sig;
    if (!recorded("$C2", symbol, sig)) l.outside("a complete constructor whose base-object constructor the parser recorded none of");
    std::vector<ValueId> ops(1, l.this_);
    if (l.vtt_.valid()) ops.push_back(l.vttAt(0));
    ops.insert(ops.end(), l.forwarded_.begin(), l.forwarded_.end());
    if (l.T(sig).params().size() != ops.size()) l.outside("a complete constructor of another shape");
    l.directCall(symbol, sig, ops);
}

}   // namespace ir
