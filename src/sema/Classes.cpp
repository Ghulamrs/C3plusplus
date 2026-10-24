// sema::Classes - see Classes.h.

#include "Classes.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"
#include "../Source.h"

#include <algorithm>

namespace sema {

// The deleting destructor's name. Built through the manglers rather than by
// concatenation, because a nested class's is a whole nested-name -
// ??_GInner@Outer@@UEAAPEAXI@Z, not ??_GOuter::Inner@@...
bool Classes::overrides(const VSlot &s, const std::string &name,
                       const std::vector<const Type *> &params, bool constThis,
                       RefQual refThis) {
    if (s.name != name || s.constThis != constThis || s.refThis != refThis) return false;
    return sameParameters(s.params, params);
}

// **[class.virtual]/8: an override returns the same type, or a pointer or
// reference to a class derived from the one the base's returns.** The slot was
// taken on name and parameters alone: `double f()` over `virtual int f()` ran.
void Classes::checkOverrideReturn(const VSlot &s, const Type *returns, const std::string &cls,
                                 const std::string &name, std::size_t pos) const {
    if (s.returns == nullptr || returns == nullptr) return;
    if (s.returns == returns || s.returns->describe() == returns->describe()) return;
    const bool ptr = s.returns->isPointer() && returns->isPointer();
    const bool ref = s.returns->isReference() && returns->isReference();
    if (ptr || ref) {
        const Type *base = s.returns->pointee(), *over = returns->pointee();
        if (base->unqualified()->isStructOrUnion() && over->unqualified()->isStructOrUnion() &&
            (base->isConst() || !over->isConst())) {
            const int off = publicBaseOffset(over, base);
            if (off == 0) return;
            if (off > 0)
                p_.src_.fail(pos, "'" + cls + "::" + name + "' returns '" + returns->describe() +
                               "' over the base's '" + s.returns->describe() + "' - a covariant "
                               "return, and this one moves the result by " + std::to_string(off) +
                               " bytes at every call through the base, which needs a thunk this "
                               "compiler does not build yet. Return the base's type here");
        }
    }
    p_.src_.fail(pos, "'" + cls + "::" + name + "' overrides a virtual function with a "
                   "different return type: '" + returns->describe() + "' where the base's "
                   "is '" + s.returns->describe() + "'. An override returns the same type, "
                   "or a pointer or reference to a class derived from what the base's "
                   "points at");
}

// **The vbtable pointer, which only the Microsoft ABI has.**
void Classes::storeVbptr(const std::string &cls, const Type *memberOf,
                        int thisSlot, std::vector<StmtPtr> &into) {
    const std::vector<Type::VbPtr> &ptrs = memberOf->vbptrs();
    const Type *charPtr = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *entry = p_.types_.intType();

    // **One store per pointer.**
    for (std::size_t p = 0; p < ptrs.size(); p++) {
        const int vbp = ptrs[p].offset;
        const std::string symbol =
            ptrs.size() == 1 || ptrs[p].base == nullptr
                ? vbtableSymbol(cls)
                : vbtableSymbol(cls, ptrs[p].base->tag());

        // The table's address, the same way the vftable's is taken: a global
        // with an array type, decayed, so what is stored is where it is and
        // not what its first word happens to hold.
        int entries = 1;
        const std::vector<Type::BaseSpec> &bs =
            p == 0 || ptrs[p].base == nullptr ? memberOf->bases()
                                              : ptrs[p].base->bases();
        for (std::size_t i = 0; i < bs.size(); i++)
            if (bs[i].isVirtual) entries++;
        ExprPtr table(Var::global(symbol));
        table->setType(p_.types_.arrayOf(entry, entries));
        ExprPtr value(new Cast(charPtr, p_.decay(std::move(table))));
        value->setType(charPtr);

        ExprPtr self(Var::local("this", thisSlot));
        self->setType(charPtr);
        ExprPtr where = std::move(self);
        if (vbp != 0) {
            ExprPtr at(new Num(static_cast<long long>(vbp)));
            at->setType(p_.types_.get(Kind::LongLong));
            ExprPtr sum(new Binary(BinOp::Add, std::move(where),
                                   std::move(at)));
            sum->setType(charPtr);
            where = std::move(sum);
        }
        ExprPtr slot(new Cast(p_.types_.pointerTo(charPtr), std::move(where)));
        slot->setType(p_.types_.pointerTo(charPtr));
        ExprPtr there(new Unary('*', std::move(slot)));
        there->setType(charPtr);
        ExprPtr store(new Assign(std::move(there), std::move(value)));
        store->setType(charPtr);
        into.push_back(StmtPtr(new ExprStmt(std::move(store))));
    }
}

// **Setting the vptr, for whoever is building the object**, pulled out of the
// constructor path when implicit constructors arrived. Itanium's whole walk
// is ParserVtable.cpp's; Microsoft has one table, and no header before it.
std::vector<StmtPtr> Classes::storeVptrs(const std::string &cls,
                                        const Type *memberOf, int thisSlot) {
    std::vector<StmtPtr> withVptr;
    if (!p_.target_.microsoftNames())
        return itaniumVptrStores(cls, memberOf, thisSlot);
    // **Microsoft has two pointers and a class can want either alone.**
    if (!memberOf->polymorphic()) return withVptr;
    // **The class itself where it is the one named**, so a specialization's
    // table is spelled by the mangler rather than by counting the letters of
    // `P<int>`.
    const std::string table =
        memberOf != nullptr && memberOf->unqualified()->tag() == cls
            ? vtableSymbol(memberOf, true) : vtableSymbol(cls, true);
    const Type *entry = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *entries = p_.types_.pointerTo(entry);

    // **The table's ADDRESS, not its contents.** A global Var is an lvalue and
    // reading one loads from it, which stored the table's first word in the vptr and
    // crashed on the first call. Giving it the array type and decaying it is it.
    const std::size_t entryCount = p_.vtables_[cls].size();
    ExprPtr base(Var::global(table));
    base->setType(p_.types_.arrayOf(entry, static_cast<long long>(entryCount)));
    ExprPtr value = p_.decay(std::move(base));
    ExprPtr asVoid(new Cast(entry, std::move(value)));
    asVoid->setType(entry);

    ExprPtr self(Var::local("this", thisSlot));
    self->setType(entries);                        // the vptr lives at offset 0
    ExprPtr where(new Unary('*', std::move(self)));
    where->setType(entry);

    ExprPtr store(new Assign(std::move(where), std::move(asVoid)));
    store->setType(entry);

    withVptr.push_back(StmtPtr(new ExprStmt(std::move(store))));
    return withVptr;
}

// **A function with no source behind it.** The deleting destructor runs the
// destructor and then gives the memory back, because `delete p` through a base
// pointer reaches both through one slot. Itanium's D0 and Microsoft's ??_G differ.
void Classes::synthesizeDeleting(const std::string &cls, const Type *type,
                                Access access, std::size_t pos) {
    const bool ms = p_.target_.microsoftNames();
    const Type *self = p_.types_.pointerTo(type);
    const std::string symbol = p_.linkage_.deletingDestructorSymbol(cls);
    (void)access;

    // Its own frame: `this`, and on Windows the flag beside it.
    const Parser::FrameSave savedFrame = p_.openFrame();
    std::vector<Param> params;
    int thisSlot = p_.allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    int flagSlot = 0;
    const Type *flagType = p_.types_.get(Kind::UInt);
    if (ms) {
        flagSlot = p_.allocateFrameSlot(flagType);
        params.push_back(Param{ flagType, flagSlot });
    }

    std::vector<StmtPtr> body;

    const Signature *dtor = p_.destructorOf(type);
    if (dtor != nullptr) {
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        body.push_back(StmtPtr(new ExprStmt(p_.destructorCall(std::move(me), *dtor, pos))));
    }

    // operator delete(this)
    ExprPtr again(Var::local("this", thisSlot));
    again->setType(self);
    const Type *vp = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    ExprPtr raw(new Cast(vp, std::move(again)));
    raw->setType(vp);
    StmtPtr freeIt(new ExprStmt(p_.deallocate(type, std::move(raw), pos)));

    if (ms) {
        // if (flags & 1) operator delete(this);
        ExprPtr flags(Var::local("flags", flagSlot));
        flags->setType(flagType);
        ExprPtr one(new Num(1LL));
        one->setType(flagType);
        ExprPtr test(new Binary(BinOp::BitAnd, std::move(flags), std::move(one)));
        test->setType(flagType);
        body.push_back(StmtPtr(new If(std::move(test), std::move(freeIt), nullptr)));

        ExprPtr back(Var::local("this", thisSlot));
        back->setType(self);
        ExprPtr asVoid(new Cast(vp, std::move(back)));
        asVoid->setType(vp);
        body.push_back(StmtPtr(new Return(std::move(asVoid))));
    } else {
        body.push_back(std::move(freeIt));
        body.push_back(StmtPtr(new Return(nullptr)));
    }

    const Type *returns = ms ? vp : p_.types_.get(Kind::Void);
    p_.current_->functions.push_back(Function(cls + "::deleting", returns,
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(symbol);
    // **A compiler-written special member is inline** - [class.copy] and its
    // neighbours say the implicit definition is - so several translation units
    // may each hold one and the linker folds them.
    p_.current_->functions.back().setInline(true);
    p_.closeFrame(savedFrame);
}

void Classes::synthesizeCompleteCtor(const Type *type,
                                    const std::vector<const Type *> &ctorParams,
                                    const std::string &c1, const std::string &c2,
                                    bool isInline, std::size_t pos,
                                    int copyArg, bool moving,
                                    std::map<std::string,
                                        std::vector<ExprPtr> > *vbaseArgs) {
    const std::string &cls = type->tag();
    const Type *self = p_.types_.pointerTo(type);

    const Parser::FrameSave savedFrame = p_.openFrame();
    // **The arguments take their slots before `this`, which is C2's order and
    // not the obvious one.**
    std::vector<int> argSlots;
    std::vector<const Type *> argHeld;
    for (std::size_t i = 0; i < ctorParams.size(); i++) {
        const Type *held = ctorParams[i]->isReference()
                         ? p_.types_.pointerTo(ctorParams[i]->referent())
                         : ctorParams[i];
        argSlots.push_back(p_.allocateFrameSlot(held));
        argHeld.push_back(held);
    }
    const int thisSlot = p_.allocateFrameSlot(self);
    std::vector<Param> params;
    params.push_back(Param{ self, thisSlot });
    for (std::size_t i = 0; i < ctorParams.size(); i++)
        params.push_back(Param{ argHeld[i], argSlots[i] });

    const int srcSlot = (copyArg >= 0 &&
                         static_cast<std::size_t>(copyArg) < argSlots.size())
                      ? argSlots[copyArg] : -1;
    std::vector<int> builtBase;
    std::vector<StmtPtr> body = p_.virtualBaseCalls(type, thisSlot, true, pos,
                                                 srcSlot, moving, vbaseArgs, &builtBase);

    // Then C2, which builds everything else - the non-virtual bases, the
    // members, and the body the user wrote.
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    std::vector<ExprPtr> args;
    args.push_back(std::move(me));
    std::vector<const Type *> ps;
    ps.push_back(self);
    if (p_.takesVtt(type)) {
        args.push_back(vttOfClass(type, 0));
        ps.push_back(p_.vttType());
    }
    for (std::size_t i = 0; i < ctorParams.size(); i++) {
        ExprPtr a(Var::local("a", argSlots[i]));
        if (ctorParams[i]->isReference()) {
            // The slot holds the address; the callee wants the object, bound
            // to its reference parameter the way every other call binds one.
            a->setType(p_.types_.pointerTo(ctorParams[i]->referent()));
            ExprPtr obj(new Unary('*', std::move(a)));
            obj->setType(ctorParams[i]->referent());
            if (moving) obj->setXvalue();
            a = std::move(obj);
        } else {
            a->setType(ctorParams[i]);
        }
        args.push_back(std::move(a));
        ps.push_back(ctorParams[i]);
    }
    // [except.ctor]/3 for the virtual bases: what follows each one's construction destroys it on
    // unwind. This function is written on its own, so no enclosing `try` is any of its business.
    std::vector<Parser::Alive> done(body.size());
    for (std::size_t i = 0; i < body.size(); i++)
        if (builtBase[i] >= 0) {
            done[i] = Parser::Alive{ "this", thisSlot, type };
            done[i].owner = type;
            done[i].base = builtBase[i];
        }
    body.push_back(StmtPtr(new ExprStmt(
        p_.completeCall(cls, c2, nullptr, p_.types_.get(Kind::Void), ps, false, pos,
                     std::move(args)))));
    bool guarded = false;
    StmtPtr whole = p_.guardConstruction(std::move(body), std::move(done), pos, &guarded);
    body.clear();
    body.push_back(std::move(whole));

    p_.current_->functions.push_back(Function(cls + "::complete",
                                           p_.types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(c1);
    p_.current_->functions.back().setHasLandingPads(guarded);
    if (isInline) p_.current_->functions.back().setInline(true);
    p_.closeFrame(savedFrame);
}

void Classes::synthesizeCompleteDtor(const Type *type, const std::string &d1,
                                    const std::string &d2, bool isInline,
                                    std::size_t pos) {
    const std::string &cls = type->tag();
    const Type *self = p_.types_.pointerTo(type);

    const Parser::FrameSave savedFrame = p_.openFrame();
    std::vector<Param> params;
    const int thisSlot = p_.allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });

    // D2 first - it destroys what C2 built - and the virtual bases after it.
    std::vector<StmtPtr> body;
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    std::vector<ExprPtr> args;
    args.push_back(std::move(me));
    std::vector<const Type *> ps;
    ps.push_back(self);
    if (p_.takesVtt(type)) {
        args.push_back(vttOfClass(type, 0));
        ps.push_back(p_.vttType());
    }
    body.push_back(StmtPtr(new ExprStmt(
        p_.completeCall(cls, d2, nullptr, p_.types_.get(Kind::Void), ps, false, pos,
                     std::move(args)))));

    std::vector<StmtPtr> after = p_.virtualBaseCalls(type, thisSlot, false, pos);
    for (std::size_t i = 0; i < after.size(); i++)
        body.push_back(std::move(after[i]));

    p_.current_->functions.push_back(Function(cls + "::completeDtor",
                                           p_.types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(d1);
    if (isInline) p_.current_->functions.back().setInline(true);
    p_.closeFrame(savedFrame);
}

// The vtable: one pointer per virtual function, in the order the base declared them,
// an override replacing an entry. **The two ABIs differ in the header** - Itanium 16
// bytes with a zero typeinfo, Microsoft none - and a thunk walks `this` back.
std::string Classes::synthesizeThunk(const std::string &cls, const Type *type,
                                    const VSlot &slot, int offset,
                                    std::size_t pos) {
    const bool ms = p_.target_.microsoftNames();
    const std::string name = ms
        ? slot.symbol + "$adj" + std::to_string(offset)
        // _ZThn16_N1C1gEv.
        : "_ZThn" + std::to_string(offset) + "_" + slot.symbol.substr(2);
    // One per name: a construction vtable names the same thunk again.
    for (std::size_t i = 0; i < p_.current_->functions.size(); i++)
        if (p_.current_->functions[i].symbol() == name) return name;

    const Type *self = p_.types_.pointerTo(type);
    const Parser::FrameSave savedFrame = p_.openFrame();

    std::vector<Param> params;
    int thisSlot = p_.allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    std::vector<int> argSlots;
    for (std::size_t i = 0; i < slot.params.size(); i++)
        argSlots.push_back(p_.allocateFrameSlot(slot.params[i]));
    for (std::size_t i = 0; i < slot.params.size(); i++)
        params.push_back(Param{ slot.params[i], argSlots[i] });

    // (C *)((char *)this - offset)
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    const Type *chars = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    ExprPtr asChars(new Cast(chars, std::move(me)));
    asChars->setType(chars);
    ExprPtr back(new Num(static_cast<long long>(-offset)));
    back->setType(p_.types_.intType());
    ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(back)));
    moved->setType(chars);
    ExprPtr whole(new Cast(self, std::move(moved)));
    whole->setType(self);

    std::vector<ExprPtr> args;
    args.push_back(std::move(whole));
    std::vector<const Type *> full;
    full.push_back(self);
    for (std::size_t i = 0; i < slot.params.size(); i++) {
        ExprPtr a(Var::local("a" + std::to_string(i), argSlots[i]));
        a->setType(slot.params[i]);
        args.push_back(std::move(a));
        full.push_back(slot.params[i]);
    }

    const Signature *target = nullptr;
    if (const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(cls + "::" + slot.name))
        for (std::size_t k = 0; k < set->size(); k++)
            if (p_.functions_[(*set)[k]].symbol == slot.symbol) target = &p_.functions_[(*set)[k]];
    const Type *returns = target != nullptr ? target->returns : p_.types_.get(Kind::Void);

    // A thunk forwards a member call and carries the same `this`, which is
    // what decides where the Microsoft ABI puts a hidden return pointer.
    ExprPtr call = p_.completeCall(slot.name, slot.symbol, nullptr, returns, full,
                                false, pos, std::move(args), true);
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new Return(returns->isVoid() ? nullptr : std::move(call))));
    if (returns->isVoid()) body.insert(body.begin(), StmtPtr(new ExprStmt(std::move(call))));

    p_.current_->functions.push_back(Function(name, returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(name);
    p_.current_->functions.back().setInline(true);
    p_.closeFrame(savedFrame);
    return name;
}

// The body is the virtual call `this->f(a0, a1, ...)` as memberCall builds
// one - the vptr read, the slot at its index - forwarding every parameter.
std::string Classes::synthesizeVcallThunk(const Type *cls, const Signature &f,
                                         int index, std::size_t pos) {
    std::string name, why;
    if (!microsoftVcallThunkName(cls, index * p_.pointerBytes(), &name, &why))
        p_.src_.fail(pos, "'&" + cls->tag() + "::" + f.name + "' cannot be named: " + why);
    for (std::size_t i = 0; i < p_.current_->functions.size(); i++)
        if (p_.current_->functions[i].symbol() == name) return name;   // one per slot

    const Type *self = p_.types_.pointerTo(cls);
    const Parser::FrameSave savedFrame = p_.openFrame();
    std::vector<Param> params;
    const int thisSlot = p_.allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    std::vector<int> argSlots;
    for (std::size_t i = 0; i < f.params.size(); i++)
        argSlots.push_back(p_.allocateFrameSlot(f.params[i]));
    for (std::size_t i = 0; i < f.params.size(); i++)
        params.push_back(Param{ f.params[i], argSlots[i] });

    std::vector<const Type *> full;
    full.push_back(self);
    for (std::size_t i = 0; i < f.params.size(); i++) full.push_back(f.params[i]);
    const Type *fnType = p_.types_.functionType(f.returns, full, f.variadic);
    const Type *fnPtr = p_.types_.pointerTo(fnType);
    const Type *table = p_.types_.pointerTo(fnPtr);

    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    ExprPtr forLoad(new Cast(p_.types_.pointerTo(table), std::move(me)));
    forLoad->setType(p_.types_.pointerTo(table));
    ExprPtr vptr(new Unary('*', std::move(forLoad)));
    vptr->setType(table);
    if (index != 0) {
        ExprPtr at(new Num(static_cast<long long>(index) * fnPtr->size(p_.target_)));
        at->setType(p_.types_.intType());
        ExprPtr moved(new Binary(BinOp::Add, std::move(vptr), std::move(at)));
        moved->setType(table);
        vptr = std::move(moved);
    }
    ExprPtr entry(new Unary('*', std::move(vptr)));
    entry->setType(fnPtr);

    std::vector<ExprPtr> args;
    ExprPtr again(Var::local("this", thisSlot));
    again->setType(self);
    args.push_back(std::move(again));
    for (std::size_t i = 0; i < f.params.size(); i++) {
        ExprPtr a(Var::local("a" + std::to_string(i), argSlots[i]));
        a->setType(f.params[i]);
        args.push_back(std::move(a));
    }
    ExprPtr call = p_.completeCall(f.name, f.symbol, std::move(entry), f.returns,
                                full, f.variadic, pos, std::move(args), true);
    std::vector<StmtPtr> body;
    if (f.returns->isVoid()) {
        body.push_back(StmtPtr(new ExprStmt(std::move(call))));
        body.push_back(StmtPtr(new Return(nullptr)));
    } else {
        body.push_back(StmtPtr(new Return(std::move(call))));
    }
    p_.current_->functions.push_back(Function(name, f.returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(name);
    p_.current_->functions.back().setInline(true);
    p_.closeFrame(savedFrame);
    return name;
}

bool Classes::sameParameters(const std::vector<const Type *> &a,
                            const std::vector<const Type *> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i]) return false;
    return true;
}

bool Classes::memberFromBase(const Type *, const Member &m) {
    // Which class wrote it, not where it sits: a base's data size reaches past its own members
    // once a virtual base is laid at the end, and a derived member may sit in that range.
    return m.declaredIn != nullptr || m.inVirtualBase != nullptr;
}

// **[dcl.init]/7 refuses a const object that nothing would initialise**, and
// the line is CWG 253's rather than the paragraph's letter - which is what
// clang applies and what this was measured against.
bool Classes::constDefaultInitialisable(const Type *t) const {
    const Type *u = t->unqualified();
    while (u->isArray()) u = u->pointee()->unqualified();
    if (!u->isStructOrUnion()) return false;      // `const int n;` and its kind

    // A constructor somebody wrote initialises whatever it means to; one the
    // compiler wrote initialises only what the members ask for, which is the
    // whole of this question.
    if (const Signature *ctor = defaultConstructorOf(u))
        if (!ctor->implicit) return true;

    const std::vector<Type::BaseSpec> &bs = u->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (!constDefaultInitialisable(bs[i].type)) return false;

    const std::vector<Member> &ms = u->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        if (memberFromBase(u, ms[i])) continue;   // its own base answered for it
        if (p_.memberInit_.count(u->tag() + "::" + ms[i].name)) continue;
        if (!constDefaultInitialisable(ms[i].type)) return false;
    }
    return true;
}

// **Which entry of the vbtable holds this base**, and where the base sits in
// the complete object. Entry 0 is the vbptr's own offset back to the top, so
// the first virtual base is entry 1 and a zero answer means "not there".
int Classes::virtualBaseSlot(const Type *owner, const Type *vbase, int *baseAt) {
    const std::vector<Type::BaseSpec> &bs = owner->bases();
    int seen = 0;
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (!bs[i].isVirtual) continue;
        seen++;
        if (bs[i].type == vbase) {
            if (baseAt != nullptr) *baseAt = bs[i].offset;
            return seen;
        }
    }
    return 0;
}

// **The step from a derived object to its virtual base, on the Microsoft
// ABI**: `vbptrOffset + table[slot]`, where the table is the one the object's
// vbptr points at.
ExprPtr Classes::microsoftVirtualBaseStep(const Type *owner, const Type *vbase,
                                         const std::string &temp, int held,
                                         int *baseAt) const {
    const int vbp = owner->vbptrOffset();
    const int slot = virtualBaseSlot(owner, vbase, baseAt);
    if (vbp < 0 || slot <= 0) return ExprPtr();

    const Type *charPtr = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *offType = p_.types_.get(Kind::LongLong);
    const Type *entry = p_.types_.intType();

    // `p = this + vbptrOffset`, the one address both the table and the base
    // are measured from.
    ExprPtr atVbptr(Var::local(temp, held));
    atVbptr->setType(charPtr);
    if (vbp != 0) {
        ExprPtr n(new Num(static_cast<long long>(vbp)));
        n->setType(offType);
        ExprPtr sum(new Binary(BinOp::Add, std::move(atVbptr), std::move(n)));
        sum->setType(charPtr);
        atVbptr = std::move(sum);
    }

    // `delta = ((int *)*(char **)p)[slot]`, four bytes and signed.
    ExprPtr asTable(new Cast(p_.types_.pointerTo(charPtr), std::move(atVbptr)));
    asTable->setType(p_.types_.pointerTo(charPtr));
    ExprPtr table(new Unary('*', std::move(asTable)));
    table->setType(charPtr);
    ExprPtr step(new Num(static_cast<long long>(slot) * 4));
    step->setType(offType);
    ExprPtr entryAt(new Binary(BinOp::Add, std::move(table), std::move(step)));
    entryAt->setType(charPtr);
    ExprPtr asEntry(new Cast(p_.types_.pointerTo(entry), std::move(entryAt)));
    asEntry->setType(p_.types_.pointerTo(entry));
    ExprPtr delta(new Unary('*', std::move(asEntry)));
    delta->setType(entry);
    ExprPtr wide(new Cast(offType, std::move(delta)));
    wide->setType(offType);
    if (vbp == 0) return wide;

    // The pointer stepped to the vbptr first, so that much is part of the
    // answer: the entry is measured from there and not from the object.
    ExprPtr n(new Num(static_cast<long long>(vbp)));
    n->setType(offType);
    ExprPtr sum(new Binary(BinOp::Add, std::move(wide), std::move(n)));
    sum->setType(offType);
    return sum;
}

// **Reaching a virtual base's member on the Microsoft ABI.**
ExprPtr Classes::microsoftVirtualBaseMember(ExprPtr object, const Type *owner,
                                           const Member &m) {
    if (owner->vbptrOffset() < 0) return ExprPtr();     // refused before this

    const Type *charPtr = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *offType = p_.types_.get(Kind::LongLong);

    // The object's address, kept in a slot: the step below reads it twice and
    // evaluating the expression twice would run its side effects twice.
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(p_.types_.pointerTo(owner));
    const int held = p_.allocateFrameSlot(charPtr);
    const std::string temp = ".vb" + std::to_string(p_.refTemps_++);
    ExprPtr keep(Var::local(temp, held));
    keep->setType(charPtr);
    ExprPtr asChar(new Cast(charPtr, std::move(addr)));
    asChar->setType(charPtr);
    ExprPtr save(new Assign(std::move(keep), std::move(asChar)));
    save->setType(charPtr);

    int baseAt = 0;
    ExprPtr step = microsoftVirtualBaseStep(owner, m.inVirtualBase, temp, held,
                                            &baseAt);
    if (!step) return ExprPtr();

    ExprPtr from(Var::local(temp, held));
    from->setType(charPtr);
    ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(step)));
    moved->setType(charPtr);
    // And where the member sits inside that base, which is constant.
    if (m.offset != baseAt) {
        ExprPtr n(new Num(static_cast<long long>(m.offset - baseAt)));
        n->setType(offType);
        ExprPtr sum(new Binary(BinOp::Add, std::move(moved), std::move(n)));
        sum->setType(charPtr);
        moved = std::move(sum);
    }

    ExprPtr asT(new Cast(p_.types_.pointerTo(m.type), std::move(moved)));
    asT->setType(p_.types_.pointerTo(m.type));
    ExprPtr whole(new Comma(std::move(save), std::move(asT)));
    whole->setType(p_.types_.pointerTo(m.type));
    ExprPtr deref(new Unary('*', std::move(whole)));
    deref->setType(m.type);
    return deref;
}

ExprPtr Classes::virtualBaseMember(ExprPtr object, const Type *staticType,
                                  const Member &m) {
    if (m.inVirtualBase == nullptr) return ExprPtr();
    const Type *owner = staticType->unqualified();
    if (!owner->isStructOrUnion()) return ExprPtr();
    if (p_.target_.microsoftNames())
        return microsoftVirtualBaseMember(std::move(object), owner, m);

    const long long back = itaniumVbaseOffsetSlot(owner, m.inVirtualBase);
    const int baseAt = virtualBaseAt(owner, m.inVirtualBase);
    if (back == 0 || baseAt < 0) return ExprPtr();

    const Type *charPtr = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *offType = p_.ptrdiffType();

    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(p_.types_.pointerTo(owner));
    const int held = p_.allocateFrameSlot(charPtr);
    const std::string temp = ".vb" + std::to_string(p_.refTemps_++);
    ExprPtr keep(Var::local(temp, held));
    keep->setType(charPtr);
    ExprPtr asChar(new Cast(charPtr, std::move(addr)));
    asChar->setType(charPtr);
    ExprPtr save(new Assign(std::move(keep), std::move(asChar)));
    save->setType(charPtr);

    ExprPtr obj(Var::local(temp, held));
    obj->setType(charPtr);
    ExprPtr asTable(new Cast(p_.types_.pointerTo(charPtr), std::move(obj)));
    asTable->setType(p_.types_.pointerTo(charPtr));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(charPtr);
    ExprPtr backNum(new Num(back));
    backNum->setType(offType);
    ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(backNum)));
    at->setType(charPtr);
    ExprPtr asOff(new Cast(p_.types_.pointerTo(offType), std::move(at)));
    asOff->setType(p_.types_.pointerTo(offType));
    ExprPtr delta(new Unary('*', std::move(asOff)));
    delta->setType(offType);

    ExprPtr from(Var::local(temp, held));
    from->setType(charPtr);
    ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(delta)));
    moved->setType(charPtr);
    const int within = m.offset - baseAt;
    if (within != 0) {
        ExprPtr w(new Num(static_cast<long long>(within)));
        w->setType(offType);
        ExprPtr sum(new Binary(BinOp::Add, std::move(moved), std::move(w)));
        sum->setType(charPtr);
        moved = std::move(sum);
    }
    ExprPtr asT(new Cast(p_.types_.pointerTo(m.type), std::move(moved)));
    asT->setType(p_.types_.pointerTo(m.type));
    ExprPtr whole(new Comma(std::move(save), std::move(asT)));
    whole->setType(p_.types_.pointerTo(m.type));
    ExprPtr deref(new Unary('*', std::move(whole)));
    deref->setType(m.type);
    return deref;
}

// **The type_info beside a vtable, and the name string beside that.**
std::string Classes::typeInfoSymbolFor(const Type *t, std::size_t pos,
                                      std::string *why) {
    const Type *u = t->unqualified();
    std::string name;
    if (itaniumTypeInfoName(u, &name, why)) return name;
    if (u->isPointer()) return emitPointerTypeInfo(u, pos, why);
    if (u->isEnumeration()) return emitEnumTypeInfo(u, pos, why);

    if (u->isStructOrUnion() && !u->tag().empty()) {
        // More than one base wants `__vmi_class_type_info`, which is not built
        // - emitClassTypeInfo answers empty for it and says so here.
        const std::string ti = emitClassTypeInfo(u, u->tag(), pos);
        if (!ti.empty()) { why->clear(); return ti; }
        *why = "'" + u->describe() + "' has a base this compiler cannot "
               "describe a type_info for";
        return std::string();
    }
    return std::string();
}

// **An enumeration's type_info is an `__enum_type_info`** - [ABI 2.9.5]:
// the vptr and the name string, nothing more, emitted weak beside its use.
std::string Classes::emitEnumTypeInfo(const Type *e, std::size_t pos,
                                     std::string *why) {
    (void)pos;
    std::string spelt;
    if (!itaniumTypeSpelling(e, &spelt, why)) return std::string();
    const std::string ti = "_ZTI" + spelt;
    for (std::size_t i = 0; i < p_.current_->globals.size(); i++)
        if (p_.current_->globals[i].symbol == ti) return ti;
    const std::string ts = "_ZTS" + spelt;
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= spelt.size(); i++)
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                       i < spelt.size() ? spelt[i] : 0,
                                       std::string() });
    const Type *chars = p_.types_.arrayOf(p_.types_.get(Kind::Char),
                                       static_cast<long long>(spelt.size() + 1));
    p_.current_->globals.push_back(Global{ ts, ts, chars, std::move(letters),
                                        true, false, true, std::string(), true });
    const int w = p_.pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{ 0, w, 2 * w, "_ZTVN10__cxxabiv116__enum_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    const Type *word = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *object = p_.types_.arrayOf(word, 2);
    p_.current_->globals.push_back(Global{ ti, ti, object, std::move(pieces),
                                        true, false, true, std::string(), true });
    return ti;
}

// **A pointer's type_info is a `__pointer_type_info`** - [ABI 2.9.5]: vptr,
// name, the pointee's cv-qualifiers as flags, the pointee's own type_info.
// One to a fundamental type - `_ZTIPi`, `_ZTIPKc` - is the library's.
std::string Classes::emitPointerTypeInfo(const Type *ptr, std::size_t pos,
                                        std::string *why) {
    const Type *pointee = ptr->pointee();
    const Type *plain = pointee->unqualified();
    if (plain->isFunction() || plain->isMemberPointer() || plain->isMemberFunctionPointer()) {
        *why = "a pointer to a function has no type_info this compiler can "
               "emit - '" + ptr->describe() + "' would need a "
               "__function_type_info for what it points at";
        return std::string();
    }
    std::string spelt;
    if (!itaniumTypeSpelling(ptr, &spelt, why)) return std::string();
    const bool builtin = itaniumBuiltinCode(plain->kind()) != nullptr;
    if (builtin) return "_ZTI" + spelt;                   // the library's

    const std::string ti = "_ZTI" + spelt;
    for (std::size_t i = 0; i < p_.current_->globals.size(); i++)
        if (p_.current_->globals[i].symbol == ti) return ti;
    const std::string inner = typeInfoSymbolFor(plain, pos, why);
    if (inner.empty()) return std::string();

    const std::string ts = "_ZTS" + spelt;
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= spelt.size(); i++)
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                       i < spelt.size() ? spelt[i] : 0,
                                       std::string() });
    const Type *chars = p_.types_.arrayOf(p_.types_.get(Kind::Char),
                                       static_cast<long long>(spelt.size() + 1));
    p_.current_->globals.push_back(Global{ ts, ts, chars, std::move(letters),
                                        true, false, true, std::string(), true });

    // vptr, name, flags (const 1, volatile 2 - only const is in this type
    // system), the pointee's type_info at the next pointer boundary.
    const int w = p_.pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{ 0, w, 2 * w, "_ZTVN10__cxxabiv119__pointer_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    pieces.push_back(GlobalPiece{ 2 * w, 4, pointee->isConst() ? 1 : 0, std::string() });
    pieces.push_back(GlobalPiece{ 3 * w, w, 0, inner });
    const Type *word = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *object = p_.types_.arrayOf(word, 4);
    p_.current_->globals.push_back(Global{ ti, ti, object, std::move(pieces),
                                        true, false, true, std::string(), true });
    return ti;
}

// **The flags word of a `__vmi_class_type_info`**, [ABI 2.9.5]: bit 0 says a
// base class appears more than once but not diamond-shaped, bit 1 says it
// does.
long long Classes::itaniumVmiFlags(const Type *cls) {
    std::vector<const Type *> seen;
    std::vector<int> count;
    std::vector<bool> virtualPath;
    std::vector<const Type *> stack;
    std::vector<bool> stackVirtual;
    const std::vector<Type::BaseSpec> &direct = cls->bases();
    for (std::size_t i = 0; i < direct.size(); i++) {
        stack.push_back(direct[i].type);
        stackVirtual.push_back(direct[i].isVirtual);
    }
    while (!stack.empty()) {
        const Type *b = stack.back();
        const bool wasVirtual = stackVirtual.back();
        stack.pop_back();
        stackVirtual.pop_back();
        std::size_t k = 0;
        while (k < seen.size() && seen[k] != b) k++;
        if (k == seen.size()) {
            seen.push_back(b);
            count.push_back(0);
            virtualPath.push_back(false);
        }
        count[k]++;
        if (wasVirtual) virtualPath[k] = true;
        const std::vector<Type::BaseSpec> &up = b->bases();
        for (std::size_t i = 0; i < up.size(); i++) {
            stack.push_back(up[i].type);
            stackVirtual.push_back(wasVirtual || up[i].isVirtual);
        }
    }
    long long flags = 0;
    for (std::size_t k = 0; k < seen.size(); k++) {
        if (count[k] < 2) continue;
        flags |= virtualPath[k] ? 2 : 1;
    }
    return flags;
}

std::string Classes::emitClassTypeInfo(const Type *cls, const std::string &tag,
                                      std::size_t pos) {
    const std::string ti = cls->unqualified()->tag() == tag
                         ? itaniumClassTypeInfoSymbol(cls)
                         : itaniumClassTypeInfoSymbol(tag);
    for (std::size_t i = 0; i < p_.current_->globals.size(); i++)
        if (p_.current_->globals[i].symbol == ti) return ti;      // one per class
    p_.classSymbols_[tag].push_back(ti);

    // **Which of the three shapes this class is.**
    std::vector<Type::BaseSpec> bases;
    {
        const std::vector<Type::BaseSpec> &all = cls->bases();
        for (std::size_t i = 0; i < all.size(); i++)
            if (all[i].direct) bases.push_back(all[i]);
    }
    const bool simple = bases.size() == 1 && !bases[0].isVirtual &&
                        bases[0].offset == 0 &&
                        bases[0].access == Access::Public;

    // **Every base first, and nothing is laid down until they all answer.**
    std::vector<std::string> baseTypeInfo;
    for (std::size_t i = 0; i < bases.size(); i++) {
        const std::string one = emitClassTypeInfo(bases[i].type,
                                                  bases[i].type->tag(), pos);
        if (one.empty()) return std::string();
        baseTypeInfo.push_back(one);
    }

    const bool own = cls->unqualified()->tag() == tag;
    const std::string ts = own ? itaniumClassTypeNameSymbol(cls)
                               : itaniumClassTypeNameSymbol(tag);
    p_.classSymbols_[tag].push_back(ts);
    const std::string text = own ? itaniumClassNameString(cls)
                                 : itaniumClassNameString(tag);
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= text.size(); i++)             // the NUL too
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                       i < text.size() ? text[i] : 0,
                                       std::string() });
    const Type *chars = p_.types_.arrayOf(p_.types_.get(Kind::Char),
                                       static_cast<long long>(text.size() + 1));
    // **Weak**: every translation unit that sees the class emits these three,
    // and the linker folds them rather than rejecting them - which is what
    // clang does and what a program of more than one file needs.
    p_.current_->globals.push_back(Global{ ts, ts, chars, std::move(letters),
                                        true, false, true,
                                        std::string(), true });

    // Every field is a pointer or pointer-wide, except the two `unsigned int`
    // of a vmi class, so the layout is written in pointer widths: w.
    const int w = p_.pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{
        0, w, 2 * w,
        bases.empty() ? "_ZTVN10__cxxabiv117__class_type_infoE"
                      : simple ? "_ZTVN10__cxxabiv120__si_class_type_infoE"
                               : "_ZTVN10__cxxabiv121__vmi_class_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    int bytes = 2 * w;
    if (simple) {
        pieces.push_back(GlobalPiece{ 2 * w, w, 0, baseTypeInfo[0] });
        bytes = 3 * w;
    } else if (!bases.empty()) {
        // Two `unsigned int` after the header - the class's flags and the
        // number of bases - and then a pair of words per base: its `_ZTI` and
        // an offset with four flag bits under it.
        pieces.push_back(GlobalPiece{ 2 * w, 4, itaniumVmiFlags(cls),
                                      std::string() });
        pieces.push_back(GlobalPiece{ 2 * w + 4, 4,
                                      static_cast<long long>(bases.size()),
                                      std::string() });
        int at = 2 * w + 8;
        for (std::size_t i = 0; i < bases.size(); i++) {
            pieces.push_back(GlobalPiece{ at, w, 0, baseTypeInfo[i] });
            // **`__public_mask` is 2 and `__virtual_mask` is 1**, and the offset above them is
            // where the base *is* - except for a virtual one, where it is where its `vbase_offset`
            // sits in the vtable, a negative number the runtime reads through the object's vptr.
            long long flags = bases[i].access == Access::Public ? 2 : 0;
            long long where = bases[i].offset;
            if (bases[i].isVirtual) {
                flags |= 1;
                where = itaniumVbaseOffsetSlot(cls, bases[i].type);
            }
            pieces.push_back(GlobalPiece{ at + w, w, (where << 8) | flags,
                                          std::string() });
            at += 2 * w;
        }
        bytes = at;
    }

    const Type *word = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *object = p_.types_.arrayOf(word, (bytes + w - 1) / w);
    p_.current_->globals.push_back(Global{ ti, ti, object, std::move(pieces),
                                        true, false, true,
                                        std::string(), true });
    return ti;
}

// **The Microsoft vbtable**, which has no Itanium counterpart: that ABI keeps
// a virtual base's offset in the vftable and this one keeps it in a table of
// its own, reached through a second pointer.
void Classes::emitVbtable(const Type *cls, const std::string &tag,
                         std::size_t pos) {
    if (!p_.target_.microsoftNames()) return;
    const Type *plain = cls->unqualified();
    const std::vector<Type::VbPtr> &ptrs = plain->vbptrs();
    (void)pos;

    // **One table per vbptr, and the name says which.**
    for (std::size_t p = 0; p < ptrs.size(); p++) {
        const int vbp = ptrs[p].offset;
        const std::string symbol =
            ptrs.size() == 1 || ptrs[p].base == nullptr
                ? vbtableSymbol(tag)
                : vbtableSymbol(tag, ptrs[p].base->tag());
        bool already = false;
        for (std::size_t i = 0; i < p_.current_->globals.size(); i++)
            if (p_.current_->globals[i].symbol == symbol) already = true;
        if (already) continue;                              // one per class

        // **Entry 0 steps back to the top of the class that introduced the
        // pointer**, not to the top of this one. cl.
        const Type *owner = ptrs[p].owner != nullptr ? ptrs[p].owner : plain;
        std::vector<GlobalPiece> pieces;
        int at = 0;
        pieces.push_back(GlobalPiece{ at, 4,
                                      -static_cast<long long>(
                                          owner->vbptrOffset()),
                                      std::string() });
        at += 4;

        // **Which bases the table names, and in whose order.**
        const std::vector<Type::BaseSpec> &bs = plain->bases();
        std::vector<const Type *> want;
        if (p != 0 && ptrs[p].base != nullptr) {
            const std::vector<Type::BaseSpec> &theirs =
                ptrs[p].base->bases();
            for (std::size_t i = 0; i < theirs.size(); i++)
                if (theirs[i].isVirtual) want.push_back(theirs[i].type);
        } else {
            for (std::size_t i = 0; i < bs.size(); i++)
                if (bs[i].isVirtual) want.push_back(bs[i].type);
        }

        for (std::size_t i = 0; i < want.size(); i++) {
            // Where that base sits **in this object**, which is the whole
            // point of the class writing a table of its own.
            int where = -1;
            for (std::size_t j = 0; j < bs.size(); j++)
                if (bs[j].isVirtual && bs[j].type == want[i])
                    where = bs[j].offset;
            if (where < 0) continue;
            pieces.push_back(GlobalPiece{ at, 4,
                static_cast<long long>(where - vbp), std::string() });
            at += 4;
        }

        const Type *entry = p_.types_.intType();
        const Type *table = p_.types_.arrayOf(entry,
                                           static_cast<long long>(
                                               pieces.size()));
        p_.current_->globals.push_back(Global{ symbol, symbol, table,
                                            std::move(pieces), true, false,
                                            true, std::string(), true });
    }
}

void Classes::emitVtable(const Type *cls, const std::string &tag,
                        std::size_t pos) {
    if (tag.empty())
        p_.src_.fail(pos, "a class with a virtual function needs a name - its "
                       "vtable is a symbol, and an anonymous class has none");

    const std::vector<VSlot> &slots = p_.vtables_[tag];
    const bool ms = p_.target_.microsoftNames();
    const std::string symbol = cls->unqualified()->tag() == tag
                             ? vtableSymbol(cls, ms) : vtableSymbol(tag, ms);

    for (std::size_t i = 0; i < p_.current_->globals.size(); i++)
        if (p_.current_->globals[i].symbol == symbol) return;   // one per class
    p_.classSymbols_[tag].push_back(symbol);

    // **The table holding a function's address is a use of it.** The `used` flag came
    // only from calls, so a class with an implicit virtual destructor got a table
    // pointing at a `~D` nothing emitted. Marked during the class's own completion.
    for (std::size_t i = 0; i < slots.size(); i++)
        p_.markSymbolUsed(slots[i].symbol);
    // **And the destructor itself, which the Microsoft table does not name.**
    if (const Signature *dtor = p_.destructorOf(cls)) p_.markSymbolUsed(dtor->symbol);

    // **The Itanium group is its own file's work** - ParserVtable.cpp - and
    // the Microsoft vftable is the one table below.
    if (!ms) {
        emitItaniumVtables(cls, tag, symbol, pos);
        return;
    }

    std::vector<GlobalPiece> pieces;
    const int w = p_.pointerBytes();                  // one entry
    int at = 0;
    for (std::size_t i = 0; i < slots.size(); i++) {
        pieces.push_back(GlobalPiece{ at, w, 0, slots[i].symbol });
        at += w;
    }

    // **The Microsoft ABI arranges a second polymorphic base differently,
    // and it is not the same thing under other names.** Measured with
    // clang: two vftable symbols rather than one table in two parts.
    const std::vector<Type::BaseSpec> &bases = cls->bases();
    for (std::size_t bi = 1; bi < bases.size(); bi++) {
        const Type *b = bases[bi].type;
        if (!b->hasVptr() || bases[bi].isVirtual) continue;
        if (b->polymorphic())
            p_.src_.fail(pos, "'" + tag + "' has virtual functions in a base "
                           "that is not the first, and the Microsoft ABI "
                           "lays that out differently - two vftable "
                           "symbols rather than one table in two parts. "
                           "Not supported yet; it is measured for Itanium "
                           "only");
    }

    // **The Microsoft locator goes in front of the table, not behind it.**
    std::string locatorWord;
    if (cls->bases().size() <= 1) {
        MicrosoftRtti names;
        std::string why;
        if (microsoftClassRttiNames(cls, &names, &why)) {
            locatorWord = names.locator;
            bool had = false;
            for (std::size_t i = 0; i < p_.current_->rtti.size(); i++)
                if (p_.current_->rtti[i] == cls) had = true;
            if (!had) p_.current_->rtti.push_back(cls);
        }
    }

    const Type *entry = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *table = p_.types_.arrayOf(entry, static_cast<long long>(pieces.size()));
    p_.current_->globals.push_back(Global{ symbol, symbol, table, std::move(pieces),
                                        true, false, true, locatorWord, true });
}

// **A default constructor is one that can be called with no arguments, not one whose
// parameter list is empty** - [class.ctor]/5, so `S(int a = 1)` is one. Whoever calls
// this still supplies the defaults; two that both take nothing answer nullptr.
const Parser::Signature *Classes::defaultConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(Parser::constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    const Signature *found = nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = p_.functions_[(*set)[k]];
        if (p_.overload_.leastArguments(f) != 0) continue;
        if (found != nullptr) return nullptr;
        found = &f;
    }
    return found;
}

const Parser::Signature *Classes::copyConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(Parser::constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = p_.functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isReference()) continue;
        // **`S(S &&)` is not a copy constructor**, and until rung 7 there was no way
        // to write one, so isReference() alone was enough. Answering one here would
        // hand an lvalue to a constructor whose whole contract is that it gets none.
        if (f.params[0]->isRValueReference()) continue;
        if (f.params[0]->referent()->unqualified() != cls->unqualified()) continue;
        return &f;
    }
    return nullptr;
}

// `S(S &&)`, written by hand - the compiler does not yet write one. The
// mirror of copyConstructorOf and told apart from it by exactly one thing,
// which is the kind of reference the parameter is.
const Parser::Signature *Classes::moveConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(Parser::constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = p_.functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isRValueReference()) continue;
        if (f.params[0]->referent()->unqualified() != cls->unqualified()) continue;
        return &f;
    }
    return nullptr;
}

// **Only `X`, `X &`, `const X &` - or `X &&`, the move - make an assignment a copy**,
// [class.copy]/17: `operator=(const double *)` is an operator of the class and not its
// copy, which the class still has. The copy is answered before the move.
const Parser::Signature *Classes::copyAssignOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(Parser::assignmentKey(cls->tag()));
    if (set == nullptr) return nullptr;
    const Signature *move = nullptr;
    for (std::size_t i = 0; i < set->size(); i++) {
        const Signature &f = p_.functions_[(*set)[i]];
        if (f.params.size() != 1) continue;
        const Type *p = f.params[0];
        const bool ref = p->isReference();
        if ((ref ? p->pointee() : p)->unqualified() != cls->unqualified()) continue;
        if (ref && p->isRValueReference()) { if (move == nullptr) move = &f; continue; }
        return &f;
    }
    return move;
}

// The move assignment of a class, `X &operator=(X &&)`, written or defaulted; copyAssignOf answers the copy first.
const Parser::Signature *Classes::moveAssignOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty()) return nullptr;
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(Parser::assignmentKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t i = 0; i < set->size(); i++) {
        const Signature &f = p_.functions_[(*set)[i]];
        if (f.params.size() == 1 && f.params[0]->isRValueReference() &&
            f.params[0]->referent()->unqualified() == cls->unqualified()) return &f;
    }
    return nullptr;
}

// **A trivial special member is not a function**, and that is measured rather than
// reasoned: cl and clang emit no symbol for one with no work to do. So an implicit
// member is declared only where it has some - a vptr, or a base or member to build.
void Classes::declareImplicitSpecials(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (tag.empty()) return;
    // Asked before the copy constructor is declared, because declaring one would answer it yes.
    const bool wroteConstructor = p_.hasConstructors(tag);
    // [class.union]/4: a union's one member initialiser is applied by its implicit default
    // constructor, the only special member of a union of trivial members that has work.
    if (type->kind() == Kind::Union) {
        if (!wroteConstructor && p_.hasMemberInitialiser(tag))
            declareImplicitDefaultCtor(tag, type, pos, Access::Public);
        return;
    }
    // A member written `= default` is user-declared for [class.copy]/7 and /9 and implicit for everything else.
    const Parser::Defaulted df = p_.defaulted_.count(tag) != 0 ? p_.defaulted_[tag] : Parser::Defaulted();
    // **Read now, as above.** After the three calls below these all answer yes for a class that wrote nothing,
    // and [class.copy]/9 asks what the *user* declared; a written copy or move assignment is one of its five
    // (2026-10-21, review of 1.25, fix 4). copyAssignOf also sees a lone move assignment.
    const bool wroteCopyOrDtor = copyConstructorOf(type) != nullptr ||
                                 moveConstructorOf(type) != nullptr ||
                                 copyAssignOf(type) != nullptr ||
                                 p_.lookup_.overloadsOf(Parser::destructorKey(tag)) != nullptr ||
                                 df.copyCtor.is || df.copyAssign.is || df.moveAssign.is || df.dtor.is;
    // [class.copy]/7 and /18: a defaulted move constructor or move assignment deletes the implicit copies.
    declareImplicitDestructor(tag, type, pos);
    declareImplicitCopyCtor(tag, type, pos);
    declareImplicitCopyAssign(tag, type, pos);
    // [class.copy]/20: the implicit move assignment, where none of the five was written - a
    // defaulted move constructor counts, declared below as it is (2026-10-21, fix 4: there
    // was none, and `b = std::move(a)` ran the copy assignment).
    if (df.moveAssign.is) declareDefaultedMoveAssign(tag, type, pos);
    else declareImplicitMoveAssign(tag, type, pos, wroteCopyOrDtor || df.moveCtor.is);
    // Before the `wroteConstructor` return below: writing a constructor of
    // your own costs you the implicit *default* one and nothing else.
    declareImplicitMoveCtor(tag, type, pos, wroteCopyOrDtor && !df.moveCtor.is);
    p_.applyDefaultedAccess(tag, type);
    if (wroteConstructor && !df.defaultCtor.is) return;
    // [class.ctor]/5: only a user-declared constructor suppresses this one - a deleted implicit copy does not.
    const bool needDefault = df.defaultCtor.is || (!df.copyCtor.is && !df.moveCtor.is);
    if (!needDefault) return;

    // **An initialiser on a member is work**: without a constructor there is no function to put the
    // store in, and `S s;` would leave x holding the stack. So is a defaulted default constructor
    // beside a written one, since `S s;` then has to find a constructor to call.
    bool work = type->hasVptr() || p_.hasConstructors(tag) || Parser::nonPublicDefault(df.defaultCtor);
    for (std::size_t i = 0; i < type->members().size() && !work; i++)
        if (p_.memberInit_.find(tag + "::" + type->members()[i].name) !=
            p_.memberInit_.end())
            work = true;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (!bs[i].type->tag().empty() &&
            p_.hasConstructors(bs[i].type->tag()))
            work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (mc != nullptr && !mc->tag().empty() &&
            p_.hasConstructors(mc->tag()))
            work = true;
    }
    if (!work) return;
    declareImplicitDefaultCtor(tag, type, pos,
                               df.defaultCtor.is ? df.defaultCtor.access : Access::Public);
}

// The implicit default constructor's signature, named for the linker; its body is written on use.
void Classes::declareImplicitDefaultCtor(const std::string &tag, const Type *type,
                                        std::size_t pos, Access access) {
    const std::vector<const Type *> params;
    const Type *fn = p_.types_.functionType(p_.types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = p_.target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        p_.src_.fail(pos, "'" + tag + "' needs a default constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    p_.functionIndex_[Parser::constructorKey(tag)].push_back(p_.functions_.size());
    p_.functions_.push_back(Signature{ tag, out, p_.types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false, access, false });
    p_.functions_.back().implicit = true;
}

// **The destructor the class did not write**, which becomes a function exactly when a
// base or a member has one of its own to run - measured with cl. **A virtual function
// does not make it non-trivial**; a base whose destructor is virtual makes it virtual.
void Classes::declareImplicitDestructor(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (p_.lookup_.overloadsOf(Parser::destructorKey(tag)) != nullptr) return;

    bool work = false;
    bool isVirtual = false;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (const Signature *d = p_.destructorOf(bs[i].type)) {
            work = true;
            if (d->isVirtual) isVirtual = true;
        }
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (p_.destructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (Parser::nonPublicDefault(p_.defaultedFor(tag).dtor)) work = true;
    if (!work) return;

    p_.registerDestructor(tag, pos, Access::Public, isVirtual, true);
}

// Its body: the members this class added, in the reverse of the order they
// were declared, then the bases in the reverse of theirs.
std::vector<StmtPtr> Classes::memberDestructors(const std::string &cls,
                                               const Type *type, int thisSlot,
                                               std::size_t pos) {
    std::vector<StmtPtr> out;
    const std::vector<Member> &ms = type->members();
    for (std::size_t n = ms.size(); n-- > 0; )
        if (!memberFromBase(type, ms[n])) memberDestructor(out, cls, type, thisSlot, n, pos);
    return out;
}

void Classes::memberDestructor(std::vector<StmtPtr> &out, const std::string &cls, const Type *type,
                              int thisSlot, std::size_t n, std::size_t pos) {
    const std::vector<Member> &ms = type->members();
    const Type *mt = ms[n].type;
    long long elemCount = 1;
    const Type *elem = mt->isArray() ? memberElements(mt, &elemCount) : mt;
    const Signature *dtor = p_.destructorOf(memberClass(mt));
    if (dtor == nullptr) return;
    if (dtor->access != Access::Public)
        p_.src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                       "the compiler would write: the destructor of '" +
                       memberClass(mt)->tag() + "', the type of '" +
                       ms[n].name + "', is " +
                       (dtor->access == Access::Private ? "private"
                                                        : "protected"));

    int indexSlot = 0;
    long long count = 0;
    if (mt->isArray()) {
        count = elemCount;
        if (count < 0)
            p_.src_.fail(pos, "'" + cls + "::" + ms[n].name + "' has no length, "
                           "so the destructor the compiler would write does "
                           "not know how many elements to destroy");
        indexSlot = p_.allocateFrameSlot(p_.types_.intType());
    }

    ExprPtr acc = p_.thisMember(thisSlot, type, ms[n]);

    ExprPtr address;
    if (mt->isArray()) {
        // **Backwards**, because an array is destroyed in the reverse of
        // the order it was built: the index counts up and the element it
        // reaches is (count - 1 - i).
        const Type *idx = p_.types_.intType();
        ExprPtr last(new Num(count - 1));
        last->setType(idx);
        ExprPtr i(Var::local("$i", indexSlot));
        i->setType(idx);
        ExprPtr back(new Binary(BinOp::Sub, std::move(last), std::move(i)));
        back->setType(idx);
        ExprPtr size(new Num(static_cast<long long>(elem->size(p_.target_))));
        size->setType(idx);
        ExprPtr off(new Binary(BinOp::Mul, std::move(back), std::move(size)));
        off->setType(idx);
        const Type *ptr = p_.types_.pointerTo(elem->unqualified());
        ExprPtr at(new Binary(BinOp::Add, p_.decay(std::move(acc)),
                              std::move(off)));
        at->setType(ptr);
        address = std::move(at);
    } else {
        address = ExprPtr(new Unary('&', std::move(acc)));
        address->setType(p_.types_.pointerTo(elem->unqualified()));
    }

    StmtPtr one(new ExprStmt(p_.destructorCall(std::move(address), *dtor, pos)));
    out.push_back(mt->isArray()
                   ? p_.eachElement(indexSlot, count, std::move(one))
                   : std::move(one));
}

void Classes::synthesizeDestructor(std::size_t which) {
    const std::string cls = p_.functions_[which].owner;
    const std::size_t pos = p_.functions_[which].pos;
    const std::string symbol = p_.functions_[which].symbol;
    const bool isVirtual = p_.functions_[which].isVirtual;
    const Type *type = p_.lookup_.findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const Parser::FrameSave savedFrame = p_.openFrame();
    const Type *self = p_.types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = p_.allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    const int savedVtt = p_.vttSlot_;
    p_.vttSlot_ = -1;
    if (p_.takesVtt(type)) {
        p_.vttSlot_ = p_.allocateFrameSlot(p_.vttType());
        params.push_back(Param{ p_.vttType(), p_.vttSlot_ });
    }

    std::vector<StmtPtr> body;

    const std::vector<Type::BaseSpec> &bs = type->bases();

    // The members, then the bases below - [class.dtor]/8's order. The member
    // half is shared with the destructor the program writes, which is where it
    // was missing.
    {
        std::vector<StmtPtr> mine = memberDestructors(cls, type, thisSlot, pos);
        for (std::size_t i = 0; i < mine.size(); i++)
            body.push_back(std::move(mine[i]));
    }

    for (std::size_t n = bs.size(); n-- > 0; ) {
        const Type *base = bs[n].type;
        // D1 destroys the virtual bases, after this body has run - the mirror
        // of C1 building them before it. See synthesizeCompleteDtor.
        if (bs[n].isVirtual) continue;
        const Signature *dtor = p_.destructorOf(base);
        if (dtor == nullptr) continue;
        if (dtor->access != Access::Public)
            p_.src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                           "the compiler would write: the destructor of its "
                           "base '" + base->tag() + "' is " +
                           (dtor->access == Access::Private ? "private"
                                                            : "protected"));
        // **Calling one is what asks for a body**, and this is the only synthesiser that built its call by hand and forgot to say so.
        p_.markUsed(dtor);
        // The base-subobject form, D2, which is what a derived class calls -
        // the same name a written destructor reaches for.
        std::string sym = dtor->symbol;
        if (!p_.target_.microsoftNames())
            itaniumDestructorName(base->tag(), base, false, &sym);

        const Type *basePtr = p_.types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[n].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = p_.convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        if (p_.takesVtt(base)) {
            args.push_back(vttForBase(type, base));
            ps.push_back(p_.vttType());
        }
        body.push_back(StmtPtr(new ExprStmt(
            p_.completeCall("~" + base->tag(), sym, nullptr, p_.types_.get(Kind::Void),
                         ps, false, pos, std::move(args)))));
    }
    p_.vttSlot_ = savedVtt;

    p_.current_->functions.push_back(Function(cls + "::~" + Parser::localOf(cls),
                                           p_.types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(symbol);
    p_.current_->functions.back().setInline(true);
    // The Microsoft wrapper: `??1` above stops at this class's own part, and
    // `??_D` destroys it and then the virtual bases.
    if (p_.target_.microsoftNames() && type->hasVirtualBase()) {
        p_.closeFrame(savedFrame);
        synthesizeCompleteDtor(type, vbaseDestructorSymbol(cls), symbol, true,
                               pos);
    }
    if (!p_.target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(cls, type, false, &d2);
        if (type->hasVirtualBase()) {
            p_.current_->functions.back().setSymbol(d2);
            p_.closeFrame(savedFrame);
            synthesizeCompleteDtor(type, symbol, d2, true, pos);
        } else {
            p_.current_->functions.back().setAlias(d2);
        }
    }
    p_.closeFrame(savedFrame);

    // A virtual one carries the deleting form into the vtable beside it, the
    // same as a written virtual destructor does.
    if (isVirtual) synthesizeDeleting(cls, type, Access::Public, pos);
}

// The copy assignment operator the class did not write. The trivial line is drawn
// where the others are and measured the same way with cl. A polymorphic class is
// non-trivial even though the body leaves the vptr alone: it writes into its own.
void Classes::declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (copyAssignOf(type) != nullptr) return;
    const Parser::Defaulted df = p_.defaultedFor(tag);
    // [class.copy]/18: a move written `= default` deletes the implicit copy, not a `= default` one (1.25, fix 4).
    bool deleted = (df.moveCtor.is || df.moveAssign.is) && !df.copyAssign.is;
    // [class.copy]/23.
    if (moveConstructorOf(type) != nullptr && !deleted && !df.copyAssign.is) return;

    // **A const member has no assignment to give**, so the operator the compiler would
    // write is deleted rather than non-trivial and none is declared. Asked over every
    // member first: the search below stops at the first that gives it work.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].type->isConst()) return;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (Parser::deletedSpecial(copyAssignOf(bs[i].type))) deleted = true;
    for (std::size_t i = 0; i < ms.size(); i++)
        if (Parser::deletedSpecial(copyAssignOf(memberClass(ms[i].type)))) deleted = true;
    bool work = type->polymorphic() || deleted || Parser::nonPublicDefault(df.copyAssign);
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyAssignOf(bs[i].type) != nullptr) work = true;
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyAssignOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(p_.types_.referenceTo(p_.types_.withConst(type)));
    const Type *self = p_.types_.referenceTo(type);
    const Type *fn = p_.types_.functionType(self, params, false);
    std::string out, why;
    const bool ok = p_.target_.microsoftNames()
            ? microsoftCopyAssignName(tag, type, fn, 'Q', &out, &why)
            : itaniumCopyAssignName(tag, type, fn, &out, &why);
    if (!ok)
        p_.src_.fail(pos, "'" + tag + "' needs a copy assignment the compiler would "
                       "write, and it cannot be given a name the linker can "
                       "hold: " + why);

    p_.functionIndex_[Parser::assignmentKey(tag)].push_back(p_.functions_.size());
    p_.functions_.push_back(Signature{ "operator=", out, self, params, false, false,
                                    pos, false, tag, false, Access::Public,
                                    false });
    p_.functions_.back().implicit = true;
    p_.functions_.back().deleted = deleted;
}

// The implicit move assignment - [class.copy]/20: declared where none of /9's five is user-declared; trivial ones
// are not functions. One /23 would delete (a base or member neither movable nor copyable) is ignored by overload
// resolution (DR 1402), so it is left undeclared, as for a const member (2026-10-21, review of 1.25, fix 4).
void Classes::declareImplicitMoveAssign(const std::string &tag, const Type *type, std::size_t pos,
                                       bool userDeclared) {
    if (userDeclared) return;
    if (moveAssignOf(type) != nullptr) return;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].type->isConst()) return;
    // What each subobject's move would call: its move assignment, or its copy where it has none.
    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Signature *op = moveAssignOf(bs[i].type);
        if (op == nullptr) op = copyAssignOf(bs[i].type);
        if (Parser::deletedSpecial(op)) return;
        if (op != nullptr) work = true;
    }
    for (std::size_t i = 0; i < ms.size(); i++) {
        const Signature *op = moveAssignOf(memberClass(ms[i].type));
        if (op == nullptr) op = copyAssignOf(memberClass(ms[i].type));
        if (Parser::deletedSpecial(op)) return;
        if (op != nullptr) work = true;
    }
    if (!work) return;
    declareDefaultedMoveAssign(tag, type, pos);
}

// `X &operator=(X &&) = default;` - the move assignment the compiler writes: each subobject's own
// move assignment, or its copy where it has none ([class.copy]/28).
void Classes::declareDefaultedMoveAssign(const std::string &tag, const Type *type, std::size_t pos) {
    std::vector<const Type *> params;
    params.push_back(p_.types_.rvalueReferenceTo(type));
    const Type *self = p_.types_.referenceTo(type);
    const Type *fn = p_.types_.functionType(self, params, false);
    std::string out, why;
    const bool ok = p_.target_.microsoftNames()
            ? microsoftCopyAssignName(tag, type, fn, 'Q', &out, &why)
            : itaniumCopyAssignName(tag, type, fn, &out, &why);
    if (!ok)
        p_.src_.fail(pos, "'" + tag + "' needs a move assignment the compiler would write, and it "
                       "cannot be given a name the linker can hold: " + why);
    p_.functionIndex_[Parser::assignmentKey(tag)].push_back(p_.functions_.size());
    p_.functions_.push_back(Signature{ "operator=", out, self, params, false, false,
                                    pos, false, tag, false, Access::Public, false });
    p_.functions_.back().implicit = true;
}

// The copy constructor the class did not write, on the same measured line: a class
// writing any constructor still gets one and only writing a copy takes it away. The
// implicit *move* needs all five of [class.copy]/9's absences, two of them vacuous.
void Classes::declareImplicitMoveCtor(const std::string &tag, const Type *type,
                                     std::size_t pos, bool userDeclared) {
    if (userDeclared) return;
    if (moveConstructorOf(type) != nullptr) return;

    // A defaulted move is declared even where it is trivial: the copy it deletes would otherwise take the call.
    bool work = type->polymorphic() || p_.defaultedFor(tag).moveCtor.is;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (moveConstructorOf(bs[i].type) != nullptr ||
            copyConstructorOf(bs[i].type) != nullptr) work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (moveConstructorOf(mc) != nullptr ||
            copyConstructorOf(mc) != nullptr) work = true;
    }
    if (!work) return;

    std::vector<const Type *> params;
    // Not const, and that is the whole point of it: the source is going to be
    // taken apart, so the member moves below need to be able to write to it.
    params.push_back(p_.types_.rvalueReferenceTo(type));
    const Type *fn = p_.types_.functionType(p_.types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = p_.target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        p_.src_.fail(pos, "'" + tag + "' needs a move constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    p_.functionIndex_[Parser::constructorKey(tag)].push_back(p_.functions_.size());
    p_.functions_.push_back(Signature{ tag, out, p_.types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    p_.functions_.back().implicit = true;
}

void Classes::declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (copyConstructorOf(type) != nullptr) return;
    const Parser::Defaulted df = p_.defaultedFor(tag);
    // [class.copy]/7: a user-declared move constructor **deletes** the implicit copy; written by hand it
    // is left undeclared, and the copy is refused where the move is not viable. **Not a copy the class wrote
    // `= default`** (2026-10-21, review of 1.25, fix 4): /7 deletes the *implicitly-declared* one only.
    bool deleted = (df.moveCtor.is || df.moveAssign.is) && !df.copyCtor.is;
    if (moveConstructorOf(type) != nullptr && !deleted && !df.copyCtor.is) return;

    // [class.copy]/11: a base or member whose copy is deleted deletes this one.
    const std::vector<Type::BaseSpec> &bs = type->bases();
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (Parser::deletedSpecial(copyConstructorOf(bs[i].type))) deleted = true;
    for (std::size_t i = 0; i < ms.size(); i++)
        if (Parser::deletedSpecial(copyConstructorOf(memberClass(ms[i].type)))) deleted = true;
    // A defaulted copy beside a move is declared even where it is trivial, as the move is: an
    // lvalue call would otherwise find the move alone and have nothing viable.
    bool work = type->polymorphic() || deleted || Parser::nonPublicDefault(df.copyCtor) ||
                (df.copyCtor.is && (df.moveCtor.is || moveConstructorOf(type) != nullptr));
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyConstructorOf(bs[i].type) != nullptr) work = true;
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyConstructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(p_.types_.referenceTo(p_.types_.withConst(type)));
    const Type *fn = p_.types_.functionType(p_.types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = p_.target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        p_.src_.fail(pos, "'" + tag + "' needs a copy constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    p_.functionIndex_[Parser::constructorKey(tag)].push_back(p_.functions_.size());
    p_.functions_.push_back(Signature{ tag, out, p_.types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    p_.functions_.back().implicit = true;
    p_.functions_.back().deleted = deleted;
}

// **`using B::B;`**: each constructor of B but its default, copy and move ones becomes one of this
// class's, with B's parameters, defaults, access and `explicit`, building B from them and every other
// base and member as the implicit default constructor would - [class.inhctor] as P0136 has it.
void Classes::declareInheritingCtors(const std::string &tag, const Type *type, std::size_t pos) {
    auto it = p_.inheritFrom_.find(tag);
    if (it == p_.inheritFrom_.end()) return;
    const std::vector<const Type *> bases = it->second;
    for (const Type *base : bases) {
        if (p_.memberTemplates_.count(Parser::constructorKey(base->tag())) != 0)
            p_.src_.fail(pos, "inheriting '" + base->describe() + "''s constructor template is not "
                           "supported yet - its specializations would have to be made for the derived class");
        const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(Parser::constructorKey(base->tag()));
        if (set == nullptr) continue;
        const std::vector<std::size_t> from = *set;
        for (std::size_t idx : from) {
            const Signature b = p_.functions_[idx];
            if (b.deleted || b.owner != base->tag()) continue;
            // An inherited default constructor is D's own default one, by name and body, as clang has it.
            if (b.params.empty()) {
                bool has = false;
                if (const std::vector<std::size_t> *own = p_.lookup_.overloadsOf(Parser::constructorKey(tag)))
                    for (std::size_t k : *own)
                        if (p_.functions_[k].params.empty()) has = true;
                if (has) continue;
                const Type *fn0 = p_.types_.functionType(p_.types_.get(Kind::Void), std::vector<const Type *>(), false);
                const char code0 = b.access == Access::Public ? 'Q' : b.access == Access::Protected ? 'I' : 'A';
                std::string out0, why0;
                const bool ok0 = p_.target_.microsoftNames()
                        ? microsoftConstructorName(tag, type, fn0, code0, &out0, &why0)
                        : itaniumConstructorName(tag, type, fn0, true, &out0, &why0);
                if (!ok0)
                    p_.src_.fail(pos, "'" + tag + "''s default constructor cannot be given a name the linker "
                                   "can hold: " + why0);
                p_.functionIndex_[Parser::constructorKey(tag)].push_back(p_.functions_.size());
                p_.functions_.push_back(Signature{ tag, out0, p_.types_.get(Kind::Void), std::vector<const Type *>(),
                                                false, false, pos, false, tag, false, b.access, false });
                p_.functions_.back().implicit = true;
                continue;
            }
            if (b.params.size() == 1 && b.params[0]->isReference() &&
                b.params[0]->referent()->unqualified() == base->unqualified())
                continue;
            // One this class declared itself with the same parameters hides it.
            bool hidden = false;
            if (const std::vector<std::size_t> *own = p_.lookup_.overloadsOf(Parser::constructorKey(tag)))
                for (std::size_t k : *own)
                    if (sameParameters(p_.functions_[k].params, b.params)) hidden = true;
            if (hidden) continue;
            if (b.variadic)
                p_.src_.fail(pos, "inheriting '" + base->tag() + "''s constructor with '...' is not "
                               "supported yet - its arguments would have to be forwarded as they came");
            for (const Type *p : b.params)
                if (!p->isReference() && p_.passedByAddress(p))
                    p_.src_.fail(pos, "inheriting '" + base->tag() + "''s constructor that takes '" +
                                   p->describe() + "' by value is not supported yet - the copy "
                                   "would have to be passed on rather than made twice");
            const Type *fn = p_.types_.functionType(p_.types_.get(Kind::Void), b.params, false);
            auto chained = p_.inheritedFrom_.find(b.symbol);
            const Type *origin = chained != p_.inheritedFrom_.end() ? chained->second : base;
            const char code = b.access == Access::Public ? 'Q' : b.access == Access::Protected ? 'I' : 'A';
            std::string out, why;
            const bool ok = p_.target_.microsoftNames()
                    ? microsoftConstructorName(tag, type, fn, code, &out, &why)
                    : itaniumInheritingConstructorName(tag, type, origin, fn, true, &out, &why);
            if (!ok)
                p_.src_.fail(pos, "'" + tag + "''s constructor inherited from '" + base->tag() +
                               "' cannot be given a name the linker can hold: " + why);
            p_.functionIndex_[Parser::constructorKey(tag)].push_back(p_.functions_.size());
            p_.functions_.push_back(Signature{ tag, out, p_.types_.get(Kind::Void), b.params,
                                            false, false, pos, false, tag, false, b.access, false });
            p_.functions_.back().implicit = true;
            p_.functions_.back().isExplicit = b.isExplicit;
            auto dflt = p_.defaultArgs_.find(b.symbol);
            if (dflt != p_.defaultArgs_.end()) {
                p_.defaultArgs_[out] = dflt->second;
                auto ns = p_.defaultArgNamespace_.find(b.symbol);
                if (ns != p_.defaultArgNamespace_.end()) p_.defaultArgNamespace_[out] = ns->second;
            }
            p_.inheriting_[p_.functions_.size() - 1] = Parser::Inheriting{ base, idx };
            p_.inheritedFrom_[out] = origin;
        }
    }
}

// The body of a default constructor nobody wrote: the bases in the order they were
// written, then the vptrs, then the members with constructors of their own. Scalars
// are left alone, as [dcl.init] has it; an inheriting constructor's forwards its parameters to its base.
void Classes::synthesizeDefaultCtor(std::size_t which) {
    const std::string cls = p_.functions_[which].owner;
    const std::size_t pos = p_.functions_[which].pos;
    const std::string symbol = p_.functions_[which].symbol;
    const Type *type = p_.lookup_.findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const Parser::FrameSave savedFrame = p_.openFrame();
    Parser::OwnPads own(p_);
    const Type *self = p_.types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = p_.allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    // Itanium's VTT second, for the same reason a written constructor takes
    // one; cl's hidden most-derived flag last, likewise.
    const int savedVtt = p_.vttSlot_;
    p_.vttSlot_ = -1;
    if (p_.takesVtt(type)) {
        p_.vttSlot_ = p_.allocateFrameSlot(p_.vttType());
        params.push_back(Param{ p_.vttType(), p_.vttSlot_ });
    }
    // An inheriting constructor's own parameters, B's: a reference arrives as its address.
    const auto inh = p_.inheriting_.find(which);
    const Type *inhBase = inh != p_.inheriting_.end() ? inh->second.base : nullptr;
    const std::vector<const Type *> forwarded = p_.functions_[which].params;
    std::vector<int> fwdSlots;
    for (const Type *p : forwarded) {
        const Type *slotType = p->isReference() ? p_.types_.pointerTo(p->referent()) : p;
        fwdSlots.push_back(p_.allocateFrameSlot(slotType));
        params.push_back(Param{ slotType, fwdSlots.back() });
    }
    int flagSlot = -1;
    if (p_.target_.microsoftNames() && type->hasVirtualBase()) {
        flagSlot = p_.allocateFrameSlot(p_.types_.intType());
        params.push_back(Param{ p_.types_.intType(), flagSlot });
    }

    std::vector<StmtPtr> body;
    // What each statement of `body` finishes building, for [except.ctor]/3.
    std::vector<Parser::Alive> done;
    // cl's virtual bases, built under the most-derived flag and guarded around the rest, [except.ctor]/3.
    std::vector<Parser::Alive> vbs;
    std::vector<StmtPtr> vbInit =
        p_.guardedVirtualBaseInit(type, cls, thisSlot, flagSlot, pos, -1, false, nullptr, &vbs);

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        if (base->tag().empty()) continue;
        // **A virtual base belongs to C1, not to this body.** Two things go
        // wrong when it is built here.
        if (bs[i].isVirtual) continue;
        if (!p_.hasConstructors(base->tag())) continue;
        const bool inherited = inhBase != nullptr && base->unqualified() == inhBase->unqualified();
        const Signature *ctor = inherited ? &p_.functions_[inh->second.from] : defaultConstructorOf(base);
        if (ctor == nullptr)
            p_.src_.fail(pos, "'" + cls + "' has no constructor of its own, and the "
                           "one the compiler would write cannot build its base '" +
                           base->tag() + "', which has no constructor taking "
                           "nothing - write a constructor for '" + cls + "' with "
                           "': " + base->tag() + "(...)' in its initialiser list");
        if (!p_.accessibleFrom(type, base, ctor->access))
            p_.src_.fail(pos, "'" + cls + "' cannot be built by the constructor the "
                           "compiler would write: the constructor of its base '" +
                           base->tag() + "' taking nothing is " +
                           (ctor->access == Access::Private ? "private"
                                                            : "protected"));
        p_.markUsed(ctor);
        // Copied before the defaults are read - see constructLocalArray.
        const Signature chosen = *ctor;
        std::vector<ExprPtr> defaults;
        if (inherited) {
            // Forwarded as they came: an lvalue reference as the object, an rvalue one as an xvalue.
            for (std::size_t k = 0; k < forwarded.size(); k++) {
                const Type *p = forwarded[k];
                ExprPtr v(Var::local("$inh" + std::to_string(k), fwdSlots[k]));
                if (p->isReference()) {
                    v->setType(p_.types_.pointerTo(p->referent()));
                    ExprPtr obj(new Unary('*', std::move(v)));
                    obj->setType(p->referent());
                    if (p->kind() == Kind::RValueRef) obj->setXvalue();
                    defaults.push_back(std::move(obj));
                } else {
                    v->setType(p);
                    defaults.push_back(std::move(v));
                }
            }
        } else {
            p_.applyDefaults(chosen, defaults, pos);
        }
        const std::string sym = p_.linkage_.baseConstructorSymbol(chosen, base);

        const Type *basePtr = p_.types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = p_.convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        if (p_.takesVtt(base)) {
            args.push_back(vttForBase(type, base));
            ps.push_back(p_.vttType());
        }
        for (std::size_t k = 0; k < defaults.size(); k++) {
            args.push_back(std::move(defaults[k]));
            ps.push_back(chosen.params[k]);
        }
        // A base subobject, so cl's flag is 0: its virtual bases are this
        // class's to build, and it built them above.
        body.push_back(StmtPtr(new ExprStmt(
            p_.completeCall(base->tag(), sym, nullptr, p_.types_.get(Kind::Void), ps,
                         false, pos, std::move(args), false, 0))));
        done.resize(body.size());
        if (p_.destructorOf(base) != nullptr) {
            done.back() = Parser::Alive{ "this", thisSlot, type };
            done.back().owner = type;
            done.back().base = static_cast<int>(i);
        }
    }

    // The vptr is written for either reason it exists.
    if (type->hasVptr()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    // The members, in declaration order, each by the one rule that applies to it: the
    // initialiser the class wrote on it, or else its own default constructor. **One
    // walk, not two** - `M m = M(2);` used to be stored and then built over.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        // **A base's members are this class's list too**, the layout having
        // copied them down - and the base's own constructor has already built
        // them by the time this body runs.
        if (memberFromBase(type, ms[i])) continue;
        StmtPtr one = p_.memberInitialiser(cls, type, ms[i], thisSlot, pos);
        std::vector<ExprPtr> none;
        if (one == nullptr && type->kind() != Kind::Union && ms[i].anonUnion == nullptr)
            one = p_.constructMember(cls, type, ms[i], thisSlot, none, pos, true);
        if (one == nullptr) continue;
        body.push_back(std::move(one));
        done.resize(body.size());
        if (p_.memberHasDestructor(ms[i].type)) {
            done.back() = Parser::Alive{ "this", thisSlot, type };
            done.back().owner = type;
            done.back().member = static_cast<int>(i);
        }
    }

    bool guarded = false;
    StmtPtr built = p_.withVirtualBases(std::move(vbInit), vbs,
                                     p_.guardConstruction(std::move(body), std::move(done), pos, &guarded),
                                     pos);
    p_.current_->functions.push_back(Function(cls + "::" + cls, p_.types_.get(Kind::Void),
                                           std::move(params), std::move(built),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(symbol);
    p_.current_->functions.back().setHasLandingPads(guarded || own.made());
    p_.current_->functions.back().setInline(true);
    // The same two names a written constructor is emitted under: C1 for a
    // complete object and C2 for a base subobject, the second a label in front
    // of the first. Microsoft has one name and wants no alias.
    if (!p_.target_.microsoftNames() && inhBase != nullptr) {
        const Type *fnType = p_.types_.functionType(p_.types_.get(Kind::Void), forwarded, false);
        std::string c2, why;
        auto origin = p_.inheritedFrom_.find(symbol);
        if (itaniumInheritingConstructorName(cls, type, origin != p_.inheritedFrom_.end() ? origin->second : inhBase,
                                             fnType, false, &c2, &why)) {
            // A virtual base of the class splits it as it splits any constructor: this body is CI2.
            if (type->hasVirtualBase()) {
                p_.current_->functions.back().setSymbol(c2);
                p_.closeFrame(savedFrame);
                p_.vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, forwarded, symbol, c2, true, pos);
                return;
            }
            p_.current_->functions.back().setAlias(c2);
        }
    } else if (!p_.target_.microsoftNames()) {
        const Type *fnType = p_.types_.functionType(p_.types_.get(Kind::Void),
                                                 std::vector<const Type *>(), false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why)) {
            // **A virtual base splits this the same way it splits a written
            // constructor.** The walk above skipped them, so this body is C2;
            // C1 builds them and calls it.
            if (type->hasVirtualBase()) {
                p_.current_->functions.back().setSymbol(c2);
                p_.closeFrame(savedFrame);
                p_.vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, std::vector<const Type *>(),
                                       symbol, c2, true, pos);
                return;
            }
            p_.current_->functions.back().setAlias(c2);
        }
    }
    p_.closeFrame(savedFrame);
    p_.vttSlot_ = savedVtt;
}

// The body of a copy constructor nobody wrote: the bases with one of their own, then
// the vptrs, then every member no base copied. **The vptr is set and not copied** -
// measured in cl's listing. Members go one at a time, for the tail-padding rule.
void Classes::synthesizeCopy(std::size_t which, bool assigning) {
    const std::string cls = p_.functions_[which].owner;
    const std::size_t pos = p_.functions_[which].pos;
    const std::string symbol = p_.functions_[which].symbol;
    const Type *srcRef = p_.functions_[which].params[0];
    const Type *type = p_.lookup_.findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    // **The signature says which of the three this is**, so nothing that calls
    // this had to learn about moves: an implicit constructor whose parameter
    // is `X &&` is the move constructor and there is nothing else it could be.
    const bool moving = srcRef->isRValueReference();
    const char *kind = assigning ? (moving ? "move assignment" : "copy assignment")
                     : moving    ? "move constructor"
                                 : "copy constructor";

    const Parser::FrameSave savedFrame = p_.openFrame();
    Parser::OwnPads own(p_);
    const Type *self = p_.types_.pointerTo(type);
    const Type *srcPtr = p_.types_.pointerTo(srcRef->referent());
    std::vector<Param> params;
    const int thisSlot = p_.allocateFrameSlot(self);
    const int thatSlot = p_.allocateFrameSlot(srcPtr);
    params.push_back(Param{ self, thisSlot });
    // The VTT and cl's most-derived flag, for a *constructor* of such a
    // class: assignment takes neither, having no virtual bases to build.
    const int savedVtt = p_.vttSlot_;
    p_.vttSlot_ = -1;
    if (!assigning && p_.takesVtt(type)) {
        p_.vttSlot_ = p_.allocateFrameSlot(p_.vttType());
        params.push_back(Param{ p_.vttType(), p_.vttSlot_ });
    }
    params.push_back(Param{ srcPtr, thatSlot });
    int flagSlot = -1;
    if (!assigning && p_.target_.microsoftNames() && type->hasVirtualBase()) {
        flagSlot = p_.allocateFrameSlot(p_.types_.intType());
        params.push_back(Param{ p_.types_.intType(), flagSlot });
    }

    std::vector<StmtPtr> body;
    // What each statement of `body` finishes building - a constructor's, for [except.ctor]/3.
    std::vector<Parser::Alive> finished;
    std::vector<Parser::Alive> vbs;
    std::vector<StmtPtr> vbInit;
    {
        // **Copied out of the source's own virtual base**, which is what the
        // Itanium C1 does with the argument forwarded to it.
        vbInit = p_.guardedVirtualBaseInit(type, cls, thisSlot, flagSlot, pos,
                                        thatSlot, moving, nullptr, assigning ? nullptr : &vbs);
    }

    // What a base's own copy constructor has already dealt with.
    std::vector<std::pair<int, int> > taken;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        // **A virtual base is copied by C1**, for the reason the default constructor records.
        if (bs[i].isVirtual && !assigning) continue;
        // **A member or base without a move constructor is copied, not refused.**
        // [class.copy]/15: the implicit move moves each subobject, and moving
        // something that has only a copy is what its copy constructor does.
        const Signature *cc = nullptr;
        if (assigning && moving) cc = moveAssignOf(base);
        if (assigning && cc == nullptr) cc = copyAssignOf(base);
        else if (!assigning) {
            if (moving) cc = moveConstructorOf(base);
            if (cc == nullptr) cc = copyConstructorOf(base);
        }
        if (cc == nullptr) continue;              // trivial: its members copy below
        if (!p_.accessibleFrom(type, base, cc->access))
            p_.src_.fail(pos, std::string("'") + cls + "' cannot be built by the " +
                           kind + " the compiler would write: the " + kind +
                           " of its base '" + base->tag() + "' is " +
                           (cc->access == Access::Private ? "private" : "protected"));
        p_.markUsed(cc);
        const std::string sym = assigning ? cc->symbol
                                          : p_.linkage_.baseConstructorSymbol(*cc, base);
        const Type *basePtr = p_.types_.pointerTo(base);

        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = p_.convert(std::move(me), basePtr);
        }
        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        from = p_.convert(std::move(from),
                       p_.types_.pointerTo(moving ? base : p_.types_.withConst(base)));
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(base);
        if (moving) fromObj->setXvalue();

        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        if (!assigning && p_.takesVtt(base)) {
            args.push_back(vttForBase(type, base));
            ps.push_back(p_.vttType());
        }
        args.push_back(std::move(fromObj));
        ps.push_back(cc->params[0]);
        body.push_back(StmtPtr(new ExprStmt(
            p_.completeCall(base->tag(), sym, nullptr, cc->returns, ps,
                         false, pos, std::move(args), false,
                         assigning ? 1 : 0))));
        finished.resize(body.size());
        if (!assigning && p_.destructorOf(base) != nullptr) {
            finished.back() = Parser::Alive{ "this", thisSlot, type };
            finished.back().owner = type;
            finished.back().base = static_cast<int>(i);
        }
        taken.push_back(std::make_pair(bs[i].offset,
                                       bs[i].offset + base->dataSize()));
    }

    // **A copy constructor sets the vptr; a copy assignment leaves it alone.** That is
    // the whole difference between the two bodies, measured in cl's own listing:
    // assignment writes into an object that already exists and is already this class.
    if (!assigning && type->polymorphic()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        bool done = false;
        for (std::size_t k = 0; k < taken.size() && !done; k++)
            if (ms[i].offset >= taken[k].first && ms[i].offset < taken[k].second)
                done = true;
        if (done) continue;

        const Type *mt = ms[i].type;
        long long elemCount = 1;
        const Type *elem = mt->isArray() ? memberElements(mt, &elemCount) : mt;
        const Signature *cc = nullptr;
        if (assigning && moving) cc = moveAssignOf(memberClass(mt));
        if (assigning && cc == nullptr) cc = copyAssignOf(memberClass(mt));
        else if (!assigning) {
            // A const member is `const T &&` to move from, which only the copy constructor takes ([class.copy]/15).
            if (moving && !elem->isConst()) cc = moveConstructorOf(memberClass(mt));
            if (cc == nullptr) cc = copyConstructorOf(memberClass(mt));
        }
        if (cc != nullptr) {
            if (cc->access != Access::Public)
                p_.src_.fail(pos, std::string("'") + cls + "' cannot be built by "
                               "the " + kind + " the compiler would write: the " +
                               kind + " of '" + memberClass(mt)->tag() +
                               "', the type of '" + ms[i].name + "', is " +
                               (cc->access == Access::Private ? "private"
                                                              : "protected"));
            p_.markUsed(cc);
        }

        int indexSlot = 0;
        long long count = 0;
        if (mt->isArray()) {
            count = elemCount;
            if (count < 0)
                p_.src_.fail(pos, "'" + cls + "::" + ms[i].name + "' has no length, "
                               "so the copy constructor the compiler would write "
                               "does not know how much to copy");
            indexSlot = p_.allocateFrameSlot(p_.types_.intType());
        }

        // Both sides of the copy, as lvalues.
        ExprPtr dst = p_.thisMember(thisSlot, type, ms[i]);

        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(srcRef->referent());
        if (moving) fromObj->setXvalue();
        ExprPtr src(new MemberAccess(std::move(fromObj), ms[i].name, ms[i].offset,
                                     ms[i].width, ms[i].bitOffset));
        src->setType(mt);

        if (mt->isArray()) {
            ExprPtr dstAt = indexBytes(p_.types_, p_.decay(std::move(dst)), elem,
                                       indexSlot, p_.target_);
            ExprPtr srcAt = indexBytes(p_.types_, p_.decay(std::move(src)), elem,
                                       indexSlot, p_.target_);
            dst = ExprPtr(new Unary('*', std::move(dstAt)));
            dst->setType(elem);
            src = ExprPtr(new Unary('*', std::move(srcAt)));
            src->setType(elem);
        }

        // After the array unwrap, so that it lands on the element actually handed over.
        if (moving) src->setXvalue();

        StmtPtr one;
        if (cc != nullptr) {
            ExprPtr addr(new Unary('&', std::move(dst)));
            addr->setType(p_.types_.pointerTo(elem->unqualified()));
            std::vector<ExprPtr> args;
            args.push_back(std::move(addr));
            args.push_back(std::move(src));
            std::vector<const Type *> ps;
            ps.push_back(p_.types_.pointerTo(elem->unqualified()));
            ps.push_back(cc->params[0]);
            one = StmtPtr(new ExprStmt(
                p_.completeCall(elem->unqualified()->tag(), cc->symbol, nullptr,
                             cc->returns, ps, false, pos, std::move(args))));
        } else {
            ExprPtr store(new Assign(std::move(dst), std::move(src)));
            store->setType(elem);
            one = StmtPtr(new ExprStmt(std::move(store)));
        }

        if (mt->isArray()) {
            Parser::Alive built{ "this", thisSlot, elem->unqualified() };
            built.owner = type;
            built.member = static_cast<int>(i);
            one = p_.eachElement(indexSlot, count, std::move(one));
            if (!assigning) one = p_.guardElements(std::move(one), built, indexSlot, pos);
        }
        body.push_back(std::move(one));
        finished.resize(body.size());
        if (!assigning && p_.memberHasDestructor(mt)) {
            finished.back() = Parser::Alive{ "this", thisSlot, type };
            finished.back().owner = type;
            finished.back().member = static_cast<int>(i);
        }
    }

    // **`a = b` is an expression and has to have a value**, and the value is the object
    // assigned to. The declared return type is `X &`, and a reference is a pointer
    // everywhere below the parser, so what the function actually returns is `this`.
    const Type *returns = p_.types_.get(Kind::Void);
    if (assigning) {
        returns = p_.types_.pointerTo(type);
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        body.push_back(StmtPtr(new Return(std::move(me))));
    }

    bool guarded = false;
    StmtPtr built = p_.withVirtualBases(std::move(vbInit), vbs,
                                     p_.guardConstruction(std::move(body), std::move(finished), pos, &guarded),
                                     pos);
    p_.current_->functions.push_back(Function(cls + "::" + (assigning ? "operator="
                                                                  : cls),
                                           returns, std::move(params), std::move(built),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(symbol);
    p_.current_->functions.back().setHasLandingPads(guarded || own.made());
    p_.current_->functions.back().setInline(true);
    // A constructor is emitted under both of Itanium's names; an operator has
    // one name in either ABI.
    if (!assigning && !p_.target_.microsoftNames()) {
        std::vector<const Type *> ps;
        ps.push_back(srcRef);
        const Type *fnType = p_.types_.functionType(p_.types_.get(Kind::Void), ps, false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why)) {
            // The same split, with the source forwarded: C1 copies the one
            // virtual base out of `that` and then calls C2 for the rest.
            if (type->hasVirtualBase()) {
                p_.current_->functions.back().setSymbol(c2);
                p_.closeFrame(savedFrame);
                p_.vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, ps, symbol, c2, true, pos, 0,
                                       moving);
                return;
            }
            p_.current_->functions.back().setAlias(c2);
        }
    }
    p_.closeFrame(savedFrame);
    p_.vttSlot_ = savedVtt;
}

// [class.virtual]'s key function, as the Itanium ABI reads it: the first
// non-pure virtual member declared in the class and not defined inline. Where
// that one is not defined in this unit, the vtable and type_info are not either.
bool Classes::keyFunctionUndefined(const std::string &tag) const {
    const Signature *key = keyFunction(tag);
    return key != nullptr && !key->defined;
}

// Whether the class has a key function and this unit defines it: then the group is emitted here.
bool Classes::hasKeyFunction(const std::string &tag) const {
    const Signature *key = keyFunction(tag);
    return key != nullptr && key->defined;
}

const Parser::Signature *Classes::keyFunction(const std::string &tag) const {
    std::map<std::string, std::vector<VSlot> >::const_iterator vt = p_.vtables_.find(tag);
    for (std::size_t i = 0; i < p_.functions_.size(); i++) {
        const Signature &f = p_.functions_[i];
        if (f.owner != tag || !f.isVirtual || f.implicit || f.inlineBody) continue;
        bool pure = false;
        if (vt != p_.vtables_.end())
            for (std::size_t k = 0; k < vt->second.size(); k++)
                if (vt->second[k].pure && overrides(vt->second[k], f.name, f.params, f.constThis, f.refThis))
                    pure = true;
        if (pure) continue;
        return &f;
    }
    return nullptr;
}

// A class whose key function lives in another unit gets its vtable, its
// type_info and its deleting destructor from there: clang emits none of the
// three, and so `std::exception`'s are the runtime's rather than a copy.
void Classes::pruneExternalVtables(Program &program) {
    if (p_.target_.microsoftNames()) return;   // cl emits every vftable, as a COMDAT
    for (std::map<std::string, std::vector<std::string> >::const_iterator it =
             p_.classSymbols_.begin(); it != p_.classSymbols_.end(); ++it) {
        const Type *cls = p_.lookup_.findTypedef(it->first);
        if (cls != nullptr && cls->unqualified()->isSpecialization()) continue;
        if (!keyFunctionUndefined(it->first)) continue;
        std::vector<std::string> gone = it->second;
        gone.push_back(p_.linkage_.deletingDestructorSymbol(it->first));
        for (std::size_t g = 0; g < gone.size(); g++) {
            for (std::size_t i = program.globals.size(); i-- > 0; )
                if (program.globals[i].symbol == gone[g])
                    program.globals.erase(program.globals.begin() + static_cast<long>(i));
            for (std::size_t i = program.functions.size(); i-- > 0; )
                if (program.functions[i].symbol() == gone[g])
                    program.functions.erase(program.functions.begin() + static_cast<long>(i));
        }
    }
}

// **To a fixed point, because a body can be what first calls another.** Giving
// Owner its constructor is what calls Held's, and Held's may not have been
// wanted by anything the program wrote.
void Classes::defineImplicitFunctions() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < p_.functions_.size(); i++) {
            if (!p_.functions_[i].implicit || !p_.functions_[i].used ||
                p_.functions_[i].defined)
                continue;
            p_.functions_[i].defined = true;
            if (!p_.functions_[i].name.empty() && p_.functions_[i].name[0] == '~')
                                                    synthesizeDestructor(i);
            else if (p_.functions_[i].name == "operator=") synthesizeCopy(i, true);
            else if (p_.functions_[i].params.empty() || p_.inheriting_.count(i)) synthesizeDefaultCtor(i);
            else                                    synthesizeCopy(i, false);
            again = true;
        }
    }
}

// Where a virtual base sits, which only the class that laid it down knows.
int Classes::virtualBaseAt(const Type *layout, const Type *vbase) {
    const std::vector<Type::BaseSpec> &bs = layout->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (bs[i].isVirtual && bs[i].type == vbase) return bs[i].offset;
    return -1;
}

std::string Classes::itaniumClassEncoding(const Type *cls) const {
    return vtableSymbol(cls, false).substr(4);
}

// **The primary base's first, then this class's in inheritance-graph
// preorder** - clang's VCallAndVBaseOffsetBuilder, whose list is laid down
// reversed, so the last of these sits lowest in the table.
void Classes::vbaseComponents(const Type *cls, const Type *layout, int clsAt,
                             std::set<const Type *> &seen,
                             std::vector<std::pair<const Type *, int> > &out)
    const {
    if (const Type *p = cls->primaryBase())
        vbaseComponents(p, layout, clsAt, seen, out);
    const std::vector<Type::BaseSpec> &bs = cls->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (bs[i].isVirtual && seen.insert(bs[i].type).second)
            out.push_back(std::make_pair(bs[i].type,
                                         virtualBaseAt(layout, bs[i].type)));
}

// Where a virtual base's `vbase_offset` sits in this class's vtable, which is
// what the type_info names rather than the base's place in the object: the
// first component is nearest the address point, three entries below it.
long long Classes::itaniumVbaseOffsetSlot(const Type *cls, const Type *vbase) const {
    std::vector<std::pair<const Type *, int> > vb;
    std::set<const Type *> seen;
    vbaseComponents(cls->unqualified(), cls->unqualified(), 0, seen, vb);
    for (std::size_t i = 0; i < vb.size(); i++)
        if (vb[i].first == vbase->unqualified())
            return -static_cast<long long>(3 + i) * p_.pointerBytes();
    return 0;
}

void Classes::subobjectsOf(const Type *x, int xAt, const Type *layout,
                          std::set<const Type *> &seen,
                          std::vector<Subobject> &out) const {
    out.push_back(Subobject{ x, xAt });
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        int at = xAt + bs[i]->offset;
        if (bs[i]->isVirtual) {
            if (!seen.insert(bs[i]->type).second) continue;
            at = virtualBaseAt(layout, bs[i]->type);
        }
        subobjectsOf(bs[i]->type, at, layout, seen, out);
    }
}

bool Classes::containsSubobject(const Type *y, int yAt, const Type *x, int xAt,
                               const Type *layout) const {
    std::vector<Subobject> all;
    std::set<const Type *> seen;
    subobjectsOf(y, yAt, layout, seen, all);
    for (std::size_t i = 0; i < all.size(); i++)
        if (all[i].cls == x && all[i].at == xAt) return true;
    return false;
}

// The slot in a class's own table holding a function the class itself
// declared - not one it inherited - matching a base's slot; null otherwise.
const VSlot *Classes::declaredSlot(const Type *cls, const VSlot &s) const {
    std::map<std::string, std::vector<VSlot> >::const_iterator it =
        p_.vtables_.find(cls->tag());
    if (it == p_.vtables_.end()) return nullptr;
    const bool dtor = s.name == "~" || s.name == "~$deleting";
    if (dtor) {
        if (p_.destructorOf(cls) == nullptr) return nullptr;
    } else {
        const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(cls->tag() + "::" + s.name);
        if (set == nullptr) return nullptr;
        bool own = false;
        for (std::size_t k = 0; k < set->size() && !own; k++) {
            const Signature &f = p_.functions_[(*set)[k]];
            if (f.owner == cls->tag() && f.constThis == s.constThis &&
                f.refThis == s.refThis &&
                f.isVirtual && sameParameters(f.params, s.params))
                own = true;
        }
        if (!own) return nullptr;
    }
    for (std::size_t i = 0; i < it->second.size(); i++) {
        const VSlot &c = it->second[i];
        if (dtor ? c.name == s.name
                 : overrides(c, s.name, s.params, s.constThis, s.refThis))
            return &c;
    }
    return nullptr;
}

// **[class.virtual]/2**: of the subobjects of `root` that contain the target
// and declare an override, the one no other contains. None means the
// target's own entry stands; two means the program is ill-formed.
Overrider Classes::finalOverrider(const Type *root, int rootAt,
                                         const VSlot &s, const Type *target,
                                         int targetAt, const Type *layout,
                                         std::size_t pos) {
    std::vector<Subobject> all;
    std::set<const Type *> seen;
    subobjectsOf(root, rootAt, layout, seen, all);

    struct Candidate { const Type *cls; int at; const VSlot *slot; };
    std::vector<Candidate> found;
    for (std::size_t i = 0; i < all.size(); i++) {
        if (all[i].cls == target && all[i].at == targetAt) continue;
        bool dup = false;
        for (std::size_t k = 0; k < found.size(); k++)
            if (found[k].cls == all[i].cls && found[k].at == all[i].at) dup = true;
        if (dup) continue;
        if (!containsSubobject(all[i].cls, all[i].at, target, targetAt, layout))
            continue;
        if (const VSlot *d = declaredSlot(all[i].cls, s))
            found.push_back(Candidate{ all[i].cls, all[i].at, d });
    }
    std::vector<Candidate> keep;
    for (std::size_t i = 0; i < found.size(); i++) {
        bool inside = false;
        for (std::size_t k = 0; k < found.size() && !inside; k++)
            if (k != i && containsSubobject(found[k].cls, found[k].at,
                                            found[i].cls, found[i].at, layout))
                inside = true;
        if (!inside) keep.push_back(found[i]);
    }
    if (keep.empty())
        return Overrider{ target, targetAt, s.symbol, s.pure };
    if (keep.size() > 1)
        p_.src_.fail(pos, "'" + root->tag() + "' has more than one final overrider "
                       "for '" + target->tag() + "::" +
                       (s.name == "~$deleting" ? std::string("~") : s.name) +
                       "': '" + keep[0].cls->tag() + "' and '" +
                       keep[1].cls->tag() + "' each override it and neither "
                       "is derived from the other - [class.virtual]/2");
    return Overrider{ keep[0].cls, keep[0].at, keep[0].slot->symbol,
                      keep[0].slot->pure };
}

// **One `vcall_offset` per function of the virtual base**, in the order
// clang's AddVCallOffsets walks them: the primary chain's, then the class's
// own new ones, then those of its non-primary bases; a destructor counts once.
void Classes::vcallEntries(const Type *y, int yAt, const Type *vbase,
                          int vbaseAt, const Type *root, int rootAt,
                          const Type *layout, std::vector<VcallEntry> &out,
                          std::size_t pos) {
    std::size_t from = 0;
    if (const Type *p = y->primaryBase()) {
        vcallEntries(p, yAt, vbase, vbaseAt, root, rootAt, layout, out, pos);
        from = p_.vtables_[p->tag()].size();
    }
    const std::vector<VSlot> &slots = p_.vtables_[y->tag()];
    for (std::size_t i = from; i < slots.size(); i++) {
        if (slots[i].name == "~$deleting") continue;
        const Overrider ov = finalOverrider(root, rootAt, slots[i], y, yAt,
                                            layout, pos);
        out.push_back(VcallEntry{ y, slots[i].name, slots[i].params,
                                  slots[i].constThis, slots[i].refThis,
                                  static_cast<long long>(ov.at - vbaseAt) });
    }
    const std::vector<const Type::BaseSpec *> bs = y->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (bs[i]->isVirtual || bs[i]->type == y->primaryBase()) continue;
        if (!bs[i]->type->hasVptr()) continue;
        vcallEntries(bs[i]->type, yAt + bs[i]->offset, vbase, vbaseAt, root,
                     rootAt, layout, out, pos);
    }
}

// **A virtual thunk**: `this` moves by a fixed amount to the virtual base
// whose table holds the vcall offset, then by what that offset says, and the
// real function is called - _ZTv0_n24_N1D1fEv, `n16` first where the step is -16.
std::string Classes::synthesizeVirtualThunk(const Type *type, const VSlot &slot,
                                           long long fixed, long long vcallBack,
                                           std::size_t pos) {
    std::string name = "_ZTv";
    name += fixed < 0 ? "n" + std::to_string(-fixed) : std::to_string(fixed);
    name += "_n" + std::to_string(vcallBack) + "_" + slot.symbol.substr(2);
    for (std::size_t i = 0; i < p_.current_->functions.size(); i++)
        if (p_.current_->functions[i].symbol() == name) return name;

    const Type *self = p_.types_.pointerTo(type);
    const Type *chars = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *offType = p_.ptrdiffType();
    const Parser::FrameSave savedFrame = p_.openFrame();

    std::vector<Param> params;
    const int thisSlot = p_.allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    std::vector<int> argSlots;
    for (std::size_t i = 0; i < slot.params.size(); i++)
        argSlots.push_back(p_.allocateFrameSlot(slot.params[i]));
    for (std::size_t i = 0; i < slot.params.size(); i++)
        params.push_back(Param{ slot.params[i], argSlots[i] });

    // this = (char *)this + fixed, held in its own slot for the two reads
    const int held = p_.allocateFrameSlot(chars);
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    ExprPtr asChars(new Cast(chars, std::move(me)));
    asChars->setType(chars);
    ExprPtr step(new Num(fixed));
    step->setType(offType);
    ExprPtr stepped(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    stepped->setType(chars);
    ExprPtr keep(Var::local(".vt", held));
    keep->setType(chars);
    ExprPtr save(new Assign(std::move(keep), std::move(stepped)));
    save->setType(chars);
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new ExprStmt(std::move(save))));

    // (T *)(held + *(ptrdiff_t *)(*(char **)held - vcallBack))
    ExprPtr obj(Var::local(".vt", held));
    obj->setType(chars);
    ExprPtr asTable(new Cast(p_.types_.pointerTo(chars), std::move(obj)));
    asTable->setType(p_.types_.pointerTo(chars));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(chars);
    ExprPtr back(new Num(-vcallBack));
    back->setType(offType);
    ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(back)));
    at->setType(chars);
    ExprPtr asOff(new Cast(p_.types_.pointerTo(offType), std::move(at)));
    asOff->setType(p_.types_.pointerTo(offType));
    ExprPtr delta(new Unary('*', std::move(asOff)));
    delta->setType(offType);
    ExprPtr from(Var::local(".vt", held));
    from->setType(chars);
    ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(delta)));
    moved->setType(chars);
    ExprPtr whole(new Cast(self, std::move(moved)));
    whole->setType(self);

    std::vector<ExprPtr> args;
    args.push_back(std::move(whole));
    std::vector<const Type *> full;
    full.push_back(self);
    for (std::size_t i = 0; i < slot.params.size(); i++) {
        ExprPtr a(Var::local("a" + std::to_string(i), argSlots[i]));
        a->setType(slot.params[i]);
        args.push_back(std::move(a));
        full.push_back(slot.params[i]);
    }

    const Type *returns = p_.types_.get(Kind::Void);
    if (slot.name != "~" && slot.name != "~$deleting")
        if (const std::vector<std::size_t> *set =
                p_.lookup_.overloadsOf(type->tag() + "::" + slot.name))
            for (std::size_t k = 0; k < set->size(); k++)
                if (p_.functions_[(*set)[k]].symbol == slot.symbol)
                    returns = p_.functions_[(*set)[k]].returns;
    ExprPtr call = p_.completeCall(slot.name, slot.symbol, nullptr, returns, full,
                                false, pos, std::move(args), true);
    if (returns->isVoid()) {
        body.push_back(StmtPtr(new ExprStmt(std::move(call))));
        body.push_back(StmtPtr(new Return(nullptr)));
    } else {
        body.push_back(StmtPtr(new Return(std::move(call))));
    }

    p_.current_->functions.push_back(Function(name, returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(p_.frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    p_.current_->functions.back().setSymbol(name);
    p_.current_->functions.back().setInline(true);
    p_.closeFrame(savedFrame);
    return name;
}

// **One table**, for the subobject `x` at `xAt` in the group of `root`: vcall
// offsets when x is itself a virtual base, its vbase offsets, offset-to-top,
// the type_info, then x's slots as `root` overrides them (`vbase` holds x).
void Classes::layoutOneVtable(VtableGroup &g, const Type *x, int xAt,
                             bool xIsVirtual, const Type *vbase, int vbaseAt,
                             const Type *root, int rootAt, const Type *layout,
                             const std::string &typeInfo, std::size_t pos) {
    const int w = p_.pointerBytes();
    int at = g.pieces.empty() ? 0 : g.pieces.back().offset + g.pieces.back().size;
    const std::vector<VSlot> slots = p_.vtables_[x->tag()];

    std::vector<Overrider> ov;
    for (std::size_t i = 0; i < slots.size(); i++)
        ov.push_back(finalOverrider(root, rootAt, slots[i], x, xAt, layout, pos));

    // The vcall offsets live in the virtual base's own table, x's when it
    // is one; a base inside it reaches them through that base's vptr.
    std::vector<VcallEntry> vcalls;
    if (vbase != nullptr)
        vcallEntries(vbase, vbaseAt, vbase, vbaseAt, root, rootAt, layout,
                     vcalls, pos);
    if (xIsVirtual)
        for (std::size_t k = vcalls.size(); k-- > 0; ) {
            g.pieces.push_back(GlobalPiece{ at, w, vcalls[k].value,
                                            std::string() });
            at += w;
        }

    std::vector<std::pair<const Type *, int> > vb;
    std::set<const Type *> seen;
    vbaseComponents(x, layout, xAt, seen, vb);
    for (std::size_t k = vb.size(); k-- > 0; ) {
        g.pieces.push_back(GlobalPiece{ at, w,
            static_cast<long long>(vb[k].second - xAt), std::string() });
        at += w;
    }
    g.pieces.push_back(GlobalPiece{ at, w,
        static_cast<long long>(rootAt - xAt), std::string() });
    at += w;
    g.pieces.push_back(GlobalPiece{ at, w, 0, typeInfo });
    at += w;
    g.points[std::make_pair(x, xAt)] = at;

    // How many entries below the address point the vcall offsets of the
    // virtual base's table begin, past its header and vbase offsets.
    std::vector<std::pair<const Type *, int> > vbOfBase;
    if (vbase != nullptr) {
        std::set<const Type *> seen2;
        vbaseComponents(vbase, layout, vbaseAt, seen2, vbOfBase);
    }
    for (std::size_t i = 0; i < slots.size(); i++) {
        std::string entry = ov[i].symbol;
        p_.markSymbolUsed(entry);
        // The overrider's own slot, whose name and parameters the thunk copies.
        VSlot target = slots[i];
        target.symbol = ov[i].symbol;
        if (ov[i].at != xAt && !ov[i].pure) {
            const bool viaVbase = vbase != nullptr &&
                !containsSubobject(vbase, vbaseAt, ov[i].cls, ov[i].at, layout);
            if (viaVbase) {
                const std::string want = slots[i].name == "~$deleting"
                                       ? std::string("~") : slots[i].name;
                std::size_t idx = vcalls.size();
                for (std::size_t k = 0; k < vcalls.size() && idx == vcalls.size(); k++)
                    if (vcalls[k].cls == x && vcalls[k].name == want &&
                        vcalls[k].constThis == slots[i].constThis &&
                        vcalls[k].refThis == slots[i].refThis &&
                        sameParameters(vcalls[k].params, slots[i].params))
                        idx = k;
                if (idx == vcalls.size())
                    p_.src_.fail(pos, "no vcall offset for '" + x->tag() + "::" +
                                   want + "' in '" + vbase->tag() + "'");
                const long long back =
                    static_cast<long long>(3 + vbOfBase.size() + idx) * w;
                entry = synthesizeVirtualThunk(ov[i].cls, target,
                                               static_cast<long long>(vbaseAt - xAt),
                                               back, pos);
            } else {
                entry = synthesizeThunk(ov[i].cls->tag(), ov[i].cls, target,
                                        xAt - ov[i].at, pos);
            }
        }
        g.pieces.push_back(GlobalPiece{ at, w, 0, entry });
        at += w;
    }
}

// **A secondary table for every non-primary base at any depth**: a primary
// base shares its parent's, and only its own bases are looked at. A
// construction group leaves out a base with no virtual base in or above it.
void Classes::layoutSecondaryVtables(VtableGroup &g, const Type *x, int xAt,
                                    const Type *vbase, int vbaseAt,
                                    const Type *root, int rootAt,
                                    const Type *layout, bool construction,
                                    const std::string &typeInfo,
                                    std::size_t pos) {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        if (bs[i]->isVirtual || !b->hasVptr()) continue;
        if (construction && vbase == nullptr && !b->hasVirtualBase()) continue;
        const int bAt = xAt + bs[i]->offset;
        if (b != x->primaryBase())
            layoutOneVtable(g, b, bAt, false, vbase, vbaseAt, root, rootAt,
                            layout, typeInfo, pos);
        layoutSecondaryVtables(g, b, bAt, vbase, vbaseAt, root, rootAt, layout,
                               construction, typeInfo, pos);
    }
}

// **And one for every virtual base with a vptr**, met in inheritance-graph
// preorder and laid down once, with the secondary tables of its own
// non-primary bases behind it.
void Classes::layoutVirtualBaseVtables(VtableGroup &g, const Type *x,
                                      const Type *root, int rootAt,
                                      const Type *layout, bool construction,
                                      const std::string &typeInfo,
                                      std::set<const Type *> &seen,
                                      std::size_t pos) {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        if (bs[i]->isVirtual && b->hasVptr() && seen.insert(b).second) {
            const int bAt = virtualBaseAt(layout, b);
            layoutOneVtable(g, b, bAt, true, b, bAt, root, rootAt, layout,
                            typeInfo, pos);
            layoutSecondaryVtables(g, b, bAt, b, bAt, root, rootAt, layout,
                                   construction, typeInfo, pos);
        }
        if (b->hasVirtualBase())
            layoutVirtualBaseVtables(g, b, root, rootAt, layout, construction,
                                     typeInfo, seen, pos);
    }
}


VtableGroup Classes::buildVtableGroup(const Type *cls, int clsAt,
                                             bool clsIsVirtual,
                                             const Type *layout,
                                             const std::string &typeInfo,
                                             std::size_t pos) {
    VtableGroup g;
    const bool construction = cls != layout;
    const Type *vbase = clsIsVirtual ? cls : nullptr;
    layoutOneVtable(g, cls, clsAt, clsIsVirtual, vbase, clsAt, cls, clsAt,
                    layout, typeInfo, pos);
    layoutSecondaryVtables(g, cls, clsAt, vbase, clsAt, cls, clsAt, layout,
                           construction, typeInfo, pos);
    std::set<const Type *> seen;
    layoutVirtualBaseVtables(g, cls, cls, clsAt, layout, construction,
                             typeInfo, seen, pos);
    return g;
}

void Classes::emitVtableGroup(const std::string &symbol, VtableGroup g) {
    if (p_.groups_.count(symbol) != 0) return;
    const Type *entry = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *table = p_.types_.arrayOf(entry,
                                       static_cast<long long>(g.pieces.size()));
    p_.current_->globals.push_back(Global{ symbol, symbol, table, g.pieces,
                                        true, false, true, std::string(),
                                        true });
    p_.groups_[symbol] = std::move(g);
}

// **A construction vtable**, `_ZTC<layout><offset>_<base>`: the base's group
// as if it were the complete object, with the virtual bases where the
// layout class put them and the base's own type_info.
std::string Classes::constructionVtable(const Type *base, int at,
                                       bool baseIsVirtual, const Type *layout,
                                       const std::string &typeInfo,
                                       std::size_t pos) {
    (void)typeInfo;
    const std::string symbol = "_ZTC" + itaniumClassEncoding(layout) +
                               std::to_string(at) + "_" +
                               itaniumClassEncoding(base);
    if (p_.groups_.count(symbol) != 0) return symbol;
    const std::string ti = emitClassTypeInfo(base, base->tag(), pos);
    emitVtableGroup(symbol, buildVtableGroup(base, at, baseIsVirtual, layout,
                                             ti, pos));
    return symbol;
}

// **One VTT part**: the primary virtual pointer, a sub-VTT for each direct
// non-virtual base with virtual bases, then the secondary virtual pointers;
// the complete object's part ends with the virtual VTTs - [2.6.2].
void Classes::vttPart(const Type *cls, int clsAt, const std::string &group,
                     const Type *layout, bool top, const std::string &typeInfo,
                     VttLayout &out, std::size_t pos) {
    const int w = p_.pointerBytes();
    const std::size_t from = out.pieces.size();
    const VtableGroup &g = p_.groups_[group];
    std::map<std::pair<const Type *, int>, int>::const_iterator own =
        g.points.find(std::make_pair(cls, clsAt));
    if (own == g.points.end())
        p_.src_.fail(pos, "'" + group + "' has no table for '" + cls->tag() + "'");
    out.pieces.push_back(GlobalPiece{ static_cast<int>(from) * w, w,
                                      own->second, group });

    const std::vector<const Type::BaseSpec *> bs = cls->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (bs[i]->isVirtual || !bs[i]->type->hasVirtualBase()) continue;
        if (top) out.subVtt[bs[i]->type] = static_cast<int>(out.pieces.size());
        const int bAt = clsAt + bs[i]->offset;
        const std::string sub = constructionVtable(bs[i]->type, bAt, false,
                                                   layout, typeInfo, pos);
        vttPart(bs[i]->type, bAt, sub, layout, false, typeInfo, out, pos);
    }
    std::set<const Type *> seen;
    vttSecondaryPointers(cls, clsAt, false, group, layout, top, seen, out,
                         static_cast<int>(from), pos);
    if (top) {
        std::set<const Type *> seenV;
        vttVirtualParts(cls, layout, typeInfo, seenV, out, pos);
    }
}

// **A secondary virtual pointer for each base that has virtual bases or is
// reached through one, and is not a non-virtual primary base** - the vptrs
// a base-subobject constructor cannot set from the class's own group.
void Classes::vttSecondaryPointers(const Type *x, int xAt, bool morally,
                                  const std::string &group, const Type *layout,
                                  bool top, std::set<const Type *> &seen,
                                  VttLayout &out, int from, std::size_t pos) {
    const int w = p_.pointerBytes();
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        int bAt = xAt + bs[i]->offset;
        bool morallyB = morally, nvPrimary = b == x->primaryBase();
        if (bs[i]->isVirtual) {
            if (!seen.insert(b).second) continue;
            bAt = virtualBaseAt(layout, b);
            morallyB = true;
            nvPrimary = false;
        }
        if (!nvPrimary && b->hasVptr() && (b->hasVirtualBase() || morallyB)) {
            const VtableGroup &g = p_.groups_[group];
            std::map<std::pair<const Type *, int>, int>::const_iterator p =
                g.points.find(std::make_pair(b, bAt));
            if (p == g.points.end())
                p_.src_.fail(pos, "'" + group + "' has no table for '" + b->tag() +
                               "' at " + std::to_string(bAt));
            if (top)
                out.secondary[std::make_pair(b, bAt)] =
                    static_cast<int>(out.pieces.size()) - from;
            out.pieces.push_back(GlobalPiece{
                static_cast<int>(out.pieces.size()) * w, w, p->second, group });
        }
        vttSecondaryPointers(b, bAt, morallyB, group, layout, top, seen, out,
                             from, pos);
    }
}

// **A virtual VTT for every virtual base that has virtual bases of its
// own**: its part, built on a construction vtable, for its C2 to read.
void Classes::vttVirtualParts(const Type *x, const Type *layout,
                             const std::string &typeInfo,
                             std::set<const Type *> &seen, VttLayout &out,
                             std::size_t pos) {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        if (bs[i]->isVirtual && seen.insert(b).second && b->hasVirtualBase()) {
            const int bAt = virtualBaseAt(layout, b);
            out.virtualVtt[b] = static_cast<int>(out.pieces.size());
            const std::string sub = constructionVtable(b, bAt, true, layout,
                                                       typeInfo, pos);
            vttPart(b, bAt, sub, layout, false, typeInfo, out, pos);
        }
        if (b->hasVirtualBase())
            vttVirtualParts(b, layout, typeInfo, seen, out, pos);
    }
}

// The class's `_ZTV`, and with virtual bases its `_ZTT` and every
// construction vtable the VTT names.
void Classes::emitItaniumVtables(const Type *cls, const std::string &tag,
                                const std::string &symbol, std::size_t pos) {
    const std::string typeInfo = emitClassTypeInfo(cls, tag, pos);
    emitVtableGroup(symbol, buildVtableGroup(cls, 0, false, cls, typeInfo, pos));
    if (!cls->hasVirtualBase()) return;

    VttLayout vtt;
    vttPart(cls, 0, symbol, cls, true, typeInfo, vtt, pos);
    const std::string name = "_ZTT" + itaniumClassEncoding(cls);
    const Type *entry = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *table = p_.types_.arrayOf(entry,
                                       static_cast<long long>(vtt.pieces.size()));
    p_.current_->globals.push_back(Global{ name, name, table, vtt.pieces, true,
                                        false, true, std::string(), true });
    p_.vtts_[tag] = std::move(vtt);
}

ExprPtr Classes::vttEntry(ExprPtr vtt, int entry) {
    const Type *chars = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *word = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    ExprPtr asChars(new Cast(chars, std::move(vtt)));
    asChars->setType(chars);
    ExprPtr step(new Num(static_cast<long long>(entry) * p_.pointerBytes()));
    step->setType(p_.types_.intType());
    ExprPtr at(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    at->setType(chars);
    ExprPtr slot(new Cast(p_.types_.pointerTo(word), std::move(at)));
    slot->setType(p_.types_.pointerTo(word));
    ExprPtr value(new Unary('*', std::move(slot)));
    value->setType(word);
    return value;
}

// The sub-VTT for a direct non-virtual base, `vtt + n` in this function's own.
ExprPtr Classes::vttForBase(const Type *cls, const Type *base) {
    std::map<std::string, VttLayout>::const_iterator it = p_.vtts_.find(cls->tag());
    if (p_.vttSlot_ < 0 || it == p_.vtts_.end() ||
        it->second.subVtt.count(base) == 0)
        p_.src_.fail(0, "'" + cls->tag() + "' has no sub-VTT for its base '" +
                     base->tag() + "'");
    const Type *chars = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    ExprPtr vtt(Var::local(".vtt", p_.vttSlot_));
    vtt->setType(p_.vttType());
    ExprPtr asChars(new Cast(chars, std::move(vtt)));
    asChars->setType(chars);
    ExprPtr step(new Num(static_cast<long long>(
        it->second.subVtt.find(base)->second) * p_.pointerBytes()));
    step->setType(p_.types_.intType());
    ExprPtr at(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    at->setType(chars);
    ExprPtr whole(new Cast(p_.vttType(), std::move(at)));
    whole->setType(p_.vttType());
    return whole;
}

// `_ZTT<cls> + n`, for the complete object's own calls: its C2 with 0, a
// virtual base's C2 with that base's virtual VTT.
ExprPtr Classes::vttOfClass(const Type *cls, int entry) {
    std::map<std::string, VttLayout>::const_iterator it = p_.vtts_.find(cls->tag());
    if (it == p_.vtts_.end())
        p_.src_.fail(0, "'" + cls->tag() + "' has no VTT");
    const Type *word = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *chars = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    ExprPtr table(Var::global("_ZTT" + itaniumClassEncoding(cls)));
    table->setType(p_.types_.arrayOf(word,
        static_cast<long long>(it->second.pieces.size())));
    ExprPtr asChars(new Cast(chars, p_.decay(std::move(table))));
    asChars->setType(chars);
    ExprPtr step(new Num(static_cast<long long>(entry) * p_.pointerBytes()));
    step->setType(p_.types_.intType());
    ExprPtr at(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    at->setType(chars);
    ExprPtr whole(new Cast(p_.vttType(), std::move(at)));
    whole->setType(p_.vttType());
    return whole;
}

// `*(ptrdiff_t *)(*(char **)object + slot)`: the `vbase_offset` behind the
// object's vptr, which is why the vptr is stored before any use of this.
ExprPtr Classes::vbaseOffsetRead(ExprPtr objectChars, const Type *cls,
                                const Type *vbase) {
    const Type *chars = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *offType = p_.ptrdiffType();
    ExprPtr asTable(new Cast(p_.types_.pointerTo(chars), std::move(objectChars)));
    asTable->setType(p_.types_.pointerTo(chars));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(chars);
    ExprPtr backNum(new Num(itaniumVbaseOffsetSlot(cls, vbase)));
    backNum->setType(offType);
    ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(backNum)));
    at->setType(chars);
    ExprPtr asOff(new Cast(p_.types_.pointerTo(offType), std::move(at)));
    asOff->setType(p_.types_.pointerTo(offType));
    ExprPtr delta(new Unary('*', std::move(asOff)));
    delta->setType(offType);
    return delta;
}

// Every vptr in the object below the class's own, as clang's getVTablePointers
// walks them: a non-virtual primary base shares its parent's, a virtual base
// is met once, and each remembers the virtual base it sits inside.
void Classes::vptrSites(const Type *x, int xAt, bool morally, const Type *vbase,
                       int fromVBase, const Type *layout,
                       std::set<const Type *> &seen,
                       std::vector<VptrSite> &out) const {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        int bAt = xAt + bs[i]->offset, from = fromVBase + bs[i]->offset;
        bool morallyB = morally, nvPrimary = b == x->primaryBase();
        const Type *inside = vbase;
        if (bs[i]->isVirtual) {
            if (!seen.insert(b).second) continue;
            bAt = virtualBaseAt(layout, b);
            morallyB = true;
            nvPrimary = false;
            inside = b;
            from = 0;
        }
        if (!nvPrimary && b->hasVptr())
            out.push_back(VptrSite{ b, bAt, morallyB, inside, from });
        vptrSites(b, bAt, morallyB, inside, from, layout, seen, out);
    }
}

// **The stores themselves.** A C2 or D2 of a class with virtual bases takes its
// own vptr and every one below a virtual base from its VTT, those tables
// depending on the complete object; every other vptr is the class's own group's.
std::vector<StmtPtr> Classes::itaniumVptrStores(const std::string &cls,
                                               const Type *memberOf,
                                               int thisSlot) {
    std::vector<StmtPtr> out;
    const Type *plain = memberOf->unqualified();
    const std::string table = plain->tag() == cls ? vtableSymbol(plain, false)
                                                   : vtableSymbol(cls, false);
    std::map<std::string, VtableGroup>::const_iterator git = p_.groups_.find(table);
    if (git == p_.groups_.end()) return out;
    const VtableGroup &g = git->second;
    const bool useVtt = p_.takesVtt(plain) && p_.vttSlot_ >= 0;
    const VttLayout *vtt = useVtt ? &p_.vtts_[cls] : nullptr;

    const Type *word = p_.types_.pointerTo(p_.types_.get(Kind::Void));
    const Type *chars = p_.types_.pointerTo(p_.types_.get(Kind::Char));
    const Type *offType = p_.ptrdiffType();

    std::vector<VptrSite> sites;
    sites.push_back(VptrSite{ plain, 0, false, nullptr, 0 });
    std::set<const Type *> seen;
    vptrSites(plain, 0, false, nullptr, 0, plain, seen, sites);

    for (std::size_t i = 0; i < sites.size(); i++) {
        const VptrSite &s = sites[i];
        const std::pair<const Type *, int> key(s.cls, s.offset);

        // **The address point**, out of the VTT or out of the group.
        ExprPtr value;
        const bool fromVtt = useVtt && (i == 0 || s.cls->hasVirtualBase() ||
                                        s.morallyVirtual);
        if (fromVtt) {
            int entry = 0;
            if (i != 0) {
                std::map<std::pair<const Type *, int>, int>::const_iterator e =
                    vtt->secondary.find(key);
                if (e == vtt->secondary.end()) continue;
                entry = e->second;
            }
            ExprPtr v(Var::local(".vtt", p_.vttSlot_));
            v->setType(p_.vttType());
            value = vttEntry(std::move(v), entry);
        } else {
            std::map<std::pair<const Type *, int>, int>::const_iterator p =
                g.points.find(key);
            if (p == g.points.end()) continue;
            ExprPtr base(Var::global(table));
            base->setType(p_.types_.arrayOf(word,
                static_cast<long long>(g.pieces.size())));
            ExprPtr asChars(new Cast(chars, p_.decay(std::move(base))));
            asChars->setType(chars);
            ExprPtr skip(new Num(static_cast<long long>(p->second)));
            skip->setType(p_.types_.intType());
            ExprPtr past(new Binary(BinOp::Add, std::move(asChars),
                                    std::move(skip)));
            past->setType(chars);
            value = ExprPtr(new Cast(word, std::move(past)));
            value->setType(word);
        }

        // **Where the vptr is**: a constant from `this`, or through the
        // virtual base's offset read from the vptr just stored.
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(chars);
        ExprPtr where = std::move(me);
        if (s.morallyVirtual) {
            ExprPtr again(Var::local("this", thisSlot));
            again->setType(chars);
            ExprPtr delta = vbaseOffsetRead(std::move(again), plain,
                                            s.nearestVBase);
            ExprPtr moved(new Binary(BinOp::Add, std::move(where),
                                     std::move(delta)));
            moved->setType(chars);
            where = std::move(moved);
            if (s.fromVBase != 0) {
                ExprPtr more(new Num(static_cast<long long>(s.fromVBase)));
                more->setType(offType);
                ExprPtr moved2(new Binary(BinOp::Add, std::move(where),
                                          std::move(more)));
                moved2->setType(chars);
                where = std::move(moved2);
            }
        } else if (s.offset != 0) {
            ExprPtr step(new Num(static_cast<long long>(s.offset)));
            step->setType(offType);
            ExprPtr moved(new Binary(BinOp::Add, std::move(where),
                                     std::move(step)));
            moved->setType(chars);
            where = std::move(moved);
        }
        ExprPtr slot(new Cast(p_.types_.pointerTo(word), std::move(where)));
        slot->setType(p_.types_.pointerTo(word));
        ExprPtr there(new Unary('*', std::move(slot)));
        there->setType(word);
        ExprPtr store(new Assign(std::move(there), std::move(value)));
        store->setType(word);
        out.push_back(StmtPtr(new ExprStmt(std::move(store))));
    }
    return out;
}

}   // namespace sema
