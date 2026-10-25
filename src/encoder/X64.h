#pragma once

#include "AsmUnit.h"

namespace masm {

enum OpKind { O_REG, O_IMM, O_MEM };

struct Operand {
    OpKind kind;
    int size;
    int reg;
    int base;
    int index;
    int scale;
    long long value;
    int sym;
    bool label;
    bool high;   // AH, BH, CH or DH: never with a REX prefix
    bool rexonly;   // SPL, BPL, SIL or DIL: only with a REX prefix
    bool wide;   // an immediate spelled with 2^32 or more (MOV r64 takes the 64-bit form)
    bool ptr;   // the size was written as a PTR (not taken from the label's type)
    bool near;   // a code label (a NEAR label or PROC): its bare use is its address
    bool short_ptr;   // SHORT written: the 8-bit jump, or an error
    bool near_ptr;   // NEAR PTR written: the 32-bit form
    int seg;   // a segment override written before the address, FS: 64 or GS: 65 (the TEB, thread_local), else 0
    bool secrel;   // [reg + SECTIONREL label]: the label's offset in its section, SECREL as ml64 makes it
};

struct Code {
    unsigned char b[16];
    int n;
    int disp_at;
    int sym;
    long long addend;   // the label's offset in the displacement, for a RELA writer
    RelKind kind;
    bool rex;   // a REX prefix is needed even with no bits set
    bool high;   // a high byte register is an operand
    bool bad;   // both: the combination ml64 refuses (A2218)
};

// **An instruction a compiler hands over already parsed** (M4.4): its MASM name, a REP or LOCK prefix byte, and its
// operands with their symbols by name - what the text path's split_line and operand() make of a line. The unit
// resolves the names each pass, in operand order, as the parse would have.
struct Instruction {
    std::string name;                 // upper case: "MOV", "MOVSQ"
    unsigned prefix;                  // 0, or 0xF0 LOCK, 0xF3 REP, 0xF2 REPNE, emitted in front
    std::vector<Operand> ops;         // sym -1; the name in syms stands for it
    std::vector<std::string> syms;    // one per operand: the symbol it names, or empty
};
// An operand with nothing in it yet, as operand() begins one; and a register by its MASM name (AL, R15, XMM3).
void clearOperand(Operand &o);
bool registerNamed(const std::string &upperName, Operand &o);

class X64Target : public Target {
public:
    X64Target();
    void begin_pass(int pass);
    void statement(Unit &u, std::vector<Token> &t);
    // An Instruction in place of a line: its symbols resolved, a label typed as a parsed one is, then encoded.
    void encode(Unit &u, const Instruction &ins);
    bool finished() const;
    bool again() const;
    bool resolve_here(const Unit &u, const Fixup &f, const Symbol &s, long long &value, int &width) const;

private:
    bool done;
    int pass;
    int proc;
    int anon;   // the @@ labels defined so far this pass
    std::vector<unsigned char> jsize;   // each jump in source order: 0 short, 1 near; only ever grows
    // Where each jump was in the previous pass, for a forward target whose value is that pass's too.
    std::vector<long long> jhere;
    size_t jn;   // the jumps seen this pass
    bool grew;   // a jump was widened this pass
    bool proc_private;   // OPTION PROC:PRIVATE: PROCs are Static unless PUBLIC
    bool frame;   // the open PROC is a PROC FRAME
    unsigned long frame_start;
    int frame_reg;   // .SETFRAME register and offset/16
    int frame_off;
    int prolog_end;   // bytes of prologue, or -1 until .ENDPROLOG
    unsigned long frame_alloc;   // .ALLOCSTACK and .PUSHREG so far, and whether .SETFRAME
    int frame_pushes;   // came: CodeView's S_FRAMEPROC is made of them
    bool frame_set;
    std::vector<std::vector<unsigned char> > codes;   // unwind codes, in the order written
    int xdatasym;   // the $xdatasym symbol ml64 relocates .pdata against, or -1
    std::string directives;   // INCLUDELIB: the .drectve text, written at END
    std::vector<int> segs;   // sections enclosing the open SEGMENT blocks
    std::vector<std::string> segnames;   // their names as written, for ENDS

    bool directive(Unit &u, std::vector<Token> &t);
    void segment(Unit &u, const std::vector<Token> &t);
    void option(Unit &u, const std::vector<Token> &t);
    void typed(Unit &u, Operand &o);
    int jump_width(Unit &u, const Operand &o);
    void unwind(Unit &u, const std::vector<Token> &t, const std::string &w);
    void source_line(Unit &u, const std::vector<Token> &t, const std::string &w);
    void end_frame(Unit &u);
    void data(Unit &u, const std::vector<Token> &t, size_t from, int width);
    bool operand(Unit &u, const std::vector<Token> &t, size_t a, size_t b, Operand &o);
    bool memory(Unit &u, const std::vector<Token> &t, const std::vector<size_t> &ranges, Operand &o);
    void instruction(Unit &u, const std::string &name, std::vector<Operand> &ops);
};

}
