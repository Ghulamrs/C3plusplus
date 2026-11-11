#pragma once

// **The lowering of the typed tree to the SSA IR** (M3b.1, doc-cxx3/M3-IR.md 3.8), built with no Parser in reach: what
// it reads is the typed tree with its record, the syntax tree it points into, the TypeTable for layout, the Target and
// the program's data - this header includes nothing of src/parser/. Every local is an alloca (no mem2reg until M3b.4).

#include "Ir.h"
#include "../sema/Typing.h"
#include "../syntax/Syntax.h"
#include "../Type.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

struct Program;

namespace ir {

// What the lowering is told of the function being lowered, which Ast.h's Function carries and the typed tree does not.
struct FunctionFacts {
    std::string symbol, displayName;
    bool isStatic, isInline, variadic, hasThis, sret, landingPads, wrappedBody, noexcept_;
    bool isConstructor;     // the parser read the definition as its class's constructor
    TypeId returns;
    TypeId thisType;        // a member function's `this`, the first of the parser's parameter slots
    std::size_t pos, paramCount;
    std::string specFilter;     // an ordinary function's throw(...) list as the parser's filter row names it: `$spec,<types>`
    bool noexceptWrap;          // [except.spec]/9 as the parser kept it: the body under a catch-all that aborts
    bool ctorTry;               // [except.pre]/4: a constructor's try block over its mem-initialisers and body, or a destructor's
    std::string recordKey;      // the symbol the parser keyed this function's records by: Itanium's C1 or D1 for a split C2 or D2
    bool takesVtt;              // Itanium's C2 or D2 of a class with virtual bases: a VTT is its second parameter
    bool msFlag;                // a Microsoft constructor of a class with virtual bases: cl's most-derived flag, last
    std::map<int, int> localAligns;     // a local aligned past its type (alignas), by the parser's serial: the alignment its slot keeps
    std::vector<TypeId> paramTypes;     // a compiler-written function's own parameters after `this` and a VTT: an inheriting constructor's (M3b.3)
    FunctionFacts() : isStatic(false), isInline(false), variadic(false), hasThis(false), sret(false), landingPads(false),
                      wrappedBody(false), noexcept_(false), isConstructor(false), pos(0), paramCount(0), noexceptWrap(false),
                      ctorTry(false), takesVtt(false), msFlag(false) {}
};

class Lowering {
public:
    Lowering(const sema::TypedTree &tree, const syntax::Tree &syntax, TypeTable &types, const Target &target, Program &program,
             Module &into);
    const sema::TypedTree &tree() const { return tree_; }
    TypeTable &types() const { return types_; }
    const Target &target() const { return target_; }
    Program &program() const { return program_; }
    // One function's typed `function` node lowered into the module, or none with the reason the subset does not reach it.
    FuncId lower(sema::TypedId fn, const FunctionFacts &facts, std::string &why);
    // Which classes a constructor builds - the parser's to say, asked by the glue (M3b.2).
    void setConstructed(std::function<bool(TypeId)> f) { constructed_ = f; }
    void setAssigned(std::function<bool(TypeId)> f) { assigned_ = f; }
    void setDefaultInit(std::function<bool(TypeId, const std::string &)> f) { defaultInit_ = f; }
    // A member's own initialiser as a written constructor's mem-initialiser, typed: by constructor and "class::member".
    void setMemberDefault(std::function<sema::TypedId(const std::string &, const std::string &)> f) { memberDefault_ = f; }
    void setGuarded(std::function<bool(const std::string &)> f) { guarded_ = f; }
    void setStaticGuard(std::function<bool(const std::string &, sema::StaticGuard &)> f) { staticGuard_ = f; }
    void setTypeInfo(std::function<std::string(TypeId)> f) { typeInfo_ = f; }
    void setUnnamedCaught(std::function<std::string(syntax::NodeId)> f) { unnamedCaught_ = f; }
    void setMemberZeroed(std::function<bool(const std::string &, const std::string &)> f) { memberZeroed_ = f; }
    void setDynamicCast(std::function<int(syntax::NodeId, std::string &, std::string &)> f) { dynamicCast_ = f; }
    void setTypeid(std::function<bool(syntax::NodeId, std::string &)> f) { typeid_ = f; }
    void setMicrosoftThrowInfo(std::function<std::string(TypeId)> f) { msThrowInfo_ = f; }
    void setMicrosoftCatch(std::function<bool(TypeId, std::string &, bool &)> f) { msCatch_ = f; }
    void setMicrosoftUnnamedCaught(std::function<bool(syntax::NodeId, std::string &, bool &, bool &)> f) { msUnnamedCaught_ = f; }
    void setNewFacts(std::function<bool(syntax::NodeId, sema::NewFacts &)> f) { newFacts_ = f; }
    void setRangeCalls(std::function<bool(syntax::NodeId, sema::Resolution &, sema::Resolution &, std::vector<sema::Resolution> &)> f) { rangeCalls_ = f; }
    void setCaughtCopy(std::function<bool(syntax::NodeId, std::string &, TypeId &)> f) { caughtCopy_ = f; }
    // The constructor the parser chose for a member, and a class's destructor - Sema's records, asked by the glue.
    void setMemberBuilt(std::function<bool(const std::string &, const std::string &, std::uint32_t, std::string &, TypeId &, bool &)> f) { memberBuilt_ = f; }
    void setDestructor(std::function<std::string(TypeId)> f) { destructor_ = f; }
    // The vptr stores the parser made in a written constructor or destructor, by its symbol.
    void setVptrStores(std::function<bool(const std::string &, std::vector<sema::VptrStore> &)> f) { vptrStores_ = f; }
    void setBaseVtt(std::function<int(const std::string &, const std::string &)> f) { baseVtt_ = f; }
    // Itanium's vbase_offset slot, bytes before the vptr, for a virtual base of a class - Sema's record, asked by the glue.
    void setVbaseSlot(std::function<long long(TypeId, TypeId)> f) { vbaseSlot_ = f; }
    // The typed body of the written constructor whose C1 `key` names, for the Builder (M3b.3): its entries build C1's virtual bases.
    void setSplitBody(std::function<sema::TypedId(const std::string &)> f) { splitBody_ = f; }
    // Microsoft: which constructors take cl's most-derived flag, and what a written one's guard stores and builds.
    void setMsVbase(std::function<bool(const std::string &)> takes,
                    std::function<bool(const std::string &, std::vector<sema::VptrStore> &, std::vector<std::string> &)> init) {
        msVbaseCtor_ = takes;
        msVbaseInit_ = init;
    }
    // The constructor the parser chose for a local's `var` node - Sema's record, asked by the glue.
    void setLocalBuilt(std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &, bool &)> f) { localBuilt_ = f; }
    // The initializer_list parameter a local's braces were made into, and their count - Sema's record, asked by the glue.
    void setLocalList(std::function<bool(syntax::NodeId, TypeId &, std::uint32_t &)> f) { localList_ = f; }
    // Each element of the braced list an array local of a class was built from - Sema's record, asked by the glue.
    void setListBuilt(std::function<bool(syntax::NodeId, sema::ListBuilt &)> f) { listBuilt_ = f; }
    void setMemberList(std::function<bool(const std::string &, const std::string &, std::uint32_t, TypeId &, std::uint32_t &)> f) { memberList_ = f; }
    void setTemporaryBuilt(std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &)> f) { temporaryBuilt_ = f; }
    // The converting constructor that made a temporary of a value at a node - Sema's record, asked by the glue.
    void setConvertedBuilt(std::function<bool(syntax::NodeId, std::string &, TypeId &)> f) { convertedBuilt_ = f; }
    // The copy the parser made of a by-value class argument, by the argument's node - Sema's record, asked by the glue.
    void setArgumentCopy(std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &)> f) { argumentCopy_ = f; }
    // A lambda's closure and its captures' members and sources - Sema's record, asked by the glue.
    void setClosure(std::function<bool(syntax::NodeId, sema::ClosureFacts &)> f) { closure_ = f; }
    void setReturnBuilt(std::function<bool(syntax::NodeId, int &, std::string &, TypeId &)> f) { returnBuilt_ = f; }
    void setNamedReturn(std::function<bool(syntax::NodeId)> f) { namedReturn_ = f; }
    // Whether the parser built a call's class result straight into a local (`var`, the call) - Sema's record.
    void setElidedInto(std::function<bool(syntax::NodeId, syntax::NodeId)> f) { elidedInto_ = f; }
    // The copy or move assignment the parser called for `a = b`, by b's node - Sema's record.
    void setAssignCall(std::function<bool(syntax::NodeId, std::string &, TypeId &)> f) { assignCall_ = f; }
    // The operator-> calls `->` on a class value made, by the value's node - Sema's record ([over.ref]).
    void setArrowCalls(std::function<bool(syntax::NodeId, std::vector<sema::Resolution> &)> f) { arrowCalls_ = f; }
    // The conversion function `convert` called on a class value, by the value's node - Sema's record ([class.conv.fct]).
    void setConversionCall(std::function<bool(syntax::NodeId, sema::Resolution &)> f) { conversionCall_ = f; }
    void setDynamicInits(std::function<bool(std::vector<sema::DynamicInit> &, bool)> f) { dynamicInits_ = f; }
private:
    friend class Builder;       // M3b.3: a compiler-written function, built from the class's layout with these same pieces
    struct Outside { std::string why; explicit Outside(const std::string &w) : why(w) {} };
    struct Val { ValueId v; TypeId type; Val() {} Val(ValueId a, TypeId t) : v(a), type(t) {} };
    struct Loop { BlockId brk, cont; std::size_t handlers, live; };
    // A local a destructor destroys, alive from its construction to the end of its scope; `pad` is its cleanup's index in pads_; `when`: a count its cleanup reads, destroying only where it is above `above` (a virtual base's); `count`: an array being built - the slot counting its elements built so far, which its cleanup hands `dtor`, the class's destructor loop.
    struct Live { ValueId slot; TypeId cls; std::string dtor; std::size_t pad; ValueId vtt, when, built, count; long long above; Live() : pad(0), above(0) {} };
    // A landing pad a call inside its region unwinds to (M3-IR 3.6): a try's, or a handler's end-catch cleanup. Its block is
    // made on first use; `types` is the whole action chain, own handlers first, then the enclosing pads' - the walker's.
    struct Pad { BlockId block; ValueId ptr, sel; bool cleanup; std::vector<std::string> types; std::vector<int> indices; std::size_t own; };
    struct SwitchCtx { std::vector<long long> values; std::vector<BlockId> targets; BlockId deflt; BlockId brk; };

    // The builder: a new instruction in the current block, its result value made where `type` is one.
    InstId add(Op op, TypeId type, const std::vector<ValueId> &ops);
    Function &F() { return module_.functions[fnIndex_]; }
    const Function &F() const { return module_.functions[fnIndex_]; }
    Inst &at(InstId id) { return F().insts[id.v]; }
    ValueId result(InstId id) const { return F().inst(id).result; }
    BlockId newBlock();
    void place(BlockId b);
    bool terminated() const;
    bool unreached(BlockId b) const;
    bool madePads() const;
    void specFilterChain(const Pad &p);
    void noexceptAbort(const Pad &p);
    void br(BlockId to, const std::vector<ValueId> &args = std::vector<ValueId>());
    void condbr(ValueId c, BlockId t, BlockId f, const std::vector<ValueId> &tArgs = std::vector<ValueId>(),
                const std::vector<ValueId> &fArgs = std::vector<ValueId>());
    ValueId param(BlockId b, TypeId type);
    ValueId iconst(TypeId type, long long k);
    ValueId fconst(TypeId type, long double v);
    ValueId fallOffValue(const Type &rt);   // the zero a body that falls off its end returns
    ValueId zeroOf(TypeId type);
    ValueId addrOf(const std::string &sym, TypeId type);
    ValueId load(ValueId p, TypeId type);
    ValueId op2(Op o, TypeId type, ValueId a, ValueId b);     // a two-operand instruction
    void store(ValueId p, ValueId v);
    // `volatile` (M3b.4 round 6): `p` made the address of an object written volatile where `written`; a load or store
    // through it, or through a gep or bitcast of it, is marked.
    Val markVolatile(Val p, bool written);
    bool volatileAt(ValueId p) const { return p.v < volatile_.size() && volatile_[p.v] != 0; }

    // The typed tree, read.
    const syntax::Node &syn(sema::TypedId id) const { return syntax_.node(tree_.node(id).syntax); }
    const std::string &text(sema::TypedId id) const { return syntax_.text(tree_.node(id).syntax); }
    sema::TypedId child(sema::TypedId id, std::uint32_t i) const { return tree_.child(id, i); }
    std::uint32_t count(sema::TypedId id) const { return tree_.node(id).count; }
    const Type &T(TypeId t) const { return *types_.type(t); }
    TypeId tid(const Type &t) const { return t.id(); }
    static void outside(const std::string &why);

    // Statements.
    void stmt(sema::TypedId id);
    void block(sema::TypedId id);
    void declaration(sema::TypedId id);
    void localArray(sema::TypedId var, ValueId slot, TypeId type);
    void stringArray(sema::TypedId lit, ValueId slot, const Type &t);
    void returnStmt(sema::TypedId id);
    void returnConstructed(sema::TypedId value);
    void returnDestroyed(sema::TypedId value);
    void returnCopied(sema::TypedId id, sema::TypedId value);
    void namedReturnLocal(sema::TypedId var, ValueId slot, const Type &t);
    bool elidedLocal(sema::TypedId var) const;
    void elidedCall(sema::TypedId call, ValueId slot);
    bool classAssign(sema::TypedId id, Val &out);
    bool listFits(const Type &t, std::uint32_t values) const;
    void listInit(ValueId slot, const Type &t, sema::TypedId list, std::uint32_t from);
    void aggregateInit(ValueId slot, const Type &t, const std::vector<sema::TypedId> &parts);
    void zeroBlock(ValueId slot, long long bytes);
    void aggregateFill(ValueId slot, const Type &t, const std::vector<sema::TypedId> &parts, std::size_t &next);
    void aggregateItem(ValueId at0, const Type &mt, const std::vector<sema::TypedId> &parts, std::size_t &next);
    bool recordedAggregate(sema::TypedId var, ValueId slot, const Type &t);
    bool recordedAggregate(sema::TypedId var, ValueId slot, const Type &t, const sema::ListBuilt &lb);
    bool recordedMember(const Type &mt) const;
    void aggregateMember(ValueId at0, const Type &mt, const std::vector<sema::TypedId> &parts, std::size_t &next);
    void aggregateTail(ValueId at0, const Type &t);
    Val classValue(sema::TypedId id, const Type &t);
    void forStmt(sema::TypedId id);
    void rangeForStmt(sema::TypedId id);
    Val bracedRange(sema::TypedId range, const Type &e, TypeId iter, std::size_t scope);
    ValueId rangeEnd(const sema::Resolution &r, Val range, TypeId &iter);
    void switchStmt(sema::TypedId id);
    BlockId labelBlock(const std::string &name);
    void gotoStmt(const std::string &name);
    void labelReached(const std::string &name, BlockId b);
    void tryStatement(sema::TypedId id);
    // An Itanium try's handlers read and its pad pushed, then its body lowered, then its chain: a statement's, or a
    // constructor's function try block, whose handlers rethrow at their end ([except.handle]/15).
    struct TryHandler { sema::TypedId node, var, body; TypeId caught; std::string sym; int index; };
    struct TryState { std::vector<TryHandler> hs; BlockId done; };
    void tryOpen(const std::vector<sema::TypedId> &handlers, TryState &st);
    bool caughtByValue(sema::TypedId handler, const Type &c);
    void caughtCopied(ValueId slot, ValueId from, const std::string &copy, TypeId sig);
    void tryClose(TryState &st, bool rethrows);
    void microsoftTry(sema::TypedId id);
    void leaveHandlers(std::size_t n, std::size_t mark, BlockId target, const std::vector<ValueId> &args = std::vector<ValueId>());
    void checkFuncletUses(BlockId hb);
    void msLeave(std::size_t n, std::size_t mark, BlockId target, const std::vector<ValueId> &args, std::size_t upper);   // MsShapes.cpp
    BlockId padBlock(Pad &p);
    BlockId msEnclosingPad();
    void resumeFrom(const Pad &p, std::size_t enclosing);
    void endCatches(std::size_t n);
    void leaveCatches(std::size_t n, std::size_t mark);
    int typeIndexFor(const std::string &sym);
    ValueId entryAlloca(TypeId slotType, const std::string &name);
    Val runtimeCall(const std::string &symbol, TypeId returns, const std::vector<ValueId> &args);
    InstId callOrInvoke(TypeId carries, const std::vector<ValueId> &ops);

    // Expressions: an rvalue, or an object's address with its type.
    Val expr(sema::TypedId id);
    Val lvalue(sema::TypedId id);
    bool isLvalueNode(sema::TypedId id) const;
    bool referenceCast(sema::TypedId id) const;
    bool reinterpretsReference(sema::TypedId id) const;
    Val name(sema::TypedId id);
    Val nameAddress(sema::TypedId id);
    Val member(sema::TypedId id);
    Val referenceMember(ValueId object, const Type &cls, const Member &m);
    Val globalAddress(const sema::Resolution &r);
    void destroyWhen(const Live &l, bool invoke = false);
    bool canFlag() const;
    void storageGuard(ValueId p, const Type &made, const std::string &release);
    void staticArray(sema::TypedId var, const std::string &symbol, const Type &array, const sema::StaticGuard &g);
    Val memberAddress(sema::TypedId id);
    Val memberOfThis(sema::TypedId id);
    Val fieldAddress(sema::TypedId id, Member &bf);
    Val bitUnit(ValueId object, const Member &m, bool constant);
    Val bitRead(const Val &unit, const Member &m);
    Val bitAssign(sema::TypedId id, const Val &unit, const Member &m);
    bool namesCalledFunction(const std::string &head, const std::string &symbol) const;
    Val functorMember(sema::TypedId head, const Type &self);
    Val capturedThis();
    Val ownThis(const Type &want);
    ValueId virtualBaseAddress(ValueId object, const Type &owner, const Type &vbase);
    ValueId virtualBaseMember(ValueId object, const Type &owner, const Member &m, TypeId type);
    Val readOf(ValueId p, TypeId type);
    Val unary(sema::TypedId id);
    Val step(sema::TypedId id, bool increment, bool prefix, ValueId &where);
    bool boundLocal(sema::TypedId init, const Type &t, ValueId slot);
    Val binary(const std::string &op, sema::TypedId l, sema::TypedId r);
    Val arithmetic(const std::string &op, Val a, Val b);
    Val logical(bool isAnd, sema::TypedId l, sema::TypedId r);
    Val conditional(sema::TypedId id);
    Val voidConditional(sema::TypedId id, BlockId a, BlockId b, BlockId merge);
    bool compositePointer(sema::TypedId id, const Type &tx, const Type &ty) const;
    Val assign(sema::TypedId id);
    Val assignScalar(sema::TypedId id, Val &target);
    Val bracedAssign(sema::TypedId id, Val &target);
    bool scalarAssignNode(sema::TypedId id) const;
    bool globalFunctor(sema::TypedId head, const std::string &symbol, const Type &self, Val &object);
    Val qualifiedMemberOfThis(sema::TypedId id);
    bool baseAssignNode(sema::TypedId id) const;
    Val baseAssigned(sema::TypedId id);
    bool anonymousUnionMember(sema::TypedId id) const;
    Val unionMemberAddress(sema::TypedId id);
    Val call(sema::TypedId id);
    bool vaBuiltinNode(sema::TypedId id) const;
    Val vaBuiltin(sema::TypedId id);
    Val newExpression(sema::TypedId id);
    ValueId arrayCookie(ValueId raw, ValueId n, const Type &made, int cookie);
    Val deleteExpression(sema::TypedId id);
    sema::NewFacts newFacts(sema::TypedId id, const std::string &what);
    Val literal(sema::TypedId id);
    Val truth(Val v);
    Val convert(Val v, TypeId to, bool cast = false);
    Val exprTo(sema::TypedId id, TypeId to);
    Val promote(Val v);
    Val decay(Val v);
    Val pointerAdd(Val p, Val n);
    Val pointerSub(Val a, Val b);
    bool foldConstant(sema::TypedId id, long long &out);
    bool isNullConstant(sema::TypedId id) const;

    // The conversion rules the parser applies, written again over the TypeTable ([conv.prom], [expr.arith.conv]).
    TypeId promoted(TypeId t) const;
    TypeId usualArithmetic(TypeId a, TypeId b) const;
    TypeId unsignedVersion(TypeId t) const;
    TypeId ptrdiffType() const;
    static bool scalar(const Type &t);
    static bool referable(const Type &t);
    bool plainClass(const Type &t) const;
    bool bytesClass(const Type &t) const;
    bool simpleLayout(const Type &cls) const;
    bool builtMember(const Type &t) const;
    bool buildableLayout(const Type &cls, bool vptr = false, bool arrays = false, bool vbases = false) const;
    TypeId builtArray(const Type &t, long long &count, bool local = false) const;
    void buildElements(const Member &m, const Type &et, long long elems);
    void builtArrayLocal(sema::TypedId var, ValueId slot, const Type &et, long long elems);
    void listArray(sema::TypedId var, ValueId base, const Type &et, long long elems, const sema::ListBuilt &lb, const std::string &dtor);
    static bool scalarArray(const Type &t);
    void buildMember(ValueId at, const Member &m, sema::TypedId entry);
    void destroyMembers(const Type &cls);
    int baseNamed(const Type &cls, const std::string &name) const;
    void bindParameter(sema::TypedId p, ValueId pv, TypeId slotType);
    ValueId baseAddress(const Type::BaseSpec &b);
    TypeId msFlagged(const std::string &symbol, TypeId sig, std::vector<ValueId> &ops, int flag);
    void buildBase(const Type::BaseSpec &b, sema::TypedId entry);
    void copiedBase(const Type::BaseSpec &b, sema::TypedId entry);
    bool delegating(const Type &cls);
    void delegateTo(const Type &cls, sema::TypedId entry);
    void msVirtualBases(const Type &cls, const std::map<std::string, sema::TypedId> &named, ValueId from = ValueId());
    Live msGuardedVirtualBase(const Type::BaseSpec &b, ValueId count);
    void msCountedDestroy(const Live &l, BlockId pad);
    void destroyBases(const Type &cls);
    void arguments(sema::TypedId id, std::uint32_t from, const Type &ft, std::size_t skip, bool variadic, std::vector<ValueId> &ops);
    void argumentList(const Type &ft, std::size_t skip, bool variadic, const std::vector<sema::TypedId> &args, bool dummyInt,
                      std::vector<ValueId> &ops);
    void directCall(const std::string &symbol, TypeId signature, const std::vector<ValueId> &ops);
    bool pseudoDestructor(sema::TypedId member, const sema::Resolution &called);
    bool threadLocalGlobal(const std::string &symbol) const;
    static bool blockExtern(const sema::Resolution &r);
    ValueId objectAddr(const std::string &sym, TypeId type, const sema::Resolution &r);
    Val toBase(Val v, TypeId to, bool cast = false);
    ValueId boundConverted(sema::TypedId an, const Type &p);
    bool convertedTemporary(sema::TypedId an, const Type &c, bool destroys, ValueId &slot);
    bool bracedArgument(sema::TypedId an, const Type &p, ValueId &slot);
    ValueId byValueArgument(sema::TypedId an, const Type &p);
    Val classArgumentSource(sema::TypedId an, const Type &c);
    void guardParameter(ValueId pv, const Type &pt);
    bool heldClass(const Type &t) const;
    static bool arrayBinds(const Type &from, const Type &referent);
    Val baseOfObject(Val a, const Type &p);
    Val baseSubobject(Val a, const Type &base, const std::string &why);
    Val valueInitialised(sema::TypedId id);
    Val deleteDestroying(sema::TypedId id, const sema::NewFacts &f);
    Val deleteArray(sema::TypedId id, const sema::NewFacts &f);
    ValueId virtualCallee(ValueId self, TypeId fnType, int slot);
    bool memberPointerNode(sema::TypedId id) const;
    bool memberAddressNode(sema::TypedId id) const;
    Val memberFunctionPair(sema::TypedId id);
    Val memberPointerCall(sema::TypedId id, sema::TypedId mp, CallInfo info);
    bool dataMemberPointerNode(sema::TypedId id) const;
    void offsetModel(const Type &mp);
    Val dataMemberPointer(sema::TypedId id);
    ValueId nullMemberPointer(TypeId mp);
    Val memberPointerConversion(Val v, TypeId to);
    void memberFunctionPointerInto(ValueId slot, Val from, const Type &to);
    Val memberPointerCompare(const std::string &op, Val a, Val b);
    Val memberPointerLvalue(sema::TypedId id);
    Val callTail(TypeId sig, CallInfo info, std::vector<ValueId> ops, std::size_t skip, const std::vector<sema::TypedId> &args,
                 bool dummyInt);
    bool operatorCalls(sema::TypedId id) const;
    Val operatorCall(sema::TypedId id);
    Val condition(sema::TypedId id);
    void throwExpr(sema::TypedId id);
    bool abortingThrow(sema::TypedId id);
    ValueId localAlloca(sema::TypedId var, TypeId slotType);
    void microsoftThrow(sema::TypedId id);
    Val thrownValue(sema::TypedId id, TypeId &thrown);
    bool classThrow(sema::TypedId id, ValueId &object, TypeId &thrown);
    void throwCall(const std::string &symbol, const std::vector<ValueId> &args);
    void basePaths(const Type &cls, const Type &base, int at, int &found, int &offset);
    void memberInits(sema::TypedId fnNode, std::uint32_t from, std::uint32_t to);
    void storeVptrs();
    // A function's opening and close, shared with the Builder: the module's new Function reset and named, its return type
    // checked, its entry block with `this` and a VTT; and the return block with its one `ret`.
    void beginFunction(const FunctionFacts &facts);
    void finishFunction(bool landingPads);
    ValueId vttAt(int entry);
    std::size_t baseVttArgument(const Type &base, const Type &ft, std::vector<ValueId> &ops);
    void classLocal(sema::TypedId var, ValueId slot, const Type &t);
    void guardedStatic(sema::TypedId var, const std::string &symbol);
    void staticConstruction(sema::TypedId var, const std::string &symbol, const sema::StaticGuard &g);
    ValueId staticAddr(const std::string &symbol, TypeId type);
    std::string atexitFor(const std::string &symbol) const;
    bool builtClass(const Type &t) const;
    void builtLocal(sema::TypedId var, ValueId slot, const Type &t);
    ValueId initializerList(sema::TypedId parent, const Type &param, std::uint32_t n);
    ValueId initializerListOf(const std::vector<sema::TypedId> &elems, const Type &param, const sema::ListBuilt &lb = sema::ListBuilt());
    ValueId backingArray(const std::vector<sema::TypedId> &elems, const Type &e, const sema::ListBuilt &lb);
    void listElement(ValueId at, const Type &et, sema::TypedId part, const sema::ListElement &e);
    void guard(ValueId slot, const Type &c, const std::string &dtor);
    void destroy(const Live &l, bool invoke = false);
    void destroyLive(std::size_t mark);
    void closeScope(std::size_t mark, bool keep = false);
    void scopedStmt(sema::TypedId id);
    void copyObject(ValueId to, ValueId from, const Type &t);
    bool plainTemporary(sema::TypedId id) const;
    bool polymorphicTemporary(sema::TypedId id, ValueId &slot);
    bool constructedClass(const Type &t) const;
    bool newBuilt(const Type &t) const;
    bool bytesReturned(const Type &t) const;
    bool classResult(sema::TypedId id) const;
    bool destroyedReturned(const Type &t) const;
    bool copiedReturn(const Type &t) const;
    bool destroyedClass(const Type &t) const;
    bool temporaryShape(sema::TypedId id, std::uint32_t &from) const;
    bool builtTemporary(sema::TypedId id, bool destroyed) const;
    bool constructedTemporary(sema::TypedId id) const { return builtTemporary(id, false); }
    bool destroyedTemporary(sema::TypedId id) const { return builtTemporary(id, true); }
    bool copiedTemporary(sema::TypedId id) const;
    bool zeroedTemporary(sema::TypedId id) const;
    bool temporaryNode(sema::TypedId id) const {
        return plainTemporary(id) || constructedTemporary(id) || destroyedTemporary(id) || copiedTemporary(id)
            || listTemporaryNode(id) || zeroedTemporary(id);
    }
    bool listTemporaryNode(sema::TypedId id) const;
    void listTemporary(sema::TypedId id, ValueId slot);
    void constructFromList(ValueId where, const std::string &symbol, TypeId sig, const Type &cls, bool zeroFirst, sema::TypedId args, std::uint32_t from);
    bool classObjectNode(sema::TypedId id) const;
    Val classObject(sema::TypedId id);
    Val arrowPointer(sema::TypedId member);
    bool convertedNode(sema::TypedId id, sema::Resolution &r) const;
    Val converted(sema::TypedId id, const sema::Resolution &r);
    Val convertedObject(Val o, const sema::Resolution &r, bool object = false);
    bool convertedCast(sema::TypedId id, sema::Resolution &r) const;
    Val iteratorCall(const sema::Resolution &r, const std::vector<Val> &objects, TypeId cls);
    bool typeidNode(sema::TypedId id) const;
    Val typeidAddress(sema::TypedId id);
    bool classConditionalNode(sema::TypedId id) const;
    Val conditionalObject(sema::TypedId id);
    std::string temporaryDestructor(const Type &c);
    bool destroyedResultClass(const Type &t) const;
    bool destroyedResult(sema::TypedId id) const;
    void openFull() { openFull(live_.size()); }
    void openFull(std::size_t from);
    void closeFull();
    std::size_t fullFrom() const;
    Val temporary(sema::TypedId id);
    Val extendedTemporary(sema::TypedId id);
    Val extendedResult(sema::TypedId id);
    bool heldTemporaryInto(sema::TypedId id, ValueId slot, const Type &c);
    void construct(ValueId where, const std::string &symbol, TypeId sig, const Type &cls, bool zeroFirst, sema::TypedId args, std::uint32_t from,
                   sema::TypedId defaults = sema::TypedId());
    bool objectNode(sema::TypedId id) const;
    bool bracedNode(sema::TypedId id) const { return syn(id).kind == syntax::Kind::Other && text(id) == "{}"; }   // a `{}` over its values
    Val object(sema::TypedId id);
    bool closureNode(sema::TypedId id) const;
    bool closureBuiltNode(sema::TypedId id) const;
    Val closure(sema::TypedId id, ValueId into = ValueId());
    static bool scalarArithmetic(const Type &t);
    static long long normalise(long long v, const Type &t, const Target &target);

    const sema::TypedTree &tree_;
    const syntax::Tree &syntax_;
    TypeTable &types_;
    const Target &target_;
    Program &program_;

    Module &module_;
    // Per function: the one being lowered, by index.
    std::size_t fnIndex_;
    BlockId cur_;
    TypeId returns_;
    ValueId this_;          // a member function's `this`, none in any other
    TypeId thisType_;
    std::function<bool(TypeId)> constructed_, assigned_;
    std::function<bool(TypeId, const std::string &)> defaultInit_;
    std::function<sema::TypedId(const std::string &, const std::string &)> memberDefault_;
    std::function<bool(const std::string &)> guarded_;
    std::function<bool(const std::string &, sema::StaticGuard &)> staticGuard_;
    std::function<std::string(TypeId)> typeInfo_;
    std::function<std::string(syntax::NodeId)> unnamedCaught_;
    std::function<bool(const std::string &, const std::string &)> memberZeroed_;
    std::function<int(syntax::NodeId, std::string &, std::string &)> dynamicCast_;
    std::function<bool(syntax::NodeId, std::string &)> typeid_;
    Val dynamicCastPointer(sema::TypedId id);
    bool dynamicReference(sema::TypedId id) const;
    Val dynamicCastReference(sema::TypedId id);
    Val dynamicWalk(sema::TypedId id, const Val &v, TypeId to);
    std::function<std::string(TypeId)> msThrowInfo_;
    std::function<bool(TypeId, std::string &, bool &)> msCatch_;
    std::function<bool(syntax::NodeId, std::string &, bool &, bool &)> msUnnamedCaught_;
    BlockId funclet_;                           // the Microsoft handler whose funclet the blocks made now belong to
    BlockId msTryPad_;                          // the try that handler is of: its pad's CatchPad lists the exits
    BlockId msTryFunclet_;                      // the handler that try stands in, whose funclet its exits belong to; none in the body
    std::size_t funcletPads_;                   // pads_ where that handler's body began: a pad above it was made inside the funclet
    std::function<bool(syntax::NodeId, sema::NewFacts &)> newFacts_;
    std::function<bool(syntax::NodeId, sema::Resolution &, sema::Resolution &, std::vector<sema::Resolution> &)> rangeCalls_;
    std::function<bool(syntax::NodeId, std::string &, TypeId &)> caughtCopy_;
    std::function<bool(const std::string &, const std::string &, std::uint32_t, std::string &, TypeId &, bool &)> memberBuilt_;
    std::function<std::string(TypeId)> destructor_;
    std::function<bool(const std::string &, std::vector<sema::VptrStore> &)> vptrStores_;
    std::vector<sema::VptrStore> vptrs_;        // what a constructor or destructor of a class with a vptr stores
    std::function<int(const std::string &, const std::string &)> baseVtt_;
    std::function<long long(TypeId, TypeId)> vbaseSlot_;
    std::function<bool(const std::string &)> msVbaseCtor_;
    std::function<bool(const std::string &, std::vector<sema::VptrStore> &, std::vector<std::string> &)> msVbaseInit_;
    ValueId msFlag_;                            // a Microsoft constructor's most-derived flag; none otherwise
    std::string key_;                           // FunctionFacts::recordKey: the memberBuilt_ and vptrStores_ records' key
    ValueId vtt_;                               // Itanium's C2 or D2 handed a VTT: it, the second parameter; none otherwise
    std::vector<ValueId> forwarded_;            // an inheriting constructor's own parameters, handed on to the base it inherits from
    std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &, bool &)> localBuilt_;
    std::function<bool(syntax::NodeId, TypeId &, std::uint32_t &)> localList_;
    std::function<bool(syntax::NodeId, sema::ListBuilt &)> listBuilt_;
    std::function<bool(const std::string &, const std::string &, std::uint32_t, TypeId &, std::uint32_t &)> memberList_;
    std::vector<Live> live_;                    // innermost last
    ValueId sret_;                              // the caller's result pointer, where a copied class is returned
    ValueId sretSlot_;                          // where a Microsoft funclet reads it (resultPtr, MsShapes.cpp)
    ValueId resultPtr();
    ValueId elidedSlot_;                        // the named return value, not destroyed by the `return` naming it
    bool intoSret_ = false;                     // the next call builds its class result in the caller's object
    std::function<bool(syntax::NodeId, int &, std::string &, TypeId &)> returnBuilt_;
    std::function<bool(syntax::NodeId)> namedReturn_;
    std::function<bool(syntax::NodeId, syntax::NodeId)> elidedInto_;
    std::function<bool(syntax::NodeId, std::string &, TypeId &)> assignCall_;
    std::function<bool(syntax::NodeId, std::vector<sema::Resolution> &)> arrowCalls_;
    std::function<bool(syntax::NodeId, sema::Resolution &)> conversionCall_;
    std::function<bool(std::vector<sema::DynamicInit> &, bool)> dynamicInits_;    // the init function's or __tls_init's objects, for the Builder
    std::function<sema::TypedId(const std::string &)> splitBody_;
    sema::TypedId intoCall_;                    // the call elidedCall lowers, whose result goes into a local's alloca
    int intoAlloca_ = -1, intoNow_ = -1;        // that alloca; and callTail's, set only as that call's tail is entered
    std::vector<sema::ListElement> aggCtors_;     // recordedAggregate's: each member a constructor builds, the next at aggNext_
    std::size_t aggNext_ = 0;
    std::size_t liveFloor_;                     // a constructor's built bases and members: below it, kept by a return
    std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &)> temporaryBuilt_;
    std::function<bool(syntax::NodeId, std::string &, TypeId &)> convertedBuilt_;
    std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &)> argumentCopy_;
    std::function<bool(syntax::NodeId, sema::ClosureFacts &)> closure_;
    std::vector<Pad> pads_;                     // innermost last
    std::vector<std::string> typeTable_;        // the function's selector indices: one per type, first occurrence, 1-based
    std::size_t handlerDepth_;                  // handlers the statement being lowered is inside
    bool noexceptWrap_;                         // FunctionFacts::noexceptWrap: the body is under a catch-all that aborts
    std::map<int, int> localAligns_;            // FunctionFacts::localAligns of the function being lowered
    std::vector<std::size_t> handlerLive_;      // live_ where each handler's body began, innermost last
    std::vector<std::pair<BlockId, BlockId> > msHandlers_;   // beside it on Microsoft: the handler's msTryPad_ and msTryFunclet_
    struct Full {                               // a full expression open: its temporaries from live_[from]; flags zeroed at block, at
        std::size_t from; BlockId block; std::size_t at;
        Full(std::size_t f, BlockId b, std::size_t a) : from(f), block(b), at(a) {}
    };
    std::vector<Full> full_;                    // innermost last; from is kNoFull where none may be built (a throw's operand)
    std::size_t skippable_;                     // operands being lowered that may not be evaluated: `&&`, `||`, `?:`, `new`
    static const std::size_t kNoFull = static_cast<std::size_t>(-1);
    BlockId returnBlock_;
    std::map<std::uint32_t, std::pair<ValueId, TypeId> > locals_;    // a `var` node -> its alloca and declared type
    std::vector<char> volatile_;       // by value: 1 for the address of a volatile object (markVolatile)
    std::map<std::uint32_t, ValueId> unionSlots_;   // an anonymous union in a block, by its type -> its alloca
    bool bitWanted_ = false;           // fieldAddress asks memberAddress or memberOfThis for a bit-field's unit
    Member bitField_;                 // and a copy of the bit-field it found (width 0: none)
    std::map<std::uint32_t, std::pair<std::string, TypeId> > staticLocals_;   // a static `var` -> its symbol and type
    std::vector<Loop> loops_;
    std::vector<SwitchCtx> switches_;
    std::map<std::string, BlockId> labels_;
    std::map<std::string, std::pair<std::size_t, std::size_t> > labelScope_;    // a placed label: live_ and handlers there
    // A goto ahead of its label: the block it branches to, and what was alive and which handlers were open there.
    struct PendingGoto { std::string label; BlockId from; std::vector<Live> live; std::vector<std::size_t> handlerLive; std::vector<std::pair<BlockId, BlockId> > msHandlers; };
    std::vector<PendingGoto> pendingGotos_;
    void msGotoExit(const PendingGoto &g, BlockId label, std::size_t keep, std::size_t depth);
    bool hasGoto_;
};

}   // namespace ir
