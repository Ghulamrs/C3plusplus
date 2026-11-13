// **The thunks and the init function** (M3b.3, lane C): a thunk moves `this` from the subobject a call arrived through to
// the object its override was compiled against, by what the ABI states in its name (Classes::synthesizeThunk and
// synthesizeVirtualThunk); the init function builds each namespace-scope object as the parser recorded it (ParserInit.cpp).

#include "Builder.h"

#include <cstdlib>

namespace ir {

// _ZThn16_N1C1gEv: back 16, to _ZN1C1gEv; _ZTv0_n24_N1D1fEv: by 0, then by the vcall offset 24 bytes before that
// vptr's address point; Microsoft's ?g@C@@UEAAHXZ$adj16: back 16; its vcall thunk ??_9S@@$B7AA: the vftable's slot at byte
// 8 (cl's number: A@ 0, a digit d d+1, else hex in A-P and @), `callee` empty. False for a name of none of the four shapes.
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
    const std::size_t b = s.rfind("$B");
    if (s.compare(0, 4, "??_9") == 0 && b != std::string::npos && b + 4 < s.size()) {
        std::size_t i = b + 2;
        if (s[i] >= '0' && s[i] <= '9') vcall = s[i++] - '0' + 1;
        else for (; i < s.size() && s[i] >= 'A' && s[i] <= 'P'; i++) vcall = vcall * 16 + (s[i] - 'A');
        if (s[b + 2] > '9' && (i >= s.size() || s[i++] != '@')) return false;
        fixed = 0;
        return s.compare(i, std::string::npos, "AA") == 0;
    }
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
    ValueId self = l.this_;
    if (!callee.empty()) {
        const InstId moved = l.add(Op::Gep, chars, std::vector<ValueId>(1, l.this_));
        l.at(moved).k = fixed;
        self = l.result(moved);
    }
    if (virt && !callee.empty()) {
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
    const TypeId sig = l.types_.functionType(l.returns_, params);
    // A vcall thunk calls through the vptr's slot, as memberCall's dispatch does (virtualCallee).
    if (callee.empty()) ops.insert(ops.begin(), l.virtualCallee(ops[0], sig, static_cast<int>(vcall / l.T(l.thisType_).size(l.target_))));
    const Lowering::Val r = l.callTail(sig, info, ops, params.size(),
                                      std::vector<sema::TypedId>(), false);
    if (rt.isVoid()) l.br(l.returnBlock_);
    else l.br(l.returnBlock_, std::vector<ValueId>(1, r.v));
    l.finishFunction(facts.landingPads);
    return FuncId(static_cast<std::uint32_t>(l.fnIndex_));
}

// _GLOBAL__sub_I_<file>, [basic.start.init]/2: each object in the order the parser met it, built and its destructor registered
// as a static local's are (staticConstruction) - a template's static member under its weak guard, the first unit to run building it.
// __tls_init is its per-thread twin ([basic.stc.thread]), under the thread_local byte __tls_guard on Itanium (finishTlsInit).
FuncId Builder::initFunction(const FunctionFacts &facts, bool tls) {
    Lowering &l = l_;
    std::vector<sema::DynamicInit> inits;
    if (!l.dynamicInits_ || !l.dynamicInits_(inits, tls) || inits.empty()) l.broken("an init function whose objects the parser recorded no `var` for");
    l.beginFunction(facts);
    if (l.this_.valid() || facts.paramCount != 0 || !l.T(l.returns_).isVoid()) l.outside("an init function with parameters");
    l.returnBlock_ = l.newBlock();
    const BlockId first = l.newBlock();
    l.br(first);
    l.place(first);
    const bool ms = l.target_.microsoftNames();
    const TypeId intT = l.tid(*l.types_.intType()), boolT = l.tid(*l.types_.get(Kind::Bool));
    BlockId once;
    if (tls && !ms) {
        const TypeId byte = l.tid(*l.types_.get(Kind::UChar));
        const ValueId guard = l.staticAddr("__tls_guard", l.types_.pointerTo(byte));
        std::vector<ValueId> ops(1, l.promote(Lowering::Val(l.load(guard, byte), byte)).v);
        ops.push_back(l.iconst(intT, 0));
        const InstId fresh = l.add(Op::ICmp, boolT, ops);
        l.at(fresh).cc = Cmp::Eq;
        const BlockId build = l.newBlock();
        once = l.newBlock();
        l.condbr(l.result(fresh), build, once);
        l.place(build);
        l.store(guard, l.iconst(byte, 1));
    }
    for (const sema::DynamicInit &e : inits) {
        BlockId done;
        if (!e.once.empty()) {
            // guardTemplateMember's test: the guard's first byte on Itanium, its int on Microsoft, set before the object is built.
            const TypeId gt = l.tid(*l.types_.get(ms ? Kind::Int : Kind::UChar));
            const ValueId guard = l.addrOf(e.once, l.types_.pointerTo(gt));
            std::vector<ValueId> ops(1, l.promote(Lowering::Val(l.load(guard, gt), gt)).v);
            ops.push_back(l.iconst(intT, 0));
            const InstId fresh = l.add(Op::ICmp, boolT, ops);
            l.at(fresh).cc = Cmp::Eq;
            const BlockId build = l.newBlock();
            done = l.newBlock();
            l.condbr(l.result(fresh), build, done);
            l.place(build);
            l.store(guard, l.iconst(gt, 1));
        }
        if (l.tree_.node(e.var).type == nullptr) l.outside("an init function's object not typed");
        if (tls && (l.tree_.node(e.var).type->isReference() || !l.threadLocalGlobal(e.symbol))) l.outside("a thread_local reference, or no thread_local, in __tls_init");
        if (l.tree_.node(e.var).type->isArray() && !e.built.array && !e.built.elements.recorded()) l.outside("an array built at run time before main");
        l.staticConstruction(e.var, e.symbol, e.built);
        if (done.valid()) { l.br(done); l.place(done); }
    }
    if (once.valid()) { l.br(once); l.place(once); }
    l.br(l.returnBlock_);
    l.finishFunction(facts.landingPads);
    return FuncId(static_cast<std::uint32_t>(l.fnIndex_));
}

}   // namespace ir
