// **The thunks** (M3b.3, lane C): a slot of a secondary vtable holds one, which moves `this` from the subobject the
// call arrived through to the object the override was compiled against and calls it (Classes::synthesizeThunk,
// synthesizeVirtualThunk). What it moves by is the ABI's own statement in the thunk's name, so the name is read here.

#include "Builder.h"

#include <cstdlib>

namespace ir {

// _ZThn16_N1C1gEv: back 16, to _ZN1C1gEv; _ZTv0_n24_N1D1fEv: by 0, then by the vcall offset 24 bytes before that
// vptr's address point; Microsoft's ?g@C@@UEAAHXZ$adj16: back 16. False for a name of none of the three shapes.
static bool readThunkName(const std::string &s, bool virt, long long &fixed, long long &vcall, std::string &callee) {
    auto number = [&s](std::size_t &i, long long &out) {
        const bool neg = i < s.size() && s[i] == 'n';
        if (neg) i++;
        const std::size_t from = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
        if (i == from || i >= s.size() || s[i] != '_') return false;
        out = std::strtoll(s.substr(from, i - from).c_str(), nullptr, 10) * (neg ? -1 : 1);
        i++;
        return true;
    };
    vcall = 0;
    const std::size_t adj = s.rfind("$adj");
    if (!virt && adj != std::string::npos && s[0] == '?') {
        fixed = -std::strtoll(s.c_str() + adj + 4, nullptr, 10);
        callee = s.substr(0, adj);
        return true;
    }
    std::size_t i = 4;
    if (s.compare(0, 4, virt ? "_ZTv" : "_ZTh") != 0 || !number(i, fixed)) return false;
    if (virt && (!number(i, vcall) || vcall >= 0)) return false;
    callee = "_Z" + s.substr(i);
    return i < s.size();
}

FuncId Builder::thunk(const FunctionFacts &facts, bool virt) {
    Lowering &l = l_;
    long long fixed = 0, vcall = 0;
    std::string callee;
    if (!readThunkName(facts.symbol, virt, fixed, vcall, callee)) l.outside("a thunk whose name states no adjustment");
    l.beginFunction(facts);
    if (!l.this_.valid() || facts.takesVtt || facts.paramCount != 1 + facts.paramTypes.size()) l.outside("a thunk with no `this`");
    // Its parameters forwarded as they arrived: a reference as its address, a scalar as itself; a class by value is not.
    std::vector<TypeId> params(1, l.thisType_);
    std::vector<ValueId> ops(1, ValueId());
    for (TypeId pt : facts.paramTypes) {
        const Type &t = l.T(pt);
        if (t.isReference() ? !Lowering::referable(*t.referent()) : !Lowering::scalar(t)) l.outside("a thunk forwarding a '" + t.describe() + "'");
        ops.push_back(l.param(l.F().entry, t.isReference() ? l.tid(*l.types_.pointerTo(t.referent())) : pt));
        l.F().params.push_back(ops.back());
        params.push_back(pt);
    }
    const Type &rt = l.T(l.returns_);
    if (!(rt.isVoid() || Lowering::scalar(rt) || (rt.isReference() && Lowering::referable(*rt.referent())))) l.outside("a thunk returning a '" + rt.describe() + "'");
    l.returnBlock_ = l.newBlock();
    const BlockId first = l.newBlock();
    l.br(first);
    l.place(first);
    // this + fixed; for a virtual thunk, + the ptrdiff_t at -vcall from the vptr found there (the vcall offset).
    const TypeId chars = l.types_.pointerTo(l.types_.charType()->id());
    const InstId moved = l.add(Op::Gep, chars, std::vector<ValueId>(1, l.this_));
    l.at(moved).k = fixed;
    ValueId self = l.result(moved);
    if (virt) {
        const ValueId vptr = l.load(l.result(l.add(Op::Bitcast, l.types_.pointerTo(chars), std::vector<ValueId>(1, self))), chars);
        const InstId slot = l.add(Op::Gep, l.types_.pointerTo(l.ptrdiffType()), std::vector<ValueId>(1, vptr));
        l.at(slot).k = vcall;
        std::vector<ValueId> by(1, self);
        by.push_back(l.load(l.result(slot), l.ptrdiffType()));
        self = l.result(l.add(Op::Gep, chars, by));
    }
    ops[0] = l.result(l.add(Op::Bitcast, l.thisType_, std::vector<ValueId>(1, self)));
    CallInfo info;
    info.symbol = callee;
    info.hasThis = true;
    const Lowering::Val r = l.callTail(l.types_.functionType(l.returns_, params), info, ops, params.size(),
                                      std::vector<sema::TypedId>(), false);
    if (rt.isVoid()) l.br(l.returnBlock_);
    else l.br(l.returnBlock_, std::vector<ValueId>(1, r.v));
    l.finishFunction(facts.landingPads);
    return FuncId(static_cast<std::uint32_t>(l.fnIndex_));
}

}   // namespace ir
