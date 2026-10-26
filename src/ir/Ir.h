#pragma once

// **The SSA IR** - M3b.1, doc-cxx3/M3-IR.md part 3. One arena per function for values, instructions and blocks, one
// module per translation unit; every handle a wrapped index numbered in creation order, 0 none, so two runs make the
// same IR (design section 2). Types are TypeId handles into the front end's TypeTable (R6.3); no raw pointer here.

#include "../Type.h"
#include "../syntax/SyntaxId.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ir {

struct ValueId { std::uint32_t v; ValueId() : v(0) {} explicit ValueId(std::uint32_t x) : v(x) {} bool valid() const { return v != 0; } };
struct BlockId { std::uint32_t v; BlockId() : v(0) {} explicit BlockId(std::uint32_t x) : v(x) {} bool valid() const { return v != 0; } };
struct InstId  { std::uint32_t v; InstId()  : v(0) {} explicit InstId(std::uint32_t x)  : v(x) {} bool valid() const { return v != 0; } };
struct FuncId  { std::uint32_t v; FuncId()  : v(0) {} explicit FuncId(std::uint32_t x)  : v(x) {} bool valid() const { return v != 0; } };

// The instruction set of M3-IR 3.2, in its table's order.
enum class Op : std::uint8_t {
    IConst, FConst, Null, Addr, TlsAddr, Undef,
    Alloca, Load, Store, Memcpy, Memset, Gep,
    Add, Sub, Mul, SDiv, UDiv, SRem, URem, And, Or, Xor, Shl, LShr, AShr, Neg, Not, ICmp,
    FAdd, FSub, FMul, FDiv, FNeg, FCmp,
    Trunc, SExt, ZExt, FpToSi, FpToUi, SiToFp, UiToFp, FpExt, FpTrunc, Bitcast, PtrToInt, IntToPtr,
    Br, CondBr, Switch, Ret, Unreachable,
    Call, Invoke,
    CatchSwitch, CatchPad, CleanupPad, CatchRet, CleanupRet, Rethrow,
    Select, VaStart, VaArg, VaEnd, Trap
};

// icmp's ten conditions, then fcmp's: ordered forms, and `Une` (not equal or unordered), which `!=` on floats wants.
enum class Cmp : std::uint8_t { Eq, Ne, Slt, Sle, Sgt, Sge, Ult, Ule, Ugt, Uge, Oeq, Olt, Ole, Ogt, Oge, Une };

std::string opName(Op op);
std::string cmpName(Cmp cc);
bool isTerminator(Op op);

// A branch target with its block arguments, a range in Function::operands.
struct Succ { BlockId block; std::uint32_t first, count; Succ() : first(0), count(0) {} };

// The facts a call carries (M3-IR 3.5): Ast.h's Call less the frame slots.
struct CallInfo {
    std::string symbol;     // empty for a call through a value, which is then the first operand
    TypeId signature;       // the function type called through
    bool variadic, hasThis, sret, noexcept_;
    int namedArgs;
    CallInfo() : variadic(false), hasThis(false), sret(false), noexcept_(false), namedArgs(0) {}
};

struct Value { TypeId type; InstId def; BlockId param; std::uint16_t index; Value() : index(0) {} };

struct Inst {
    Op op;
    TypeId type;                    // the result's; none for an instruction with no result
    std::uint32_t first, count;     // operands in Function::operands
    ValueId result;
    long long k;                    // an integer constant, a gep's constant offset, an alloca's frame index, a switch's case count
    unsigned long long bits;        // fconst's IEEE bits
    std::string sym;                // addr's and tlsaddr's symbol
    std::vector<std::string> syms;  // a catchpad's or cleanuppad's type_info chain, "" for catch(...); `k` its own handlers' count
    Cmp cc;
    std::vector<Succ> succ;         // br: one; condbr: true then false; switch: default then one per case value in `cases`
    std::vector<long long> cases;
    std::uint32_t call;             // an index into Function::calls for Call and Invoke
    bool isVolatile;
    int align;                      // an alloca's
    syntax::NodeId at;
    std::size_t pos;                // the source offset, for -g
    Inst() : op(Op::Undef), first(0), count(0), k(0), bits(0), cc(Cmp::Eq), call(0), isVolatile(false), align(0), pos(0) {}
};

struct Block { std::vector<ValueId> params; std::vector<InstId> insts; InstId term; bool pad; Block() : pad(false) {} };

struct AllocaInfo { TypeId type; int align; std::string name; AllocaInfo() : align(0) {} };

struct Function {
    std::string symbol, displayName;
    bool isStatic, isInline, variadic, hasThis, noexcept_;
    TypeId returns;
    std::vector<ValueId> params;
    ValueId sret;
    std::vector<AllocaInfo> frame;
    BlockId entry;
    std::vector<BlockId> blocks;
    std::vector<std::string> spec;
    std::string synthesised;
    int frameAlign;
    std::size_t pos;
    // The arenas, index 0 none.
    std::vector<Value> values;
    std::vector<Inst> insts;
    std::vector<Block> blockArena;
    std::vector<ValueId> operands;
    std::vector<CallInfo> calls;
    Function() : isStatic(false), isInline(false), variadic(false), hasThis(false), noexcept_(false), frameAlign(16), pos(0) {
        values.push_back(Value()); insts.push_back(Inst()); blockArena.push_back(Block()); calls.push_back(CallInfo());
    }
    const Value &value(ValueId id) const { return values[id.v]; }
    const Inst &inst(InstId id) const { return insts[id.v]; }
    const Block &block(BlockId id) const { return blockArena[id.v]; }
    ValueId operand(const Inst &i, std::uint32_t n) const { return operands[i.first + n]; }
    ValueId arg(const Succ &s, std::uint32_t n) const { return operands[s.first + n]; }
};

struct Module {
    std::vector<Function> functions;    // index 0 none
    Module() { functions.push_back(Function()); }
    // The function of a symbol, or none.
    FuncId find(const std::string &symbol) const;
};

// **-dump=ssa**: every function of the module in the format of M3-IR 3.3, one instruction a line.
std::string dump(const Module &m, const TypeTable &types);
// **-check=ssa**: the verifier of M3-IR 3.4. Faults are appended to `out`, one a line, naming function and block.
int verify(const Module &m, const TypeTable &types, std::string &out);

}   // namespace ir
