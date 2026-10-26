// ir::Lowering - see Lowering.h. The subset it reaches at M3b.1 is the scalar core of the adapter's: free functions
// over scalars, pointers, 1-D arrays of scalars and plain structs' scalar members, with every statement kind but
// `try`; a class object, a temporary, a member call, `this`, a static local or a throw leaves the function outside.

#include "Lowering.h"

#include <cstring>

namespace ir {

Lowering::Lowering(const sema::TypedTree &tree, const syntax::Tree &syntax, TypeTable &types, const Target &target,
                   Program &program, Module &into)
    : tree_(tree), syntax_(syntax), types_(types), target_(target), program_(program), module_(into), fnIndex_(0), handlerDepth_(0), hasGoto_(false) {}

void Lowering::outside(const std::string &why) { throw Outside(why); }

bool Lowering::scalarArithmetic(const Type &t) { return t.isArithmetic() && !t.isScopedEnumeration(); }
bool Lowering::scalar(const Type &t) { return scalarArithmetic(t) || t.isPointer(); }
// [class]/6 and [basic.types]/9 as far as the lowering needs them: a complete class with no vptr, no virtual base, no
// destructor, no copy of its own and no constructor - an object of it is its bytes, made and copied as such.
bool Lowering::plainClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete()) return false;
    const Type &c = *t.unqualified();
    return !c.polymorphic() && !c.hasVirtualBase() && !c.hasDestructor() && !c.nonTrivialCopy() &&
           !(constructed_ && constructed_(&c));
}

// [class.base.init] for a class with no base and no vptr whose members are scalars or plain classes: nothing is
// built but what the list names, so every member's code is in the constructor's syntax but a default initialiser.
bool Lowering::simpleLayout(const Type &cls) const {
    if (cls.polymorphic() || cls.hasVirtualBase() || !cls.bases().empty()) return false;
    for (const Member &m : cls.members()) {
        const Type &t = *m.type->unqualified();
        if (m.width != 0 || m.anonUnion != nullptr || m.type->isReference() || t.isX87(target_)) return false;
        if (!scalar(t) && !plainClass(t) && !(t.isArray() && t.pointee()->isComplete() && scalar(*t.pointee()))) return false;
    }
    return true;
}

// The members in declaration order: each named in the list initialised from it - `m()` and `m{}` value-initialised,
// one value stored or a plain class copied - and one the list leaves alone default-initialised, which is nothing.
void Lowering::memberInits(sema::TypedId fnNode, std::uint32_t from, std::uint32_t to) {
    const Type &cls = *T(thisType_).pointee()->unqualified();
    std::map<std::string, sema::TypedId> named;
    for (std::uint32_t i = from; i < to; i++) {
        const sema::TypedId mi = child(fnNode, i);
        if (syn(mi).kind != syntax::Kind::MemInit) outside("a constructor part that is not a mem-initialiser");
        if (cls.findMember(text(mi)) == nullptr) outside("a mem-initialiser of a base or of the class itself");
        named[text(mi)] = mi;
    }
    for (const Member &m : cls.members()) {
        const std::map<std::string, sema::TypedId>::const_iterator e = named.find(m.name);
        if (e == named.end()) {
            if (defaultInit_ && defaultInit_(&cls, m.name)) outside("a member's default initialiser");
            continue;
        }
        const Type &mt = *m.type->unqualified();
        const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, this_));
        at(g).k = m.offset;
        const std::uint32_t k = count(e->second);
        if (k == 0) {
            const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, result(g)));
            at(z).k = mt.size(target_);
        } else if (k == 1 && scalar(mt)) {
            store(result(g), exprTo(child(e->second, 0), tid(mt)).v);
        } else if (k == 1 && plainClass(mt) && isLvalueNode(child(e->second, 0))) {
            const Val src = lvalue(child(e->second, 0));
            if (T(src.type).unqualified() != &mt) outside("a member built from another type");
            copyObject(result(g), src.v, mt);
        } else {
            outside("a mem-initialiser of " + std::to_string(k) + " values for '" + m.name + "'");
        }
    }
}

// A plain class's local from its form ([dcl.init]): default-initialised, it is left as it is; `{}` zeroes it;
// a braced list of scalars stores each member in order and zeroes the rest; `= o` or `(o)` copies o's bytes.
void Lowering::classLocal(sema::TypedId var, ValueId slot, const Type &t) {
    const sema::TypedNode &v = tree_.node(var);
    if (tree_.resolution(var).what == sema::Resolution::Call) outside("a local a constructor builds");
    const long long size = t.size(target_);
    switch (v.initForm) {
    case 0:
        if (v.count != 0) outside("a class local's initialiser of no form");
        return;
    case 3: {
        const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
        at(z).k = size;
        return;
    }
    case 1: {
        if (v.count != 1 || !isLvalueNode(child(var, 0))) outside("a class local initialised by what is not an object");
        const Val o = lvalue(child(var, 0));
        if (T(o.type).unqualified() != t.unqualified()) outside("a class local initialised from another type");
        copyObject(slot, o.v, t);
        return;
    }
    default: {
        const std::vector<Member> &ms = t.unqualified()->members();
        if (!t.unqualified()->bases().empty() || v.count > ms.size()) outside("a braced list for a class with bases or more values than members");
        for (std::size_t i = 0; i < ms.size(); i++)
            if (!scalar(*ms[i].type) || ms[i].width != 0 || ms[i].anonUnion != nullptr || ms[i].type->isX87(target_))
                outside("a braced list for a member that is not a plain scalar");
        const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
        at(z).k = size;
        for (std::uint32_t i = 0; i < v.count; i++) {
            const TypeId mt = tid(*ms[i].type->unqualified());
            const InstId g = add(Op::Gep, tid(*types_.pointerTo(&T(mt))), std::vector<ValueId>(1, slot));
            at(g).k = ms[i].offset;
            store(result(g), exprTo(child(var, i), mt).v);
        }
        return;
    }
    }
}

void Lowering::copyObject(ValueId to, ValueId from, const Type &t) {
    std::vector<ValueId> ops;
    ops.push_back(to);
    ops.push_back(from);
    const InstId c = add(Op::Memcpy, TypeId(), ops);
    at(c).k = t.size(target_);
}

// What a reference may refer to here: a scalar, or a complete class, which a reference only ever names by its address.
bool Lowering::referable(const Type &t) { return scalar(t) || (t.isStructOrUnion() && t.isComplete()); }

// A constant as the walker keeps every value in a register: sign- or zero-extended to 64 bits by its type.
long long Lowering::normalise(long long v, const Type &t, const Target &target) {
    const int size = t.size(target);
    if (t.isBool()) return v != 0 ? 1 : 0;
    if (size >= 8) return v;
    const unsigned long long mask = (1ULL << (size * 8)) - 1;
    unsigned long long u = static_cast<unsigned long long>(v) & mask;
    if (t.isSigned(target) && (u >> (size * 8 - 1)) != 0) u |= ~mask;
    return static_cast<long long>(u);
}

// ----- the builder

InstId Lowering::add(Op op, TypeId type, const std::vector<ValueId> &ops) {
    Function &f = F();
    if (!cur_.valid() || f.block(cur_).term.valid()) place(newBlock());    // code past a terminator: unreachable, kept
    Inst i;
    i.op = op;
    i.type = type;
    i.first = static_cast<std::uint32_t>(f.operands.size());
    i.count = static_cast<std::uint32_t>(ops.size());
    for (ValueId v : ops) f.operands.push_back(v);
    const InstId id(static_cast<std::uint32_t>(f.insts.size()));
    if (type.valid() && !T(type).isVoid() && op != Op::Store && op != Op::Memcpy && op != Op::Memset && (!isTerminator(op) || op == Op::Invoke)) {
        Value v;
        v.type = type;
        v.def = id;
        i.result = ValueId(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(v);
    }
    f.insts.push_back(i);
    Block &b = f.blockArena[cur_.v];
    if (isTerminator(op)) b.term = id; else b.insts.push_back(id);
    return id;
}

BlockId Lowering::newBlock() {
    Function &f = F();
    const BlockId b(static_cast<std::uint32_t>(f.blockArena.size()));
    f.blockArena.push_back(Block());
    f.blocks.push_back(b);
    return b;
}

void Lowering::place(BlockId b) { cur_ = b; }

bool Lowering::terminated() const { return cur_.valid() && F().block(cur_).term.valid(); }

void Lowering::br(BlockId to, const std::vector<ValueId> &args) {
    if (terminated()) return;
    Function &f = F();
    Succ s;
    s.block = to;
    s.first = static_cast<std::uint32_t>(f.operands.size());
    s.count = static_cast<std::uint32_t>(args.size());
    for (ValueId v : args) f.operands.push_back(v);
    at(add(Op::Br, TypeId(), std::vector<ValueId>())).succ.push_back(s);
}

void Lowering::condbr(ValueId c, BlockId t, BlockId f) {
    if (terminated()) return;
    const InstId id = add(Op::CondBr, TypeId(), std::vector<ValueId>(1, c));
    Succ a, b;
    a.block = t;
    b.block = f;
    at(id).succ.push_back(a);
    at(id).succ.push_back(b);
}

ValueId Lowering::param(BlockId b, TypeId type) {
    Function &f = F();
    Value v;
    v.type = type;
    v.param = b;
    v.index = static_cast<std::uint16_t>(f.block(b).params.size());
    const ValueId id(static_cast<std::uint32_t>(f.values.size()));
    f.values.push_back(v);
    f.blockArena[b.v].params.push_back(id);
    return id;
}

ValueId Lowering::iconst(TypeId type, long long k) {
    const InstId id = add(Op::IConst, type, std::vector<ValueId>());
    at(id).k = normalise(k, T(type), target_);
    return result(id);
}

ValueId Lowering::addrOf(const std::string &sym, TypeId type) {
    const InstId id = add(Op::Addr, type, std::vector<ValueId>());
    at(id).sym = sym;
    return result(id);
}

ValueId Lowering::load(ValueId p, TypeId type) { return result(add(Op::Load, type, std::vector<ValueId>(1, p))); }

// [expr]/6: what is read from an object is a value, and a value of a scalar type carries no cv-qualifier.
Lowering::Val Lowering::readOf(ValueId p, TypeId type) {
    const TypeId u = tid(*T(type).unqualified());
    return Val(load(p, u), u);
}

void Lowering::store(ValueId p, ValueId v) {
    std::vector<ValueId> ops;
    ops.push_back(p);
    ops.push_back(v);
    add(Op::Store, TypeId(), ops);
}

// ----- the conversion rules

TypeId Lowering::promoted(TypeId t) const {
    const Type &ty = T(t);
    if (ty.isScopedEnumeration()) return t;
    const Type &u = ty.isEnumeration() ? *types_.get(ty.kind()) : ty;
    if (u.isInteger() && u.rank() < types_.intType()->rank()) return tid(*types_.intType());
    if (u.kind() == Kind::Char32) return tid(*types_.get(Kind::UInt));
    return tid(u);
}

TypeId Lowering::unsignedVersion(TypeId t) const {
    switch (T(t).kind()) {
    case Kind::Int:      return tid(*types_.get(Kind::UInt));
    case Kind::Long:     return tid(*types_.get(Kind::ULong));
    case Kind::LongLong: return tid(*types_.get(Kind::ULongLong));
    default:             return t;
    }
}

TypeId Lowering::usualArithmetic(TypeId a, TypeId b) const {
    const Type &ta = T(a), &tb = T(b);
    if (ta.isScopedEnumeration() || tb.isScopedEnumeration()) {
        if (ta.unqualified() == tb.unqualified()) return tid(*ta.unqualified());
        outside("a scoped enumeration mixed with another type");
    }
    if (ta.kind() == Kind::LongDouble || tb.kind() == Kind::LongDouble) return tid(*types_.get(Kind::LongDouble));
    if (ta.kind() == Kind::Double || tb.kind() == Kind::Double) return tid(*types_.doubleType());
    if (ta.kind() == Kind::Float || tb.kind() == Kind::Float) return tid(*types_.get(Kind::Float));
    const TypeId pa = promoted(a), pb = promoted(b);
    if (pa == pb) return pa;
    const Type &x = T(pa), &y = T(pb);
    const bool xs = x.isSigned(target_), ys = y.isSigned(target_);
    const TypeId hi = x.rank() >= y.rank() ? pa : pb;
    if (xs == ys) return hi;
    const TypeId uns = xs ? pb : pa, sig = xs ? pa : pb;
    if (T(uns).rank() >= T(sig).rank()) return uns;
    if (T(sig).size(target_) > T(uns).size(target_)) return sig;
    return unsignedVersion(sig);
}

TypeId Lowering::ptrdiffType() const {
    return tid(*types_.get(target_.sizeOf(Kind::Long) == target_.sizeOf(Kind::Pointer) ? Kind::Long : Kind::LongLong));
}

// ----- the function

FuncId Lowering::lower(sema::TypedId fnNode, const FunctionFacts &facts, std::string &why) {
    const std::size_t index = module_.functions.size();
    module_.functions.push_back(Function());
    fnIndex_ = index;
    locals_.clear();
    loops_.clear();
    switches_.clear();
    labels_.clear();
    hasGoto_ = false;
    pads_.clear();
    typeTable_.clear();
    handlerDepth_ = 0;
    try {
        if (facts.variadic) outside("a variadic function");
        if (facts.wrappedBody) outside("a function try block or an exception specification");
        if (facts.landingPads && facts.noexcept_) outside("a noexcept function with landing pads");
        if (facts.landingPads && target_.microsoftNames()) outside("exceptions on the Microsoft ABI");
        Function &f = F();
        f.symbol = facts.symbol;
        f.displayName = facts.displayName;
        f.isStatic = facts.isStatic;
        f.isInline = facts.isInline;
        f.noexcept_ = facts.noexcept_;
        f.pos = facts.pos;
        returns_ = T(facts.returns).isReference() ? facts.returns : tid(*T(facts.returns).unqualified());
        f.returns = returns_;
        const Type &rt = T(returns_);
        if (!(rt.isVoid() || scalar(rt) || plainClass(rt) || (rt.isReference() && referable(*rt.referent()))))
            outside("a return type of '" + rt.describe() + "'");
        if (rt.isX87(target_)) outside("an x87 long double");
        // A constructor and a destructor of a class with a base, a vptr or a member that needs building or
        // destroying are M3b.3's: what they do beyond their syntax comes from the class's layout.
        bool isCtor = false;
        if (facts.hasThis && facts.thisType.valid() && T(facts.thisType).isPointer()) {
            const Type &cls = *T(facts.thisType).pointee()->unqualified();
            isCtor = text(fnNode) == cls.localName();
            const bool isDtor = !text(fnNode).empty() && text(fnNode)[0] == '~';
            if ((isCtor || isDtor) && !simpleLayout(cls)) outside(isCtor ? "a constructor of a class with bases, a vptr or members to build"
                                                                    : "a destructor of a class with bases, a vptr or members to destroy");
        }
        const std::uint32_t n = count(fnNode);
        if (n == 0) outside("a function with no body");
        const sema::TypedId body = child(fnNode, n - 1);
        if (syn(body).kind != syntax::Kind::Block) outside("a body that is not a block");
        // The parameters, then a constructor's mem-initialisers, then the body. A declarator's array bounds and a
        // template-id's arguments are noted among the parameters: constants the parameter types already hold, never run.
        std::vector<sema::TypedId> paramNodes;
        std::uint32_t inits = n - 1;
        for (std::uint32_t i = 0; i + 1 < n; i++) {
            const syntax::Kind k = syn(child(fnNode, i)).kind;
            if (k == syntax::Kind::MemInit) { inits = i; break; }
            if (k == syntax::Kind::Var) paramNodes.push_back(child(fnNode, i));
        }
        if (inits != n - 1 && !isCtor) outside("a mem-initialiser outside a constructor");
        const std::uint32_t params = static_cast<std::uint32_t>(paramNodes.size());
        if (params + (facts.hasThis ? 1 : 0) != facts.paramCount) outside("a parameter list the typed tree and the function disagree about");
        f.entry = newBlock();
        place(f.entry);
        // [class.this]: `this` is the first parameter, a value never stored to - nothing can assign to it.
        this_ = ValueId();
        thisType_ = TypeId();
        f.hasThis = facts.hasThis;
        if (facts.hasThis) {
            if (!facts.thisType.valid() || !T(facts.thisType).isPointer() || !T(facts.thisType).pointee()->isStructOrUnion()) outside("a `this` not recorded");
            thisType_ = facts.thisType;
            this_ = param(f.entry, thisType_);
            f.params.push_back(this_);
        }
        // The parameters: a value each, copied into an alloca so that every local is one (no mem2reg at M3b.1).
        for (std::uint32_t i = 0; i < params; i++) {
            const sema::TypedId p = paramNodes[i];
            if (syn(p).kind != syntax::Kind::Var) outside("a parameter that is not a var");
            if (tree_.node(p).type == nullptr) outside("a parameter not typed");
            const Type &pt = *tree_.node(p).type;
            TypeId slotType;
            if (pt.isReference()) {
                if (!referable(*pt.referent())) outside("a reference parameter to '" + pt.referent()->describe() + "'");
                slotType = tid(*types_.pointerTo(pt.referent()));
            } else if (pt.isArray()) {
                slotType = tid(*types_.pointerTo(pt.pointee()));
            } else if (plainClass(pt)) {
                // A plain class by value is the callee's own copy, in the slot the receive fills on both ABIs: its address.
                const ValueId pv = param(f.entry, tid(*types_.pointerTo(&pt)));
                f.params.push_back(pv);
                locals_[tree_.node(p).syntax.v] = std::make_pair(pv, tid(pt));
                continue;
            } else {
                if (!scalar(pt) || pt.isX87(target_)) outside("a parameter of type '" + pt.describe() + "'");
                slotType = tid(pt);
            }
            const ValueId pv = param(f.entry, slotType);
            f.params.push_back(pv);
            AllocaInfo a;
            a.type = slotType;
            a.align = T(slotType).align(target_);
            a.name = text(p);
            const InstId al = add(Op::Alloca, tid(*types_.pointerTo(&T(slotType))), std::vector<ValueId>());
            at(al).k = static_cast<long long>(f.frame.size());
            at(al).align = a.align;
            f.frame.push_back(a);
            store(result(al), pv);
            locals_[tree_.node(p).syntax.v] = std::make_pair(result(al), pt.isReference() ? tid(pt) : slotType);
        }
        // The return block: one `ret`, reached by a branch carrying the value.
        returnBlock_ = newBlock();
        const BlockId first = newBlock();
        br(first);
        place(first);
        if (isCtor) memberInits(fnNode, inits, n - 1);
        block(body);
        if (!terminated()) {
            if (!rt.isVoid()) outside("a body that falls off its end without a value");
            br(returnBlock_);
        }
        place(returnBlock_);
        if (rt.isVoid()) {
            add(Op::Ret, TypeId(), std::vector<ValueId>());
        } else {
            // A reference and a class are returned as an address: the selector copies a class out by the ABI's rule.
            const TypeId carried = rt.isReference() ? tid(*types_.pointerTo(rt.referent()))
                                 : rt.isStructOrUnion() ? tid(*types_.pointerTo(&rt)) : returns_;
            const ValueId rv = param(returnBlock_, carried);
            add(Op::Ret, TypeId(), std::vector<ValueId>(1, rv));
        }
        return FuncId(static_cast<std::uint32_t>(index));
    } catch (const Outside &o) {
        why = o.why;
        module_.functions.pop_back();
        return FuncId();
    }
}

// ----- statements

void Lowering::block(sema::TypedId id) {
    for (std::uint32_t i = 0; i < count(id); i++) stmt(child(id, i));
}

void Lowering::stmt(sema::TypedId id) {
    const syntax::Node &s = syn(id);
    const std::uint32_t n = count(id);
    switch (s.kind) {
    case syntax::Kind::Block: block(id); return;
    case syntax::Kind::Null: return;
    case syntax::Kind::DeclStmt: declaration(id); return;
    case syntax::Kind::ExprStmt:
        if (n != 1) outside("an expression statement of " + std::to_string(n) + " parts");
        expr(child(id, 0));
        return;
    case syntax::Kind::Return: returnStmt(id); return;
    case syntax::Kind::Try: tryStatement(id); return;
    case syntax::Kind::If: {
        if (n != 2 && n != 3) outside("an if of " + std::to_string(n) + " parts");
        if (syn(child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
        const Val c = truth(expr(child(id, 0)));
        const BlockId then = newBlock(), merge = newBlock();
        const BlockId otherwise = n == 3 ? newBlock() : merge;
        condbr(c.v, then, otherwise);
        place(then);
        stmt(child(id, 1));
        br(merge);
        if (n == 3) {
            place(otherwise);
            stmt(child(id, 2));
            br(merge);
        }
        place(merge);
        return;
    }
    case syntax::Kind::While: {
        if (n != 2) outside("a while of " + std::to_string(n) + " parts");
        if (syn(child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a condition");
        const BlockId cond = newBlock(), body = newBlock(), exit = newBlock();
        br(cond);
        place(cond);
        const Val c = truth(expr(child(id, 0)));
        condbr(c.v, body, exit);
        place(body);
        loops_.push_back(Loop{ exit, cond, handlerDepth_ });
        stmt(child(id, 1));
        loops_.pop_back();
        br(cond);
        place(exit);
        return;
    }
    case syntax::Kind::Do: {
        if (n != 2) outside("a do of " + std::to_string(n) + " parts");
        const BlockId body = newBlock(), cond = newBlock(), exit = newBlock();
        br(body);
        place(body);
        loops_.push_back(Loop{ exit, cond, handlerDepth_ });
        stmt(child(id, 0));
        loops_.pop_back();
        br(cond);
        place(cond);
        const Val c = truth(expr(child(id, 1)));
        condbr(c.v, body, exit);
        place(exit);
        return;
    }
    case syntax::Kind::For: forStmt(id); return;
    case syntax::Kind::Switch: switchStmt(id); return;
    case syntax::Kind::Case: case syntax::Kind::Default: {
        if (switches_.empty() || n == 0) outside("a case label outside a switch");
        SwitchCtx &sw = switches_.back();
        const BlockId here = newBlock();
        br(here);
        place(here);
        if (s.kind == syntax::Kind::Default) {
            if (sw.deflt.valid()) outside("two default labels");
            sw.deflt = here;
        } else {
            if (n != 2) outside("a case of " + std::to_string(n) + " parts");
            long long v = 0;
            if (!foldConstant(child(id, 0), v)) outside("a case label that is not a constant the lowering folds");
            for (long long k : sw.values) if (k == v) outside("two case labels of one value");
            sw.values.push_back(v);
            sw.targets.push_back(here);
        }
        stmt(child(id, n - 1));
        return;
    }
    case syntax::Kind::Break:
        if (loops_.empty()) outside("a break outside a loop or switch");
        endCatches(handlerDepth_ - loops_.back().handlers);
        br(loops_.back().brk);
        return;
    case syntax::Kind::Continue: {
        for (std::size_t i = loops_.size(); i-- > 0; )
            if (loops_[i].cont.valid()) { endCatches(handlerDepth_ - loops_[i].handlers); br(loops_[i].cont); return; }
        outside("a continue outside a loop");
        return;
    }
    case syntax::Kind::Label: {
        if (n != 1) outside("a label of " + std::to_string(n) + " parts");
        const BlockId b = labelBlock(text(id));
        br(b);
        place(b);
        stmt(child(id, 0));
        return;
    }
    case syntax::Kind::Goto:
        if (handlerDepth_ != 0 || !pads_.empty()) outside("a goto inside a try or a handler");
        hasGoto_ = true;
        br(labelBlock(text(id)));
        return;
    default:
        outside(std::string("a '") + syntax::kindName(s.kind) + "' statement");
    }
}

BlockId Lowering::labelBlock(const std::string &name) {
    std::map<std::string, BlockId>::const_iterator l = labels_.find(name);
    if (l != labels_.end()) return l->second;
    const BlockId b = newBlock();
    labels_[name] = b;
    return b;
}

void Lowering::returnStmt(sema::TypedId id) {
    const std::uint32_t n = count(id);
    const Type &rt = T(returns_);
    if (n == 0) {
        if (!rt.isVoid()) outside("a return with no value");
        endCatches(handlerDepth_); br(returnBlock_);
        return;
    }
    if (n == 1 && rt.isVoid()) {
        const Val v = expr(child(id, 0));
        if (v.type.valid() && !T(v.type).isVoid()) outside("a return of a value from a void function");
        endCatches(handlerDepth_); br(returnBlock_);
        return;
    }
    if (n != 1) outside("a return of " + std::to_string(n) + " parts");
    if (rt.isReference()) {
        const Val a = lvalue(child(id, 0));
        if (T(a.type).unqualified() != rt.referent()->unqualified()) outside("a reference returned to another type");
        if (T(a.type).isConst() && !rt.referent()->isConst()) outside("a reference that can write returned from a const");
        const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(rt.referent())), std::vector<ValueId>(1, a.v));
        endCatches(handlerDepth_); br(returnBlock_, std::vector<ValueId>(1, result(c)));
        return;
    }
    if (rt.isStructOrUnion()) {
        if (!isLvalueNode(child(id, 0))) outside("a class returned from what is not an object");
        const Val a = lvalue(child(id, 0));
        if (T(a.type).unqualified() != &rt) outside("a class returned from another type");
        const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(&rt)), std::vector<ValueId>(1, a.v));
        br(returnBlock_, std::vector<ValueId>(1, result(c)));
        return;
    }
    const Val v = exprTo(child(id, 0), returns_);
    endCatches(handlerDepth_); br(returnBlock_, std::vector<ValueId>(1, v.v));
}

// A `for` keeps its parts in order and leaves an empty one out, so only the four-part shape - and `for (;;)` - can be
// read with certainty from the typed tree; which part a three-part `for` lacks is M3b.2's to record on the node.
void Lowering::forStmt(sema::TypedId id) {
    const std::uint32_t n = count(id);
    if (n != 4 && n != 1) outside("a for of " + std::to_string(n) + " parts");
    if (n == 4) {
        const sema::TypedId init = child(id, 0);
        if (syn(init).kind == syntax::Kind::DeclStmt) declaration(init); else expr(init);
    }
    const BlockId cond = newBlock(), body = newBlock(), step = newBlock(), exit = newBlock();
    br(cond);
    place(cond);
    if (n == 4) {
        const Val c = truth(expr(child(id, 1)));
        condbr(c.v, body, exit);
    } else {
        br(body);
    }
    place(body);
    loops_.push_back(Loop{ exit, step, handlerDepth_ });
    stmt(child(id, n - 1));
    loops_.pop_back();
    br(step);
    place(step);
    if (n == 4) expr(child(id, 2));
    br(cond);
    place(exit);
}

void Lowering::switchStmt(sema::TypedId id) {
    if (count(id) != 2) outside("a switch of " + std::to_string(count(id)) + " parts");
    if (syn(child(id, 0)).kind == syntax::Kind::DeclStmt) outside("a declaration in a switch's condition");
    Val c = decay(expr(child(id, 0)));
    if (!T(c.type).isInteger()) outside("a switch on what is not an integer");
    c = convert(c, promoted(c.type));
    const BlockId head = cur_, exit = newBlock();
    const BlockId body = newBlock();
    SwitchCtx ctx;
    ctx.brk = exit;
    switches_.push_back(ctx);
    loops_.push_back(Loop{ exit, BlockId(), handlerDepth_ });
    place(body);
    stmt(child(id, 1));
    br(exit);
    loops_.pop_back();
    const SwitchCtx done = switches_.back();
    switches_.pop_back();
    place(head);
    const InstId sw = add(Op::Switch, TypeId(), std::vector<ValueId>(1, c.v));
    Succ d;
    d.block = done.deflt.valid() ? done.deflt : exit;
    at(sw).succ.push_back(d);
    for (std::size_t i = 0; i < done.values.size(); i++) {
        Succ s;
        s.block = done.targets[i];
        at(sw).succ.push_back(s);
        at(sw).cases.push_back(normalise(done.values[i], T(c.type), target_));
    }
    at(sw).k = static_cast<long long>(done.values.size());
    // The body's first block is reached only through a label; the walker's switch body is the same.
    (void)body;
    place(exit);
}

// A declaration: each `var` an alloca of its declared type, its initialiser stored; a reference's alloca holds the
// address it binds; an array of scalars is filled from its flat list and zeroed past it.
void Lowering::declaration(sema::TypedId id) {
    for (std::uint32_t i = 0; i < count(id); i++) {
        const sema::TypedId var = child(id, i);
        if (syn(var).kind != syntax::Kind::Var) continue;
        const sema::TypedNode &v = tree_.node(var);
        if (v.type == nullptr) outside("a var not typed");
        const sema::Resolution &r = tree_.resolution(var);
        if (r.what == sema::Resolution::Local && !r.symbol.empty()) outside("a static local");
        const Type &t = *v.type;
        TypeId slotType;
        if (t.isReference()) {
            if (!referable(*t.referent())) outside("a reference to '" + t.referent()->describe() + "'");
            slotType = tid(*types_.pointerTo(t.referent()));
        } else if (t.isArray()) {
            if (!t.pointee()->isComplete() || !scalar(*t.pointee()) || t.pointee()->isX87(target_) || t.length() <= 0)
                outside("an array of '" + t.pointee()->describe() + "'");
            slotType = tid(t);
        } else if (plainClass(t)) {
            slotType = tid(*t.unqualified());
        } else {
            if (!scalar(t) || t.isX87(target_)) outside("a local of type '" + t.describe() + "'");
            slotType = tid(t);
        }
        Function &f = F();
        AllocaInfo a;
        a.type = slotType;
        a.align = T(slotType).align(target_);
        const Type &st = T(slotType);
        if ((st.isArray() || st.isStructOrUnion()) && st.size(target_) >= 16 && a.align < 16) a.align = 16;   // as the walker lays it
        a.name = text(var);
        // Allocas go to the entry block, whatever block the declaration stands in.
        const BlockId here = cur_;
        place(f.entry);
        // The entry block ends in its branch already; the alloca is appended in front of it.
        Inst in;
        in.op = Op::Alloca;
        in.type = tid(*types_.pointerTo(&T(slotType)));
        in.first = static_cast<std::uint32_t>(f.operands.size());
        in.count = 0;
        in.k = static_cast<long long>(f.frame.size());
        in.align = a.align;
        const InstId al(static_cast<std::uint32_t>(f.insts.size()));
        Value val;
        val.type = in.type;
        val.def = al;
        in.result = ValueId(static_cast<std::uint32_t>(f.values.size()));
        f.values.push_back(val);
        f.insts.push_back(in);
        f.blockArena[f.entry.v].insts.push_back(al);
        f.frame.push_back(a);
        place(here);
        const ValueId slot = result(al);
        locals_[v.syntax.v] = std::make_pair(slot, tid(t));
        if (t.isReference()) {
            if (v.count != 1) outside("a reference of " + std::to_string(v.count) + " parts");
            const Val b = lvalue(child(var, 0));
            if (T(b.type).unqualified() != t.referent()->unqualified()) outside("a reference bound to another type");
            if (T(b.type).isConst() && !t.referent()->isConst()) outside("a reference that can write bound to a const");
            store(slot, result(add(Op::Bitcast, slotType, std::vector<ValueId>(1, b.v))));
            continue;
        }
        if (t.isArray()) { localArray(var, slot, slotType); continue; }
        if (t.isStructOrUnion()) { classLocal(var, slot, t); continue; }
        if (v.count > 1) outside("a var of " + std::to_string(v.count) + " parts");
        if (v.count == 0) continue;
        store(slot, exprTo(child(var, 0), slotType).v);
    }
}

// An array's `var` holds its written bound first and its values after, and the typed tree does not say where the
// one ends: the bound is the first part when it folds to the array's length and the parts after it fit; a list of
// exactly the length whose first value is the length is refused rather than guessed. The tail past the list is zeroed.
void Lowering::localArray(sema::TypedId var, ValueId slot, TypeId type) {
    const sema::TypedNode &v = tree_.node(var);
    if (v.count == 0) {
        // `T a[n] = {}` is value-initialised: zeroed (the form, not the parts, says so).
        if (v.initForm >= 2) { const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot)); at(z).k = T(type).size(target_); }
        return;
    }
    const Type &t = T(type);
    const long long length = t.length();
    long long bound = 0;
    std::uint32_t first = 0;
    if (foldConstant(child(var, 0), bound) && bound == length) {
        if (static_cast<long long>(v.count) == length && length > 1) outside("an array whose bound cannot be told from its first value");
        first = 1;
    }
    const long long items = static_cast<long long>(v.count - first);
    if (items == 0) {
        if (v.initForm >= 2) { const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot)); at(z).k = t.size(target_); }
        return;
    }
    if (items > length) outside("an array initialised with more values than it holds");
    if (syn(child(var, first)).kind == syntax::Kind::StringLit) outside("an array initialised from a string");
    if (length - items > 64) outside("an array zeroed past 64 elements");
    const TypeId elem = tid(*t.pointee());
    const long long size = t.pointee()->size(target_);
    const ValueId base = result(add(Op::Bitcast, tid(*types_.pointerTo(&T(elem))), std::vector<ValueId>(1, slot)));
    for (long long i = 0; i < length; i++) {
        ValueId value;
        if (i < items) {
            value = exprTo(child(var, static_cast<std::uint32_t>(first + i)), elem).v;
        } else if (T(elem).isFloating()) {
            const InstId z = add(Op::FConst, elem, std::vector<ValueId>());
            at(z).bits = 0;
            value = result(z);
        } else if (T(elem).isPointer()) {
            value = result(add(Op::Null, elem, std::vector<ValueId>()));
        } else {
            value = iconst(elem, 0);
        }
        const InstId g = add(Op::Gep, tid(*types_.pointerTo(&T(elem))), std::vector<ValueId>(1, base));
        long long offset = size;
        offset *= i;
        at(g).k = offset;
        store(result(g), value);
    }
}

// ----- expressions

bool Lowering::isLvalueNode(sema::TypedId id) const {
    const syntax::Node &s = syn(id);
    switch (s.kind) {
    case syntax::Kind::Name: {
        if (tree_.node(id).decl.valid()) return true;
        const sema::Resolution &r = tree_.resolution(id);
        return r.what == sema::Resolution::Global || r.what == sema::Resolution::Member;
    }
    case syntax::Kind::StringLit: case syntax::Kind::Member: case syntax::Kind::Subscript: return true;
    case syntax::Kind::Unary: return text(id) == "*";
    case syntax::Kind::Paren: return count(id) == 1 && isLvalueNode(child(id, 0));
    case syntax::Kind::Cast:
        return count(id) == 1 && tree_.node(id).castType.valid() &&
               (T(tree_.node(id).castType).isReference() || tree_.node(id).castXvalue) && isLvalueNode(child(id, 0));
    case syntax::Kind::Conditional:
        return count(id) == 3 && isLvalueNode(child(id, 1)) && isLvalueNode(child(id, 2)) &&
               tree_.node(child(id, 1)).type != nullptr && tree_.node(child(id, 1)).type == tree_.node(child(id, 2)).type;
    case syntax::Kind::Call: {
        const sema::Resolution &r = tree_.resolution(id);
        return r.what == sema::Resolution::Call && r.type.valid() && T(r.type).isFunction() && T(r.type).returns()->isReference();
    }
    default: return false;
    }
}

// An object's address and its type, const kept on the type.
Lowering::Val Lowering::lvalue(sema::TypedId id) {
    const syntax::Node &s = syn(id);
    switch (s.kind) {
    case syntax::Kind::Name: return nameAddress(id);
    case syntax::Kind::Paren:
        if (count(id) != 1) outside("a parenthesis of " + std::to_string(count(id)) + " parts");
        return lvalue(child(id, 0));
    case syntax::Kind::StringLit: {
        if (tree_.node(id).type == nullptr || !tree_.node(id).literal) outside("a string not typed");
        const std::string &label = tree_.stringLabel(id);
        if (label.empty()) outside("a string whose label was not recorded");
        return Val(addrOf(label, tid(*types_.pointerTo(tree_.node(id).type))), tid(*tree_.node(id).type));
    }
    case syntax::Kind::Member: return memberAddress(id);
    case syntax::Kind::Subscript: {
        if (count(id) != 2) outside("a subscript of " + std::to_string(count(id)) + " parts");
        const Val sum = arithmetic("+", expr(child(id, 0)), expr(child(id, 1)));
        if (!T(sum.type).isPointer()) outside("a subscript of what is not a pointer");
        return Val(sum.v, tid(*T(sum.type).pointee()));
    }
    case syntax::Kind::Unary: {
        if (text(id) != "*" || count(id) != 1) outside("an address of a '" + text(id) + "'");
        const Val p = decay(expr(child(id, 0)));
        const Type &pt = T(p.type);
        if (!pt.isPointer() || pt.pointee()->isVoid() || pt.pointee()->isFunction()) outside("a '*' of what is not an object pointer");
        return Val(p.v, tid(*pt.pointee()));
    }
    case syntax::Kind::Cast: {
        // A cast to a reference names its operand's object: the same type, const or an rvalue's && aside.
        if (!isLvalueNode(id)) outside("a cast to a reference of what is not an object");
        const Type &cast = T(tree_.node(id).castType);
        const Type &to = cast.isReference() ? *cast.referent() : cast;      // static_cast<T &&> is noted as T, an xvalue
        const Val a = lvalue(child(id, 0));
        if (T(a.type).unqualified() != to.unqualified()) outside("a cast to a reference to another type");
        if (T(a.type).isConst() && !to.isConst()) outside("a cast that takes a const away");
        return Val(a.v, tid(to));
    }
    case syntax::Kind::Conditional: {
        if (!isLvalueNode(id)) outside("a conditional that is no lvalue");
        const Val c = truth(expr(child(id, 0)));
        const BlockId a = newBlock(), b = newBlock(), merge = newBlock();
        condbr(c.v, a, b);
        place(a);
        const Val x = lvalue(child(id, 1));
        const TypeId pt = tid(*types_.pointerTo(&T(x.type)));
        br(merge, std::vector<ValueId>(1, x.v));
        place(b);
        const Val y = lvalue(child(id, 2));
        if (y.type != x.type) outside("a conditional of two lvalues apart in type");
        br(merge, std::vector<ValueId>(1, y.v));
        place(merge);
        return Val(param(merge, pt), x.type);
    }
    case syntax::Kind::Call: {
        const Val r = call(id);
        if (!T(r.type).isPointer()) outside("a call that is no lvalue");
        return Val(r.v, tid(*T(r.type).pointee()));
    }
    default:
        outside(std::string("an address of a '") + syntax::kindName(s.kind) + "'");
    }
    return Val();
}

Lowering::Val Lowering::expr(sema::TypedId id) {
    const syntax::Node &s = syn(id);
    const sema::TypedNode &n = tree_.node(id);
    const std::uint32_t k = n.count;
    // What the typer left untyped - a pack's name in a list, a form past its subset - the lowering does not read either.
    if (n.type == nullptr) outside(std::string("a '") + syntax::kindName(s.kind) + "' the typer did not type");
    Val e;
    switch (s.kind) {
    case syntax::Kind::IntLit: case syntax::Kind::CharLit: case syntax::Kind::FloatLit: case syntax::Kind::WordLit:
        e = literal(id);
        break;
    case syntax::Kind::StringLit: case syntax::Kind::Member: case syntax::Kind::Subscript: {
        if (s.kind == syntax::Kind::Member && pseudoDestructor(id)) { e = Val(ValueId(), tid(*types_.get(Kind::Void))); break; }
        const Val a = lvalue(id);
        e = decay(Val(a.v, a.type));
        if (T(e.type).isPointer() && T(a.type).isArray()) break;
        if (!scalar(T(a.type))) outside("a read of an object of type '" + T(a.type).describe() + "'");
        e = readOf(a.v, a.type);
        break;
    }
    case syntax::Kind::Name: e = name(id); break;
    case syntax::Kind::This:
        if (!this_.valid()) outside("`this` where there is none");
        e = Val(this_, thisType_);
        break;
    case syntax::Kind::Paren:
        if (k != 1) outside("a parenthesis of " + std::to_string(k) + " parts");
        e = expr(child(id, 0));
        break;
    case syntax::Kind::Unary: e = unary(id); break;
    case syntax::Kind::Postfix:
        if (k != 1) outside("a postfix of " + std::to_string(k) + " parts");
        if (text(id) != "++" && text(id) != "--") outside("a postfix '" + text(id) + "'");
        e = step(child(id, 0), text(id) == "++", false);
        break;
    case syntax::Kind::Binary:
        if (k != 2) outside("a binary of " + std::to_string(k) + " parts");
        e = binary(text(id), child(id, 0), child(id, 1));
        break;
    case syntax::Kind::Assign: e = assign(id); break;
    case syntax::Kind::Comma:
        if (k != 2) outside("a comma of " + std::to_string(k) + " parts");
        expr(child(id, 0));
        e = expr(child(id, 1));
        break;
    case syntax::Kind::Conditional: e = conditional(id); break;
    case syntax::Kind::Call: {
        e = call(id);
        const sema::Resolution &r = tree_.resolution(id);
        if (r.what == sema::Resolution::Call && T(r.type).returns()->isReference())
            e = readOf(e.v, tid(*T(r.type).returns()->referent()));
        break;
    }
    case syntax::Kind::Cast: {
        if (k != 1 || !n.castType.valid()) outside("a cast not recorded");
        const Type &to = T(n.castType);
        const Type &named = to.isReference() ? *to.referent() : to;
        if ((to.isReference() || n.castXvalue) && scalar(named) && !named.isX87(target_) && isLvalueNode(id)) {
            const Val a = lvalue(id);            // a cast to a reference read as a value: the object it names
            e = readOf(a.v, a.type);
            break;
        }
        if (to.isReference() || n.castXvalue) outside("a cast to a reference");
        const Val v = decay(expr(child(id, 0)));
        if (to.isVoid()) { e = Val(ValueId(), n.castType); break; }
        if (!scalar(to) || to.isX87(target_)) outside("a cast to '" + to.describe() + "'");
        if (!scalar(T(v.type)) && !T(v.type).isNullPtr()) outside("a cast of what is not a scalar");
        e = convert(v, n.castType, true);
        break;
    }
    case syntax::Kind::New: e = newExpression(id); break;
    case syntax::Kind::Delete: e = deleteExpression(id); break;
    case syntax::Kind::Sizeof:
        if (!n.folded.has) outside("a sizeof not folded");
        e = Val(iconst(n.folded.type, n.folded.value), n.folded.type);
        break;
    default:
        outside(std::string("a '") + syntax::kindName(s.kind) + "' expression");
    }
    if (n.type != nullptr && e.type.valid()) {
        const Type &mine = T(e.type);
        const Type &theirs = n.type->isReference() ? *n.type->referent() : *n.type;
        const bool decayed = mine.isPointer() && ((theirs.isArray() && mine.pointee() == theirs.pointee()) ||
                                                  (theirs.isFunction() && mine.pointee() == &theirs));
        if (&mine != &theirs && mine.unqualified() != theirs.unqualified() && !decayed)
            outside(std::string("a '") + syntax::kindName(s.kind) + "' the typer and the lowering type apart: '" +
                    mine.describe() + "' against '" + theirs.describe() + "'");
    }
    return e;
}

Lowering::Val Lowering::literal(sema::TypedId id) {
    const sema::TypedNode &n = tree_.node(id);
    if (n.type == nullptr) outside("a literal not typed");
    const TypeId t = tid(*n.type);
    if (syn(id).kind == syntax::Kind::WordLit) {
        if (text(id) == "nullptr") return Val(result(add(Op::Null, t, std::vector<ValueId>())), t);
        return Val(iconst(t, text(id) == "true" ? 1 : 0), t);
    }
    if (!n.literal) outside("a literal whose token was not recorded");
    const Token &tok = tree_.literal(id);
    if (!tok.udSuffix.empty()) outside("a user-defined literal");
    if (!tok.isFloat) {
        if (!n.folded.has) outside("an integer literal not folded");
        return Val(iconst(t, n.folded.value), t);
    }
    if (n.type->isX87(target_)) outside("an x87 long double");
    const InstId c = add(Op::FConst, t, std::vector<ValueId>());
    if (n.type->kind() == Kind::Float) {
        const float f = static_cast<float>(tok.dvalue);
        unsigned int bits = 0;
        std::memcpy(&bits, &f, 4);
        at(c).bits = bits;
    } else {
        const double d = static_cast<double>(tok.dvalue);
        unsigned long long bits = 0;
        std::memcpy(&bits, &d, 8);
        at(c).bits = bits;
    }
    return Val(result(c), t);
}

// A name's address: a local's alloca, a global's symbol; a reference holds the address it binds.
Lowering::Val Lowering::nameAddress(sema::TypedId id) {
    const sema::TypedNode &n = tree_.node(id);
    if (n.decl.valid()) {
        std::map<std::uint32_t, std::pair<ValueId, TypeId> >::const_iterator l = locals_.find(n.decl.v);
        if (l == locals_.end()) outside("a name whose local the lowering did not declare");
        const sema::Resolution &r = tree_.resolution(id);
        if (r.what == sema::Resolution::Local && !r.symbol.empty()) outside("a static local");
        const Type &t = T(l->second.second);
        if (t.isReference()) {
            const TypeId pt = tid(*types_.pointerTo(t.referent()));
            return Val(load(l->second.first, pt), tid(*t.referent()));
        }
        return Val(l->second.first, l->second.second);
    }
    const sema::Resolution &r = tree_.resolution(id);
    if (r.what == sema::Resolution::Member) return memberOfThis(id);
    if (r.what != sema::Resolution::Global) outside("the name '" + text(id) + "' is no object");
    if (r.isThreadLocal) outside("a thread_local");
    if (!r.type.valid()) outside("a global not typed");
    const Type &t = T(r.type);
    if (t.isReference()) {
        const TypeId pt = tid(*types_.pointerTo(t.referent()));
        if (!referable(*t.referent())) outside("a reference to '" + t.referent()->describe() + "'");
        return Val(load(addrOf(r.symbol, tid(*types_.pointerTo(&T(pt)))), pt), tid(*t.referent()));
    }
    return Val(addrOf(r.symbol, tid(*types_.pointerTo(&t))), r.type);
}

Lowering::Val Lowering::name(sema::TypedId id) {
    const sema::TypedNode &n = tree_.node(id);
    if (!n.decl.valid()) {
        const sema::Resolution &r = tree_.resolution(id);
        if (r.what == sema::Resolution::Enumerator) {
            if (!r.type.valid() || !scalarArithmetic(T(r.type))) outside("an enumerator of no arithmetic type");
            return Val(iconst(r.type, r.value), r.type);
        }
        if (r.what == sema::Resolution::Function) {
            if (r.owner.valid() || !r.type.valid() || !T(r.type).isFunction()) outside("a member named as a value");
            const TypeId pt = tid(*types_.pointerTo(&T(r.type)));
            return Val(addrOf(r.symbol, pt), pt);
        }
        if (r.what != sema::Resolution::Global && r.what != sema::Resolution::Member)
            outside("the name '" + text(id) + "', " + (r.what == sema::Resolution::StaticMember ? "a static member"
                                                        : r.what == sema::Resolution::This ? "this" : "of another kind"));
    }
    const Val a = nameAddress(id);
    const Type &t = T(a.type);
    if (t.isArray()) return decay(Val(a.v, a.type));
    if (!scalar(t) || t.isX87(target_)) outside("a read of an object of type '" + t.describe() + "'");
    return readOf(a.v, a.type);
}

// [expr.ref]: `o.m` and `p->m` of a struct's scalar data member, the object's const carried onto the member.
Lowering::Val Lowering::memberAddress(sema::TypedId id) {
    const std::string &t = text(id);
    if (count(id) != 1) outside("a member of " + std::to_string(count(id)) + " parts");
    const bool arrow = t.compare(0, 2, "->") == 0;
    const std::string mname = t.substr(arrow ? 2 : 1);
    if (mname.empty() || mname.find_first_of(":~<") != std::string::npos || mname.compare(0, 8, "template") == 0) outside("a member named '" + mname + "'");
    Val object;
    if (arrow) {
        const Val p = decay(expr(child(id, 0)));
        if (!T(p.type).isPointer() || !T(p.type).pointee()->isStructOrUnion()) outside("a '->' of what is not a pointer to a struct");
        object = Val(p.v, tid(*T(p.type).pointee()));
    } else {
        object = lvalue(child(id, 0));
    }
    const Type &obj = T(object.type);
    if (!obj.isStructOrUnion()) outside("a member of '" + obj.describe() + "'");
    if (obj.unqualified()->findStaticMember(mname) != nullptr) outside("a static member");
    if (obj.unqualified()->findMember(mname) == nullptr) outside("a member '" + mname + "' that is not a data member");
    const Member &m = *obj.unqualified()->findMember(mname);
    if (m.ambiguous || m.inVirtualBase != nullptr || m.anonUnion != nullptr || m.type->isReference() || m.width != 0)
        outside("a member '" + mname + "' the lowering does not reach");
    const Type &mt = obj.isConst() && !m.isMutable ? *types_.withConst(m.type) : *m.type;
    if (!scalar(mt) && !mt.isArray() && !mt.isStructOrUnion()) outside("a member of type '" + mt.describe() + "'");
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, object.v));
    at(g).k = m.offset;
    return Val(result(g), tid(mt));
}

Lowering::Val Lowering::member(sema::TypedId id) { return memberAddress(id); }

// [class.mfct.non-static]/3: a member's bare name in a member function is `(*this).m` - the record's offset from
// `this`, its type the access's. One reached through another object - a lambda's captured `this` - is refused.
Lowering::Val Lowering::memberOfThis(sema::TypedId id) {
    const sema::Resolution &r = tree_.resolution(id);
    if (!this_.valid()) outside("a member named where there is no `this`");
    const Type &cls = *T(thisType_).pointee()->unqualified();
    const Member *m = cls.findMember(text(id));
    if (m == nullptr || m->offset != r.serial || !r.type.valid()) outside("a member reached through another object's `this`");
    if (m->ambiguous || m->inVirtualBase != nullptr || m->anonUnion != nullptr || m->type->isReference() || m->width != 0)
        outside("a member '" + text(id) + "' the lowering does not reach");
    const Type &mt = T(r.type);
    if (!scalar(mt) && !mt.isArray() && !mt.isStructOrUnion()) outside("a member of type '" + mt.describe() + "'");
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, this_));
    at(g).k = m->offset;
    return Val(result(g), r.type);
}

Lowering::Val Lowering::unary(sema::TypedId id) {
    const std::string &op = text(id);
    if (count(id) != 1) outside("a unary of " + std::to_string(count(id)) + " parts");
    const sema::TypedId o = child(id, 0);
    if (op == "&") {
        // [conv.func]: `&f` and `f` are one address.
        if (syn(o).kind == syntax::Kind::Name && !tree_.node(o).decl.valid() && tree_.resolution(o).what == sema::Resolution::Function)
            return name(o);
        if (!isLvalueNode(o)) outside("an address of what is not an object");
        const Val a = lvalue(o);
        return Val(a.v, tid(*types_.pointerTo(&T(a.type))));
    }
    if (op == "++" || op == "--") return step(o, op == "++", true);
    const Val a = decay(expr(o));
    const Type &t = T(a.type);
    if (op == "*") {
        if (!t.isPointer() || t.pointee()->isVoid() || t.pointee()->isFunction()) outside("a '*' of what is not an object pointer");
        const Type &e = *t.pointee();
        if (!scalar(e) || e.isX87(target_)) outside("a read through a pointer to '" + e.describe() + "'");
        return readOf(a.v, tid(e));
    }
    if (op == "!") {
        if (!scalar(t) && !t.isNullPtr()) outside("a '!' of what is not a scalar");
        const TypeId b = tid(*types_.get(Kind::Bool));
        const Val tr = truth(a);
        std::vector<ValueId> ops;
        ops.push_back(tr.v);
        ops.push_back(iconst(b, 0));
        const InstId c = add(Op::ICmp, b, ops);
        at(c).cc = Cmp::Eq;
        return Val(result(c), b);
    }
    if (!scalarArithmetic(t)) outside("a unary '" + op + "' of what is not a number");
    if (op == "+") return promote(a);
    if (op == "-") {
        const Val p = promote(a);
        return Val(result(add(T(p.type).isFloating() ? Op::FNeg : Op::Neg, p.type, std::vector<ValueId>(1, p.v))), p.type);
    }
    if (op == "~") {
        if (!t.isInteger()) outside("'~' of what is not an integer");
        const Val p = promote(a);
        return Val(result(add(Op::Not, p.type, std::vector<ValueId>(1, p.v))), p.type);
    }
    outside("a unary '" + op + "'");
    return Val();
}

// `++x`, `x++` and their decrements on a scalar object: one load, one add or gep, one store.
Lowering::Val Lowering::step(sema::TypedId target, bool increment, bool prefix) {
    if (!isLvalueNode(target)) outside("a step of what is not an object");
    const Val a = lvalue(target);
    const Type &t = T(a.type);
    if (!scalar(t) || t.isBool() || t.isEnumeration() || t.isConst() || t.isX87(target_)) outside("a step of what is not a scalar object");
    if (t.isPointer() && (t.pointee()->isFunction() || !t.pointee()->isComplete())) outside("a step of a pointer to an incomplete type");
    const ValueId old = load(a.v, a.type);
    ValueId made;
    if (t.isPointer()) {
        const InstId g = add(Op::Gep, a.type, std::vector<ValueId>(1, old));
        at(g).k = (increment ? 1 : -1) * static_cast<long long>(t.pointee()->size(target_));
        made = result(g);
    } else if (t.isFloating()) {
        const InstId one = add(Op::FConst, a.type, std::vector<ValueId>());
        if (t.kind() == Kind::Float) { const float f = 1.0f; unsigned int b = 0; std::memcpy(&b, &f, 4); at(one).bits = b; }
        else { const double d = 1.0; unsigned long long b = 0; std::memcpy(&b, &d, 8); at(one).bits = b; }
        std::vector<ValueId> ops;
        ops.push_back(old);
        ops.push_back(result(one));
        made = result(add(increment ? Op::FAdd : Op::FSub, a.type, ops));
    } else {
        // In the promoted type, as `x + 1` is, then back - which is what the parser writes for a `short`.
        const TypeId pt = promoted(a.type);
        const Val p = convert(Val(old, a.type), pt);
        std::vector<ValueId> ops;
        ops.push_back(p.v);
        ops.push_back(iconst(pt, 1));
        const Val sum(result(add(increment ? Op::Add : Op::Sub, pt, ops)), pt);
        made = convert(sum, a.type).v;
    }
    store(a.v, made);
    return Val(prefix ? made : old, a.type);
}

Lowering::Val Lowering::truth(Val v) {
    const TypeId b = tid(*types_.get(Kind::Bool));
    const Type &t = T(v.type);
    if (t.isBool()) return v;
    if (!scalar(t) && !t.isNullPtr()) outside("a condition that is not a scalar");
    std::vector<ValueId> ops;
    ops.push_back(v.v);
    if (t.isFloating()) {
        const InstId z = add(Op::FConst, v.type, std::vector<ValueId>());
        at(z).bits = 0;
        ops.push_back(result(z));
        const InstId c = add(Op::FCmp, b, ops);
        at(c).cc = Cmp::Une;
        return Val(result(c), b);
    }
    if (t.isNullPtr()) return Val(iconst(b, 0), b);
    ops.push_back(t.isPointer() ? result(add(Op::Null, v.type, std::vector<ValueId>())) : iconst(v.type, 0));
    const InstId c = add(Op::ICmp, b, ops);
    at(c).cc = Cmp::Ne;
    return Val(result(c), b);
}

// [conv]: a scalar to another, as the parser's convertNode has them; to bool a comparison with zero.
Lowering::Val Lowering::convert(Val v, TypeId to, bool cast) {
    if (v.type == to) return v;
    const Type &from = T(v.type), &t = T(to);
    if (t.isX87(target_) || from.isX87(target_)) outside("an x87 long double");
    if (from.unqualified() == t.unqualified()) return Val(result(add(Op::Bitcast, to, std::vector<ValueId>(1, v.v))), to);
    if (t.isBool()) return truth(v);
    if (from.isNullPtr()) {
        if (!t.isPointer()) outside("a nullptr converted to '" + t.describe() + "'");
        return Val(result(add(Op::Null, to, std::vector<ValueId>())), to);
    }
    if (!scalar(from) || !scalar(t)) outside("a conversion of '" + from.describe() + "' to '" + t.describe() + "'");
    if (t.isPointer()) {
        if (from.isPointer()) {
            if (from.pointee()->unqualified()->isStructOrUnion() && t.pointee()->unqualified()->isStructOrUnion() &&
                from.pointee()->unqualified() != t.pointee()->unqualified())
                outside("a conversion of a pointer to a class to one to another class");
            return Val(result(add(Op::Bitcast, to, std::vector<ValueId>(1, v.v))), to);
        }
        if (!cast) outside("a conversion of a number to a pointer");
        return Val(result(add(Op::IntToPtr, to, std::vector<ValueId>(1, v.v))), to);
    }
    if (from.isPointer()) {
        if (!cast || !t.isInteger()) outside("a conversion of a pointer to a number");
        return Val(result(add(Op::PtrToInt, to, std::vector<ValueId>(1, v.v))), to);
    }
    const bool ff = from.isFloating(), tf = t.isFloating();
    Op op = Op::Bitcast;
    if (!ff && !tf) {
        const int a = from.size(target_), b = t.size(target_);
        op = a < b ? (from.isSigned(target_) ? Op::SExt : Op::ZExt) : a > b ? Op::Trunc : Op::Bitcast;
    } else if (!ff) {
        op = from.isSigned(target_) ? Op::SiToFp : Op::UiToFp;
    } else if (!tf) {
        op = t.isSigned(target_) ? Op::FpToSi : Op::FpToUi;
    } else {
        const int a = from.size(target_), b = t.size(target_);
        op = a < b ? Op::FpExt : a > b ? Op::FpTrunc : Op::Bitcast;
    }
    return Val(result(add(op, to, std::vector<ValueId>(1, v.v))), to);
}

// A node's value converted to `to`: the literal 0 and `nullptr` are a null pointer where a pointer is wanted.
Lowering::Val Lowering::exprTo(sema::TypedId id, TypeId to) {
    if (T(to).isPointer() && isNullConstant(id)) return Val(result(add(Op::Null, to, std::vector<ValueId>())), to);
    return convert(decay(expr(id)), to);
}

Lowering::Val Lowering::promote(Val v) {
    const TypeId t = promoted(v.type);
    return t == v.type ? v : convert(v, t);
}

// An array is its first element's address; a function named as a value is already its address.
Lowering::Val Lowering::decay(Val v) {
    if (!v.type.valid()) return v;
    const Type &t = T(v.type);
    if (t.isFunction()) outside("a function named as a value");
    if (!t.isArray()) return v;
    const TypeId to = tid(*types_.pointerTo(t.pointee()));
    return Val(result(add(Op::Bitcast, to, std::vector<ValueId>(1, v.v))), to);
}

Lowering::Val Lowering::pointerAdd(Val p, Val n) {
    const Type &pt = T(p.type);
    if (pt.pointee()->isFunction() || !pt.pointee()->isComplete() || pt.pointee()->isVoid()) outside("arithmetic on a pointer to an incomplete type");
    const TypeId pd = ptrdiffType();
    const Val index = convert(n, pd);
    std::vector<ValueId> ops;
    ops.push_back(index.v);
    ops.push_back(iconst(pd, pt.pointee()->size(target_)));
    const ValueId bytes = result(add(Op::Mul, pd, ops));
    std::vector<ValueId> g;
    g.push_back(p.v);
    g.push_back(bytes);
    return Val(result(add(Op::Gep, p.type, g)), p.type);
}

Lowering::Val Lowering::pointerSub(Val a, Val b) {
    const TypeId pd = ptrdiffType();
    if (T(a.type).pointee()->unqualified() != T(b.type).pointee()->unqualified() || !T(a.type).pointee()->isComplete())
        outside("a difference of two pointers apart in type");
    const ValueId x = result(add(Op::PtrToInt, pd, std::vector<ValueId>(1, a.v)));
    const ValueId y = result(add(Op::PtrToInt, pd, std::vector<ValueId>(1, b.v)));
    std::vector<ValueId> ops;
    ops.push_back(x);
    ops.push_back(y);
    const ValueId diff = result(add(Op::Sub, pd, ops));
    std::vector<ValueId> d;
    d.push_back(diff);
    d.push_back(iconst(pd, T(a.type).pointee()->size(target_)));
    return Val(result(add(Op::SDiv, pd, d)), pd);
}

bool Lowering::isNullConstant(sema::TypedId id) const {
    const syntax::Node &s = syn(id);
    if (s.kind == syntax::Kind::WordLit) return text(id) == "nullptr";
    if (s.kind == syntax::Kind::Paren && count(id) == 1) return isNullConstant(child(id, 0));
    const sema::TypedNode &n = tree_.node(id);
    return (s.kind == syntax::Kind::IntLit || s.kind == syntax::Kind::CharLit) && n.folded.has && n.folded.value == 0;
}

namespace {
struct Opcode { const char text[3]; Op op; bool compare; Cmp s, u, f; };
const Opcode kOps[] = {
    { "+", Op::Add, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq }, { "-", Op::Sub, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq },
    { "*", Op::Mul, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq }, { "/", Op::SDiv, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq },
    { "%", Op::SRem, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq }, { "<<", Op::Shl, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq },
    { ">>", Op::AShr, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq }, { "&", Op::And, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq },
    { "|", Op::Or, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq }, { "^", Op::Xor, false, Cmp::Eq, Cmp::Eq, Cmp::Oeq },
    { "==", Op::ICmp, true, Cmp::Eq, Cmp::Eq, Cmp::Oeq }, { "!=", Op::ICmp, true, Cmp::Ne, Cmp::Ne, Cmp::Une },
    { "<", Op::ICmp, true, Cmp::Slt, Cmp::Ult, Cmp::Olt }, { "<=", Op::ICmp, true, Cmp::Sle, Cmp::Ule, Cmp::Ole },
    { ">", Op::ICmp, true, Cmp::Sgt, Cmp::Ugt, Cmp::Ogt }, { ">=", Op::ICmp, true, Cmp::Sge, Cmp::Uge, Cmp::Oge },
};
}   // namespace

// The parser's arithmetic, comparison and logical operators: [expr.arith.conv] for two numbers, [expr.shift]
// promoting each alone, a pointer scaled, a pointer compared unsigned as it stands.
Lowering::Val Lowering::arithmetic(const std::string &op, Val a, Val b) {
    a = decay(a);
    b = decay(b);
    int at_ = -1;
    for (int i = 0; i < static_cast<int>(sizeof kOps / sizeof kOps[0]); i++) if (op == kOps[i].text) at_ = i;
    if (at_ < 0) outside("the operator '" + op + "'");
    const Opcode &found = kOps[at_];
    const Type &ta = T(a.type), &tb = T(b.type);
    const bool nullA = ta.isNullPtr(), nullB = tb.isNullPtr();
    if ((!scalar(ta) && !nullA) || (!scalar(tb) && !nullB)) outside("'" + op + "' of what is not a scalar");
    const TypeId boolT = tid(*types_.get(Kind::Bool));
    if (found.compare && (ta.isPointer() || tb.isPointer() || nullA || nullB)) {
        if (!(ta.isPointer() || nullA) || !(tb.isPointer() || nullB)) outside("a pointer compared with a number");
        if (nullA && nullB) return Val(iconst(boolT, found.s == Cmp::Eq || found.s == Cmp::Sle || found.s == Cmp::Sge ? 1 : 0), boolT);
        const TypeId pt = ta.isPointer() ? a.type : b.type;
        if (nullA) a = convert(a, pt);
        if (nullB) b = convert(b, pt);
        if (ta.isPointer() && tb.isPointer() && ta.pointee()->unqualified() != tb.pointee()->unqualified() &&
            !ta.pointee()->isVoid() && !tb.pointee()->isVoid())
            outside("two pointers to different types compared");
        if (a.type != b.type) b = Val(result(add(Op::Bitcast, a.type, std::vector<ValueId>(1, b.v))), a.type);
        std::vector<ValueId> ops;
        ops.push_back(a.v);
        ops.push_back(b.v);
        const InstId c = add(Op::ICmp, boolT, ops);
        at(c).cc = found.u;
        return Val(result(c), boolT);
    }
    if (nullA || nullB) outside("arithmetic on a nullptr");
    if (op == "+" && ta.isPointer() && tb.isInteger()) return pointerAdd(a, b);
    if (op == "+" && ta.isInteger() && tb.isPointer()) return pointerAdd(b, a);
    if (op == "-" && ta.isPointer() && tb.isInteger()) {
        const Val p = promote(b);
        return pointerAdd(a, Val(result(add(Op::Neg, p.type, std::vector<ValueId>(1, p.v))), p.type));
    }
    if (op == "-" && ta.isPointer() && tb.isPointer()) return pointerSub(a, b);
    if (!scalarArithmetic(ta) || !scalarArithmetic(tb)) outside("'" + op + "' of a pointer");
    if ((op == "%" || op == "<<" || op == ">>" || op == "&" || op == "|" || op == "^") && (!ta.isInteger() || !tb.isInteger()))
        outside("'" + op + "' of what is not an integer");
    if (op == "<<" || op == ">>") {
        const Val x = promote(a), y = promote(b);
        std::vector<ValueId> ops;
        ops.push_back(x.v);
        ops.push_back(convert(y, x.type).v);
        const Op o = op == "<<" ? Op::Shl : T(x.type).isSigned(target_) ? Op::AShr : Op::LShr;
        return Val(result(add(o, x.type, ops)), x.type);
    }
    const TypeId t = usualArithmetic(a.type, b.type);
    const Type &tt = T(t);
    if (tt.isX87(target_)) outside("an x87 long double");
    const Val x = convert(a, t), y = convert(b, t);
    std::vector<ValueId> ops;
    ops.push_back(x.v);
    ops.push_back(y.v);
    if (found.compare) {
        const InstId c = add(tt.isFloating() ? Op::FCmp : Op::ICmp, boolT, ops);
        at(c).cc = tt.isFloating() ? found.f : tt.isSigned(target_) ? found.s : found.u;
        return Val(result(c), boolT);
    }
    Op o = found.op;
    if (tt.isFloating()) {
        o = o == Op::Add ? Op::FAdd : o == Op::Sub ? Op::FSub : o == Op::Mul ? Op::FMul : Op::FDiv;
    } else if (!tt.isSigned(target_)) {
        if (o == Op::SDiv) o = Op::UDiv;
        if (o == Op::SRem) o = Op::URem;
    }
    return Val(result(add(o, t, ops)), t);
}

Lowering::Val Lowering::binary(const std::string &op, sema::TypedId l, sema::TypedId r) {
    if (op == "&&" || op == "||") return logical(op == "&&", l, r);
    Val a = expr(l);
    Val b = expr(r);
    // A pointer against the literal 0: the null pointer constant takes the pointer's type ([expr.eq]/2).
    if (T(a.type).isPointer() && isNullConstant(r)) b = Val(result(add(Op::Null, a.type, std::vector<ValueId>())), a.type);
    else if (T(b.type).isPointer() && isNullConstant(l)) a = Val(result(add(Op::Null, b.type, std::vector<ValueId>())), b.type);
    return arithmetic(op, a, b);
}

// `a && b` and `a || b`: the second operand in a block of its own, the answer a bool parameter of the merge.
Lowering::Val Lowering::logical(bool isAnd, sema::TypedId l, sema::TypedId r) {
    const TypeId boolT = tid(*types_.get(Kind::Bool));
    const Val a = truth(decay(expr(l)));
    const BlockId rhs = newBlock(), merge = newBlock();
    const ValueId decided = iconst(boolT, isAnd ? 0 : 1);
    if (isAnd) condbr(a.v, rhs, merge); else condbr(a.v, merge, rhs);
    // The merge's parameter: the short-circuit value from here, the second operand's truth from the other block.
    const BlockId from = cur_;
    place(rhs);
    const Val b = truth(decay(expr(r)));
    br(merge, std::vector<ValueId>(1, b.v));
    // The first branch's argument, written now that the merge's parameter exists.
    Function &f = F();
    Inst &cb = f.insts[f.block(from).term.v];
    const std::size_t which = isAnd ? 1 : 0;
    cb.succ[which].first = static_cast<std::uint32_t>(f.operands.size());
    cb.succ[which].count = 1;
    f.operands.push_back(decided);
    place(merge);
    return Val(param(merge, boolT), boolT);
}

// [expr.cond]/6 for scalars: both arms converted to one type, the answer the merge's parameter.
Lowering::Val Lowering::conditional(sema::TypedId id) {
    if (count(id) != 3) outside("a conditional of " + std::to_string(count(id)) + " parts");
    if (isLvalueNode(id)) {
        const Val a = lvalue(id);
        const Type &t = T(a.type);
        if (!scalar(t)) outside("a conditional of lvalues of type '" + t.describe() + "'");
        return readOf(a.v, a.type);
    }
    const Val c = truth(expr(child(id, 0)));
    const BlockId a = newBlock(), b = newBlock(), merge = newBlock();
    condbr(c.v, a, b);
    // The arms are typed first, which the parser does too: the result type decides what each arm converts to.
    const sema::TypedNode &xn = tree_.node(child(id, 1)), &yn = tree_.node(child(id, 2));
    if (xn.type == nullptr || yn.type == nullptr) outside("a conditional's arm not typed");
    const TypeId txid = xn.type->isArray() ? tid(*types_.pointerTo(xn.type->pointee())) : tid(*xn.type);
    const TypeId tyid = yn.type->isArray() ? tid(*types_.pointerTo(yn.type->pointee())) : tid(*yn.type);
    const Type &tx = T(txid), &ty = T(tyid);
    if (!(scalar(tx) || tx.isNullPtr()) || !(scalar(ty) || ty.isNullPtr())) outside("a conditional of what is not a scalar");
    TypeId t;
    if (scalarArithmetic(tx) && scalarArithmetic(ty)) {
        const bool oneEnum = tx.unqualified() == ty.unqualified() && tx.unqualified()->isEnumeration();
        t = oneEnum ? tid(*tx.unqualified()) : usualArithmetic(tid(tx), tid(ty));
    } else if (&tx == &ty) {
        t = tid(tx);
    } else if (tx.unqualified() == ty.unqualified()) {
        t = tid(*tx.unqualified());
    } else if (tx.isPointer() && (ty.isNullPtr() || isNullConstant(child(id, 2)))) {
        t = tid(tx);
    } else if (ty.isPointer() && (tx.isNullPtr() || isNullConstant(child(id, 1)))) {
        t = tid(ty);
    } else {
        outside("a conditional of '" + tx.describe() + "' and '" + ty.describe() + "'");
    }
    place(a);
    const Val x = convert(decay(expr(child(id, 1))), t);
    br(merge, std::vector<ValueId>(1, x.v));
    place(b);
    const Val y = convert(decay(expr(child(id, 2))), t);
    br(merge, std::vector<ValueId>(1, y.v));
    place(merge);
    return Val(param(merge, t), t);
}

// `a = b` converts b to a's type; `a op= b` is `a = a op b` with a read once.
Lowering::Val Lowering::assign(sema::TypedId id) {
    const std::string &op = text(id);
    if (count(id) != 2) outside("an assignment of " + std::to_string(count(id)) + " parts");
    const sema::TypedId tn = child(id, 0);
    if (!isLvalueNode(tn)) outside("an assignment to what is not an object");
    const Val target = lvalue(tn);
    const Type &t = T(target.type);
    // [class.copy]/18: a plain class's implicit copy assignment copies its bytes; a written operator= is a call.
    if (op == "=" && plainClass(t) && !t.isConst() && !(assigned_ && assigned_(&t)) && isLvalueNode(child(id, 1))) {
        const Val from = lvalue(child(id, 1));
        if (T(from.type).unqualified() != t.unqualified()) outside("a class assigned from another type");
        copyObject(target.v, from.v, t);
        return target;
    }
    if (!scalar(t) || t.isConst() || t.isX87(target_)) outside("an assignment to what is not a scalar object");
    Val value;
    if (op == "=") {
        value = exprTo(child(id, 1), target.type);
    } else {
        if (t.isBool() || t.isEnumeration()) outside("a compound assignment to a bool or an enumeration");
        const Val old(load(target.v, target.type), target.type);
        value = arithmetic(op.substr(0, op.size() - 1), old, expr(child(id, 1)));
    }
    value = convert(value, target.type);
    store(target.v, value.v);
    return value;
}

// A call of the function the parser recorded, by its symbol, or through a pointer to a function.
// [expr.pseudo]: `p->~T()` of a scalar or a class with nothing to destroy ends no one's life but by name - the
// object is evaluated and nothing more is done. The syntax notes the call as the member alone, or inside a call.
bool Lowering::pseudoDestructor(sema::TypedId member) {
    const std::string &t = text(member);
    const bool arrow = t.compare(0, 2, "->") == 0;
    if (count(member) != 1 || t.size() <= (arrow ? 2u : 1u) || t[arrow ? 2 : 1] != '~') return false;
    const Val o = arrow ? decay(expr(child(member, 0))) : lvalue(child(member, 0));
    if (arrow && !T(o.type).isPointer()) outside("a destructor called through what is not a pointer");
    const Type &ot = arrow ? *T(o.type).pointee() : T(o.type);
    if (!(scalar(ot) || plainClass(*ot.unqualified()))) outside("a destructor called by name");
    return true;
}

Lowering::Val Lowering::call(sema::TypedId id) {
    const std::uint32_t n = count(id);
    if (n == 0) outside("a call of nothing");
    const sema::TypedId head = child(id, 0);
    const sema::Resolution &r = tree_.resolution(id);
    if (r.what == sema::Resolution::Temporary || r.what == sema::Resolution::TypeName) outside("a class temporary");
    if (n == 1 && syn(head).kind == syntax::Kind::Member && pseudoDestructor(head)) return Val(ValueId(), tid(*types_.get(Kind::Void)));
    CallInfo info;
    std::vector<ValueId> ops;
    TypeId sig;
    // A call by name reaches the recorded function; one through a value - a local, a call's result - is read as such.
    const bool direct = r.what == sema::Resolution::Call && !r.symbol.empty() && !r.hasThis && !r.indirect && syn(head).kind == syntax::Kind::Name &&
                        !tree_.node(head).decl.valid() && r.symbol.find_first_of(" ()*&[") == std::string::npos;
    // A member function called by name, its object's address the first argument: `o.f()`, `p->f()`, `f()` for `this->f()`.
    if (r.what == sema::Resolution::Call && r.hasThis && r.indirect) outside("a virtual call or a call through a member pointer");
    const bool member = r.what == sema::Resolution::Call && !r.symbol.empty() && r.hasThis &&
                        r.symbol.find_first_of(" ()*&[") == std::string::npos;
    if (member) {
        if (!r.type.valid() || !T(r.type).isFunction() || T(r.type).params().empty()) outside("a member call whose function type was not recorded");
        const Type &self = *T(r.type).params()[0];
        if (!self.isPointer() || !self.pointee()->isStructOrUnion()) outside("a member call whose object is not a class");
        Val object;
        if (syn(head).kind == syntax::Kind::Name) {
            if (!this_.valid()) outside("a member call where there is no `this`");
            object = Val(this_, thisType_);
        } else if (syn(head).kind == syntax::Kind::Member && count(head) == 1) {
            const bool arrow = text(head).compare(0, 2, "->") == 0;
            if (arrow) object = decay(expr(child(head, 0)));
            else { const Val o = lvalue(child(head, 0)); object = Val(o.v, tid(*types_.pointerTo(&T(o.type)))); }
        } else {
            outside("a member call through '" + text(head) + "'");
        }
        if (!T(object.type).isPointer() || T(object.type).pointee()->unqualified() != self.pointee()->unqualified())
            outside("a member call on a base's function");
        if (T(object.type).pointee()->isConst() && !self.pointee()->isConst()) outside("a member call that drops const");
        info.symbol = r.symbol;
        info.hasThis = true;
        sig = r.type;
        ops.push_back(result(add(Op::Bitcast, tid(self), std::vector<ValueId>(1, object.v))));
    } else if (direct) {
        if (!r.type.valid() || !T(r.type).isFunction()) outside("a call whose function type was not recorded");
        info.symbol = r.symbol;
        sig = r.type;
    } else {
        if (syn(head).kind == syntax::Kind::Member) outside("a member call");
        const Val callee = decay(expr(head));
        const Type &ct = T(callee.type);
        if (!ct.isPointer() || !ct.pointee()->isFunction()) outside("a call of what is not a function pointer");
        sig = tid(*ct.pointee());
        ops.push_back(callee.v);
    }
    const Type &ft = T(sig);
    info.signature = sig;
    info.variadic = ft.isVariadicFn();
    info.namedArgs = static_cast<int>(ft.params().size());
    const Type &gives = *ft.returns();
    if (!(gives.isVoid() || scalar(gives) || (gives.isReference() && referable(*gives.referent())))) outside("a call returning '" + gives.describe() + "'");
    if (gives.isX87(target_)) outside("an x87 long double");
    const std::size_t argc = n - 1, skip = member ? 1 : 0;
    const std::size_t paramCount = ft.params().size() - skip;
    if (argc < paramCount || (argc > paramCount && !info.variadic)) outside("a call with defaults or too many arguments");
    for (std::size_t i = 0; i < argc; i++) {
        const sema::TypedId an = child(id, static_cast<std::uint32_t>(i + 1));
        if (i >= paramCount) {
            Val a = decay(expr(an));
            const Type &t = T(a.type);
            if (t.isPointer()) { ops.push_back(a.v); continue; }
            if (!scalarArithmetic(t)) outside("a variadic argument of type '" + t.describe() + "'");
            a = t.kind() == Kind::Float ? convert(a, tid(*types_.doubleType())) : promote(a);
            ops.push_back(a.v);
            continue;
        }
        const Type &p = *ft.params()[i + skip];
        if (plainClass(p)) {
            // A plain class by value: its address, which the selector hands the walker as the object to copy in.
            if (!isLvalueNode(an)) outside("a class argument that is not an object");
            const Val a = lvalue(an);
            if (T(a.type).unqualified() != p.unqualified()) outside("a class argument of another type");
            ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(p.unqualified())), std::vector<ValueId>(1, a.v))));
            continue;
        }
        if (p.isReference()) {
            // [dcl.init.ref]/5: an object of the referent's type, its address passed; a temporary is M3b.2's.
            if (!isLvalueNode(an)) outside("a reference parameter bound to what is not an object");
            const Val a = lvalue(an);
            if (T(a.type).unqualified() != p.referent()->unqualified()) outside("a reference parameter bound to another type");
            if (T(a.type).isConst() && !p.referent()->isConst()) outside("a reference that can write bound to a const");
            if (!referable(T(a.type))) outside("a reference parameter to '" + p.referent()->describe() + "'");
            ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(p.referent())), std::vector<ValueId>(1, a.v))));
            continue;
        }
        if (!scalar(p) || p.isX87(target_)) outside("a parameter of type '" + p.describe() + "'");
        ops.push_back(exprTo(an, tid(p)).v);
    }
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    const TypeId carries = gives.isVoid() ? TypeId() : gives.isReference() ? tid(*types_.pointerTo(gives.referent())) : tid(gives);
    const InstId c = callOrInvoke(carries, ops);
    at(c).call = callIndex;
    return Val(result(c), carries.valid() ? carries : tid(gives));
}

// The parser's facts for a `new` or `delete` node, which the lowering takes only for a scalar or a plain class.
sema::NewFacts Lowering::newFacts(sema::TypedId id, const char *what) {
    sema::NewFacts f;
    if (!newFacts_ || !newFacts_(tree_.node(id).syntax, f) || (f.why.empty() && f.symbol.empty()))
        outside(std::string("a '") + what + "' not recorded");
    if (!f.why.empty()) outside(f.why);
    const Type &t = T(f.made);
    if (t.isX87(target_) || !(scalar(t) || plainClass(t))) outside(std::string("a '") + what + "' of '" + t.describe() + "'");
    return f;
}

// [expr.new] for a scalar or a plain class: the global allocation function the parser called, given the bytes - n of
// them times the size for an array, counted first - then the initialiser's stores: none, zeroes, or the one value.
Lowering::Val Lowering::newExpression(sema::TypedId id) {
    const sema::NewFacts f = newFacts(id, "new");
    const Type &made = T(f.made);
    const std::uint32_t parts = (f.array ? 1u : 0u) + (f.init == 2 ? 1u : 0u);
    if (count(id) != parts) outside("a 'new' of " + std::to_string(count(id)) + " parts");
    const TypeId sizeT = tid(*types_.get(target_.sizeType()));
    ValueId bytes = iconst(sizeT, made.size(target_));
    if (f.array) {
        std::vector<ValueId> ops;
        ops.push_back(exprTo(child(id, 0), sizeT).v);
        ops.push_back(bytes);
        bytes = result(add(Op::Mul, sizeT, ops));
    }
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    CallInfo info;
    info.symbol = f.symbol;
    info.signature = tid(*types_.functionType(&T(voidPtr), std::vector<const Type *>(1, &T(sizeT)), false));
    info.namedArgs = 1;
    const std::uint32_t callIndex = static_cast<std::uint32_t>(F().calls.size());
    F().calls.push_back(info);
    const InstId c = callOrInvoke(voidPtr, std::vector<ValueId>(1, bytes));      // bad_alloc may unwind
    at(c).call = callIndex;
    const TypeId pt = tid(*types_.pointerTo(&made));
    const ValueId p = result(add(Op::Bitcast, pt, std::vector<ValueId>(1, result(c))));
    if (f.init == 1 && f.array) {
        // `new T[n]()` zeroes n elements with the platform's memset, as the parser and clang do.
        std::vector<ValueId> args;
        args.push_back(result(c));
        args.push_back(iconst(tid(*types_.get(Kind::Int)), 0));
        args.push_back(bytes);
        runtimeCall("memset", voidPtr, args);
    } else if (f.init == 1) {
        const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, p));      // [dcl.init]/8: zero
        at(z).k = made.size(target_);
    } else if (f.init == 2 && made.isStructOrUnion()) {
        const sema::TypedId v = child(id, parts - 1);
        if (!isLvalueNode(v)) outside("a 'new' of a class copied from what is not an object");
        const Val o = lvalue(v);
        if (T(o.type).unqualified() != made.unqualified()) outside("a 'new' of a class copied from another type");
        copyObject(p, o.v, made);
    } else if (f.init == 2) {
        store(p, exprTo(child(id, parts - 1), tid(*made.unqualified())).v);
    }
    return Val(p, pt);
}

// [expr.delete] for a scalar or a plain class: nothing to destroy, the pointer handed to the global deallocation
// function the parser called - which takes a null and does nothing, [expr.delete]/2.
Lowering::Val Lowering::deleteExpression(sema::TypedId id) {
    const sema::NewFacts f = newFacts(id, "delete");
    if (count(id) != 1) outside("a 'delete' of " + std::to_string(count(id)) + " parts");
    const Val p = decay(expr(child(id, 0)));
    if (!T(p.type).isPointer() || T(p.type).pointee()->unqualified() != T(f.made).unqualified()) outside("a 'delete' of another type");
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    CallInfo info;
    info.symbol = f.symbol;
    info.signature = tid(*types_.functionType(types_.voidType(), std::vector<const Type *>(1, &T(voidPtr)), false));
    info.namedArgs = 1;
    const std::uint32_t callIndex = static_cast<std::uint32_t>(F().calls.size());
    F().calls.push_back(info);
    const InstId c = add(Op::Call, TypeId(), std::vector<ValueId>(1, result(add(Op::Bitcast, voidPtr, std::vector<ValueId>(1, p.v)))));
    at(c).call = callIndex;
    return Val(ValueId(), tid(*types_.voidType()));
}

// ----- exceptions (M3-IR 3.6, the Itanium half)

// A call inside a try's body or a handler is an invoke: it continues in a fresh block and unwinds to the innermost pad.
InstId Lowering::callOrInvoke(TypeId carries, const std::vector<ValueId> &ops) {
    if (pads_.empty()) return add(Op::Call, carries, ops);
    const BlockId pad = padBlock(pads_.back());
    const BlockId normal = newBlock();
    const InstId c = add(Op::Invoke, carries, ops);
    Succ n, u;
    n.block = normal;
    u.block = pad;
    at(c).succ.push_back(n);
    at(c).succ.push_back(u);
    place(normal);
    return c;
}

// A call of the runtime's own: never an invoke, the way the parser's runtimeCall is written.
Lowering::Val Lowering::runtimeCall(const char *symbol, TypeId returns, const std::vector<ValueId> &args) {
    std::vector<const Type *> params;
    for (std::size_t i = 0; i < args.size(); i++) params.push_back(&T(F().value(args[i]).type));
    CallInfo info;
    info.symbol = symbol;
    info.signature = tid(*types_.functionType(&T(returns), params, false));
    info.namedArgs = static_cast<int>(params.size());
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    const TypeId carries = T(returns).isVoid() ? TypeId() : returns;
    const InstId c = add(Op::Call, carries, args);
    at(c).call = callIndex;
    return Val(result(c), returns);
}

// One selector index per type_info, however many rows name it - the parser's typeIndexFor, per function.
int Lowering::typeIndexFor(const std::string &sym) {
    for (std::size_t i = 0; i < typeTable_.size(); i++) if (typeTable_[i] == sym) return static_cast<int>(i) + 1;
    typeTable_.push_back(sym);
    return static_cast<int>(typeTable_.size());
}

// A pad's block, made on first use: the runtime arrives with the exception pointer and the selector as its parameters.
BlockId Lowering::padBlock(Pad &p) {
    if (p.block.valid()) return p.block;
    p.block = newBlock();
    F().blockArena[p.block.v].pad = true;
    p.ptr = param(p.block, tid(*types_.pointerTo(types_.voidType())));
    p.sel = param(p.block, tid(*types_.get(Kind::Int)));
    return p.block;
}

// An alloca in the entry block, whatever block the statement stands in; its value is the object's address.
ValueId Lowering::entryAlloca(TypeId slotType, const std::string &name) {
    Function &f = F();
    AllocaInfo a;
    a.type = slotType;
    a.align = T(slotType).align(target_);
    a.name = name;
    Inst in;
    in.op = Op::Alloca;
    in.type = tid(*types_.pointerTo(&T(slotType)));
    in.first = static_cast<std::uint32_t>(f.operands.size());
    in.k = static_cast<long long>(f.frame.size());
    in.align = a.align;
    const InstId al(static_cast<std::uint32_t>(f.insts.size()));
    Value val;
    val.type = in.type;
    val.def = al;
    in.result = ValueId(static_cast<std::uint32_t>(f.values.size()));
    f.values.push_back(val);
    f.insts.push_back(in);
    f.blockArena[f.entry.v].insts.push_back(al);
    f.frame.push_back(a);
    return result(al);
}

// Where a pad goes when it is done: the enclosing pad with the same pointer and selector, or out of the frame.
void Lowering::resumeFrom(const Pad &p, const Pad *enclosing) {
    if (enclosing != nullptr) {
        std::vector<ValueId> args;
        args.push_back(p.ptr);
        args.push_back(p.sel);
        br(padBlock(*const_cast<Pad *>(enclosing)), args);
        return;
    }
    runtimeCall("_Unwind_Resume", tid(*types_.voidType()), std::vector<ValueId>(1, p.ptr));
    add(Op::Unreachable, TypeId(), std::vector<ValueId>());
}

// [except.handle]/16: a jump out of n handlers ends each catch, innermost first.
void Lowering::endCatches(std::size_t n) {
    for (std::size_t i = 0; i < n; i++) runtimeCall("__cxa_end_catch", tid(*types_.voidType()), std::vector<ValueId>());
}

// **A `try` and its handlers, as the parser writes them for the Itanium ABI**: the body's calls unwind to one pad, whose
// chain tests the selector against each handler's index and hands what nothing matched to the enclosing pad or to
// _Unwind_Resume; a handler begins its catch, binds its object, and ends it on every way out (a jump: endCatches; a throw: its own cleanup pad).
void Lowering::tryStatement(sema::TypedId id) {
    const std::uint32_t n = count(id);
    if (n < 2) outside("a try with no handler");
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    struct Handler { sema::TypedId node, var, body; const Type *caught; std::string sym; int index; };
    std::vector<Handler> hs;
    Pad p;
    p.cleanup = false;
    p.own = n - 1;
    for (std::uint32_t i = 1; i < n; i++) {
        Handler h;
        h.node = child(id, i);
        if (syn(h.node).kind != syntax::Kind::Catch) outside("a try whose part is not a handler");
        const std::uint32_t parts = count(h.node);
        h.caught = nullptr;
        h.index = 0;
        if (parts == 1 && text(h.node) == "...") {
            h.body = child(h.node, 0);
        } else if (parts == 2 && syn(child(h.node, 0)).kind == syntax::Kind::Var) {
            h.var = child(h.node, 0);
            h.body = child(h.node, 1);
            const Type *t = tree_.node(h.var).type;
            if (t == nullptr) outside("a caught object not typed");
            h.caught = t->isReference() ? t->referent()->unqualified() : t->unqualified();
            if (t->isReference() ? !referable(*h.caught) : (!scalar(*h.caught) || h.caught->isPointer() || h.caught->isX87(target_)))
                outside("a catch of '" + t->describe() + "'");
            if (!typeInfo_) outside("a catch with no type_info to name");
            h.sym = typeInfo_(h.caught);
            if (h.sym.empty()) outside("a catch of a type with no type_info");
        } else {
            // `catch (T)` with no name has no var node; so, in one shape of the suite, has a named one the tree lost (M3-PLAN).
            outside(parts == 1 ? "a handler whose caught object the syntax tree does not hold" : "a handler of " + std::to_string(parts) + " parts");
        }
        h.index = typeIndexFor(h.sym);
        hs.push_back(h);
        p.types.push_back(h.sym);
        p.indices.push_back(h.index);
    }
    // The chain goes on with the enclosing pads' types, each once; a cleanup anywhere outside asks for the filter-0 row.
    for (std::size_t k = pads_.size(); k-- > 0; ) {
        const Pad &outer = pads_[k];
        if (outer.cleanup) p.cleanup = true;
        for (std::size_t t = 0; t < outer.types.size(); t++) {
            bool have = false;
            for (std::size_t c = 0; c < p.types.size(); c++) if (p.types[c] == outer.types[t] && p.indices[c] == outer.indices[t]) have = true;
            if (!have) { p.types.push_back(outer.types[t]); p.indices.push_back(outer.indices[t]); }
        }
    }
    const BlockId done = newBlock();
    pads_.push_back(p);
    block(child(id, 0));
    p = pads_.back();
    pads_.pop_back();
    br(done);
    if (!p.block.valid()) { place(done); return; }       // nothing in the body could throw: the handlers are dead code
    // The enclosing pad by index: pads_ grows under a handler and a pointer into it would not survive.
    const std::size_t encAt = pads_.size();
    const Pad *enclosing = encAt != 0 ? &pads_[encAt - 1] : nullptr;

    place(p.block);
    const InstId cp = add(Op::CatchPad, TypeId(), std::vector<ValueId>());
    at(cp).syms = p.types;
    for (std::size_t i = 0; i < p.indices.size(); i++) at(cp).cases.push_back(p.indices[i]);
    at(cp).k = static_cast<long long>(p.own);
    at(cp).bits = (p.cleanup ? 1u : 0u) | (enclosing != nullptr ? 2u : 0u);
    std::vector<BlockId> handlerBlocks;
    for (std::size_t i = 0; i < hs.size(); i++) {
        const BlockId hb = newBlock(), next = newBlock();
        handlerBlocks.push_back(hb);
        std::vector<ValueId> cmp;
        cmp.push_back(p.sel);
        cmp.push_back(iconst(tid(*types_.get(Kind::Int)), hs[i].index));
        const InstId c = add(Op::ICmp, tid(*types_.get(Kind::Bool)), cmp);
        at(c).cc = Cmp::Eq;
        condbr(result(c), hb, next);
        place(next);
    }
    resumeFrom(p, enclosing);

    for (std::size_t i = 0; i < hs.size(); i++) {
        place(handlerBlocks[i]);
        const Val obj = runtimeCall("__cxa_begin_catch", voidPtr, std::vector<ValueId>(1, p.ptr));
        if (hs[i].var.valid()) {
            const Type &t = *tree_.node(hs[i].var).type;
            // By value, the scalar is copied out of the exception object; a reference holds what __cxa_begin_catch handed back.
            const TypeId objPtr = tid(*types_.pointerTo(hs[i].caught));
            const ValueId asT = result(add(Op::Bitcast, objPtr, std::vector<ValueId>(1, obj.v)));
            if (t.isReference()) {
                const TypeId slotType = tid(*types_.pointerTo(t.referent()));
                const ValueId slot = entryAlloca(slotType, text(hs[i].var));
                store(slot, result(add(Op::Bitcast, slotType, std::vector<ValueId>(1, asT))));
                locals_[tree_.node(hs[i].var).syntax.v] = std::make_pair(slot, tid(t));
            } else {
                const ValueId slot = entryAlloca(tid(t), text(hs[i].var));
                store(slot, load(asT, tid(*hs[i].caught)));
                locals_[tree_.node(hs[i].var).syntax.v] = std::make_pair(slot, tid(t));
            }
        }
        // The handler's own cleanup: an exception out of it ends this catch and goes on to what encloses the try.
        Pad cleanup;
        cleanup.cleanup = true;
        cleanup.own = 0;
        if (encAt != 0) { cleanup.types = pads_[encAt - 1].types; cleanup.indices = pads_[encAt - 1].indices; }
        pads_.push_back(cleanup);
        handlerDepth_++;
        block(hs[i].body);
        handlerDepth_--;
        cleanup = pads_.back();
        pads_.pop_back();
        if (!terminated()) {
            endCatches(1);
            br(done);
        }
        if (cleanup.block.valid()) {
            place(cleanup.block);
            const InstId cl = add(Op::CleanupPad, TypeId(), std::vector<ValueId>());
            at(cl).syms = cleanup.types;
            for (std::size_t k = 0; k < cleanup.indices.size(); k++) at(cl).cases.push_back(cleanup.indices[k]);
            // A row of no types is a cleanup by its action 0; the filter-0 record is asked only where the chain has types.
            at(cl).bits = (cleanup.types.empty() ? 0u : 1u) | (encAt != 0 ? 2u : 0u);
            endCatches(1);
            resumeFrom(cleanup, encAt != 0 ? &pads_[encAt - 1] : nullptr);
        }
    }
    place(done);
}

// A case label's constant: a literal, an enumerator, a sizeof, a sign or a parenthesis over one.
bool Lowering::foldConstant(sema::TypedId id, long long &out) {
    const syntax::Node &s = syn(id);
    const sema::TypedNode &n = tree_.node(id);
    switch (s.kind) {
    case syntax::Kind::IntLit: case syntax::Kind::CharLit: case syntax::Kind::WordLit: case syntax::Kind::Sizeof:
        if (!n.folded.has) return false;
        out = n.folded.value;
        return true;
    case syntax::Kind::Name: {
        const sema::Resolution &r = tree_.resolution(id);
        if (n.decl.valid() || r.what != sema::Resolution::Enumerator) return false;
        out = r.value;
        return true;
    }
    case syntax::Kind::Paren:
        return count(id) == 1 && foldConstant(child(id, 0), out);
    case syntax::Kind::Unary: {
        if (count(id) != 1 || !foldConstant(child(id, 0), out)) return false;
        const std::string &op = text(id);
        if (op == "-") out = -out;
        else if (op == "~") out = ~out;
        else if (op == "!") out = out == 0 ? 1 : 0;
        else if (op != "+") return false;
        return true;
    }
    case syntax::Kind::Cast:
        return count(id) == 1 && foldConstant(child(id, 0), out);
    default:
        return false;
    }
}

}   // namespace ir
