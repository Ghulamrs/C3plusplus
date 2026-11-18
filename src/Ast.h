#pragma once

#include "Dialect.h"
#include "Type.h"
#include "Mangle.h"
#include "Operator.h"
#include "Owned.h"
#include "syntax/SyntaxId.h"
#include "ir/Ir.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Num;
class Var;
class Assign;
class Unary;
class Binary;
class Call;
class Cast;
class Postfix;
class StrLit;
class VaStart;
class VaArg;
class MemberAccess;
class ExprStmt;
class Return;
class Block;
class If;
class While;
class For;
class Switch;
class Case;
class Goto;
class Label;
class Conditional;
class Comma;
class FuncletLeave;
class Try;

class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visit(const Num &) = 0;
    virtual void visit(const Var &) = 0;
    virtual void visit(const Assign &) = 0;
    virtual void visit(const Unary &) = 0;
    virtual void visit(const Binary &) = 0;
    virtual void visit(const Postfix &) = 0;
    virtual void visit(const Call &) = 0;
    virtual void visit(const Cast &) = 0;
    virtual void visit(const StrLit &) = 0;
    virtual void visit(const VaStart &) = 0;
    virtual void visit(const VaArg &) = 0;
    virtual void visit(const MemberAccess &) = 0;
    virtual void visit(const ExprStmt &) = 0;
    virtual void visit(const Return &) = 0;
    virtual void visit(const Block &) = 0;
    virtual void visit(const If &) = 0;
    virtual void visit(const While &) = 0;
    virtual void visit(const For &) = 0;
    virtual void visit(const Switch &) = 0;
    virtual void visit(const Case &) = 0;
    virtual void visit(const Goto &) = 0;
    virtual void visit(const Label &) = 0;
    virtual void visit(const Conditional &) = 0;
    virtual void visit(const Comma &) = 0;
    virtual void visit(const FuncletLeave &) = 0;
    virtual void visit(const Try &) = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(Visitor &v) const = 0;
};

class Expr : public Node {
public:
    const Type *type() const { return type_; }
    void setType(const Type *t) { type_ = t; }

    // **Set only by `static_cast<T &&>`, and a value category rather than a
    // type**: [basic.lval] separates an lvalue from an xvalue by what may be done
    // with the object, not by how its address is computed. No backend reads it.
    bool isXvalue() const { return xvalue_; }
    void setXvalue() { xvalue_ = true; }
    // **What `decltype` asks where C's categories answer wrong**: an assignment and a prefix `++` are
    // lvalues, a cast to a non-reference type a prvalue ([expr.ass]/1, [expr.cast]/1). Only decltype reads it.
    enum class Category { Natural, Lvalue, Prvalue };
    Category declCategory() const { return category_; }
    void setDeclCategory(Category c) { category_ = c; }
    // The syntax node the grammar recorded for this expression, if it recorded one (M2 step 2); nothing reads it yet.
    syntax::NodeId syntax() const { return syntax_; }
    void setSyntax(syntax::NodeId s) { syntax_ = s; }
    // The entity the parser resolved this expression to, as an index into its table (M2.16); 0 is none. No backend reads it.
    std::uint32_t resolved() const { return resolved_; }
    void setResolved(std::uint32_t r) { resolved_ = r; }
private:
    const Type *type_ = nullptr;
    bool xvalue_ = false;
    Category category_ = Category::Natural;
    syntax::NodeId syntax_;
    std::uint32_t resolved_ = 0;
};

class Stmt : public Node {
public:
    std::size_t pos() const { return pos_; }
    void setPos(std::size_t p) { pos_ = p; }
    // The syntax node the grammar recorded for this statement (M2 step 2); nothing reads it yet.
    syntax::NodeId syntax() const { return syntax_; }
    void setSyntax(syntax::NodeId s) { syntax_ = s; }
private:
    std::size_t pos_ = 0;
    syntax::NodeId syntax_;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

class Num final : public Expr {
public:
    explicit Num(long long v) : value_(v) {}

    explicit Num(long double d) : dvalue_(d) {}
    long long value() const { return value_; }
    long double dvalue() const { return dvalue_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    long long value_ = 0;
    long double dvalue_ = 0;
};

class Var final : public Expr {
public:
    static Var *local(std::string name, int offset) { return new Var(std::move(name), true, offset); }
    static Var *global(std::string name) { return new Var(std::move(name), false, 0); }

    const std::string &name() const { return name_; }

    // What the linker is told, which is the mangled name once a program has
    // C++ linkage. name() stays what the programmer wrote, because that is
    // what a diagnostic and a debugger should say.
    const std::string &symbol() const { return symbol_.empty() ? name_ : symbol_; }
    void setSymbol(std::string s) { symbol_ = std::move(s); }
    bool isLocal() const { return isLocal_; }
    int offset() const { return offset_; }
    bool readOnly() const { return readOnly_; }
    void setReadOnly(bool r) { readOnly_ = r; }
    bool noAddress() const { return noAddress_; }
    void setNoAddress(bool n) { noAddress_ = n; }
    // **An object with thread storage duration** - [basic.stc.thread] - reached through the thread
    // pointer rather than by its own address; `viaWrapper` says to go through the ABI's wrapper
    // function (Itanium's _ZTW) or its guard (Microsoft's __tls_guard) first, see Parser::tlsVar.
    bool isThreadLocal() const { return threadLocal_; }
    void setThreadLocal(bool t) { threadLocal_ = t; }
    bool viaWrapper() const { return viaWrapper_; }
    void setViaWrapper(bool w) { viaWrapper_ = w; }
    void accept(Visitor &v) const override { v.visit(*this); }

private:
    Var(std::string name, bool isLocal, int offset)
        : name_(std::move(name)), isLocal_(isLocal), offset_(offset) {}
    std::string name_;
    std::string symbol_;
    bool isLocal_;
    int offset_;
    bool readOnly_ = false;
    bool noAddress_ = false;
    bool threadLocal_ = false;
    bool viaWrapper_ = false;
};

class StrLit final : public Expr {
public:
    StrLit(std::string label, std::string text)
        : label_(std::move(label)), text_(std::move(text)) {}
    const std::string &label() const { return label_; }
    const std::string &text() const { return text_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string label_;
    std::string text_;
};

class VaStart final : public Expr {
public:
    explicit VaStart(ExprPtr list) : list_(std::move(list)) {}
    const Expr &list() const { return *list_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr list_;
};

class VaArg final : public Expr {
public:
    explicit VaArg(ExprPtr list) : list_(std::move(list)) {}
    const Expr &list() const { return *list_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr list_;
};

class Assign final : public Expr {
public:
    Assign(ExprPtr target, ExprPtr value)
        : target_(std::move(target)), value_(std::move(value)) {}
    const Expr &target() const { return *target_; }
    const Expr &value() const { return *value_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr target_, value_;
};

class Unary final : public Expr {
public:
    Unary(char op, ExprPtr operand) : op_(op), operand_(std::move(operand)) {}
    char op() const { return op_; }
    const Expr &operand() const { return *operand_; }
    ExprPtr &operandSlot() { return operand_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    char op_;
    ExprPtr operand_;
};

class Binary final : public Expr {
public:
    Binary(BinOp op, ExprPtr lhs, ExprPtr rhs)
        : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}
    BinOp op() const { return op_; }
    const Expr &lhs() const { return *lhs_; }
    const Expr &rhs() const { return *rhs_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    BinOp op_;
    ExprPtr lhs_, rhs_;
};

class Call final : public Expr {
public:
    Call(std::string name, ExprPtr callee, std::vector<ExprPtr> args, bool variadic,
         int resultSlot = 0, int namedArgs = -1, std::vector<int> argSlots = {})
        : name_(std::move(name)), callee_(std::move(callee)),
          args_(std::move(args)), variadic_(variadic), resultSlot_(resultSlot),
          namedArgs_(namedArgs < 0 ? static_cast<int>(args_.size()) : namedArgs),
          argSlots_(std::move(argSlots)) {}
    const std::string &name() const { return name_; }
    const std::string &symbol() const { return symbol_.empty() ? name_ : symbol_; }
    void setSymbol(std::string s) { symbol_ = std::move(s); }
    const std::vector<ExprPtr> &args() const { return args_; }
    std::vector<ExprPtr> &argsSlots() { return args_; }
    const Expr *callee() const { return callee_.get(); }
    bool isVariadic() const { return variadic_; }
    int resultSlot() const { return resultSlot_; }
    // Copy elision points this at the variable being initialised, so the callee builds its result there and no copy constructor runs.
    void setResultSlot(int s) { resultSlot_ = s; }
    // **The result slot holds a pointer to where the result goes**: `return f();` hands f the hidden pointer
    // this function was given, so the object is built in its caller's storage once.
    bool resultIndirect() const { return resultIndirect_; }
    void setResultIndirect(bool b) { resultIndirect_ = b; }

    int argSlot(std::size_t i) const {
        return i < argSlots_.size() ? argSlots_[i] : 0;
    }

    // **Whether the first argument is a `this` pointer**, which the Microsoft ABI
    // needs and the Itanium one does not: cl puts `this` first and the hidden
    // return pointer second. A lowered member call needs this to be told apart.
    bool hasThis() const { return hasThis_; }
    void setHasThis(bool t) { hasThis_ = t; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string name_;
    std::string symbol_;
    ExprPtr callee_;
    std::vector<ExprPtr> args_;
    bool variadic_;
    int resultSlot_;
    int namedArgs_;
    std::vector<int> argSlots_;
    bool hasThis_ = false;
    bool resultIndirect_ = false;
};

class Postfix final : public Expr {
public:
    Postfix(ExprPtr target, bool, long long step)
        : target_(std::move(target)), step_(step) {}
    const Expr &target() const { return *target_; }
    long long step() const { return step_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr target_;
    long long step_;
};

class Cast final : public Expr {
public:
    Cast(const Type *to, ExprPtr value) : value_(std::move(value)) { setType(to); }
    const Expr &value() const { return *value_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr value_;
};

class Comma final : public Expr {
public:
    Comma(ExprPtr left, ExprPtr right)
        : left_(std::move(left)), right_(std::move(right)) {}
    const Expr &left() const { return *left_; }
    const Expr &right() const { return *right_; }
    Expr &right() { return *right_; }
    Expr &left() { return *left_; }
    ExprPtr &rightSlot() { return right_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr left_, right_;
};

// **The call a class prvalue comes from**, through the commas in front of it - a virtual call's saved
// object address, `(a, f())` - so its result slot is elided into or bound to as a bare call's is.
inline Call *producingCall(Expr *e) {
    while (Comma *c = dynamic_cast<Comma *>(e)) e = &c->right();
    return dynamic_cast<Call *>(e);
}
inline const Call *producingCall(const Expr *e) { return producingCall(const_cast<Expr *>(e)); }

class Conditional final : public Expr {
public:
    Conditional(ExprPtr cond, ExprPtr thenArm, ExprPtr elseArm)
        : cond_(std::move(cond)), then_(std::move(thenArm)),
          else_(std::move(elseArm)) {}
    const Expr &cond() const { return *cond_; }
    const Expr &thenArm() const { return *then_; }
    const Expr &elseArm() const { return *else_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_, then_, else_;
};

class MemberAccess final : public Expr {
public:
    MemberAccess(ExprPtr object, std::string name, int offset,
                 int width = 0, int bitOffset = 0)
        : object_(std::move(object)), name_(std::move(name)), offset_(offset),
          width_(width), bitOffset_(bitOffset) {}
    const Expr &object() const { return *object_; }
    const std::string &name() const { return name_; }
    int offset() const { return offset_; }
    int width() const { return width_; }
    int bitOffset() const { return bitOffset_; }
    bool isBitField() const { return width_ != 0; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr object_;
    std::string name_;
    int offset_;
    int width_;
    int bitOffset_;
};

class ExprStmt final : public Stmt {
public:
    explicit ExprStmt(ExprPtr e) : expr_(std::move(e)) {}
    const Expr &expr() const { return *expr_; }
    // **Handing the expression back out.**
    ExprPtr release() { return std::move(expr_); }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr expr_;
};

class Return final : public Stmt {
public:
    explicit Return(ExprPtr value) : value_(std::move(value)) {}
    bool hasValue() const { return value_ != nullptr; }
    const Expr &value() const { return *value_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr value_;
};

class Block final : public Stmt {
public:
    explicit Block(std::vector<StmtPtr> body) : body_(std::move(body)) {}
    const std::vector<StmtPtr> &body() const { return body_; }
    // For the one block that is filled in after it is placed.
    void append(StmtPtr s) { body_.push_back(std::move(s)); }

    void setScope(int s) { scope_ = s; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::vector<StmtPtr> body_;
    int scope_ = -1;
};

class If final : public Stmt {
public:
    If(ExprPtr cond, StmtPtr thenArm, StmtPtr elseArm)
        : cond_(std::move(cond)), then_(std::move(thenArm)), else_(std::move(elseArm)) {}
    const Expr &cond() const { return *cond_; }
    const Stmt &thenArm() const { return *then_; }
    const Stmt *elseArm() const { return else_.get(); }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_;
    StmtPtr then_, else_;
};

class While final : public Stmt {
public:
    While(ExprPtr cond, StmtPtr body)
        : cond_(std::move(cond)), body_(std::move(body)) {}
    const Expr &cond() const { return *cond_; }
    const Stmt &body() const { return *body_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_;
    StmtPtr body_;
};

class For final : public Stmt {
public:
    For(StmtPtr init, ExprPtr cond, ExprPtr step, StmtPtr body)
        : init_(std::move(init)), cond_(std::move(cond)),
          step_(std::move(step)), body_(std::move(body)) {}
    const Stmt *init() const { return init_.get(); }
    const Expr *cond() const { return cond_.get(); }
    const Expr *step() const { return step_.get(); }
    const Stmt &body() const { return *body_; }

    void setScope(int s) { scope_ = s; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    StmtPtr init_;
    ExprPtr cond_, step_;
    StmtPtr body_;
    int scope_ = -1;
};

class Case final : public Stmt {
public:
    Case(long long value, bool isDefault, int id, StmtPtr body)
        : value_(value), isDefault_(isDefault), id_(id), body_(std::move(body)) {}
    long long value() const { return value_; }
    bool isDefault() const { return isDefault_; }
    int id() const { return id_; }
    const Stmt &body() const { return *body_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    long long value_;
    bool isDefault_;
    int id_;
    StmtPtr body_;
};

class Switch final : public Stmt {
public:
    Switch(ExprPtr cond, StmtPtr body, std::vector<const Case *> cases,
           const Case *)
        : cond_(std::move(cond)), body_(std::move(body)),
          cases_(std::move(cases)) {}
    const Expr &cond() const { return *cond_; }
    const Stmt &body() const { return *body_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_;
    StmtPtr body_;
    std::vector<const Case *> cases_;
};

class Goto final : public Stmt {
public:
    explicit Goto(std::string label) : label_(std::move(label)) {}
    const std::string &label() const { return label_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string label_;
};

class Label final : public Stmt {
public:
    Label(std::string name, StmtPtr body)
        : name_(std::move(name)), body_(std::move(body)) {}
    const std::string &name() const { return name_; }
    const Stmt &body() const { return *body_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string name_;
    StmtPtr body_;
};

// **Leaving a Microsoft handler funclet early**: the runtime carries on at the
// label named, which the parser laid down in the parent after the `try`.
class FuncletLeave final : public Stmt {
public:
    explicit FuncletLeave(std::string label) : label_(std::move(label)) {}
    const std::string &label() const { return label_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string label_;
};

struct Param {
    const Type *type;
    int offset;
};

// `try { ... } catch (T e) { ... }` - rung 6.3, and almost all of it is ordinary
// statements. Itanium wants the labels bounding the body, the pad, and the
// type_info symbols in order; Microsoft keeps each handler whole, to be called.
struct MsHandler {
    std::string descriptor;    // ??_R0H@8 and the like; empty for catch (...)
    int objectSlot = 0;        // frame slot for the caught object, 0 if unnamed
    int objectSize = 0;
    bool byReference = false;  // the slot takes the runtime's pointer, not a copy
    bool constPointer = false; // `catch (const T *)`: HT_IsConst, the const off the name
    StmtPtr body;
    std::size_t pos = 0;       // where `catch` is written: the line the funclet's entry is named by under -g (1.29)
};

class Try final : public Stmt {
public:
    // The body is a *list* and not a block, because a cleanup region covers a
    // slice of an enclosing block's statements and must not open a scope of its
    // own - the objects it destroys belong to the block outside it.
    Try(std::vector<StmtPtr> body, StmtPtr pad, int, int,
        std::vector<std::string> types)
        : body_(std::move(body)), pad_(std::move(pad)),
          types_(std::move(types)) {}
    const std::vector<StmtPtr> &body() const { return body_; }

    // **Null on Windows.**
    bool hasPad() const { return pad_ != nullptr; }
    const Stmt &pad() const { return *pad_; }
    const std::vector<std::string> &types() const { return types_; }
    // **Set late, and only for a `try` body's cleanup rows.**
    void setTypes(std::vector<std::string> t) { types_ = std::move(t); }

    const std::vector<MsHandler> &handlers() const { return handlers_; }
    void setHandlers(std::vector<MsHandler> h) { handlers_ = std::move(h); }

    // **A cleanup region, where the Microsoft ABI wants a state rather than a
    // handler.** Nothing is caught here, and it holds what *this* region built
    // and no more: the runtime chains to the region before it.
    const Stmt *cleanup() const { return cleanup_.get(); }
    void setCleanup(StmtPtr c) { cleanup_ = std::move(c); }

    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::vector<StmtPtr> body_;
    StmtPtr pad_;
    std::vector<std::string> types_;
    std::vector<MsHandler> handlers_;
    StmtPtr cleanup_;
};

struct Local {
    std::string name;
    const Type *type;
    int offset;
    bool isParam;

    std::string staticName;

    int scope;
    int align = 0;      // the slot's alignment, alignas included: what a backend laying the frame out must keep
    int serial = -1;    // the parser's serial for it, which a Resolved record of the local names (M3b.2)
    const Type *declared = nullptr;   // a reference's own type, for DWARF: `type` is the pointer it is stored as

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    Local(std::string name = std::string(), const Type *type = nullptr, int offset = 0,
          bool isParam = false, std::string staticName = std::string(), int scope = 0)
        : name(std::move(name)), type(type), offset(offset), isParam(isParam),
          staticName(std::move(staticName)), scope(scope) {}
};

class Function {
public:
    Function(std::string name, const Type *returns, std::vector<Param> params,
             StmtPtr body, int frameSize, bool isStatic, int sretSlot = 0,
             bool variadic = false, int regSaveSlot = 0, std::size_t pos = 0,
             std::vector<Local> locals = std::vector<Local>())
        : name_(std::move(name)), returns_(returns), params_(std::move(params)),
          body_(std::move(body)), frameSize_(frameSize), isStatic_(isStatic),
          sretSlot_(sretSlot), variadic_(variadic), regSaveSlot_(regSaveSlot),
          pos_(pos), locals_(std::move(locals)) {}
    const std::string &name() const { return name_; }
    const std::string &symbol() const { return symbol_.empty() ? name_ : symbol_; }
    void setSymbol(std::string s) { symbol_ = std::move(s); }
    // **The name a debugger shows** (1.29): a specialization's as cl spells it - `biggest<int>`, `Counter::head<3>` - else the plain name, the backend qualifying it from the symbol.
    const std::string &displayName() const { return displayName_.empty() ? name_ : displayName_; }
    // Whether one was given (it may spell the same as the name: `geo::Tally::~Tally`, which needs no qualifying).
    bool hasDisplayName() const { return !displayName_.empty(); }
    void setDisplayName(std::string s) { displayName_ = std::move(s); }

    // The definition side of Call::hasThis - the same question asked of the
    // function being emitted rather than of a call to one.
    bool hasThis() const { return hasThis_; }
    void setHasThis(bool t) { hasThis_ = t; }

    // **Implicitly inline** - [dcl.inline]/6 - so the definition may appear in
    // several translation units and the linker has to fold the copies rather
    // than reject them.
    bool isInline() const { return isInline_; }
    void setInline(bool v) { isInline_ = v; }

    // A second name for the same code, emitted as an extra label in front of it.
    // Itanium gives a constructor C1 for a complete object and C2 for a base and
    // clang emits both; empty for everything else, Microsoft included.
    const std::string &alias() const { return alias_; }
    void setAlias(std::string a) { alias_ = std::move(a); }
    const Type *returns() const { return returns_; }
    const std::vector<Param> &params() const { return params_; }
    const Stmt &body() const { return *body_; }
    int frameSize() const { return frameSize_; }
    // **What the frame must be aligned to**: the stack's 16 unless a local asked for more with
    // `alignas` or its type, when the prologue realigns the frame to it - [basic.align], measured
    // from clang's `and $-64, %rsp`. The parser records the widest slot it allocated.
    int frameAlign() const { return frameAlign_; }
    void setFrameAlign(int a) { frameAlign_ = a; }
    bool isStatic() const { return isStatic_; }
    int sretSlot() const { return sretSlot_; }
    bool isVariadic() const { return variadic_; }

    int regSaveSlot() const { return regSaveSlot_; }

    std::size_t pos() const { return pos_; }
    const std::vector<Local> &locals() const { return locals_; }

    const std::vector<int> &blocks() const { return blocks_; }
    const std::vector<std::size_t> &blockPositions() const { return blockAt_; }     // where each opens: ir::Laid::blockAt reads them
    void setBlocks(std::vector<int> b, std::vector<std::size_t> at) { blocks_ = std::move(b); blockAt_ = std::move(at); }

    // Whether this function has a landing pad in it, which the prologue has
    // to know before the body is walked: the personality routine and the LSDA
    // are named there.
    bool hasLandingPads() const { return landingPads_; }
    void setHasLandingPads(bool b) { landingPads_ = b; }
    // Declared noexcept (or throw()): a table that says so lets the runtime end the program itself.
    bool isNoexcept() const { return noexcept_; }
    void setNoexcept(bool b) { noexcept_ = b; }
    // `throw(T...)`'s type_info symbols, for a target whose table states the list; empty where none was written.
    const std::vector<std::string> &exceptionSpec() const { return spec_; }
    void setExceptionSpec(std::vector<std::string> s) { spec_ = std::move(s); }
    // The kind of a function the compiler wrote with no source body - an implicit special member, a thunk, an init
    // function - and empty for one the grammar read (M2.15): the census of what the typed tree covers reads it.
    const std::string &synthesised() const { return synthesised_; }
    void setSynthesised(const char *kind) { synthesised_ = kind; }
private:
    std::string synthesised_;
    bool noexcept_ = false;
    std::vector<std::string> spec_;
    bool hasThis_ = false;
    bool isInline_ = false;
    std::string name_;
    std::string symbol_;
    std::string displayName_;
    std::string alias_;
    const Type *returns_;
    std::vector<Param> params_;
    StmtPtr body_;
    int frameSize_;
    int frameAlign_ = 16;
    bool isStatic_;
    int sretSlot_;
    bool variadic_;
    int regSaveSlot_;
    std::size_t pos_;
    std::vector<Local> locals_;
    std::vector<int> blocks_;
    std::vector<std::size_t> blockAt_;
    bool landingPads_ = false;
};

struct Program {
    std::vector<Function> functions;
    // **Whether the file wrote `volatile`**: the type system drops it, so -O is off (Driver::compile).
    bool usesVolatile = false;
    // Where the first `volatile` is, `file:line:col`, for the warning that says so.
    std::string volatileAt;
    // **Every function lowered to the SSA IR** (M3b.7), which the x86 generator selects from, found by symbol.
    ir::Module module;
};
