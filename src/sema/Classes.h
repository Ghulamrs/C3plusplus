#pragma once

// **sema::Classes, out of the parser in place** - M2 step 1, doc-cxx3/M2-PLAN.md. Its functions moved here
// whole; they still read the parser's state through p_, and the state follows in a later commit.

#include "Entities.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Type.h"

#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Parser;

namespace sema {

class Classes {
public:
    explicit Classes(Parser &p) : p_(p) {}

    void checkOverrideReturn(const VSlot &s, const Type *returns, const std::string &cls,
                             const std::string &name, std::size_t pos) const;
    // Whether a slot is the one a member with this name and parameter list overrides.
    // Three places ask it - the slot search, the search across the bases after the
    // first, and the secondary table's walk - and the third copy is why it is named.
    static bool overrides(const VSlot &s, const std::string &name,
                          const std::vector<const Type *> &params,
                          bool constThis, RefQual refThis);
    void vptrSites(const Type *cls, int at, bool morally, const Type *vbase,
                   int fromVBase, const Type *layout,
                   std::set<const Type *> &seen,
                   std::vector<VptrSite> &out) const;
    // The virtual bases of a class in the order their `vbase_offset`
    // entries are built, each with its offset in the layout class.
    void vbaseComponents(const Type *cls, const Type *layout, int clsAt,
                         std::set<const Type *> &seen,
                         std::vector<std::pair<const Type *, int> > &out)
        const;
    static int virtualBaseAt(const Type *layout, const Type *vbase);
    void vcallEntries(const Type *y, int yAt, const Type *vbase, int vbaseAt,
                      const Type *root, int rootAt, const Type *layout,
                      std::vector<VcallEntry> &out, std::size_t pos);
    const VSlot *declaredSlot(const Type *cls, const VSlot &s) const;
    Overrider finalOverrider(const Type *root, int rootAt, const VSlot &s,
                             const Type *target, int targetAt,
                             const Type *layout, std::size_t pos);
    std::string synthesizeVirtualThunk(const Type *type, const VSlot &slot,
                                       long long fixed, long long vcallBack,
                                       std::size_t pos);
    void subobjectsOf(const Type *x, int xAt, const Type *layout,
                      std::set<const Type *> &seen,
                      std::vector<Subobject> &out) const;
    bool containsSubobject(const Type *y, int yAt, const Type *x, int xAt,
                           const Type *layout) const;
    void layoutOneVtable(VtableGroup &g, const Type *x, int xAt,
                         bool xIsVirtual, const Type *vbase, int vbaseAt,
                         const Type *root, int rootAt, const Type *layout,
                         const std::string &typeInfo, std::size_t pos);
    void layoutSecondaryVtables(VtableGroup &g, const Type *x, int xAt,
                                const Type *vbase, int vbaseAt,
                                const Type *root, int rootAt,
                                const Type *layout, bool construction,
                                const std::string &typeInfo, std::size_t pos);
    void layoutVirtualBaseVtables(VtableGroup &g, const Type *x,
                                  const Type *root, int rootAt,
                                  const Type *layout, bool construction,
                                  const std::string &typeInfo,
                                  std::set<const Type *> &seen,
                                  std::size_t pos);
    VtableGroup buildVtableGroup(const Type *cls, int clsAt,
                                 bool clsIsVirtual, const Type *layout,
                                 const std::string &typeInfo,
                                 std::size_t pos);
    // A group written out under `symbol`, its address points recorded.
    void emitVtableGroup(const std::string &symbol, VtableGroup g);
    std::string constructionVtable(const Type *base, int at,
                                   bool baseIsVirtual, const Type *layout,
                                   const std::string &typeInfo,
                                   std::size_t pos);
    void emitItaniumVtables(const Type *cls, const std::string &tag,
                            const std::string &symbol, std::size_t pos);
    void vttPart(const Type *cls, int clsAt, const std::string &group,
                 const Type *layout, bool top, const std::string &typeInfo,
                 VttLayout &out, std::size_t pos);
    void vttSecondaryPointers(const Type *x, int xAt, bool morally,
                              const std::string &group, const Type *layout,
                              bool top, std::set<const Type *> &seen,
                              VttLayout &out, int from, std::size_t pos);
    void vttVirtualParts(const Type *x, const Type *layout,
                         const std::string &typeInfo,
                         std::set<const Type *> &seen, VttLayout &out,
                         std::size_t pos);
    // The class's mangled name alone - what `_ZTV`, `_ZTT` and `_ZTC` share.
    std::string itaniumClassEncoding(const Type *cls) const;
    // The VTT argument a call to a C2 or D2 carries: the sub-VTT for a
    // non-virtual base from this function's own VTT, or an entry of the
    // class's `_ZTT` for the complete object's calls.
    ExprPtr vttForBase(const Type *cls, const Type *base);
    ExprPtr vttOfClass(const Type *cls, int entry);
    // `*(vtt + n)`, the address point an entry holds.
    ExprPtr vttEntry(ExprPtr vtt, int entry);
    // The offset to a virtual base read through the object's vptr.
    ExprPtr vbaseOffsetRead(ExprPtr objectChars, const Type *cls,
                            const Type *vbase);
    // The vptrs a constructor or destructor of `cls` stores, the whole
    // object's - its own, every non-primary base's at any depth, every
    // virtual base's - and where each comes from.
    std::vector<StmtPtr> itaniumVptrStores(const std::string &cls,
                                           const Type *memberOf, int thisSlot,
                                           std::vector<VptrStore> *record);
    // The _ZTI a throw or a catch names, emitting one for a class on the way.
    // Answers empty and fills `why` where this compiler cannot describe it.
    std::string emitPointerTypeInfo(const Type *ptr, std::size_t pos,
                                    std::string *why);
    std::string emitEnumTypeInfo(const Type *e, std::size_t pos, std::string *why);
    std::string typeInfoSymbolFor(const Type *t, std::size_t pos,
                                  std::string *why);
    // The flags word and a virtual base's vtable slot, both for the
    // `__vmi_class_type_info` a class with anything but a single base at
    // offset zero needs.
    static long long itaniumVmiFlags(const Type *cls);
    long long itaniumVbaseOffsetSlot(const Type *cls, const Type *vbase) const;
    std::string emitClassTypeInfo(const Type *cls, const std::string &tag,
                                  std::size_t pos);
    void emitVtable(const Type *cls, const std::string &tag, std::size_t pos);
    // The Microsoft vbtable: where each virtual base is, measured from the vbptr.
    void emitVbtable(const Type *cls, const std::string &tag, std::size_t pos);
    // Two parameter lists compared as C++ compares them - same length, same
    // types, in order. Six places asked it inline; what goes beside it, constThis
    // or variadic, stays with the caller, because the answers differ.
    static bool sameParameters(const std::vector<const Type *> &a,
                               const std::vector<const Type *> &b);
    ExprPtr virtualBaseMember(ExprPtr object, const Type *staticType,
                              const Member &m);
    ExprPtr microsoftVirtualBaseMember(ExprPtr object, const Type *owner,
                                       const Member &m);
    // The bytes from an object to its virtual base, read out of the vbtable.
    // `temp`/`held` name a char * lvalue already holding the address.
    ExprPtr microsoftVirtualBaseStep(const Type *owner, const Type *vbase,
                                     const std::string &temp, int held,
                                     int *baseAt) const;
    // Which vbtable entry holds a virtual base, and where that base sits.
    // Entry 0 is the vbptr's own offset, so 0 back means "not in this table".
    static int virtualBaseSlot(const Type *owner, const Type *vbase,
                               int *baseAt);
    // A member the layout copied down from a base, told by the offset it sits at.
    static bool memberFromBase(const Type *cls, const Member &m);
    // **Would default-initialising this leave anything uninitialised?**
    bool constDefaultInitialisable(const Type *t) const;
    // The statements that set an object's vptrs - the primary one and any a
    // polymorphic second base needs. Every constructor emits these, written
    // or implicit, which is why they live in one place.
    std::vector<StmtPtr> storeVptrs(const std::string &cls, const Type *memberOf,
                                    int thisSlot, std::vector<VptrStore> *record = nullptr);
    // And the vbtable pointer beside them, Microsoft only.
    void storeVbptr(const std::string &cls, const Type *memberOf, int thisSlot,
                    std::vector<StmtPtr> &into);
    // A thunk: the entry a secondary table holds for a function this class
    // overrides. It moves `this` back to the complete object and calls the
    // real one. Named for the offset it undoes - _ZThn16_N1C1gEv.
    std::string synthesizeThunk(const std::string &cls, const Type *type,
                                const VSlot &slot, int offset, std::size_t pos);
    // cl's vcall thunk: `&S::f` on a virtual f is the address of a function
    // that dispatches through the object's vftable, since the Microsoft
    // member pointer is one code pointer with no room for a slot index.
    std::string synthesizeVcallThunk(const Type *cls, const Signature &f,
                                     int index, std::size_t pos);
    void declareImplicitDestructor(const std::string &tag, const Type *type,
                                   std::size_t pos);
    void synthesizeDestructor(std::size_t which);
    // [class.dtor]/8's member half, shared by the destructor the compiler
    // writes and the one the program writes.
    std::vector<StmtPtr> memberDestructors(const std::string &cls,
                                           const Type *type, int thisSlot,
                                           std::size_t pos);
    // One member's destruction, appended to `out` - nothing for a member with no destructor.
    void memberDestructor(std::vector<StmtPtr> &out, const std::string &cls, const Type *type,
                          int thisSlot, std::size_t n, std::size_t pos);
    // The deleting destructor, which no program writes: it runs the
    // destructor and then frees. Itanium calls it D0 and Microsoft ??_G, and
    // it is what a `delete` through a base pointer reaches.
    void synthesizeDeleting(const std::string &cls, const Type *type,
                            Access access, std::size_t pos);
    // **A class with a virtual base needs two bodies where every other class
    // needs one.**
    void synthesizeCompleteCtor(const Type *type,
                                const std::vector<const Type *> &ctorParams,
                                const std::string &c1, const std::string &c2,
                                bool isInline, std::size_t pos,
                                int copyArg = -1, bool moving = false,
                                std::map<std::string, std::vector<ExprPtr> >
                                    *vbaseArgs = nullptr);
    // The same split on the way out, and the order reverses with it: D1 calls
    // D2 - which destroys what C2 built - and then destroys the virtual bases.
    void synthesizeCompleteDtor(const Type *type, const std::string &d1,
                                const std::string &d2, bool isInline,
                                std::size_t pos);
    // A constructor of this class taking nothing, written or implicit.
    const Signature *defaultConstructorOf(const Type *cls) const;
    // What the class did not write, the compiler declares. Called once the
    // class is complete, because whether an implicit member is trivial - and
    // so whether it is a function at all - is a question about the members.
    void declareImplicitSpecials(const std::string &tag, const Type *type,
                                 std::size_t pos);
    void declareImplicitDefaultCtor(const std::string &tag, const Type *type,
                                    std::size_t pos, Access access);
    // Bodies for the implicit members something actually called, run at the
    // end of the file and to a fixed point: giving one class a body can be
    // what first calls another's.
    void defineImplicitFunctions();
    void synthesizeDefaultCtor(std::size_t which);
    void declareInheritingCtors(const std::string &tag, const Type *type, std::size_t pos);
    bool keyFunctionUndefined(const std::string &tag) const;
    bool hasKeyFunction(const std::string &tag) const;
    const Signature *keyFunction(const std::string &tag) const;
    void pruneExternalVtables(Program &program);
    // One body for both halves of the copy. They differ in three places -
    // which member function to call, whether the vptr is stored, and whether
    // there is a value to return - and in nothing else.
    void synthesizeCopy(std::size_t which, bool assigning);
    const Signature *copyAssignOf(const Type *cls) const;
    void declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                   std::size_t pos);
    void declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                 std::size_t pos);
    // A constructor of this class taking one reference to it, written or
    // implicit - which is what [class.copy] calls a copy constructor.
    const Signature *copyConstructorOf(const Type *cls) const;
    const Signature *moveConstructorOf(const Type *cls) const;
    void declareImplicitMoveCtor(const std::string &tag, const Type *type,
                                 std::size_t pos, bool userDeclared);
    void declareDefaultedMoveAssign(const std::string &tag, const Type *type, std::size_t pos);
    // [class.copy]/20 - the implicit move assignment, through declareDefaultedMoveAssign where it has work.
    void declareImplicitMoveAssign(const std::string &tag, const Type *type, std::size_t pos,
                                   bool userDeclared);
    const Signature *moveAssignOf(const Type *cls) const;

    std::map<std::string, std::vector<VSlot> > vtables_;
    // Where a class's own new slots begin, after its primary base's - the Microsoft overload order is kept within that run only.
    std::map<std::string, std::size_t> ownSlotsFrom_;

private:
    Parser &p_;
    // Per inheriting constructor (its index in lookup_.functions_), the base and the base constructor it forwards to.
    struct Inheriting { const Type *base; std::size_t from; };
    std::map<std::size_t, Inheriting> inheriting_;
    // Every group written, by symbol: a `_ZTV`, or a construction `_ZTC`.
    std::map<std::string, VtableGroup> groups_;
};

}   // namespace sema
