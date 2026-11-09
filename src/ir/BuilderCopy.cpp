// **Lane A's kinds of the Builder** (M3b.3): the implicit copy and move constructors and assignments
// (Classes::synthesizeCopy), the array loops `__cxx1_vec_new_`/`_del_` (Parser::vectorConstructor, vectorDestructor) and
// the static array's exit helper (buildStaticArrayConstruction) - each from the class's layout and the synthesiser's records.

#include "Builder.h"

namespace ir {

// `base + i * size`: element `i` of an array of `et`, through char arithmetic as indexBytes writes it.
ValueId Builder::elementAt(ValueId base, ValueId i, const Type &et) {
    Lowering &l = l_;
    const TypeId chars = l.tid(*l.types_.pointerTo(l.types_.charType()));
    const TypeId diff = l.ptrdiffType();
    std::vector<ValueId> ops(1, l.result(l.add(Op::Bitcast, chars, std::vector<ValueId>(1, base))));
    ops.push_back(l.op2(Op::Mul, diff, l.convert(Lowering::Val(i, l.types_.intType()->id()), diff).v, l.iconst(diff, et.size(l.target_))));
    return l.result(l.add(Op::Bitcast, l.types_.pointerTo(et.id()), std::vector<ValueId>(1, l.result(l.add(Op::Gep, chars, ops)))));
}

// `at` bytes into the object `p` points at, as a pointer to `to`.
ValueId Builder::into(ValueId p, long long at, TypeId to) {
    const InstId g = l_.add(Op::Gep, to, std::vector<ValueId>(1, p));
    l_.at(g).k = at;
    return l_.result(g);
}

// The entry block closed and the body's first block opened, as build() opens every compiler-written function.
void Builder::openBody() {
    l_.returnBlock_ = l_.newBlock();
    const BlockId first = l_.newBlock();
    l_.br(first);
    l_.place(first);
    l_.liveFloor_ = 0;
}

// The record `name` the synthesiser left under this function's key, its signature's parameter `at` a pointer to `cls`.
bool Builder::recorded(const std::string &name, const Type &cls, std::size_t at, std::string &symbol, TypeId &sig) {
    bool defaulted = false;
    if (!l_.memberBuilt_ || !l_.memberBuilt_(l_.key_, name, 0, symbol, sig, defaulted)) return false;
    const Type &ft = l_.T(sig);
    if (ft.params().size() <= at || ft.params()[at]->pointee()->unqualified() != &cls) l_.outside("a compiler-written function's record of another class");
    if (defaulted) l_.outside("a compiler-written function's call with default arguments");
    return true;
}

// [class.copy]/15 and /28, as synthesizeCopy writes them: each non-virtual base by the constructor or assignment the parser
// chose, the vptrs (a constructor's), then each member not inside such a base - by the parser's choice for its class, each
// element of an array, or its bytes; an assignment returns `this`. A constructor's parts built are guarded ([except.ctor]/3).
void Builder::copyOrMove(const FunctionFacts &facts) {
    Lowering &l = l_;
    l.beginFunction(facts);
    const bool assigning = !l.T(l.returns_).isVoid();
    const std::string what = assigning ? "an implicit copy or move assignment" : "an implicit copy or move constructor";
    if (!l.this_.valid()) l.outside(what + " with no `this`");
    const Type &cls = *l.T(l.thisType_).pointee()->unqualified();
    if (cls.hasVirtualBase()) l.outside(what + " of a class with a virtual base");
    if (facts.paramTypes.size() != 1 || !l.T(facts.paramTypes[0]).isPointer() || facts.paramCount != 2) l.outside(what + " with parameters it does not take");
    if (assigning && (!l.T(l.returns_).isPointer() || l.T(l.returns_).pointee()->unqualified() != &cls)) l.outside(what + " returning another type");
    if (l.T(facts.paramTypes[0]).pointee()->unqualified() != &cls) l.outside(what + " from another class");
    const ValueId that = l.param(l.F().entry, facts.paramTypes[0]);
    l.F().params.push_back(that);
    openBody();
    std::string symbol;
    TypeId sig;
    // What a base's own copy or move has dealt with: its members are not copied again below.
    std::vector<std::pair<int, int> > taken;
    for (const Type::BaseSpec &b : cls.bases()) {
        const Type &bt = *b.type->unqualified();
        if (!recorded("^" + bt.tag(), bt, 0, symbol, sig)) continue;
        const Type &ft = l.T(sig);
        if (ft.params().size() != 2 || !ft.params()[1]->isReference() || ft.params()[1]->referent()->unqualified() != &bt)
            l.outside(what + " whose base's copy takes other parameters");
        std::vector<ValueId> ops(1, l.result(l.add(Op::Bitcast, l.tid(*ft.params()[0]), std::vector<ValueId>(1, l.baseAddress(b)))));
        ops.push_back(into(that, b.offset, l.tid(*l.types_.pointerTo(ft.params()[1]->referent()))));
        l.directCall(symbol, sig, ops);
        if (!assigning && bt.hasDestructor()) {
            if (!recorded("~" + bt.tag(), bt, 0, symbol, sig)) l.outside(what + " whose base is destroyed on unwinding by no destructor the parser recorded");
            l.guard(ops[0], bt, symbol);
        }
        taken.push_back(std::make_pair(b.offset, b.offset + bt.dataSize()));
    }
    if (!assigning && cls.polymorphic()) {
        if (!l.vptrStores_ || !l.vptrStores_(l.key_, l.vptrs_) || l.vptrs_.empty() || l.vptrs_[0].symbol.empty()) l.outside(what + " with a vptr the parser recorded no store for");
        l.storeVptrs();
    }
    for (const Member &m : cls.members()) {
        bool done = false;
        for (std::size_t k = 0; k < taken.size(); k++) done = done || (m.offset >= taken[k].first && m.offset < taken[k].second);
        if (done) continue;
        if (m.width != 0 || m.type->isReference()) l.outside(what + " of a class with a bit-field or reference member");
        long long elems = 1;
        TypeId et = l.tid(*m.type);
        for (; l.T(et).isArray(); et = l.tid(*l.T(et).pointee())) {
            if (l.T(et).length() < 0) l.outside(what + " of a class with an array member of no length");
            elems *= l.T(et).length();
        }
        const Type &e = *l.T(et).unqualified();
        const TypeId whole = l.tid(*l.types_.pointerTo(m.type->unqualified()));
        if (!recorded(m.name, e, 0, symbol, sig)) { l.copyObject(into(l.this_, m.offset, whole), into(that, m.offset, whole), *m.type); continue; }
        const Type &ft = l.T(sig);
        if (ft.params().size() != 2 || !ft.params()[1]->isReference() || ft.params()[1]->referent()->unqualified() != &e)
            l.outside(what + " whose member's copy takes other parameters");
        if (elems > (e.hasDestructor() ? 16 : 64)) l.outside(what + " of an array member of more elements than the lowering copies one by one");
        const std::string dtor = !assigning && e.hasDestructor() && l.destructor_ ? l.destructor_(e.id()) : std::string();
        if (!assigning && e.hasDestructor() && dtor.empty()) l.outside(what + " whose member is destroyed on unwinding by no destructor the parser recorded");
        for (long long k = 0; k < elems; k++) {
            std::vector<ValueId> ops(1, into(l.this_, m.offset + k * e.size(l.target_), l.tid(*ft.params()[0])));
            ops.push_back(into(that, m.offset + k * e.size(l.target_), l.tid(*l.types_.pointerTo(ft.params()[1]->referent()))));
            l.directCall(symbol, sig, ops);
            if (!dtor.empty()) l.guard(ops[0], e, dtor);
        }
    }
    l.closeScope(0, true);
    if (assigning) l.br(l.returnBlock_, std::vector<ValueId>(1, l.this_));
    else l.br(l.returnBlock_);
    l.finishFunction(facts.landingPads);
}

// `__cxx1_vec_new_T(base, n)`: `for (i = 0; i < n; i++) T::T(base + i)`, the elements built so far destroyed by `_del_` if one
// throws; `__cxx1_vec_del_T(base, n)`: `i = n; while (i > 0) ~T(base + --i)` - the parser's two loops, [class.dtor] last first.
void Builder::vectorLoop(const FunctionFacts &facts, bool constructs) {
    Lowering &l = l_;
    l.beginFunction(facts);
    const std::string what = constructs ? "a vector constructor" : "a vector destructor";
    if (l.this_.valid() || facts.paramTypes.size() != 2 || facts.paramCount != 2 || !l.T(facts.paramTypes[0]).isPointer() || !l.T(l.returns_).isVoid())
        l.outside(what + " with parameters it does not take");
    const Type &cls = *l.T(facts.paramTypes[0]).pointee()->unqualified();
    if (cls.hasVirtualBase()) l.outside(what + " of a class with a virtual base");
    const ValueId base = l.param(l.F().entry, facts.paramTypes[0]), n = l.param(l.F().entry, facts.paramTypes[1]);
    l.F().params.push_back(base);
    l.F().params.push_back(n);
    const TypeId intT = l.types_.intType()->id(), boolT = l.tid(*l.types_.get(Kind::Bool));
    const ValueId i = l.entryAlloca(intT, "$i");
    openBody();
    std::string symbol, loop;
    TypeId sig, loopSig;
    const ValueId count = l.convert(Lowering::Val(n, facts.paramTypes[1]), intT).v;
    if (constructs) {
        if (!recorded("^element", cls, 0, symbol, sig)) l.outside(what + " built by no constructor the parser recorded");
        if (l.T(sig).params().size() != 1) l.outside(what + " whose constructor takes parameters");
        l.store(i, l.iconst(intT, 0));
        if (recorded("~elements", cls, 0, loop, loopSig)) { l.guard(base, cls, loop); l.live_.back().count = i; }
    } else {
        symbol = l.destructor_ ? l.destructor_(cls.id()) : std::string();
        if (symbol.empty()) l.outside(what + " of a class whose destructor the parser recorded none for");
        sig = l.types_.functionType(l.types_.voidType()->id(), std::vector<TypeId>(1, l.types_.pointerTo(cls.id())));
        l.store(i, count);
    }
    const BlockId cond = l.newBlock(), body = l.newBlock(), done = l.newBlock();
    l.br(cond);
    l.place(cond);
    const ValueId now = l.load(i, intT);
    std::vector<ValueId> cmp(1, now);
    cmp.push_back(constructs ? count : l.iconst(intT, 0));
    const InstId c = l.add(Op::ICmp, boolT, cmp);
    l.at(c).cc = constructs ? Cmp::Slt : Cmp::Sgt;
    l.condbr(l.result(c), body, done);
    l.place(body);
    const ValueId index = constructs ? now : l.op2(Op::Sub, intT, now, l.iconst(intT, 1));
    if (!constructs) l.store(i, index);
    l.directCall(symbol, sig, std::vector<ValueId>(1, l.result(l.add(Op::Bitcast, l.tid(*l.T(sig).params()[0]), std::vector<ValueId>(1, elementAt(base, index, cls))))));
    if (constructs) l.store(i, l.op2(Op::Add, intT, now, l.iconst(intT, 1)));
    l.br(cond);
    l.place(done);
    l.closeScope(0, true);
    l.br(l.returnBlock_);
    l.finishFunction(facts.landingPads);
}

// The static array's exit helper: `__cxx1_vec_del_T(first, count)` over the array, handed as `__cxa_atexit`'s `void *` on
// Itanium and named by its symbol on Microsoft, where `atexit` hands nothing.
void Builder::arrayHelper(const FunctionFacts &facts) {
    Lowering &l = l_;
    l.beginFunction(facts);
    const std::string what = "an array destructor helper";
    std::string loop, array;
    TypeId loopSig, arrayType;
    bool defaulted = false;
    if (l.this_.valid() || !l.T(l.returns_).isVoid() || !l.memberBuilt_ || !l.memberBuilt_(l.key_, "~elements", 0, loop, loopSig, defaulted) ||
        !l.memberBuilt_(l.key_, "$array", 0, array, arrayType, defaulted))
        l.outside(what + " the parser recorded no loop or array for");
    long long count = 1;
    TypeId et = arrayType;
    for (; l.T(et).isArray(); et = l.tid(*l.T(et).pointee())) count *= l.T(et).length();
    const Type &ft = l.T(loopSig);
    if (ft.params().size() != 2 || ft.params()[0]->pointee()->unqualified() != l.T(et).unqualified()) l.outside(what + " whose loop takes other parameters");
    ValueId first;
    if (l.target_.microsoftNames()) {
        if (facts.paramCount != 0 || l.threadLocalGlobal(array)) l.outside(what + " of a thread_local array");
        first = l.addrOf(array, l.tid(*ft.params()[0]));
    } else {
        if (facts.paramCount != 1 || facts.paramTypes.size() != 1) l.outside(what + " with parameters it does not take");
        const ValueId a0 = l.param(l.F().entry, facts.paramTypes[0]);
        l.F().params.push_back(a0);
        first = l.result(l.add(Op::Bitcast, l.tid(*ft.params()[0]), std::vector<ValueId>(1, a0)));
    }
    openBody();
    std::vector<ValueId> ops(1, first);
    ops.push_back(l.iconst(l.tid(*ft.params()[1]), count));
    l.directCall(loop, loopSig, ops);
    l.br(l.returnBlock_);
    l.finishFunction(facts.landingPads);
}

}   // namespace ir
