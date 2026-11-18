#pragma once

// The TMS320C6747 (C674x, C6000 family) as a code-generation target: C6000
// assembly text only, selected from the SSA IR (C6xSelect.cpp), delay slots as NOPs.
// The conventions, the ABI as emitted and the milestones: VM6747/TMS6747.md.

#include "Backend.h"
#include "Regions.h"
#include "../Operator.h"
#include "../ir/Ir.h"

#include "../optimizer/C6xMir.h"
#include "../optimizer/OptCosts.h"

#include <iosfwd>
#include <map>
#include <memory>
#include <sstream>
#include <string>

class Tms6747Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::UInt; }
    Kind wcharType() const override { return Kind::UShort; }   // 16-bit and unsigned: TI, measured
    bool resumeTakesException() const override { return false; }   // __cxa_end_cleanup(), measured from cl6x
    bool hasGetExceptionPtr() const override { return false; }     // not in rts6740_elf_eh.lib
    bool beginCatchReturnsObject() const override { return true; } // the object's address for a pointer too, measured on TI's simulator
    bool specInTable() const override { return true; }      // an FESD with the types, as cl6x writes
    int stackAlign() const override { return 8; }
    bool realignsFrame() const override { return false; }   // no `AND B15` prologue is built for the C6000

    bool loadsUnaligned() const override { return false; }   // LDW faults; no LDNW is emitted
    bool hasThreadStorage() const override { return false; }   // cl6x 8.2.2 refuses `thread_local`, measured 2026-10-20
    bool microsoftNames() const override { return false; }
    const char *name() const override { return "tms6747"; }
};

class Tms6747Backend final : public Backend {
public:
    const char *name() const override { return "tms6747"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink, Syntax) const override;
    // -g writes cl6x's `.dwpsn` before each statement: the source position vm6747 and sim6747 stop on.
    bool emitsLineTable(Syntax) const override { return true; }
private:
    Tms6747Target target_;
};

std::vector<ir::BlockId> ssaBlockOrder(const ir::Function &f);     // SsaSelect.cpp's, the one order of every selector

class Tms6747 final : public Regions {
public:
    Tms6747(std::ostream &sink, const Target &target, const Abi &abi)
        : sink_(sink), target_(target), abi_(abi) {}

    void run(const ir::Module &module) override;
    void emitLoc(int file, int line, int column) override;
    void setLineSource(const Source *s, const std::string &dir) override { holdLineSource(s, dir); }
    void vaStartAt();                         // A4 = &ap
    void vaArgAt(const Type *t);
    // Regions' hooks for a generator that walks: the selector writes its own pads and switches.
    void caseBranch(long long, const std::string &) override {}
    bool terminateScopes() const override { return true; }
    void setOptimize(int level) override;
    void setOptimizeForSize(bool size) override;
    void setSsa(const ir::Module *module, const TypeTable *types) override { ssa_ = module; ssaTypes_ = types; }

private:
    std::ostringstream out_;    // the piece being emitted (one function at a time)
    std::string file_;          // the finished pieces, in order
    int optimize_ = 0;          // -O1 and -O2 run the body through c6xSchedule
    bool forSize_ = false;      // -Os: the smaller code where a choice is offered - see constDivisor, earlyExit
    std::ostream &sink_;
    const Target &target_;
    const Abi &abi_;

    std::string functionName_;
    std::string returnLabel_;
    std::string labelPrefix_;

    // Gathered while the body is emitted, then used to shape the prologue: a
    // function that calls saves B3, and one that loads the callee-saved
    // argument registers (A10/B10/A12/B12, arguments 7-10) saves those too.
    bool hasCall_ = false;
    std::vector<int> areas_;                  // the call areas open, innermost last
    bool usesSavedArgRegs_ = false;
    bool usesSavedPairRegs_ = false;   // A11/B11/A13/B13, written by a 64-bit argument in A10-B12
    int frame_ = 0;                           // the locals, 8-aligned
    bool needsUnexpected_ = false;            // a noexcept function's table names __cxa_call_unexpected
    bool needsPr2_ = false;                   // a table past 64 KB names __c6xabi_unwind_cpp_pr2
    bool anyExidx_ = false;                   // a function got an exception-index entry, so the personality is named
    std::vector<std::string> savedRegs() const;
    unsigned unwindWord(bool needFrame) const;
    std::vector<unsigned> longUnwindWords() const;
    int sretSlot_ = 0;                        // where the caller's result pointer is kept
    std::size_t sretShift_ = 0;               // 1 when that pointer is A4 and the parameters start at B4
    std::size_t firstStack_ = 0;              // the first parameter passed on the stack
    int vaStart_ = 0;                         // the unnamed arguments, above the caller's B15

    const Type *returns_ = nullptr;           // the function's result type
    std::map<int, std::string> regOf_;        // a parameter's offset -> its home register
    std::vector<std::string> promoted_;       // the home registers used, to be saved and restored
    bool paramInFrame_ = false;               // a parameter reached through A15
    std::vector<int> ssaPlace_;               // -g: each of fn.locals()'s slot, -1 for none
    int saveBytes_ = 40;
    static std::string pairHigh(const std::string &r);
    void regWrite(const std::string &r, bool wide);   // the register = A4 (A5:A4)

    // A selected body counts its instructions instead (m6e's MIR: nothing in selectBody reads out_'s position).
    std::size_t emittedSize() override { return ssaCounting_ ? ssaInsts_ : static_cast<std::size_t>(out_.tellp()); }
    bool ssaCounting_ = false;
    std::size_t ssaInsts_ = 0;
    void defineLabel(const std::string &l) override;
    void jump(const std::string &l);
    // A5:A4 shifted by a constant, the pair's halves spliced through A3.
    void shiftPairLeft(int count);
    void shiftPairRight(int count, bool sign);
    std::string label(const char *kind, int id) const override;
    std::string userLabel(const std::string &name) const override;

    void unsupported(const char *what);
    void movImm(const char *reg, long long value);
    static std::string symName(const std::string &sym);
    void movSym(const char *reg, const std::string &sym);
    void regAdd(const char *base, int off, const char *dst); // dst = base + off
    void call(const std::string &target);     // B3 = return address; B target
    void copyBlock(int size, const char *from, const char *to, int align, bool wide = false, const char *pred = "");
    void copyBlockBatched(int size, const char *from, const char *to, int align, bool wide, const char *pred);
    bool inPair(const Type *t) const;         // a struct of 8 bytes or less: in registers, argument or result
    bool inPairWide(const Type *t) const;     // and one of 5 to 8 takes the pair
    bool hiddenInA4(const Type *t) const;     // a class non-trivial for calls: its result pointer is the first parameter
    void loadPair(int size, int align);       // A5:A4 = the struct at *A4
    void storePair(int size, int align);      // the struct at *A3 = A5:A4
    void spAdjust(int delta);
    void openArea(int area, bool stackArgs = false);
    void closeArea();                 // B15 += delta (negative allocates)
    void localAddr(int off, const char *dst); // dst = A15 - off
    static bool frameFits(int disp, int size);        // whether frameOperand can name it
    std::string frameOperand(int disp, int size);     // *-A15(k), *+A15(k), or the scaled form through A0
    int accessSize(const Type *t) const;              // the bytes one load or store of t moves
    bool isDouble(const Type *t) const;       // a 64-bit floating type
    bool isWide(const Type *t) const;         // any 64-bit scalar: it rides in A5:A4
    static std::string pairOf(const char *reg);  // "A4" -> "A5:A4"
    void fpBinary(BinOp op, bool dp);       // operands in A5:A4 / A7:A6
    void wideBinary(BinOp op, bool sign);     // 64-bit integers, the same places
    void wideCast(const Type *from, const Type *to);
    int stackParamOffset(const std::vector<ir::LaidParam> &ps, std::size_t i);
    int stackArg(const Type *t, int &end);
    std::string stackArgAccess(const Type *t, bool store, const std::string &mem);
    void load(const Type *t);                 // [A4] -> A4
    void loadFrom(const Type *t, const std::string &mem);   // mem -> A4
    void store(const Type *t, const std::string &mem);      // A4 -> mem
    void narrowInt(const Type *t);            // truncate A4 to t's width
    void emitGlobal(const Global &g, Segment seg);
    void emitData(const UnitData &data);
    void emitParams(const ir::Function &f);
    void emitVariables(const ir::Function &f);
    void emitScope(const ir::Function &f, int scope);
    bool declaresAnything(const ir::Function &f, int scope) const;
    // -g: every type a variable names, described once at the file's end (emitTypes) for the debugger
    std::vector<const Type *> debugTypes_;
    void noteType(const Type *t);
    void emitTypes();
    void emitExceptionTable(bool noexcept_, const std::vector<std::string> &spec);
    void emitFunction(const std::string &symbol);

    // **The SSA selector** (M6a, C6xSelect.cpp): a function inside its subset is written from the module, the rest
    // refused by name - ssaChoose says why. A value has a home register (ssaAssign, M6b) or a word above the parser's parameters;
    // A4, A6, B4 and A1 carry the rest. `CXX1_C6X_HOMES` 0 gives no homes, 1 the saved registers alone.
    const ir::Module *ssa_ = nullptr;
    const TypeTable *ssaTypes_ = nullptr;
    std::vector<int> ssaAllocaSlot_, ssaValueSlot_, ssaHome_;
    std::vector<int> ssaArgCopy_;     // by operand index: the frame copy of a class argument over 8 bytes (W5)      // ssaHome_: 0, or one past the home's index
    std::vector<ir::BlockId> ssaOrder_;                            // the order the blocks are written in
    std::vector<char> ssaFused_;                                   // 1: a compare written by its branch; 2: a cast remade at its calls
    std::vector<std::pair<std::string, const ir::Succ *> > ssaLater_;   // edge stubs owed after the next jump: a guard falls into its loop
    int ssaSelected_ = 0;
    void ssaChoose(const ir::Function &f);
    std::string ssaOutside(const ir::Function &f) const;
    // A type of the parser's frame record (ir::Laid), null where it has none.
    const Type *laidType(TypeId t) const { return t.valid() ? ssaTypes_->type(t) : nullptr; }
    const Type *ssaType(const ir::Function &f, ir::ValueId v) const { return ssaTypes_->type(f.value(v).type); }
    int layoutSsa(const ir::Function &f);
    int layoutSsaLocals(const ir::Function &f, int cur);
    int layoutSsaTemps(const ir::Function &f, int cur);
    void selectBody(const ir::Function &f);
    void selectInst(const ir::Function &f, const ir::Inst &i, ir::BlockId next);
    std::string ssaMem(int off, int size);                 // the frame word `off` above the saves, as an operand
    bool ssaFrameMem(const ir::Function &f, ir::ValueId p, int size, std::string &mem);   // *p where p is a frame address
    void ssaLoad(const ir::Function &f, ir::ValueId v, const char *reg, bool fresh = false);
    void ssaStore(const ir::Function &f, ir::ValueId v, const char *reg);
    void ssaMoves(const ir::Function &f, const ir::Succ &s);
    void ssaAssign(const ir::Function &f);
    const char *ssaHomeOf(ir::ValueId v) const;
    std::string ssaUse(const ir::Function &f, ir::ValueId v, const char *scratch);
    std::string ssaRead(const ir::Function &f, ir::ValueId v, const char *scratch);
    std::string ssaPut(c6x::MInst t);
    bool ssaSides_ = false;         // -O1 and up: homes on both files, read where they stand; CXX1_C6X_NOSIDES=1 is round 2's text
    bool ssaCompare(const ir::Function &f, const ir::Inst &c, const std::string &d);
    // The body as MIR (M6b): the selector's instructions, a walker helper's text read in where it was written (ssaAbsorb).
    c6x::MStream ssaMir_;
    std::string ssaHead_;       // what stood in out_ before a selected body
    int ssaRegionAt_ = 0;       // an invoke's row, counted in address order
    std::string ssaLandLabel(ir::BlockId b) const { return label("ssaland", static_cast<int>(b.v)); }
    void ssaAbsorb();
    void ssaEmit(const c6x::MInst &i) { ssaAbsorb(); ssaMir_.push_back(i); }
    void ssaBranchIf(bool zero, const std::string &l);
    void ssaNop(int n) { ssaEmit(c6x::MInst("NOP", n)); }
    bool ssaDeadClassLoad(const ir::Function &f, ir::ValueId v, int uses) const;     // a class read nothing uses
    bool ssaPairPastSixth(const ir::Function &f, const ir::Inst &i) const;       // a call's 64-bit argument in A10-B12
    bool ssaArrayDecay(const ir::Function &f, const ir::Inst &i) const;          // an array read, its address
    void ssaClassResult(const Type *t, bool built);     // a class result from its address in A4 (W3), unless built in place
    int ssaAlign(const ir::Function &f, ir::ValueId p) const;    // what a block op may assume of p (W5)
    void ssaFill(int size, const char *to, int align);         // memset to zero (W5)
    bool ssaCopiedArg(const Type *p) const { return p->isStructOrUnion() && !inPair(p) && !hiddenInA4(p); }
    // A 64-bit value (W4) has no home: eight bytes of the frame, read into a pair and written from A5:A4.
    bool ssaWideV(const ir::Function &f, ir::ValueId v) const { return v.valid() && f.value(v).type.valid() && isWide(ssaType(f, v)); }
    void ssaPair(const ir::Function &f, ir::ValueId v, const char *lo);    // v into lo's pair, or a word into lo
    void ssaKeep(const ir::Function &f, ir::ValueId v);                    // A5:A4, or A4, into v's place
    bool ssaWideInPlace(const ir::Function &f, const ir::Inst &i);         // a 64-bit operation on the home pairs
    bool ssaWideInst(const ir::Function &f, const ir::Inst &i);            // an instruction over 64-bit or floating values
    // A block inside a loop that is neither its head nor what its latch falls into: C6xSched reads through the label.
    std::vector<char> ssaInner_;
    std::string ssaBlockLabel(ir::BlockId b) const { return label(b.v < ssaInner_.size() && ssaInner_[b.v] ? "ssain" : "ssa", static_cast<int>(b.v)); }
};
