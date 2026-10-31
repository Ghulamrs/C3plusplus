// ir::Lowering - see Lowering.h. The subset it reaches at M3b.1 is the scalar core of the adapter's: free functions
// over scalars, pointers, 1-D arrays of scalars and plain structs' scalar members, with every statement kind but
// `try`; a class object, a temporary, a member call, `this`, a static local or a throw leaves the function outside.

#include "Lowering.h"
#include "../Ast.h"

#include <cctype>
#include <cstring>

namespace ir {

Lowering::Lowering(const sema::TypedTree &tree, const syntax::Tree &syntax, TypeTable &types, const Target &target,
                   Program &program, Module &into)
    : tree_(tree), syntax_(syntax), types_(types), target_(target), program_(program), module_(into), fnIndex_(0), landingPads_(false), handlerDepth_(0), fullFrom_(kNoFull), skippable_(0), hasGoto_(false) {}

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
// `vptr`, one with a vptr a written constructor or destructor stores; `arrays`, members arrays of such classes; `vbases`,
// virtual bases and bases that have them, left to the most-derived object.
bool Lowering::buildableLayout(const Type &cls, bool vptr, bool arrays, bool vbases) const {
    if ((cls.polymorphic() && !vptr) || (cls.hasVirtualBase() && !vbases) || cls.kind() == Kind::Union) return false;
    for (const Type::BaseSpec &b : cls.bases()) {
        if (b.isVirtual && vbases) continue;     // not this function's to build or destroy
        if (b.isVirtual || !b.direct || !b.type->isComplete() || (b.type->unqualified()->hasVirtualBase() && !vbases) ||
            (b.type->unqualified()->polymorphic() && !vptr))
            return false;
    }
    long long elems = 0;
    for (const Member &m : cls.members()) {
        const Type &t = *m.type->unqualified();
        if (m.width != 0 || t.isX87(target_)) return false;
        if (m.type->isReference() ? !referable(*m.type->referent())
                                  : !scalar(t) && !plainClass(t) && !builtMember(t) && !scalarArray(t) && !(arrays && builtArray(t, elems).valid()))
            return false;
    }
    return true;
}

// An array, of any depth, of complete scalar elements: its bytes, built by nothing.
bool Lowering::scalarArray(const Type &t) {
    if (!t.isArray() || !t.pointee()->isComplete()) return false;
    const Type &e = *t.pointee()->unqualified();
    return scalar(e) || scalarArray(e);
}

// An array, of any depth, of a class builtMember takes - builtClass, as a `local` - its elements counted into `count`:
// its element class, or none.
TypeId Lowering::builtArray(const Type &t, long long &count, bool local) const {
    if (!t.isArray()) return TypeId();
    long long n = 1;
    TypeId e = tid(t);
    while (T(e).isArray()) {
        if (T(e).length() <= 0) return TypeId();
        n *= T(e).length();
        e = tid(*T(e).pointee()->unqualified());
    }
    if (local ? !builtClass(T(e)) : !builtMember(T(e))) return TypeId();
    count = n;
    return e;
}

// [class.base.init]/8 for an array member: each element, first to last, by the default constructor the parser chose;
// [except.ctor]/3, each element built is destroyed - the last built first - if a later element or part throws.
void Lowering::buildElements(const Member &m, const Type &et, long long elems) {
    const bool guarded = landingPads_ && et.hasDestructor();
    if (elems > (guarded ? 16 : 64)) outside("an array member of more elements than the lowering builds one by one");
    std::string symbol;
    TypeId sig;
    bool defaulted = false;
    if (!memberBuilt_ || !memberBuilt_(key_, m.name, 0, symbol, sig, defaulted)) {
        if (constructed_ && constructed_(et.id())) outside("an array member built by no constructor the parser recorded");
        return;
    }
    if (defaulted) outside("an array member built with default arguments");
    const Type &ft = T(sig);
    if (ft.params().size() != 1 || ft.params()[0]->pointee()->unqualified() != &et) outside("an array member's constructor of another class");
    for (long long k = 0; k < elems; k++) {
        const InstId g = add(Op::Gep, tid(*ft.params()[0]), std::vector<ValueId>(1, this_));
        at(g).k = m.offset + k * et.size(target_);
        directCall(symbol, sig, std::vector<ValueId>(1, result(g)));
        if (!guarded) continue;
        const std::string dtor = destructor_ ? destructor_(et.id()) : std::string();
        if (dtor.empty()) outside("an array member destroyed on unwinding by no destructor the parser recorded");
        guard(result(g), et, dtor);
    }
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
    if (!memberBuilt_ || !memberBuilt_(key_, m.name, pos, symbol, sig, defaulted)) {
        if (!entry.valid() && !(constructed_ && constructed_(mt.id()))) return;
        outside("a member built by no constructor the parser recorded");
    }
    if (defaulted) outside("a member built with default arguments");
    // A member the parser copied as bytes - one object of its type, the class having no copy constructor of its own.
    if (symbol == "$bytes") {
        if (!entry.valid() || count(entry) != 1 || !objectNode(child(entry, 0))) outside("a member copied from what is not an object");
        const Val src = object(child(entry, 0));
        if (T(src.type).unqualified() != &mt || T(sig).unqualified() != &mt) outside("a member copied from another type");
        copyObject(at, src.v, mt);
        return;
    }
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
        long long elems = 1;
        const TypeId array = builtArray(mt, elems);
        const Type &et = array.valid() ? T(array) : mt;
        if (ms[n].declaredIn != nullptr || !builtMember(et) || !destructor_) continue;
        const std::string symbol = destructor_(et.id());
        if (symbol.empty()) continue;
        if (elems > 64) outside("an array member of more elements than the lowering destroys one by one");
        // An array's elements in the reverse of the order they were built.
        const TypeId self = types_.pointerTo(et.id());
        for (long long k = elems; k-- > 0; ) {
            const InstId g = add(Op::Gep, self, std::vector<ValueId>(1, this_));
            at(g).k = ms[n].offset + k * et.size(target_);
            directCall(symbol, types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, self)),
                       std::vector<ValueId>(1, result(g)));
        }
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
    if (!memberBuilt_ || !memberBuilt_(key_, "^" + bt.tag(), 0, symbol, sig, defaulted)) {
        if (!entry.valid() && !(constructed_ && constructed_(bt.id()))) return;
        outside("a base built by no constructor the parser recorded");
    }
    if (defaulted) outside("a base built with default arguments");
    const Type &ft = T(sig);
    if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &bt) outside("a base's constructor of another class");
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, baseAddress(b)))));
    const std::size_t skip = baseVttArgument(bt, ft, ops);
    if (entry.valid()) arguments(entry, 0, ft, skip, false, ops);
    else if (ft.params().size() != skip) outside("a base's default constructor of parameters");
    // cl's most-derived flag, last, where the base's constructor takes one: 0, a subobject's ([class.base.init]/7).
    if (target_.microsoftNames() && msVbaseCtor_ && msVbaseCtor_(symbol)) {
        std::vector<TypeId> ps;
        for (const Type *p : ft.params()) ps.push_back(p->id());
        ps.push_back(types_.intType()->id());
        ops.push_back(iconst(types_.intType()->id(), 0));
        directCall(symbol, types_.functionType(types_.voidType()->id(), ps), ops);
        return;
    }
    directCall(symbol, sig, ops);
}

// cl's `if (mostDerived)` in front of a constructor of a class with virtual bases: the vbtable pointers the parser
// stored, then each virtual base it built, in its order, by the constructor it chose ([class.base.init]/10).
void Lowering::msVirtualBases(const Type &cls, const std::map<std::string, sema::TypedId> &named) {
    std::vector<sema::VptrStore> vbptrs;
    std::vector<std::string> order;
    if (!msVbaseInit_ || !msVbaseInit_(key_, vbptrs, order)) outside("a most-derived guard the parser recorded nothing of");
    // The guarded bases' cleanups ([except.ctor]/3) read their addresses and the count outside the guard, which they are not under.
    std::map<std::string, Live> guarded;
    ValueId count;
    for (std::size_t b = 0; b < cls.bases().size(); b++)
        if (landingPads_ && cls.bases()[b].isVirtual && cls.bases()[b].type->unqualified()->hasDestructor()) {
            if (!count.valid()) {
                count = entryAlloca(types_.intType()->id(), "$vb");
                store(count, iconst(types_.intType()->id(), 0));
            }
            guarded[cls.bases()[b].type->unqualified()->tag()] = msGuardedVirtualBase(cls.bases()[b], count);
        }
    const BlockId then = newBlock(), join = newBlock();
    condbr(truth(Val(msFlag_, types_.intType()->id())).v, then, join);
    place(then);
    long long built = 0;
    const TypeId word = types_.pointerTo(types_.voidType()->id());
    for (const sema::VptrStore &s : vbptrs) {
        const InstId g = add(Op::Gep, types_.pointerTo(word), std::vector<ValueId>(1, this_));
        at(g).k = s.at;
        const InstId a = add(Op::Addr, word, std::vector<ValueId>());
        at(a).sym = s.symbol;
        store(result(g), result(a));
    }
    for (const std::string &tag : order) {
        int which = -1;
        for (std::size_t b = 0; b < cls.bases().size(); b++)
            if (cls.bases()[b].isVirtual && cls.bases()[b].type->unqualified()->tag() == tag) which = static_cast<int>(b);
        if (which < 0) outside("a virtual base the class does not lay out");
        sema::TypedId entry;
        for (std::map<std::string, sema::TypedId>::const_iterator e = named.begin(); e != named.end(); ++e)
            if (cls.findMember(e->first) == nullptr && baseNamed(cls, e->first) == which) entry = e->second;
        buildBase(cls.bases()[which], entry);
        const std::map<std::string, Live>::const_iterator g = guarded.find(tag);
        if (g == guarded.end()) continue;
        // Built: alive to the cleanups, which destroy it only where the count says so - the bases built so far, one more.
        guard(g->second.slot, T(g->second.cls), g->second.dtor);
        live_.back().when = count;
        live_.back().above = built++;
        store(count, iconst(types_.intType()->id(), built));
    }
    br(join);
    place(join);
}

// [except.ctor]/3 for a virtual base cl's flag guards: destroyed if a later part throws, only where this constructor
// built it - the walker's `$vb` count, 0 before the guard and the bases built so far after each; its cleanup asks it.
// The destructor is the base's own, as the walker's baseDestructorCall names it, on the address the layout gives it.
Lowering::Live Lowering::msGuardedVirtualBase(const Type::BaseSpec &b, ValueId count) {
    const Type &bt = *b.type->unqualified();
    if (bt.hasVirtualBase()) outside("a virtual base with virtual bases destroyed on unwinding on the Microsoft ABI");
    Live l;
    l.dtor = destructor_ ? destructor_(bt.id()) : std::string();
    if (l.dtor.empty()) outside("a virtual base destroyed on unwinding whose destructor the parser recorded none for");
    l.cls = bt.id();
    l.when = count;
    l.slot = baseAddress(b);
    return l;
}

// [2.6.2]: a base with virtual bases of its own is handed its sub-VTT, `vtt + n` in this C2's or D2's - the entry
// the parser chose for it - as its second argument. What the ops take before the written arguments: 1, or 2.
std::size_t Lowering::baseVttArgument(const Type &base, const Type &ft, std::vector<ValueId> &ops) {
    const int entry = baseVtt_ ? baseVtt_(key_, base.tag()) : -1;
    if (entry < 0 && base.hasVirtualBase() && !target_.microsoftNames()) outside("a base handed a VTT the parser recorded no entry for");
    if (entry < 0) return 1;
    if (!vtt_.valid() || ft.params().size() < 2) outside("a base handed a VTT where this function has none");
    ops.push_back(vttAt(entry));
    return 2;
}

// Entry `entry` of this function's VTT, as the address a sub-VTT is: `vtt + entry` words.
ValueId Lowering::vttAt(int entry) {
    const InstId g = add(Op::Gep, F().value(vtt_).type, std::vector<ValueId>(1, vtt_));
    at(g).k = static_cast<long long>(entry) * T(types_.pointerTo(types_.voidType()->id())).size(target_);
    return result(g);
}

// [class.dtor]/8: last of all, the bases by their base-object destructors - Itanium's D2 - in the reverse order.
void Lowering::destroyBases(const Type &cls) {
    for (std::size_t n = cls.bases().size(); n-- > 0; ) {
        const Type::BaseSpec &b = cls.bases()[n];
        if (b.isVirtual) continue;     // the most-derived object's to destroy, never a base-object destructor's
        const Type &bt = *b.type->unqualified();
        std::string symbol;
        TypeId sig;
        bool defaulted = false;
        if (!memberBuilt_ || !memberBuilt_(key_, "~" + bt.tag(), 0, symbol, sig, defaulted)) {
            if (destructor_ && !destructor_(bt.id()).empty()) outside("a base destroyed by no destructor the parser recorded");
            continue;
        }
        const Type &ft = T(sig);
        if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &bt) outside("a base's destructor of another class");
        std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, baseAddress(b)))));
        if (ft.params().size() != baseVttArgument(bt, ft, ops)) outside("a base's destructor of another class");
        directCall(symbol, sig, ops);
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
    if (msFlag_.valid()) msVirtualBases(cls, named);
    // [class.base.init]/10: the bases first, in the order the class names them, each by the constructor the parser chose.
    for (std::size_t b = 0; b < cls.bases().size(); b++) {
        if (cls.bases()[b].isVirtual && (vtt_.valid() || msFlag_.valid())) continue;     // C1's, or the guard's, never here
        sema::TypedId entry;
        for (std::map<std::string, sema::TypedId>::const_iterator e = named.begin(); e != named.end(); ++e)
            if (cls.findMember(e->first) == nullptr && baseNamed(cls, e->first) == static_cast<int>(b)) entry = e->second;
        const std::size_t was = openFull();     // [class.base.init]/7: each entry a full expression of its own
        buildBase(cls.bases()[b], entry);
        closeFull(was);
        // [except.ctor]/3: a base with a destructor, built, is destroyed - by its D2 - if a later part throws.
        const Type &bt = *cls.bases()[b].type->unqualified();
        if (landingPads_ && bt.hasDestructor()) {
            std::string symbol;
            TypeId sig;
            bool defaulted = false;
            if (!memberBuilt_ || !memberBuilt_(key_, "~" + bt.tag(), 0, symbol, sig, defaulted))
                outside("a base destroyed on unwinding by no destructor the parser recorded");
            std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*T(sig).params()[0]), std::vector<ValueId>(1, baseAddress(cls.bases()[b])))));
            if (T(sig).params().size() != baseVttArgument(bt, T(sig), ops)) outside("a base's destructor of another class");
            guard(ops[0], bt, symbol);
            if (ops.size() == 2) live_.back().vtt = ops[1];     // its D2, handed the same sub-VTT
        }
    }
    // [class.cdtor]/4: once the bases are built the object is of this class - its vptrs, before any member.
    storeVptrs();
    for (const Member &m : cls.members()) {
        if (m.declaredIn != nullptr) continue;     // a base's, which its constructor built
        std::map<std::string, sema::TypedId>::const_iterator e = named.find(m.name);
        const Type &mt = *m.type->unqualified();
        long long elems = 0;
        const TypeId array = builtArray(mt, elems);
        if (array.valid()) {
            if (e != named.end() || (defaultInit_ && defaultInit_(cls.id(), m.name))) outside("an array member given an initialiser");
            buildElements(m, T(array), elems);
            continue;
        }
        if (e == named.end() && defaultInit_ && defaultInit_(cls.id(), m.name)) {
            // [class.base.init]/9: the member's own initialiser, which the parser replayed as this constructor's entry.
            const sema::TypedId d = memberDefault_ ? memberDefault_(key_, cls.tag() + "::" + m.name) : sema::TypedId();
            if (!d.valid() || m.type->isReference() || builtMember(mt)) outside("a member's default initialiser");
            e = named.insert(std::make_pair(m.name, d)).first;
        } else if (e == named.end() && !builtMember(mt)) {
            continue;
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
            const std::size_t was = openFull();
            buildMember(result(g), m, e != named.end() ? e->second : sema::TypedId());
            closeFull(was);
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
            const std::size_t was = openFull();
            store(result(g), exprTo(child(e->second, 0), tid(mt)).v);
            closeFull(was);
        } else if (k == 1 && plainClass(mt) && objectNode(child(e->second, 0))) {
            const Val src = object(child(e->second, 0));
            if (T(src.type).unqualified() != &mt) outside("a member built from another type");
            copyObject(result(g), src.v, mt);
        } else {
            outside("a mem-initialiser of " + std::to_string(k) + " values for '" + m.name + "'");
        }
    }
}

// The vptrs the parser stores in this constructor or destructor: each the address point of a table, at its offset -
// or, in Itanium's C2 or D2, the VTT's entry, into a word below a virtual base found through the vptr stored first.
void Lowering::storeVptrs() {
    const TypeId word = types_.pointerTo(types_.voidType()->id());
    for (const sema::VptrStore &s : vptrs_) {
        ValueId where;
        if (s.viaVBase) {
            // `this` + the vbase_offset `vbaseSlot` bytes from the vptr at `this`, + `fromVBase`: clang's getVTablePointers.
            const TypeId chars = types_.pointerTo(types_.charType()->id());
            const ValueId vptr = load(result(add(Op::Bitcast, types_.pointerTo(chars), std::vector<ValueId>(1, this_))), chars);
            const InstId slot = add(Op::Gep, types_.pointerTo(ptrdiffType()), std::vector<ValueId>(1, vptr));
            at(slot).k = s.vbaseSlot;
            std::vector<ValueId> moved(1, result(add(Op::Bitcast, chars, std::vector<ValueId>(1, this_))));
            moved.push_back(load(result(slot), ptrdiffType()));
            const InstId g = add(Op::Gep, types_.pointerTo(word), std::vector<ValueId>(1, result(add(Op::Gep, chars, moved))));
            at(g).k = s.fromVBase;
            where = result(g);
        } else {
            const InstId g = add(Op::Gep, types_.pointerTo(word), std::vector<ValueId>(1, this_));
            at(g).k = s.at;
            where = result(g);
        }
        if (s.fromVtt) {
            if (!vtt_.valid()) outside("a vptr out of a VTT this function is not handed");
            store(where, load(result(add(Op::Bitcast, types_.pointerTo(word), std::vector<ValueId>(1, vttAt(s.vttEntry)))), word));
            continue;
        }
        if (s.symbol.empty()) outside("a vptr out of no table the parser named");
        const InstId a = add(Op::Addr, word, std::vector<ValueId>());
        at(a).sym = s.symbol;
        at(a).k = s.addend;
        store(where, result(a));
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
        Val o = object(child(var, 0));
        // [class.copy]/12 with [conv.ptr]/3: a derived object's base subobject is what is copied - its bytes.
        if (T(o.type).isStructOrUnion() && T(o.type).unqualified() != t.unqualified())
            o = baseSubobject(o, t, "a class local initialised from another type");
        if (T(o.type).unqualified() != t.unqualified()) outside("a class local initialised from another type");
        copyObject(slot, o.v, t);
        return;
    }
    default:
        if (listFits(t, v.count)) { listInit(slot, t, var, 0); return; }
        std::vector<sema::TypedId> parts;
        for (std::uint32_t i = 0; i < v.count; i++) parts.push_back(child(var, i));
        aggregateInit(slot, t, parts);
        return;
    }
}

// [dcl.init.aggr] for a plain class from a list the flat one does not fit: zeroed, then each value placed in order -
// a `{}` node initialising the member it reaches whole, a value reaching into a member that is an aggregate (brace
// elision, [dcl.init.aggr]/11) - as the parser's emitAggregate places them; a value past the last member is refused.
void Lowering::aggregateInit(ValueId slot, const Type &t, const std::vector<sema::TypedId> &parts) {
    const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
    at(z).k = t.size(target_);
    std::size_t next = 0;
    aggregateFill(slot, *t.unqualified(), parts, next);
    if (next != parts.size()) outside("a braced list with values past its aggregate");
}

// The members of a class, or the elements of an array, at `slot`, each from the cursor while values are left:
// a scalar or a scoped enumeration stored, a plain class or a scalar array filled in turn. The forms the parser
// binds or groups - a reference, a bit-field, an anonymous union, a union, a base - are refused by name.
void Lowering::aggregateFill(ValueId slot, const Type &t, const std::vector<sema::TypedId> &parts, std::size_t &next) {
    if (t.isArray()) {
        if (!scalarArray(t) || t.length() <= 0) outside("a braced list reaching an array of '" + t.pointee()->describe() + "'");
        const Type &et = *t.pointee()->unqualified();
        const TypeId ep = tid(*types_.pointerTo(&et));
        const ValueId base = result(add(Op::Bitcast, ep, std::vector<ValueId>(1, slot)));
        for (long long i = 0; i < t.length() && next < parts.size(); i++) {
            const InstId g = add(Op::Gep, ep, std::vector<ValueId>(1, base));
            at(g).k = i * et.size(target_);
            aggregateItem(result(g), et, parts, next);
        }
        return;
    }
    if (!plainClass(t) || t.kind() == Kind::Union || !t.bases().empty()) outside("a braced list reaching a '" + t.describe() + "'");
    for (const Member &m : t.members()) {
        if (next >= parts.size()) return;
        if (m.name.empty() || m.width != 0 || m.anonUnion != nullptr || m.type->isReference() || m.type->isX87(target_))
            outside("a braced list reaching a member the lowering does not place");
        const Type &mt = *m.type->unqualified();
        const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, slot));
        at(g).k = m.offset;
        aggregateItem(result(g), mt, parts, next);
    }
}

// One member or element from the cursor: `{}` takes the next value as a list of its own - empty, it leaves the zero;
// a scalar's `{v}` is v ([dcl.init.list]/3) - and any other value is stored in a scalar or elided into an aggregate.
void Lowering::aggregateItem(ValueId at0, const Type &mt, const std::vector<sema::TypedId> &parts, std::size_t &next) {
    const sema::TypedId p = parts[next];
    const bool leaf = scalar(mt) || mt.isScopedEnumeration();
    if (syn(p).kind == syntax::Kind::Other && text(p) == "{}") {
        next++;
        std::vector<sema::TypedId> inner;
        for (std::uint32_t i = 0; i < count(p); i++) inner.push_back(child(p, i));
        if (inner.empty()) return;
        if (leaf) {
            if (inner.size() != 1) outside("a scalar's braces holding more than one value");
            store(at0, exprTo(inner[0], tid(mt)).v);
            return;
        }
        std::size_t k = 0;
        aggregateFill(at0, mt, inner, k);
        if (k != inner.size()) outside("a braced list with values past its aggregate");
        return;
    }
    if (leaf) {
        store(at0, exprTo(p, tid(mt)).v);
        next++;
        return;
    }
    if (syn(p).kind == syntax::Kind::StringLit) outside("a string inside a braced list");
    aggregateFill(at0, mt, parts, next);     // brace elision: the value reaches into the member
}

// [dcl.init.aggr]/2 for a list a plain class takes member by member: no base, no more values than members, every
// member a plain scalar or a scoped enumeration - so no value reaches into an array or a nested class, which the
// flattened list cannot say.
bool Lowering::listFits(const Type &t, std::uint32_t values) const {
    const std::vector<Member> &ms = t.unqualified()->members();
    if (!t.unqualified()->bases().empty() || values > ms.size()) return false;
    for (std::size_t i = 0; i < ms.size(); i++)
        if (!(scalar(*ms[i].type) || ms[i].type->isScopedEnumeration()) || ms[i].width != 0 || ms[i].anonUnion != nullptr || ms[i].type->isX87(target_)) return false;
    return true;
}

// Such a list into `slot`: zeroed, then each value stored in its member in order - the values node `list`'s parts from `from`.
void Lowering::listInit(ValueId slot, const Type &t, sema::TypedId list, std::uint32_t from) {
    const std::vector<Member> &ms = t.unqualified()->members();
    const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
    at(z).k = t.size(target_);
    for (std::uint32_t i = from; i < count(list); i++) {
        const TypeId mt = tid(*ms[i - from].type->unqualified());
        const InstId g = add(Op::Gep, tid(*types_.pointerTo(&T(mt))), std::vector<ValueId>(1, slot));
        at(g).k = ms[i - from].offset;
        store(result(g), exprTo(child(list, i), mt).v);
    }
}

// A class whose local a constructor builds or a destructor destroys, the lowering calling both: no vptr, no base, the
// layout simpleLayout or buildableLayout takes - the constructors and destructors the lowering itself reaches (M3b.2).
bool Lowering::builtClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete() || plainClass(t)) return false;
    const Type &c = *t.unqualified();
    return c.kind() != Kind::Union && (simpleLayout(c) || buildableLayout(c, false, true));
}

// A local of a class builtClass does not take - a vptr, a polymorphic base, a member of either - built and destroyed by
// the calls the parser recorded as builtLocal's is: its layout is its constructors' business. A virtual base stays outside.
bool Lowering::heldClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete() || plainClass(t) || builtClass(t)) return false;
    const Type &c = *t.unqualified();
    return c.kind() != Kind::Union && !c.hasVirtualBase() && constructed_ && constructed_(c.id());
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
        const Type &ft = T(sig);
        // [dcl.init.list]/3: braces reach a constructor as parentheses do, one value per parameter, unless a parameter
        // is the initializer_list the parser built a backing array for.
        if (v.initForm == 2)
            for (std::size_t i = 1; i < ft.params().size(); i++) {
                const Type &p = ft.params()[i]->isReference() ? *ft.params()[i]->referent()->unqualified() : *ft.params()[i]->unqualified();
                if (p.isStructOrUnion() && p.tag().compare(0, 22, "std::initializer_list<") == 0) outside("a local built from a braced list");
            }
        if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &c) outside("a local's constructor of another class");
        if (zeroed) {
            const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
            at(z).k = c.size(target_);
        }
        std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, slot))));
        // [class.temporary]/4: the arguments' temporaries are destroyed at the declarator's end, before the local is alive.
        const std::size_t was = openFull();
        if (v.count != 0) arguments(var, 0, ft, 1, false, ops);
        else if (ft.params().size() != 1) outside("a local's default constructor of parameters");
        directCall(symbol, sig, ops);
        closeFull(was);
    } else if (elidedLocal(var)) {
        elidedCall(child(var, 0), slot);
    } else if (v.initForm == 1 && v.count == 1 && temporaryNode(child(var, 0)) && !plainTemporary(child(var, 0))) {
        // `T x = T(...)` copied, not built in place: the temporary's bytes, the temporary destroyed at the full expression's end.
        const std::size_t was = openFull();
        const Val o = temporary(child(var, 0));
        if (T(o.type).unqualified() != &c) outside("a class local initialised from a temporary of another type");
        copyObject(slot, o.v, c);
        closeFull(was);
    } else {
        // No constructor ran: a copy of bytes, which a class whose copy is a constructor would have recorded as a call.
        if (constructed_ && constructed_(c.id()) && v.initForm != 1) outside("a local of a constructed class built by no constructor");
        classLocal(var, slot, t);
    }
    if (!c.hasDestructor()) return;
    Live l;
    l.dtor = destructor_ ? destructor_(c.id()) : std::string();
    if (l.dtor.empty()) outside("a local whose destructor the parser recorded none for");
    guard(slot, c, l.dtor);
}

// `T x = f();` where the parser built f's result straight into x ([class.copy]/31): a call of f or an operator.
bool Lowering::elidedLocal(sema::TypedId var) const {
    const sema::TypedNode &v = tree_.node(var);
    if (v.initForm != 1 || v.count != 1 || !elidedInto_) return false;
    const sema::TypedId c = child(var, 0);
    if (syn(c).kind != syntax::Kind::Call && !operatorCalls(c)) return false;
    const sema::Resolution &r = tree_.resolution(c);
    return r.what == sema::Resolution::Call && r.type.valid() && T(r.type).isFunction() &&
           T(r.type).returns()->unqualified() == v.type->unqualified() && elidedInto_(v.syntax, tree_.node(c).syntax);
}

// That call, its result coming back into the local's alloca - the local's guard, not its full expression, destroys it;
// the arguments' temporaries are destroyed at the full expression's end, before the local is alive.
void Lowering::elidedCall(sema::TypedId c, ValueId slot) {
    const std::size_t was = openFull();
    intoCall_ = c;
    intoAlloca_ = static_cast<int>(F().inst(F().values[slot.v].def).k);
    const Val r = operatorCalls(c) ? operatorCall(c) : call(c);
    intoCall_ = sema::TypedId();
    intoAlloca_ = -1;
    if (!r.v.valid() || F().calls.empty() || F().calls.back().resultAlloca != static_cast<int>(F().inst(F().values[slot.v].def).k))
        outside("an elided call that did not come back into its local");
    closeFull(was);
}

// An object alive from here: destroyed by `dtor` at its scope's end, and by a cleanup where a call in its region unwinds -
// unless `unwinds` is false: a call's result in a function the parser gave no landing pads, which its unwinding leaves be.
void Lowering::guard(ValueId slot, const Type &c, const std::string &dtor, bool unwinds) {
    Live l;
    l.slot = slot;
    l.cls = c.id();
    l.dtor = dtor;
    if (!unwinds) {
        l.pad = kNoFull;
        live_.push_back(l);
        return;
    }
    Pad p;
    p.cleanup = true;
    p.own = 0;
    if (!pads_.empty()) { p.types = pads_.back().types; p.indices = pads_.back().indices; }
    l.pad = pads_.size();
    pads_.push_back(p);
    live_.push_back(l);
}

// [stmt.dcl]/4: a static local built the first time control passes, under the guard guardOnce named - a byte read, then
// __cxa_guard_acquire and _release on Itanium; _Init_thread_header and _footer with the guard at -1 on Microsoft. A throw
// out of the construction aborts the guard, as the parser's cleanup does, so the next pass tries again.
void Lowering::guardedStatic(sema::TypedId var, const std::string &symbol) {
    const sema::TypedNode &v = tree_.node(var);
    sema::StaticGuard g;
    if (!staticGuard_ || !staticGuard_(symbol, g)) outside("a static local built at run time with no guard recorded");
    if (v.type->isArray()) outside("a static array built at run time");
    const bool ms = target_.microsoftNames();
    if (ms && handlerDepth_ != 0) outside("a static local built at run time inside a handler on the Microsoft ABI");
    staticLocals_[v.syntax.v] = std::make_pair(symbol, tid(*v.type));
    const TypeId gt = tid(*types_.get(ms ? Kind::Int : Kind::LongLong)), gp = tid(*types_.pointerTo(&T(gt)));
    const TypeId intT = tid(*types_.get(Kind::Int)), boolT = tid(*types_.get(Kind::Bool)), voidT = tid(*types_.voidType());
    const BlockId build = newBlock(), done = newBlock();
    std::vector<ValueId> ops;
    if (!ms) {
        const TypeId byte = tid(*types_.get(Kind::UChar));
        ops.push_back(promote(Val(load(addrOf(g.guard, tid(*types_.pointerTo(&T(byte)))), byte), byte)).v);
        ops.push_back(iconst(intT, 0));
        const BlockId acquire = newBlock();
        const InstId fresh = add(Op::ICmp, boolT, ops);
        at(fresh).cc = Cmp::Eq;
        condbr(result(fresh), acquire, done);
        place(acquire);
        condbr(truth(runtimeCall("__cxa_guard_acquire", intT, std::vector<ValueId>(1, addrOf(g.guard, gp)))).v, build, done);
    } else {
        runtimeCall("_Init_thread_header", voidT, std::vector<ValueId>(1, addrOf(g.guard, gp)));
        ops.push_back(load(addrOf(g.guard, gp), gt));
        ops.push_back(iconst(gt, -1));
        const InstId unbuilt = add(Op::ICmp, boolT, ops);
        at(unbuilt).cc = Cmp::Eq;
        condbr(result(unbuilt), build, done);
    }
    place(build);
    // The abort's region: a cleanup pad of its own, under every pad the construction makes.
    Pad p;
    p.cleanup = true;
    p.own = 0;
    if (!pads_.empty()) { p.types = pads_.back().types; p.indices = pads_.back().indices; }
    const std::size_t mark = pads_.size(), liveMark = live_.size();
    if (g.aborts) pads_.push_back(p);
    staticConstruction(var, symbol, g);
    if (live_.size() != liveMark || pads_.size() != mark + (g.aborts ? 1 : 0)) outside("a static local whose construction left a scope open");
    if (g.aborts) {
        p = pads_.back();
        pads_.pop_back();
    }
    if (g.aborts && p.block.valid()) {
        if (!landingPads_) outside("a static local's abort in a function the parser gave no landing pads");
        const BlockId back = cur_;
        place(p.block);
        const InstId cl = add(Op::CleanupPad, TypeId(), std::vector<ValueId>());
        at(cl).syms = p.types;
        for (std::size_t k = 0; k < p.indices.size(); k++) at(cl).cases.push_back(p.indices[k]);
        at(cl).bits = (p.types.empty() ? 0u : 1u) | (!pads_.empty() ? 2u : 0u);
        runtimeCall(ms ? "_Init_thread_abort" : "__cxa_guard_abort", voidT, std::vector<ValueId>(1, addrOf(g.guard, gp)));
        if (ms) {
            const InstId r = add(Op::CleanupRet, TypeId(), std::vector<ValueId>());
            if (!pads_.empty()) {
                Succ s;
                s.block = padBlock(pads_.back());
                s.first = static_cast<std::uint32_t>(F().operands.size());
                s.count = 2;
                F().operands.push_back(p.ptr);
                F().operands.push_back(p.sel);
                at(r).succ.push_back(s);
            }
        } else {
            resumeFrom(p, pads_.size());
        }
        place(back);
    }
    runtimeCall(ms ? "_Init_thread_footer" : "__cxa_guard_release", voidT, std::vector<ValueId>(1, addrOf(g.guard, gp)));
    br(done);
    place(done);
}

// What guardOnce guards: emitInit's store of a scalar or a plain class's bytes; or the constructor the parser chose, its
// temporaries destroyed after it, then the destructor registered - __cxa_atexit with D1 on Itanium, atexit(helper) on Microsoft.
void Lowering::staticConstruction(sema::TypedId var, const std::string &symbol, const sema::StaticGuard &g) {
    const sema::TypedNode &v = tree_.node(var);
    const Type &c = *v.type->unqualified();
    const std::size_t was = openFull();
    // bindStaticReference's binding: the global holds the address of the lvalue, as a local reference's alloca would.
    if (c.isReference()) {
        if (v.count != 1) outside("a static reference of " + std::to_string(v.count) + " parts");
        Val b = lvalue(child(var, 0));
        if (T(b.type).isStructOrUnion() && c.referent()->isStructOrUnion() && T(b.type).unqualified() != c.referent()->unqualified())
            b = baseOfObject(b, c);
        if (T(b.type).unqualified() != c.referent()->unqualified()) outside("a static reference bound to another type");
        if (T(b.type).isConst() && !c.referent()->isConst()) outside("a static reference that can write bound to a const");
        const TypeId slotType = tid(*types_.pointerTo(c.referent()));
        const ValueId slot = addrOf(symbol, tid(*types_.pointerTo(&T(slotType))));   // before the bitcast, whatever the host's compiler
        store(slot, result(add(Op::Bitcast, slotType, std::vector<ValueId>(1, b.v))));
        closeFull(was);
        return;
    }
    const ValueId obj = addrOf(symbol, tid(*types_.pointerTo(&c)));
    if (g.built && !g.copied) {
        if (g.list) outside("a static local built from a braced list");
        if (g.ctor.empty() || !g.ctorSig.valid()) outside("a static local built by no constructor the parser recorded");
        if (g.defaulted) outside("a static local built with default arguments");
        const Type &ft = T(g.ctorSig);
        if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &c) outside("a static local's constructor of another class");
        if (g.zeroed) {
            const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, obj));
            at(z).k = c.size(target_);
        }
        std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, obj))));
        if (v.count != 0) arguments(var, 0, ft, 1, false, ops);
        else if (ft.params().size() != 1) outside("a static local's default constructor of parameters");
        directCall(g.ctor, g.ctorSig, ops);
    } else if (c.isStructOrUnion()) {
        if (v.count != 1) outside("a static class of " + std::to_string(v.count) + " parts");
        const Val o = classValue(child(var, 0), c);
        copyObject(obj, o.v, c);
    } else {
        if ((!scalar(c) && !c.isNullPtr()) || c.isX87(target_)) outside("a static local of type '" + c.describe() + "'");
        if (v.count != 1) outside("a static local of " + std::to_string(v.count) + " parts");
        store(obj, exprTo(child(var, 0), tid(c)).v);
    }
    closeFull(was);
    if (!c.isStructOrUnion() || !c.hasDestructor()) return;
    if (!g.registered) outside("a static local whose destructor the parser registered none for");
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType())), intT = tid(*types_.get(Kind::Int));
    std::vector<ValueId> args;
    if (target_.microsoftNames()) {
        if (g.helper.empty()) outside("a static local with no atexit helper recorded");
        args.push_back(addrOf(g.helper, types_.pointerTo(types_.functionType(types_.voidType()->id(), std::vector<TypeId>()))));
        runtimeCall("atexit", intT, args);
        return;
    }
    const std::string dtor = destructor_ ? destructor_(c.id()) : std::string();
    if (dtor.empty()) outside("a static local whose destructor the parser recorded none for");
    args.push_back(addrOf(dtor, types_.pointerTo(types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, types_.pointerTo(c.id()))))));
    args.push_back(result(add(Op::Bitcast, voidPtr, std::vector<ValueId>(1, obj))));
    args.push_back(addrOf("__dso_handle", voidPtr));
    runtimeCall("__cxa_atexit", intT, args);
}

// The destructor called on the object: a call on the normal path, as the walker's is; an invoke in a cleanup's terminate scope.
void Lowering::destroy(const Live &l, bool invoke) {
    std::vector<TypeId> params(1, types_.pointerTo(l.cls));
    std::vector<ValueId> ops(1, l.slot);
    if (l.vtt.valid()) { params.push_back(F().value(l.vtt).type); ops.push_back(l.vtt); }     // a base's D2, handed its sub-VTT
    CallInfo info;
    info.symbol = l.dtor;
    info.signature = types_.functionType(types_.voidType()->id(), params);
    info.namedArgs = static_cast<int>(params.size());
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    at(invoke ? callOrInvoke(TypeId(), ops) : add(Op::Call, TypeId(), ops)).call = callIndex;
}

// A jump out of the scopes above `mark`: their objects destroyed, last built first; they stay alive on the path not taken.
void Lowering::destroyLive(std::size_t mark) {
    for (std::size_t k = live_.size(); k-- > mark; )
        if (live_[k].slot.v != elidedSlot_.v) destroy(live_[k]);     // the named return value a `return` hands back
}

// A scope's end: each object it built destroyed on the way out, last built first, and its cleanup - made where a call in
// its region could unwind - destroying it and handing on to the enclosing pad, or out of the frame. `keep`: a
// constructor's own parts, which its normal way out leaves built and only its cleanups destroy ([except.ctor]/3).
void Lowering::closeScope(std::size_t mark, bool keep) {
    while (live_.size() > mark) {
        const Live l = live_.back();
        live_.pop_back();
        if (l.pad == kNoFull) { if (!terminated() && !keep) destroy(l); continue; }
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
            if (l.when.valid()) msCountedDestroy(l, p.block); else destroy(l);
            const InstId r = add(Op::CleanupRet, TypeId(), std::vector<ValueId>());
            const BlockId to = msEnclosingPad();
            if (to.valid()) {
                // A handler's block takes no arguments; a pad's takes the pointer and the selector.
                std::vector<ValueId> args;
                if (F().block(to).params.size() == 2) { args.push_back(p.ptr); args.push_back(p.sel); }
                Succ s;
                s.block = to;
                s.first = static_cast<std::uint32_t>(F().operands.size());
                s.count = static_cast<std::uint32_t>(args.size());
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
// built; or `P{...}`, a call `{}` the parser records only where no value is a list, of values listFits takes.
bool Lowering::plainTemporary(sema::TypedId id) const {
    const bool list = text(id) == "{}";
    if (syn(id).kind != syntax::Kind::Call || count(id) == 0 || (count(id) > 2 && !list)) return false;
    const sema::Resolution &r = tree_.resolution(id);
    const TypeId t = TypeTable::id(tree_.node(id).type);
    return r.what == sema::Resolution::Temporary && r.symbol.empty() && t.valid() && plainClass(T(t)) &&
           tree_.resolution(child(id, 0)).what == sema::Resolution::TypeName && (!list || listFits(T(t), count(id) - 1));
}

// A class with constructors the lowering can build on storage it has: buildableLayout's, with no destructor to run
// at a temporary's end and no virtual base - the constructor the parser chose called on the address, nothing else.
bool Lowering::constructedClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete()) return false;
    const Type &c = *t.unqualified();
    return constructed_ && constructed_(c.id()) && !c.hasDestructor() && !c.hasVirtualBase() && buildableLayout(c);
}

// [expr.new] of a class a constructor builds: the one the parser chose called on the storage, which the class's layout - a
// vptr, a destructor the `new` never runs - does not enter into; a virtual base's most-derived flag stays outside.
bool Lowering::newBuilt(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete()) return false;
    const Type &c = *t.unqualified();
    return constructed_ && constructed_(c.id()) && !c.hasVirtualBase() && c.kind() != Kind::Union;
}

// constructedClass's layout with a destructor besides, which the end of the temporary's full expression runs.
bool Lowering::destroyedClass(const Type &t) const {
    if (!t.isStructOrUnion() || !t.isComplete()) return false;
    const Type &c = *t.unqualified();
    return constructed_ && constructed_(c.id()) && c.hasDestructor() && !c.hasVirtualBase() && c.kind() != Kind::Union &&
           buildableLayout(c);
}

// A class temporary a constructor built, `*(ctor(&$tmp, ...), &$tmp)`: `T(...)`, a call whose head names the type, its
// arguments from child 1; or `(T)v` and `static_cast<T>(v)` through T's converting constructor, a cast of one operand.
bool Lowering::temporaryShape(sema::TypedId id, std::uint32_t &from) const {
    const bool cast = syn(id).kind == syntax::Kind::Cast;
    if (tree_.resolution(id).what != sema::Resolution::Temporary || !temporaryBuilt_) return false;
    if (cast ? count(id) != 1 : syn(id).kind != syntax::Kind::Call || count(id) == 0 ||
                                tree_.resolution(child(id, 0)).what != sema::Resolution::TypeName)
        return false;
    from = cast ? 0 : 1;
    return true;
}

// Such a temporary of a class the lowering builds, the constructor the parser chose recorded for the node.
bool Lowering::builtTemporary(sema::TypedId id, bool destroyed) const {
    std::uint32_t from = 0;
    const TypeId t = TypeTable::id(tree_.node(id).type);
    if (!temporaryShape(id, from) || !t.valid() || !(destroyed ? destroyedClass(T(t)) : constructedClass(T(t)))) return false;
    std::string symbol;
    TypeId sig;
    bool zero;
    if (!temporaryBuilt_(tree_.node(id).syntax, symbol, sig, zero)) return false;
    // `T{a, b}` reaches its constructor as `T(a, b)` does where every parameter is a scalar: no list becomes a class.
    if (text(id) == "{}")
        for (std::size_t i = 1; i < T(sig).params().size(); i++)
            if (!scalar(*T(sig).params()[i])) return false;
    return true;
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

// [expr.type.conv] into a slot of this frame: `P()` zeroed ([dcl.init]/8), `P(o)` o's bytes, a class with constructors built
// by the one the parser chose - with a destructor, alive to its full expression's end ([class.temporary]/3), which
// destroys it, as its cleanup does. The slot's address is the temporary's value.
Lowering::Val Lowering::temporary(sema::TypedId id) {
    const Type &t = *tree_.node(id).type->unqualified();
    const ValueId slot = entryAlloca(tid(t), "$tmp");
    const bool destroyed = destroyedTemporary(id);
    if (destroyed || constructedTemporary(id)) {
        const std::string dtor = destroyed ? temporaryDestructor(t) : std::string();
        std::string symbol;
        TypeId sig;
        bool zero = false;
        std::uint32_t from = 1;
        temporaryShape(id, from);
        temporaryBuilt_(tree_.node(id).syntax, symbol, sig, zero);
        construct(slot, symbol, sig, t, zero, id, from);
        if (destroyed) guard(slot, t, dtor);
        return Val(slot, tid(t));
    }
    if (text(id) == "{}") {
        if (!plainTemporary(id)) outside("a class temporary from a braced list");
        listInit(slot, t, id, 1);      // `P{a, b}`, an aggregate's list ([dcl.init.list]/3)
    } else if (count(id) == 1) {
        const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, slot));
        at(z).k = t.size(target_);
    } else {
        if (!classObjectNode(child(id, 1))) outside("a class temporary from what is not an object");
        Val o = classObject(child(id, 1));
        // [class.copy]/12 with [conv.ptr]/3: `B(d)` of a derived object copies its base subobject's bytes.
        if (T(o.type).isStructOrUnion() && T(o.type).unqualified() != &t) o = baseSubobject(o, t, "a class temporary from another type");
        if (T(o.type).unqualified() != &t) outside("a class temporary from another type");
        copyObject(slot, o.v, t);
    }
    return Val(slot, tid(t));
}

// `T(o)` of a class with constructors whose copy is its bytes and that has no destructor: a temporary record no
// constructor built, its one value a class object - of T, or of a class derived from it.
bool Lowering::copiedTemporary(sema::TypedId id) const {
    if (syn(id).kind != syntax::Kind::Call || count(id) != 2) return false;
    const sema::Resolution &r = tree_.resolution(id);
    const TypeId t = TypeTable::id(tree_.node(id).type), from = TypeTable::id(tree_.node(child(id, 1)).type);
    if (r.what != sema::Resolution::Temporary || !r.symbol.empty() || !t.valid() || !from.valid() ||
        tree_.resolution(child(id, 0)).what != sema::Resolution::TypeName)
        return false;
    return constructedClass(T(t)) && !T(t).unqualified()->nonTrivialCopy() && T(from).isStructOrUnion() && classObjectNode(child(id, 1));
}

// What a class value's object is - [class.temporary], a prvalue materialised: an lvalue's; a temporary's slot; a
// call's result with a destructor, the full expression's to destroy; or any object objectNode copies from.
bool Lowering::classObjectNode(sema::TypedId id) const {
    return isLvalueNode(id) || temporaryNode(id) || destroyedResult(id) || objectNode(id);
}

// That object's address and its class, const kept on the type.
Lowering::Val Lowering::classObject(sema::TypedId id) {
    if (isLvalueNode(id)) return lvalue(id);
    if (temporaryNode(id)) return temporary(id);
    if (destroyedResult(id)) {
        const Val r = syn(id).kind == syntax::Kind::Call ? call(id) : operatorCall(id);
        if (!r.type.valid() || !T(r.type).isPointer()) outside("a call's class result that is no object");
        return Val(r.v, tid(*T(r.type).pointee()));
    }
    if (!objectNode(id)) outside("a class value that is not an object");
    return object(id);
}

// [over.ref]: `->` on a class asks its operator-> for a pointer, and asks again while a class answers - each hop the
// member call the parser recorded on the operand's node, its object the answer before; a pointer operand reads as it is.
Lowering::Val Lowering::arrowPointer(sema::TypedId member) {
    const sema::TypedId o = child(member, 0);
    const TypeId ot = TypeTable::id(tree_.node(o).type);
    if (!ot.valid() || !T(ot).isStructOrUnion()) return decay(expr(o));
    std::vector<sema::Resolution> hops;
    if (!arrowCalls_ || !arrowCalls_(tree_.node(o).syntax, hops) || hops.empty()) outside("a '->' on a class with no operator-> recorded");
    if (!classObjectNode(o)) outside("a '->' on a class value that is not an object");
    Val object = classObject(o);
    Val p;
    for (std::size_t h = 0; h < hops.size(); h++) {
        const sema::Resolution &r = hops[h];
        if (r.what != sema::Resolution::Call || r.symbol.empty() || !r.hasThis || r.indirect || !r.type.valid() || !T(r.type).isFunction())
            outside("an operator-> the lowering does not call");
        const Type &ft = T(r.type);
        if (ft.params().size() != 1 || !ft.params()[0]->isPointer() || !ft.params()[0]->pointee()->isStructOrUnion())
            outside("an operator-> of another shape");
        const Type &self = *ft.params()[0];
        if (!T(object.type).isStructOrUnion()) outside("an operator-> asked of what is not a class");
        if (T(object.type).isConst() && !self.pointee()->isConst()) outside("an operator-> that drops const");
        Val at(object.v, tid(*types_.pointerTo(&T(object.type))));
        if (T(object.type).unqualified() != self.pointee()->unqualified()) at = toBase(at, tid(self));
        CallInfo info;
        info.symbol = r.symbol;
        info.hasThis = true;
        std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(self), std::vector<ValueId>(1, at.v))));
        p = callTail(r.type, info, ops, 1, std::vector<sema::TypedId>(), false);
        if (!p.type.valid() || !T(p.type).isPointer()) outside("an operator-> answering with no pointer");
        // A class answered - its result slot, or the object a reference names - is asked again; a pointer is the answer.
        if (ft.returns()->isStructOrUnion() || ft.returns()->isReference()) object = Val(p.v, tid(*T(p.type).pointee()));
        else return p;
    }
    outside("an operator-> whose last answer is a class");
    return Val();
}

// What a plain class's value can be copied from: an lvalue, a call's result, a temporary, an assignment, or a
// conditional of two such.
bool Lowering::objectNode(sema::TypedId id) const {
    if (isLvalueNode(id) || plainTemporary(id)) return true;
    const syntax::Kind k = syn(id).kind;
    if (k == syntax::Kind::Paren) return count(id) == 1 && objectNode(child(id, 0));
    if (k == syntax::Kind::Lambda) return closureNode(id);
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
    if (syn(id).kind == syntax::Kind::Lambda && closureNode(id)) return closure(id);
    if (syn(id).kind != syntax::Kind::Conditional || !objectNode(id)) outside("a class value that is not an object");
    const Val c = truth(expr(child(id, 0)));
    const BlockId a = newBlock(), b = newBlock(), merge = newBlock();
    condbr(c.v, a, b);
    place(a);
    skippable_++;
    const Val x = object(child(id, 1));
    const Type &cls = *T(x.type).unqualified();
    const TypeId pt = tid(*types_.pointerTo(&cls));
    br(merge, std::vector<ValueId>(1, result(add(Op::Bitcast, pt, std::vector<ValueId>(1, x.v)))));
    place(b);
    const Val y = object(child(id, 2));
    skippable_--;
    if (T(y.type).unqualified() != &cls) outside("a conditional of two objects apart in type");
    br(merge, std::vector<ValueId>(1, result(add(Op::Bitcast, pt, std::vector<ValueId>(1, y.v)))));
    place(merge);
    return Val(param(merge, pt), tid(cls));
}

// A lambda whose closure is a plain class, its captures the parser recorded: scalar locals, each copied or bound.
bool Lowering::closureNode(sema::TypedId id) const {
    sema::ClosureFacts f;
    return closure_ && closure_(tree_.node(id).syntax, f) && f.closure.valid() && plainClass(T(f.closure));
}

// [expr.prim.lambda]/2: the closure a temporary of this frame - each by-copy member the local's value, each by-reference
// one its address ([expr.prim.lambda]/21) - and the slot's address the lambda's value, as a plain temporary's is.
Lowering::Val Lowering::closure(sema::TypedId id) {
    sema::ClosureFacts f;
    closure_(tree_.node(id).syntax, f);
    const Type &cls = *T(f.closure).unqualified();
    const ValueId slot = entryAlloca(tid(cls), "$closure");
    // [expr.prim.lambda]/18: `this` captured is the enclosing member function's own, copied in; a closure's is not.
    if (f.thisOffset >= 0) {
        if (!this_.valid() || !f.thisType.valid() || T(f.thisType).pointee()->unqualified() != T(thisType_).pointee()->unqualified())
            outside("a lambda capturing a `this` that is not the member function's");
        const InstId g = add(Op::Gep, tid(*types_.pointerTo(&T(f.thisType))), std::vector<ValueId>(1, slot));
        at(g).k = f.thisOffset;
        store(result(g), result(add(Op::Bitcast, f.thisType, std::vector<ValueId>(1, this_))));
    }
    for (const sema::ClosureCapture &c : f.captures) {
        const std::map<std::uint32_t, std::pair<ValueId, TypeId> >::const_iterator l = locals_.find(c.source.v);
        if (l == locals_.end()) outside("a lambda capturing what is not a local of the body");
        // A reference local captures what it refers to; a class is held by reference, or by value where its copy is bytes.
        const Type &declared = T(l->second.second), &mt = T(c.type);
        const Type &src = *(declared.isReference() ? declared.referent() : &declared)->unqualified();
        const bool byRef = mt.isReference();
        if (!(scalar(src) && !src.isX87(target_)) && !(src.isStructOrUnion() && src.isComplete() && (byRef || plainClass(src))))
            outside("a lambda capturing what is not a scalar or a class it holds");
        if ((byRef ? mt.referent() : &mt)->unqualified() != &src) outside("a lambda's capture of another type");
        const TypeId held = byRef ? tid(*types_.pointerTo(&src)) : tid(src);
        const InstId g = add(Op::Gep, tid(*types_.pointerTo(&T(held))), std::vector<ValueId>(1, slot));
        at(g).k = c.offset;
        const ValueId from = declared.isReference() ? load(l->second.first, tid(*types_.pointerTo(declared.referent()))) : l->second.first;
        const ValueId at0 = result(add(Op::Bitcast, tid(*types_.pointerTo(&src)), std::vector<ValueId>(1, from)));
        if (!byRef && !scalar(src)) copyObject(result(g), at0, src);
        else store(result(g), byRef ? at0 : load(at0, held));
    }
    return Val(slot, tid(cls));
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
    labelScope_.clear();
    pendingGotos_.clear();
    hasGoto_ = false;
    pads_.clear();
    live_.clear();
    liveFloor_ = 0;
    typeTable_.clear();
    handlerDepth_ = 0;
    handlerLive_.clear();
    fullFrom_ = kNoFull;
    skippable_ = 0;
    funclet_ = BlockId();
    msTryPad_ = BlockId();
    msTryFunclet_ = BlockId();
    funcletPads_ = 0;
    sret_ = ValueId();
    elidedSlot_ = ValueId();
    intoSret_ = false;
    landingPads_ = facts.landingPads;
    vptrs_.clear();
    key_ = facts.recordKey.empty() ? facts.symbol : facts.recordKey;
    vtt_ = ValueId();
    msFlag_ = ValueId();
    try {
        if (facts.variadic) outside("a variadic function");
        if (facts.wrappedBody) outside("a function try block or an exception specification");
        // A noexcept body with pads of its own is not wrapped (wrappedBody says when it is): its pads are any function's.
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
        if (!(rt.isVoid() || scalar(rt) || plainClass(rt) || bytesReturned(rt) || destroyedReturned(rt) || copiedReturn(rt) ||
              (rt.isReference() && referable(*rt.referent()))))
            outside("a return type of '" + rt.describe() + "'");
        if (rt.isX87(target_)) outside("an x87 long double");
        // A constructor and a destructor of a class with a base, a vptr or a member that needs building or
        // destroying are M3b.3's: what they do beyond their syntax comes from the class's layout.
        bool isCtor = false, destroys = false;
        if (facts.hasThis && facts.thisType.valid() && T(facts.thisType).isPointer()) {
            const Type &cls = *T(facts.thisType).pointee()->unqualified();
            isCtor = facts.isConstructor;
            const bool isDtor = !text(fnNode).empty() && text(fnNode)[0] == '~';
            // A destructor of a class with a virtual base destroys its own part: cl's ??1, the virtual bases left to ??_D;
            // Itanium's C2 and D2, handed a VTT, build and destroy theirs, the virtual bases left to C1 and D1.
            const bool msVbaseDtor = isDtor && cls.hasVirtualBase() && target_.microsoftNames();
            const bool vbaseParts = (isCtor || isDtor) && cls.hasVirtualBase() && (msVbaseDtor || facts.takesVtt || (isCtor && facts.msFlag));
            if (isDtor && cls.hasVirtualBase() && !vbaseParts) outside("a destructor of a class with a virtual base, handed a VTT");
            if (msVbaseDtor && cls.polymorphic()) outside("a destructor of a polymorphic class with a virtual base");
            if ((isCtor || isDtor) && !simpleLayout(cls)) {
                if (!buildableLayout(cls, true, true, vbaseParts)) outside(isCtor ? "a constructor of a class with bases, a vptr or members to build"
                                                                : "a destructor of a class with bases, a vptr or members to destroy");
                // The vptr stores are the parser's record, made where it stores them: one, out of the class's own table -
                // or, in a C2 or D2, every vptr in the object, the VTT's where the complete object decides it.
                if (cls.hasVptr() && !msVbaseDtor && !(target_.microsoftNames() && !cls.polymorphic())) {
                    if (!vptrStores_ || !vptrStores_(key_, vptrs_) || vptrs_.empty()) outside("a vptr the parser recorded no store for");
                    if (vptrs_.size() != 1 && !facts.takesVtt) outside("a polymorphic base that is not primary");
                    if (!vptrs_[0].fromVtt && vptrs_[0].symbol.empty()) outside("a vptr out of a VTT");
                }
                // A constructor's guards are memberInits' own; a destructor's - what a throwing body still destroys - are not yet.
                if (facts.landingPads && !isCtor) outside("a destructor with landing pads that destroys members");
                destroys = isDtor;
            }
        }
        std::uint32_t n = count(fnNode);
        if (n == 0) outside("a function with no body");
        // A constructor's function try block: its handlers follow the body, which is then the last block.
        std::vector<sema::TypedId> ctorHandlers;
        while (facts.ctorTry && n > 1 && syn(child(fnNode, n - 1)).kind == syntax::Kind::Catch) ctorHandlers.insert(ctorHandlers.begin(), child(fnNode, --n));
        if (facts.ctorTry && (ctorHandlers.empty() || !isCtor || target_.microsoftNames())) outside("a function try block or an exception specification");
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
        if (params + (facts.hasThis ? 1 : 0) + (facts.takesVtt ? 1 : 0) + (facts.msFlag ? 1 : 0) != facts.paramCount)
            outside("a parameter list the typed tree and the function disagree about");
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
            // [2.6.2]: Itanium's C2 or D2 of a class with virtual bases is handed its VTT second.
            if (facts.takesVtt) {
                vtt_ = param(f.entry, types_.pointerTo(types_.pointerTo(types_.voidType()->id())));
                f.params.push_back(vtt_);
            }
        }
        // The caller's result pointer, which the parser's prologue keeps in its slot: every `return` of such a class
        // builds there and hands it back (buildReturned). Not among params: the selector copies it from that slot.
        if (copiedReturn(rt)) {
            if (!facts.sret) outside("a class with a copy of its own returned with no result pointer");
            sret_ = param(f.entry, tid(*types_.pointerTo(&rt)));
            f.sret = sret_;
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
            } else if (plainClass(pt) || bytesReturned(pt)) {
                // A plain class by value is the callee's own copy, in the slot the receive fills on both ABIs: its address.
                const ValueId pv = param(f.entry, tid(*types_.pointerTo(&pt)));
                f.params.push_back(pv);
                locals_[tree_.node(p).syntax.v] = std::make_pair(pv, tid(pt));
                continue;
            } else if (pt.isStructOrUnion()) {
                // A class that is not its bytes: the caller's copy, by address, where copying or destroying it is a call on
                // Itanium or copying it is one on Microsoft; else Microsoft's own copy in the slot - destroyed here (guardParameter).
                const ValueId pv = param(f.entry, tid(*types_.pointerTo(&pt)));
                f.params.push_back(pv);
                locals_[tree_.node(p).syntax.v] = std::make_pair(pv, tid(pt));
                guardParameter(pv, pt, isCtor);
                continue;
            } else {
                if ((!scalar(pt) && !pt.isNullPtr()) || pt.isX87(target_)) outside("a parameter of type '" + pt.describe() + "'");
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
        // cl's most-derived flag, after every written parameter.
        if (facts.msFlag) {
            msFlag_ = param(f.entry, types_.intType()->id());
            f.params.push_back(msFlag_);
        }
        // The return block: one `ret`, reached by a branch carrying the value.
        returnBlock_ = newBlock();
        const BlockId ret = returnBlock_;
        // A destructor's every way out of its body goes through the members' destruction first.
        if (destroys) returnBlock_ = newBlock();
        const BlockId first = newBlock();
        br(first);
        place(first);
        // A constructor's function try block: one region over its mem-initialisers and its body, the parts' cleanups in it.
        TryState ctorTry;
        if (facts.ctorTry) tryOpen(ctorHandlers, ctorTry);
        if (isCtor) memberInits(fnNode, inits, n - 1);
        else if (destroys) storeVptrs();
        // [except.unexpected]: a throw(...) list is one filter row over the body, under every pad the body makes.
        if (!facts.specFilter.empty()) {
            if (target_.microsoftNames()) outside("a throw(...) list on the Microsoft ABI");
            Pad spec;
            spec.cleanup = false;
            spec.own = 1;
            spec.types.push_back(facts.specFilter);
            spec.indices.push_back(-1);
            pads_.push_back(spec);
        }
        // [except.spec]/9: a noexcept body the parser wrapped is under a catch-all, inside the constructor's own parts.
        if (facts.noexceptWrap) {
            if (target_.microsoftNames() || !facts.specFilter.empty()) outside("a noexcept wrap beside another");
            Pad wrap;
            wrap.cleanup = false;
            wrap.own = 1;
            wrap.types.push_back(std::string());
            wrap.indices.push_back(typeIndexFor(std::string()));
            pads_.push_back(wrap);
        }
        const std::size_t wrapAt = pads_.size();
        // A constructor's parts, below the floor, are kept by a return; a by-value parameter the callee destroys is above it.
        liveFloor_ = isCtor ? live_.size() : 0;
        block(body);
        if (!pendingGotos_.empty()) outside("a goto to a label the body does not place");
        if (!terminated()) {
            destroyLive(liveFloor_);
            if (rt.isVoid()) br(returnBlock_);
            else br(returnBlock_, std::vector<ValueId>(1, fallOffValue(rt)));
        }
        if (facts.noexceptWrap) {
            if (pads_.size() != wrapAt) outside("a noexcept body that left a pad open");
            const Pad wrap = pads_.back();
            pads_.pop_back();
            if (wrap.block.valid()) noexceptAbort(wrap);
        }
        closeScope(0, true);
        if (facts.ctorTry) tryClose(ctorTry, true);
        if (!facts.specFilter.empty()) {
            const Pad spec = pads_.back();
            pads_.pop_back();
            if (spec.block.valid()) specFilterChain(spec);
        }
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

// [basic.start.main]/5 for main, and the walker's answer for any other body that falls off its end: a zero of the
// return type in the return register. A class, a reference or an x87 value has no such zero here.
ValueId Lowering::fallOffValue(const Type &rt) {
    if (rt.isStructOrUnion() || rt.isReference() || rt.isX87(target_) || !scalar(rt))
        outside("a body that falls off its end without a value");
    if (rt.isFloating()) {
        const InstId z = add(Op::FConst, returns_, std::vector<ValueId>());
        at(z).bits = 0;
        return result(z);
    }
    if (rt.isPointer()) return result(add(Op::Null, returns_, std::vector<ValueId>()));
    return iconst(returns_, 0);
}

// ----- statements

void Lowering::block(sema::TypedId id) {
    const std::size_t mark = live_.size();
    for (std::uint32_t i = 0; i < count(id); i++) stmt(child(id, i));
    closeScope(mark);
}

// A substatement is a scope of its own ([stmt.select]/1, [stmt.iter]/2): what a bare declaration there builds ends with it.
// The destructor of a temporary this full expression builds, which only an unconditional operand of one may hold.
std::string Lowering::temporaryDestructor(const Type &c) {
    if (fullFrom_ == kNoFull) outside("a class temporary with a destructor outside a full expression the lowering ends");
    if (skippable_ != 0) outside("a class temporary with a destructor in an operand that may not be evaluated");
    const std::string dtor = destructor_ ? destructor_(c.id()) : std::string();
    if (dtor.empty()) outside("a class temporary whose destructor the parser recorded none for");
    return dtor;
}

// A class a call returns into its caller's temporary, the caller destroying it ([class.temporary]/3): any with a destructor.
bool Lowering::destroyedResultClass(const Type &t) const {
    return t.isStructOrUnion() && t.isComplete() && t.unqualified()->hasDestructor() && t.unqualified()->kind() != Kind::Union;
}

// A call, or an operator the parser resolved to one, whose result is such a class.
bool Lowering::destroyedResult(sema::TypedId id) const {
    if (syn(id).kind != syntax::Kind::Call && !operatorCalls(id)) return false;
    const sema::Resolution &r = tree_.resolution(id);
    return r.what == sema::Resolution::Call && r.type.valid() && T(r.type).isFunction() && destroyedResultClass(*T(r.type).returns());
}

// A full expression opened: the temporaries it builds are alive from live_'s size now; closeFull destroys them at its end.
std::size_t Lowering::openFull() {
    const std::size_t was = fullFrom_;
    fullFrom_ = live_.size();
    return was;
}

void Lowering::closeFull(std::size_t was) {
    closeScope(fullFrom_);
    fullFrom_ = was;
}

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
        {
            const std::size_t was = openFull();
            expr(child(id, 0));
            closeFull(was);
        }
        return;
    case syntax::Kind::Return: returnStmt(id); return;
    case syntax::Kind::Try: tryStatement(id); return;
    case syntax::Kind::If: {
        if (n != 2 && n != 3) outside("an if of " + std::to_string(n) + " parts");
        const std::size_t was = openFull();
        const Val c = condition(child(id, 0));
        closeFull(was);
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
        const std::size_t was = openFull();
        const Val c = condition(child(id, 0));
        closeFull(was);
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
        const std::size_t was = openFull();
        const Val c = truth(expr(child(id, 1)));
        closeFull(was);
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
        labelReached(text(id), b);
        place(b);
        stmt(child(id, 0));
        return;
    }
    case syntax::Kind::Goto:
        gotoStmt(text(id));
        return;
    default:
        outside(std::string("a '") + syntax::kindName(s.kind) + "' statement");
    }
}

// [stmt.jump]/2 for a goto: what it leaves destroyed and the catches it leaves ended, as a jump out of a scope does. A
// label already passed says where it stands; a later one is reached through a block of the goto's own, filled there.
void Lowering::gotoStmt(const std::string &name) {
    if (elidedSlot_.valid()) outside("a goto in a function with a named return value");
    hasGoto_ = true;
    const std::map<std::string, std::pair<std::size_t, std::size_t> >::const_iterator at = labelScope_.find(name);
    if (at != labelScope_.end()) {
        if (at->second.first > live_.size() || at->second.second > handlerDepth_) outside("a goto into a scope it is not in");
        // On the Microsoft ABI a jump out of a handler leaves its funclet through an exit, as a return or a break does.
        leaveHandlers(handlerDepth_ - at->second.second, at->second.first, labelBlock(name));
        return;
    }
    PendingGoto g;
    g.label = name;
    g.msTryPad = msTryPad_;
    g.msTryFunclet = msTryFunclet_;
    g.from = newBlock();
    g.live = live_;
    g.handlerLive = handlerLive_;
    br(g.from);
    pendingGotos_.push_back(g);
}

// A label placed: each goto that went ahead of it destroys, in a block of its own, what it leaves beyond the label's
// objects - which [stmt.dcl]/3 makes a prefix of its own, checked here - ending its catches on the way, then joins it.
void Lowering::labelReached(const std::string &name, BlockId b) {
    labelScope_[name] = std::make_pair(live_.size(), handlerDepth_);
    for (std::size_t i = 0; i < pendingGotos_.size(); ) {
        const PendingGoto g = pendingGotos_[i];
        if (g.label != name) { i++; continue; }
        pendingGotos_.erase(pendingGotos_.begin() + static_cast<std::ptrdiff_t>(i));
        const std::size_t keep = live_.size(), depth = handlerLive_.size();
        if (keep > g.live.size() || depth > g.handlerLive.size()) outside("a goto into a scope it is not in");
        for (std::size_t k = 0; k < keep; k++) if (g.live[k].slot.v != live_[k].slot.v) outside("a goto into a scope it is not in");
        for (std::size_t k = 0; k < depth; k++) if (g.handlerLive[k] != handlerLive_[k]) outside("a goto into a scope it is not in");
        place(g.from);
        if (target_.microsoftNames() && depth < g.handlerLive.size()) { msGotoExit(g, b, keep, depth); continue; }
        std::size_t upper = g.live.size();
        for (std::size_t h = g.handlerLive.size(); h-- > depth; ) {
            for (std::size_t k = upper; k-- > g.handlerLive[h]; ) destroy(g.live[k]);
            endCatches(1);
            upper = g.handlerLive[h];
        }
        for (std::size_t k = upper; k-- > keep; ) destroy(g.live[k]);
        br(b);
    }
}

// A goto that went ahead of its label out of a Microsoft handler, filled where the label is placed: the handler's own
// objects destroyed in the funclet, a catchret to an exit the try's pad lists, and the exit destroys the rest and joins.
void Lowering::msGotoExit(const PendingGoto &g, BlockId label, std::size_t keep, std::size_t depth) {
    if (g.handlerLive.size() - depth != 1) outside("a goto out of nested Microsoft handlers");
    const std::size_t own = g.handlerLive.back();
    for (std::size_t k = g.live.size(); k-- > own; ) destroy(g.live[k]);
    const BlockId was = funclet_;
    funclet_ = g.msTryFunclet;
    const BlockId exit = newBlock();
    funclet_ = was;
    Function &f = F();
    at(f.block(g.msTryPad).insts[0]).cases.push_back(exit.v);
    Succ s;
    s.block = exit;
    at(add(Op::CatchRet, TypeId(), std::vector<ValueId>())).succ.push_back(s);
    place(exit);
    for (std::size_t k = own; k-- > keep; ) destroy(g.live[k]);
    br(label);
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
        const std::size_t was = openFull();
        const Val v = expr(child(id, 0));
        closeFull(was);
        if (v.type.valid() && !T(v.type).isVoid()) outside("a return of a value from a void function");
        leaveHandlers(handlerDepth_, liveFloor_, returnBlock_);
        return;
    }
    if (n != 1) outside("a return of " + std::to_string(n) + " parts");
    if (rt.isReference()) {
        // [stmt.return]/2 with [class.temporary]/4: the address taken, then the full expression's temporaries destroyed.
        const std::size_t was = openFull();
        const Val a = lvalue(child(id, 0));
        closeFull(was);
        if (T(a.type).unqualified() != rt.referent()->unqualified()) outside("a reference returned to another type");
        if (T(a.type).isConst() && !rt.referent()->isConst()) outside("a reference that can write returned from a const");
        const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(rt.referent())), std::vector<ValueId>(1, a.v));
        leaveHandlers(handlerDepth_, liveFloor_, returnBlock_, std::vector<ValueId>(1, result(c)));
        return;
    }
    if (copiedReturn(rt)) { returnCopied(id, child(id, 0)); return; }
    if (rt.isStructOrUnion() && !plainClass(rt)) { returnConstructed(child(id, 0)); return; }
    if (rt.isStructOrUnion()) {
        if (!objectNode(child(id, 0))) outside("a class returned from what is not an object");
        Val a = object(child(id, 0));
        if (T(a.type).isStructOrUnion() && T(a.type).unqualified() != &rt) a = baseSubobject(a, rt, "a class returned from another type");
        if (T(a.type).unqualified() != &rt) outside("a class returned from another type");
        if (!live_.empty()) outside("a class returned while a local with a destructor is alive");
        const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(&rt)), std::vector<ValueId>(1, a.v));
        br(returnBlock_, std::vector<ValueId>(1, result(c)));
        return;
    }
    // [stmt.return]: the value computed, then its full expression's temporaries destroyed, then the scopes left.
    const std::size_t was = openFull();
    const Val v = exprTo(child(id, 0), returns_);
    closeFull(was);
    leaveHandlers(handlerDepth_, liveFloor_, returnBlock_, std::vector<ValueId>(1, v.v));
}

// A class with constructors and no copy of its own or destructor, returned as a plain one is - its bytes, which the
// walker's return copies out by the ABI's rule: so the parser builds no copy for its own local or for `T(...)`.
bool Lowering::bytesReturned(const Type &t) const {
    return t.isStructOrUnion() && t.isComplete() && constructedClass(t) && !t.unqualified()->nonTrivialCopy();
}

// The forms the parser elides for such a class - a local of the function, a temporary the return builds - and any other
// object of it, a call's result among them (a slot of this frame, by the same ABI rule): its bytes copied out from there.
void Lowering::returnConstructed(sema::TypedId value) {
    const Type &rt = T(returns_);
    if (destroyedReturned(rt)) { returnDestroyed(value); return; }
    if (!live_.empty() || handlerDepth_ != 0) outside("a constructed class returned inside a handler or beside a destructor");
    while (syn(value).kind == syntax::Kind::Paren && count(value) == 1) value = child(value, 0);
    Val a;
    if (constructedTemporary(value)) {
        a = temporary(value);
    } else if ((syn(value).kind == syntax::Kind::Call || operatorCalls(value)) && isLvalueNode(value)) {
        a = lvalue(value);      // a call's result: the slot of this frame the callee built into, or the object it names
    } else if (!(syn(value).kind == syntax::Kind::Name && tree_.node(value).decl.valid() &&
                 locals_.find(tree_.node(value).decl.v) != locals_.end()) && isLvalueNode(value)) {
        a = lvalue(value);      // `*this`, a member, an element, a global: an object whose bytes the return copies out
    } else {
        const sema::TypedNode &n = tree_.node(value);
        if (syn(value).kind != syntax::Kind::Name || !n.decl.valid() || locals_.find(n.decl.v) == locals_.end())
            outside("a constructed class returned from what is neither a local nor a temporary");
        if (!T(locals_.find(n.decl.v)->second.second).isStructOrUnion()) outside("a constructed class returned through a reference");
        a = nameAddress(value);
    }
    if (T(a.type).unqualified() != rt.unqualified()) outside("a constructed class returned from another type");
    const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(&rt)), std::vector<ValueId>(1, a.v));
    br(returnBlock_, std::vector<ValueId>(1, result(c)));
}

// A class with a destructor and no copy of its own, returned as the parser returns it: its bytes, copied out by the ABI's
// rule, the source not destroyed where the parser elides it - a local it built, a temporary it released.
bool Lowering::destroyedReturned(const Type &t) const {
    return builtClass(t) && t.unqualified()->hasDestructor() && !t.unqualified()->nonTrivialCopy() && !t.unqualified()->polymorphic();
}

// [class.copy]/31 as the parser takes it: a local this function built keeps its bytes for the `ret` and every other object
// alive is destroyed; `T(...)` is built in a slot no scope destroys; any other object's bytes are saved before the
// destructors run. A parameter, a call's result and a handler's way out stay the walker's.
void Lowering::returnDestroyed(sema::TypedId value) {
    const Type &rt = T(returns_);
    if (handlerDepth_ != 0) outside("a class with a destructor returned inside a handler");
    while (syn(value).kind == syntax::Kind::Paren && count(value) == 1) value = child(value, 0);
    const sema::TypedNode &n = tree_.node(value);
    ValueId at0, kept;
    std::uint32_t from = 1;
    if (syn(value).kind == syntax::Kind::Name && n.decl.valid() && locals_.find(n.decl.v) != locals_.end()) {
        const std::pair<ValueId, TypeId> &l = locals_.find(n.decl.v)->second;
        if (T(l.second).unqualified() != rt.unqualified()) outside("a class with a destructor returned from another type");
        for (std::size_t k = liveFloor_; k < live_.size(); k++)
            if (live_[k].slot.v == l.first.v) kept = l.first;
        if (!kept.valid()) outside("a class with a destructor returned from a local no scope destroys");
        at0 = kept;
    } else if (temporaryShape(value, from)) {
        std::string symbol;
        TypeId sig;
        bool zero = false;
        if (!temporaryBuilt_(n.syntax, symbol, sig, zero)) outside("a class with a destructor returned from a temporary no constructor built");
        if (T(TypeTable::id(n.type)).unqualified() != rt.unqualified()) outside("a class with a destructor returned from another type");
        at0 = entryAlloca(tid(*rt.unqualified()), "$tmp");
        construct(at0, symbol, sig, *rt.unqualified(), zero, value, from);
    } else if (syn(value).kind != syntax::Kind::Call && !operatorCalls(value) && isLvalueNode(value)) {
        const Val a = lvalue(value);
        if (T(a.type).unqualified() != rt.unqualified()) outside("a class with a destructor returned from another type");
        at0 = entryAlloca(tid(*rt.unqualified()), "$ret");
        copyObject(at0, a.v, *rt.unqualified());
    } else {
        outside("a class with a destructor returned from what is neither a local, a temporary nor an object");
    }
    for (std::size_t k = live_.size(); k-- > liveFloor_; )
        if (live_[k].slot.v != kept.v) destroy(live_[k]);
    const InstId c = add(Op::Bitcast, tid(*types_.pointerTo(&rt)), std::vector<ValueId>(1, at0));
    br(returnBlock_, std::vector<ValueId>(1, result(c)));
}

// A class with a copy or move constructor of its own: the parser builds the caller's object through the hidden
// pointer (buildReturned), and the lowering does what it recorded for each `return`.
bool Lowering::copiedReturn(const Type &t) const {
    return t.isStructOrUnion() && t.isComplete() && t.unqualified()->nonTrivialCopy();
}

// The named return value is built where it is declared, by its constructor, into the caller's object: builtLocal
// again, over the result pointer - its guard destroys it on every way out but the `return` that names it.
void Lowering::namedReturnLocal(sema::TypedId var, ValueId slot, const Type &t) {
    const Type &c = *t.unqualified();
    const sema::TypedNode &v = tree_.node(var);
    if (tree_.resolution(var).what != sema::Resolution::Call) outside("a named return value built by no constructor");
    std::string symbol;
    TypeId sig;
    bool defaulted = false, zeroed = false;
    if (!localBuilt_ || !localBuilt_(v.syntax, symbol, sig, defaulted, zeroed)) outside("a named return value built by no constructor the parser recorded");
    if (defaulted || zeroed || v.initForm == 2) outside("a named return value built with defaults, zeroed or from a braced list");
    const Type &ft = T(sig);
    if (ft.params().empty() || ft.params()[0]->pointee()->unqualified() != &c) outside("a named return value's constructor of another class");
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, slot))));
    if (v.count != 0) arguments(var, 0, ft, 1, false, ops);
    else if (ft.params().size() != 1) outside("a named return value's default constructor of parameters");
    directCall(symbol, sig, ops);
    if (!c.hasDestructor()) return;
    if (handlerDepth_ != 0) outside("a named return value inside a handler");
    const std::string dtor = destructor_ ? destructor_(c.id()) : std::string();
    if (dtor.empty()) outside("a named return value whose destructor the parser recorded none for");
    guard(slot, c, dtor);
}

// The forms buildReturned records: the named return value, already the caller's object; a temporary `T(...)` built
// there; and any other object copied there by the constructor it chose - then the destructors every return runs.
void Lowering::returnCopied(sema::TypedId id, sema::TypedId value) {
    const Type &rt = T(returns_);
    int form = 0;
    std::string symbol;
    TypeId sig;
    if (!returnBuilt_ || !returnBuilt_(tree_.node(id).syntax, form, symbol, sig)) outside("a class with a copy of its own returned with no record of how");
    while (syn(value).kind == syntax::Kind::Paren && count(value) == 1) value = child(value, 0);
    if (form == 1) {
        const sema::TypedNode &n = tree_.node(value);
        if (syn(value).kind != syntax::Kind::Name || !n.decl.valid() || locals_.find(n.decl.v) == locals_.end() ||
            locals_.find(n.decl.v)->second.first.v != sret_.v)
            outside("a named return value that is not the recorded local");
        elidedSlot_ = sret_;
        leaveHandlers(handlerDepth_, liveFloor_, returnBlock_, std::vector<ValueId>(1, sret_));
        elidedSlot_ = ValueId();
        return;
    } else if (form == 3) {
        // Built in the caller's object and never destroyed here, so a destructor is no bar, as it is for a temporary of the frame.
        bool zero = false;
        std::uint32_t from = 1;
        if (!temporaryShape(value, from) || !temporaryBuilt_(tree_.node(value).syntax, symbol, sig, zero))
            outside("a return's temporary the parser recorded no constructor for");
        construct(sret_, symbol, sig, rt, zero, value, from);
    } else if (form == 4) {
        if (!isLvalueNode(value)) outside("a class copied on return from what is not an lvalue");
        const Val a = lvalue(value);
        if (T(a.type).unqualified() != rt.unqualified()) outside("a class copied on return from another type");
        const Type &ft = T(sig);
        if (!ft.isFunction() || ft.params().size() != 2 || !ft.params()[1]->isReference() ||
            ft.params()[0]->pointee()->unqualified() != rt.unqualified())
            outside("a return's copy constructor of another shape");
        std::vector<ValueId> ops;
        ops.push_back(result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, sret_))));
        ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(ft.params()[1]->referent())), std::vector<ValueId>(1, a.v))));
        directCall(symbol, sig, ops);
    } else if (form == 2) {
        // `return f();`: the callee builds the caller's object through this function's own result pointer.
        if (syn(value).kind != syntax::Kind::Call && !operatorCalls(value)) outside("a return built in place by what is not a call");
        const std::size_t was = openFull();
        intoSret_ = true;
        const Val v = syn(value).kind == syntax::Kind::Call ? call(value) : operatorCall(value);
        intoSret_ = false;
        if (!v.type.valid() || !T(v.type).isPointer() || T(v.type).pointee()->unqualified() != rt.unqualified())
            outside("a return built in place by a call of another type");
        closeFull(was);
    } else {
        outside("a class with a copy of its own returned by a form not recorded");
    }
    leaveHandlers(handlerDepth_, liveFloor_, returnBlock_, std::vector<ValueId>(1, sret_));
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
    if (!(parts & 8)) { rangeForStmt(id); return; }
    const std::uint32_t want = 1 + ((parts & 1) ? 1 : 0) + ((parts & 2) ? 1 : 0) + ((parts & 4) ? 1 : 0);
    if (n != want) outside("a for whose parts the record and the syntax disagree about");
    std::uint32_t at = 0;
    const std::size_t scope = live_.size();      // [stmt.for]/1: the init's objects live to the loop's end
    if (parts & 1) {
        const sema::TypedId init = child(id, at++);
        if (syn(init).kind == syntax::Kind::DeclStmt) declaration(init);
        else { const std::size_t was = openFull(); expr(init); closeFull(was); }
    }
    const sema::TypedId condNode = (parts & 2) ? child(id, at++) : sema::TypedId();
    const sema::TypedId stepNode = (parts & 4) ? child(id, at++) : sema::TypedId();
    if (condNode.valid() && syn(condNode).kind == syntax::Kind::DeclStmt) outside("a declaration in a for's condition");
    const BlockId cond = newBlock(), body = newBlock(), step = newBlock(), exit = newBlock();
    br(cond);
    place(cond);
    if (condNode.valid()) {
        const std::size_t was = openFull();
        const Val c = truth(expr(condNode));
        closeFull(was);
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
    if (stepNode.valid()) { const std::size_t was = openFull(); expr(stepNode); closeFull(was); }
    br(cond);
    place(exit);
    closeScope(scope);
}

// [stmt.ranged]/1 for a class range whose iterators are pointers, as the parser rewrote it: `begin` or `end` - its member
// on the range's address, or the free function given the range - called once, by the record the parser made of it.
ValueId Lowering::rangeEnd(const sema::Resolution &r, Val range, TypeId &iter) {
    if (r.what != sema::Resolution::Call || r.indirect || r.symbol.empty() || !r.type.valid() || !T(r.type).isFunction())
        outside("a range-based for whose begin or end is not a direct call");
    const Type &ft = T(r.type), &cls = *T(range.type).unqualified();
    if (ft.isVariadicFn() || ft.params().size() != 1 || !ft.returns()->isPointer()) outside("a range-based for's begin or end of another shape");
    const Type &p = *ft.params()[0];
    TypeId to;
    if (r.hasThis ? p.isPointer() : p.isReference()) to = tid(r.hasThis ? *p.pointee() : *p.referent());
    if (!to.valid() || T(to).unqualified() != &cls || (T(range.type).isConst() && !T(to).isConst()))
        outside("a range-based for's begin or end taking another type");
    CallInfo info;
    info.symbol = r.symbol;
    info.signature = r.type;
    info.hasThis = r.hasThis;
    info.namedArgs = 1;
    const std::uint32_t callIndex = static_cast<std::uint32_t>(F().calls.size());
    F().calls.push_back(info);
    const TypeId rt = tid(*ft.returns()->unqualified());
    const ValueId self = result(add(Op::Bitcast, types_.pointerTo(to), std::vector<ValueId>(1, range.v)));
    const InstId c = callOrInvoke(rt, std::vector<ValueId>(1, self));
    at(c).call = callIndex;
    if (iter.valid() && iter.v != rt.v) outside("a range-based for whose begin and end differ in type");
    iter = rt;
    return result(c);
}

// [stmt.ranged]/1, the parser's rewrite: `__b` walks from the first element to one past the last - an array's, or what a
// class range's `begin` and `end` hand back as pointers - the range read once, and the variable takes `*__b` - or binds
// it - each turn. A class's range with class iterators, or a range not an object, stays outside.
void Lowering::rangeForStmt(sema::TypedId id) {
    if (count(id) != 3 || syn(child(id, 1)).kind != syntax::Kind::Var) outside("a range-based for of other parts");
    const sema::TypedId range = child(id, 0), var = child(id, 1);
    const TypeId rid = TypeTable::id(tree_.node(range).type), vid = TypeTable::id(tree_.node(var).type);
    sema::Resolution rb, re;
    const bool calls = rid.valid() && T(rid).unqualified()->isStructOrUnion() && rangeCalls_ &&
                       rangeCalls_(tree_.node(id).syntax, rb, re);
    if (!calls && (!rid.valid() || !T(rid).isArray() || T(rid).length() <= 0)) outside("a range-based for over what is not an array");
    if (!isLvalueNode(range)) outside("a range-based for over what is not an object");
    TypeId iter;
    if (calls) {
        if (rb.type.valid() && T(rb.type).isFunction() && T(rb.type).returns()->isPointer()) iter = tid(*T(rb.type).returns()->unqualified());
        if (!iter.valid()) outside("a range-based for whose begin is not a pointer");
    }
    const Type &rt = T(rid), &elem = calls ? *T(iter).pointee() : *rt.pointee();
    if (!vid.valid() || !elem.isComplete() || (!calls && (!scalar(elem) || elem.isX87(target_))))
        outside("a range-based for over an array of '" + elem.describe() + "'");
    const Type &vt = T(vid);
    TypeId slotType;
    if (vt.isReference()) {
        if (vt.referent()->unqualified() != elem.unqualified() || (elem.isConst() && !vt.referent()->isConst()))
            outside("a range-based for's reference of another type");
        slotType = tid(*types_.pointerTo(vt.referent()));
    } else {
        if (!scalar(vt) || vt.isX87(target_) || !scalar(elem) || elem.isX87(target_))
            outside("a range-based for's variable of type '" + vt.describe() + "'");
        slotType = vid;
    }
    const std::size_t scope = live_.size();
    Val first;
    ValueId past;
    if (calls) {
        const Val object = lvalue(range);
        TypeId same = iter;
        first = Val(rangeEnd(rb, object, same), iter);
        past = rangeEnd(re, object, same);
    } else {
        first = decay(lvalue(range));
        const InstId e = add(Op::Gep, first.type, std::vector<ValueId>(1, first.v));
        at(e).k = rt.length() * elem.size(target_);
        past = result(e);
    }
    const ValueId cursor = entryAlloca(first.type, "$range");
    store(cursor, first.v);
    const ValueId slot = entryAlloca(slotType, text(var));
    locals_[tree_.node(var).syntax.v] = std::make_pair(slot, vid);
    const BlockId cond = newBlock(), body = newBlock(), step = newBlock(), exit = newBlock();
    br(cond);
    place(cond);
    std::vector<ValueId> ops;
    ops.push_back(load(cursor, first.type));
    ops.push_back(past);
    const InstId c = add(Op::ICmp, tid(*types_.get(Kind::Bool)), ops);
    at(c).cc = Cmp::Ne;
    condbr(result(c), body, exit);
    place(body);
    const ValueId here = load(cursor, first.type);
    if (vt.isReference()) store(slot, result(add(Op::Bitcast, slotType, std::vector<ValueId>(1, here))));
    else store(slot, convert(readOf(here, tid(elem)), slotType).v);
    loops_.push_back(Loop{ exit, step, handlerDepth_, live_.size() });
    scopedStmt(child(id, 2));
    loops_.pop_back();
    br(step);
    place(step);
    const InstId next = add(Op::Gep, first.type, std::vector<ValueId>(1, load(cursor, first.type)));
    at(next).k = elem.size(target_);
    store(cursor, result(next));
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
            if (v.type->isReference() && !referable(*v.type->referent())) outside("a static reference to '" + v.type->referent()->describe() + "'");
            if (!guarded_) outside("a static local built at run time");
            if (guarded_(r.symbol)) { guardedStatic(var, r.symbol); continue; }
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
            long long elems = 0;
            if (!builtArray(t, elems, true).valid() &&
                (!t.pointee()->isComplete() || !scalar(*t.pointee()) || t.pointee()->isX87(target_) || t.length() <= 0))
                outside("an array of '" + t.pointee()->describe() + "'");
            slotType = tid(t);
        } else if (plainClass(t) || builtClass(t) || heldClass(t)) {
            slotType = tid(*t.unqualified());
        } else {
            if ((!scalar(t) && !t.isNullPtr()) || t.isX87(target_)) outside("a local of type '" + t.describe() + "'");
            slotType = tid(t);
        }
        // nrvoCandidate's local is built in the caller's object: alive as any local - its scope's end, a jump out and a
        // cleanup destroy it - but for the `return` that names it, which hands it back ([class.copy]/31).
        if (namedReturn_ && namedReturn_(v.syntax)) {
            if (!sret_.valid() || !t.isStructOrUnion() || t.unqualified() != T(returns_).unqualified()) outside("a named return value of another type");
            locals_[v.syntax.v] = std::make_pair(sret_, tid(t));
            namedReturnLocal(var, sret_, t);
            continue;
        }
        const ValueId slot = entryAlloca(slotType, text(var));
        locals_[v.syntax.v] = std::make_pair(slot, tid(t));
        if (t.isReference()) {
            if (v.count != 1) outside("a reference of " + std::to_string(v.count) + " parts");
            Val b = lvalue(child(var, 0));
            // [dcl.init.ref]/5: a class lvalue bound to a reference to its base binds the base subobject.
            if (T(b.type).isStructOrUnion() && t.referent()->isStructOrUnion() && T(b.type).unqualified() != t.referent()->unqualified())
                b = baseOfObject(b, t);
            if (T(b.type).unqualified() != t.referent()->unqualified()) outside("a reference bound to another type");
            if (T(b.type).isConst() && !t.referent()->isConst()) outside("a reference that can write bound to a const");
            store(slot, result(add(Op::Bitcast, slotType, std::vector<ValueId>(1, b.v))));
            continue;
        }
        if (t.isArray()) {
            long long elems = 0;
            const TypeId e = builtArray(t, elems, true);
            if (e.valid()) builtArrayLocal(var, slot, T(e), elems);
            else localArray(var, slot, slotType);
            continue;
        }
        if (t.isStructOrUnion()) { if (plainClass(t)) classLocal(var, slot, t); else builtLocal(var, slot, t); continue; }
        if (v.count > 1) outside("a var of " + std::to_string(v.count) + " parts");
        if (v.count == 0) {
            // [dcl.init]/8: `int n = {}` and `int n{}` are value-initialised - zero; a scalar with no initialiser is left as it is.
            if (v.initForm == 3) store(slot, zeroOf(slotType));
            continue;
        }
        const std::size_t was = openFull();
        store(slot, exprTo(child(var, 0), slotType).v);
        closeFull(was);
    }
}

// [dcl.init]/7 and [class.dtor]/10 for an array of such a class with no initialiser: each element, first to last, by the
// default constructor the parser chose - where it chose one - and alive to the scope's end, the last built destroyed first.
void Lowering::builtArrayLocal(sema::TypedId var, ValueId slot, const Type &et, long long elems) {
    const sema::TypedNode &v = tree_.node(var);
    if (v.initForm != 0) outside("an array of a class initialised from a list");
    const bool dies = et.hasDestructor();
    if (elems > (dies ? 16 : 64)) outside("an array of more elements than the lowering builds one by one");
    if (dies && handlerDepth_ != 0) outside("a local with a destructor inside a handler");
    const std::string dtor = dies && destructor_ ? destructor_(et.id()) : std::string();
    if (dies && dtor.empty()) outside("an array whose elements' destructor the parser recorded none for");
    std::string symbol;
    TypeId sig;
    bool defaulted = false, zeroed = false;
    const bool built = localBuilt_ && localBuilt_(v.syntax, symbol, sig, defaulted, zeroed);
    if (!built && constructed_ && constructed_(et.id())) outside("an array built by no constructor the parser recorded");
    if (defaulted || zeroed) outside("an array built with default arguments or zeroed");
    if (built && (T(sig).params().size() != 1 || T(sig).params()[0]->pointee()->unqualified() != &et))
        outside("an array's constructor of another class");
    const TypeId self = types_.pointerTo(et.id());
    for (long long k = 0; k < elems; k++) {
        const InstId g = add(Op::Gep, self, std::vector<ValueId>(1, slot));
        at(g).k = k * et.size(target_);
        if (built) directCall(symbol, sig, std::vector<ValueId>(1, result(g)));
        if (dies) guard(result(g), et, dtor);
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
    if (items == 1 && syn(child(var, first)).kind == syntax::Kind::StringLit && !t.pointee()->isPointer()) {
        stringArray(child(var, first), slot, t);
        return;
    }
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

// [dcl.init.string]: an array of characters from a string literal - the literal's code units with their terminating
// zero, which the program holds as bytes under the literal's label, copied in, and the elements past them zeroed.
void Lowering::stringArray(sema::TypedId lit, ValueId slot, const Type &t) {
    const std::string &label = tree_.stringLabel(lit);
    if (!tree_.node(lit).literal || label.empty()) outside("an array initialised from a string whose label was not recorded");
    std::size_t found = program_.strings.size();
    for (std::size_t k = 0; k < program_.strings.size(); k++)
        if (program_.strings[k].label == label) found = k;
    if (found == program_.strings.size()) outside("an array initialised from a string the program does not hold");
    const StringLit &s = program_.strings[found];
    const long long size = t.size(target_), copied = static_cast<long long>(s.bytes.size());
    if (s.width != t.pointee()->size(target_) || copied > size) outside("an array initialised from a string of another width");
    std::vector<ValueId> ops;
    ops.push_back(slot);
    ops.push_back(addrOf(label, tid(*types_.pointerTo(&t))));
    const InstId c = add(Op::Memcpy, TypeId(), ops);
    at(c).k = copied;
    if (copied == size) return;
    const TypeId elem = tid(*types_.pointerTo(t.pointee()));
    const InstId g = add(Op::Gep, elem, std::vector<ValueId>(1, result(add(Op::Bitcast, elem, std::vector<ValueId>(1, slot)))));
    at(g).k = copied;
    const InstId z = add(Op::Memset, TypeId(), std::vector<ValueId>(1, result(g)));
    at(z).k = size - copied;
}

// A scalar's zero: a null pointer, 0.0, or 0 of the integer type.
ValueId Lowering::zeroOf(TypeId type) {
    if (T(type).isPointer() || T(type).isNullPtr()) return result(add(Op::Null, type, std::vector<ValueId>()));
    if (!T(type).isFloating()) return iconst(type, 0);
    const InstId z = add(Op::FConst, type, std::vector<ValueId>());
    at(z).bits = 0;
    return result(z);
}

// ----- expressions

bool Lowering::isLvalueNode(sema::TypedId id) const {
    const syntax::Node &s = syn(id);
    if (operatorCalls(id)) {
        const sema::Resolution &r = tree_.resolution(id);
        return r.type.valid() && T(r.type).isFunction() &&
               (T(r.type).returns()->isReference() || plainClass(*T(r.type).returns()) || bytesReturned(*T(r.type).returns()));
    }
    switch (s.kind) {
    case syntax::Kind::Name: {
        if (tree_.node(id).decl.valid()) return true;
        const sema::Resolution &r = tree_.resolution(id);
        // A static data member with storage is an object as a global is; a folded one has none.
        return r.what == sema::Resolution::Global || r.what == sema::Resolution::Member ||
               (r.what == sema::Resolution::StaticMember && r.serial != 1);
    }
    case syntax::Kind::StringLit: case syntax::Kind::Member: case syntax::Kind::Subscript: return true;
    case syntax::Kind::Unary: return text(id) == "*";
    case syntax::Kind::Assign: {
        // A class's operator= the parser called: what it returns, `X &`, names an object (`(b = a).v`, `c = b = a`).
        std::string symbol;
        TypeId sig;
        return count(id) == 2 && text(id) == "=" && assignCall_ && assignCall_(tree_.node(child(id, 1)).syntax, symbol, sig);
    }
    case syntax::Kind::Paren: return count(id) == 1 && isLvalueNode(child(id, 0));
    case syntax::Kind::Cast: return referenceCast(id) && isLvalueNode(child(id, 0));
    case syntax::Kind::Conditional:
        return count(id) == 3 && isLvalueNode(child(id, 1)) && isLvalueNode(child(id, 2)) &&
               tree_.node(child(id, 1)).type != nullptr && tree_.node(child(id, 1)).type == tree_.node(child(id, 2)).type;
    case syntax::Kind::Call: {
        const sema::Resolution &r = tree_.resolution(id);
        return r.what == sema::Resolution::Call && r.type.valid() && T(r.type).isFunction() &&
               (T(r.type).returns()->isReference() || plainClass(*T(r.type).returns()) || bytesReturned(*T(r.type).returns()));
    }
    default: return false;
    }
}

// A cast that names its operand's object: to a reference, an xvalue, or a static_cast or const_cast written to an
// lvalue reference, which the parser notes as its referent - where the operand has that type, or a class a step from it.
bool Lowering::referenceCast(sema::TypedId id) const {
    const sema::TypedNode &n = tree_.node(id);
    if (count(id) != 1 || !n.castType.valid()) return false;
    if (T(n.castType).isReference() || n.castXvalue) return true;
    const std::string &w = text(id);
    const TypeId from = TypeTable::id(tree_.node(child(id, 0)).type);
    return w.find("&>") != std::string::npos && (w.compare(0, 12, "static_cast<") == 0 || w.compare(0, 11, "const_cast<") == 0) &&
           from.valid() && (T(from).unqualified() == T(n.castType).unqualified() ||
                            (T(from).isStructOrUnion() && T(n.castType).isStructOrUnion()));
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
        const Val base = expr(child(id, 0));   // the operands lowered in order, not in the argument order of the host's compiler
        const Val sum = arithmetic("+", base, expr(child(id, 1)));
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
        if (temporaryNode(id)) return temporary(id);     // `(T)v` through T's converting constructor: its slot
        // A cast to a reference names its operand's object: the same type, const or an rvalue's && aside.
        if (!isLvalueNode(id)) outside("a cast to a reference of what is not an object");
        const Type &cast = T(tree_.node(id).castType);
        const Type &to = cast.isReference() ? *cast.referent() : cast;      // static_cast<T &&> is noted as T, an xvalue
        const Val a = lvalue(child(id, 0));
        if (T(a.type).isConst() && !to.isConst()) outside("a cast that takes a const away");
        // [expr.static.cast]/2 and /11: a reference to a base names the base subobject, one to a derived class steps back.
        if (T(a.type).unqualified() != to.unqualified() && T(a.type).isStructOrUnion() && to.isStructOrUnion()) {
            const Val p = toBase(Val(a.v, tid(*types_.pointerTo(&T(a.type)))), tid(*types_.pointerTo(&to)), true);
            return Val(p.v, tid(to));
        }
        if (T(a.type).unqualified() != to.unqualified()) outside("a cast to a reference to another type");
        return Val(a.v, tid(to));
    }
    case syntax::Kind::Conditional: {
        if (!isLvalueNode(id)) outside("a conditional that is no lvalue");
        const Val c = truth(expr(child(id, 0)));
        const BlockId a = newBlock(), b = newBlock(), merge = newBlock();
        condbr(c.v, a, b);
        place(a);
        skippable_++;
        const Val x = lvalue(child(id, 1));
        const TypeId pt = tid(*types_.pointerTo(&T(x.type)));
        br(merge, std::vector<ValueId>(1, x.v));
        place(b);
        const Val y = lvalue(child(id, 2));
        skippable_--;
        if (y.type != x.type) outside("a conditional of two lvalues apart in type");
        br(merge, std::vector<ValueId>(1, y.v));
        place(merge);
        return Val(param(merge, pt), x.type);
    }
    case syntax::Kind::Assign: {
        Val o;
        if (!isLvalueNode(id) || !classAssign(id, o) || !o.v.valid()) outside("an assignment that is no lvalue");
        return o;
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
    if (s.kind == syntax::Kind::Other && text(id) == "{}") outside("a braced list as an argument");
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
        if (k == 0) { e = valueInitialised(id); break; }
        if (k != 1 || !n.castType.valid()) outside("a cast not recorded");
        const Type &to = T(n.castType);
        const Type &named = to.isReference() ? *to.referent() : to;
        if (referenceCast(id) && scalar(named) && !named.isX87(target_) && isLvalueNode(id)) {
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
    case syntax::Kind::Throw: {
        // [except.throw]/3 destroys the operand's temporaries before the throw, which no full expression here ends.
        const std::size_t was = fullFrom_;
        fullFrom_ = kNoFull;
        throwExpr(id);
        fullFrom_ = was;
        e = Val();
        break;
    }
    case syntax::Kind::New: skippable_++; e = newExpression(id); skippable_--; break;
    case syntax::Kind::Delete: skippable_++; e = deleteExpression(id); skippable_--; break;
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

// [expr.type.conv]/2: `T()` of a scalar is value-initialised - zero, a null pointer, `false`.
Lowering::Val Lowering::valueInitialised(sema::TypedId id) {
    const TypeId id0 = TypeTable::id(tree_.node(id).type);
    if (!tree_.node(id).folded.has) outside("a cast not recorded");
    if (!id0.valid() || !scalar(T(id0)) || T(id0).isX87(target_)) outside("a value-initialised cast to what is not a scalar");
    const TypeId ut = tid(*T(id0).unqualified());
    if (T(ut).isPointer()) return Val(result(add(Op::Null, ut, std::vector<ValueId>())), ut);
    if (!T(ut).isFloating()) return Val(iconst(ut, 0), ut);
    return Val(result(add(Op::FConst, ut, std::vector<ValueId>())), ut);
}

// A name's address: a local's alloca, a global's symbol; a reference holds the address it binds.
Lowering::Val Lowering::nameAddress(sema::TypedId id) {
    const sema::TypedNode &n = tree_.node(id);
    if (n.decl.valid()) {
        const std::map<std::uint32_t, std::pair<std::string, TypeId> >::const_iterator s = staticLocals_.find(n.decl.v);
        if (s != staticLocals_.end() && T(s->second.second).isReference()) {
            // A static reference is a global holding the address it binds, as a local reference's alloca does.
            const TypeId pt = tid(*types_.pointerTo(T(s->second.second).referent()));
            return Val(load(addrOf(s->second.first, tid(*types_.pointerTo(&T(pt)))), pt), tid(*T(s->second.second).referent()));
        }
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
    if (r.what == sema::Resolution::StaticMember && r.serial == 1) outside("a folded static member, which has no storage");
    if (r.what != sema::Resolution::Global && r.what != sema::Resolution::StaticMember) outside("the name '" + text(id) + "' is no object");
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
            // A scoped enumeration's enumerator is a constant of its type too; what may be done with it is checked where it is used.
            if (!r.type.valid() || !T(r.type).isArithmetic()) outside("an enumerator of no arithmetic type");
            return Val(iconst(r.type, r.value), r.type);
        }
        if (r.what == sema::Resolution::Function) {
            if (!r.type.valid() || !T(r.type).isFunction()) outside("a function named as a value whose type was not recorded");
            const TypeId pt = tid(*types_.pointerTo(&T(r.type)));
            return Val(addrOf(r.symbol, pt), pt);
        }
        // A static member folded in its class is its value; any other is the one object its symbol names.
        if (r.what == sema::Resolution::StaticMember && r.serial == 1) {
            if (!r.type.valid() || !scalarArithmetic(T(r.type)) || !T(r.type).isInteger()) outside("a folded static member of no integer type");
            const TypeId ut = tid(*T(r.type).unqualified());
            return Val(iconst(ut, r.value), ut);
        }
        if (r.what != sema::Resolution::Global && r.what != sema::Resolution::Member && r.what != sema::Resolution::StaticMember)
            outside("the name '" + text(id) + "', " + (r.what == sema::Resolution::StaticMember ? "a static member"
                                                        : r.what == sema::Resolution::This ? "this" : "of another kind"));
    }
    const Val a = nameAddress(id);
    const Type &t = T(a.type);
    if (t.isArray()) return decay(Val(a.v, a.type));
    // [conv.lval]/2: a std::nullptr_t object read is a null pointer constant - no value is fetched.
    if (t.isNullPtr()) return Val(result(add(Op::Null, tid(*t.unqualified()), std::vector<ValueId>())), tid(*t.unqualified()));
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
        const Val p = arrowPointer(id);
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
    if (m.ambiguous || m.anonUnion != nullptr || m.type->isReference() || m.width != 0)
        outside("a member '" + mname + "' the lowering does not reach");
    const Type &mt = obj.isConst() && !m.isMutable ? *types_.withConst(m.type) : *m.type;
    if (!scalar(mt) && !mt.isArray() && !mt.isStructOrUnion()) outside("a member of type '" + mt.describe() + "'");
    if (m.inVirtualBase != nullptr) return Val(virtualBaseMember(object.v, *obj.unqualified(), m, tid(mt)), tid(mt));
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
    Val self(this_, thisType_);
    // [expr.prim.lambda]/7: in a closure's body a name of the enclosing class is reached through the `this` it captured.
    if (T(thisType_).pointee()->unqualified()->findMember(text(id)) == nullptr) {
        const Val outer = capturedThis();
        if (outer.v.valid()) self = outer;
    }
    const Type &cls = *T(self.type).pointee()->unqualified();
    if (cls.findMember(text(id)) == nullptr) outside("a member reached through another object's `this`");
    const Member &m = *cls.findMember(text(id));
    if (m.offset != r.serial || !r.type.valid()) outside("a member reached through another object's `this`");
    if (m.ambiguous || m.anonUnion != nullptr || m.type->isReference() || m.width != 0)
        outside("a member '" + text(id) + "' the lowering does not reach");
    const Type &mt = T(r.type);
    if (!scalar(mt) && !mt.isArray() && !mt.isStructOrUnion()) outside("a member of type '" + mt.describe() + "'");
    if (m.inVirtualBase != nullptr) return Val(virtualBaseMember(self.v, cls, m, r.type), r.type);
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, self.v));
    at(g).k = m.offset;
    return Val(result(g), r.type);
}

// The address of a virtual base of the object at `object`, of class `owner`, on Itanium: moved by the vbase_offset
// behind its vptr, the slot the parser's record names - the walk the parser's virtualBaseMember makes.
ValueId Lowering::virtualBaseAddress(ValueId object, const Type &owner, const Type &vbase) {
    if (target_.microsoftNames()) outside("a virtual base reached through its vbtable");
    const long long slot = vbaseSlot_ ? vbaseSlot_(owner.id(), vbase.id()) : 0;
    if (slot == 0) outside("a virtual base the parser recorded no vbase_offset for");
    const TypeId chars = types_.pointerTo(types_.charType()->id());
    const ValueId vptr = load(result(add(Op::Bitcast, types_.pointerTo(chars), std::vector<ValueId>(1, object))), chars);
    const InstId back = add(Op::Gep, types_.pointerTo(ptrdiffType()), std::vector<ValueId>(1, vptr));
    at(back).k = slot;
    std::vector<ValueId> moved(1, result(add(Op::Bitcast, chars, std::vector<ValueId>(1, object))));
    moved.push_back(load(result(back), ptrdiffType()));
    return result(add(Op::Gep, chars, moved));
}

// A member of a virtual base: the base's address, then where the member sits inside it, which is constant.
ValueId Lowering::virtualBaseMember(ValueId object, const Type &owner, const Member &m, TypeId type) {
    int baseAt = -1;
    for (const Type::BaseSpec &b : owner.bases())
        if (b.isVirtual && b.type->unqualified() == m.inVirtualBase->unqualified()) baseAt = b.offset;
    if (baseAt < 0) outside("a member of a virtual base the class does not lay out");
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(&T(type))), std::vector<ValueId>(1, virtualBaseAddress(object, owner, *m.inVirtualBase)));
    at(g).k = m.offset - baseAt;
    return result(g);
}

// Whether a call's head `f` names the member function the parser reached, as `this->f()`: its last name is in the
// symbol where the ABI spells it; an operator() - a class object called by its name - is not.
bool Lowering::namesCalledFunction(const std::string &head, const std::string &symbol) const {
    std::string last = head.substr(head.rfind(':') == std::string::npos ? 0 : head.rfind(':') + 1);
    last = last.substr(0, last.find('<'));
    if (last.empty() || symbol.empty()) return false;
    // `operator=(o)`, `B::operator+(n)`: an operator function called by its name, which no object can be named.
    if (last.compare(0, 8, "operator") == 0 && (last.size() == 8 || !(std::isalnum(static_cast<unsigned char>(last[8])) || last[8] == '_')))
        return true;
    if (symbol[0] == '?')
        return symbol.compare(0, last.size() + 2, "?" + last + "@") == 0 || symbol.compare(0, last.size() + 4, "??$" + last + "@") == 0;
    if (symbol.find("clE") != std::string::npos || symbol.find("clI") != std::string::npos) return false;
    const std::string spelt = std::to_string(last.size()) + last;
    for (std::size_t at = symbol.find(spelt); at != std::string::npos; at = symbol.find(spelt, at + 1)) {
        const std::size_t end = at + spelt.size();
        if (at > 0 && std::isdigit(static_cast<unsigned char>(symbol[at - 1]))) continue;
        if (end < symbol.size() && (symbol[end] == 'E' || symbol[end] == 'I')) return true;
    }
    return false;
}

// The member object `m` of `m(x)`, found as `this`'s data member of that name - the class whose operator() the parser
// called, at its own offset; anything else (a global, a static member, an inherited ambiguity) stays outside.
Lowering::Val Lowering::functorMember(sema::TypedId head, const Type &self) {
    const Type &cls = *T(thisType_).pointee()->unqualified();
    if (cls.findMember(text(head)) == nullptr) outside("a class object called by its name");
    const Member &m = *cls.findMember(text(head));
    if (m.ambiguous || m.inVirtualBase != nullptr || m.anonUnion != nullptr || m.type->isReference() || m.width != 0 ||
        m.type->unqualified() != self.pointee()->unqualified())
        outside("a class object called by its name");
    const bool constant = T(thisType_).pointee()->isConst() && !m.isMutable;
    const Type &mt = constant ? *types_.withConst(m.type->unqualified()) : *m.type;
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(&mt)), std::vector<ValueId>(1, this_));
    at(g).k = m.offset;
    return Val(result(g), tid(*types_.pointerTo(&mt)));
}

// The enclosing object's address in a closure that captured `this`: its `$this` member, the parser's name for it;
// none (an invalid value) in any other function.
Lowering::Val Lowering::capturedThis() {
    if (!this_.valid() || T(thisType_).pointee()->unqualified()->findMember("$this") == nullptr) return Val();
    const Member &held = *T(thisType_).pointee()->unqualified()->findMember("$this");
    if (!held.type->isPointer() || !held.type->pointee()->isStructOrUnion() || held.width != 0) return Val();
    const TypeId pt = tid(*held.type->unqualified());
    const InstId g = add(Op::Gep, tid(*types_.pointerTo(&T(pt))), std::vector<ValueId>(1, this_));
    at(g).k = held.offset;
    return Val(load(result(g), pt), pt);
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
    // A class a call hands back is no value (expr) - `f()->m` reaches its operator->, which the lowering does not take.
    if (!v.type.valid()) outside("a class value read where a value is wanted");
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
    skippable_++;
    const Val b = truth(decay(expr(r)));
    skippable_--;
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
        // [expr.cond]/6: arms of one type give that type, as the typer says (the parser promotes and converts back).
        const bool oneType = tx.unqualified() == ty.unqualified();
        t = oneType ? tid(*tx.unqualified()) : usualArithmetic(tid(tx), tid(ty));
    } else if (&tx == &ty) {
        t = tid(tx);
    } else if (tx.unqualified() == ty.unqualified()) {
        t = tid(*tx.unqualified());
    } else if (tx.isPointer() && (ty.isNullPtr() || isNullConstant(child(id, 2)))) {
        t = tid(tx);
    } else if (ty.isPointer() && (tx.isNullPtr() || isNullConstant(child(id, 1)))) {
        t = tid(ty);
    } else if (tx.isPointer() && ty.isPointer() && compositePointer(id, tx, ty)) {
        t = tid(*tree_.node(id).type);
    } else {
        outside("a conditional of '" + tx.describe() + "' and '" + ty.describe() + "'");
    }
    place(a);
    skippable_++;
    const Val x = exprTo(child(id, 1), t);       // an arm that is the literal 0 is the null pointer, [conv.ptr]/1
    br(merge, std::vector<ValueId>(1, x.v));
    place(b);
    const Val y = exprTo(child(id, 2), t);
    skippable_--;
    br(merge, std::vector<ValueId>(1, y.v));
    place(merge);
    return Val(param(merge, t), t);
}

// [expr.cond]/6 with [expr]/13: two pointers whose composite pointer type the typer gave the node - the pointees one
// type apart in cv, a pointer to void, or a class and its base - each arm converted to it.
bool Lowering::compositePointer(sema::TypedId id, const Type &tx, const Type &ty) const {
    if (tree_.node(id).type == nullptr || !tree_.node(id).type->isPointer()) return false;
    const Type &c = *tree_.node(id).type;
    const Type &cp = *c.pointee()->unqualified();
    const auto fits = [&](const Type &a) -> bool {
        const Type &ap = *a.pointee();
        if (ap.isConst() && !c.pointee()->isConst()) return false;
        if (ap.unqualified() == &cp || (cp.isVoid() && !ap.isFunction())) return true;
        return ap.unqualified()->isStructOrUnion() && cp.isStructOrUnion();
    };
    return fits(tx) && fits(ty);
}

// `a = b` converts b to a's type; `a op= b` is `a = a op b` with a read once.
Lowering::Val Lowering::assign(sema::TypedId id) {
    const std::string &op = text(id);
    if (count(id) != 2) outside("an assignment of " + std::to_string(count(id)) + " parts");
    { Val c; if (classAssign(id, c)) return Val(); }     // a class's value is no value: the statement is the assignment
    const sema::TypedId tn = child(id, 0);
    if (!isLvalueNode(tn)) outside("an assignment to what is not an object");
    const Val target = lvalue(tn);
    const Type &t = T(target.type);
    // [class.copy]/18: a plain class's implicit copy assignment copies its bytes; a written operator= is a call.
    if (op == "=" && plainClass(t) && !t.isConst() && !(assigned_ && assigned_(t.id())) && objectNode(child(id, 1))) {
        Val from = object(child(id, 1));
        if (T(from.type).isStructOrUnion() && T(from.type).unqualified() != t.unqualified())
            from = baseSubobject(from, t, "a class assigned from another type");    // [class.copy]/12: its base's bytes
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

// [class.copy]/18 and /25 for a class that is not plain: the copy or move assignment the parser called, by its record,
// on the left object, the right bound to its parameter; or, where the class has no operator= of its own, the right
// object's bytes copied over the left's, as the parser's assignment copies them. `out` is the object it yields.
bool Lowering::classAssign(sema::TypedId id, Val &out) {
    const sema::TypedId tn = child(id, 0), vn = child(id, 1);
    const TypeId lt = TypeTable::id(tree_.node(tn).type);
    if (text(id) != "=" || !lt.valid() || !T(lt).isStructOrUnion() || !T(lt).isComplete() || plainClass(T(lt))) return false;
    const Type &t = *T(lt).unqualified();
    std::string symbol;
    TypeId sig;
    const bool called = assignCall_ && assignCall_(tree_.node(vn).syntax, symbol, sig);
    if (!called && assigned_ && assigned_(t.id())) return false;
    if (!isLvalueNode(tn)) outside("a class assigned to what is not an object");
    const Val target = lvalue(tn);
    if (T(target.type).isConst() || T(target.type).unqualified() != &t) outside("a class assigned to a const or another object");
    out = target;
    if (!called) {
        const Val from = classValue(vn, t);
        copyObject(target.v, from.v, t);
        return true;
    }
    const Type &ft = T(sig);
    if (!ft.isFunction() || ft.params().size() != 2 || !ft.params()[0]->isPointer() || ft.params()[0]->pointee()->unqualified() != &t ||
        !ft.params()[1]->isReference() || !ft.returns()->isPointer())
        outside("an assignment's operator= of another shape");
    CallInfo info;
    info.symbol = symbol;
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, target.v))));
    const Val r = callTail(sig, info, ops, 1, std::vector<sema::TypedId>(1, vn), false);
    out = Val(r.v, tid(t));      // [over.ass]: what the operator= returns, the object it names
    return true;
}

// A class value's object for a copy of its bytes: a temporary built in a slot of this frame, a call's result with a
// destructor (the full expression's to destroy), or any object objectNode takes.
Lowering::Val Lowering::classValue(sema::TypedId id, const Type &t) {
    if (!classObjectNode(id)) outside("a class assigned from what is not an object");
    Val v = classObject(id);
    if (T(v.type).isStructOrUnion() && T(v.type).unqualified() != t.unqualified())
        v = baseSubobject(v, t, "a class assigned from another type");    // [class.copy]/12: the base subobject's bytes
    if (T(v.type).unqualified() != t.unqualified()) outside("a class assigned from another type");
    return v;
}

// A call of the function the parser recorded, by its symbol, or through a pointer to a function.
// [conv.ptr]/3: a pointer to a class to one to its unambiguous non-virtual base, by the base's offset in the class,
// a null pointer kept null. A cast down, across, or to a virtual base stays outside.
Lowering::Val Lowering::toBase(Val v, TypeId to, bool cast) {
    const Type &from = *T(v.type).pointee()->unqualified(), &base = *T(to).pointee()->unqualified();
    // A virtual base, direct or not, which [class.mi]/4 makes one subobject however many paths reach it.
    for (const Type::BaseSpec &b : from.bases())
        if (b.isVirtual && b.type->unqualified() == &base) {
            const Val nonNull = truth(v);
            const BlockId step = newBlock(), null = newBlock(), merge = newBlock();
            condbr(nonNull.v, step, null);
            place(step);
            br(merge, std::vector<ValueId>(1, result(add(Op::Bitcast, to, std::vector<ValueId>(1, virtualBaseAddress(v.v, from, base))))));
            place(null);
            br(merge, std::vector<ValueId>(1, result(add(Op::Null, to, std::vector<ValueId>()))));
            place(merge);
            return Val(param(merge, to), to);
        }
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
    const bool sret = intoSret_;
    intoSret_ = false;
    const std::uint32_t n = count(id);
    if (n == 0) outside("a call of nothing");
    const sema::TypedId head = child(id, 0);
    const sema::Resolution &r = tree_.resolution(id);
    if (r.what == sema::Resolution::Temporary && !r.symbol.empty()) outside("a class temporary a constructor builds");
    if (r.what == sema::Resolution::Temporary || r.what == sema::Resolution::TypeName) outside("a class temporary");
    if (n == 1 && syn(head).kind == syntax::Kind::Member && pseudoDestructor(head)) return Val(ValueId(), tid(*types_.get(Kind::Void)));
    CallInfo info;
    info.sret = sret;
    std::vector<ValueId> ops;
    TypeId sig;
    // A call by name reaches the recorded function; one through a value - a local, a call's result - is read as such.
    const bool direct = r.what == sema::Resolution::Call && !r.symbol.empty() && !r.hasThis && !r.indirect && syn(head).kind == syntax::Kind::Name &&
                        !tree_.node(head).decl.valid() && r.symbol.find_first_of(" ()*&[") == std::string::npos;
    // A member function called by name, its object's address the first argument: `o.f()`, `p->f()`, `f()` for `this->f()`.
    if (r.what == sema::Resolution::Call && r.hasThis && r.indirect && r.vslot < 0) outside("a call through a member pointer");
    const bool member = r.what == sema::Resolution::Call && !r.symbol.empty() && r.hasThis &&
                        r.symbol.find_first_of(" ()*&[") == std::string::npos;
    const bool staticThrough = r.what == sema::Resolution::Call && !r.symbol.empty() && !r.hasThis && !r.indirect &&
                               syn(head).kind == syntax::Kind::Member && count(head) == 1 && r.symbol.find_first_of(" ()*&[") == std::string::npos;
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
            // A closure's body calling a member of the class whose `this` it captured: on that object.
            if (T(thisType_).pointee()->unqualified() != self.pointee()->unqualified()) {
                const Val outer = capturedThis();
                if (outer.v.valid()) object = outer;
            }
            // `m(x)` for a member object m: its operator(), on the member - never `this` ([over.call.object]).
            if (!namesCalledFunction(text(head), r.symbol)) object = functorMember(head, self);
        } else if (syn(head).kind == syntax::Kind::Member && count(head) == 1) {
            const bool arrow = text(head).compare(0, 2, "->") == 0;
            if (arrow) object = arrowPointer(head);
            else { const Val o = lvalue(child(head, 0)); object = Val(o.v, tid(*types_.pointerTo(&T(o.type)))); }
        } else if (syn(head).kind == syntax::Kind::Unary && text(head) == "*" && count(head) == 1) {
            // [over.call.object] on `*p`: the class object p points at is the object, p its address.
            object = decay(expr(child(head, 0)));
            if (!T(object.type).isPointer() || !T(object.type).pointee()->isStructOrUnion()) outside("a member call through '*' of what is not a pointer to a class");
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
        if (r.indirect) {
            ops.insert(ops.begin(), virtualCallee(ops[0], r.type, r.vslot));
            info.symbol.clear();
        }
    } else if (direct || staticThrough) {
        if (!r.type.valid() || !T(r.type).isFunction()) outside("a call whose function type was not recorded");
        // [expr.ref]/4: a static member function named through an object - the object is evaluated, and nothing more.
        if (staticThrough) {
            const sema::TypedId o = child(head, 0);
            if (text(head).compare(0, 2, "->") == 0) arrowPointer(head);
            else if (isLvalueNode(o)) lvalue(o);
            else outside("a static member function called through what is not an object");
        }
        info.symbol = r.symbol;
        sig = r.type;
    } else {
        // A data member holding a pointer to a function, read and called; a member function reaches no data member and stays outside.
        if (syn(head).kind == syntax::Kind::Member && count(head) != 1) outside("a member call");
        const Val callee = decay(expr(head));
        const Type &ct = T(callee.type);
        if (!ct.isPointer() || !ct.pointee()->isFunction()) outside("a call of what is not a function pointer");
        sig = tid(*ct.pointee());
        ops.push_back(callee.v);
    }
    std::vector<sema::TypedId> args;
    for (std::uint32_t i = 1; i < n; i++) args.push_back(child(id, i));
    intoNow_ = id.v == intoCall_.v ? intoAlloca_ : -1;
    return callTail(sig, info, ops, member ? 1 : 0, args, false);
}

// [class.virtual]: a virtual call reads the vptr at the object's first word and the function at the slot the parser
// recorded, as its own dispatch does on both ABIs; `self` is the object's address, already the slot's class.
ValueId Lowering::virtualCallee(ValueId self, TypeId fnType, int slot) {
    const TypeId fnPtr = tid(*types_.pointerTo(&T(fnType)));
    const TypeId table = tid(*types_.pointerTo(&T(fnPtr)));
    const ValueId vptr = load(result(add(Op::Bitcast, tid(*types_.pointerTo(&T(table))), std::vector<ValueId>(1, self))), table);
    const Val entry = pointerAdd(Val(vptr, table), Val(iconst(tid(*types_.get(Kind::Int)), slot), tid(*types_.get(Kind::Int))));
    return load(entry.v, fnPtr);
}

// A call once its function is known: the arguments by the parameters' rules, a class result's temporary, the call
// itself - an invoke inside a try. `skip` parameters are taken already (`this`); `dummyInt` is a postfix ++'s 0.
Lowering::Val Lowering::callTail(TypeId sig, CallInfo info, std::vector<ValueId> ops, std::size_t skip,
                                 const std::vector<sema::TypedId> &args, bool dummyInt) {
    const int into = intoNow_;      // the local elidedCall builds this call's result in, which no full expression destroys
    intoNow_ = -1;
    const Type &ft = T(sig);
    info.signature = sig;
    info.variadic = ft.isVariadicFn();
    info.namedArgs = static_cast<int>(ft.params().size());
    const Type &gives = *ft.returns();
    // info.sret: a class with a copy of its own built in this function's result object, which no scope here destroys.
    if (info.sret && !(copiedReturn(gives) && sret_.valid())) outside("a call built in place of what is not this function's result");
    const bool destroyed = !info.sret && destroyedResultClass(gives);
    if (!(gives.isVoid() || scalar(gives) || plainClass(gives) || bytesReturned(gives) || destroyed || info.sret ||
          (gives.isReference() && referable(*gives.referent()))))
        outside("a call returning '" + gives.describe() + "'");
    if (gives.isX87(target_)) outside("an x87 long double");
    std::string dtor;
    if (destroyed && into < 0) dtor = temporaryDestructor(*gives.unqualified());
    argumentList(ft, skip, info.variadic, args, dummyInt, ops);
    if (dummyInt) ops.push_back(iconst(tid(*types_.get(Kind::Int)), 0));      // [over.inc]/1: the postfix form's 0
    // A plain class comes back into a temporary of this frame, and the call carries its address, as a reference's does.
    ValueId resultSlot;
    if (gives.isStructOrUnion() && !info.sret && into >= 0) {
        info.resultAlloca = into;
    } else if (gives.isStructOrUnion() && !info.sret) {
        resultSlot = entryAlloca(tid(*gives.unqualified()), "$result");
        info.resultAlloca = static_cast<int>(F().frame.size() - 1);
    }
    Function &f = F();
    const std::uint32_t callIndex = static_cast<std::uint32_t>(f.calls.size());
    f.calls.push_back(info);
    const TypeId carries = gives.isVoid() ? TypeId() : gives.isReference() ? tid(*types_.pointerTo(gives.referent()))
                         : gives.isStructOrUnion() ? tid(*types_.pointerTo(gives.unqualified())) : tid(gives);
    const InstId c = callOrInvoke(carries, ops);
    at(c).call = callIndex;
    // [class.temporary]/3: a result with a destructor is the caller's, alive once the call has returned it.
    if (destroyed && into < 0) guard(resultSlot, *gives.unqualified(), dtor, landingPads_);
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
            // [expr.call]/7: a std::nullptr_t through `...` is a void *.
            if (t.isNullPtr()) { ops.push_back(result(add(Op::Null, tid(*types_.pointerTo(types_.voidType())), std::vector<ValueId>()))); continue; }
            if (!scalarArithmetic(t)) outside("a variadic argument of type '" + t.describe() + "'");
            a = t.kind() == Kind::Float ? convert(a, tid(*types_.doubleType())) : promote(a);
            ops.push_back(a.v);
            continue;
        }
        const Type &p = *ft.params()[i + skip];
        if (p.isStructOrUnion() && !plainClass(p) && !bytesReturned(p)) { ops.push_back(byValueArgument(an, p)); continue; }
        if (plainClass(p) || bytesReturned(p)) {
            // A plain class by value: its address, which the selector hands the walker as the object to copy in; one with
            // constructors and no copy of its own goes the same way, a temporary of it built in a slot of this frame first.
            ValueId made;
            if (convertedTemporary(an, p, false, made)) {
                ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(p.unqualified())), std::vector<ValueId>(1, made))));
                continue;
            }
            if (!classObjectNode(an)) outside("a class argument that is not an object");
            const Val a = classObject(an);
            if (T(a.type).unqualified() != p.unqualified()) outside("a class argument of another type");
            ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(p.unqualified())), std::vector<ValueId>(1, a.v))));
            continue;
        }
        if (p.isReference()) {
            // [dcl.init.ref]/5: an object of the referent's type, its address passed; a scalar value bound to a const
            // or an rvalue reference is a temporary of the frame holding it, alive until the call is done.
            const Type &ref = *p.referent();
            ValueId made;
            if ((ref.isConst() || p.isRValueReference()) && convertedTemporary(an, ref, true, made)) {
                ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(&ref)), std::vector<ValueId>(1, made))));
                continue;
            }
            if (!isLvalueNode(an) && scalar(ref) && !ref.isX87(target_) && (ref.isConst() || p.isRValueReference())) {
                const ValueId temp = entryAlloca(tid(*ref.unqualified()), "$bound");
                store(temp, exprTo(an, tid(*ref.unqualified())).v);
                ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(&ref)), std::vector<ValueId>(1, temp))));
                continue;
            }
            // A class prvalue bound to one - a temporary or a closure in a slot of the frame, alive to the call's end;
            // neither has a destructor for the full expression's end to run.
            if (!isLvalueNode(an) && (ref.isConst() || p.isRValueReference()) && (temporaryNode(an) || closureNode(an))) {
                const Val a = closureNode(an) ? closure(an) : temporary(an);
                if (T(a.type).unqualified() != ref.unqualified()) outside("a reference parameter bound to a temporary of another type");
                ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(&ref)), std::vector<ValueId>(1, a.v))));
                continue;
            }
            // A call's result with a destructor, the caller's temporary to its full expression's end, bound the same way.
            if (!isLvalueNode(an) && (ref.isConst() || p.isRValueReference()) && destroyedResult(an)) {
                const Val a = syn(an).kind == syntax::Kind::Call ? call(an) : operatorCall(an);
                if (!a.type.valid() || !T(a.type).isPointer() || T(a.type).pointee()->unqualified() != ref.unqualified())
                    outside("a reference parameter bound to a call's result of another type");
                ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(&ref)), std::vector<ValueId>(1, a.v))));
                continue;
            }
            if (!isLvalueNode(an)) outside("a reference parameter bound to what is not an object");
            if (scalar(ref) && !ref.isX87(target_) && (ref.isConst() || p.isRValueReference()) && tree_.node(an).type != nullptr &&
                tree_.node(an).type->unqualified() != ref.unqualified()) {
                ops.push_back(boundConverted(an, p));
                continue;
            }
            Val a = lvalue(an);
            if (arrayBinds(T(a.type), ref)) {
                ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(&ref)), std::vector<ValueId>(1, a.v))));
                continue;
            }
            if (T(a.type).isStructOrUnion() && ref.isStructOrUnion() && T(a.type).unqualified() != ref.unqualified())
                a = baseOfObject(a, p);
            if (T(a.type).unqualified() != p.referent()->unqualified()) outside("a reference parameter bound to another type");
            if (T(a.type).isConst() && !p.referent()->isConst()) outside("a reference that can write bound to a const");
            if (!referable(T(a.type))) outside("a reference parameter to '" + p.referent()->describe() + "'");
            ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(p.referent())), std::vector<ValueId>(1, a.v))));
            continue;
        }
        // [conv.ptr]/1: a std::nullptr_t parameter takes a null pointer constant, a value with nothing in it to read.
        if (p.isNullPtr()) {
            if (tree_.node(an).type != nullptr && tree_.node(an).type->isNullPtr()) expr(an);
            else if (!isNullConstant(an)) outside("a std::nullptr_t parameter given what is not a null pointer constant");
            ops.push_back(result(add(Op::Null, tid(*p.unqualified()), std::vector<ValueId>())));
            continue;
        }
        if (!scalar(p) || p.isX87(target_)) outside("a parameter of type '" + p.describe() + "'");
        ops.push_back(exprTo(an, tid(p)).v);
    }
}

// [dcl.init]/17 for a by-value parameter of a class that is not its bytes, as Parser::materialiseCopy makes it: the caller's
// copy by address where copying or destroying it is a call (Itanium) or copying it is (Microsoft) - by the recorded constructor,
// or built by the producing call ([class.copy]/31) - destroyed at the full expression's end on Itanium; else its bytes, the callee destroying.
ValueId Lowering::byValueArgument(sema::TypedId an, const Type &p) {
    const Type &c = *p.unqualified();
    if (!c.isComplete() || c.kind() == Kind::Union || c.hasVirtualBase()) outside("a parameter of type '" + p.describe() + "'");
    const bool ms = target_.microsoftNames();
    if (!c.nonTrivialCopy()) {
        const Val src = classArgumentSource(an, c);
        if (ms) return result(add(Op::Bitcast, tid(*types_.pointerTo(&c)), std::vector<ValueId>(1, src.v)));
        const std::string dtor = temporaryDestructor(c);
        const ValueId copy = entryAlloca(tid(c), "$copy");
        copyObject(copy, src.v, c);
        guard(copy, c, dtor);
        return copy;
    }
    std::string symbol;
    TypeId sig;
    bool elided = false;
    if (!argumentCopy_ || !argumentCopy_(tree_.node(an).syntax, symbol, sig, elided)) outside("a by-value argument whose copy the parser recorded none for");
    const std::string dtor = !ms && c.hasDestructor() ? temporaryDestructor(c) : std::string();
    const ValueId copy = entryAlloca(tid(c), "$copy");
    if (elided) {
        // The producing call builds its result in the copy, as elidedCall's does in a local: no full expression destroys it.
        sema::TypedId made = an;
        while (syn(made).kind == syntax::Kind::Paren && count(made) == 1) made = child(made, 0);
        if (syn(made).kind != syntax::Kind::Call && !operatorCalls(made)) outside("a by-value argument built in place from what is not a call");
        const sema::TypedId wasCall = intoCall_;
        const int wasAlloca = intoAlloca_, idx = static_cast<int>(F().inst(F().values[copy.v].def).k);
        intoCall_ = made;
        intoAlloca_ = idx;
        const Val r = operatorCalls(made) ? operatorCall(made) : call(made);
        intoCall_ = wasCall;
        intoAlloca_ = wasAlloca;
        if (!r.v.valid() || F().calls.empty() || F().calls.back().resultAlloca != idx) outside("a by-value argument's call that did not come back into its copy");
    } else {
        const Type &ft = T(sig);
        if (!ft.isFunction() || ft.params().size() != 2 || ft.params()[0]->pointee()->unqualified() != &c) outside("a by-value argument copied by a constructor of another shape");
        std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, copy))));
        argumentList(ft, 1, false, std::vector<sema::TypedId>(1, an), false, ops);
        directCall(symbol, sig, ops);
    }
    if (!dtor.empty()) guard(copy, c, dtor);
    return copy;
}

// The object a by-value class argument of `c` is copied from, its address: an lvalue - a call's result in its slot of this
// frame among them, destroyed at the full expression's end - or a temporary the parser's constructor built.
Lowering::Val Lowering::classArgumentSource(sema::TypedId an, const Type &c) {
    while (syn(an).kind == syntax::Kind::Paren && count(an) == 1) an = child(an, 0);
    Val a;
    if (isLvalueNode(an)) {
        a = lvalue(an);
    } else if (temporaryNode(an)) {
        a = temporary(an);
    } else if (destroyedResult(an)) {
        const Val r = syn(an).kind == syntax::Kind::Call ? call(an) : operatorCall(an);
        if (!r.type.valid() || !T(r.type).isPointer()) outside("a class argument from a call's result of another shape");
        a = Val(r.v, tid(*T(r.type).pointee()));
    } else {
        outside("a class argument that is not an object");
    }
    if (T(a.type).unqualified() != &c) outside("a class argument of another type");
    return a;
}

// Microsoft's rule for a by-value class parameter with a destructor: the callee destroys its own copy on every way out, a
// cleanup funclet's when its body unwinds - a guard like a local's, below every other object; Itanium's caller destroys.
void Lowering::guardParameter(ValueId pv, const Type &pt, bool isCtor) {
    const Type &c = *pt.unqualified();
    if (!c.isComplete() || c.kind() == Kind::Union || c.hasVirtualBase()) outside("a parameter of type '" + pt.describe() + "'");
    if (!target_.microsoftNames() || !c.hasDestructor()) return;
    if (isCtor) outside("a by-value parameter with a destructor of a constructor on the Microsoft ABI");
    const std::string dtor = destructor_ ? destructor_(c.id()) : std::string();
    if (dtor.empty()) outside("a parameter whose destructor the parser recorded none for");
    guard(pv, c, dtor);
}

// [dcl.init.ref]/5: a scalar lvalue of another type bound to a const or an rvalue reference - converted into a
// temporary of the frame, which the reference binds, alive until the call is done.
ValueId Lowering::boundConverted(sema::TypedId an, const Type &p) {
    const Type &ref = *p.referent();
    const ValueId temp = entryAlloca(tid(*ref.unqualified()), "$bound");
    store(temp, exprTo(an, tid(*ref.unqualified())).v);
    return result(add(Op::Bitcast, tid(*types_.pointerTo(&ref)), std::vector<ValueId>(1, temp)));
}

// [over.ics.user]: a value of another type made a temporary of class `c` in a slot of the frame by the converting
// constructor the parser chose - with a destructor, alive to the full expression's end; `slot` its address. False
// where the parser built none; `destroys`, whether a class with a destructor may be made.
bool Lowering::convertedTemporary(sema::TypedId an, const Type &c, bool destroys, ValueId &slot) {
    std::string symbol;
    TypeId sig;
    if (!c.isStructOrUnion() || !convertedBuilt_ || !convertedBuilt_(tree_.node(an).syntax, symbol, sig)) return false;
    if (tree_.node(an).type == nullptr || tree_.node(an).type->unqualified() == c.unqualified()) outside("a converted value of its own class");
    const bool destroyed = destroys && destroyedClass(c);
    if (!destroyed && !constructedClass(c)) outside("a converted value of a class the lowering does not build");
    const Type &ft = T(sig);
    if (!ft.isFunction() || ft.params().size() != 2 || ft.params()[0]->pointee()->unqualified() != c.unqualified())
        outside("a converted value built by a constructor of another shape");
    const std::string dtor = destroyed ? temporaryDestructor(*c.unqualified()) : std::string();
    slot = entryAlloca(tid(*c.unqualified()), "$tmp");
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, slot))));
    argumentList(ft, 1, false, std::vector<sema::TypedId>(1, an), false, ops);
    directCall(symbol, sig, ops);
    if (destroyed) guard(slot, *c.unqualified(), dtor);
    return true;
}

// [dcl.init.ref]/5: an array lvalue bound to a reference to an array of its shape, the element gaining const at most.
bool Lowering::arrayBinds(const Type &from, const Type &referent) {
    return from.isArray() && referent.isArray() && from.sameArrayShape(&referent) &&
           from.innermostElement()->unqualified() == referent.innermostElement()->unqualified() &&
           (referent.innermostElement()->isConst() || !from.innermostElement()->isConst());
}

// [dcl.init.ref]/5: an lvalue of a class bound to a reference to its unambiguous non-virtual base - the base subobject.
Lowering::Val Lowering::baseOfObject(Val a, const Type &p) { return baseSubobject(a, *p.referent(), "a reference parameter bound to another type"); }

// The unambiguous non-virtual base `base` of the class object at `a`, at its offset, keeping a's const - an object is
// never null, so there is nothing to test; refused as `why` where there is no one such base.
Lowering::Val Lowering::baseSubobject(Val a, const Type &base, const std::string &why) {
    int found = 0, offset = 0;
    basePaths(*T(a.type).unqualified(), *base.unqualified(), 0, found, offset);
    if (found != 1) outside(why);
    const Type &bt = T(a.type).isConst() ? *types_.withConst(base.unqualified()) : *base.unqualified();
    const TypeId pt = tid(*types_.pointerTo(&bt));
    if (offset == 0) return Val(result(add(Op::Bitcast, pt, std::vector<ValueId>(1, a.v))), tid(bt));
    const InstId g = add(Op::Gep, pt, std::vector<ValueId>(1, a.v));
    at(g).k = offset;
    return Val(result(g), tid(bt));
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
    const bool sret = intoSret_;
    intoSret_ = false;
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
    info.sret = sret;
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
    intoNow_ = id.v == intoCall_.v ? intoAlloca_ : -1;
    return callTail(r.type, info, ops, skip, operands, dummyInt);
}

// The parser's facts for a `new` or `delete` node, which the lowering takes only for a scalar or a plain class.
sema::NewFacts Lowering::newFacts(sema::TypedId id, const std::string &what) {
    sema::NewFacts f;
    if (!newFacts_ || !newFacts_(tree_.node(id).syntax, f) || (f.why.empty() && f.symbol.empty()))
        outside(std::string("a '") + what + "' not recorded");
    if (!f.why.empty()) outside(f.why);
    const Type &t = T(f.made);
    if (t.isX87(target_) || !(scalar(t) || plainClass(t) || constructedClass(t) || newBuilt(t)))
        outside(std::string("a '") + what + "' of '" + t.describe() + "'");
    return f;
}

// [expr.new] for a scalar or a plain class: the global allocation function the parser called, given the bytes - n of
// them times the size for an array, counted first - then the initialiser's stores: none, zeroes, or the one value.
// A class with constructors takes the constructor the parser chose, called on the storage with the trailing parts.
Lowering::Val Lowering::newExpression(sema::TypedId id) {
    const sema::NewFacts f = newFacts(id, "new");
    const Type &made = T(f.made);
    const bool copied = f.bytesCopied && made.isStructOrUnion();     // `new T(t)`, [class.copy]/12: t's bytes, no constructor
    const bool loop = !f.vectorLoop.empty() && f.array && newBuilt(made);     // `new T[n]`: each element by the parser's loop
    const bool built = !copied && !loop && (constructedClass(made) || newBuilt(made));
    if (built && f.ctor.empty()) outside("a 'new' of a class with constructors");
    if (built && f.array) outside("an array 'new' of a class with constructors");
    if (built && f.ctorDefaulted) outside("a 'new' with default arguments");
    // [expr.new]/20: a constructor that throws has the storage freed, by a pad where the parser made pads; a placement frees nothing.
    const bool frees = (built || loop) && !f.placed && landingPads_;
    if ((built || loop) && !pads_.empty() && !f.placed && !landingPads_) outside("a 'new' of a class with constructors inside a try");
    const std::uint32_t lead = f.placed ? 1u : 0u;        // the placement's address is the first part
    const std::uint32_t argc = built ? static_cast<std::uint32_t>(T(f.ctorSig).params().size()) - 1u : (f.init == 2 || copied ? 1u : 0u);
    const std::uint32_t parts = lead + (f.array ? 1u : 0u) + argc;
    if (count(id) != parts) outside("a 'new' of " + std::to_string(count(id)) + " parts");
    const TypeId sizeT = tid(*types_.get(target_.sizeType()));
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    ValueId at0 = f.placed ? exprTo(child(id, 0), voidPtr).v : ValueId();
    ValueId bytes = iconst(sizeT, made.size(target_)), n;
    if (f.array) {
        std::vector<ValueId> ops;
        n = exprTo(child(id, lead), sizeT).v;
        ops.push_back(n);
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
    if (loop) {
        // [expr.new]/17 and /20: the elements built by the loop, the storage given back by operator delete[] if one throws.
        const std::size_t mark = live_.size();
        if (frees) guard(p, made, target_.microsoftNames() ? "??_V@YAXPEAX@Z" : "_ZdaPv");
        std::vector<TypeId> params;
        params.push_back(pt);
        params.push_back(sizeT);
        std::vector<ValueId> ops;
        ops.push_back(p);
        ops.push_back(n);
        directCall(f.vectorLoop, types_.functionType(types_.voidType()->id(), params), ops);
        if (frees) closeScope(mark, true);
    } else if (built) {
        const std::size_t mark = live_.size();
        if (frees) guard(p, made, target_.microsoftNames() ? "??3@YAXPEAX@Z" : "_ZdlPv");
        construct(p, f.ctor, f.ctorSig, made, f.ctorZero, id, lead);
        if (frees && live_.size() != mark + 1) outside("a 'new' whose constructor's arguments outlive it");
        if (frees) closeScope(mark, true);      // the object made: the storage's guard ends without a free
    } else if (copied) {
        const sema::TypedId v = child(id, parts - 1);
        if (!isLvalueNode(v)) outside("a 'new' of a class copied from what is not an object");
        const Val o = lvalue(v);
        if (T(o.type).unqualified() != made.unqualified()) outside("a 'new' of a class copied from another type");
        copyObject(p, o.v, made);
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
    sema::NewFacts d;
    if (newFacts_ && newFacts_(tree_.node(id).syntax, d) && d.why.empty() && (!d.dtor.empty() || d.dslot >= 0)) return deleteDestroying(id, d);
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

// [expr.delete]/6 for a class with a destructor, as the parser built it: a null pointer is not destroyed (/2); the
// destructor the parser called, then the global operator delete - or, a virtual one, the deleting slot it read, which
// frees as well: Itanium's D0, Microsoft's ??_G given 1.
Lowering::Val Lowering::deleteDestroying(sema::TypedId id, const sema::NewFacts &f) {
    if (count(id) != 1) outside("a 'delete' of " + std::to_string(count(id)) + " parts");
    const Val p = decay(expr(child(id, 0)));
    if (!T(p.type).isPointer() || T(p.type).pointee()->unqualified() != T(f.made).unqualified()) outside("a 'delete' of another type");
    const Type &ft = T(f.dtorSig);
    if (!ft.isFunction() || ft.params().empty() || !ft.params()[0]->isPointer()) outside("a destructor whose type was not recorded");
    const BlockId run = newBlock(), done = newBlock();
    condbr(truth(p).v, run, done);
    place(run);
    std::vector<ValueId> ops(1, result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, p.v))));
    CallInfo info;
    info.symbol = f.dtor;
    info.signature = f.dtorSig;
    info.namedArgs = static_cast<int>(ft.params().size());
    if (f.dslot >= 0) {
        if (ft.params().size() > 1) ops.push_back(iconst(tid(*ft.params()[1]), 1));
        ops.insert(ops.begin(), virtualCallee(ops[0], f.dtorSig, f.dslot));
        info.symbol.clear();
    }
    if (ops.size() != ft.params().size() + (info.symbol.empty() ? 1 : 0)) outside("a destructor of another shape");
    const std::uint32_t callIndex = static_cast<std::uint32_t>(F().calls.size());
    F().calls.push_back(info);
    const InstId c = callOrInvoke(ft.returns()->isVoid() ? TypeId() : tid(*ft.returns()), ops);
    at(c).call = callIndex;
    if (!terminated()) br(done);
    place(done);
    if (f.dslot >= 0) return Val(ValueId(), tid(*types_.voidType()));
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    CallInfo release;
    release.symbol = f.symbol;
    release.signature = types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, voidPtr));
    release.namedArgs = 1;
    const std::uint32_t freeIndex = static_cast<std::uint32_t>(F().calls.size());
    F().calls.push_back(release);
    const InstId g = add(Op::Call, TypeId(), std::vector<ValueId>(1, result(add(Op::Bitcast, voidPtr, std::vector<ValueId>(1, p.v)))));
    at(g).call = freeIndex;
    return Val(ValueId(), tid(*types_.voidType()));
}

// ----- exceptions (M3-IR 3.6, the Itanium half)

// A call inside a try's body or a handler is an invoke: it continues in a fresh block and unwinds to the innermost pad.
InstId Lowering::callOrInvoke(TypeId carries, const std::vector<ValueId> &ops) {
    if (pads_.empty()) return add(Op::Call, carries, ops);
    const BlockId pad = padBlock(pads_.back());
    const BlockId normal = newBlock();
    const InstId c = add(Op::Invoke, carries, ops);
    // Microsoft: a pad opened inside a handler's funclet is a state of its own there, which the selector's rows name;
    // one enclosing the try is not - the call stays in the handler's state (M3-IR 3.6).
    if (funclet_.valid() && pads_.size() > funcletPads_) at(c).bits = 1;
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
    // Inside a Microsoft handler a pad is a cleanup funclet of its own, emitted apart from the handler's blocks.
    const BlockId was = funclet_;
    funclet_ = BlockId();
    p.block = newBlock();
    funclet_ = was;
    F().blockArena[p.block.v].pad = true;
    p.ptr = param(p.block, tid(*types_.pointerTo(types_.voidType())));
    p.sel = param(p.block, tid(*types_.get(Kind::Int)));
    return p.block;
}

// A virtual base's cleanup destroys it only where its constructor's count says it was built (msGuardedVirtualBase): the
// funclet's own blocks, tagged with its pad so the selector emits them inside it, and the cleanupret after them.
void Lowering::msCountedDestroy(const Live &l, BlockId pad) {
    const BlockId was = funclet_;
    funclet_ = pad;
    const BlockId yes = newBlock(), after = newBlock();
    funclet_ = was;
    const TypeId intT = types_.intType()->id();
    std::vector<ValueId> cmp;
    cmp.push_back(load(l.when, intT));
    cmp.push_back(iconst(intT, l.above));
    const InstId c = add(Op::ICmp, tid(*types_.get(Kind::Bool)), cmp);
    at(c).cc = Cmp::Sgt;
    condbr(result(c), yes, after);
    place(yes);
    destroy(l);
    br(after);
    place(after);
}

// The state a pad made inside a Microsoft handler chains to (the walker's toState): a pad opened inside the funclet,
// else the handler's own, whose state goes on to the try's parent; outside a handler, the innermost pad or none.
BlockId Lowering::msEnclosingPad() {
    if (pads_.size() > funcletPads_) return padBlock(pads_.back());
    return funclet_;
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
    if (tree_.node(operand).type != nullptr && plainClass(*tree_.node(operand).type->unqualified()) && objectNode(operand)) {
        v = object(operand);        // `throw e`, `throw P()`: the object whose bytes the exception object takes
    } else {
        if (tree_.node(operand).type != nullptr && tree_.node(operand).type->unqualified()->isStructOrUnion())
            outside("a throw of a class that is not plain");
        v = decay(expr(operand));
        if (!v.type.valid() || !scalar(T(v.type)) || T(v.type).isX87(target_)) outside("a throw of what is not a scalar");
    }
    thrown = T(v.type).unqualified()->id();
    return v;
}

// [except.throw]/3 for a class a constructor builds: the exception object - the runtime's on Itanium, the frame's on
// Microsoft - copied by the copy constructor the parser chose, or by its bytes where the copy is trivial, from an object or
// from a temporary built here that dies after it; where the parser made pads, those of a copy that throws. False otherwise.
bool Lowering::classThrow(sema::TypedId id, ValueId &object, TypeId &thrown) {
    if (count(id) != 1) return false;
    const sema::TypedId operand = child(id, 0);
    const TypeId ot = TypeTable::id(tree_.node(operand).type);
    if (!ot.valid() || !T(ot).unqualified()->isStructOrUnion() || plainClass(*T(ot).unqualified())) return false;
    const Type &t = *T(ot).unqualified();
    std::string copy;
    TypeId sig;
    bool defaulted = false;
    // No copy constructor recorded: a class whose copy is trivial, which the parser stores as bytes ([class.copy]/12).
    const bool bytes = !memberBuilt_ || !memberBuilt_(key_, "$throw", syn(id).loc, copy, sig, defaulted);
    if (bytes && (t.nonTrivialCopy() || t.polymorphic())) outside("a throw of a class that is not plain");
    if (t.hasVirtualBase()) outside("a throw of a class with a virtual base");
    if (!bytes) {
        const Type &ft = T(sig);
        if (ft.params().size() != 2 || ft.params()[0]->pointee()->unqualified() != &t || !ft.params()[1]->isReference() ||
            ft.params()[1]->referent()->unqualified() != &t)
            outside("a throw copied by a constructor of another class");
    }
    const bool temporaryOperand = syn(operand).kind == syntax::Kind::Call && count(operand) != 0 && temporaryBuilt_ &&
                                  tree_.resolution(operand).what == sema::Resolution::Temporary &&
                                  tree_.resolution(child(operand, 0)).what == sema::Resolution::TypeName;
    if (!isLvalueNode(operand) && !temporaryOperand) outside("a throw of a class value that is not an object or a temporary");
    const std::string dtor = t.hasDestructor() && destructor_ ? destructor_(t.id()) : std::string();
    if (t.hasDestructor() && dtor.empty()) outside("a throw of a class whose destructor the parser recorded none for");
    thrown = t.id();
    const std::size_t mark = live_.size();
    // The storage is the parser's temporary only where the copy is a constructor's call that may throw.
    const bool storageGuard = !bytes && !target_.microsoftNames() && landingPads_;
    if (target_.microsoftNames()) {
        object = entryAlloca(thrown, ".ex");
    } else {
        const TypeId size = tid(*types_.get(target_.sizeType()));
        const Val got = runtimeCall("__cxa_allocate_exception", tid(*types_.pointerTo(types_.voidType())),
                                    std::vector<ValueId>(1, iconst(size, t.size(target_))));
        object = result(add(Op::Bitcast, types_.pointerTo(thrown), std::vector<ValueId>(1, got.v)));
        if (storageGuard) guard(object, t, "__cxa_free_exception");
    }
    ValueId from;
    if (isLvalueNode(operand)) {
        const Val o = lvalue(operand);
        if (T(o.type).unqualified() != &t) outside("a throw of an object of another class");
        if (!bytes && T(o.type).isConst() && !T(sig).params()[1]->referent()->isConst()) outside("a throw whose copy drops const");
        from = o.v;
    } else {
        // A polymorphic class's constructor stores its own vptr: the temporary is that call on a slot of the frame.
        if (!constructed_ || !constructed_(t.id()) || !buildableLayout(t, true)) outside("a throw of a temporary the lowering cannot build");
        std::string symbol;
        TypeId built;
        bool zero = false;
        if (!temporaryBuilt_(tree_.node(operand).syntax, symbol, built, zero)) outside("a thrown temporary built by no constructor the parser recorded");
        if (text(operand) == "{}") outside("a thrown temporary list-initialised");
        from = entryAlloca(thrown, "$tmp");
        // Its arguments' temporaries are the operand's: alive to the copy below, which destroys them before the throw.
        const std::size_t argMark = live_.size(), wasFull = fullFrom_;
        fullFrom_ = argMark;
        construct(from, symbol, built, t, zero, operand, 1);
        fullFrom_ = wasFull;
        if (live_.size() != argMark && !landingPads_) outside("a thrown temporary whose arguments make temporaries, with no landing pads");
        if (t.hasDestructor() && landingPads_) guard(from, t, dtor);
    }
    const bool dies = !isLvalueNode(operand) && t.hasDestructor();
    if (bytes) {
        copyObject(object, from, t);
    } else {
        const Type &ft = T(sig);
        std::vector<ValueId> ops;
        ops.push_back(result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, object))));
        ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(ft.params()[1]->referent())), std::vector<ValueId>(1, from))));
        directCall(copy, sig, ops);
    }
    // [except.throw]/3: the object made, the operand's temporaries destroyed, last made first; the storage's guard ends without a free.
    closeScope(mark + (storageGuard ? 1 : 0));
    closeScope(mark, true);
    if (dies && !landingPads_) {
        const TypeId self = types_.pointerTo(t.id());
        directCall(dtor, types_.functionType(types_.voidType()->id(), std::vector<TypeId>(1, self)), std::vector<ValueId>(1, from));
    }
    return true;
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
    ValueId built;
    if (classThrow(id, built, thrown)) {
        if (!typeInfo_) outside("a throw with no type_info to name");
        const std::string info = typeInfo_(thrown);
        if (info.empty()) outside("a throw of a type with no type_info");
        const std::string dtor = T(thrown).hasDestructor() && destructor_ ? destructor_(thrown) : std::string();
        std::vector<ValueId> args;
        args.push_back(result(add(Op::Bitcast, voidPtr, std::vector<ValueId>(1, built))));
        args.push_back(addrOf(info, voidPtr));
        args.push_back(dtor.empty() ? result(add(Op::Null, voidPtr, std::vector<ValueId>())) : addrOf(dtor, voidPtr));
        throwCall("__cxa_throw", args);
        return;
    }
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
    ValueId object;
    if (!classThrow(id, object, thrown)) {
        const Val v = thrownValue(id, thrown);
        object = entryAlloca(thrown, ".ex");
        if (T(thrown).isStructOrUnion()) copyObject(object, v.v, T(thrown));
        else store(object, convert(v, thrown).v);
    }
    if (!msThrowInfo_) outside("a throw with no ThrowInfo to name");
    const std::string info = msThrowInfo_(thrown);
    if (info.empty()) outside("a throw of a type whose ThrowInfo the parser did not finish");
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

// [except.handle]/16 for a jump out of n Itanium handlers: each handler's own objects destroyed, then its catch ended,
// innermost first; then what the jump leaves above `mark` outside them.
void Lowering::leaveCatches(std::size_t n, std::size_t mark) {
    if (n > handlerLive_.size()) outside("a jump out of handlers the lowering did not open");
    std::size_t upper = live_.size();
    for (std::size_t h = 0; h < n; h++) {
        const std::size_t from = handlerLive_[handlerLive_.size() - 1 - h];
        for (std::size_t k = upper; k-- > from; ) if (live_[k].slot.v != elidedSlot_.v) destroy(live_[k]);
        endCatches(1);
        upper = from;
    }
    for (std::size_t k = upper; k-- > mark; ) if (live_[k].slot.v != elidedSlot_.v) destroy(live_[k]);
}

// **A `try` and its handlers, as the parser writes them for the Itanium ABI**: the body's calls unwind to one pad, whose
// chain tests the selector against each handler's index and hands what nothing matched to the enclosing pad or to
// _Unwind_Resume; a handler begins its catch, binds its object, and ends it on every way out (a jump: endCatches; a throw: its own cleanup pad).
void Lowering::tryStatement(sema::TypedId id) {
    if (target_.microsoftNames()) { microsoftTry(id); return; }
    const std::uint32_t n = count(id);
    if (n < 2) outside("a try with no handler");
    std::vector<sema::TypedId> handlers;
    for (std::uint32_t i = 1; i < n; i++) handlers.push_back(child(id, i));
    TryState st;
    tryOpen(handlers, st);
    block(child(id, 0));
    tryClose(st, false);
}

// What an Itanium handler takes by value: a scalar that is no pointer, or a class copied by the copy constructor the parser
// recorded for the handler, or by its bytes ([class.copy]/12) - with its destructor known where it has one.
bool Lowering::caughtByValue(sema::TypedId handler, const Type &c) {
    if (!c.isStructOrUnion()) return scalar(c) && !c.isPointer() && !c.isX87(target_);
    if (!c.isComplete() || c.kind() == Kind::Union || c.hasVirtualBase()) return false;
    std::string copy;
    TypeId sig;
    if (!(caughtCopy_ && caughtCopy_(tree_.node(handler).syntax, copy, sig)) && (c.nonTrivialCopy() || c.polymorphic())) return false;
    return !c.hasDestructor() || (destructor_ && !destructor_(c.id()).empty());
}

// [except.handle]/3: the caught object copy-initialised from the exception object by its copy constructor, which ends the
// program should it throw - a catch-all row's pad, as the parser's terminate scope around the copy is.
void Lowering::caughtCopied(ValueId slot, ValueId from, const std::string &copy, TypeId sig) {
    const Type &ft = T(sig);
    if (ft.params().size() != 2 || !ft.params()[0]->isPointer() || !ft.params()[1]->isReference())
        outside("a catch copied by a constructor of another shape");
    Pad t;
    t.cleanup = false;
    t.own = 1;
    t.types.push_back(std::string());
    t.indices.push_back(typeIndexFor(std::string()));
    pads_.push_back(t);
    std::vector<ValueId> ops;
    ops.push_back(result(add(Op::Bitcast, tid(*ft.params()[0]), std::vector<ValueId>(1, slot))));
    ops.push_back(result(add(Op::Bitcast, tid(*types_.pointerTo(ft.params()[1]->referent())), std::vector<ValueId>(1, from))));
    directCall(copy, sig, ops);
    t = pads_.back();
    pads_.pop_back();
    if (!t.block.valid()) return;
    if (!landingPads_) outside("a catch's copy in a function the parser gave no landing pads");
    const BlockId back = cur_;
    place(t.block);
    const InstId tp = add(Op::CatchPad, TypeId(), std::vector<ValueId>());
    at(tp).syms = t.types;
    at(tp).cases.push_back(t.indices[0]);
    at(tp).k = 1;
    runtimeCall("__cxa_begin_catch", tid(*types_.pointerTo(types_.voidType())), std::vector<ValueId>(1, t.ptr));
    runtimeCall("_ZSt9terminatev", tid(*types_.voidType()), std::vector<ValueId>());
    add(Op::Unreachable, TypeId(), std::vector<ValueId>());
    place(back);
}

void Lowering::tryOpen(const std::vector<sema::TypedId> &handlers, TryState &st) {
    std::vector<TryHandler> &hs = st.hs;
    Pad p;
    p.cleanup = false;
    p.own = handlers.size();
    for (std::size_t i = 0; i < handlers.size(); i++) {
        TryHandler h;
        h.node = handlers[i];
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
            if (t.isReference() ? !referable(c) : !caughtByValue(h.node, c))
                outside("a catch of '" + t.describe() + "'");
            if (!typeInfo_) outside("a catch with no type_info to name");
            h.sym = typeInfo_(h.caught);
            if (h.sym.empty()) outside("a catch of a type with no type_info");
        } else if (parts == 1 && text(h.node).empty() && unnamedCaught_ && !unnamedCaught_(tree_.node(h.node).syntax).empty()) {
            // `catch (T)` with no name: nothing to bind, only the type_info the parser named for it.
            h.body = child(h.node, 0);
            h.sym = unnamedCaught_(tree_.node(h.node).syntax);
        } else {
            // A named handler whose var node the tree lost, in one shape of the suite (M3-PLAN), stays the parser's.
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
    st.done = newBlock();
    pads_.push_back(p);
}

void Lowering::tryClose(TryState &st, bool rethrows) {
    const std::vector<TryHandler> &hs = st.hs;
    const BlockId done = st.done;
    const TypeId voidPtr = tid(*types_.pointerTo(types_.voidType()));
    Pad p = pads_.back();
    pads_.pop_back();
    br(done);
    if (!p.block.valid()) {     // nothing in the body could throw: the handlers are dead code
        place(done);
        if (rethrows && !terminated()) add(Op::Unreachable, TypeId(), std::vector<ValueId>());
        return;
    }
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
        const sema::TypedId hv = hs[i].var;
        const TypeId declared = hv.valid() ? TypeTable::id(tree_.node(hv).type) : TypeId();
        const bool byValueClass = declared.valid() && !T(declared).isReference() && T(hs[i].caught).isStructOrUnion();
        std::string copy;
        TypeId copySig;
        const bool copies = byValueClass && caughtCopy_ && caughtCopy_(tree_.node(hs[i].node).syntax, copy, copySig);
        // [except.handle]/3: a class with a copy constructor is copied from __cxa_get_exception_ptr's pointer, the catch begun after.
        const bool copyFirst = copies && target_.hasGetExceptionPtr();
        ValueId slot;
        if (copyFirst) {
            slot = entryAlloca(declared, text(hv));
            caughtCopied(slot, runtimeCall("__cxa_get_exception_ptr", voidPtr, std::vector<ValueId>(1, p.ptr)).v, copy, copySig);
        }
        const Val obj = runtimeCall("__cxa_begin_catch", voidPtr, std::vector<ValueId>(1, p.ptr));
        if (hv.valid()) {
            const Type &t = T(declared);
            // By value, the scalar is copied out of the exception object; a reference holds what __cxa_begin_catch handed back.
            const TypeId objPtr = types_.pointerTo(hs[i].caught);
            const ValueId asT = result(add(Op::Bitcast, objPtr, std::vector<ValueId>(1, obj.v)));
            if (t.isReference()) {
                const TypeId slotType = tid(*types_.pointerTo(t.referent()));
                slot = entryAlloca(slotType, text(hv));
                store(slot, result(add(Op::Bitcast, slotType, std::vector<ValueId>(1, asT))));
            } else if (byValueClass) {
                if (!copyFirst) slot = entryAlloca(tid(t), text(hv));
                if (copies && !copyFirst) caughtCopied(slot, obj.v, copy, copySig);
                else if (!copies) copyObject(slot, asT, T(hs[i].caught));      // a copy that is its bytes, [class.copy]/12
            } else {
                slot = entryAlloca(tid(t), text(hv));
                store(slot, load(asT, hs[i].caught));
            }
            locals_[tree_.node(hv).syntax.v] = std::make_pair(slot, tid(t));
        }
        // The handler's own cleanup: an exception out of it ends this catch and goes on to what encloses the try.
        Pad cleanup;
        cleanup.cleanup = true;
        cleanup.own = 0;
        if (encAt != 0) { cleanup.types = pads_[encAt - 1].types; cleanup.indices = pads_[encAt - 1].indices; }
        pads_.push_back(cleanup);
        handlerDepth_++;
        handlerLive_.push_back(live_.size());
        // [except.handle]/16: the object caught by value is the handler's, destroyed on each way out before the catch ends.
        const std::size_t caughtMark = live_.size();
        if (byValueClass && T(hs[i].caught).hasDestructor()) {
            const std::string dtor = destructor_ ? destructor_(hs[i].caught) : std::string();
            if (dtor.empty()) outside("a catch of a class whose destructor the parser recorded none for");
            guard(slot, T(hs[i].caught), dtor);
        }
        block(hs[i].body);
        // A constructor's handler that falls off its end rethrows, inside its own cleanup: the catch ends as the exception goes.
        if (rethrows && !terminated()) throwCall("__cxa_rethrow", std::vector<ValueId>());
        closeScope(caughtMark);
        handlerLive_.pop_back();
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
    if (rethrows && !terminated()) add(Op::Unreachable, TypeId(), std::vector<ValueId>());     // a constructor's body returned
}

// The filter's chain: a selector below 0 is an exception the list does not allow, handed to __cxa_call_unexpected;
// anything else the list allowed, and the frame unwinds on.
// The catch-all a noexcept body is wrapped in: the exception begun, as clang's __clang_call_terminate does, then abort().
void Lowering::noexceptAbort(const Pad &p) {
    if (!landingPads_) outside("a noexcept wrap in a function the parser gave no landing pads");
    const BlockId back = cur_;
    place(p.block);
    const InstId cp = add(Op::CatchPad, TypeId(), std::vector<ValueId>());
    at(cp).syms = p.types;
    at(cp).cases.push_back(p.indices[0]);
    at(cp).k = 1;
    runtimeCall("__cxa_begin_catch", tid(*types_.pointerTo(types_.voidType())), std::vector<ValueId>(1, p.ptr));
    runtimeCall("abort", tid(*types_.voidType()), std::vector<ValueId>());
    add(Op::Unreachable, TypeId(), std::vector<ValueId>());
    place(back);
}

void Lowering::specFilterChain(const Pad &p) {
    if (!landingPads_) outside("a throw(...) filter in a function the parser gave no landing pads");
    const BlockId back = cur_;
    place(p.block);
    const InstId cp = add(Op::CatchPad, TypeId(), std::vector<ValueId>());
    at(cp).syms = p.types;
    at(cp).cases.push_back(p.indices[0]);
    at(cp).k = 1;
    const BlockId unexpected = newBlock(), allowed = newBlock();
    std::vector<ValueId> cmp;
    cmp.push_back(p.sel);
    cmp.push_back(iconst(tid(*types_.get(Kind::Int)), 0));
    const InstId c = add(Op::ICmp, tid(*types_.get(Kind::Bool)), cmp);
    at(c).cc = Cmp::Slt;
    condbr(result(c), unexpected, allowed);
    place(unexpected);
    runtimeCall("__cxa_call_unexpected", tid(*types_.voidType()), std::vector<ValueId>(1, p.ptr));
    add(Op::Unreachable, TypeId(), std::vector<ValueId>());
    place(allowed);
    resumeFrom(p, 0);
    place(back);
}

// A jump out of n handlers: the catches ended, what the jump leaves destroyed, and the branch. On the Microsoft ABI a
// handler is a funclet, so it hands the runtime an exit block's address (a catchret, M3-IR 3.6) and the exit, in the
// body, destroys and branches on; the exit is listed on the try's CatchPad for the selector's flow.
void Lowering::leaveHandlers(std::size_t n, std::size_t mark, BlockId target, const std::vector<ValueId> &args) {
    if (!target_.microsoftNames() || n == 0) {
        leaveCatches(n, mark);
        br(target, args);
        return;
    }
    if (terminated()) return;
    // The handler's own objects are destroyed in its funclet, before the runtime ends the catch; the exit destroys the rest.
    if (n != 1) outside("a jump out of nested Microsoft handlers");
    if (handlerLive_.empty() || handlerLive_.back() < mark) outside("a jump out of Microsoft handlers the lowering did not open");
    const std::size_t own = handlerLive_.back();
    for (std::size_t k = live_.size(); k-- > own; ) if (live_[k].slot.v != elidedSlot_.v) destroy(live_[k]);
    // The exit is a block of the funclet the try stands in: the body's, or an enclosing handler's.
    const BlockId was = funclet_;
    funclet_ = msTryFunclet_;
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
    for (std::size_t k = own; k-- > mark; ) if (live_[k].slot.v != elidedSlot_.v) destroy(live_[k]);
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
        } else if (parts == 1 && text(h.node).empty() && msUnnamedCaught_ &&
                   msUnnamedCaught_(tree_.node(h.node).syntax, h.descriptor, h.byRef, h.isConst)) {
            // `catch (T)` with no name: no slot for the runtime to fill, only the descriptor the parser named.
            h.body = child(h.node, 0);
        } else {
            outside(parts == 1 ? "a handler whose caught object the syntax tree does not hold" : "a handler of " + std::to_string(parts) + " parts");
        }
        hs.push_back(h);
    }
    // The enclosing pad's block before the try's, so its state precedes tryLow; the try's before the body's, so the
    // states made inside the body are tryLow..tryHigh, and the handlers', made after it, follow (the walker's numbering).
    // Inside a handler the enclosing state is a pad opened in its funclet, else the handler's own (M3-IR 3.6).
    const BlockId enclosing = msEnclosingPad();
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
    // A handler's funclet is emitted apart from the one the try stands in, so its block belongs to no funclet.
    const BlockId wasTry = msTryPad_, wasFunclet = funclet_, wasTryFunclet = msTryFunclet_;
    const std::size_t wasPads = funcletPads_;
    funclet_ = BlockId();
    std::vector<BlockId> handlerBlocks;
    for (std::size_t i = 0; i < hs.size(); i++) {
        handlerBlocks.push_back(newBlock());
        F().blockArena[handlerBlocks.back().v].pad = true;
    }
    funclet_ = wasFunclet;
    place(p.block);
    const InstId cs = add(Op::CatchSwitch, TypeId(), std::vector<ValueId>());
    at(cs).k = enclosing.valid() ? static_cast<long long>(enclosing.v) : 0;
    for (std::size_t i = 0; i < hs.size(); i++) {
        Succ s;
        s.block = handlerBlocks[i];
        at(cs).succ.push_back(s);
    }
    msTryPad_ = p.block;
    msTryFunclet_ = wasFunclet;
    funcletPads_ = pads_.size();
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
        handlerLive_.push_back(live_.size());
        block(hs[i].body);
        handlerLive_.pop_back();
        handlerDepth_--;
        if (!terminated()) {
            Succ s;
            s.block = done;
            at(add(Op::CatchRet, TypeId(), std::vector<ValueId>())).succ.push_back(s);
        }
        funclet_ = wasFunclet;
        checkFuncletUses(handlerBlocks[i]);
    }
    // The last block the handlers made: the states inside them end there (the selector's catchHigh).
    at(cs).bits = F().blockArena.size() - 1;
    msTryPad_ = wasTry;
    msTryFunclet_ = wasTryFunclet;
    funcletPads_ = wasPads;
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
        // The cast narrows: `(char)258` is 2 ([conv.integral]/3, as the parser's fold reads it).
        if (count(id) != 1 || !n.castType.valid() || !T(n.castType).isInteger() || T(n.castType).isBool()) return false;
        if (!foldConstant(child(id, 0), out)) return false;
        out = normalise(out, T(n.castType), target_);
        return true;
    default:
        return false;
    }
}

}   // namespace ir
