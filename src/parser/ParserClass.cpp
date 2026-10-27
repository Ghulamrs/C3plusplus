// The parser: what a class needs written for it. Constructors and destructors,
// vtables and thunks, the implicit special members and the code that defines them
// when something calls one, and the static data members. Rungs 3 and 4.
#include <functional>
#include <cstdio>
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

StmtPtr Parser::constructLocal(const Declared &d, int offset,
                               std::vector<ExprPtr> args, bool copyInit,
                               bool valueInit) {
    return constructObject(d, std::string(), offset, std::move(args), copyInit,
                           valueInit);
}

// The same construction wherever the object lives: a frame slot, or a global
// named by `symbol` - a static local, a file-scope object, a static member.
StmtPtr Parser::constructObject(const Declared &d, const std::string &symbol,
                                int offset, std::vector<ExprPtr> args,
                                bool copyInit, bool valueInit) {
    checkNotAbstract(d.type, d.pos, "'" + d.name + "'");
    const std::string key = constructorKey(d.type->tag());
    const Signature &ctor = overload_.resolveOverload(key, args, d.pos);
    const std::size_t written = args.size();
    applyDefaults(ctor, args, d.pos);

    // **[dcl.init]/16: copy-initialization may not pick an `explicit` constructor.**
    // `S s(3);` and `S s = 3;` call the same function, and this is the whole of what
    // `explicit` does. Checked after resolution, so the reader is told which one.
    if (copyInit && ctor.isExplicit)
        src_.fail(d.pos, "'" + d.type->describe() + "' has a constructor "
                         "taking these arguments and it is 'explicit', so it "
                         "will not be chosen for '" + d.name + " = ...' - "
                         "write '" + d.type->describe() + " " + d.name +
                         "(...)', which asks for it by name");

    if (ctor.access != Access::Public &&
        !insideAccessOf(d.type, ctor.access) && !isFriendOf(d.type))
        src_.fail(d.pos, "'" + d.type->describe() + "' has no public constructor "
                         "taking these arguments - the one that matches is " +
                         (ctor.access == Access::Private ? "private" : "protected"));

    const Type *thisType = types_.pointerTo(d.type->unqualified());
    ExprPtr addr(new Unary('&', objectAt(d, symbol, offset)));
    addr->setType(thisType);

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));

    std::vector<const Type *> full;
    full.push_back(thisType);
    for (std::size_t i = 0; i < ctor.params.size(); i++) full.push_back(ctor.params[i]);

    ExprPtr call = completeCall(d.type->tag(), ctor.symbol, nullptr,
                                types_.get(Kind::Void), full, false, d.pos,
                                std::move(all));
    // The constructor this declaration chose, by the local's serial, for its `var` node to carry (M2.16).
    if (symbol.empty())
        if (const Local *built = lookup_.findLocal(d.name)) {
            const LocalBuilt b = { ctor.symbol, types_.functionType(types_.get(Kind::Void), full, false), args.size() != written,
                                   valueInit && ctor.implicit };
            ctorBySerial_[built->serial] = b;
        }

    // **The zeroing half of [dcl.init]/8, spelt as the temporary path spells
    // it.** A constructor somebody wrote is the whole of the initialisation; an
    // implicit one leaves the members it does not name, so `{}` zeroes first.
    if (valueInit && ctor.implicit) {
        ExprPtr fresh = objectAt(d, symbol, offset);
        if (ExprPtr chain = zeroChain(*fresh, d.type->unqualified())) {
            ExprPtr seq(new Comma(std::move(chain), std::move(call)));
            seq->setType(types_.get(Kind::Void));
            call = std::move(seq);
        }
    }
    return StmtPtr(new ExprStmt(std::move(call)));
}

// [over.load]/2: with one parameter-type-list, either every overload has a ref-qualifier or none has.
static bool mixesRefQualifier(RefQual a, RefQual b) {
    return (a == RefQual::None) != (b == RefQual::None);
}

// [class.ctor]/1 and [class.dtor]/1 give neither a cv-qualifier nor a ref-qualifier, as clang says.
void Parser::refuseSpecialRefQualifier(const char *what) const {
    if (!peek().is("&") && !peek().is("&&")) return;
    src_.fail(peek().pos, "ref-qualifier '" + peek().text + "' is not allowed on a " + what);
}

void Parser::declareDestructor(const std::string &cls, std::size_t pos,
                               Access access, bool isVirtual) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (!params.empty() || variadic)
        src_.fail(pos, "a destructor takes no parameters");
    refuseSpecialRefQualifier("destructor");
    // A destructor is `noexcept` in C++11 whether or not it says so.
    pendingNoexcept_ = exceptionSpecification();
    const VirtSpecifiers virt = takeVirtSpecifiers();
    const SpecialDef special = takeSpecialDef();

    if (lookup_.overloadsOf(destructorKey(cls)) != nullptr)
        src_.fail(pos, "'" + cls + "' has two destructors, and a class has one");
    // Every object's death is a use, and the destroy sites are many: a deleted destructor is refused by name.
    if (special == SpecialDef::Deleted)
        src_.fail(pendingSpecialAt_, "a deleted destructor - '~" + localOf(cls) + "() = delete' - is "
                                     "not supported yet: every place an object of the class is "
                                     "destroyed would have to refuse it, and only calls are checked");
    // **A defaulted destructor is the implicit one.** A virtual one is registered now, so that it has its
    // slot whether or not a member gives it work; the rest is left to declareImplicitDestructor.
    if (special == SpecialDef::Defaulted) {
        defaultedMember(defaulted_[cls].dtor, access, pos, "the destructor");
        if (isVirtual || virt.isOverride || virt.isFinal)
            registerDestructor(cls, pos, access, isVirtual, true, virt);
        return;
    }
    registerDestructor(cls, pos, access, isVirtual, false, virt);
}

// Everything a destructor needs in the tables, whether a program wrote it or
// the compiler did: the name, the entry, and the vtable slots a virtual one
// claims.
void Parser::registerDestructor(const std::string &cls, std::size_t pos,
                                Access access, bool isVirtual, bool implicit,
                                VirtSpecifiers virt) {
    const std::vector<const Type *> params;
    const std::string key = destructorKey(cls);
    const std::string name = "~" + localOf(cls);

    // **A base with a virtual destructor makes this one virtual**, keyword or not -
    // [class.dtor]. The base's slots are already in this class's table, so it is
    // answered by looking for the "~" entry, and before the name is built.
    std::vector<VSlot> &slots = classes_.vtables_[cls];
    std::size_t slot = slots.size();
    bool foundBase = false;
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].name == "~") {
            checkNotFinal(slots[i], cls, name, pos);
            slot = i; isVirtual = foundBase = true; break;
        }
    // And a base off the primary chain - a second base, a virtual one - whose
    // destructor is virtual makes this one virtual too, in a slot of its own.
    if (!isVirtual)
        if (const Type *self = lookup_.findTypedef(cls)) {
            const std::vector<Type::BaseSpec> &bs = self->bases();
            for (std::size_t bi = 0; bi < bs.size() && !isVirtual; bi++) {
                std::map<std::string, std::vector<VSlot> >::const_iterator it =
                    classes_.vtables_.find(bs[bi].type->tag());
                if (it == classes_.vtables_.end()) continue;
                for (std::size_t i = 0; i < it->second.size(); i++)
                    if (it->second[i].name == "~") {
                        checkNotFinal(it->second[i], cls, name, pos);
                        isVirtual = foundBase = true; break;
                    }
            }
        }
    // [class.virtual]/4, for the destructor: `override` wants a base's virtual one and `final` a virtual one.
    if (virt.isOverride && !foundBase)
        src_.fail(virt.overrideAt, "'" + cls + "::" + name + "' is marked 'override' but overrides "
                                   "nothing - no base has a virtual destructor");
    if (virt.isFinal && !isVirtual)
        src_.fail(virt.finalAt, "'" + cls + "::" + name + "' is marked 'final' and is not virtual "
                                "- only a virtual destructor can be");

    // **A virtual destructor is U on Microsoft whatever its access**, the same
    // rule a virtual member function already followed - measured with cl,
    // which writes ??1VB@@UEAA@XZ where a non-virtual public one is QEAA.
    const char code = isVirtual                   ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out;
    if (target_.microsoftNames()) out = microsoftDestructorName(cls, lookup_.findTypedef(cls), code);
    else                          itaniumDestructorName(cls, lookup_.findTypedef(cls), true, &out);

    lookup_.functionIndex_[key].push_back(lookup_.functions_.size());
    lookup_.functions_.push_back(Signature{ "~" + localOf(cls), out, types_.get(Kind::Void),
                                    params, false, false, pos, false, cls, false,
                                    access, isVirtual });
    lookup_.functions_.back().implicit = implicit;

    // **A virtual destructor claims slots where it is declared**, and how many
    // depends on the ABI: Itanium two and adjacent, Microsoft one holding only the
    // deleting form. A derived class overrides in place, matching on "~".
    if (!isVirtual) return;
    const bool ms = target_.microsoftNames();
    const std::string deleting = linkage_.deletingDestructorSymbol(cls);

    std::vector<const Type *> none;
    if (slot < slots.size()) {
        slots[slot].symbol = ms ? deleting : out;       // the complete form
        slots[slot].isFinal = virt.isFinal;
        if (!ms && slot + 1 < slots.size() &&
            slots[slot + 1].name == "~$deleting")
            slots[slot + 1].symbol = deleting;
        return;
    }
    slots.push_back(VSlot{ "~", ms ? deleting : out, none, false });
    slots.back().isFinal = virt.isFinal;
    if (!ms) slots.push_back(VSlot{ "~$deleting", deleting, none, false });
}

// **What cl's most-derived flag guards**: the vbtable pointers this class
// stores and the virtual bases it builds.
std::vector<StmtPtr> Parser::guardedVirtualBaseInit(
        const Type *type, const std::string &cls, int thisSlot, int flagSlot,
        std::size_t pos, int srcSlot, bool moving,
        std::map<std::string, std::vector<ExprPtr> > *vbaseArgs,
        std::vector<Alive> *vbs) {
    std::vector<StmtPtr> out;
    if (!target_.microsoftNames() || !type->hasVirtualBase() || flagSlot < 0)
        return out;

    std::vector<StmtPtr> inside;
    classes_.storeVbptr(cls, type, thisSlot, inside);
    std::vector<int> builtBase;
    std::vector<StmtPtr> built = virtualBaseCalls(type, thisSlot, true, pos,
                                                  srcSlot, moving, vbaseArgs, &builtBase);
    const int count = vbs != nullptr ? allocateFrameSlot(types_.intType()) : 0;
    auto counted = [&](long long n) {
        ExprPtr slot(Var::local("$vb", count));
        slot->setType(types_.intType());
        ExprPtr value(new Num(n));
        value->setType(types_.intType());
        ExprPtr set(new Assign(std::move(slot), std::move(value)));
        set->setType(types_.intType());
        return StmtPtr(new ExprStmt(std::move(set)));
    };
    for (std::size_t i = 0; i < built.size(); i++) {
        inside.push_back(std::move(built[i]));
        if (vbs == nullptr || builtBase[i] < 0) continue;
        Alive vb{ "this", thisSlot, type };
        vb.owner = type;
        vb.base = builtBase[i];
        vb.whenSlot = count;
        vb.whenAbove = static_cast<int>(vbs->size());
        vbs->push_back(vb);
        inside.push_back(counted(static_cast<long long>(vbs->size())));
    }
    if (inside.empty()) return out;
    if (vbs != nullptr) out.push_back(counted(0));

    ExprPtr flag(Var::local(".initVBases", flagSlot));
    flag->setType(types_.intType());
    out.push_back(StmtPtr(new If(std::move(flag),
                                 StmtPtr(new Block(std::move(inside))),
                                 StmtPtr())));
    return out;
}

// The virtual bases of `type`, built or destroyed through `this`.
std::vector<StmtPtr> Parser::virtualBaseCalls(const Type *type, int thisSlot,
                                              bool building, std::size_t pos,
                                              int srcSlot, bool moving,
                                              std::map<std::string,
                                                  std::vector<ExprPtr> >
                                                  *vbaseArgs,
                                              std::vector<int> *builtBase) {
    std::vector<StmtPtr> out;
    // **[class.base.init]/10's order, which is not the layout's**: a base's
    // own virtual bases come before it, each direct base in turn - clang's
    // vbases(), and V before the W that names V virtually.
    struct Order {
        static void of(const Type *t, std::vector<const Type::BaseSpec *> &out,
                       std::set<const Type *> &seen) {
            const std::vector<const Type::BaseSpec *> ds = t->directBases();
            for (std::size_t i = 0; i < ds.size(); i++) {
                of(ds[i]->type, out, seen);
                if (ds[i]->isVirtual && seen.insert(ds[i]->type).second)
                    out.push_back(ds[i]);
            }
        }
    };
    std::vector<const Type::BaseSpec *> found;
    std::set<const Type *> seen;
    Order::of(type, found, seen);
    std::vector<Type::BaseSpec> bs;
    std::vector<int> bsIndex;
    for (std::size_t i = 0; i < found.size(); i++) {
        const Type *b = found[i]->type;
        const std::vector<Type::BaseSpec> &all = type->bases();
        for (std::size_t k = 0; k < all.size(); k++)
            if (all[k].isVirtual && all[k].type == b) {
                bs.push_back(all[k]);
                bsIndex.push_back(static_cast<int>(k));
            }
    }
    const Type *self = types_.pointerTo(type);
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    for (std::size_t n = 0; n < bs.size(); n++) {
        const std::size_t i = building ? n : bs.size() - 1 - n;
        if (!bs[i].isVirtual) continue;
        const Type *base = bs[i].type;
        if (base->tag().empty()) continue;

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        ExprPtr addr;
        if (bs[i].offset == 0) {
            me->setType(basePtr);
            addr = std::move(me);
        } else {
            me->setType(self);
            ExprPtr asChars(new Cast(chars, std::move(me)));
            asChars->setType(chars);
            ExprPtr step(new Num(static_cast<long long>(bs[i].offset)));
            step->setType(types_.get(Kind::LongLong));
            ExprPtr moved(new Binary(BinOp::Add, std::move(asChars),
                                     std::move(step)));
            moved->setType(chars);
            addr = ExprPtr(new Cast(basePtr, std::move(moved)));
            addr->setType(basePtr);
        }

        std::string symbol;
        ExprPtr srcArg;
        const Type *srcParam = nullptr;
        Signature namedCtor;
        const Signature *ctor = nullptr;
        std::vector<ExprPtr> ctorArgs;
        if (building) {
            // **A copy builds its virtual base from the source's**, not from
            // nothing: `Dia b(a)` copy-constructs the one `V`, which is what
            // clang emits.
            std::map<std::string, std::vector<ExprPtr> >::iterator said =
                vbaseArgs == nullptr ? std::map<std::string,
                    std::vector<ExprPtr> >::iterator()
                                     : vbaseArgs->find(base->tag());
            if (vbaseArgs != nullptr && said != vbaseArgs->end() &&
                !said->second.empty()) {
                ctorArgs.swap(said->second);
                namedCtor = overload_.resolveOverload(constructorKey(base->tag()),
                                            ctorArgs, pos);
                applyDefaults(namedCtor, ctorArgs, pos);
                ctor = &namedCtor;
            } else if (srcSlot >= 0) {
                if (moving) ctor = classes_.moveConstructorOf(base);
                if (ctor == nullptr) ctor = classes_.copyConstructorOf(base);
                // A trivial virtual base has no constructor to call; its bytes
                // travel with the member walk in C2, as they do today.
                if (ctor == nullptr) continue;
                markUsed(ctor);
            } else {
                ctor = classes_.defaultConstructorOf(base);
                if (ctor == nullptr) continue;
                markUsed(ctor);
            }
            // **The base-subobject name, where the ABI has one.** Itanium
            // spells C2 differently from C1; Microsoft has one name and says
            // "subobject" with the flag instead, which `completeCall` adds.
            if (!target_.microsoftNames()) {
                symbol = linkage_.baseConstructorSymbol(*ctor, base);
            } else {
                symbol = ctor->symbol;
            }
            if (srcSlot >= 0 && !ctor->params.empty()) {
                srcParam = ctor->params[0];
                ExprPtr that(Var::local("that", srcSlot));
                that->setType(self);
                ExprPtr asChars2(new Cast(chars, std::move(that)));
                asChars2->setType(chars);
                ExprPtr step2(new Num(static_cast<long long>(bs[i].offset)));
                step2->setType(types_.get(Kind::LongLong));
                ExprPtr moved2(new Binary(BinOp::Add, std::move(asChars2),
                                          std::move(step2)));
                moved2->setType(chars);
                ExprPtr at(new Cast(basePtr, std::move(moved2)));
                at->setType(basePtr);
                ExprPtr obj(new Unary('*', std::move(at)));
                obj->setType(base);
                if (moving) obj->setXvalue();
                srcArg = std::move(obj);
            }
        } else {
            const Signature *dtor = destructorOf(base);
            if (dtor == nullptr) continue;
            markUsed(dtor);
            // D2 on Itanium; on Microsoft `??1`, which is D2 there - the
            // virtual base's *own* virtual bases, if it has any, belong to
            // this class and not to it.
            if (!target_.microsoftNames())
                itaniumDestructorName(base->tag(), base, false, &symbol);
            else
                symbol = dtor->symbol;
        }

        std::vector<ExprPtr> args;
        args.push_back(std::move(addr));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        // A virtual base with virtual bases of its own reads its vptrs from
        // this class's VTT, the part written for it.
        if (takesVtt(base)) {
            const VttLayout &vtt = vtts_[type->tag()];
            std::map<const Type *, int>::const_iterator part =
                vtt.virtualVtt.find(base);
            if (part == vtt.virtualVtt.end())
                src_.fail(pos, "'" + type->tag() + "' has no virtual VTT for '" +
                               base->tag() + "'");
            args.push_back(classes_.vttOfClass(type, part->second));
            ps.push_back(vttType());
        }
        if (srcArg != nullptr) {
            args.push_back(std::move(srcArg));
            ps.push_back(srcParam);
        }
        // **The mem-initialiser's own arguments**, converted to the parameters
        // the chosen constructor declared, the way every other call converts
        // them. `ctorArgs` is empty for the default and copy paths above.
        for (std::size_t k = 0; k < ctorArgs.size(); k++) {
            const Type *want = k < ctor->params.size() ? ctor->params[k]
                                                       : ctorArgs[k]->type();
            ExprPtr one = decay(std::move(ctorArgs[k]));
            checkAssignable(*one, want, pos, "'" + base->tag() + "'");
            args.push_back(convert(std::move(one), want));
            ps.push_back(want);
        }
        // **A virtual base is a subobject and not the complete object**, so
        // the flag it is handed is 0: anything *it* has virtually is this
        // class's to build as well.
        out.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), symbol, nullptr, types_.get(Kind::Void),
                         ps, false, pos, std::move(args), false, 0))));
        if (builtBase != nullptr)
            builtBase->push_back(building && destructorOf(base) != nullptr ? bsIndex[i] : -1);
    }
    return out;
}

const Parser::Signature *Parser::destructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = lookup_.overloadsOf(destructorKey(cls->tag()));
    return set == nullptr ? nullptr : &lookup_.functions_[(*set)[0]];
}

// One destructor call, given the address of what to destroy. A destructor
// takes nothing but `this`, so this is the smallest call the compiler makes.
ExprPtr Parser::destructorCall(ExprPtr address, const Signature &dtor,
                               std::size_t pos) {
    // Calling one is what asks for a body, which is the only thing that makes
    // an implicit destructor a function at all.
    markUsed(&dtor);
    std::vector<ExprPtr> args;
    args.push_back(std::move(address));
    std::vector<const Type *> params;
    params.push_back(args[0]->type());
    // **A complete object of a Microsoft class with a virtual base is
    // destroyed through `??_D`**, which destroys the class's own part and then
    // the virtual bases - Itanium's D1.
    std::string symbol = dtor.symbol;
    if (target_.microsoftNames() && msVbaseClasses_.count(dtor.owner) != 0) {
        symbol = vbaseDestructorSymbol(dtor.owner);
        markSymbolUsed(symbol);
    }
    return completeCall("~" + dtor.owner, symbol, nullptr,
                        types_.get(Kind::Void), params, false, pos,
                        std::move(args));
}

// **RAII is this function**: everything constructed since `from` is destroyed, last
// first. **One region per stretch, and the stretches do not overlap** - which is what
// lets a call-site table hold them, and what the Microsoft chain walks backwards.
std::vector<StmtPtr> Parser::wrapMsCleanups(
    std::vector<StmtPtr> body,
    const std::vector<std::pair<std::size_t, std::size_t> > &built,
    std::size_t aliveAtEntry, std::size_t pos,
    const std::vector<Temporary> &temps) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int pointerSlot = allocateFrameSlot(voidPtr);
    const int selectorSlot = allocateFrameSlot(types_.intType());
    const int helpSlot = msUnwindHelp();
    functionHasPads_ = true;

    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++)
        out.push_back(std::move(body[i]));

    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first;
        const std::size_t to = k + 1 < built.size() ? built[k + 1].first
                                                    : body.size();
        std::vector<StmtPtr> guarded;
        for (std::size_t i = from; i < to; i++) guarded.push_back(std::move(body[i]));
        if (guarded.empty()) continue;

        std::vector<StmtPtr> steps;
        // The block's temporaries, each under its guard - see cleanupPad. The
        // funclets chain, so clearing as it destroys is what stops the next
        // one in the chain destroying the same object again.
        for (std::size_t j = temps.size(); j-- > 0; )
            releaseGuarded(steps, temps[j]);
        emitDestructors(steps, k == 0 ? aliveAtEntry : built[k - 1].second,
                        pos, -1, built[k].second);
        Block *b = new Block(std::move(steps));
        b->setScope(-1);

        Try *t = new Try(std::move(guarded), nullptr, pointerSlot,
                         selectorSlot, std::vector<std::string>());
        t->setCleanup(StmtPtr(b));
        t->setUnwindHelpSlot(helpSlot);
        out.push_back(StmtPtr(t));
    }
    return out;
}

std::vector<StmtPtr> Parser::wrapCleanups(
    std::vector<StmtPtr> body,
    const std::vector<std::pair<std::size_t, std::size_t> > &built,
    std::size_t aliveAtEntry, std::size_t pos,
    const std::vector<Temporary> &temps,
    const std::vector<std::size_t> &tryAt) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    // **Inside a `try` body the slots are the `try`'s.**
    int targetPtr = 0, targetSel = 0;
    const std::string handOver = unwindTarget(&targetPtr, &targetSel);
    const bool intoTry = !handOver.empty();
    const int pointerSlot = intoTry ? targetPtr : allocateFrameSlot(voidPtr);
    const int selectorSlot = intoTry ? targetSel
                                     : allocateFrameSlot(types_.intType());
    functionHasPads_ = true;
    lastCleanupSlots_ = std::make_pair(pointerSlot, selectorSlot);

    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++)
        out.push_back(std::move(body[i]));

    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first;
        const std::size_t to = k + 1 < built.size() ? built[k + 1].first
                                                    : body.size();
        // **A region is split around every `try` in it.**
        std::size_t cur = from;
        while (cur < to) {
            std::size_t stop = to;
            for (std::size_t t = 0; t < tryAt.size(); t++)
                if (tryAt[t] >= cur && tryAt[t] < stop) stop = tryAt[t];
            std::vector<StmtPtr> guarded;
            for (std::size_t i = cur; i < stop; i++)
                guarded.push_back(std::move(body[i]));
            if (!guarded.empty()) {
                // **From the `try`'s entry, not this block's.**
                Try *seg = new Try(
                    std::move(guarded),
                    cleanupPad(handOver == tryChainLabel_ && intoTry
                                   ? tryChainAliveFrom_ : aliveAtEntry,
                               built[k].second, pointerSlot,
                               temps, pos, handOver),
                    pointerSlot, selectorSlot, std::vector<std::string>());
                // The types are the `try`'s and are not read yet; tryStatement
                // patches every segment once its handlers have been.
                if (intoTry && handOver == tryChainLabel_)
                    tryBodySegments_.push_back(seg);
                out.push_back(StmtPtr(seg));
            }
            if (stop == to) break;
            out.push_back(std::move(body[stop]));      // the try, uncovered
            cur = stop + 1;
        }
    }
    return out;
}

// **A statement list under a cleanup action of its own**: `onUnwind` runs when an exception leaves it, then
// unwinding goes on - for a non-destructor cleanup, such as the guard a throwing static local's initialiser
// leaves "being built", [stmt.dcl]/4 (2026-10-21, review of 1.25, fix 3). Shapes: wrapMsCleanups, wrapCleanups.
StmtPtr Parser::cleanupRegion(std::vector<StmtPtr> body, StmtPtr onUnwind) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    functionHasPads_ = true;
    std::vector<StmtPtr> steps;
    steps.push_back(std::move(onUnwind));
    if (target_.microsoftNames()) {
        Block *b = new Block(std::move(steps));
        b->setScope(-1);
        Try *t = new Try(std::move(body), nullptr, allocateFrameSlot(voidPtr),
                         allocateFrameSlot(types_.intType()), std::vector<std::string>());
        t->setCleanup(StmtPtr(b));
        t->setUnwindHelpSlot(msUnwindHelp());
        return StmtPtr(t);
    }
    int targetPtr = 0, targetSel = 0;
    const std::string handOver = unwindTarget(&targetPtr, &targetSel);
    const bool intoTry = !handOver.empty();
    const int pointerSlot = intoTry ? targetPtr : allocateFrameSlot(voidPtr);
    const int selectorSlot = intoTry ? targetSel : allocateFrameSlot(types_.intType());
    if (intoTry) steps.push_back(StmtPtr(new Goto(handOver)));
    else         steps.push_back(resumeUnwinding(pointerSlot));
    Try *seg = new Try(std::move(body), unwindPad(std::move(steps)), pointerSlot, selectorSlot,
                       std::vector<std::string>());
    if (intoTry && handOver == tryChainLabel_) tryBodySegments_.push_back(seg);
    return StmtPtr(seg);
}

// **One temporary of a block, released on the way out of it, under its
// guard.**
void Parser::releaseGuarded(std::vector<StmtPtr> &steps, const Temporary &t) {
    if (t.flag == 0) return;
    std::vector<StmtPtr> both;
    if (t.exceptionStorage) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        std::vector<ExprPtr> args;
        ExprPtr held(Var::local("$copy", t.slot));
        held->setType(voidPtr);
        args.push_back(std::move(held));
        both.push_back(StmtPtr(new ExprStmt(
            runtimeCall("__cxa_free_exception", types_.get(Kind::Void),
                        std::move(args)))));
    } else if (t.newStorage) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        ExprPtr held(Var::local("$copy", t.slot));
        held->setType(chars);
        if (t.newCookie != 0) {
            // The allocation began a cookie before the first element.
            ExprPtr back(new Num(static_cast<long long>(-t.newCookie)));
            back->setType(types_.intType());
            ExprPtr start(new Binary(BinOp::Add, std::move(held), std::move(back)));
            start->setType(chars);
            held = std::move(start);
        }
        ExprPtr raw(new Cast(voidPtr, std::move(held)));
        raw->setType(voidPtr);
        ExprPtr give = t.newNothrow ? callNothrowDeallocator(t.newArray, std::move(raw))
                     : t.newArray  ? deallocateArray(t.type, std::move(raw), 0)
                                   : deallocate(t.type, std::move(raw), 0);
        both.push_back(StmtPtr(new ExprStmt(std::move(give))));
    } else {
        ExprPtr gone = temporaryDestruction(t, 0);
        if (gone == nullptr) return;
        both.push_back(StmtPtr(make_owned<ExprStmt>(std::move(gone))));
    }
    both.push_back(StmtPtr(new ExprStmt(setGuard(t.flag, 0))));
    ExprPtr live(Var::local("$guard", t.flag));
    live->setType(types_.intType());
    steps.push_back(StmtPtr(new If(std::move(live),
                                   StmtPtr(new Block(std::move(both))),
                                   StmtPtr())));
}

// **What an exception has to do on its way out of a scope.** The objects are the ones
// a `return` unwinds - `alive_` holds them and nothing new had to track them - and
// the difference is where the code runs from: a pad, ending in _Unwind_Resume.
StmtPtr Parser::cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                           const std::vector<Temporary> &temps,
                           std::size_t pos, const std::string &chainLabel) {
    // **Bounded rather than truncated.**
    std::vector<StmtPtr> steps;
    // **The temporaries of this block go first**, being the most recently
    // built - and each under its own guard, because the pad may be reached
    // from a point in the statement where this one does not exist yet.
    for (std::size_t k = temps.size(); k-- > 0; )
        releaseGuarded(steps, temps[k]);
    emitDestructors(steps, from, pos, -1, to);

    // **A segment of a `try` body hands over rather than resuming.** Its row
    // carries the `try`'s catch types, so the selector may name a handler -
    // and the one chain that tests it lives in the `try`'s own pad.
    if (chainLabel.empty()) steps.push_back(resumeUnwinding(pointerSlot));
    else                    steps.push_back(StmtPtr(new Goto(chainLabel)));
    return unwindPad(std::move(steps));
}

StmtPtr Parser::resumeUnwinding(int pointerSlot) {
    std::vector<ExprPtr> args;
    if (target_.resumeTakesException()) {
        ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
        ptr->setType(types_.pointerTo(types_.get(Kind::Void)));
        args.push_back(std::move(ptr));
        return StmtPtr(new ExprStmt(runtimeCall("_Unwind_Resume", types_.get(Kind::Void), std::move(args))));
    }
    return StmtPtr(new ExprStmt(runtimeCall("__cxa_end_cleanup", types_.get(Kind::Void), std::move(args))));
}

StmtPtr Parser::unwindPad(std::vector<StmtPtr> steps) {
    StmtPtr last = std::move(steps.back());
    steps.pop_back();
    Block *inner = new Block(std::move(steps));
    inner->setScope(-1);
    inner->setUnwindCleanup();
    std::vector<StmtPtr> outer;
    outer.push_back(StmtPtr(inner));
    outer.push_back(std::move(last));
    Block *b = new Block(std::move(outer));
    b->setScope(-1);
    return StmtPtr(b);
}

void Parser::emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                             std::size_t pos, int except, std::size_t to) {
    if (to > alive_.size()) to = alive_.size();
    for (std::size_t i = to; i > from; i--) {
        const Alive &a = alive_[i - 1];
        if (except >= 0 && a.offset == except && (!a.byAddress || except == sretSlot_)) continue;
        destroyObject(into, a, pos);
    }
}

// One object's destructor call, for the end of a scope and for every jump
// that leaves one early - a goto fills these in after its label is known,
// which is why this takes the record and not an index into alive_.
void Parser::destroyObject(std::vector<StmtPtr> &into, const Alive &a,
                           std::size_t pos) {
    if (a.whenSlot >= 0) {
        Alive plain = a;
        plain.whenSlot = -1;
        std::vector<StmtPtr> calls;
        destroyObject(calls, plain, pos);
        ExprPtr n(Var::local("$vb", a.whenSlot));
        n->setType(types_.intType());
        ExprPtr above(new Num(static_cast<long long>(a.whenAbove)));
        above->setType(types_.intType());
        ExprPtr built(new Binary(BinOp::Gt, std::move(n), std::move(above)));
        built->setType(types_.intType());
        into.push_back(StmtPtr(new If(std::move(built), StmtPtr(new Block(std::move(calls))), StmtPtr())));
        return;
    }
    if (a.builtSlot >= 0) {
        // The first `$i` elements, from the array's first element: a member's, a pointer's or a slot's.
        const Type *ptr = types_.pointerTo(a.cls);
        ExprPtr base;
        if (a.owner != nullptr) {
            base = decay(thisMember(a.offset, a.owner, a.owner->members()[a.member]));
        } else if (a.byAddress) {
            base = ExprPtr(Var::local(a.name, a.offset));
            base->setType(ptr);
        } else {
            ExprPtr whole(Var::local(a.name, a.offset));
            whole->setType(a.cls);
            base = ExprPtr(new Unary('&', std::move(whole)));
            base->setType(ptr);
        }
        base = ExprPtr(new Cast(ptr, std::move(base)));
        base->setType(ptr);
        ExprPtr n(Var::local("$i", a.builtSlot));
        n->setType(types_.intType());
        into.push_back(StmtPtr(new ExprStmt(callVectorLoop(
            vectorDestructor(a.cls, pos), a.cls, std::move(base), std::move(n), pos))));
        return;
    }
    if (a.owner != nullptr) {
        if (a.member >= 0)
            classes_.memberDestructor(into, a.owner->tag(), a.owner, a.offset, static_cast<std::size_t>(a.member), pos);
        else if (a.base >= 0)
            into.push_back(baseDestructorCall(a.owner, static_cast<std::size_t>(a.base), a.offset, pos));
        else
            into.push_back(selfDestructorCall(a.owner, a.offset, pos));
        return;
    }
    const Signature *dtor = destructorOf(a.cls);
    if (dtor == nullptr) return;

    // An array: its elements last first, by the class's loop.
    if (a.count > 0) {
        ExprPtr first(Var::local(a.name, a.offset));
        first->setType(a.cls);
        ExprPtr base(new Unary('&', std::move(first)));
        base->setType(types_.pointerTo(a.cls));
        ExprPtr n(new Num(a.count));
        n->setType(types_.get(target_.sizeType()));
        into.push_back(StmtPtr(new ExprStmt(callVectorLoop(
            vectorDestructor(a.cls, pos), a.cls, std::move(base), std::move(n), pos))));
        return;
    }

    ExprPtr addr;
    if (a.byAddress) {
        // The slot holds the caller's pointer, and that pointer IS the
        // object's address.
        addr = ExprPtr(Var::local(a.name, a.offset));
        addr->setType(types_.pointerTo(a.cls));
    } else {
        ExprPtr object(Var::local(a.name, a.offset));
        object->setType(a.cls);
        addr = ExprPtr(new Unary('&', std::move(object)));
        addr->setType(types_.pointerTo(a.cls));
    }
    into.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(addr),
                                                       *dtor, pos))));
}

void Parser::markSymbolUsed(const std::string &symbol) {
    if (symbol.empty()) return;
    for (std::size_t i = 0; i < lookup_.functions_.size(); i++)
        if (lookup_.functions_[i].symbol == symbol) {
            refuseDeleted(lookup_.functions_[i], at_ + 1 < tokens_.size() ? peek().pos : lookup_.functions_[i].pos);
            lookup_.functions_[i].used = true;
            return;
        }
}

// A use of a deleted function is refused here as well as at a call: the compiler-written members reach their bases' and members' through this.
void Parser::markUsed(const Signature *f) {
    refuseDeleted(*f, at_ + 1 < tokens_.size() ? peek().pos : f->pos);
    noteUsed(static_cast<std::size_t>(f - &lookup_.functions_[0]));
}

// [temp.inst]: a candidate made for ranking uses nothing until it is chosen - clang declares such a
// specialization and never instantiates its body - so while one's body is read, its uses are its own.
void Parser::noteUsed(std::size_t i) {
    if (candidateUses_ != nullptr) { candidateUses_->push_back(i); return; }
    lookup_.functions_[i].used = true;
    std::vector<std::size_t> later;
    later.swap(lookup_.functions_[i].deferredUses);
    for (std::size_t k : later) noteUsed(k);
}

void Parser::requireConstInitialised(const Type *t, const std::string &name,
                                     std::size_t pos) {
    if (!t->isConst() || classes_.constDefaultInitialisable(t)) return;
    const Type *u = t->unqualified();
    while (u->isArray()) u = u->pointee()->unqualified();
    if (u->isStructOrUnion())
        // The suggestion is spelled with the tag and not with describe(), which
        // says "struct S" - and `struct S()` is not something anybody can write.
        src_.fail(pos, "'" + name + "' is const and nothing here would "
                       "initialise it - '" + u->describe() + "' has no "
                       "constructor of its own and leaves a member unset. Write "
                       "'const " + u->tag() + " " + name + " = " +
                       u->tag() + "();'");
    src_.fail(pos, "'" + name + "' is const and has no initialiser, so it would "
                   "hold whatever was there - [dcl.init]/7 refuses that. Give it "
                   "a value where it is declared");
}

// **A member of a virtual base is reached through the vtable**, because where
// that base sits depends on the complete object and not on the type written
// here.
void Parser::refuseVirtualBaseMember(const Type *staticType, const Member &m,
                                     const std::string &name,
                                     std::size_t pos) {
    if (m.inVirtualBase == nullptr || !target_.microsoftNames()) return;
    const Type *owner = staticType->unqualified();
    int baseAt = 0;
    if (owner->isStructOrUnion() && owner->vbptrOffset() >= 0 &&
        classes_.virtualBaseSlot(owner, m.inVirtualBase, &baseAt) > 0)
        return;                       // reachable through this class's vbtable
    src_.fail(pos, "'" + name + "' is a member of '" +
                   m.inVirtualBase->describe() + "', which is a virtual base "
                   "this class does not reach through a vbtable of its own, "
                   "and x86_64-windows has no other way there. Both Itanium "
                   "targets compile this");
}

ExprPtr Parser::thisMember(int thisSlot, const Type *cls, const Member &m) {
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(types_.pointerTo(cls));
    ExprPtr obj(new Unary('*', std::move(me)));
    obj->setType(cls);
    ExprPtr acc(new MemberAccess(std::move(obj), m.name, m.offset,
                                 m.width, m.bitOffset));
    acc->setType(m.type);
    return acc;
}

// A constructor, read at the point its '(' was seen: a member function whose name is
// the class and whose return type is nothing at all, keyed under "Point::Point" so
// every piece of overload machinery applies to it unchanged.
void Parser::declareConstructor(const std::string &cls, std::size_t pos,
                                Access access, bool isExplicit) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (variadic)
        src_.fail(pos, "a constructor cannot take '...'");
    refuseSpecialRefQualifier("constructor");
    // Read here rather than at the call site.
    pendingNoexcept_ = exceptionSpecification();
    const SpecialDef special = takeSpecialDef();

    // **A defaulted constructor is the implicit one and is not declared here**: declareImplicitSpecials
    // writes it exactly as it would have, and reads `defaulted_` for the access and for [class.copy]/9.
    if (special == SpecialDef::Defaulted) {
        const Type *self = lookup_.findTypedef(cls);
        Defaulted &df = defaulted_[cls];
        const Type *p = params.size() == 1 ? params[0] : nullptr;
        const bool ofSelf = p != nullptr && p->isReference() && self != nullptr &&
                            p->referent()->unqualified() == self->unqualified();
        if (params.empty())                      defaultedMember(df.defaultCtor, access, pos, "the default constructor");
        else if (ofSelf && !p->isRValueReference()) defaultedMember(df.copyCtor, access, pos, "the copy constructor");
        else if (ofSelf)                         defaultedMember(df.moveCtor, access, pos, "the move constructor");
        else
            src_.fail(pendingSpecialAt_, "'" + cls + "::" + cls + "' with these parameters is not a special "
                                         "member function, and only one of those may be defaulted - "
                                         "[dcl.fct.def.default]/1");
        return;
    }

    // A constructor returns nothing, and saying so as void is what lets the rest of the compiler treat the call like any other.
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);

    std::string key = constructorKey(cls);
    std::vector<std::size_t> &set = lookup_.functionIndex_[key];
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = lookup_.functions_[set[k]];
        if (classes_.sameParameters(f.params, params))
            src_.fail(pos, "'" + cls + "::" + cls + "' is declared twice");
    }

    const char code = access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out, why;
    bool ok = target_.microsoftNames()
            ? microsoftConstructorName(cls, lookup_.findTypedef(cls), linkage_.manglingType(fn),
                                       code, &out, &why)
            : itaniumConstructorName(cls, lookup_.findTypedef(cls), fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + cls + "::" + cls + "' cannot be given a name the "
                       "linker can hold: " + why);

    set.push_back(lookup_.functions_.size());
    if (!pendingDefaults_.empty()) defaultArgs_[out] = pendingDefaults_;
    if (!pendingDefaults_.empty())
        defaultArgNamespace_[out] = namespaceStack_;
    pendingDefaults_.clear();
    lookup_.functions_.push_back(Signature{ cls, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, cls, false, access });
    lookup_.functions_.back().isExplicit = isExplicit;
    lookup_.functions_.back().isNoexcept = pendingNoexcept_;
    lookup_.functions_.back().spec = pendingSpec_;
    lookup_.functions_.back().deleted = special == SpecialDef::Deleted;
    pendingNoexcept_ = false;
}

// **How many objects a member array holds, and of what type.** Every dimension
// counts: `E g[2][3]` is six elements of `E`, not two of `E[3]`, and
// [class.base.init]/8 default-initialises every one of them.
const Type *memberElements(const Type *t, long long *count) {
    *count = 1;
    while (t != nullptr && t->isArray()) {
        if (t->length() < 0) { *count = -1; return t->pointee(); }
        *count *= t->length();
        t = t->pointee();
    }
    return t;
}

// The class a member is built from: the element type when the member is an
// array, and nothing at all when it is not of class type.
const Type *memberClass(const Type *t) {
    while (t != nullptr && t->isArray()) t = t->pointee();
    return (t != nullptr && t->isStructOrUnion()) ? t->unqualified() : nullptr;
}

bool Parser::memberHasDestructor(const Type *t) const {
    return destructorOf(memberClass(t)) != nullptr;
}

// One element of an array member, by address: the member's own address, decayed, plus
// the index times the element's size. **In bytes, and deliberately** - a Binary built
// here is not the parser's pointer arithmetic and gets none of its scaling.
ExprPtr indexBytes(TypeTable &types, ExprPtr decayed, const Type *elem,
                   int indexSlot, const Target &target) {
    const Type *idx = types.intType();
    ExprPtr i(Var::local("$i", indexSlot));
    i->setType(idx);
    ExprPtr size(new Num(static_cast<long long>(elem->size(target))));
    size->setType(idx);
    ExprPtr off(new Binary(BinOp::Mul, std::move(i), std::move(size)));
    off->setType(idx);
    const Type *ptr = types.pointerTo(elem);
    ExprPtr at(new Binary(BinOp::Add, std::move(decayed), std::move(off)));
    at->setType(ptr);
    return at;
}

// `S a[4];` where S has constructors - the default constructor once per
// element, in a loop.
const Parser::Signature *
Parser::initializerListConstructor(const Type *cls, const Type **ilType) {
    const std::vector<std::size_t> *set = lookup_.overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t i = 0; i < set->size(); i++) {
        const Signature &c = lookup_.functions_[(*set)[i]];
        if (c.params.size() != 1) continue;
        const Type *p = c.params[0]->unqualified();
        if (p->isStructOrUnion() && p->isSpecialization() &&
            p->templateName() == "initializer_list" &&
            (p->templateNamespace() == "std" ||
             p->templateNamespace() == "std::")) {
            if (ilType != nullptr) *ilType = c.params[0];
            return &c;
        }
    }
    return nullptr;
}

// **A brace-init-list becomes a backing array and an initializer_list over
// it**, [dcl.init.list]/5. The array holds the elements; the list is `{
// &array[0], count }` - the pointer and count <initializer_list> lays out.
ExprPtr Parser::buildInitializerList(const Type *ilType, Init &in,
                                     std::size_t pos, std::vector<StmtPtr> &into, bool flush) {
    const Type *elem = ilType->templateArgs().empty()
                         ? types_.get(Kind::Int)
                         : ilType->templateArgs()[0].type;
    const long long n = static_cast<long long>(in.items.size());
    const int k = ilTemps_++;

    const Type *arrType = types_.arrayOf(elem, n);
    const std::string bk = ".ilbacking" + std::to_string(k);
    const int bkOff = declare(bk, arrType, pos);

    // **A class element with an initializer_list constructor is built through
    // it, not aggregate-initialised.**
    const Type *plainElem = elem->unqualified();
    const Type *innerIl = nullptr;
    const Signature *innerCtor = plainElem->isStructOrUnion()
        ? initializerListConstructor(plainElem, &innerIl) : nullptr;
    if (innerCtor != nullptr) {
        markUsed(innerCtor);
        const std::string sym = innerCtor->symbol;
        const Type *innerParam = innerCtor->params[0];
        const Type *elemPtr = types_.pointerTo(plainElem);
        for (std::size_t i = 0; i < in.items.size(); i++) {
            if (!in.items[i].isList)
                src_.fail(in.items[i].pos, "this element is built from a braced "
                          "list and this initialiser is not braced");
            ExprPtr list = buildInitializerList(innerIl, in.items[i], pos, into, flush);
            ExprPtr arr(Var::local(bk, bkOff)); arr->setType(arrType);
            ExprPtr idx(new Num(static_cast<long long>(i)));
            idx->setType(types_.get(Kind::Int));
            ExprPtr addr = arithmetic(BinOp::Add, decay(std::move(arr)),
                                      std::move(idx), pos);
            std::vector<ExprPtr> all;
            all.push_back(std::move(addr));
            all.push_back(std::move(list));
            std::vector<const Type *> ps{ elemPtr, innerParam };
            into.push_back(StmtPtr(new ExprStmt(
                completeCall(plainElem->tag(), sym, nullptr,
                             types_.get(Kind::Void), ps, false, pos,
                             std::move(all)))));
        }
    } else if (plainElem->isStructOrUnion() && hasConstructors(plainElem->tag())) {
        // [dcl.init.list]/5: each element is copy-initialised from its initialiser, by a constructor.
        const Type *elemPtr = types_.pointerTo(plainElem);
        for (std::size_t i = 0; i < in.items.size(); i++) {
            // A braced element is copy-list-initialised, [over.match.list]: built in its slot by the
            // constructor its items choose, never an explicit one - no temporary and no copy.
            if (in.items[i].isList) {
                checkNotAbstract(plainElem, in.items[i].pos, "an element of this initializer_list would make");
                std::vector<ExprPtr> args;
                Signature ctor = listConstructor(plainElem, in.items[i], args, true, in.items[i].pos);
                applyDefaults(ctor, args, in.items[i].pos);
                if (ctor.access != Access::Public && !insideAccessOf(plainElem, ctor.access) &&
                    !isFriendOf(plainElem))
                    src_.fail(in.items[i].pos, "'" + plainElem->describe() + "' has no public constructor "
                              "taking these arguments - the one that matches is " +
                              (ctor.access == Access::Private ? "private" : "protected"));
                ExprPtr arr(Var::local(bk, bkOff)); arr->setType(arrType);
                ExprPtr idx(new Num(static_cast<long long>(i)));
                idx->setType(types_.get(Kind::Int));
                std::vector<ExprPtr> all;
                all.push_back(arithmetic(BinOp::Add, decay(std::move(arr)), std::move(idx), pos));
                for (ExprPtr &a : args) all.push_back(std::move(a));
                std::vector<const Type *> ps{ elemPtr };
                ps.insert(ps.end(), ctor.params.begin(), ctor.params.end());
                into.push_back(StmtPtr(new ExprStmt(
                    completeCall(plainElem->tag(), ctor.symbol, nullptr, types_.get(Kind::Void), ps,
                                 ctor.variadic, pos, std::move(all)))));
                continue;
            }
            ExprPtr value = std::move(in.items[i].value);
            if (ExprPtr made = userConversion(plainElem, value, pos)) value = std::move(made);
            const Signature *cc = classes_.copyConstructorOf(plainElem);
            const Signature *mv = classes_.moveConstructorOf(plainElem);
            const Signature *use = (!isLvalue(*value) && mv != nullptr) ? mv : cc;
            ExprPtr arr(Var::local(bk, bkOff)); arr->setType(arrType);
            ExprPtr idx(new Num(static_cast<long long>(i)));
            idx->setType(types_.get(Kind::Int));
            ExprPtr addr = arithmetic(BinOp::Add, decay(std::move(arr)), std::move(idx), pos);
            if (use == nullptr) {
                ExprPtr slot(new Unary('*', std::move(addr))); slot->setType(plainElem);
                ExprPtr store(new Assign(std::move(slot), std::move(value))); store->setType(plainElem);
                into.push_back(StmtPtr(new ExprStmt(std::move(store))));
                continue;
            }
            markUsed(use);
            std::vector<ExprPtr> all;
            all.push_back(std::move(addr));
            all.push_back(std::move(value));
            std::vector<const Type *> ps{ elemPtr, use->params[0] };
            into.push_back(StmtPtr(new ExprStmt(
                completeCall(plainElem->tag(), use->symbol, nullptr,
                             types_.get(Kind::Void), ps, false, pos, std::move(all)))));
        }
        // In an expression the elements' temporaries wait for its end, [class.temporary]/4 (M1).
        if (flush) flushTemporaries(into);
        if (destructorOf(plainElem) != nullptr) {
            Alive whole{ bk, bkOff, plainElem };
            whole.count = n;
            alive_.push_back(whole);
        }
    } else {
        std::vector<InitStep> path;
        emitInit(bk, path, arrType, in, into);
    }

    const std::string ilName = ".ilobject" + std::to_string(k);
    const int ilOff = declare(ilName, ilType, pos);

    const Member *fm = ilType->findMember("first_");
    const Member *cm = ilType->findMember("count_");
    if (fm == nullptr || cm == nullptr)
        src_.fail(pos, "the std::initializer_list here has no first_ and count_ "
                       "members - the <initializer_list> the compiler builds is "
                       "not the one this program included");

    // first_ = &array[0], the array decayed to a pointer to its first element.
    ExprPtr arr(Var::local(bk, bkOff)); arr->setType(arrType);
    ExprPtr first = decay(std::move(arr));
    ExprPtr ilA(Var::local(ilName, ilOff)); ilA->setType(ilType);
    ExprPtr fdst(new MemberAccess(std::move(ilA), "first_", fm->offset));
    fdst->setType(fm->type);
    ExprPtr fst(new Assign(std::move(fdst), convert(std::move(first), fm->type)));
    fst->setType(fm->type);
    into.push_back(StmtPtr(new ExprStmt(std::move(fst))));

    // count_ = n
    ExprPtr ilB(Var::local(ilName, ilOff)); ilB->setType(ilType);
    ExprPtr cdst(new MemberAccess(std::move(ilB), "count_", cm->offset));
    cdst->setType(cm->type);
    ExprPtr cnt(new Num(n)); cnt->setType(cm->type);
    ExprPtr cst(new Assign(std::move(cdst), std::move(cnt)));
    cst->setType(cm->type);
    into.push_back(StmtPtr(new ExprStmt(std::move(cst))));

    ExprPtr result(Var::local(ilName, ilOff));
    result->setType(ilType);
    return result;
}

StmtPtr Parser::constructLocalArray(const Declared &d, int offset,
                                    int indexSlot) {
    const Type *elem = d.type;
    long long count = 1;
    while (elem->isArray()) { count *= elem->length(); elem = elem->pointee(); }
    const Type *plain = elem->unqualified();

    const Signature *ctor = classes_.defaultConstructorOf(plain);
    if (ctor == nullptr)
        src_.fail(d.pos, "'" + plain->describe() + "' has constructors but none "
                         "that takes nothing, and an array of it has no way to "
                         "say what to pass");
    if (ctor->access != Access::Public &&
        !insideAccessOf(plain, ctor->access) && !isFriendOf(plain))
        src_.fail(d.pos, "'" + plain->describe() + "' has no public default "
                         "constructor, and an array of it needs one");
    // **Marked used, or an implicit one is declared and never emitted.**
    markUsed(ctor);
    // **Copied before the defaults are read**: reading one can parse an expression
    // that grows `lookup_.functions_` under the pointer just taken into it. The defaults sit
    // inside the statement the loop repeats, so they are evaluated once per element.
    const Signature chosen = *ctor;
    std::vector<ExprPtr> defaults;
    applyDefaults(chosen, defaults, d.pos);

    const Type *ptr = types_.pointerTo(plain);
    ExprPtr base(Var::local(d.name, offset));
    base->setType(d.type);
    ExprPtr at = indexBytes(types_, decay(std::move(base)), plain, indexSlot,
                            target_);

    std::vector<ExprPtr> args;
    args.push_back(std::move(at));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    for (std::size_t i = 0; i < defaults.size(); i++) {
        args.push_back(std::move(defaults[i]));
        ps.push_back(chosen.params[i]);
    }

    StmtPtr one(new ExprStmt(completeCall(plain->tag(), chosen.symbol, nullptr,
                                          types_.get(Kind::Void), ps, false,
                                          d.pos, std::move(args))));
    return guardElements(eachElement(indexSlot, count, std::move(one)),
                         Alive{ d.name, offset, plain }, indexSlot, d.pos);
}

// `S a[3] = { S(1), {2, 3}, x };` - [dcl.init.aggr]/2: each element copy-initialised from its
// initialiser, in order, nested braces for each dimension or elided; the rest value-initialised,
// as from `{}`. What each one becomes is what `S s = ...;` would make of it.
StmtPtr Parser::constructLocalArrayFromList(Declared &d, int offset,
                                            const std::function<int()> &declareLate,
                                            const std::string &symbol) {
    // `S a[] = {...}` takes its length from the list: read first, declared after.
    const bool unbounded = d.type->length() < 0;
    long long total = 1;
    const Type *elem = d.type;
    while (elem->isArray()) { total *= elem->length() < 0 ? 0 : elem->length(); elem = elem->pointee(); }
    const Type *plain = elem->unqualified();
    const int size = plain->size(target_);

    // One initialiser per element, read from the list: `given` says whether it had one.
    struct Init { std::vector<ExprPtr> args; std::vector<Temporary> temps; bool braced = false; bool direct = false; bool given = false; };
    std::vector<Init> inits(static_cast<std::size_t>(total));
    // `S(args)` is the whole initialiser when a ',' or '}' follows its closing parenthesis.
    auto closesElement = [&]() {
        std::size_t k = 1;
        int depth = 0;
        for (;; k++) {
            const Token &t = peekAt(k);
            if (t.kind == TokenKind::End) return false;
            if (t.is("(")) depth++;
            if (t.is(")") && --depth == 0) break;
        }
        return peekAt(k + 1).is(",") || peekAt(k + 1).is("}");
    };
    std::function<void(const Type *, long long)> readList = [&](const Type *t, long long base) {
        long long flat = 1;
        const Type *e = t;
        while (e->isArray()) { flat *= e->length(); e = e->pointee(); }
        if (t->length() < 0) flat = LLONG_MAX;
        const Type *sub = t->pointee();
        long long subFlat = 1;
        for (const Type *u = sub; u->isArray(); u = u->pointee()) subFlat *= u->length();
        const std::size_t open = peek().pos;
        expect("{");
        long long at = 0;
        while (!peek().is("}")) {
            if (at >= flat)
                src_.fail(peek().pos, "too many initialisers for '" + t->describe() +
                                      "' - it holds " + std::to_string(flat));
            if (sub->isArray() && peek().is("{")) {
                // a nested list starts the next row; one already begun is left to its defaults
                at = (at + subFlat - 1) / subFlat * subFlat;
                if (at >= flat)
                    src_.fail(peek().pos, "too many initialisers for '" + t->describe() + "'");
                readList(sub, base + at);
                at += subFlat;
            } else {
                if (static_cast<std::size_t>(base + at) >= inits.size())
                    inits.resize(static_cast<std::size_t>(base + at + 1));
                Init &one = inits[static_cast<std::size_t>(base + at)];
                one.given = true;
                std::vector<Temporary> before;
                before.swap(pendingTemps_);
                if (consume("{")) {
                    one.braced = true;
                    while (!peek().is("}")) {
                        one.args.push_back(assign());
                        if (!consume(",")) break;
                    }
                    expect("}");
                } else if (peek().kind == TokenKind::Ident && peekAt(1).is("(") &&
                           (peek().text == plain->localName() || peek().text == plain->tag()) &&
                           closesElement()) {
                    // `S(args)` builds the element itself, as clang does: direct-init, no copy.
                    one.direct = true;
                    at_ += 2;
                    while (!peek().is(")")) {
                        one.args.push_back(assign());
                        if (!consume(",")) break;
                    }
                    expect(")");
                } else {
                    one.args.push_back(assign());
                }
                // **Each element's initialiser is a full-expression of its own**: its
                // temporaries go with it and die before the next element is built.
                one.temps.swap(pendingTemps_);
                pendingTemps_.swap(before);
                at++;
            }
            if (!consume(",")) break;
        }
        (void)open;
        expect("}");
    };
    readList(d.type, 0);
    if (unbounded) {
        long long rowFlat = 1;
        for (const Type *u = d.type->pointee(); u->isArray(); u = u->pointee()) rowFlat *= u->length();
        const long long rows = (static_cast<long long>(inits.size()) + rowFlat - 1) / rowFlat;
        if (rows == 0)
            src_.fail(d.pos, "'" + d.name + "' takes its length from an empty list, and an "
                             "array of no elements is not C++");
        d.type = types_.arrayOf(d.type->pointee(), rows);
        total = rows * rowFlat;
        inits.resize(static_cast<std::size_t>(total));
        offset = declareLate();
    }

    // Each element in turn, as the object `S e = init;` - its temporaries gone before the next.
    Declared one = d;
    one.type = plain;
    std::vector<StmtPtr> all;
    long long firstDefault = total;
    for (long long i = total; i > 0 && !inits[static_cast<std::size_t>(i - 1)].given; i--)
        firstDefault = i - 1;
    // [except.ctor]/3: a local array counts the elements it has built, which an exception destroys.
    const bool counted = symbol.empty() && destructorOf(plain) != nullptr;
    const int builtSlot = counted ? allocateFrameSlot(types_.intType()) : 0;
    auto builtNow = [&](ExprPtr value) {
        ExprPtr slot(Var::local("$i", builtSlot));
        slot->setType(types_.intType());
        value->setType(types_.intType());
        ExprPtr set(new Assign(std::move(slot), std::move(value)));
        set->setType(types_.intType());
        return StmtPtr(new ExprStmt(std::move(set)));
    };
    if (counted) all.push_back(builtNow(ExprPtr(new Num(0LL))));
    for (long long i = 0; i < firstDefault; i++) {
        Init &e = inits[static_cast<std::size_t>(i)];
        const int at = symbol.empty() ? offset - static_cast<int>(i * size)
                                      : static_cast<int>(i * size);
        const bool empty = e.args.empty();
        pendingTemps_.insert(pendingTemps_.end(), e.temps.begin(), e.temps.end());
        // **A trivially copyable class is copied, not constructed** - there is no copy
        // constructor to resolve to, as `S s = t;` finds for one object.
        if (e.args.size() == 1 && !e.braced && !e.direct && classes_.copyConstructorOf(plain) == nullptr &&
            classes_.moveConstructorOf(plain) == nullptr && e.args[0]->type() != nullptr &&
            e.args[0]->type()->unqualified() == plain) {
            ExprPtr dst = objectAt(one, symbol, at);
            ExprPtr store(new Assign(std::move(dst), std::move(e.args[0])));
            store->setType(plain);
            all.push_back(StmtPtr(new ExprStmt(std::move(store))));
        } else {
            const bool copyInit = !e.direct && (!e.braced || !empty);
            if (copyInit) {
                const Signature chosen = overload_.resolveOverload(constructorKey(plain->tag()), e.args, d.pos);
                if (chosen.isExplicit)
                    src_.fail(d.pos, "element " + std::to_string(i) + " of '" + d.name + "' is "
                                     "copy-initialised from its initialiser, and the constructor of '" +
                                     plain->describe() + "' taking it is 'explicit' - write '" +
                                     plain->localName() + "(...)' in the list, which asks for it by name");
            }
            all.push_back(constructObject(one, symbol, at, std::move(e.args), copyInit, empty));
        }
        flushTemporaries(all);
        if (counted) all.push_back(builtNow(ExprPtr(new Num(i + 1))));
    }
    if (firstDefault < total) {
        const Signature *ctor = classes_.defaultConstructorOf(plain);
        if (ctor == nullptr)
            src_.fail(d.pos, "'" + d.name + "' has " + std::to_string(firstDefault) +
                             " initialisers for " + std::to_string(total) + " elements, and '" +
                             plain->describe() + "' has no constructor taking nothing to build "
                             "the rest");
        if (ctor->implicit) {
            // value-initialisation zeroes first where nobody wrote the constructor: one by one
            for (long long i = firstDefault; i < total; i++) {
                all.push_back(constructObject(one, symbol, symbol.empty()
                                                  ? offset - static_cast<int>(i * size)
                                                  : static_cast<int>(i * size),
                                              std::vector<ExprPtr>(), true, true));
                if (counted) all.push_back(builtNow(ExprPtr(new Num(i + 1))));
            }
        } else {
            markUsed(ctor);
            const Signature chosen = *ctor;
            std::vector<ExprPtr> defaults;
            applyDefaults(chosen, defaults, d.pos);
            const int indexSlot = allocateFrameSlot(types_.intType());
            ExprPtr first = objectAt(one, symbol, symbol.empty()
                ? offset - static_cast<int>(firstDefault * size)
                : static_cast<int>(firstDefault * size));
            ExprPtr start(new Unary('&', std::move(first)));
            start->setType(types_.pointerTo(plain));
            std::vector<ExprPtr> args;
            args.push_back(indexBytes(types_, std::move(start), plain, indexSlot, target_));
            std::vector<const Type *> ps;
            ps.push_back(types_.pointerTo(plain));
            for (std::size_t k = 0; k < defaults.size(); k++) {
                args.push_back(std::move(defaults[k]));
                ps.push_back(chosen.params[k]);
            }
            StmtPtr call(new ExprStmt(completeCall(plain->tag(), chosen.symbol, nullptr,
                                                   types_.get(Kind::Void), ps, false, d.pos,
                                                   std::move(args))));
            if (counted) {
                ExprPtr was(Var::local("$i", builtSlot));
                was->setType(types_.intType());
                ExprPtr step(new Num(1LL));
                step->setType(types_.intType());
                std::vector<StmtPtr> both;
                both.push_back(std::move(call));
                both.push_back(builtNow(ExprPtr(new Binary(BinOp::Add, std::move(was), std::move(step)))));
                call = StmtPtr(new Block(std::move(both)));
            }
            all.push_back(eachElement(indexSlot, total - firstDefault, std::move(call)));
        }
    }
    if (!counted) return StmtPtr(new Block(std::move(all)));
    Alive built{ d.name, offset, plain };
    built.builtSlot = builtSlot;
    return guardSubobject(StmtPtr(new Block(std::move(all))), built, d.pos);
}

// The loop functions: the first element's address and a count, a frame of
// their own, once per class and unit, file-local - `__cxx1_vec_new_<class>`
// and `_del_`, the class spelled as the Itanium mangler spells a nested one.
std::string Parser::vectorLoopName(const char *which, const Type *cls,
                                   std::size_t pos) {
    std::string spelt, why;
    if (!itaniumTypeSpelling(cls, &spelt, &why))
        src_.fail(pos, "an array of '" + cls->describe() + "' cannot be named: " + why);
    return std::string("__cxx1_vec_") + which + "_" + spelt;
}

ExprPtr Parser::callVectorLoop(const std::string &fn, const Type *cls,
                               ExprPtr base, ExprPtr count, std::size_t pos) {
    std::vector<const Type *> ps;
    ps.push_back(types_.pointerTo(cls));
    ps.push_back(types_.get(target_.sizeType()));
    std::vector<ExprPtr> args;
    args.push_back(std::move(base));
    args.push_back(convert(std::move(count), ps[1]));
    return completeCall(fn, fn, nullptr, types_.get(Kind::Void), ps, false,
                        pos, std::move(args));
}

std::string Parser::vectorConstructor(const Type *cls, std::size_t pos) {
    const std::string name = vectorLoopName("new", cls, pos);
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;

    const Signature *ctor = classes_.defaultConstructorOf(cls);
    if (ctor == nullptr)
        src_.fail(pos, "'" + cls->describe() + "' has constructors but none "
                       "that takes nothing, and an array of it has no way to "
                       "say what to pass");
    if (ctor->access != Access::Public && !insideAccessOf(cls, ctor->access) &&
        !isFriendOf(cls))
        src_.fail(pos, "'" + cls->describe() + "' has no public default "
                       "constructor, and an array of it needs one");
    markUsed(ctor);
    const Signature chosen = *ctor;

    const Type *ptr = types_.pointerTo(cls);
    const Type *sizeT = types_.get(target_.sizeType());
    const FrameSave savedFrame = openFrame();
    OwnPads own(*this);
    const int baseSlot = allocateFrameSlot(ptr);
    const int countSlot = allocateFrameSlot(sizeT);
    const int indexSlot = allocateFrameSlot(types_.intType());
    std::vector<Param> params;
    params.push_back(Param{ ptr, baseSlot });
    params.push_back(Param{ sizeT, countSlot });

    std::vector<ExprPtr> defaults;
    applyDefaults(chosen, defaults, pos);
    ExprPtr base(Var::local("base", baseSlot));
    base->setType(ptr);
    ExprPtr at = indexBytes(types_, std::move(base), cls, indexSlot, target_);
    std::vector<ExprPtr> args;
    args.push_back(std::move(at));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    for (std::size_t i = 0; i < defaults.size(); i++) {
        args.push_back(std::move(defaults[i]));
        ps.push_back(chosen.params[i]);
    }
    StmtPtr one(new ExprStmt(completeCall(cls->tag(), chosen.symbol, nullptr,
                                          types_.get(Kind::Void), ps, false,
                                          pos, std::move(args))));
    ExprPtr n(Var::local("n", countSlot));
    n->setType(sizeT);
    std::vector<StmtPtr> body;
    Alive built{ "base", baseSlot, cls };
    built.byAddress = true;
    body.push_back(guardElements(eachElement(indexSlot, convert(std::move(n), types_.intType()),
                                             std::move(one)),
                                 built, indexSlot, pos));
    body.push_back(StmtPtr(new Return(nullptr)));
    current_->functions.push_back(Function(name, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSynthesised("vector constructor");
    current_->functions.back().setSymbol(name);
    current_->functions.back().setHasLandingPads(own.made());
    closeFrame(savedFrame);
    return name;
}

// `i = n; while (i > 0) { i = i - 1; ~T(base + i); }` - last first, [class.dtor].
std::string Parser::vectorDestructor(const Type *cls, std::size_t pos) {
    const std::string name = vectorLoopName("del", cls, pos);
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;
    const Signature *dtor = destructorOf(cls);
    if (dtor == nullptr) return name;
    markUsed(dtor);

    const Type *ptr = types_.pointerTo(cls);
    const Type *sizeT = types_.get(target_.sizeType());
    const Type *idx = types_.intType();
    const FrameSave savedFrame = openFrame();
    const int baseSlot = allocateFrameSlot(ptr);
    const int countSlot = allocateFrameSlot(sizeT);
    const int indexSlot = allocateFrameSlot(idx);
    std::vector<Param> params;
    params.push_back(Param{ ptr, baseSlot });
    params.push_back(Param{ sizeT, countSlot });

    auto index = [&]() { ExprPtr e(Var::local("$i", indexSlot)); e->setType(idx); return e; };
    ExprPtr n(Var::local("n", countSlot));
    n->setType(sizeT);
    ExprPtr init(new Assign(index(), convert(std::move(n), idx)));
    init->setType(idx);
    ExprPtr zero(new Num(0LL));
    zero->setType(idx);
    ExprPtr cond(new Binary(BinOp::Gt, index(), std::move(zero)));
    cond->setType(idx);
    ExprPtr one(new Num(1LL));
    one->setType(idx);
    ExprPtr less(new Binary(BinOp::Sub, index(), std::move(one)));
    less->setType(idx);
    ExprPtr step(new Assign(index(), std::move(less)));
    step->setType(idx);
    ExprPtr base(Var::local("base", baseSlot));
    base->setType(ptr);
    ExprPtr at = indexBytes(types_, std::move(base), cls, indexSlot, target_);
    std::vector<StmtPtr> inner;
    inner.push_back(StmtPtr(new ExprStmt(std::move(step))));
    inner.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(at), *dtor, pos))));
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new ExprStmt(std::move(init))));
    body.push_back(StmtPtr(new While(std::move(cond), StmtPtr(new Block(std::move(inner))))));
    body.push_back(StmtPtr(new Return(nullptr)));
    current_->functions.push_back(Function(name, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSynthesised("vector destructor");
    current_->functions.back().setSymbol(name);
    closeFrame(savedFrame);
    return name;
}

// [expr.new]/12 as the Itanium ABI fixes it: a class delete[] must destroy
// keeps its count in the last size_t of a cookie in front of the array, as
// wide as the larger of size_t and the element's alignment; else none.
int Parser::arrayCookie(const Type *elem) const {
    if (destructorOf(elem->unqualified()) == nullptr) return 0;
    const int sizeT = types_.get(target_.sizeType())->size(target_);
    const int words = target_.armArrayCookie() ? 2 * sizeT : sizeT;
    const int align = elem->align(target_);
    return align > words ? align : words;
}

StmtPtr Parser::baseDestructorCall(const Type *cls, std::size_t which, int thisSlot,
                                   std::size_t pos) {
    const Type::BaseSpec &spec = cls->bases()[which];
    const Type *base = spec.type;
    const Signature *dtor = destructorOf(base);
    markUsed(dtor);
    std::string symbol = dtor->symbol;
    if (!target_.microsoftNames()) itaniumDestructorName(base->tag(), base, false, &symbol);
    const Type *basePtr = types_.pointerTo(base);
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(types_.pointerTo(cls));
    // A virtual base is destroyed this way only by the complete object's constructor, whose layout is fixed.
    if (spec.isVirtual && spec.offset != 0) {
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        ExprPtr asChars(new Cast(chars, std::move(me)));
        asChars->setType(chars);
        ExprPtr step(new Num(static_cast<long long>(spec.offset)));
        step->setType(types_.get(Kind::LongLong));
        me = ExprPtr(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
        me->setType(chars);
        me = ExprPtr(new Cast(basePtr, std::move(me)));
    } else {
        me = spec.offset == 0 ? ExprPtr(new Cast(basePtr, std::move(me)))
                              : convert(std::move(me), basePtr);
    }
    me->setType(basePtr);
    std::vector<ExprPtr> args;
    args.push_back(std::move(me));
    std::vector<const Type *> params;
    params.push_back(basePtr);
    if (takesVtt(base)) {
        args.push_back(spec.isVirtual ? classes_.vttOfClass(cls, vtts_[cls->tag()].virtualVtt[base])
                                      : classes_.vttForBase(cls, base));
        params.push_back(vttType());
    }
    return StmtPtr(new ExprStmt(completeCall(base->tag(), symbol, nullptr, types_.get(Kind::Void),
                                             params, false, pos, std::move(args), false, 0)));
}

// [except.ctor]/2: a delegating constructor's object is complete once its target returns.
StmtPtr Parser::selfDestructorCall(const Type *cls, int thisSlot, std::size_t pos) {
    const Signature *dtor = destructorOf(cls);
    markUsed(dtor);
    std::string symbol = dtor->symbol;
    if (!target_.microsoftNames()) itaniumDestructorName(cls->tag(), cls, false, &symbol);
    const Type *self = types_.pointerTo(cls);
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    std::vector<ExprPtr> args;
    args.push_back(std::move(me));
    const std::vector<const Type *> params(1, self);
    return StmtPtr(new ExprStmt(completeCall(cls->tag(), symbol, nullptr, types_.get(Kind::Void),
                                             params, false, pos, std::move(args), false, 0)));
}

StmtPtr Parser::guardConstruction(std::vector<StmtPtr> steps, std::vector<Alive> done,
                                  std::size_t pos, bool *guarded) {
    OwnPads own(*this);
    done.resize(steps.size());
    std::vector<StmtPtr> rest;
    for (std::size_t i = steps.size(); i-- > 0; ) {
        if (done[i].owner != nullptr && !rest.empty()) {
            Block *after = new Block(std::move(rest));
            after->setScope(-1);
            rest.clear();
            rest.push_back(guardSubobject(StmtPtr(after), done[i], pos));
            *guarded = true;
        }
        rest.insert(rest.begin(), std::move(steps[i]));
    }
    Block *b = new Block(std::move(rest));
    b->setScope(-1);
    return StmtPtr(b);
}

StmtPtr Parser::guardElements(StmtPtr loop, Alive a, int indexSlot, std::size_t pos) {
    if (destructorOf(a.cls) == nullptr) return loop;
    a.builtSlot = indexSlot;
    return guardSubobject(std::move(loop), a, pos);
}

StmtPtr Parser::withVirtualBases(std::vector<StmtPtr> init, const std::vector<Alive> &vbs, StmtPtr rest,
                                 std::size_t pos) {
    if (init.empty()) return rest;
    std::vector<StmtPtr> guarded;
    for (std::size_t i = vbs.empty() ? 0 : 1; i < init.size(); i++) guarded.push_back(std::move(init[i]));
    guarded.push_back(std::move(rest));
    std::vector<StmtPtr> all;
    if (!vbs.empty()) all.push_back(std::move(init[0]));
    all.push_back(guardSubobjects(StmtPtr(new Block(std::move(guarded))), vbs, pos));
    Block *b = new Block(std::move(all));
    b->setScope(-1);
    return StmtPtr(b);
}

StmtPtr Parser::guardSubobjects(StmtPtr rest, const std::vector<Alive> &as, std::size_t pos) {
    if (as.empty()) return rest;
    for (const Alive &a : as) alive_.push_back(a);
    std::vector<StmtPtr> one;
    one.push_back(std::move(rest));
    const std::vector<std::pair<std::size_t, std::size_t> > built(
        1, std::make_pair(std::size_t(0), alive_.size()));
    std::vector<StmtPtr> out = target_.microsoftNames()
        ? wrapMsCleanups(std::move(one), built, alive_.size() - as.size(), pos)
        : wrapCleanups(std::move(one), built, alive_.size() - as.size(), pos);
    alive_.resize(alive_.size() - as.size());
    Block *b = new Block(std::move(out));
    b->setScope(-1);
    return StmtPtr(b);
}

StmtPtr Parser::guardSubobject(StmtPtr rest, const Alive &a, std::size_t pos,
                               const std::vector<Alive> &before) {
    for (const Alive &b : before) alive_.push_back(b);
    alive_.push_back(a);
    std::vector<StmtPtr> one;
    one.push_back(std::move(rest));
    const std::vector<std::pair<std::size_t, std::size_t> > built(
        1, std::make_pair(std::size_t(0), alive_.size()));
    std::vector<StmtPtr> out = target_.microsoftNames()
        ? wrapMsCleanups(std::move(one), built, alive_.size() - 1, pos)
        : wrapCleanups(std::move(one), built, alive_.size() - 1, pos);
    alive_.resize(alive_.size() - 1 - before.size());
    Block *b = new Block(std::move(out));
    b->setScope(-1);
    return StmtPtr(b);
}

StmtPtr Parser::eachElement(int indexSlot, long long count, StmtPtr one) {
    ExprPtr n(new Num(count));
    n->setType(types_.intType());
    return eachElement(indexSlot, std::move(n), std::move(one));
}

StmtPtr Parser::eachElement(int indexSlot, ExprPtr n, StmtPtr one) {
    const Type *idx = types_.intType();

    ExprPtr i0(Var::local("$i", indexSlot));
    i0->setType(idx);
    ExprPtr zero(new Num(0LL));
    zero->setType(idx);
    ExprPtr init(new Assign(std::move(i0), std::move(zero)));
    init->setType(idx);

    ExprPtr i1(Var::local("$i", indexSlot));
    i1->setType(idx);
    ExprPtr cond(new Binary(BinOp::Lt, std::move(i1), std::move(n)));
    cond->setType(idx);

    ExprPtr i2(Var::local("$i", indexSlot));
    i2->setType(idx);
    ExprPtr step1(new Num(1LL));
    step1->setType(idx);
    ExprPtr sum(new Binary(BinOp::Add, std::move(i2), std::move(step1)));
    sum->setType(idx);
    ExprPtr i3(Var::local("$i", indexSlot));
    i3->setType(idx);
    ExprPtr step(new Assign(std::move(i3), std::move(sum)));
    step->setType(idx);

    std::vector<StmtPtr> inner;
    inner.push_back(std::move(one));
    inner.push_back(StmtPtr(new ExprStmt(std::move(step))));

    std::vector<StmtPtr> all;
    all.push_back(StmtPtr(new ExprStmt(std::move(init))));
    all.push_back(StmtPtr(new While(std::move(cond),
                                    StmtPtr(new Block(std::move(inner))))));
    return StmtPtr(new Block(std::move(all)));
}


// **A candidate instantiated and never chosen has no body in clang's or cl's object**, [temp.inst]/3 -
// a member template's is replayed as it is formed, so what was emitted for one is taken back here.
void Parser::pruneUnchosenCandidates(Program &program) {
    for (std::size_t k = 0; k < candidateSpecializations_.size(); k++) {
        const Signature &f = lookup_.functions_[candidateSpecializations_[k]];
        if (f.used && !f.bodyFailure.empty()) src_.fail(f.bodyFailurePos, f.bodyFailure);
        if (f.used) continue;
        for (std::size_t i = program.functions.size(); i-- > 0; )
            if (program.functions[i].symbol() == f.symbol)
                program.functions.erase(program.functions.begin() + static_cast<long>(i));
    }
}

namespace {

// Every symbol a body names: a call, and a variable or function used as a value.
class SymbolsNamed final : public Visitor {
public:
    std::vector<std::string> *out;
    explicit SymbolsNamed(std::vector<std::string> *o) : out(o) {}
    void visit(const Num &) override {}
    void visit(const Var &n) override { out->push_back(n.symbol()); }
    void visit(const StrLit &e) override { out->push_back(e.label()); }
    void visit(const Goto &) override {}
    void visit(const Break &) override {}
    void visit(const Continue &) override {}
    void visit(const FuncletLeave &) override {}
    void visit(const Call &n) override {
        out->push_back(n.symbol());
        if (n.callee() != nullptr) n.callee()->accept(*this);
        for (const ExprPtr &a : n.args()) a->accept(*this);
    }
    void visit(const VaStart &n) override { n.list().accept(*this); }
    void visit(const VaArg &n) override { n.list().accept(*this); }
    void visit(const Assign &n) override { n.target().accept(*this); n.value().accept(*this); }
    void visit(const Unary &n) override { n.operand().accept(*this); }
    void visit(const Binary &n) override { n.lhs().accept(*this); n.rhs().accept(*this); }
    void visit(const Postfix &n) override { n.target().accept(*this); }
    void visit(const Cast &n) override { n.value().accept(*this); }
    void visit(const Comma &n) override { n.left().accept(*this); n.right().accept(*this); }
    void visit(const Conditional &n) override {
        n.cond().accept(*this); n.thenArm().accept(*this); n.elseArm().accept(*this);
    }
    void visit(const MemberAccess &n) override { n.object().accept(*this); }
    void visit(const ExprStmt &n) override { n.expr().accept(*this); }
    void visit(const Return &n) override { if (n.hasValue()) n.value().accept(*this); }
    void visit(const Block &n) override { for (const StmtPtr &s : n.body()) s->accept(*this); }
    void visit(const If &n) override {
        n.cond().accept(*this); n.thenArm().accept(*this);
        if (n.elseArm() != nullptr) n.elseArm()->accept(*this);
    }
    void visit(const While &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const DoWhile &n) override { n.body().accept(*this); n.cond().accept(*this); }
    void visit(const For &n) override {
        if (n.init() != nullptr) n.init()->accept(*this);
        if (n.cond() != nullptr) n.cond()->accept(*this);
        if (n.step() != nullptr) n.step()->accept(*this);
        n.body().accept(*this);
    }
    void visit(const Switch &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const Case &n) override { n.body().accept(*this); }
    void visit(const Label &n) override { n.body().accept(*this); }
    void visit(const Try &n) override {
        for (const std::string &t : n.types()) out->push_back(t);
        for (const StmtPtr &s : n.body()) s->accept(*this);
        if (n.hasPad()) n.pad().accept(*this);
        if (n.cleanup() != nullptr) n.cleanup()->accept(*this);
        for (const MsHandler &h : n.handlers())
            if (h.body != nullptr) h.body->accept(*this);
    }
};

}

// **An inline function, or a vtable group with no key function, nothing here uses is not emitted** -
// [basic.def.odr]/3, and what clang, g++ and cl do. Reached from every function and global not held;
// a symbol is dropped only if it was held and nothing reachable names it or its alias.
void Parser::pruneUnusedInline(Program &program) {
    for (std::size_t i = 0; i < lookup_.functions_.size(); i++)
        if (lookup_.functions_[i].fromTemplate) discardableInline_.erase(lookup_.functions_[i].symbol);
    // What the compiler writes is inline by [class.copy] and [class.dtor]: reached, or not emitted.
    std::set<std::string> fromTemplate;
    for (std::size_t i = 0; i < lookup_.functions_.size(); i++)
        if (lookup_.functions_[i].fromTemplate) fromTemplate.insert(lookup_.functions_[i].symbol);
    std::set<std::string> held = discardableInline_;
    for (const Function &f : program.functions)
        if (f.isInline() && fromTemplate.count(f.symbol()) == 0) held.insert(f.symbol());
    // A class whose key function is defined here owns its group in this unit, built or not.
    std::vector<std::string> keyed;
    if (!target_.microsoftNames())
        for (std::map<std::string, std::vector<std::string> >::const_iterator it =
                 classSymbols_.begin(); it != classSymbols_.end(); ++it)
            if (classes_.hasKeyFunction(it->first))
                for (const std::string &sym : it->second) keyed.push_back(sym.substr(4));
    auto heldGlobal = [&](const Global &g) {
        if (!g.isInline) return false;
        const std::string &n = g.symbol;
        if (target_.microsoftNames()) return n.compare(0, 4, "??_7") == 0;
        if (n.compare(0, 4, "_ZTV") != 0 && n.compare(0, 4, "_ZTI") != 0 &&
            n.compare(0, 4, "_ZTS") != 0 && n.compare(0, 4, "_ZTT") != 0 &&
            n.compare(0, 4, "_ZTC") != 0)
            return false;
        for (const std::string &k : keyed)         // _ZTC names its layout class first
            if (n.compare(4, k.size(), k) == 0) return false;
        return true;
    };
    if (held.empty()) {
        bool any = false;
        for (const Global &g : program.globals) any = any || heldGlobal(g);
        if (!any) return;
    }
    // A node is a function (its index) or a global (its index past the functions).
    const std::size_t nf = program.functions.size();
    std::map<std::string, std::size_t> bySymbol;
    for (std::size_t i = 0; i < nf; i++) {
        bySymbol[program.functions[i].symbol()] = i;
        if (!program.functions[i].alias().empty()) bySymbol[program.functions[i].alias()] = i;
    }
    for (std::size_t i = 0; i < program.globals.size(); i++)
        bySymbol[program.globals[i].symbol] = nf + i;
    std::vector<bool> reached(nf + program.globals.size(), false);
    std::set<std::string> everyName;
    std::vector<std::string> named;
    named.push_back(program.initFunction);
    for (const MicrosoftThrow &t : program.msThrows) {
        if (!t.thrown) continue;         // a type only caught needs its descriptor alone
        named.push_back(t.destructor);
        for (const MicrosoftThrow::Catchable &c : t.catchables) named.push_back(c.copyCtor);
    }
    std::vector<std::size_t> work;
    for (std::size_t i = 0; i < nf; i++)
        if (held.count(program.functions[i].symbol()) == 0) {
            reached[i] = true;
            work.push_back(i);
        }
    for (std::size_t i = 0; i < program.globals.size(); i++)
        if (!heldGlobal(program.globals[i])) {
            reached[nf + i] = true;
            work.push_back(nf + i);
        }
    for (;;) {
        for (const std::string &s : named) {
            everyName.insert(s);
            const std::map<std::string, std::size_t>::const_iterator f = bySymbol.find(s);
            if (f != bySymbol.end() && !reached[f->second]) {
                reached[f->second] = true;
                work.push_back(f->second);
            }
        }
        named.clear();
        if (work.empty()) break;
        const std::size_t i = work.back();
        work.pop_back();
        if (i < nf) {
            SymbolsNamed collect(&named);
            program.functions[i].body().accept(collect);
        } else {
            const Global &g = program.globals[i - nf];
            named.push_back(g.prefixWord);
            for (const GlobalPiece &p : g.init) named.push_back(p.symbol);
        }
    }
    std::vector<Function> kept;
    for (std::size_t i = 0; i < nf; i++)
        if (reached[i]) kept.push_back(std::move(program.functions[i]));
    program.functions.swap(kept);
    std::vector<Global> keptGlobals;
    for (std::size_t i = 0; i < program.globals.size(); i++)
        if (reached[nf + i]) keptGlobals.push_back(std::move(program.globals[i]));
    program.globals.swap(keptGlobals);
    // A literal only a pruned body used goes with it; what is kept names every literal it reads.
    std::vector<StringLit> keptStrings;
    for (StringLit &sl : program.strings)
        if (everyName.count(sl.label) != 0) keptStrings.push_back(std::move(sl));
    program.strings.swap(keptStrings);
    // A Microsoft class keeps its five records where its vftable stayed or something names one.
    std::vector<const Type *> rtti;
    for (const Type *cls : program.rtti) {
        MicrosoftRtti n;
        std::string why;
        if (!microsoftClassRttiNames(cls, &n, &why) || everyName.count(n.locator) ||
            everyName.count(n.descriptor) || everyName.count(n.baseDescriptor) ||
            everyName.count(n.array) || everyName.count(n.hierarchy))
            rtti.push_back(cls);
    }
    program.rtti.swap(rtti);
}

// `static int total;` inside a class: one object shared by every object of the class,
// taking no room in any of them, so nothing here touches the layout. What it needs is
// a name the linker can hold and a definition outside the class to go with it.
void Parser::declareStaticMember(const std::string &cls, Type *owner,
                                 const Declared &d, Access access,
                                 bool volatileWritten, bool threadLocal, bool isConstexpr) {
    if (cls.empty())
        src_.fail(d.pos, "a static member needs a class with a name - this one "
                         "is anonymous");
    if (owner->findMember(d.name) != nullptr)
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' is a static member and "
                         "an ordinary one, and it can only be one of them");
    for (const Type::StaticMember &had : owner->staticMembers())
        if (had.name == d.name)
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is declared twice");

    Type::StaticMember s;
    s.name = d.name;
    s.type = isConstexpr ? constexprObject(d.type) : d.type;
    s.access = access;
    s.isThreadLocal = threadLocal;

    // **[class.static.data]/3: a `static constexpr` member of any literal type is initialised here.**
    if (isConstexpr && !d.type->isInteger() && (peek().is("=") || peek().is("{"))) {
        refuseVolatileWithLinkage(volatileWritten, false, d.pos);
        s.symbol = linkage_.staticMemberSymbol(cls, d.name, s.type, access, d.pos);
        owner->addStaticMember(s);
        Declared typed = d;
        typed.type = s.type;
        defineConstexprStaticMember(cls + "::" + d.name, s.symbol, typed);
        return;
    }
    // **`static const int k = 5;` written in the class needs no definition**, measured
    // rather than assumed: cl emits no symbol for one and folds the value in wherever
    // it is read. Anything else with an initialiser here is refused.
    if (consume("=")) {
        if (!d.type->isConst() || !d.type->isInteger())
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is initialised "
                             "inside the class, and only a 'static const' of "
                             "integer type may be - write the value on the "
                             "definition outside the class instead");
        s.folded = true;
        s.value = constantExpression("a static member's value");
    } else if (d.type->isArray() && d.type->length() < 0) {
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' has no length, and a "
                         "static member cannot take one from its definition - "
                         "the class is what says how big it is");
    }

    // A static member is one object for the whole program, so it has a name
    // outside this file whatever else it is.
    refuseVolatileWithLinkage(volatileWritten, false, d.pos);
    s.symbol = linkage_.staticMemberSymbol(cls, d.name, d.type, access, d.pos);
    owner->addStaticMember(s);
}

// [dcl.constexpr]/9: a constexpr object is const - for a pointer, the pointer itself, which the
// const on the base type does not reach: `constexpr const char *p` is `const char *const` (M1 L6).
const Type *Parser::constexprObject(const Type *t) {
    if (t->isPointer() && !t->isConst()) return types_.withConst(t);
    if (t->isArray()) {
        const Type *e = constexprObject(t->pointee());
        return e == t->pointee() ? t : types_.arrayOf(e, t->length());
    }
    return t;
}

// [namespace.udecl]/15: each base overload a using-declaration names joins this class's set, unless this
// class declares one with the same parameters and qualifiers - which hides it.
void Parser::declareUsingMembers(const std::string &tag, const Type *type) {
    (void)type;
    auto it = usingMembers_.find(tag);
    if (it == usingMembers_.end()) return;
    for (const std::pair<const Type *, std::string> &u : it->second) {
        const std::vector<std::size_t> from = *lookup_.overloadsOf(u.first->tag() + "::" + u.second);
        std::vector<std::size_t> &mine = lookup_.functionIndex_[tag + "::" + u.second];
        const std::vector<std::size_t> own = mine;
        for (std::size_t i : from) {
            bool hidden = false;
            for (std::size_t k : own)
                if (classes_.sameParameters(lookup_.functions_[k].params, lookup_.functions_[i].params) &&
                    lookup_.functions_[k].constThis == lookup_.functions_[i].constThis && lookup_.functions_[k].refThis == lookup_.functions_[i].refThis)
                    hidden = true;
            if (!hidden) mine.push_back(i);
        }
    }
}

// The value folded where the class declares it, and laid down there as a weak definition: a constant
// expression reads the value, and a read at run time the object, defined or not outside the class.
void Parser::defineConstexprStaticMember(const std::string &full, const std::string &symbol, const Declared &d) {
    inClassConstexprStatics_.insert(symbol);
    std::vector<GlobalPiece> pieces;
    // A template's own pattern has nothing to fold; each specialization reads it again.
    if (patternOnly_) {
        for (int depth = 0; !(depth == 0 && (peek().is(";") || peek().is(","))); at_++) {
            if (peek().is("(") || peek().is("{") || peek().is("[")) depth++;
            if (peek().is(")") || peek().is("}") || peek().is("]")) depth--;
        }
        return;
    }
    const Type *cls = d.type->unqualified();
    if (cls->isStructOrUnion() && !cls->tag().empty() && hasConstructors(cls->tag())) {
        Declared m = d;
        m.name = full;
        dynamicInitialise(m, symbol, std::string(), false, &pieces);
    } else {
        consume("=");
        Init in = parseInitialiser();
        ConstObject value;
        StaticConst rec;
        if (d.type->isFloating() && !in.isList && in.value != nullptr && constant_.foldFloating(*in.value, &rec.dvalue)) {
            rec.dknown = true;
            staticConsts_[symbol] = rec;
            flattenScalar(d.type, in, 0, pieces);
        } else if (cls->isStructOrUnion() && constant_.constObjectFromInit(d.type, in, 0, &value)) {
            constant_.constObjects_[symbol] = value;
            constant_.constObjectPieces(value, pieces);
        } else if (addressConstant(d.type, in)) {
            std::string sym;
            long long off = 0;
            if (foldAddress(*in.value, &sym, &off)) constAddresses_[full] = std::make_pair(sym, off);
            flattenInit(d.type, in, 0, pieces);
        } else {
            constant_.refuseConstexprClassObject(full, d.pos);
        }
    }
    current_->globals.push_back(Global{ full, symbol, d.type, pieces, true, false, true });
    current_->globals.back().isInline = true;
}

// `int Counter::total = 0;` at file scope - the definition the declaration inside the
// class asked for. An ordinary global the class gave its name to, so all this adds is
// finding which member it is and taking the symbol from it.
void Parser::defineStaticMember(Declared &d, Program &program, bool threadLocal) {
    const Type *owner = lookup_.findTypedef(d.qualifier);
    if (owner == nullptr || !owner->isStructOrUnion())
        src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
    const Type::StaticMember *s = owner->findStaticMember(d.name);
    if (s == nullptr)
        src_.fail(d.pos, "'" + d.qualifier + "' declares no static member '" +
                         d.name + "'");
    // [dcl.stc]/4 wants `thread_local` on the definition as on the declaration; clang's words.
    if (threadLocal != s->isThreadLocal)
        src_.fail(d.pos, std::string(threadLocal ? "thread-local" : "non-thread-local") + " declaration of '" +
                         d.name + "' follows " + (threadLocal ? "non-thread-local" : "thread-local") + " declaration");
    // What the member's definition adds to the unit: what initialises it, and that it is here.
    const ThreadLocalObject tlo(this, threadLocal);
    if (threadLocal) tlsDefining(s->symbol, true);
    const StaticMemberScope scope(this, owner);
    // [class.static.data]/3: `constexpr P S::k;` defines what the class initialised, already laid down.
    if (inClassConstexprStatics_.count(s->symbol)) {
        if (!peek().is(";"))
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' was initialised inside the class, so "
                             "its definition outside it takes no initialiser");
        expect(";");
        return;
    }
    if (s->type->unqualified() != d.type->unqualified() ||
        s->type->isConst() != d.type->isConst())
        src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' was declared '" +
                         s->type->describe() + "' and this defines it as '" +
                         d.type->describe() + "'");
    for (const Global &g : program.globals)
        if (g.symbol == s->symbol)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is defined "
                             "twice");

    // **A static member of class type is built before main** - [basic.start.init]
    // - in the init function, under a guard where the class is a specialization
    // so a second translation unit's copy does not build it twice.
    if (const Type *cls = memberClass(s->type))
        if (!cls->tag().empty() &&
            hasConstructors(cls->tag())) {
            const std::string full = d.qualifier + "::" + d.name;
            if (s->type->isArray())
                src_.fail(d.pos, "'" + full + "' is a static member array of '" +
                                 cls->tag() + "', which has a constructor - an "
                                 "array with static storage duration whose "
                                 "elements have a constructor is not "
                                 "supported yet");
            Declared m = d;
            m.name = full;
            m.type = s->type;
            const std::string helper = target_.microsoftNames()
                ? linkage_.atexitHelperName(s->symbol + "@") : std::string();
            dynamicInitialise(m, s->symbol, helper, owner->isSpecialization());
            expect(";");
            // Not isConst whatever the member says: the constructor writes it.
            program.globals.push_back(Global{ full, s->symbol, s->type,
                                              std::vector<GlobalPiece>(), false,
                                              false, false });
            program.globals.back().isInline = owner->isSpecialization();
            program.globals.back().isThreadLocal = threadLocal;
            if (threadLocal) tlsDynamic_.insert(s->symbol);
            return;
        }

    std::vector<GlobalPiece> pieces;
    bool hasInit = false, stored = false;
    // **A static reference member is a slot holding an address**, bound as one at
    // namespace scope is: a global's address in the image, anything else before main.
    if (s->type->isReference()) {
        Declared m = d;
        m.name = d.qualifier + "::" + d.name;
        m.type = s->type;
        const FunctionState outer = enterInitFunction();
        bindStaticReference(m, s->symbol, pieces, hasInit, &initSink());
        leaveInitFunction(outer);
        expect(";");
        program.globals.push_back(Global{ m.name, s->symbol,
                                          types_.pointerTo(s->type->referent()),
                                          std::move(pieces), hasInit, false, false });
        program.globals.back().isInline = owner->isSpecialization();
        program.globals.back().isThreadLocal = threadLocal;
        if (threadLocal && !hasInit) tlsDynamic_.insert(s->symbol);
        return;
    }
    if (consume("=") || atBracedInitialiser(d.name)) {
        Init in = parseInitialiser();
        // Read while the initialiser tree is still in scope, as the
        // namespace-scope path does: flattenInit answers in bytes rather than
        // in the value the read-back wants.
        if (s->type->isConst()) {
            StaticConst rec;
            long long iv = 0;
            if (constant_.constantInitialiser(s->type, in, &iv)) {
                rec.known = true; rec.value = iv;
            }
            if (s->type->isFloating() && !in.isList && in.value != nullptr) {
                bool p53 = false, x87 = false;
                long double dv = 0;
                if (constant_.foldDouble(*in.value, target_, &dv, &p53, &x87)) {
                    rec.dknown = true; rec.dvalue = dv;
                }
            }
            if (rec.known || rec.dknown) staticConsts_[s->symbol] = rec;
        }
        // `T Tm<T>::st = T();` with T a trivial class, or an initialiser that
        // does not fold: stored before main, as the namespace-scope path does
        // (A13). A template's member is stored under its once-guard.
        if (staticallyInitialisable(s->type, in)) {
            flattenInit(s->type, in, 0, pieces);
            hasInit = true;
        } else {
            dynamicInitialiseStaticMember(d.qualifier + "::" + d.name, s->symbol,
                                          s->type, in, owner->isSpecialization());
            stored = true;
        }
    } else if (s->folded) {
        // `const int Outer::K;` defines what the class initialised, [class.static.data]/3, so the
        // object holds the declaration's value - `&Outer::K` read a zero (1.27, 2026-10-21, review
        // of 1.25, item 3).
        Init in;
        in.pos = d.pos;
        in.value.reset(new Num(s->value));
        in.value->setType(s->type->unqualified());
        flattenInit(s->type, in, 0, pieces);
        hasInit = true;
    }
    expect(";");

    program.globals.push_back(Global{ d.qualifier + "::" + d.name, s->symbol,
                                      s->type, std::move(pieces), hasInit, false,
                                      s->type->isConst() && !stored });
    // A template's static member is defined by every unit that uses it, and
    // the linker keeps one - a weak object, as its vtable is.
    program.globals.back().isInline = owner->isSpecialization();
    program.globals.back().isThreadLocal = threadLocal;
    if (threadLocal && stored) tlsDynamic_.insert(s->symbol);
}

// [expr.prim.general]/13: nothing reads it, so an lvalue of the member's type at no
// address answers every question sizeof and decltype may ask - `*(T *)0`, never emitted.
ExprPtr Parser::memberWithoutObject(const Type *cls, const Member &m, std::size_t pos) {
    checkAccessible(cls, m, pos);
    if (m.width != 0)
        src_.fail(pos, "sizeof cannot be applied to '" + m.name + "', which is a bit-field");
    const Type *t = m.type->isReference() ? m.type->referent() : m.type;
    ExprPtr zero(new Num(0LL));
    zero->setType(types_.intType());
    ExprPtr ptr(new Cast(types_.pointerTo(t), std::move(zero)));
    ptr->setType(types_.pointerTo(t));
    ExprPtr deref(new Unary('*', std::move(ptr)));
    deref->setType(t);
    return resolve(std::move(deref), resolvedAs(Resolved::Member, m.type, m.name, m.offset, 0, m.declaredIn));
}

// `S::x`, `Outer::Inner::y`, `n::S::q`: the longest prefix naming a class that has
// the data member wins, as the static member walk in primary() takes it.
ExprPtr Parser::unevaluatedMember() {
    std::string q = peek().text;
    const Type *owner = nullptr;
    const Member *found = nullptr;
    std::size_t consumed = 0;
    for (std::size_t k = 1; peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident;
         k += 2) {
        if (const Type *cls = lookup_.findTypedef(q))
            if (cls->isStructOrUnion())
                if (const Member *m = cls->findMember(peekAt(k + 1).text)) {
                    owner = cls;
                    found = m;
                    consumed = k + 2;
                }
        q += "::" + peekAt(k + 1).text;
    }
    if (found == nullptr || peekAt(consumed).is("(")) return nullptr;
    const std::size_t pos = peekAt(consumed - 1).pos;
    at_ += consumed;
    return memberWithoutObject(owner, *found, pos);
}

// Naming a static member, however it was reached. A folded one is its value
// and has no storage at all; every other is the one global the class named.
ExprPtr Parser::staticMemberRef(const Type *owner, const Type::StaticMember &s,
                                const std::string &cls, std::size_t pos) {
    if (s.access != Access::Public && !insideAccessOf(owner, s.access) &&
        !isFriendOf(owner))
        src_.fail(pos, "'" + cls + "::" + s.name + "' is " +
                       (s.access == Access::Private ? "private" : "protected"));
    if (s.folded) {
        ExprPtr n(new Num(s.value));
        n->setType(s.type);
        return resolve(std::move(n), resolvedAs(Resolved::StaticMember, s.type, s.symbol, 1, s.value, owner));
    }
    if (unevaluated_ == 0) usedStaticMembers_.insert(s.symbol);
    Var *v = Var::global(cls + "::" + s.name);
    v->setSymbol(s.symbol);
    // Through the wrapper unless this unit has already defined it with a constant initialiser - a
    // definition still to come, in this unit or another, may build it dynamically.
    if (s.isThreadLocal) tlsVar(v, tlsDynamic_.count(s.symbol) != 0 || tlsDefined_.count(s.symbol) == 0, true);
    ExprPtr n(v);
    n->setType(s.type);
    Resolved r = resolvedAs(Resolved::StaticMember, s.type, s.symbol, -1, 0, owner);
    r.isThreadLocal = s.isThreadLocal;
    return resolve(useReference(std::move(n)), r);
}

// A member function declaration, keyed under "Class::name" in the one table
// every function lives in.
void Parser::checkNotAbstract(const Type *t, std::size_t pos,
                              const std::string &what) {
    const Type *c = t;
    while (c != nullptr && c->isArray()) c = c->pointee();
    if (c == nullptr || !c->isStructOrUnion() || !c->abstract()) return;
    std::string why = what + " is '" + c->describe() + "', which is abstract - ";
    const std::vector<VSlot> &slots = classes_.vtables_[c->tag()];
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].pure) {
            why += "'" + slots[i].name + "' is pure and nothing has overridden "
                   "it, so an object of this class would have a slot with no "
                   "function in it";
            src_.fail(pos, why);
        }
    src_.fail(pos, why + "it has a pure virtual nothing has overridden");
}

void Parser::declareMember(const std::string &cls, const Declared &d,
                           bool constThis, RefQual refThis, Access access, bool inUnion,
                           bool isVirtual, bool isStatic, bool isPure,
                           VirtSpecifiers virt) {
    if (inUnion)
        src_.fail(d.pos, "a member function of a union is not supported yet");

    const Type *fn = d.type;
    checkOperatorDeclarable(d.name, fn->params(), true, d.pos);
    // `explicit operator bool()`: the keyword was read in the class body and reached no Signature,
    // so `bool b = f` took the function as [class.conv.fct]/2 forbids (1.28, 2026-10-22, review of
    // 1.25, neg07). Taken here, where the member's Signature is made.
    const bool explicitConversion = pendingExplicitConversion_ && isConversionName(d.name);
    pendingExplicitConversion_ = false;

    std::string key = cls + "::" + d.name;
    std::vector<std::size_t> &set = lookup_.functionIndex_[key];

    const std::vector<const Type *> &params = fn->params();
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = lookup_.functions_[set[k]];
        if (!classes_.sameParameters(f.params, params)) continue;
        if (f.constThis == constThis && f.refThis == refThis)
            src_.fail(d.pos, "'" + key + "' is declared twice");
        if (mixesRefQualifier(f.refThis, refThis))
            src_.fail(d.pos, overload_.refQualifierMixRefusal(refThis, f.refThis));
    }

    if (inUnion && isVirtual)
        src_.fail(d.pos, "a union cannot have a virtual function");

    // **A static member takes no slot and overrides nothing.**
    if (isStatic) {
        const std::string sym = linkage_.memberSymbol(cls, d.name, fn, access, false,
                                             RefQual::None, d.pos, false, true);
        if (!pendingDefaults_.empty()) {
            defaultArgs_[sym] = pendingDefaults_;
            defaultArgNamespace_[sym] = namespaceStack_;
        }
        pendingDefaults_.clear();
        set.push_back(lookup_.functions_.size());
        lookup_.functions_.push_back(Signature{
            d.name, sym,
            fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
            cls, false, access, false });
        lookup_.functions_.back().isStaticMember = true;
        lookup_.functions_.back().isNoexcept = pendingNoexcept_;
        lookup_.functions_.back().spec = pendingSpec_;
        pendingNoexcept_ = false;
        return;
    }

    // **A slot is taken once and then kept**: an override replaces the entry the base
    // put there, matching on the signature minus the return type. **And finding that
    // slot is what makes this virtual**, keyword or not - so the search runs first.
    std::vector<VSlot> &slots = classes_.vtables_[cls];
    std::size_t slot = slots.size();
    bool foundBase = false;
    for (std::size_t i = 0; i < slots.size(); i++) {
        // clang refuses an override that drops or adds the ref-qualifier as [over.load]/2 does.
        if (slots[i].name == d.name && classes_.sameParameters(slots[i].params, params) &&
            mixesRefQualifier(slots[i].refThis, refThis))
            src_.fail(d.pos, overload_.refQualifierMixRefusal(refThis, slots[i].refThis));
        if (!classes_.overrides(slots[i], d.name, params, constThis, refThis)) continue;
        classes_.checkOverrideReturn(slots[i], fn->returns(), cls, d.name, d.pos);
        checkNotFinal(slots[i], cls, d.name, d.pos);
        slot = i;
        isVirtual = foundBase = true;
        break;
    }

    // **The slots that came down are the primary base's**, and a class may
    // override a virtual of any base - a second one, or a virtual one, whose
    // function then takes a new slot here and a thunk in that base's table.
    if (!isVirtual)
        if (const Type *self = lookup_.findTypedef(cls)) {
            const std::vector<Type::BaseSpec> &bs = self->bases();
            for (std::size_t bi = 0; bi < bs.size() && !isVirtual; bi++) {
                std::map<std::string, std::vector<VSlot> >::const_iterator it =
                    classes_.vtables_.find(bs[bi].type->tag());
                if (it == classes_.vtables_.end()) continue;
                for (std::size_t i = 0; i < it->second.size(); i++)
                    if (classes_.overrides(it->second[i], d.name, params, constThis, refThis)) {
                        classes_.checkOverrideReturn(it->second[i], fn->returns(), cls, d.name, d.pos);
                        checkNotFinal(it->second[i], cls, d.name, d.pos);
                        isVirtual = foundBase = true;
                        break;
                    }
            }
        }
    // [class.virtual]/4: `override` wants a base's virtual to replace, and `final` a virtual to mark.
    if (virt.isOverride && !foundBase)
        src_.fail(virt.overrideAt, "'" + cls + "::" + d.name + "' is marked 'override' but "
                                   "overrides nothing - no base declares a virtual '" + d.name +
                                   "' with these parameters");
    if (virt.isFinal && !isVirtual)
        src_.fail(virt.finalAt, "'" + cls + "::" + d.name + "' is marked 'final' and is not "
                                "virtual - only a virtual function can be");
    // Asked here and not where `= 0` is read, because a base's slot makes a function virtual without the keyword.
    if (isPure && !isVirtual)
        src_.fail(d.pos, "'= 0' makes a function pure, and only a virtual one can be - '" +
                         d.name + "' is not declared 'virtual' and overrides nothing");

    const std::string symbol = linkage_.memberSymbol(cls, d.name, fn, access, constThis,
                                            refThis, d.pos, isVirtual);
    if (!pendingDefaults_.empty()) defaultArgs_[symbol] = pendingDefaults_;
    if (!pendingDefaults_.empty())
        defaultArgNamespace_[symbol] = namespaceStack_;
    pendingDefaults_.clear();
    set.push_back(lookup_.functions_.size());
    lookup_.functions_.push_back(Signature{
        d.name, symbol,
        fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
        cls, constThis, access, isVirtual });
    lookup_.functions_.back().isNoexcept = pendingNoexcept_;
    lookup_.functions_.back().spec = pendingSpec_;
    lookup_.functions_.back().refThis = refThis;
    lookup_.functions_.back().isExplicit = explicitConversion;
    pendingNoexcept_ = false;

    if (!isVirtual) return;

    // **A pure virtual's slot holds the runtime's trap, not this function.**
    const std::string entry = isPure ? pureVirtualSymbol() : symbol;
    if (slot < slots.size()) {
        slots[slot].symbol = entry;
        slots[slot].pure = isPure;
        slots[slot].returns = fn->returns();
        slots[slot].isFinal = virt.isFinal;
        return;
    }
    // **cl groups a class's own overloads of one name at the first one's
    // slot, latest first** - `g(int) g(double) h() g(char)` lays `g(char)
    // g(double) g(int) h()`, measured 2026-09-17 - where Itanium appends.
    std::size_t at = slots.size();
    if (target_.microsoftNames()) {
        std::map<std::string, std::size_t>::const_iterator own = classes_.ownSlotsFrom_.find(cls);
        for (std::size_t i = own == classes_.ownSlotsFrom_.end() ? 0 : own->second; i < slots.size(); i++)
            if (slots[i].name == d.name) { at = i; break; }
    }
    slots.insert(slots.begin() + static_cast<std::ptrdiff_t>(at),
                 VSlot{ d.name, entry, params, constThis, isPure, fn->returns(), virt.isFinal,
                        refThis });
}

void Parser::checkNotFinal(const VSlot &s, const std::string &cls, const std::string &name,
                           std::size_t pos) const {
    if (!s.isFinal) return;
    src_.fail(pos, "'" + cls + "::" + name + "' overrides a virtual function marked "
                   "'final' - [class.virtual]/4 - and nothing may override one");
}

// **The parameter list is what identifies a function now, not the name.** A difference
// in the parameters declares a *second* function and only an identical list is a
// redeclaration. The return type is deliberately not part of that search.
void Parser::declareFunction(const std::string &name, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, bool defining, std::size_t pos,
                             bool internal) {
    // While a specialization is being replayed the function it declares is that
    // specialization, keyed as "twice<int>" from when the call asked for it. **And the
    // namespace goes on here, once**, every table below being keyed by the whole name.
    const std::string plain = instantiationName(name);
    const std::string qualified =
        (cLinkage_ > 0 || plain == "main" || namespaceStack_.empty() ||
         (!instantiationKey_.empty() && plain == instantiationKey_))
            ? plain : namespacePrefix() + plain;
    const std::string &key = qualified;
    checkOperatorDeclarable(key, params, false, pos, internal);
    const bool cName = cLinkage_ > 0 || key == "main";
    std::vector<std::size_t> &set = lookup_.functionIndex_[key];
    const SpecialDef special = takeSpecialDef();

    // An inline namespace's member sits under the enclosing name too, and only a qualified
    // declarator reaches it there - `int lib::f(int) {}` defines lib::v2::f - after lib's own.
    for (std::size_t pass = 0, k = 0; pass < (definingQualified_ ? 2u : 1u); k++) {
        if (k == set.size()) { pass++; k = static_cast<std::size_t>(-1); continue; }
        Signature &f = lookup_.functions_[set[k]];
        // A specialization sits in this list under the template's plain name
        // so that resolution can see it. It is not a declaration of that
        // name, so a function written with the same parameters is a new one.
        if (f.fromTemplate && instantiationKey_.empty()) continue;
        if ((!f.fromTemplate && f.name != key) != (pass == 1)) continue;
        if (f.variadic != variadic || !classes_.sameParameters(f.params, params)) continue;
        // [dcl.fct.def.delete]/4: a deleted definition is the first declaration.
        if (special == SpecialDef::Deleted)
            src_.fail(pos, "'" + key + "' is deleted here and was declared before - a deleted "
                           "function has to be deleted on its first declaration");
        if (f.deleted)
            src_.fail(pos, "'" + key + "' was declared '= delete' and may not be declared again");

        if (f.returns != returns)
            src_.fail(pos, "'" + key + "' was declared to return '" +
                           f.returns->describe() + "' and this says '" +
                           returns->describe() + "' - two functions cannot "
                           "differ in the return type alone");
        // **A declaration and its definition must agree about `noexcept`.** Measured
        // both ways: clang refuses a definition that drops it and one that adds it.
        // The specification is not part of the type, so this is what holds them together.
        if (f.isNoexcept != pendingNoexcept_)
            src_.fail(pos, "'" + key + "' was declared " +
                           (f.isNoexcept ? "'noexcept'" : "without 'noexcept'") +
                           " and this says " +
                           (pendingNoexcept_ ? "'noexcept'" : "nothing") +
                           " - a declaration and its definition have to make "
                           "the same promise");
        // [except.spec]/3 asks the same of `throw(T...)`: the same set of types, or none on both.
        if (f.spec.written != pendingSpec_.written || !f.spec.sameSet(pendingSpec_))
            src_.fail(pos, "'" + key + "' " + (f.spec.written ? "was declared with a 'throw(...)' list"
                                                               : "was declared without a 'throw(...)' list") +
                           " and this " + (pendingSpec_.written ? (f.spec.written ? "names other types" : "has one")
                                                                : "has none") +
                           " - [except.spec]/3, every declaration of a function has the same specification");
        if (defining) {
            if (f.defined) src_.fail(pos, "'" + key + "' is defined twice");
            f.defined = true;
        }
        pendingNoexcept_ = false;
        pendingSpec_ = DynamicSpec();

        // **[dcl.fct.default]/4: a later declaration adds defaults, it does not discard them.**
        if (!pendingDefaults_.empty()) {
            // The scope this declaration was written in, for
            // [dcl.fct.default]/5.
            defaultArgNamespace_[f.symbol] = definitionNamespace_.empty() ? namespaceStack_ : definitionNamespace_;
            std::vector<std::size_t> &have = defaultArgs_[f.symbol];
            if (have.size() < pendingDefaults_.size())
                have.resize(pendingDefaults_.size(), 0);
            for (std::size_t i = 0; i < pendingDefaults_.size(); i++) {
                if (pendingDefaults_[i] == 0) continue;
                // **The same default read twice is not a second one.**
                if (have[i] == pendingDefaults_[i]) continue;
                if (have[i] != 0)
                    src_.fail(pos, "'" + key + "' already has a default for "
                                   "parameter " + std::to_string(i + 1) +
                                   " from an earlier declaration, and a later "
                                   "one may add a default where there was none "
                                   "but may not give a second");
                have[i] = pendingDefaults_[i];
            }
            requireDefaultsAreASuffix(have, pos);
        }
        pendingDefaults_.clear();
        return;
    }

    // A new parameter list, so a new function - unless the name can only hold
    // one. Both halves of that are refused here rather than at the link, where
    // the report would be about a duplicate symbol in a file nobody wrote.
    if (!set.empty()) {
        const Signature &first = lookup_.functions_[set[0]];
        if (!rules().overloads())
            rules().refuseCxxOnly(src_, pos, "a second declaration of one name with another parameter list");
        if (cName || first.cLinkage)
            src_.fail(pos, "'" + key + "' cannot be overloaded - " +
                           (key == "main" ? std::string("'main' is one function")
                                           : std::string("a name with C linkage "
                                             "carries one symbol")));
    }

    set.push_back(lookup_.functions_.size());
    lookup_.functions_.push_back(Signature{ key,
                                    linkage_.functionSymbol(key, returns, params, variadic,
                                                   internal, pos),
                                    returns, params, variadic, defining, pos,
                                    cName, std::string(), false,
                                    Access::Public });
    lookup_.functions_.back().isNoexcept = pendingNoexcept_;
    lookup_.functions_.back().spec = pendingSpec_;
    lookup_.functions_.back().deleted = special == SpecialDef::Deleted;
    pendingNoexcept_ = false;
    if (!cName) lookup_.aliasFunctionKey(key, lookup_.functions_.size() - 1);
    if (pendingExplicitConversion_ && isConversionName(instantiationName(name)))
        lookup_.functions_.back().isExplicit = true;
    pendingExplicitConversion_ = false;
    if (!pendingDefaults_.empty()) {
        defaultArgs_[lookup_.functions_.back().symbol] = pendingDefaults_;
        defaultArgNamespace_[lookup_.functions_.back().symbol] = definitionNamespace_.empty() ? namespaceStack_ : definitionNamespace_;
    }
    pendingDefaults_.clear();
}

void Parser::blockFunctionDeclaration(const Declared &d) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    declareFunction(d.name, d.type, params, variadic, false, d.pos);
}

// **`Ts... rest` - one thing written, several parameters made.** In a pattern the pack
// stands for itself, which Itanium spells `DpT0_`; in an instantiation it is as many
// parameters as members, named `rest$0` and `rest$1`, which `rest...` expands to.
bool Parser::packParameter(std::vector<const Type *> *types,
                           std::vector<std::string> *names) {
    // `[const] Ts [& | &&]... rest` - the pack read through packDeclarator, which says how each member is wrapped.
    int wrap = 0;
    std::size_t nameAt = 0;
    const std::size_t n = packDeclarator(&wrap, &nameAt);
    if (n == 0) return false;
    auto pk = packs_.find(peekAt(nameAt).text);
    if (pk == packs_.end()) return false;

    std::vector<const Type *> members = pk->second.types;
    const bool pattern = members.size() == 1 &&
                         members[0]->kind() == Kind::TemplateParam;
    at_ += n;
    for (std::size_t i = 0; i < members.size(); i++) members[i] = wrapPackMember(members[i], wrap);
    std::string base;
    if (peek().kind == TokenKind::Ident) { base = peek().text; at_++; }

    if (pattern) {
        types->push_back(types_.packExpansion(members[0]));
        if (names != nullptr) names->push_back(base);
        return true;
    }
    std::vector<std::string> made;
    for (std::size_t i = 0; i < members.size(); i++) {
        types->push_back(types_.withoutConst(members[i]));
        made.push_back(base + "$" + std::to_string(i));
        if (names != nullptr) names->push_back(made.back());
    }
    // Recorded under the *written* name as well, so `rest...` at a call and
    // `sizeof...(rest)` both find it beside `sizeof...(Ts)`.
    if (!base.empty()) {
        PackBinding pb;
        pb.types = members;
        pb.names = made;
        packs_[base] = pb;
    }
    pk->second.names = made;
    return true;
}

// To the ',' or ')' that ends a default argument, counting brackets so that a call or
// a subscript written inside one keeps its own commas. `<` is not counted: such a
// default is refused where it is read rather than mis-parsed here.
void Parser::skipDefaultArgument() {
    int depth = 0;
    // A template-id's arguments keep their commas too - `std::vector<std::pair<A, B> >()` (M1 L3b);
    // the angles count only where a template's name opened them, [temp.names]/3.
    std::vector<int> angles;
    for (;;) {
        const Token &t = peek();
        if (t.kind == TokenKind::End)
            src_.fail(t.pos, "this default argument never ends");
        if (t.is("<") && at_ > 0 && tokens_[at_ - 1].kind == TokenKind::Ident &&
            isTemplateName(tokens_[at_ - 1].text, true))
            angles.push_back(depth);
        else if ((t.is(">") || t.is(">>")) && !angles.empty() && angles.back() == depth) {
            angles.pop_back();
            if (t.is(">>") && !angles.empty() && angles.back() == depth) angles.pop_back();
        } else if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return;         // the ')' closing the parameters
            depth--;
        } else if (t.is(",") && depth == 0 && angles.empty()) return;
        at_++;
    }
}

// To the ',' or ';' that ends a member's own initialiser, counting brackets so
// that a call or a braced list written inside one keeps its own commas.
void Parser::skipMemberInitialiser() {
    int depth = 0;
    for (;;) {
        const Token &t = peek();
        if (t.kind == TokenKind::End)
            src_.fail(t.pos, "this member initialiser never ends");
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return;
            depth--;
        } else if (depth == 0 && (t.is(",") || t.is(";"))) return;
        at_++;
    }
}

// **[class.base.init]/9: a member the constructor did not name is initialised
// by the initialiser the class gave it.**
void Parser::refuseDeletedDefaultInit(const Type *t, const std::string &name,
                                      std::size_t pos) {
    const Type *plain = t->unqualified();
    while (plain->isArray()) plain = plain->pointee()->unqualified();
    if (!plain->isStructOrUnion() || plain->tag().empty()) return;
    if (!plain->bases().empty()) return;
    if (hasConstructors(plain->tag())) return;

    const std::vector<Member> &all = plain->members();
    for (std::size_t i = 0; i < all.size(); i++) {
        const bool needsOne = all[i].type->isConst() || all[i].type->isReference();
        if (!needsOne) continue;
        if (memberInit_.find(plain->tag() + "::" + all[i].name) != memberInit_.end())
            continue;
        // A const member of an anonymous union is set where another member of it has an initialiser.
        bool unionSet = false;
        for (std::size_t k = 0; k < all.size() && all[i].anonUnion != nullptr; k++)
            if (all[k].anonUnion == all[i].anonUnion &&
                memberInit_.find(plain->tag() + "::" + all[k].name) != memberInit_.end())
                unionSet = true;
        if (unionSet) continue;
        src_.fail(pos, "'" + name + "' has type '" + plain->describe() +
                       "', whose member '" + all[i].name + "' is " +
                       (all[i].type->isReference() ? "a reference"
                                                   : "const") +
                       " and has no initialiser - [class.ctor] deletes the "
                       "default constructor of such a class, because nothing "
                       "it could write would ever set that member. Give the "
                       "member an initialiser, or the class a constructor "
                       "that takes one");
    }
}

bool Parser::hasMemberInitialiser(const std::string &tag) const {
    const std::string prefix = tag + "::";
    std::map<std::string, std::size_t>::const_iterator it =
        memberInit_.lower_bound(prefix);
    return it != memberInit_.end() &&
           it->first.compare(0, prefix.size(), prefix) == 0;
}

std::vector<StmtPtr> Parser::memberInitialisers(const std::string &tag,
                                                const Type *type, int thisSlot,
                                                const std::set<std::string> &already,
                                                std::size_t pos) {
    std::vector<StmtPtr> out;
    const std::vector<Member> &ms = type->members();
    const std::set<const Type *> taken = unionsNamedBy(type, already);
    for (std::size_t i = 0; i < ms.size(); i++) {
        if (already.find(ms[i].name) != already.end()) continue;
        if (ms[i].anonUnion != nullptr && taken.count(ms[i].anonUnion) != 0) continue;
        StmtPtr one = memberInitialiser(tag, type, ms[i], thisSlot, pos);
        if (one != nullptr) out.push_back(std::move(one));
    }
    return out;
}

StmtPtr Parser::memberInitialiser(const std::string &tag, const Type *type,
                                  const Member &m, int thisSlot,
                                  std::size_t pos) {
    std::map<std::string, std::size_t>::const_iterator it =
        memberInit_.find(tag + "::" + m.name);
    if (it == memberInit_.end()) return nullptr;

    // Read where it was written, with the constructor's locals put aside and
    // the class current: an initialiser on a member is in the class's scope,
    // where its enumerators and static members answer unqualified.
    const std::size_t resume = at_;
    std::vector<Local> outer;
    outer.swap(locals_);
    // `this` stays: [class.mem]/4 lets the initialiser name another member, read through the object.
    for (const Local &l : outer) if (l.name == "this") locals_.push_back(l);
    const Type *outerClass = currentClass_;
    currentClass_ = type;
    struct RestoreClass {
        Parser *p; const Type *was;
        ~RestoreClass() { p->currentClass_ = was; }
    } restoreClass{ this, outerClass };
    // And in the class's namespace, wherever the constructor is being made: [basic.lookup.unqual]/7.
    struct RestoreNamespace {
        std::vector<std::string> &stack; std::vector<std::string> saved;
        ~RestoreNamespace() { stack = saved; }
    } restoreNamespace{ namespaceStack_, namespaceStack_ };
    namespaceStack_ = lookup_.classNamespacePath(type);
    at_ = it->second;

    // `{v}` on a scalar: the value, which may not narrow - [dcl.init.list]/7.
    // A class with constructors is built from any list, `{}` and `{0}` included - they are not its bytes.
    const Type *memberCls = m.type->unqualified();
    const bool built = memberCls->isStructOrUnion() && !memberCls->tag().empty() && hasConstructors(memberCls->tag());
    const bool braced = peek().is("{") && (built || (!peekAt(1).is("}") &&
        !(peekAt(1).kind == TokenKind::Num && !peekAt(1).isFloat && peekAt(1).value == 0 && peekAt(2).is("}"))));
    // `= {}` or `= {0}`: the member's bytes zeroed by memset, an array or a
    // scalar alike, which is what [dcl.init.list]/3 comes to for either.
    if (peek().is("{") && !braced) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        ExprPtr field = thisMember(thisSlot, type, m);
        ExprPtr addr(new Unary('&', std::move(field)));
        addr->setType(types_.pointerTo(m.type));
        ExprPtr asVoid(new Cast(voidPtr, std::move(addr)));
        asVoid->setType(voidPtr);
        std::vector<ExprPtr> args;
        args.push_back(std::move(asVoid));
        ExprPtr zero(new Num(0LL));
        zero->setType(types_.intType());
        args.push_back(std::move(zero));
        ExprPtr n(new Num(static_cast<long long>(m.type->size(target_))));
        n->setType(types_.get(target_.sizeType()));
        args.push_back(std::move(n));
        std::vector<int> argSlots(args.size(), 0);
        Call *fill = new Call("memset", nullptr, std::move(args), false, 0, -1,
                              std::move(argSlots));
        fill->setSymbol("memset");
        ExprPtr filled(fill);
        filled->setType(voidPtr);
        locals_.swap(outer);
        at_ = resume;
        return StmtPtr(new ExprStmt(std::move(filled)));
    }
    // **A list on a class or an array** (M1 L4): a class with constructors is built from it, direct- or
    // copy-list as `{` or `= {` was written; an aggregate or an array of scalars is laid down leaf by leaf.
    if (braced && (m.type->isArray() || m.type->unqualified()->isStructOrUnion())) {
        Init in = parseInitialiser();
        std::vector<StmtPtr> all;
        const Type *cls = m.type->unqualified();
        if (cls->isStructOrUnion() && !cls->tag().empty() && hasConstructors(cls->tag()) &&
            !(isAggregateClass(cls) && !in.items.empty())) {
            std::vector<ExprPtr> args;
            if (memberInitCopy_.count(tag + "::" + m.name)) args.push_back(listTemporaryFrom(cls, in, true, pos));
            else args = bracedInitArguments(cls, in, pos);
            all.push_back(constructMember(tag, type, m, thisSlot, args, pos, false));
        } else {
            const MemberRoot root = { thisSlot, type, &m };
            const MemberRoot *was = memberRoot_;
            memberRoot_ = &root;
            std::vector<InitStep> path;
            emitInit(std::string(), path, m.type, in, all);
            memberRoot_ = was;
        }
        locals_.swap(outer);
        at_ = resume;
        flushTemporaries(all);
        return oneStatement(std::move(all));
    }
    if (braced) at_++;
    const std::size_t valueAt = peek().pos;
    ExprPtr value = decay(assign());
    if (braced) {
        checkNarrowing(m.type, *value, valueAt, "'" + m.name + "'");
        expect("}");
    }

    // **A class-typed member is *built* from its initialiser, not assigned
    // one.**
    StmtPtr made;
    if (memberClass(m.type) != nullptr && !m.type->isReference() &&
        hasConstructors(memberClass(m.type)->tag())) {
        std::vector<ExprPtr> one;
        one.push_back(std::move(value));
        made = constructMember(tag, type, m, thisSlot, one, pos, false);
        if (made == nullptr) value = std::move(one[0]);
    }
    if (made == nullptr) {
        checkAssignable(*value, m.type, pos, "'" + m.name + "'");
        value = convert(std::move(value), m.type);
        ExprPtr field = thisMember(thisSlot, type, m);
        ExprPtr store(new Assign(std::move(field), std::move(value)));
        store->setType(m.type);
        made = StmtPtr(new ExprStmt(std::move(store)));
    }
    locals_.swap(outer);
    at_ = resume;

    // **The initialiser is a full expression, and its temporaries die at the
    // end of it** - [class.temporary]/4, which here means before the next
    // member is built and not at the end of the constructor.
    std::vector<StmtPtr> all;
    all.push_back(std::move(made));
    flushTemporaries(all);
    return oneStatement(std::move(all));
}

// Statements as one: the statement itself where there is one, a block of them otherwise.
StmtPtr Parser::oneStatement(std::vector<StmtPtr> all) {
    if (all.size() == 1) return std::move(all[0]);
    return StmtPtr(new Block(std::move(all)));
}

// **A class-typed member is built, not left.** [class.base.init]/8: one the list does
// not name is default-initialised, and a written constructor used to leave it holding
// the stack. An array member is built by a loop, N being a property of the type.
StmtPtr Parser::constructMember(const std::string &cls, const Type *type,
                                const Member &m, int thisSlot,
                                std::vector<ExprPtr> &args, std::size_t pos,
                                bool implicit) {
    const Type *mc = memberClass(m.type);
    if (mc == nullptr || mc->tag().empty() || m.type->isReference()) return nullptr;
    const std::string key = constructorKey(mc->tag());
    if (lookup_.overloadsOf(key) == nullptr) return nullptr;
    if (!args.empty() && m.type->isArray())
        src_.fail(pos, "'" + m.name + "' is an array, and an initialiser list "
                       "cannot say what to pass to each element of it");

    // **A trivially copyable member is copied, not constructed.**
    if (args.size() == 1 && !m.type->isArray() &&
        classes_.copyConstructorOf(mc) == nullptr && classes_.moveConstructorOf(mc) == nullptr) {
        const Type *at = args[0]->type();
        if (at != nullptr && memberClass(at) == mc) {
            ExprPtr dst = thisMember(thisSlot, type, m);
            dst->setType(m.type);
            ExprPtr store(new Assign(std::move(dst), std::move(args[0])));
            store->setType(m.type);
            return StmtPtr(new ExprStmt(std::move(store)));
        }
    }

    // Held by value: reading a default argument can grow `lookup_.functions_`.
    Signature chosen;
    if (!args.empty()) {
        chosen = overload_.resolveOverload(key, args, pos);
    } else {
        const Signature *ctor = classes_.defaultConstructorOf(mc);
        if (ctor == nullptr)
            src_.fail(pos, implicit
                ? "'" + cls + "' has no constructor of its own, and the one "
                  "the compiler would write cannot build its member '" +
                  m.name + "': '" + mc->tag() + "' has no constructor taking "
                  "nothing"
                : "this constructor of '" + cls + "' does not name '" + m.name +
                  "' in its initialiser list, and '" + mc->tag() + "' has no "
                  "constructor taking nothing - add ': " + m.name + "(...)' "
                  "to the list");
        chosen = *ctor;
    }
    if (chosen.access != Access::Public && !insideAccessOf(mc, chosen.access) &&
        !isFriendOf(mc))
        src_.fail(pos, implicit
            ? "'" + cls + "' cannot be built by the constructor the compiler "
              "would write: the constructor of '" + mc->tag() + "' taking "
              "nothing is " + (chosen.access == Access::Private ? "private"
                                                                 : "protected")
            : "'" + mc->tag() + "' has no public constructor taking these "
              "arguments for '" + m.name + "' - the one that matches is " +
              (chosen.access == Access::Private ? "private" : "protected"));
    // Through noteUsed: inside a candidate's body, ranked and not yet chosen, the use waits with the rest.
    for (std::size_t k = 0; k < lookup_.functions_.size(); k++)
        if (lookup_.functions_[k].symbol == chosen.symbol) noteUsed(k);
    const std::size_t written = args.size();
    applyDefaults(chosen, args, pos);

    int indexSlot = 0;
    long long count = 0;
    if (m.type->isArray()) {
        memberElements(m.type, &count);
        if (count < 0)
            src_.fail(pos, "'" + cls + "::" + m.name + "' has no length, so a "
                           "constructor does not know how many elements to "
                           "build");
        indexSlot = allocateFrameSlot(types_.intType());
    }

    ExprPtr acc = thisMember(thisSlot, type, m);

    ExprPtr addr;
    if (m.type->isArray()) {
        addr = indexBytes(types_, decay(std::move(acc)), mc, indexSlot, target_);
    } else {
        addr = ExprPtr(new Unary('&', std::move(acc)));
        addr->setType(types_.pointerTo(mc));
    }

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    std::vector<const Type *> ps;
    ps.push_back(types_.pointerTo(mc));
    for (std::size_t k = 0; k < args.size(); k++) {
        all.push_back(std::move(args[k]));
        ps.push_back(chosen.params[k]);
    }
    // Keyed by the function being defined - a member template's specialization by its own symbol, which topLevel set.
    if (!implicit && !m.type->isArray()) {
        const MemberBuilt b = { chosen.symbol, types_.functionType(types_.get(Kind::Void), ps, false), args.size() != written };
        const std::string key = currentFunction_ + "|" + m.name + "|" + std::to_string(written == 0 ? 0 : static_cast<std::uint32_t>(pos));
        // Two choices under one key - a constructor template's specializations read under one name - are no record at all.
        const std::map<std::string, MemberBuilt>::iterator had = memberBuilt_.find(key);
        if (had == memberBuilt_.end()) memberBuilt_[key] = b;
        else if (had->second.symbol != b.symbol || had->second.signature != b.signature) had->second.symbol.clear();
    }
    StmtPtr one(new ExprStmt(
        completeCall(mc->tag(), chosen.symbol, nullptr, types_.get(Kind::Void),
                     ps, false, pos, std::move(all))));
    if (!m.type->isArray()) return one;
    Alive built{ "this", thisSlot, mc };
    built.owner = type;
    for (std::size_t k = 0; k < type->members().size(); k++)
        if (&type->members()[k] == &m) built.member = static_cast<int>(k);
    StmtPtr loop = eachElement(indexSlot, count, std::move(one));
    return built.member < 0 ? std::move(loop) : guardElements(std::move(loop), built, indexSlot, pos);
}

void Parser::parameterTypes(std::vector<const Type *> &params, bool &variadic) {
    expect("(");
    variadic = false;
    pendingDefaults_.clear();
    std::vector<std::size_t> defaults;
    writtenParams_.clear();
    writtenFor_.clear();
    std::vector<const Type *> written;
    std::size_t closed = peek().pos;
    if (consume(")")) return;
    if (peek().is("void") && peekAt(1).is(")")) { at_ += 2; return; }

    for (;;) {
        if (consume("...")) { variadic = true; expect(")"); break; }
        if (packParameter(&params, nullptr)) {
            // A pack's members are their own types; keep the two lists level.
            while (written.size() < params.size()) written.push_back(params[written.size()]);
            if (consume(")")) break;
            expect(",");
            continue;
        }
        StorageClass psc;
        Qualifiers pquals;
        const Type *pt = specifiers(&psc, &pquals);
        Declared pd = declarator(pt, true);
        refuseThreadLocalHere(pquals.isThreadLocal, pd.pos);
        if (tmpl_.mentionsDeduced(pd.type))
            src_.fail(pd.pos, "a parameter's type cannot be deduced - `auto` "
                              "there is C++14, and this compiler is C++11");
        const Type *declared = pd.type;
        if (pd.type->isArray()) pd.type = types_.pointerTo(pd.type->pointee());
        if (pd.type->isFunction()) pd.type = types_.pointerTo(pd.type);   // [dcl.fct]/5, `S f(T())`
        if (pd.type->isVoid())
            src_.fail(pd.pos, "'void' is only a parameter list on its own");
        params.push_back(types_.withoutConst(pd.type));
        written.push_back(linkage_.parameterAsWritten(declared, params.back()));

        // `int b = 3`. The tokens are left where they are and their position
        // recorded; a call that omits the argument reads them again.
        defaults.resize(params.size(), 0);
        if (consume("=")) {
            if (dialect() == Dialect::C90) rules().refuseCxxOnly(src_, peek().pos, "a default argument");
            defaults.back() = at_;
            skipDefaultArgument();
            if (at_ == defaults.back())
                src_.fail(peek().pos, "this parameter says '=' and then gives "
                                      "no default");
        }
        closed = peek().pos;
        if (consume(")")) break;
        expect(",");
    }

    // Kept apart until the list ends: a template-id in a later parameter instantiates a class, and
    // its members' own lists reset pendingDefaults_ (M1, 2026-10-23).
    pendingDefaults_ = defaults;
    requireDefaultsAreASuffix(pendingDefaults_, closed);
    if (written.size() == params.size() && written != params) {
        writtenParams_ = std::move(written);
        writtenFor_ = params;
    }
}

// [dcl.fct.default]/4: once a parameter has a default, every one after it must have
// one too - a call fills them in from the right, so a parameter without one behind a
// parameter with one could never be reached. Said where the list is read.
void Parser::requireDefaultsAreASuffix(const std::vector<std::size_t> &defaults,
                                       std::size_t pos) {
    bool seen = false;
    for (std::size_t i = 0; i < defaults.size(); i++) {
        if (defaults[i] != 0) { seen = true; continue; }
        if (seen)
            src_.fail(pos, "every parameter after one with a default needs a "
                           "default of its own - a call fills them in from the "
                           "right, so there would be no way to reach this one");
    }
}

