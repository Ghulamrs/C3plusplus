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
    FunctionFacts() : isStatic(false), isInline(false), variadic(false), hasThis(false), sret(false), landingPads(false),
                      wrappedBody(false), noexcept_(false), isConstructor(false), pos(0), paramCount(0) {}
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
    void setTypeInfo(std::function<std::string(TypeId)> f) { typeInfo_ = f; }
    void setUnnamedCaught(std::function<std::string(syntax::NodeId)> f) { unnamedCaught_ = f; }
    void setMicrosoftThrowInfo(std::function<std::string(TypeId)> f) { msThrowInfo_ = f; }
    void setMicrosoftCatch(std::function<bool(TypeId, std::string &, bool &)> f) { msCatch_ = f; }
    void setNewFacts(std::function<bool(syntax::NodeId, sema::NewFacts &)> f) { newFacts_ = f; }
    // The constructor the parser chose for a member, and a class's destructor - Sema's records, asked by the glue.
    void setMemberBuilt(std::function<bool(const std::string &, const std::string &, std::uint32_t, std::string &, TypeId &, bool &)> f) { memberBuilt_ = f; }
    void setDestructor(std::function<std::string(TypeId)> f) { destructor_ = f; }
    // The vptr stores the parser made in a written constructor or destructor, by its symbol.
    void setVptrStores(std::function<bool(const std::string &, std::vector<sema::VptrStore> &)> f) { vptrStores_ = f; }
    // The constructor the parser chose for a local's `var` node - Sema's record, asked by the glue.
    void setLocalBuilt(std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &, bool &)> f) { localBuilt_ = f; }
    void setTemporaryBuilt(std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &)> f) { temporaryBuilt_ = f; }
    // A lambda's closure and its captures' members and sources - Sema's record, asked by the glue.
    void setClosure(std::function<bool(syntax::NodeId, sema::ClosureFacts &)> f) { closure_ = f; }
    void setReturnBuilt(std::function<bool(syntax::NodeId, int &, std::string &, TypeId &)> f) { returnBuilt_ = f; }
    void setNamedReturn(std::function<bool(syntax::NodeId)> f) { namedReturn_ = f; }
private:
    struct Outside { std::string why; explicit Outside(const std::string &w) : why(w) {} };
    struct Val { ValueId v; TypeId type; Val() {} Val(ValueId a, TypeId t) : v(a), type(t) {} };
    struct Loop { BlockId brk, cont; std::size_t handlers, live; };
    // A local a destructor destroys, alive from its construction to the end of its scope; `pad` is its cleanup's index in pads_.
    struct Live { ValueId slot; TypeId cls; std::string dtor; std::size_t pad; };
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
    void specFilterChain(const Pad &p);
    void br(BlockId to, const std::vector<ValueId> &args = std::vector<ValueId>());
    void condbr(ValueId c, BlockId t, BlockId f);
    ValueId param(BlockId b, TypeId type);
    ValueId iconst(TypeId type, long long k);
    ValueId fallOffValue(const Type &rt);   // the zero a body that falls off its end returns
    ValueId addrOf(const std::string &sym, TypeId type);
    ValueId load(ValueId p, TypeId type);
    void store(ValueId p, ValueId v);

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
    void returnStmt(sema::TypedId id);
    void returnConstructed(sema::TypedId value);
    void returnDestroyed(sema::TypedId value);
    void returnCopied(sema::TypedId id, sema::TypedId value);
    void namedReturnLocal(sema::TypedId var, ValueId slot, const Type &t);
    void forStmt(sema::TypedId id);
    void rangeForStmt(sema::TypedId id);
    void switchStmt(sema::TypedId id);
    BlockId labelBlock(const std::string &name);
    void tryStatement(sema::TypedId id);
    void microsoftTry(sema::TypedId id);
    void leaveHandlers(std::size_t n, std::size_t mark, BlockId target, const std::vector<ValueId> &args = std::vector<ValueId>());
    void checkFuncletUses(BlockId hb);
    BlockId padBlock(Pad &p);
    void resumeFrom(const Pad &p, std::size_t enclosing);
    void endCatches(std::size_t n);
    int typeIndexFor(const std::string &sym);
    ValueId entryAlloca(TypeId slotType, const std::string &name);
    Val runtimeCall(const std::string &symbol, TypeId returns, const std::vector<ValueId> &args);
    InstId callOrInvoke(TypeId carries, const std::vector<ValueId> &ops);

    // Expressions: an rvalue, or an object's address with its type.
    Val expr(sema::TypedId id);
    Val lvalue(sema::TypedId id);
    bool isLvalueNode(sema::TypedId id) const;
    bool referenceCast(sema::TypedId id) const;
    Val name(sema::TypedId id);
    Val nameAddress(sema::TypedId id);
    Val member(sema::TypedId id);
    Val memberAddress(sema::TypedId id);
    Val memberOfThis(sema::TypedId id);
    Val capturedThis();
    Val readOf(ValueId p, TypeId type);
    Val unary(sema::TypedId id);
    Val step(sema::TypedId id, bool increment, bool prefix);
    Val binary(const std::string &op, sema::TypedId l, sema::TypedId r);
    Val arithmetic(const std::string &op, Val a, Val b);
    Val logical(bool isAnd, sema::TypedId l, sema::TypedId r);
    Val conditional(sema::TypedId id);
    Val assign(sema::TypedId id);
    Val call(sema::TypedId id);
    Val newExpression(sema::TypedId id);
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
    bool simpleLayout(const Type &cls) const;
    bool builtMember(const Type &t) const;
    bool buildableLayout(const Type &cls, bool vptr = false) const;
    static bool scalarArray(const Type &t);
    void buildMember(ValueId at, const Member &m, sema::TypedId entry);
    void destroyMembers(const Type &cls);
    int baseNamed(const Type &cls, const std::string &name) const;
    ValueId baseAddress(const Type::BaseSpec &b);
    void buildBase(const Type::BaseSpec &b, sema::TypedId entry);
    void destroyBases(const Type &cls);
    void arguments(sema::TypedId id, std::uint32_t from, const Type &ft, std::size_t skip, bool variadic, std::vector<ValueId> &ops);
    void argumentList(const Type &ft, std::size_t skip, bool variadic, const std::vector<sema::TypedId> &args, bool dummyInt,
                      std::vector<ValueId> &ops);
    void directCall(const std::string &symbol, TypeId signature, const std::vector<ValueId> &ops);
    bool pseudoDestructor(sema::TypedId member);
    bool threadLocalGlobal(const std::string &symbol) const;
    Val toBase(Val v, TypeId to, bool cast = false);
    ValueId boundConverted(sema::TypedId an, const Type &p);
    Val baseOfObject(Val a, const Type &p);
    Val valueInitialised(sema::TypedId id);
    Val deleteDestroying(sema::TypedId id, const sema::NewFacts &f);
    ValueId virtualCallee(ValueId self, TypeId fnType, int slot);
    Val callTail(TypeId sig, CallInfo info, std::vector<ValueId> ops, std::size_t skip, const std::vector<sema::TypedId> &args,
                 bool dummyInt);
    bool operatorCalls(sema::TypedId id) const;
    Val operatorCall(sema::TypedId id);
    Val condition(sema::TypedId id);
    void throwExpr(sema::TypedId id);
    void microsoftThrow(sema::TypedId id);
    Val thrownValue(sema::TypedId id, TypeId &thrown);
    bool classThrow(sema::TypedId id, ValueId &object, TypeId &thrown);
    void throwCall(const std::string &symbol, const std::vector<ValueId> &args);
    void basePaths(const Type &cls, const Type &base, int at, int &found, int &offset);
    void memberInits(sema::TypedId fnNode, std::uint32_t from, std::uint32_t to);
    void storeVptrs();
    void classLocal(sema::TypedId var, ValueId slot, const Type &t);
    bool builtClass(const Type &t) const;
    void builtLocal(sema::TypedId var, ValueId slot, const Type &t);
    void guard(ValueId slot, const Type &c, const std::string &dtor, bool unwinds = true);
    void destroy(const Live &l, bool invoke = false);
    void destroyLive(std::size_t mark);
    void closeScope(std::size_t mark, bool keep = false);
    void scopedStmt(sema::TypedId id);
    void copyObject(ValueId to, ValueId from, const Type &t);
    bool plainTemporary(sema::TypedId id) const;
    bool constructedClass(const Type &t) const;
    bool placedClass(const Type &t) const;
    bool bytesReturned(const Type &t) const;
    bool destroyedReturned(const Type &t) const;
    bool copiedReturn(const Type &t) const;
    bool destroyedClass(const Type &t) const;
    bool builtTemporary(sema::TypedId id, bool destroyed) const;
    bool constructedTemporary(sema::TypedId id) const { return builtTemporary(id, false); }
    bool destroyedTemporary(sema::TypedId id) const { return builtTemporary(id, true); }
    bool temporaryNode(sema::TypedId id) const { return plainTemporary(id) || constructedTemporary(id) || destroyedTemporary(id); }
    std::string temporaryDestructor(const Type &c);
    bool destroyedResultClass(const Type &t) const;
    bool destroyedResult(sema::TypedId id) const;
    std::size_t openFull();
    void closeFull(std::size_t was);
    Val temporary(sema::TypedId id);
    void construct(ValueId where, const std::string &symbol, TypeId sig, const Type &cls, bool zeroFirst, sema::TypedId args, std::uint32_t from);
    bool objectNode(sema::TypedId id) const;
    Val object(sema::TypedId id);
    bool closureNode(sema::TypedId id) const;
    Val closure(sema::TypedId id);
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
    std::function<std::string(TypeId)> typeInfo_;
    std::function<std::string(syntax::NodeId)> unnamedCaught_;
    std::function<std::string(TypeId)> msThrowInfo_;
    std::function<bool(TypeId, std::string &, bool &)> msCatch_;
    BlockId funclet_;                           // the Microsoft handler whose funclet the blocks made now belong to
    BlockId msTryPad_;                          // the try that handler is of: its pad's CatchPad lists the exits
    std::function<bool(syntax::NodeId, sema::NewFacts &)> newFacts_;
    std::function<bool(const std::string &, const std::string &, std::uint32_t, std::string &, TypeId &, bool &)> memberBuilt_;
    std::function<std::string(TypeId)> destructor_;
    std::function<bool(const std::string &, std::vector<sema::VptrStore> &)> vptrStores_;
    std::vector<sema::VptrStore> vptrs_;        // what a constructor or destructor of a class with a vptr stores
    std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &, bool &)> localBuilt_;
    std::vector<Live> live_;                    // innermost last
    ValueId sret_;                              // the caller's result pointer, where a copied class is returned
    ValueId elidedSlot_;                        // the named return value, not destroyed by the `return` naming it
    std::function<bool(syntax::NodeId, int &, std::string &, TypeId &)> returnBuilt_;
    std::function<bool(syntax::NodeId)> namedReturn_;
    std::size_t liveFloor_;                     // a constructor's built bases and members: below it, kept by a return
    bool landingPads_;                          // the parser gave the function landing pads: the prologue names an LSDA
    std::function<bool(syntax::NodeId, std::string &, TypeId &, bool &)> temporaryBuilt_;
    std::function<bool(syntax::NodeId, sema::ClosureFacts &)> closure_;
    std::vector<Pad> pads_;                     // innermost last
    std::vector<std::string> typeTable_;        // the function's selector indices: one per type, first occurrence, 1-based
    std::size_t handlerDepth_;                  // handlers the statement being lowered is inside
    std::size_t fullFrom_;                      // live_ where the full expression being lowered began; kNoFull, none open
    std::size_t skippable_;                     // operands being lowered that may not be evaluated: `&&`, `||`, `?:`, `new`
    static const std::size_t kNoFull = static_cast<std::size_t>(-1);
    BlockId returnBlock_;
    std::map<std::uint32_t, std::pair<ValueId, TypeId> > locals_;    // a `var` node -> its alloca and declared type
    std::map<std::uint32_t, std::pair<std::string, TypeId> > staticLocals_;   // a static `var` -> its symbol and type
    std::vector<Loop> loops_;
    std::vector<SwitchCtx> switches_;
    std::map<std::string, BlockId> labels_;
    bool hasGoto_;
};

}   // namespace ir
