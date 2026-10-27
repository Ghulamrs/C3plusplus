// ir::Lowering - see Lowering.h. The subset it reaches at M3b.1 is the scalar core of the adapter's: free functions
// over scalars, pointers, 1-D arrays of scalars and plain structs' scalar members, with every statement kind but
// `try`; a class object, a temporary, a member call, `this`, a static local or a throw leaves the function outside.

#include "Lowering.h"
#include "../Ast.h"

#include <cstring>

namespace ir {

Lowering::Lowering(const sema::TypedTree &tree, const syntax::Tree &syntax, TypeTable &types, const Target &target,
                   Program &program, Module &into)
    : tree_(tree), syntax_(syntax), types_(types), target_(target), program_(program), module_(into), fnIndex_(0), landingPads_(false), handlerDepth_(0), hasGoto_(false) {}

void Lowering::outside(const std::string &why) { throw Outside(why); }

bool Lowering::scalarArithmetic(const Type &t) { return t.isArithmetic() && !t.isScopedEnumeration(); }
bool Lowering::scalar(const Type &t) { return scalarArithmetic(t) || t.isPointer(); }
// [class]/6 and [basic.types]/9 as far as the lowering needs them: a complete class with no vptr, no virtual base, no
// destructor, no copy of its own and no constructor - an object of it is its bytes, made and copied as such.
bool Lowering::plainClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete()) return false;
    const Type &c = *t.unqualified();
    return !c.polymorphic() && !c.hasVirtualBase() && !c.hasDestructor() && !c.nonTrivialCopy() &&
           !(constructed_ && constructed_(c.id()));
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

// A member a constructor or destructor of its own builds or destroys: a class that is not its bytes, no virtual base in it.
bool Lowering::builtMember(const Type &t) const {
    return t.isStructOrUnion() && t.isComplete() && !plainClass(t) && !t.unqualified()->hasVirtualBase();
}

// simpleLayout's class with members of class type and non-virtual bases besides, each built by what the parser chose;
// with `vptr`, a class with one and polymorphic bases besides - a written constructor's or destructor's, which stores it.
bool Lowering::buildableLayout(const Type &cls, bool vptr) const {
    if ((cls.polymorphic() && !vptr) || cls.hasVirtualBase() || cls.kind() == Kind::Union) return false;
    for (const Type::BaseSpec &b : cls.bases())
        if (b.isVirtual || !b.direct || !b.type->isComplete() || b.type->unqualified()->hasVirtualBase() ||
            (b.type->unqualified()->polymorphic() && !vptr))
            return false;
    for (const Member &m : cls.members()) {
        const Type &t = *m.type->unqualified();
        if (m.width != 0 || m.anonUnion != nullptr || t.isX87(target_)) return false;
        if (m.type->isReference() ? !referable(*m.type->referent())
                                  : !scalar(t) && !plainClass(t) && !builtMember(t) && !(t.isArray() && t.pointee()->isComplete() && scalar(*t.pointee())))
            return false;
    }
    return true;
}

// A call of a function the parser chose by symbol, its type the record's: a constructor or a destructor of a member.
void Lowering::directCall(const std::string &symbol, TypeId signature, const std::vector<ValueId> &ops) {
    CallInfo info;
    info.symbol = symbol;
    info.signature = signature;
    info.namedArgs = static_cast<int>(T(signature).params().size());
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    const InstId c = callOrInvoke(TypeId(), ops);
    at(c).call = callIndex;
}

// [class.base.init]/8 for a member of class type: the constructor the parser chose for it from the list's entry - or,
// the list leaving it, its default constructor - called on its address with the entry's values; none where none was.
void Lowering::buildMember(ValueId at, const Member &m, sema::TypedId entry) {
    const Type &mt = *m.type->unqualified();
    const std::uint32_t pos = entry.valid() ? syn(entry).loc : 0;
    std::string symbol;
    TypeId sig;
    bool defaulted = false;
    if (!memberBuilt_ || !memberBuilt_(F().symbol, m.name, pos, symbol, sig, defaulted)) {
        if (!entry.valid() && !(constructed_ && constructed_(mt.id()))) return;
        outside("a member built by no constructor the parser recorded");
    }
    if (defaulted) outside("a member built with default arguments");
    const Type &ft = T(sig);
    if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &mt) outside("a member's constructor of another class");
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, at))));
    if (entry.valid()) arguments(entry, 0, ft, 1, false, ops);
    else if (ft.params().size() != 1) outside("a member's default constructor of parameters");
    directCall(symbol, sig, ops);
}

// [class.dtor]/8: after a destructor's body, the members a destructor of their own destroys, last declared first.
void Lowering::destroyMembers(const Type &cls) {
    const std::vector<Member> &ms = cls.members();
    for (std::size_t n = ms.size(); n-- > 0; ) {
        const Type &mt = *ms[n].type->unqualified();
        if (ms[n].declaredIn != nullptr || !builtMember(mt) || !destructor_) continue;
        const std::string symbol = destructor_(mt.id());
        if (symbol.empty()) continue;
        const TypeId self = types_.pointerTo(mt.id());
        const InstId g = add(Op::Gep, self, std::vector<ValueId>(1, this_));
        at(g).k = ms[n].offset;
        directCall(symbol, types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, self)),
                   std::vector<ValueId>(1, result(g)));
    }
}

// Which of the class's bases a mem-initialiser's name is - its tag or its own name - or -1.
int Lowering::baseNamed(const Type &cls, const std::string &name) const {
    for (std::size_t b = 0; b < cls.bases().size(); b++) {
        const Type &bt = *cls.bases()[b].type->unqualified();
        if (bt.tag() == name || bt.localName() == name) return static_cast<int>(b);
    }
    return -1;
}

// The address of a non-virtual base: this, moved to where the layout put it.
ValueId Lowering::baseAddress(const Type::BaseSpec &b) {
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(b.type->unqualified())), std::vector<ValueId>(1, this_));
    at(g).k = b.offset;
    return result(g);
}

// A base built by the base-object constructor the parser chose for it - Itanium's C2 - from its entry or by default.
void Lowering::buildBase(const Type::BaseSpec &b, sema::TypedId entry) {
    const Type &bt = *b.type->unqualified();
    std::string symbol;
    TypeId sig;
    bool defaulted = false;
    if (!memberBuilt_ || !memberBuilt_(F().symbol, "^" + bt.tag(), 0, symbol, sig, defaulted)) {
        if (!entry.valid() && !(constructed_ && constructed_(bt.id()))) return;
        outside("a base built by no constructor the parser recorded");
    }
    if (defaulted) outside("a base built with default arguments");
    const Type &ft = T(sig);
    if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &bt) outside("a base's constructor of another class");
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, baseAddress(b)))));
    if (entry.valid()) arguments(entry, 0, ft, 1, false, ops);
    else if (ft.params().size() != 1) outside("a base's default constructor of parameters");
    directCall(symbol, sig, ops);
}

// [class.dtor]/8: last of all, the bases by their base-object destructors - Itanium's D2 - in the reverse order.
void Lowering::destroyBases(const Type &cls) {
    for (std::size_t n = cls.bases().size(); n-- > 0; ) {
        const Type::BaseSpec &b = cls.bases()[n];
        const Type &bt = *b.type->unqualified();
        std::string symbol;
        TypeId sig;
        bool defaulted = false;
        if (!memberBuilt_ || !memberBuilt_(F().symbol, "~" + bt.tag(), 0, symbol, sig, defaulted)) {
            if (destructor_ && !destructor_(bt.id()).empty()) outside("a base destroyed by no destructor the parser recorded");
            continue;
        }
        const Type &ft = T(sig);
        if (ft.params().size() != 1 || ft.params()[0]->pointee()->unqualified() != &bt) outside("a base's destructor of another class");
        directCall(symbol, sig, std::vector<ValueId>(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, baseAddress(b))))));
    }
}

// The members in declaration order: each named in the list initialised from it - `m()` and `m{}` value-initialised,
// one value stored or a plain class copied - and one the list leaves alone default-initialised, which is nothing.
void Lowering::memberInits(sema::TypedId fnNode, std::uint32_t from, std::uint32_t to) {
    const Type &cls = *T(thisType_).pointee()->unqualified();
    std::map<std::string, sema::TypedId> named;
    for (std::uint32_t i = from; i < to; i++) {
        const sema::TypedId mi = child(fnNode, i);
        if (syn(mi).kind != syntax::Kind::MemInit) outside("a constructor part that is not a mem-initialiser");
        if (cls.findMember(text(mi)) == nullptr && baseNamed(cls, text(mi)) < 0) outside("a mem-initialiser of the class itself or of no base");
        named[text(mi)] = mi;
    }
    // [class.base.init]/10: the bases first, in the order the class names them, each by the constructor the parser chose.
    for (std::size_t b = 0; b < cls.bases().size(); b++) {
        sema::TypedId entry;
        for (std::map<std::string, sema::TypedId>::const_iterator e = named.begin(); e != named.end(); ++e)
            if (cls.findMember(e->first) == nullptr && baseNamed(cls, e->first) == static_cast<int>(b)) entry = e->second;
        buildBase(cls.bases()[b], entry);
        // [except.ctor]/3: a base with a destructor, built, is destroyed - by its D2 - if a later part throws.
        const Type &bt = *cls.bases()[b].type->unqualified();
        if (landingPads_ && bt.hasDestructor()) {
            std::string symbol;
            TypeId sig;
            bool defaulted = false;
            if (!memberBuilt_ || !memberBuilt_(F().symbol, "~" + bt.tag(), 0, symbol, sig, defaulted))
                outside("a base destroyed on unwinding by no destructor the parser recorded");
            guard(result(add(Op::Bitcast, tid(*T(sig).params()[0]), std::vector<ValueId>(1, baseAddress(cls.bases()[b])))), bt, symbol);
        }
    }
    // [class.cdtor]/4: once the bases are built the object is of this class - its vptrs, before any member.
    storeVptrs();
    for (const Member &m : cls.members()) {
        if (m.declaredIn != nullptr) continue;     // a base's, which its constructor built
        const std::map<std::string, sema::TypedId>::const_iterator e = named.find(m.name);
        const Type &mt = *m.type->unqualified();
        if (e == named.end()) {
            if (defaultInit_ && defaultInit_(cls.id(), m.name)) outside("a member's default initialiser");
            if (!builtMember(mt)) continue;
        }
        if (m.type->isReference()) {
            // [class.base.init]/8: a reference member bound to an object of its referent's type - the address stored.
            if (count(e->second) != 1 || !isLvalueNode(child(e->second, 0))) outside("a reference member bound to what is not an object");
            const Val a = lvalue(child(e->second, 0));
            const Type &referent = *m.type->referent();
            if (T(a.type).unqualified() != referent.unqualified() || (T(a.type).isConst() && !referent.isConst())) outside("a reference member bound to another type");
            const TypeId held = types_.pointerTo(referent.id());
            const InstId r = add(Op::Gep, types_.pointerTo(held), std::vector<ValueId>(1, this_));
            at(r).k = m.offset;
            store(result(r), result(add(Op::Bitcast, held, std::vector<ValueId>(1, a.v))));
            continue;
        }
        const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, this_));
        at(g).k = m.offset;
        if (builtMember(mt)) {
            if (e != named.end() && count(e->second) == 0) outside("a member of class type value-initialised");
            buildMember(result(g), m, e != named.end() ? e->second : sema::TypedId());
            // [except.ctor]/3: a member with a destructor, built, is destroyed if a later part of the constructor throws.
            if (landingPads_ && mt.hasDestructor()) {
                const std::string dtor = destructor_ ? destructor_(mt.id()) : std::string();
                if (dtor.empty()) outside("a member destroyed on unwinding by no destructor the parser recorded");
                guard(result(g), mt, dtor);
            }
            continue;
        }
        const std::uint32_t k = count(e->second);
        if (k == 0) {
            const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, result(g)));
            at(z).k = mt.size(target_);
        } else if (k == 1 && scalar(mt)) {
            store(result(g), exprTo(child(e->second, 0), tid(mt)).v);
        } else if (k == 1 && plainClass(mt) && objectNode(child(e->second, 0))) {
            const Val src = object(child(e->second, 0));
            if (T(src.type).unqualified() != &mt) outside("a member built from another type");
            copyObject(result(g), src.v, mt);
        } else {
            outside("a mem-initialiser of " + std::to_string(k) + " values for '" + m.name + "'");
        }
    }
}

// The vptrs the parser stores in this constructor or destructor: each the address point of a table, at its offset.
void Lowering::storeVptrs() {
    const TypeId word = types_.pointerTo(types_.voidType()->id());
    for (const sema::VptrStore &s : vptrs_) {
        const InstId g = add(Op::Gep, types_.pointerTo(word), std::vector<ValueId>(1, this_));
        at(g).k = s.at;
        const InstId a = add(Op::Addr, word, std::vector<ValueId>());
        at(a).sym = s.symbol;
        at(a).k = s.addend;
        store(result(g), result(a));
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
        if (v.count != 1 || !objectNode(child(var, 0))) outside("a class local initialised by what is not an object");
        const Val o = object(child(var, 0));
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

// A class whose local a constructor builds or a destructor destroys, the lowering calling both: no vptr, no base, the
// layout simpleLayout or buildableLayout takes - the constructors and destructors the lowering itself reaches (M3b.2).
bool Lowering::builtClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete() || plainClass(t)) return false;
    const Type &c = *t.unqualified();
    return c.kind() != Kind::Union && (simpleLayout(c) || buildableLayout(c));
}

// [dcl.init]/17 and [class.dtor]/4 for such a local: the constructor the parser chose called on
// its slot with the declarator's values - after a zeroing where [dcl.init]/8 asked one - or, where none built it, the
// plain forms; then, with a destructor, alive to its scope's end and its region unwinding to a cleanup that destroys it.
void Lowering::builtLocal(sema::TypedId var, ValueId slot, const Type &t) {
    const Type &c = *t.unqualified();
    const sema::TypedNode &v = tree_.node(var);
    if (tree_.resolution(var).what == sema::Resolution::Call) {
        std::string symbol;
        TypeId sig;
        bool defaulted = false, zeroed = false;
        if (!localBuilt_ || !localBuilt_(v.syntax, symbol, sig, defaulted, zeroed)) outside("a local built by no constructor the parser recorded");
        if (defaulted) outside("a local built with default arguments");
        if (v.initForm == 2) outside("a local built from a braced list");
        const Type &ft = T(sig);
        if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &c) outside("a local's constructor of another class");
        if (zeroed) {
            const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
            at(z).k = c.size(target_);
        }
        std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, slot))));
        if (v.count != 0) arguments(var, 0, ft, 1, false, ops);
        else if (ft.params().size() != 1) outside("a local's default constructor of parameters");
        directCall(symbol, sig, ops);
    } else {
        // No constructor ran: a copy of bytes, which a class whose copy is a constructor would have recorded as a call.
        if (constructed_ && constructed_(c.id()) && v.initForm != 1) outside("a local of a constructed class built by no constructor");
        classLocal(var, slot, t);
    }
    if (!c.hasDestructor()) return;
    // [except.handle]/16 ends a catch after its handler's objects are destroyed; a jump here ends it first.
    if (handlerDepth_ != 0) outside("a local with a destructor inside a handler");
    Live l;
    l.dtor = destructor_ ? destructor_(c.id()) : std::string();
    if (l.dtor.empty()) outside("a local whose destructor the parser recorded none for");
    guard(slot, c, l.dtor);
}

// An object alive from here: destroyed by `dtor` at its scope's end, and by a cleanup where a call in its region unwinds.
void Lowering::guard(ValueId slot, const Type &c, const std::string &dtor) {
    Live l;
    l.slot = slot;
    l.cls = c.id();
    l.dtor = dtor;
    Pad p;
    p.cleanup = true;
    p.own = 0;
    if (!pads_.empty()) { p.types = pads_.back().types; p.indices = pads_.back().indices; }
    l.pad = pads_.size();
    pads_.push_back(p);
    live_.push_back(l);
}

// The destructor called on the object: a call on the normal path, as the walker's is; an invoke in a cleanup's terminate scope.
void Lowering::destroy(const Live &l, bool invoke) {
    const TypeId self = types_.pointerTo(l.cls);
    CallInfo info;
    info.symbol = l.dtor;
    info.signature = types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, self));
    info.namedArgs = 1;
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    at(invoke ? callOrInvoke(TypeId(), std::vector<ValueId>(1, l.slot)) : add(Op::Call, TypeId(), std::vector<ValueId>(1, l.slot))).call = callIndex;
}

// A jump out of the scopes above `mark`: their objects destroyed, last built first; they stay alive on the path not taken.
void Lowering::destroyLive(std::size_t mark) {
    for (std::size_t k = live_.size(); k-- > mark; ) destroy(live_[k]);
}

// A scope's end: each object it built destroyed on the way out, last built first, and its cleanup - made where a call in
// its region could unwind - destroying it and handing on to the enclosing pad, or out of the frame. `keep`: a
// constructor's own parts, which its normal way out leaves built and only its cleanups destroy ([except.ctor]/3).
void Lowering::closeScope(std::size_t mark, bool keep) {
    while (live_.size() > mark) {
        const Live l = live_.back();
        live_.pop_back();
        if (l.pad + 1 != pads_.size()) outside("a scope's cleanup that is not the innermost pad");
        const Pad p = pads_.back();
        pads_.pop_back();
        if (!terminated() && !keep) destroy(l);
        if (!p.block.valid()) continue;
        if (!landingPads_) outside("a local's cleanup in a function the parser gave no landing pads");
        const BlockId back = cur_;
        place(p.block);
        const InstId cl = add(Op::CleanupPad, TypeId(), std::vector<ValueId>());
        at(cl).syms = p.types;
        for (std::size_t k = 0; k < p.indices.size(); k++) at(cl).cases.push_back(p.indices[k]);
        at(cl).bits = (p.types.empty() ? 0u : 1u) | (!pads_.empty() ? 2u : 0u);
        // Microsoft (M3-IR 3.6): the funclet destroys and returns to the runtime, which ends the program itself should the
        // destructor throw, as the walker's cleanup does; the cleanupret names the pad whose state this one chains to.
        if (target_.microsoftNames()) {
            destroy(l);
            const InstId r = add(Op::CleanupRet, TypeId(), std::vector<ValueId>());
            if (!pads_.empty()) {
                std::vector<ValueId> args;
                args.push_back(p.ptr);
                args.push_back(p.sel);
                Succ s;
                s.block = padBlock(pads_.back());
                s.first = static_cast<std::uint32_t>(F().operands.size());
                s.count = 2;
                F().operands.insert(F().operands.end(), args.begin(), args.end());
                at(r).succ.push_back(s);
            }
            place(back);
            continue;
        }
        // [except.terminate]: a destructor that throws while the stack unwinds ends the program - a catch-all row's pad.
        Pad t;
        t.cleanup = false;
        t.own = 1;
        t.types.push_back(std::string());
        t.indices.push_back(typeIndexFor(std::string()));
        pads_.push_back(t);
        destroy(l, true);
        t = pads_.back();
        pads_.pop_back();
        resumeFrom(p, pads_.size());
        if (t.block.valid()) {
            place(t.block);
            const InstId tp = add(Op::CatchPad, TypeId(), std::vector<ValueId>());
            at(tp).syms = t.types;
            at(tp).cases.push_back(t.indices[0]);
            at(tp).k = 1;
            runtimeCall("__cxa_begin_catch", tid(*types_.pointerTo(types_.voidType())), std::vector<ValueId>(1, t.ptr));
            runtimeCall("_ZSt9terminatev", tid(*types_.voidType()), std::vector<ValueId>());
            add(Op::Unreachable, TypeId(), std::vector<ValueId>());
        }
        place(back);
    }
}

void Lowering::copyObject(ValueId to, ValueId from, const Type &t) {
    std::vector<ValueId> ops;
    ops.push_back(to);
    ops.push_back(from);
    const InstId c = add(Op::Memcpy, TypeId(), ops);
    at(c).k = t.size(target_);
}

// A plain class's temporary as the parser recorded it, `P()` or `P(o)`: a call of the class's name that no constructor
// built. `P{...}` is not one of them - its list's values are loose in the syntax tree's parent, and it stays refused.
bool Lowering::plainTemporary(sema::TypedId id) const {
    if (syn(id).kind != syntax::Kind::Call || count(id) == 0 || count(id) > 2) return false;
    const sema::Resolution &r = tree_.resolution(id);
    const TypeId t = TypeTable::id(tree_.node(id).type);
    return r.what == sema::Resolution::Temporary && r.symbol.empty() && t.valid() && plainClass(T(t)) &&
           tree_.resolution(child(id, 0)).what == sema::Resolution::TypeName;
}

// A class with constructors the lowering can build on storage it has: buildableLayout's, with no destructor to run
// at a temporary's end and no virtual base - the constructor the parser chose called on the address, nothing else.
bool Lowering::constructedClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete()) return false;
    const Type &c = *t.unqualified();
    return constructed_ && constructed_(c.id()) && !c.hasDestructor() && !c.hasVirtualBase() && buildableLayout(c);
}

// `T(...)` of such a class, the constructor the parser chose recorded for the node: `*(ctor(&$tmp, ...), &$tmp)`.
bool Lowering::constructedTemporary(sema::TypedId id) const {
    if (syn(id).kind != syntax::Kind::Call || count(id) == 0 || !temporaryBuilt_) return false;
    const TypeId t = TypeTable::id(tree_.node(id).type);
    if (tree_.resolution(id).what != sema::Resolution::Temporary || !t.valid() || !constructedClass(T(t))) return false;
    std::string symbol;
    TypeId sig;
    bool zero;
    return tree_.resolution(child(id, 0)).what == sema::Resolution::TypeName && temporaryBuilt_(tree_.node(id).syntax, symbol, sig, zero);
}

// The recorded constructor called on `at`: zeroed first where `T()` of an implicit one is ([dcl.init]/8), then the
// arguments from child `from` of `args` on against the constructor's type - one written per parameter, no default.
void Lowering::construct(ValueId where, const std::string &symbol, TypeId sig, const Type &cls, bool zeroFirst, sema::TypedId args,
                         std::uint32_t from) {
    const Type &ft = T(sig);
    if (!ft.isFunction() || ft.params().empty() || ft.params()[0]->pointee()->unqualified() != cls.unqualified())
        outside("a constructor of another class");
    if (ft.params().size() != static_cast<std::size_t>(count(args) - from) + 1) outside("a constructor called with default arguments");
    if (zeroFirst) {
        const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, where));
        at(z).k = cls.size(target_);
    }
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, where))));
    arguments(args, from, ft, 1, false, ops);
    directCall(symbol, sig, ops);
}

// [expr.type.conv] into a slot of this frame, which lives as long as the frame: `P()` value-initialised, which for a
// plain class is zeroing ([dcl.init]/8), and `P(o)` a copy of o's bytes. The slot's address is the temporary's value.
// A class with constructors is built in its slot by the constructor the parser chose, its arguments the call's.
Lowering::Val Lowering::temporary(sema::TypedId id) {
    const Type &t = *tree_.node(id).type->unqualified();
    const ValueId slot = entryAlloca(tid(t), "$tmp");
    if (constructedTemporary(id)) {
        std::string symbol;
        TypeId sig;
        bool zero = false;
        temporaryBuilt_(tree_.node(id).syntax, symbol, sig, zero);
        construct(slot, symbol, sig, t, zero, id, 1);
        return Val(slot, tid(t));
    }
    if (count(id) == 1) {
        const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
        at(z).k = t.size(target_);
    } else {
        if (!objectNode(child(id, 1))) outside("a class temporary from what is not an object");
        const Val o = object(child(id, 1));
        if (T(o.type).unqualified() != &t) outside("a class temporary from another type");
        copyObject(slot, o.v, t);
    }
    return Val(slot, tid(t));
}

// What a plain class's value can be copied from: an lvalue, a call's result, a temporary, an assignment, or a
// conditional of two such.
bool Lowering::objectNode(sema::TypedId id) const {
    if (isLvalueNode(id) || plainTemporary(id)) return true;
    const syntax::Kind k = syn(id).kind;
    if (k == syntax::Kind::Paren) return count(id) == 1 && objectNode(child(id, 0));
    // [expr.ass]/1: an assignment's value is its left object, which a plain class's copy assignment hands back.
    if (k == syntax::Kind::Assign) return text(id) == "=" && tree_.node(id).type != nullptr && plainClass(*tree_.node(id).type);
    if (k != syntax::Kind::Conditional || count(id) != 3) return false;
    const TypeId x = TypeTable::id(tree_.node(child(id, 1)).type), y = TypeTable::id(tree_.node(child(id, 2)).type);
    return x.valid() && y.valid() && T(x).unqualified() == T(y).unqualified() && plainClass(T(x)) &&
           objectNode(child(id, 1)) && objectNode(child(id, 2));
}

// Such a value's address: a conditional's is the chosen arm's, each built in its own branch, as a conditional lvalue's.
Lowering::Val Lowering::object(sema::TypedId id) {
    if (isLvalueNode(id)) return lvalue(id);
    if (plainTemporary(id)) return temporary(id);
    if (syn(id).kind == syntax::Kind::Paren && count(id) == 1) return object(child(id, 0));
    if (syn(id).kind == syntax::Kind::Assign && objectNode(id)) return assign(id);
    if (syn(id).kind != syntax::Kind::Conditional || !objectNode(id)) outside("a class value that is not an object");
    const Val c = truth(expr(child(id, 0)));
    const BlockId a = newBlock(), b = newBlock(), merge = newBlock();
    condbr(c.v, a, b);
    place(a);
    const Val x = object(child(id, 1));
    const Type &cls = *T(x.type).unqualified();
    const TypeId pt = tid(*types_.pointerTo(&cls));
    br(merge, std::vector<ValueId>(1, result(add(Op::Bitcast, pt, std::vector<ValueId>(1, x.v)))));
    place(b);
    const Val y = object(child(id, 2));
    if (T(y.type).unqualified() != &cls) outside("a conditional of two objects apart in type");
    br(merge, std::vector<ValueId>(1, result(add(Op::Bitcast, pt, std::vector<ValueId>(1, y.v)))));
    place(merge);
    return Val(param(merge, pt), tid(cls));
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
    f.blockArena.back().funclet = funclet_.v;
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
    staticLocals_.clear();
    loops_.clear();
    switches_.clear();
    labels_.clear();
    hasGoto_ = false;
    pads_.clear();
    live_.clear();
    liveFloor_ = 0;
    typeTable_.clear();
    handlerDepth_ = 0;
    funclet_ = BlockId();
    msTryPad_ = BlockId();
    landingPads_ = facts.landingPads;
    vptrs_.clear();
    try {
        if (facts.variadic) outside("a variadic function");
        if (facts.wrappedBody) outside("a function try block or an exception specification");
        if (facts.landingPads && facts.noexcept_) outside("a noexcept function with landing pads");
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
        bool isCtor = false, destroys = false;
        if (facts.hasThis && facts.thisType.valid() && T(facts.thisType).isPointer()) {
            const Type &cls = *T(facts.thisType).pointee()->unqualified();
            isCtor = text(fnNode) == cls.localName();
            const bool isDtor = !text(fnNode).empty() && text(fnNode)[0] == '~';
            if ((isCtor || isDtor) && !simpleLayout(cls)) {
                if (!buildableLayout(cls, true)) outside(isCtor ? "a constructor of a class with bases, a vptr or members to build"
                                                                : "a destructor of a class with bases, a vptr or members to destroy");
                // The vptr stores are the parser's record, made where it stores them: one, out of the class's own table.
                if (cls.hasVptr()) {
                    if (!vptrStores_ || !vptrStores_(facts.symbol, vptrs_) || vptrs_.empty()) outside("a vptr the parser recorded no store for");
                    if (vptrs_.size() != 1) outside("a polymorphic base that is not primary");
                    if (vptrs_[0].symbol.empty()) outside("a vptr out of a VTT");
                }
                // A constructor's guards are memberInits' own; a destructor's - what a throwing body still destroys - are not yet.
                if (facts.landingPads && !isCtor) outside("a destructor with landing pads that destroys members");
                destroys = isDtor;
            }
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
        const BlockId ret = returnBlock_;
        // A destructor's every way out of its body goes through the members' destruction first.
        if (destroys) returnBlock_ = newBlock();
        const BlockId first = newBlock();
        br(first);
        place(first);
        if (isCtor) memberInits(fnNode, inits, n - 1);
        else if (destroys) storeVptrs();
        liveFloor_ = live_.size();
        block(body);
        if (!terminated()) {
            if (!rt.isVoid()) outside("a body that falls off its end without a value");
            br(returnBlock_);
        }
        closeScope(0, true);
        if (destroys) {
            place(returnBlock_);
            destroyMembers(*T(thisType_).pointee()->unqualified());
            destroyBases(*T(thisType_).pointee()->unqualified());
            br(ret);
            returnBlock_ = ret;
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
    const std::size_t mark = live_.size();
    for (std::uint32_t i = 0; i < count(id); i++) stmt(child(id, i));
    closeScope(mark);
}

// A substatement is a scope of its own ([stmt.select]/1, [stmt.iter]/2): what a bare declaration there builds ends with it.
void Lowering::scopedStmt(sema::TypedId id) {
    const std::size_t mark = live_.size();
    stmt(id);
    closeScope(mark);
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
        const Val c = condition(child(id, 0));
        const BlockId then = newBlock(), merge = newBlock();
        const BlockId otherwise = n == 3 ? newBlock() : merge;
        condbr(c.v, then, otherwise);
        place(then);
        scopedStmt(child(id, 1));
        br(merge);
        if (n == 3) {
            place(otherwise);
            scopedStmt(child(id, 2));
            br(merge);
        }
        place(merge);
        return;
    }
    case syntax::Kind::While: {
        if (n != 2) outside("a while of " + std::to_string(n) + " parts");
        const BlockId cond = newBlock(), body = newBlock(), exit = newBlock();
        br(cond);
        place(cond);
        const Val c = condition(child(id, 0));
        condbr(c.v, body, exit);
        place(body);
        loops_.push_back(Loop{ exit, cond, handlerDepth_, live_.size() });
        scopedStmt(child(id, 1));
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
        loops_.push_back(Loop{ exit, cond, handlerDepth_, live_.size() });
        scopedStmt(child(id, 0));
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
        leaveHandlers(handlerDepth_ - loops_.back().handlers, loops_.back().live, loops_.back().brk);
        return;
    case syntax::Kind::Continue: {
        for (std::size_t i = loops_.size(); i-- > 0; )
            if (loops_[i].cont.valid()) { leaveHandlers(handlerDepth_ - loops_[i].handlers, loops_[i].live, loops_[i].cont); return; }
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
        leaveHandlers(handlerDepth_, liveFloor_, returnBlock_);
        return;
    }
    if (n == 1 && rt.isVoid()) {
        const Val v = expr(child(id, 0));
        if (v.type.valid() && !T(v.type).isVoid()) outside("a return of a value from a void function");
        leaveHandlers(handlerDepth_, liveFloor_, returnBlock_);
        return;
    }
    if (n != 1) outside("a return of " + std::to_string(n) + " parts");
    if (rt.isReference()) {
        const Val a = lvalue(child(id, 0));
        if (T(a.type).unqualified() != rt.referent()->unqualified()) outside("a reference returned to another type");
        if (T(a.type).isConst() && !rt.referent()->isConst()) outside("a reference that can write returned from a const");
        const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(rt.referent())), std::vector<ValueId>(1, a.v));
        leaveHandlers(handlerDepth_, liveFloor_, returnBlock_, std::vector<ValueId>(1, result(c)));
        return;
    }
    if (rt.isStructOrUnion()) {
        if (!objectNode(child(id, 0))) outside("a class returned from what is not an object");
        const Val a = object(child(id, 0));
        if (T(a.type).unqualified() != &rt) outside("a class returned from another type");
        if (!live_.empty()) outside("a class returned while a local with a destructor is alive");
        const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(&rt)), std::vector<ValueId>(1, a.v));
        br(returnBlock_, std::vector<ValueId>(1, result(c)));
        return;
    }
    const Val v = exprTo(child(id, 0), returns_);
    leaveHandlers(handlerDepth_, liveFloor_, returnBlock_, std::vector<ValueId>(1, v.v));
}

// [stmt.select]/2: a condition, or a declaration whose variable's value is the condition - declared where the test
// runs, so a `while` declares it again each turn. Only a scalar's value is converted to bool here.
Lowering::Val Lowering::condition(sema::TypedId id) {
    if (syn(id).kind != syntax::Kind::DeclStmt) return truth(expr(id));
    if (count(id) != 1 || syn(child(id, 0)).kind != syntax::Kind::Var) outside("a condition declaring more than one name");
    const sema::TypedId var = child(id, 0);
    const TypeId t = TypeTable::id(tree_.node(var).type);
    if (!t.valid() || !scalar(T(t)) || T(t).isX87(target_)) outside("a condition declaring what is not a scalar");
    declaration(id);
    const std::map<std::uint32_t, std::pair<ValueId, TypeId> >::const_iterator l = locals_.find(tree_.node(var).syntax.v);
    if (l == locals_.end()) outside("a condition's variable the lowering did not declare");
    return truth(readOf(l->second.first, l->second.second));
}

// A `for` keeps its parts in order and leaves an empty one out; which ones it has the parser records on the node
// (8 says it did - a range-based `for` is not recorded, being a rewrite of its own).
void Lowering::forStmt(sema::TypedId id) {
    const std::uint32_t n = count(id);
    const int parts = tree_.node(id).forParts;
    if (!(parts & 8)) outside("a range-based for");
    const std::uint32_t want = 1 + ((parts & 1) ? 1 : 0) + ((parts & 2) ? 1 : 0) + ((parts & 4) ? 1 : 0);
    if (n != want) outside("a for whose parts the record and the syntax disagree about");
    std::uint32_t at = 0;
    const std::size_t scope = live_.size();      // [stmt.for]/1: the init's objects live to the loop's end
    if (parts & 1) {
        const sema::TypedId init = child(id, at++);
        if (syn(init).kind == syntax::Kind::DeclStmt) declaration(init); else expr(init);
    }
    const sema::TypedId condNode = (parts & 2) ? child(id, at++) : sema::TypedId();
    const sema::TypedId stepNode = (parts & 4) ? child(id, at++) : sema::TypedId();
    if (condNode.valid() && syn(condNode).kind == syntax::Kind::DeclStmt) outside("a declaration in a for's condition");
    const BlockId cond = newBlock(), body = newBlock(), step = newBlock(), exit = newBlock();
    br(cond);
    place(cond);
    if (condNode.valid()) {
        const Val c = truth(expr(condNode));
        condbr(c.v, body, exit);
    } else {
        br(body);
    }
    place(body);
    loops_.push_back(Loop{ exit, step, handlerDepth_, live_.size() });
    scopedStmt(child(id, n - 1));
    loops_.pop_back();
    br(step);
    place(step);
    if (stepNode.valid()) expr(stepNode);
    br(cond);
    place(exit);
    closeScope(scope);
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
    loops_.push_back(Loop{ exit, BlockId(), handlerDepth_, live_.size() });
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
        if (r.what == sema::Resolution::Local && !r.symbol.empty()) {
            // [stmt.dcl]/4: a static local is the global the parser laid down, its address taken where it is named;
            // one built at run time under a guard, a thread_local and a reference stay outside.
            if (r.isThreadLocal || threadLocalGlobal(r.symbol)) outside("a thread_local");
            if (!guarded_ || guarded_(r.symbol)) outside("a static local built at run time");
            if (v.type->isReference()) outside("a static reference");
            if (v.type->isStructOrUnion() && v.type->unqualified()->hasDestructor()) outside("a static local with a destructor");
            staticLocals_[v.syntax.v] = std::make_pair(r.symbol, tid(*v.type));
            continue;
        }
        const Type &t = *v.type;
        TypeId slotType;
        if (t.isReference()) {
            if (!referable(*t.referent())) outside("a reference to '" + t.referent()->describe() + "'");
            slotType = tid(*types_.pointerTo(t.referent()));
        } else if (t.isArray()) {
            if (!t.pointee()->isComplete() || !scalar(*t.pointee()) || t.pointee()->isX87(target_) || t.length() <= 0)
                outside("an array of '" + t.pointee()->describe() + "'");
            slotType = tid(t);
        } else if (plainClass(t) || builtClass(t)) {
            slotType = tid(*t.unqualified());
        } else {
            if (!scalar(t) || t.isX87(target_)) outside("a local of type '" + t.describe() + "'");
            slotType = tid(t);
        }
        const ValueId slot = entryAlloca(slotType, text(var));
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
        if (t.isStructOrUnion()) { if (plainClass(t)) classLocal(var, slot, t); else builtLocal(var, slot, t); continue; }
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
    if (operatorCalls(id)) {
        const sema::Resolution &r = tree_.resolution(id);
        return r.type.valid() && T(r.type).isFunction() && (T(r.type).returns()->isReference() || plainClass(*T(r.type).returns()));
    }
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
        return r.what == sema::Resolution::Call && r.type.valid() && T(r.type).isFunction() &&
               (T(r.type).returns()->isReference() || plainClass(*T(r.type).returns()));
    }
    default: return false;
    }
}

// An object's address and its type, const kept on the type.
Lowering::Val Lowering::lvalue(sema::TypedId id) {
    const syntax::Node &s = syn(id);
    if (operatorCalls(id)) {
        const Val r = operatorCall(id);
        if (!T(r.type).isPointer()) outside("an operator that is no lvalue");
        return Val(r.v, tid(*T(r.type).pointee()));
    }
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
        if (temporaryNode(id)) return temporary(id);
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
    if (operatorCalls(id)) {
        const Val c = operatorCall(id);
        const Type &gives = *T(tree_.resolution(id).type).returns();
        // A scalar returned by reference is read; a class's address is no value - the statement is the call.
        if (gives.isReference()) return scalar(*gives.referent()) && !gives.referent()->isX87(target_) ? readOf(c.v, tid(*gives.referent())) : Val();
        return gives.isStructOrUnion() ? Val() : c;
    }
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
        if (temporaryNode(id)) { temporary(id); e = Val(); break; }      // a class value as a statement: built, unread
        e = call(id);
        const sema::Resolution &r = tree_.resolution(id);
        if (r.what == sema::Resolution::Call && T(r.type).returns()->isReference())
            e = readOf(e.v, tid(*T(r.type).returns()->referent()));
        else if (r.what == sema::Resolution::Call && T(r.type).returns()->isStructOrUnion())
            e = Val();      // a class result read as no value: its statement is the call
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
        // [expr.reinterpret.cast]/7: a pointer reinterpreted is the same address, never stepped to a base or back.
        if (text(id).compare(0, 16, "reinterpret_cast") == 0 && to.isPointer() && T(v.type).isPointer()) {
            e = Val(result(add(Op::Bitcast, n.castType, std::vector<ValueId>(1, v.v))), n.castType);
            break;
        }
        e = convert(v, n.castType, true);
        break;
    }
    case syntax::Kind::Throw:
        throwExpr(id);
        e = Val();
        break;
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
        const std::map<std::uint32_t, std::pair<std::string, TypeId> >::const_iterator s = staticLocals_.find(n.decl.v);
        if (s != staticLocals_.end()) return Val(addrOf(s->second.first, tid(*types_.pointerTo(&T(s->second.second)))), s->second.second);
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
    if (cls.findMember(text(id)) == nullptr) outside("a member reached through another object's `this`");
    const Member &m = *cls.findMember(text(id));
    if (m.offset != r.serial || !r.type.valid()) outside("a member reached through another object's `this`");
    if (m.ambiguous || m.inVirtualBase != nullptr || m.anonUnion != nullptr || m.type->isReference() || m.width != 0)
        outside("a member '" + text(id) + "' the lowering does not reach");
    const Type &mt = T(r.type);
    if (!scalar(mt) && !mt.isArray() && !mt.isStructOrUnion()) outside("a member of type '" + mt.describe() + "'");
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, this_));
    at(g).k = m.offset;
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
                return toBase(v, to, cast);
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
    if (op == "=" && plainClass(t) && !t.isConst() && !(assigned_ && assigned_(t.id())) && objectNode(child(id, 1))) {
        const Val from = object(child(id, 1));
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
// [conv.ptr]/3: a pointer to a class to one to its unambiguous non-virtual base, by the base's offset in the class,
// a null pointer kept null. A cast down, across, or to a virtual base stays outside.
Lowering::Val Lowering::toBase(Val v, TypeId to, bool cast) {
    const Type &from = *T(v.type).pointee()->unqualified(), &base = *T(to).pointee()->unqualified();
    int found = 0, offset = 0;
    basePaths(from, base, 0, found, offset);
    // [expr.static.cast]/11: a cast down from a non-virtual base to the class steps back by the base's offset.
    if (found == 0 && cast) {
        basePaths(base, from, 0, found, offset);
        offset = -offset;
    }
    if (found != 1) outside("a conversion of a pointer to a class to one that is not its base");
    if (offset == 0) return Val(result(add(Op::Bitcast, to, std::vector<ValueId>(1, v.v))), to);
    const Val nonNull = truth(v);
    const BlockId step = newBlock(), null = newBlock(), merge = newBlock();
    condbr(nonNull.v, step, null);
    place(step);
    const InstId g = add(Op::Gep, to, std::vector<ValueId>(1, v.v));
    at(g).k = offset;
    br(merge, std::vector<ValueId>(1, result(g)));
    place(null);
    br(merge, std::vector<ValueId>(1, result(add(Op::Null, to, std::vector<ValueId>()))));
    place(merge);
    return Val(param(merge, to), to);
}

// Every path from `cls` down its direct bases to `base`, as the parser walks them; a virtual one stays outside.
void Lowering::basePaths(const Type &cls, const Type &base, int at, int &found, int &offset) {
    for (const Type::BaseSpec &b : cls.bases()) {
        if (!b.direct) continue;
        const Type &bt = *b.type->unqualified();
        if (b.isVirtual) {
            int through = &bt == &base ? 1 : 0, ignored = 0;
            if (through == 0) basePaths(bt, base, 0, through, ignored);
            if (through != 0) outside("a conversion through a virtual base");
            continue;
        }
        if (&bt == &base) { found++; offset = at + b.offset; }
        else basePaths(bt, base, at + b.offset, found, offset);
    }
}

// Whether the program's global of this symbol is a thread_local: a block-scope one's record is made before its mark.
bool Lowering::threadLocalGlobal(const std::string &symbol) const {
    for (const ::Global &g : program_.globals)
        if (g.symbol == symbol) return g.isThreadLocal;
    return false;
}

// [expr.pseudo]: `p->~T()` of a scalar or a class with nothing to destroy ends no one's life but by name - the
// object is evaluated and nothing more is done. The syntax notes the call as the member alone, or inside a call.
bool Lowering::pseudoDestructor(sema::TypedId member) {
    const std::string &t = text(member);
    const bool arrow = t.compare(0, 2, "->") == 0;
    if (count(member) != 1 || t.size() <= (arrow ? 2u : 1u) || t[arrow ? 2 : 1] != '~') return false;
    const Val o = arrow ? decay(expr(child(member, 0))) : lvalue(child(member, 0));
    if (arrow && !T(o.type).isPointer()) outside("a destructor called through what is not a pointer");
    const Type &ot = arrow ? *T(o.type).pointee() : T(o.type);
    if (scalar(ot) || plainClass(*ot.unqualified())) return true;
    // [class.dtor]/14: a class's own destructor, called on the object; a virtual one would dispatch and stays outside.
    const Type &cls = *ot.unqualified();
    if (!cls.isStructOrUnion() || cls.hasVptr() || cls.hasVirtualBase()) outside("a destructor called by name");
    const std::string sym = destructor_ ? destructor_(cls.id()) : std::string();
    if (sym.empty() || sym.find_first_of(" ()*&[") != std::string::npos) outside("a destructor called by name whose symbol is not recorded");
    const TypeId self = tid(*types_.pointerTo(&cls));
    CallInfo info;
    info.symbol = sym;
    info.hasThis = true;
    info.signature = types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, self));
    info.namedArgs = 1;
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    const InstId c = callOrInvoke(TypeId(), std::vector<ValueId>(1, result(add(Op::Bitcast, self, std::vector<ValueId>(1, o.v)))));
    at(c).call = callIndex;
    return true;
}

Lowering::Val Lowering::call(sema::TypedId id) {
    const std::uint32_t n = count(id);
    if (n == 0) outside("a call of nothing");
    const sema::TypedId head = child(id, 0);
    const sema::Resolution &r = tree_.resolution(id);
    if (r.what == sema::Resolution::Temporary && !r.symbol.empty()) outside("a class temporary a constructor builds");
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
        if (syn(head).kind == syntax::Kind::Name && tree_.node(head).decl.valid()) {
            // [over.call.object]: a local or a parameter of class type called - its operator(), on that object.
            const Val o = lvalue(head);
            object = Val(o.v, tid(*types_.pointerTo(&T(o.type))));
        } else if (syn(head).kind == syntax::Kind::Name) {
            if (!this_.valid()) outside("a member call where there is no `this`");
            object = Val(this_, thisType_);
        } else if (syn(head).kind == syntax::Kind::Member && count(head) == 1) {
            const bool arrow = text(head).compare(0, 2, "->") == 0;
            if (arrow) object = decay(expr(child(head, 0)));
            else { const Val o = lvalue(child(head, 0)); object = Val(o.v, tid(*types_.pointerTo(&T(o.type)))); }
        } else {
            outside("a member call through '" + text(head) + "'");
        }
        if (!T(object.type).isPointer()) outside("a member call whose object is not a pointer");
        if (T(object.type).pointee()->isConst() && !self.pointee()->isConst()) outside("a member call that drops const");
        // A base's function, found through a derived object: `this` is the base subobject's address.
        if (T(object.type).pointee()->unqualified() != self.pointee()->unqualified()) object = toBase(object, tid(self));
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
    std::vector<sema::TypedId> args;
    for (std::uint32_t i = 1; i < n; i++) args.push_back(child(id, i));
    return callTail(sig, info, ops, member ? 1 : 0, args, false);
}

// A call once its function is known: the arguments by the parameters' rules, a class result's temporary, the call
// itself - an invoke inside a try. `skip` parameters are taken already (`this`); `dummyInt` is a postfix ++'s 0.
Lowering::Val Lowering::callTail(TypeId sig, CallInfo info, std::vector<ValueId> ops, std::size_t skip,
                                 const std::vector<sema::TypedId> &args, bool dummyInt) {
    const Type &ft = T(sig);
    info.signature = sig;
    info.variadic = ft.isVariadicFn();
    info.namedArgs = static_cast<int>(ft.params().size());
    const Type &gives = *ft.returns();
    if (!(gives.isVoid() || scalar(gives) || plainClass(gives) || (gives.isReference() && referable(*gives.referent()))))
        outside("a call returning '" + gives.describe() + "'");
    if (gives.isX87(target_)) outside("an x87 long double");
    argumentList(ft, skip, info.variadic, args, dummyInt, ops);
    if (dummyInt) ops.push_back(iconst(tid(*types_.get(Kind::Int)), 0));      // [over.inc]/1: the postfix form's 0
    // A plain class comes back into a temporary of this frame, and the call carries its address, as a reference's does.
    if (gives.isStructOrUnion()) {
        entryAlloca(tid(*gives.unqualified()), "$result");
        info.resultAlloca = static_cast<int>(F().frame.size() - 1);
    }
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    const TypeId carries = gives.isVoid() ? TypeId() : gives.isReference() ? tid(*types_.pointerTo(gives.referent()))
                         : gives.isStructOrUnion() ? tid(*types_.pointerTo(gives.unqualified())) : tid(gives);
    const InstId c = callOrInvoke(carries, ops);
    at(c).call = callIndex;
    return Val(result(c), carries.valid() ? carries : tid(gives));
}

// A call's arguments against function type `ft` whose first `skip` parameters are already given: a variadic one
// promoted, a plain class by its address, a reference bound, a scalar converted; `dummyInt` is a postfix ++'s 0.
void Lowering::argumentList(const Type &ft, std::size_t skip, bool variadic, const std::vector<sema::TypedId> &args,
                            bool dummyInt, std::vector<ValueId> &ops) {
    const std::size_t argc = args.size() + (dummyInt ? 1 : 0);
    const std::size_t paramCount = ft.params().size() - skip;
    if (argc < paramCount || (argc > paramCount && !variadic)) outside("a call with defaults or too many arguments");
    for (std::size_t i = 0; i < args.size(); i++) {
        const sema::TypedId an = args[i];
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
            if (!objectNode(an)) outside("a class argument that is not an object");
            const Val a = object(an);
            if (T(a.type).unqualified() != p.unqualified()) outside("a class argument of another type");
            ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(p.unqualified())), std::vector<ValueId>(1, a.v))));
            continue;
        }
        if (p.isReference()) {
            // [dcl.init.ref]/5: an object of the referent's type, its address passed; a scalar value bound to a const
            // or an rvalue reference is a temporary of the frame holding it, alive until the call is done.
            const Type &ref = *p.referent();
            if (!isLvalueNode(an) && scalar(ref) && !ref.isX87(target_) && (ref.isConst() || p.isRValueReference())) {
                const ValueId temp = entryAlloca(tid(*ref.unqualified()), "$bound");
                store(temp, exprTo(an, tid(*ref.unqualified())).v);
                ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(&ref)), std::vector<ValueId>(1, temp))));
                continue;
            }
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
}

// A call's arguments from child `from` of node `id` on.
void Lowering::arguments(sema::TypedId id, std::uint32_t from, const Type &ft, std::size_t skip, bool variadic,
                         std::vector<ValueId> &ops) {
    std::vector<sema::TypedId> args;
    for (std::uint32_t i = from; i < count(id); i++) args.push_back(child(id, i));
    argumentList(ft, skip, variadic, args, false, ops);
}

// Whether a node is an operator the parser resolved to a function - [over.match.oper] - rather than a built-in one.
bool Lowering::operatorCalls(sema::TypedId id) const {
    const syntax::Kind k = syn(id).kind;
    if (k != syntax::Kind::Binary && k != syntax::Kind::Unary && k != syntax::Kind::Postfix && k != syntax::Kind::Assign &&
        k != syntax::Kind::Subscript)
        return false;
    const sema::Resolution &r = tree_.resolution(id);
    return r.what == sema::Resolution::Call && !r.symbol.empty();
}

// An overloaded operator as the call it is: a member operator's left operand its object, a free one's operands all
// arguments, a postfix ++ or -- given its 0. What it returns is what the call carries.
Lowering::Val Lowering::operatorCall(sema::TypedId id) {
    const sema::Resolution &r = tree_.resolution(id);
    if (r.indirect) outside("a virtual operator");
    if (!r.type.valid() || !T(r.type).isFunction()) outside("an operator whose function type was not recorded");
    if (r.symbol.find_first_of(" ()*&[") != std::string::npos) outside("an operator of no plain symbol");
    const Type &ft = T(r.type);
    std::vector<sema::TypedId> operands;
    for (std::uint32_t i = 0; i < count(id); i++) operands.push_back(child(id, i));
    if (operands.empty()) outside("an operator of no operands");
    CallInfo info;
    info.symbol = r.symbol;
    std::vector<ValueId> ops;
    std::size_t skip = 0;
    if (r.hasThis) {
        if (ft.params().empty() || !ft.params()[0]->isPointer()) outside("a member operator whose `this` was not recorded");
        const Type &self = *ft.params()[0];
        if (!isLvalueNode(operands[0])) outside("a member operator of what is not an object");
        const Val o = lvalue(operands[0]);
        if (T(o.type).isConst() && !self.pointee()->isConst()) outside("a member operator that drops const");
        Val object(o.v, tid(*types_.pointerTo(&T(o.type))));
        if (T(o.type).unqualified() != self.pointee()->unqualified()) object = toBase(object, tid(self));
        ops.push_back(result(add(Op::Bitcast, tid(self), std::vector<ValueId>(1, object.v))));
        info.hasThis = true;
        operands.erase(operands.begin());
        skip = 1;
    }
    const std::size_t want = ft.params().size() - skip;
    const bool dummyInt = syn(id).kind == syntax::Kind::Postfix && want == operands.size() + 1;
    return callTail(r.type, info, ops, skip, operands, dummyInt);
}

// The parser's facts for a `new` or `delete` node, which the lowering takes only for a scalar or a plain class.
sema::NewFacts Lowering::newFacts(sema::TypedId id, const std::string &what) {
    sema::NewFacts f;
    if (!newFacts_ || !newFacts_(tree_.node(id).syntax, f) || (f.why.empty() && f.symbol.empty()))
        outside(std::string("a '") + what + "' not recorded");
    if (!f.why.empty()) outside(f.why);
    const Type &t = T(f.made);
    if (t.isX87(target_) || !(scalar(t) || plainClass(t) || constructedClass(t)))
        outside(std::string("a '") + what + "' of '" + t.describe() + "'");
    return f;
}

// [expr.new] for a scalar or a plain class: the global allocation function the parser called, given the bytes - n of
// them times the size for an array, counted first - then the initialiser's stores: none, zeroes, or the one value.
// A class with constructors takes the constructor the parser chose, called on the storage with the trailing parts.
Lowering::Val Lowering::newExpression(sema::TypedId id) {
    const sema::NewFacts f = newFacts(id, "new");
    const Type &made = T(f.made);
    const bool built = constructedClass(made);
    if (built && f.ctor.empty()) outside("a 'new' of a class with constructors");
    if (built && f.array) outside("an array 'new' of a class with constructors");
    if (built && f.ctorDefaulted) outside("a 'new' with default arguments");
    // [expr.new]/20: a constructor that throws has the storage freed, which no pad here does - the walker's own gap.
    if (built && !pads_.empty()) outside("a 'new' of a class with constructors inside a try");
    const std::uint32_t lead = f.placed ? 1u : 0u;        // the placement's address is the first part
    const std::uint32_t argc = built ? static_cast<std::uint32_t>(T(f.ctorSig).params().size()) - 1u : (f.init == 2 ? 1u : 0u);
    const std::uint32_t parts = lead + (f.array ? 1u : 0u) + argc;
    if (count(id) != parts) outside("a 'new' of " + std::to_string(count(id)) + " parts");
    const TypeId sizeT = tid(*types_.get(target_.sizeType()));
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    ValueId at0 = f.placed ? exprTo(child(id, 0), voidPtr).v : ValueId();
    ValueId bytes = iconst(sizeT, made.size(target_));
    if (f.array) {
        std::vector<ValueId> ops;
        ops.push_back(exprTo(child(id, lead), sizeT).v);
        ops.push_back(bytes);
        bytes = result(add(Op::Mul, sizeT, ops));
    }
    if (!f.placed) {
        CallInfo info;
        info.symbol = f.symbol;
        info.signature = types_.functionType(voidPtr, std::vector<TypeId>(1, sizeT));
        info.namedArgs = 1;
        const std::uint32_t callIndex = static_cast<std::uint32_t>(F().calls.size());
        F().calls.push_back(info);
        const InstId c = callOrInvoke(voidPtr, std::vector<ValueId>(1, bytes));      // bad_alloc may unwind
        at(c).call = callIndex;
        at0 = result(c);
    }
    const TypeId pt = tid(*types_.pointerTo(&made));
    const ValueId p = result(add(Op::Bitcast, pt, std::vector<ValueId>(1, at0)));
    if (built) {
        construct(p, f.ctor, f.ctorSig, made, f.ctorZero, id, lead);
    } else if (f.init == 1 && f.array) {
        // `new T[n]()` zeroes n elements with the platform's memset, as the parser and clang do.
        std::vector<ValueId> args;
        args.push_back(at0);
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
    info.signature = types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, voidPtr));
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
Lowering::Val Lowering::runtimeCall(const std::string &symbol, TypeId returns, const std::vector<ValueId> &args) {
    std::vector<TypeId> params;
    for (std::size_t i = 0; i < args.size(); i++) params.push_back(F().value(args[i]).type);
    CallInfo info;
    info.symbol = symbol;
    info.signature = types_.functionType(returns, params);
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

// An alloca in the entry block, whatever block the statement stands in; its value is the object's address. A frame
// object of 16 bytes or more is aligned at 16, as the walker lays it.
ValueId Lowering::entryAlloca(TypeId slotType, const std::string &name) {
    Function &f = F();
    AllocaInfo a;
    a.type = slotType;
    a.align = T(slotType).align(target_);
    const Type &st = T(slotType);
    if ((st.isArray() || st.isStructOrUnion()) && st.size(target_) >= 16 && a.align < 16) a.align = 16;
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
void Lowering::resumeFrom(const Pad &p, std::size_t enclosing) {
    if (enclosing != 0) {
        std::vector<ValueId> args;
        args.push_back(p.ptr);
        args.push_back(p.sel);
        br(padBlock(pads_[enclosing - 1]), args);
        return;
    }
    runtimeCall("_Unwind_Resume", tid(*types_.voidType()), std::vector<ValueId>(1, p.ptr));
    add(Op::Unreachable, TypeId(), std::vector<ValueId>());
}

// The thrown operand and the type the exception object takes: a plain class lvalue's bytes, or a scalar's value. A
// class the lowering cannot copy into the object by its bytes - one a constructor builds - stays out.
Lowering::Val Lowering::thrownValue(sema::TypedId id, TypeId &thrown) {
    if (count(id) != 1) outside("a throw of " + std::to_string(count(id)) + " parts");
    const sema::TypedId operand = child(id, 0);
    Val v;
    if (tree_.node(operand).type != nullptr && plainClass(*tree_.node(operand).type->unqualified()) && isLvalueNode(operand)) {
        v = lvalue(operand);
    } else {
        if (tree_.node(operand).type != nullptr && tree_.node(operand).type->unqualified()->isStructOrUnion())
            outside("a throw of a class that is not plain");
        v = decay(expr(operand));
        if (!v.type.valid() || !scalar(T(v.type)) || T(v.type).isX87(target_)) outside("a throw of what is not a scalar");
    }
    thrown = T(v.type).unqualified()->id();
    return v;
}

// [except.throw] on the Itanium ABI, as the parser writes it: the exception object allocated by the runtime, the
// operand stored into it - a scalar's value or a plain class's bytes - and __cxa_throw, an invoke inside a try;
// `throw;` is __cxa_rethrow. A class with a copy of its own stays outside, so does a throw in a noexcept function - the parser's value-then-abort, [except.spec]/9, with no pad on the Microsoft ABI; that ABI's own shape is below.
void Lowering::throwExpr(sema::TypedId id) {
    if (F().noexcept_) outside("a throw in a noexcept function");
    if (target_.microsoftNames()) { microsoftThrow(id); return; }
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    if (count(id) == 0) {
        throwCall("__cxa_rethrow", std::vector<ValueId>());
        return;
    }
    TypeId thrown;
    const Val v = thrownValue(id, thrown);
    if (!typeInfo_) outside("a throw with no type_info to name");
    const std::string info = typeInfo_(thrown);
    if (info.empty()) outside("a throw of a type with no type_info");
    const TypeId size = tid(*types_.get(target_.sizeType()));
    const Val object = runtimeCall("__cxa_allocate_exception", voidPtr,
                                   std::vector<ValueId>(1, iconst(size, T(thrown).size(target_))));
    const ValueId typed = result(add(Op::Bitcast, types_.pointerTo(thrown), std::vector<ValueId>(1, object.v)));
    if (T(thrown).isStructOrUnion()) copyObject(typed, v.v, T(thrown));
    else store(typed, convert(v, thrown).v);
    std::vector<ValueId> args;
    args.push_back(object.v);
    args.push_back(addrOf(info, voidPtr));
    args.push_back(result(add(Op::Null, voidPtr, std::vector<ValueId>())));
    throwCall("__cxa_throw", args);
}

// **The Microsoft ABI throws from the stack**, as the parser writes it: the object in a slot of the frame, then
// _CxxThrowException(&object, &_TI...) with the ThrowInfo chain the parser finished for this type - an invoke inside
// a cleanup's region. `throw;` is the same call with two nulls. A class with a copy of its own stays outside.
void Lowering::microsoftThrow(sema::TypedId id) {
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    std::vector<ValueId> args;
    if (count(id) == 0) {
        args.push_back(result(add(Op::Null, voidPtr, std::vector<ValueId>())));
        args.push_back(args[0]);
        throwCall("_CxxThrowException", args);
        return;
    }
    TypeId thrown;
    const Val v = thrownValue(id, thrown);
    if (!msThrowInfo_) outside("a throw with no ThrowInfo to name");
    const std::string info = msThrowInfo_(thrown);
    if (info.empty()) outside("a throw of a type whose ThrowInfo the parser did not finish");
    const ValueId object = entryAlloca(thrown, ".ex");
    if (T(thrown).isStructOrUnion()) copyObject(object, v.v, T(thrown));
    else store(object, convert(v, thrown).v);
    args.push_back(result(add(Op::Bitcast, voidPtr, std::vector<ValueId>(1, object))));
    args.push_back(addrOf(info, voidPtr));
    throwCall("_CxxThrowException", args);
}

// A runtime call that does not return: an invoke inside a try or a handler, so the throw reaches its pad, then the
// end of the block.
void Lowering::throwCall(const std::string &symbol, const std::vector<ValueId> &args) {
    std::vector<TypeId> params;
    for (std::size_t i = 0; i < args.size(); i++) params.push_back(F().value(args[i]).type);
    CallInfo info;
    info.symbol = symbol;
    info.signature = types_.functionType(types_.voidType()->id(), params);
    info.namedArgs = static_cast<int>(params.size());
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    const InstId c = callOrInvoke(TypeId(), args);
    at(c).call = callIndex;
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
    if (target_.microsoftNames()) { microsoftTry(id); return; }
    const std::uint32_t n = count(id);
    if (n < 2) outside("a try with no handler");
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    struct Handler { sema::TypedId node, var, body; TypeId caught; std::string sym; int index; };
    std::vector<Handler> hs;
    Pad p;
    p.cleanup = false;
    p.own = n - 1;
    for (std::uint32_t i = 1; i < n; i++) {
        Handler h;
        h.node = child(id, i);
        if (syn(h.node).kind != syntax::Kind::Catch) outside("a try whose part is not a handler");
        const std::uint32_t parts = count(h.node);
        h.caught = TypeId();
        h.index = 0;
        if (parts == 1 && text(h.node) == "...") {
            h.body = child(h.node, 0);
        } else if (parts == 2 && syn(child(h.node, 0)).kind == syntax::Kind::Var) {
            h.var = child(h.node, 0);
            h.body = child(h.node, 1);
            if (tree_.node(h.var).type == nullptr) outside("a caught object not typed");
            const Type &t = *tree_.node(h.var).type;
            h.caught = (t.isReference() ? t.referent()->unqualified() : t.unqualified())->id();
            const Type &c = T(h.caught);
            if (t.isReference() ? !referable(c) : (!scalar(c) || c.isPointer() || c.isX87(target_)))
                outside("a catch of '" + t.describe() + "'");
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

    place(p.block);
    const InstId cp = add(Op::CatchPad, TypeId(), std::vector<ValueId>());
    at(cp).syms = p.types;
    for (std::size_t i = 0; i < p.indices.size(); i++) at(cp).cases.push_back(p.indices[i]);
    at(cp).k = static_cast<long long>(p.own);
    at(cp).bits = (p.cleanup ? 1u : 0u) | (encAt != 0 ? 2u : 0u);
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
    resumeFrom(p, encAt);

    for (std::size_t i = 0; i < hs.size(); i++) {
        place(handlerBlocks[i]);
        const Val obj = runtimeCall("__cxa_begin_catch", voidPtr, std::vector<ValueId>(1, p.ptr));
        if (hs[i].var.valid()) {
            const Type &t = *tree_.node(hs[i].var).type;
            // By value, the scalar is copied out of the exception object; a reference holds what __cxa_begin_catch handed back.
            const TypeId objPtr = types_.pointerTo(hs[i].caught);
            const ValueId asT = result(add(Op::Bitcast, objPtr, std::vector<ValueId>(1, obj.v)));
            if (t.isReference()) {
                const TypeId slotType = tid(*types_.pointerTo(t.referent()));
                const ValueId slot = entryAlloca(slotType, text(hs[i].var));
                store(slot, result(add(Op::Bitcast, slotType, std::vector<ValueId>(1, asT))));
                locals_[tree_.node(hs[i].var).syntax.v] = std::make_pair(slot, tid(t));
            } else {
                const ValueId slot = entryAlloca(tid(t), text(hs[i].var));
                store(slot, load(asT, hs[i].caught));
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
            resumeFrom(cleanup, encAt);
        }
    }
    place(done);
}

// A jump out of n handlers: the catches ended, what the jump leaves destroyed, and the branch. On the Microsoft ABI a
// handler is a funclet, so it hands the runtime an exit block's address (a catchret, M3-IR 3.6) and the exit, in the
// body, destroys and branches on; the exit is listed on the try's CatchPad for the selector's flow.
void Lowering::leaveHandlers(std::size_t n, std::size_t mark, BlockId target, const std::vector<ValueId> &args) {
    if (!target_.microsoftNames() || n == 0) {
        endCatches(n);
        destroyLive(mark);
        br(target, args);
        return;
    }
    if (terminated()) return;
    const BlockId was = funclet_;
    funclet_ = BlockId();
    const BlockId exit = newBlock();
    funclet_ = was;
    Function &f = F();
    std::vector<ValueId> params;
    for (std::size_t i = 0; i < args.size(); i++) params.push_back(param(exit, f.value(args[i]).type));
    at(f.block(msTryPad_).insts[0]).cases.push_back(exit.v);
    Succ s;
    s.block = exit;
    s.first = static_cast<std::uint32_t>(f.operands.size());
    s.count = static_cast<std::uint32_t>(args.size());
    f.operands.insert(f.operands.end(), args.begin(), args.end());
    at(add(Op::CatchRet, TypeId(), std::vector<ValueId>())).succ.push_back(s);
    const BlockId back = cur_;
    place(exit);
    destroyLive(mark);
    br(target, params);
    place(back);
}

// A funclet runs on the parent's frame with none of its registers: a handler may read an alloca, made afresh there,
// and what its own blocks compute, and nothing else the body holds in a value.
void Lowering::checkFuncletUses(BlockId hb) {
    const Function &f = F();
    std::vector<char> inBlock(f.blockArena.size(), 0), inInst(f.insts.size(), 0);
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        const Block &blk = f.block(f.blocks[b]);
        if (f.blocks[b].v != hb.v && blk.funclet != hb.v) continue;
        inBlock[f.blocks[b].v] = 1;
        for (std::size_t k = 0; k < blk.insts.size(); k++) inInst[blk.insts[k].v] = 1;
        if (blk.term.valid()) inInst[blk.term.v] = 1;
    }
    for (std::size_t b = 0; b < f.blocks.size(); b++) {
        if (!inBlock[f.blocks[b].v]) continue;
        const Block &blk = f.block(f.blocks[b]);
        for (std::size_t k = 0; k <= blk.insts.size(); k++) {
            const InstId id = k < blk.insts.size() ? blk.insts[k] : blk.term;
            if (!id.valid()) continue;
            const Inst &i = f.inst(id);
            std::vector<ValueId> used;
            for (std::uint32_t n = 0; n < i.count; n++) used.push_back(f.operand(i, n));
            for (std::size_t s = 0; s < i.succ.size(); s++)
                for (std::uint32_t a = 0; a < i.succ[s].count; a++) used.push_back(f.arg(i.succ[s], a));
            for (std::size_t u = 0; u < used.size(); u++) {
                const Value &v = f.value(used[u]);
                const bool own = v.param.valid() ? inBlock[v.param.v] != 0
                               : !v.def.valid() || inInst[v.def.v] != 0 || f.inst(v.def).op == Op::Alloca;
                if (!own) outside("a handler reading a value of the body on the Microsoft ABI");
            }
        }
    }
}

// **A `try` on the Microsoft ABI** (M3-IR 3.6): the body's calls unwind to a dispatch pad - a CatchPad of k -1 whose `cases`
// list the exits, ended by a CatchSwitch over the handlers - and each handler is a pad of its own: a CatchPad naming the
// descriptor, the flags in `bits` (1 by reference, 2 const), the object's alloca, the try's pad in `k`; a CatchRet exits.
void Lowering::microsoftTry(sema::TypedId id) {
    if (handlerDepth_ != 0) outside("a try inside a handler on the Microsoft ABI");
    const std::uint32_t n = count(id);
    if (n < 2) outside("a try with no handler");
    struct Handler { sema::TypedId node, var, body; TypeId caught; std::string descriptor; bool byRef, isConst; };
    std::vector<Handler> hs;
    for (std::uint32_t i = 1; i < n; i++) {
        Handler h;
        h.node = child(id, i);
        h.byRef = h.isConst = false;
        if (syn(h.node).kind != syntax::Kind::Catch) outside("a try whose part is not a handler");
        const std::uint32_t parts = count(h.node);
        if (parts == 1 && text(h.node) == "...") {
            h.body = child(h.node, 0);
        } else if (parts == 2 && syn(child(h.node, 0)).kind == syntax::Kind::Var) {
            h.var = child(h.node, 0);
            h.body = child(h.node, 1);
            if (tree_.node(h.var).type == nullptr) outside("a caught object not typed");
            const Type &t = *tree_.node(h.var).type;
            h.byRef = t.isReference();
            h.caught = (h.byRef ? t.referent()->unqualified() : t.unqualified())->id();
            const Type &c = T(h.caught);
            // The runtime makes the object in the slot: a scalar's or a plain class's bytes, or a pointer to it.
            if (h.byRef ? !referable(c) : !(scalar(c) || plainClass(c)) || c.isX87(target_)) outside("a catch of '" + t.describe() + "'");
            if (!msCatch_ || !msCatch_(h.caught, h.descriptor, h.isConst)) outside("a catch of a type whose descriptor the parser did not finish");
        } else {
            outside(parts == 1 ? "a handler whose caught object the syntax tree does not hold" : "a handler of " + std::to_string(parts) + " parts");
        }
        hs.push_back(h);
    }
    // The enclosing pad's block before the try's, so its state precedes tryLow; the try's before the body's, so the
    // states made inside the body are tryLow..tryHigh, and the handlers', made after it, follow (the walker's numbering).
    const std::size_t encAt = pads_.size();
    if (encAt != 0) padBlock(pads_[encAt - 1]);
    Pad p;
    p.cleanup = false;
    p.own = n - 1;
    padBlock(p);
    const BlockId body = cur_;
    place(p.block);
    const InstId dispatch = add(Op::CatchPad, TypeId(), std::vector<ValueId>());
    at(dispatch).k = -1;
    place(body);
    const BlockId done = newBlock();
    at(dispatch).cases.push_back(done.v);
    pads_.push_back(p);
    block(child(id, 0));
    pads_.pop_back();
    br(done);
    std::vector<BlockId> handlerBlocks;
    for (std::size_t i = 0; i < hs.size(); i++) {
        handlerBlocks.push_back(newBlock());
        F().blockArena[handlerBlocks.back().v].pad = true;
    }
    place(p.block);
    const InstId cs = add(Op::CatchSwitch, TypeId(), std::vector<ValueId>());
    at(cs).k = encAt != 0 ? static_cast<long long>(pads_[encAt - 1].block.v) : 0;
    for (std::size_t i = 0; i < hs.size(); i++) {
        Succ s;
        s.block = handlerBlocks[i];
        at(cs).succ.push_back(s);
    }
    const BlockId wasTry = msTryPad_;
    msTryPad_ = p.block;
    for (std::size_t i = 0; i < hs.size(); i++) {
        place(handlerBlocks[i]);
        funclet_ = handlerBlocks[i];
        std::vector<ValueId> ops;
        if (hs[i].var.valid()) {
            const Type &t = *tree_.node(hs[i].var).type;
            const TypeId slotType = hs[i].byRef ? tid(*types_.pointerTo(t.referent())) : tid(t);
            const ValueId slot = entryAlloca(slotType, text(hs[i].var));
            locals_[tree_.node(hs[i].var).syntax.v] = std::make_pair(slot, tid(t));
            ops.push_back(slot);
        }
        const InstId cp = add(Op::CatchPad, TypeId(), ops);
        at(cp).syms.push_back(hs[i].descriptor);
        at(cp).bits = (hs[i].byRef ? 1u : 0u) | (hs[i].isConst ? 2u : 0u);
        at(cp).k = static_cast<long long>(p.block.v);
        at(cp).pos = syn(hs[i].node).loc;
        handlerDepth_++;
        block(hs[i].body);
        handlerDepth_--;
        if (!terminated()) {
            Succ s;
            s.block = done;
            at(add(Op::CatchRet, TypeId(), std::vector<ValueId>())).succ.push_back(s);
        }
        funclet_ = BlockId();
        checkFuncletUses(handlerBlocks[i]);
    }
    msTryPad_ = wasTry;
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
