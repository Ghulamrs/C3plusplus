#pragma once

// **The C6000's MIR** (M6b, doc-cxx3/M6-PLAN.md): one function's selected code as instructions with parsed operands,
// labels between them, in the serial order C6xSched reads. `write` spells it as the walker's text, byte for byte.

#include "OptStrings.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace c6x {

// A register's number: A0-A31 are 0-31 and B0-B31 32-63, the bit `bitOf` gives it; kNoReg is none.
constexpr int kNoReg = 255;
inline int sideOfReg(int r) { return r >> 5; }                  // 0 the A file, 1 the B file
std::string regName(int r);
int regNumber(const std::string &name);                         // kNoReg where it names none

struct MOperand {
    // Pair is `hi:lo`, a 64-bit value's two registers. Symbol is a label, a symbol, or text this does not model.
    enum Kind : std::uint8_t { None, Register, Pair, Immediate, Memory, Symbol };
    // Memory's address: the base as it stands, displaced either way, or stepped before or after the access.
    enum Mode : std::uint8_t { Base, Plus, Minus, PreInc, PreDec, PostInc, PostDec };
    Kind kind = None;
    Mode mode = Base;
    std::uint8_t reg = kNoReg;        // Register; Pair's low half; Memory's base
    std::uint8_t hi = kNoReg;         // Pair's high half
    std::uint8_t index = kNoReg;      // Memory: the register that displaces the base, where one does
    bool hasOffset = false;           // Memory: `(n)` or `[n]` is written
    bool scaled = false;              // Memory: `[...]`, counted in the access's size; `(...)` is bytes
    long long value = 0;              // Immediate; Memory's constant offset
    opt::StrId text;                  // Symbol

    MOperand() {}
    MOperand(const std::string &s);   // parsed; text it cannot spell back the same is a Symbol
    MOperand(const char *s);
    MOperand(long long v) : kind(Immediate), value(v) {}
    MOperand(int v) : kind(Immediate), value(v) {}
    std::string str() const;
    bool isReg() const { return kind == Register; }
private:
    void set(const std::string &s);
};

struct MInst {
    // Verbatim is a line that is no instruction (a directive, a comment), kept as written with its line end.
    enum Kind : std::uint8_t { Instruction, Label, Verbatim };
    Kind kind = Instruction;
    std::uint8_t pred = kNoReg;       // `[R]`: runs where R is not zero; predNot, `[!R]`, where it is
    bool predNot = false;
    std::uint8_t operands = 0;
    opt::StrId m;                     // the mnemonic; a Label's name; a Verbatim line
    MOperand op[4];                   // TI's order: sources first, the destination last

    MInst() {}
    MInst(const char *mnem, const MOperand &a = MOperand(), const MOperand &b = MOperand(),
          const MOperand &c = MOperand(), const MOperand &d = MOperand());
    // The cycles after issue before the result lands (delaySlots, C6xSched's one table); a serial NOP pads them.
    int slots() const;
    // The file the instruction runs on: its destination's, or its first register operand's; -1 where it names none.
    int side() const;
    std::string str() const;          // the line, with its line end
};

using MStream = std::vector<MInst>;

// Whether the instruction stands as written: one source at most over a cross path, in a position with a crossed form,
// and a unit that has the form - `crossings` and `unitsFor`, the scheduler's own reading (C6xSched.cpp).
bool legal(const MInst &i);

// The text of a walker's helper, line by line, onto the stream: an instruction where it spells back the same.
void read(const std::string &text, MStream &to);
void write(const MStream &s, std::ostream &out);
// How much of a stream is modelled: instructions, labels, verbatim lines, and operands held as text that are no label.
struct Census { int instructions = 0, labels = 0, verbatim = 0, opaque = 0; };
Census census(const MStream &s);

}   // namespace c6x
