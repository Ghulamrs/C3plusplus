#pragma once

// **MIR, the optimizer's IR** (doc-cxx3/M3-IR.md part 2): the instructions the walker wrote, each operand parsed once
// into registers, displacements and immediates, kept in one ordered stream with the labels and the directives between
// them, at every level. The passes are opt's; the webs, locals and allocator that colour it are mir's (MirWebs.h).

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

struct MOperand {
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
    bool unsignedSpelling = false;   // written as imm(unsigned) past LLONG_MAX: spelled back so at every level (M3a.3)
    bool numeric = false;
    StrId text;               // an Immediate that is not numeric, a symbol, a label: an index, never a string (M3a.1)

    static MOperand from(const Op &o);
    static MOperand ofReg(int id, int width);
    static MOperand ofImm(long long v);
    static MOperand ofMem(int base, long long disp);
    Op op() const;            // valid while this operand is
    bool isReg(int id) const { return kind == Register && reg.id == id; }
    bool isMem() const { return kind == Memory; }
    // Whether `reg` is a base register this operand reads: a Memory's or a TlsSymbol's.
    bool basedOnReg() const { return kind == Memory || kind == TlsSymbol; }
    bool indexed() const { return kind == Memory && scale != 0; }
};

struct MInst {
    OpId m;                   // an index into the opcode table, never a string (M3a.1)
    MOperand a, b;             // AT&T order: a is the source, b the destination
    int operands;
    // A call's argument registers as the walker placed them, or a ret's result registers where the walker narrowed
    // them; without the word, the convention's whole set is read.
    RegSet args = 0;
    bool exactArgs = false;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    MInst(OpId m = OpId(), MOperand a = MOperand(), MOperand b = MOperand(), int operands = 0)
        : m(m), a(a), b(b), operands(operands) {}
};

// **A spelling call that is not an instruction, held as data** (M3a.1, doc-cxx3/M3-IR.md 2.4): its kind and its
// arguments, replayed as the same call where it stood. FuncletBegin, FuncletLeave and FuncletEnd bound a Microsoft
// funclet's blocks inside its function's stream (M3a.4): a block entered from nowhere, then one that returns.
struct MDirective {
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

    MDirective() {}
    explicit MDirective(Kind k) : kind(k) {}
    void replay(Spelling &s) const;
    std::string describe() const;
    // Whether this begins a funclet, or ends one (FuncletEnd and FuncletLeave alike: the block returns).
    bool beginsFunclet() const { return kind == FuncletBegin; }
    bool endsFunclet() const { return kind == FuncletEnd || kind == FuncletLeave; }
    bool leavesFunclet() const { return kind == FuncletLeave; }
};

// How a directive moves control: a funclet's end returns, its beginning starts a block, the rest do nothing.
Control controlOf(const MDirective &d);

// Everything between functionBegin and functionEnd: instructions, labels, and directives, replayed where they stood.
using MEntry = EntryOf<MInst, MDirective>;
using MStream = std::vector<MEntry>;

// **The register file a pseudo is coloured from** (M3-IR 2.8): X87 is never allocated, its operands being opaque text.
enum class RegClass : unsigned char { Gpr, Xmm, X87 };

}

namespace mir {

// A pseudo register is numbered above every physical one; the passes before allocation see pseudos wherever
// nothing pins a value to the register it happens to be in.
constexpr int kFirstPseudo = opt::kPhysical;
inline bool isPseudo(int id) { return id >= kFirstPseudo; }
// The registers a web may be made of: the general ones and the xmm ones, numbered below this.
constexpr int kWebRegs = opt::kXmm0 + 16;

// **The target's register files, as the webs and the allocator ask about them** (M3-IR 2.8): which registers
// each class may be coloured with, and the order they are offered in, cheapest encoding first.
struct TargetDesc {
    opt::RegSet members[3] = {0, 0, 0};    // per RegClass; the frame's registers in none
    std::vector<int> offer[3];
    opt::RegClass classOf(int r) const {
        return r < opt::kGprs ? opt::RegClass::Gpr : r < opt::kXmm0 + 16 ? opt::RegClass::Xmm : opt::RegClass::X87;
    }
    bool candidate(int r) const {
        return r >= 0 && r < opt::kPhysical && (members[static_cast<int>(classOf(r))] >> r & 1);
    }
    // Both x86 targets: the general registers but rsp and rbp, and xmm0-15; which are preserved is the convention's.
    static const TargetDesc &x86_64();
};

}
