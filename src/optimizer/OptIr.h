#pragma once

// **The optimizer's view of one function**: the instructions the walker wrote,
// each operand parsed once into registers, displacements and immediates, and
// kept in one ordered stream with the labels and the events between them.

#include "OptCore.h"
#include "OptStrings.h"
#include "OptTable.h"
#include "../backend/Spelling.h"

#include <cstdint>
#include <string>
#include <vector>

namespace opt {

// The general registers in encoding order, then xmm0-15 as 16-31.
enum : int { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
constexpr int kGprs = 16;
constexpr int kXmm0 = 16;
constexpr int kRegs = 32;


// **rsp and rbp are the frame**: no pass narrows, renames or retargets them.
constexpr bool frameReg(int r) { return r == RSP || r == RBP; }

// A register's number and the width named, in bytes; -1 for one this does not
// model (the x87 stack). The name for a number and width, for writing back.
struct Reg {
    int id;
    int width;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    Reg(int id = -1, int width = 0) : id(id), width(width) {}
};
Reg parseReg(Str name);
const char *regName(int id, int width);

struct Operand {
    // SegMemory and TlsSymbol are Op::Seg and Op::Tls: memory a thread reaches through its own
    // pointer. Neither is a Memory to the passes that fold or forward through one - `isMem()` says
    // no - but a TlsSymbol names its base register wherever a Memory would, so renaming reaches it.
    enum Kind { None, Register, Immediate, Memory, RipSymbol, Indirect, Label, SegMemory, TlsSymbol };
    Kind kind = None;
    Reg reg;                  // Register; Indirect's target; Memory's and TlsSymbol's base
    long long disp = 0;       // Memory
    bool hasDisp = false;
    Reg index;                // Memory: `disp(base,index,scale)`; scale 0 is none
    int scale = 0;
    long long value = 0;      // Immediate, when numeric
    bool unsignedSpelling = false;   // written as imm(unsigned) past LLONG_MAX: spelled back so at -O0 (M3a.1)
    bool numeric = false;
    StrId text;               // an Immediate that is not numeric, a symbol, a label: an index, never a string (M3a.1)

    static Operand from(const Op &o);
    static Operand ofReg(int id, int width);
    static Operand ofImm(long long v);
    static Operand ofMem(int base, long long disp);
    Op op() const;            // valid while this operand is
    bool isReg(int id) const { return kind == Register && reg.id == id; }
    bool isMem() const { return kind == Memory; }
    // Whether `reg` is a base register this operand reads: a Memory's or a TlsSymbol's.
    bool basedOnReg() const { return kind == Memory || kind == TlsSymbol; }
    bool indexed() const { return kind == Memory && scale != 0; }
};

struct Instr {
    OpId m;                   // an index into the opcode table, never a string (M3a.1)
    Operand a, b;             // AT&T order: a is the source, b the destination
    int operands;
    // A call's argument registers as the walker placed them, or a ret's result registers where the walker narrowed
    // them; without the word, the convention's whole set is read.
    RegSet args = 0;
    bool exactArgs = false;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    Instr(OpId m = OpId(), Operand a = Operand(), Operand b = Operand(), int operands = 0)
        : m(m), a(a), b(b), operands(operands) {}
};

// **A spelling call that is not an instruction, held as data** (M3a.1, doc-cxx3/M3-IR.md 2.4): its kind and its
// arguments, replayed as the same call where it stood. FuncletBegin, FuncletLeave and FuncletEnd bound a Microsoft
// funclet's blocks inside its function's stream (M3a.4): a block entered from nowhere, then one that returns.
struct Directive {
    enum Kind : std::uint8_t {
        Prologue, FileEntry, Location, FuncletLinesBegin, FuncletLinesEnd, Globl, WeakDefinition, ObjectType,
        ObjectSize, TextSection, RodataSection, RelroSection, DataSection, BssSection, TlsDataSection, TlsBssSection,
        TlsInitialiserEntry, InitialiserEntry, Align, LoopAlign, Zero, DataInt, DataSym, DataBytes, NoteHasEh,
        FuncletBegin, FuncletLeave, FuncletEnd
    };
    Kind kind = Align;
    long long n[4] = {0, 0, 0, 0};     // the integer arguments, in the call's order
    std::string s;                     // the string argument: a name, a symbol, the bytes, the LSDA, a funclet's symbol
    std::string t;                     // FuncletBegin's second: the location of the funclet's entry under -g
    bool withSaves = false;            // Prologue: the saves finish-frame settled, said by calleeSaves first
    std::vector<SavedReg> saves;

    Directive() {}
    explicit Directive(Kind k) : kind(k) {}
    void replay(Spelling &s) const;
    std::string describe() const;
    // Whether this begins a funclet, or ends one (FuncletEnd and FuncletLeave alike: the block returns).
    bool beginsFunclet() const { return kind == FuncletBegin; }
    bool endsFunclet() const { return kind == FuncletEnd || kind == FuncletLeave; }
    bool leavesFunclet() const { return kind == FuncletLeave; }
};

// How a directive moves control: a funclet's end returns, its beginning starts a block, the rest do nothing.
Control controlOf(const Directive &d);

// Everything between functionBegin and functionEnd: instructions, labels, and directives, replayed where they stood.
using Entry = EntryOf<Instr, Directive>;
using Stream = std::vector<Entry>;

}
