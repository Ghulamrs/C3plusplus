// **The builder** (M3b.3): the parser's synthesisers build these functions straight into Ast.h from the class's layout;
// here each is built as SSA by the lowering's own pieces, from the same layout and the records the synthesiser leaves of
// what it chose (Classes::synthesizeDestructor, synthesizeDefaultCtor), so that a kind lowered here is the Ast.h path's twin.

#include "Builder.h"

namespace ir {

FuncId Builder::build(const FunctionFacts &facts, const std::string &kind, std::string &why) {
    Lowering &l = l_;
    const std::size_t index = l.module_.functions.size();
    try {
        const bool dtor = kind == "implicit destructor", ctor = kind == "implicit default constructor";
        if (!dtor && !ctor) l.outside("a compiler-written " + kind);
        l.beginFunction(facts);
        if (!l.this_.valid() || !l.T(l.returns_).isVoid()) l.outside("a compiler-written " + kind + " with no `this`");
        const Type &cls = *l.T(l.thisType_).pointee()->unqualified();
        // The parts a virtual base adds - cl's flag and guard, Itanium's C1/C2 and D1/D2 split and their VTT - are not built.
        if (cls.hasVirtualBase() || facts.takesVtt || facts.msFlag) l.outside("a compiler-written " + kind + " of a class with a virtual base");
        // An inheriting constructor forwards parameters of its own ([class.inhctor]).
        if (facts.paramCount != 1) l.outside("a compiler-written " + kind + " with parameters of its own");
        if (!l.simpleLayout(cls) && !l.buildableLayout(cls, true, true, false))
            l.outside("a compiler-written " + kind + " of a class with a member the lowering does not build");
        if (ctor && cls.hasVptr() && !(l.target_.microsoftNames() && !cls.polymorphic()) &&
            (!l.vptrStores_ || !l.vptrStores_(l.key_, l.vptrs_) || l.vptrs_.empty() || (!l.vptrs_[0].fromVtt && l.vptrs_[0].symbol.empty())))
            l.outside("a compiler-written " + kind + " with a vptr the parser recorded no store for");
        l.returnBlock_ = l.newBlock();
        const BlockId first = l.newBlock();
        l.br(first);
        l.place(first);
        l.liveFloor_ = 0;
        // [class.base.init] with no list - memberInits for a written constructor whose list names nothing; and [class.dtor]/8
        // with no body - the members last declared first, then the bases' D2s in reverse, no vptr stored (synthesizeDestructor).
        if (ctor) l.memberInits(sema::TypedId(), 0, 0);
        else { l.destroyMembers(cls); l.destroyBases(cls); }
        // A constructor's parts stay built on its normal way out; only its cleanups destroy them ([except.ctor]/3).
        l.closeScope(0, true);
        l.br(l.returnBlock_);
        l.finishFunction(facts.landingPads);
        return FuncId(static_cast<std::uint32_t>(index));
    } catch (const Lowering::Outside &o) {
        why = o.why;
        if (l.module_.functions.size() > index) l.module_.functions.pop_back();
        return FuncId();
    }
}

}   // namespace ir
