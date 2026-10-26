#pragma once

// **One description of every x86 mnemonic the optimizer and the spellings know** - what GCC keeps in its machine
// description and cxx1 kept as a dozen lists over the passes and a table of the MASM spelling's own. A row has a kind
// (how `effectsOf` reads it), the passes' flags, its suffix width, a jcc's condition, and MASM's name and PTR width (M3a.3).

#include <cstddef>
#include <initializer_list>
#include <string>

namespace opt {

// The condition a jcc or setcc tests, in the order kConds spells them; kNoCond for any other mnemonic.
enum Cond : int {
    kNoCond = -1,
    CcE, CcNe, CcZ, CcNz, CcL, CcGe, CcLe, CcG, CcB, CcAe, CcBe, CcA, CcS, CcNs, CcP, CcNp, CcO, CcNo,
    kConds
};

struct Opcode {
    // How the instruction is read for its effects: the branch classify takes.
    enum Kind {
        Unknown,        // not in the table: opaque, reads and writes everything
        Call, Ret, Jmp, Jcc, Setcc,
        Push, Pop, Leave,
        SignExtendAx,   // cqo, cdq: rax into rdx:rax
        ExtendAx,       // cltq, cdqe: eax into rax
        StringMove,     // rep movsq
        Div,            // idiv, div: rdx:rax
        Compare,        // cmp, test, ucomis: reads both, writes the flags
        Unary,          // neg, not, inc, dec: reads and writes its operand
        Move,           // a copy, an extension, lea, an SSE move
        Rmw,            // two-operand arithmetic: reads and writes b
        X87,            // f*: memory and its own stack
    };
    enum : unsigned {
        kMergeXmm = 1u << 0,    // an SSE move into a register keeps its upper lanes
        kNoFlags = 1u << 1,     // leaves the flags alone (SSE arithmetic, not)
        kExplicit = 1u << 2,    // reads registers only through its operands
        kWritesOnly = 1u << 3,  // its destination is written, not read (mov*, set*, lea)
        kImmSource = 1u << 4,   // the source may be an immediate in place of a register
        kRenamable = 1u << 5,   // a frame operand may become a register as it stands
        kShift = 1u << 6,       // a shift or rotate: the count can only be %cl
        kZeroIdiom = 1u << 7,   // of a register with itself, writes zero without reading
        kAddress = 1u << 8,     // lea: forms an address from its source
        kCvt = 1u << 9,         // a conversion between integer and floating forms
        kPlainMov = 1u << 10,   // mov and its four widths: a plain copy, the width column saying which
        kSse = 1u << 11,        // encoded with a mandatory prefix and the 0F escape (OptAlign's size estimate)
        kEscape0F = 1u << 12,   // encoded behind the 0F escape alone: imul r,r/m, the extensions, setcc
    };
    const char *name;
    Kind kind;
    unsigned flags;
    int width;                  // the width the suffix names, in bytes; 0 where only a register can
    Cond cc;                    // a jcc's or setcc's condition
    const char *masm;           // the MASM spelling's mnemonic, or null where that spelling does not know it
    int masmWidth;              // the PTR width MASM puts on a memory operand of it; 0 for none

    bool has(unsigned f) const { return (flags & f) != 0; }
};

// **The table, one row per mnemonic**: the enumerator, the AT&T name, the kind, the flags, the suffix width, the
// condition, the MASM name and its PTR width. The flags and widths are those the passes recognised by name before the
// table existed, quirks included (see OptTable.cpp's note on the Unknown rows), so that nothing they emit changes.
#define OPT_OPCODE_FLAGS_ABBREVIATED                                                                              \
    E = Opcode::kExplicit, W = Opcode::kWritesOnly, I = Opcode::kImmSource, R = Opcode::kRenamable,                 \
    S = Opcode::kShift, Z = Opcode::kZeroIdiom, N = Opcode::kNoFlags, M = Opcode::kMergeXmm, A = Opcode::kAddress, \
    C = Opcode::kCvt, P = Opcode::kPlainMov, X = Opcode::kSse, T = Opcode::kEscape0F
#define OPT_OPCODES(_)                                                                        \
    _(Call, "call", Call, 0, 0, kNoCond, "call", 0)                                           \
    _(Ret, "ret", Ret, 0, 0, kNoCond, "ret", 0)                                               \
    _(Jmp, "jmp", Jmp, 0, 0, kNoCond, "jmp", 0)                                               \
    _(Push, "push", Push, R, 0, kNoCond, "push", 0)                                           \
    _(Pushq, "pushq", Push, R, 0, kNoCond, nullptr, 0)                                        \
    _(Pop, "pop", Pop, 0, 0, kNoCond, "pop", 0)                                               \
    _(Popq, "popq", Pop, 0, 0, kNoCond, nullptr, 0)                                           \
    _(Leave, "leave", Leave, 0, 0, kNoCond, nullptr, 0)                                       \
    _(Cqo, "cqo", SignExtendAx, 0, 0, kNoCond, "cqo", 0)                                      \
    _(Cqto, "cqto", SignExtendAx, 0, 0, kNoCond, nullptr, 0)                                  \
    _(Cdq, "cdq", SignExtendAx, 0, 0, kNoCond, "cdq", 0)                                      \
    _(Cltd, "cltd", SignExtendAx, 0, 0, kNoCond, nullptr, 0)                                  \
    _(Cltq, "cltq", ExtendAx, 0, 0, kNoCond, nullptr, 0)                                      \
    _(Cdqe, "cdqe", ExtendAx, 0, 0, kNoCond, nullptr, 0)                                      \
    _(RepMovsq, "rep movsq", StringMove, 0, 0, kNoCond, "rep movsq", 0)                       \
    _(Idiv, "idiv", Div, 0, 0, kNoCond, "idiv", 0)                                            \
    _(Div, "div", Div, 0, 0, kNoCond, "div", 0)                                               \
    _(Idivl, "idivl", Div, 0, 0, kNoCond, nullptr, 0)                                         \
    _(Divl, "divl", Div, 0, 0, kNoCond, nullptr, 0)                                           \
    _(Idivq, "idivq", Div, 0, 0, kNoCond, nullptr, 0)                                         \
    _(Divq, "divq", Div, 0, 0, kNoCond, nullptr, 0)                                           \
    /* Compares: the flags are their only result. */                                          \
    _(Cmp, "cmp", Compare, E | I | R, 0, kNoCond, "cmp", 0)                                   \
    _(Cmpl, "cmpl", Compare, E | I | R, 4, kNoCond, "cmp", 4)                                 \
    _(Cmpq, "cmpq", Compare, 0, 8, kNoCond, "cmp", 8)                                         \
    _(Cmpb, "cmpb", Compare, 0, 1, kNoCond, "cmp", 1)                                         \
    _(Cmpw, "cmpw", Unknown, 0, 0, kNoCond, "cmp", 2)                                         \
    _(Test, "test", Compare, E | R, 0, kNoCond, "test", 0)                                    \
    _(Testb, "testb", Compare, E | R, 1, kNoCond, "test", 1)                                  \
    _(Testw, "testw", Unknown, 0, 0, kNoCond, "test", 2)                                      \
    _(Testl, "testl", Compare, E | R, 4, kNoCond, "test", 4)                                  \
    _(Testq, "testq", Compare, E | R, 8, kNoCond, "test", 8)                                  \
    _(Ucomisd, "ucomisd", Compare, X, 0, kNoCond, "ucomisd", 0)                               \
    _(Ucomiss, "ucomiss", Compare, X, 0, kNoCond, "ucomiss", 0)                               \
    _(Comisd, "comisd", Compare, X, 0, kNoCond, nullptr, 0)                                   \
    _(Comiss, "comiss", Compare, X, 0, kNoCond, nullptr, 0)                                   \
    /* One operand, read and written. */                                                      \
    _(Neg, "neg", Unary, 0, 0, kNoCond, "neg", 0)                                             \
    _(Negq, "negq", Unary, 0, 0, kNoCond, "neg", 8)                                           \
    _(Negb, "negb", Unknown, 0, 0, kNoCond, "neg", 1)                                         \
    _(Negw, "negw", Unknown, 0, 0, kNoCond, "neg", 2)                                         \
    _(Negl, "negl", Unknown, 0, 0, kNoCond, "neg", 4)                                         \
    _(Inc, "inc", Unary, 0, 0, kNoCond, "inc", 0)                                             \
    _(Incb, "incb", Unknown, 0, 0, kNoCond, "inc", 1)                                         \
    _(Incw, "incw", Unknown, 0, 0, kNoCond, "inc", 2)                                         \
    _(Incl, "incl", Unknown, 0, 0, kNoCond, "inc", 4)                                         \
    _(Incq, "incq", Unknown, 0, 0, kNoCond, "inc", 8)                                         \
    _(Dec, "dec", Unary, 0, 0, kNoCond, "dec", 0)                                             \
    _(Decb, "decb", Unknown, 0, 0, kNoCond, "dec", 1)                                         \
    _(Decw, "decw", Unknown, 0, 0, kNoCond, "dec", 2)                                         \
    _(Decl, "decl", Unknown, 0, 0, kNoCond, "dec", 4)                                         \
    _(Decq, "decq", Unknown, 0, 0, kNoCond, "dec", 8)                                         \
    _(Not, "not", Unary, N, 0, kNoCond, "not", 0)                                             \
    _(Notq, "notq", Unary, N, 0, kNoCond, "not", 8)                                           \
    _(Notb, "notb", Unknown, 0, 0, kNoCond, "not", 1)                                         \
    _(Notw, "notw", Unknown, 0, 0, kNoCond, "not", 2)                                         \
    _(Notl, "notl", Unknown, 0, 0, kNoCond, "not", 4)                                         \
    /* Moves: the destination is written whole, or in part where the width says. */           \
    _(Mov, "mov", Move, E | W | I | R | P, 0, kNoCond, "mov", 0)                              \
    _(Movq, "movq", Move, E | W | I | R | P | X, 8, kNoCond, "mov", 8)                        \
    _(Movl, "movl", Move, E | W | I | R | P, 4, kNoCond, "mov", 4)                            \
    _(Movw, "movw", Move, E | W | I | P, 2, kNoCond, "mov", 2)                                \
    _(Movb, "movb", Move, E | W | I | P, 1, kNoCond, "mov", 1)                                \
    _(Movabs, "movabs", Move, E | W, 0, kNoCond, "mov", 0)                                    \
    _(Movslq, "movslq", Move, E | W | R | T, 0, kNoCond, "movsxd", 4)                         \
    _(Movsbq, "movsbq", Move, E | W | T, 0, kNoCond, "movsx", 1)                              \
    _(Movswq, "movswq", Move, E | W | T, 0, kNoCond, "movsx", 2)                              \
    _(Movsbl, "movsbl", Move, E | W | T, 0, kNoCond, nullptr, 0)                              \
    _(Movswl, "movswl", Move, E | W | T, 0, kNoCond, nullptr, 0)                              \
    _(Movzbq, "movzbq", Move, E | W | T, 0, kNoCond, "movzx", 1)                              \
    _(Movzbl, "movzbl", Move, E | W | T, 0, kNoCond, "movzx", 1)                              \
    _(Movzwl, "movzwl", Move, E | W | T, 0, kNoCond, "movzx", 2)                              \
    _(Movzwq, "movzwq", Move, E | W | T, 0, kNoCond, "movzx", 2)                              \
    _(Lea, "lea", Move, E | W | A, 0, kNoCond, "lea", 0)                                      \
    _(Movd, "movd", Move, E | W | X, 0, kNoCond, "movd", 0)                                   \
    _(Movaps, "movaps", Move, E | W | X, 0, kNoCond, "movaps", 0)                             \
    _(Movapd, "movapd", Move, E | W | X, 0, kNoCond, "movapd", 0)                             \
    _(Movsd, "movsd", Move, E | W | M | X, 0, kNoCond, "movsd", 8)                            \
    _(Movss, "movss", Move, E | W | M | X, 0, kNoCond, "movss", 4)                            \
    /* Two-operand arithmetic: b is read and written. */                                      \
    _(Add, "add", Rmw, E | I | R, 0, kNoCond, "add", 0)                                       \
    _(Sub, "sub", Rmw, E | I | R | Z, 0, kNoCond, "sub", 0)                                   \
    _(And, "and", Rmw, E | I | R, 0, kNoCond, "and", 0)                                       \
    _(Or, "or", Rmw, E | I | R, 0, kNoCond, "or", 0)                                          \
    _(Xor, "xor", Rmw, E | I | R | Z, 0, kNoCond, "xor", 0)                                   \
    _(Adc, "adc", Rmw, 0, 0, kNoCond, nullptr, 0)                                             \
    _(Sbb, "sbb", Rmw, 0, 0, kNoCond, nullptr, 0)                                             \
    _(Imul, "imul", Rmw, E | I | R | T, 0, kNoCond, "imul", 0)                                \
    _(Shl, "shl", Rmw, S, 0, kNoCond, "shl", 0)                                               \
    _(Sar, "sar", Rmw, S, 0, kNoCond, "sar", 0)                                               \
    _(Shr, "shr", Rmw, S, 0, kNoCond, "shr", 0)                                               \
    _(Addl, "addl", Rmw, E | I | R, 4, kNoCond, "add", 4)                                     \
    _(Subl, "subl", Rmw, E | I | R, 4, kNoCond, "sub", 4)                                     \
    _(Addb, "addb", Unknown, 0, 0, kNoCond, "add", 1)                                         \
    _(Addw, "addw", Unknown, 0, 0, kNoCond, "add", 2)                                         \
    _(Subb, "subb", Unknown, 0, 0, kNoCond, "sub", 1)                                         \
    _(Subw, "subw", Unknown, 0, 0, kNoCond, "sub", 2)                                         \
    _(Orb, "orb", Rmw, 0, 1, kNoCond, "or", 1)                                                \
    _(Orw, "orw", Unknown, 0, 0, kNoCond, "or", 2)                                            \
    _(Orq, "orq", Unknown, 0, 0, kNoCond, "or", 8)                                            \
    _(Andb, "andb", Unknown, 0, 0, kNoCond, "and", 1)                                         \
    _(Andw, "andw", Unknown, 0, 0, kNoCond, "and", 2)                                         \
    _(Andl, "andl", Rmw, 0, 0, kNoCond, "and", 4)                                             \
    _(Andq, "andq", Unknown, 0, 0, kNoCond, "and", 8)                                         \
    _(Orl, "orl", Rmw, 0, 0, kNoCond, "or", 4)                                                \
    _(Xorb, "xorb", Unknown, 0, 0, kNoCond, "xor", 1)                                         \
    _(Xorw, "xorw", Unknown, 0, 0, kNoCond, "xor", 2)                                         \
    _(Xorl, "xorl", Rmw, Z, 0, kNoCond, "xor", 4)                                             \
    _(Xorq, "xorq", Unknown, 0, 0, kNoCond, "xor", 8)                                         \
    _(Shll, "shll", Rmw, S, 0, kNoCond, "shl", 4)                                             \
    _(Sarl, "sarl", Rmw, S, 0, kNoCond, "sar", 4)                                             \
    _(Shrl, "shrl", Rmw, S, 0, kNoCond, "shr", 4)                                             \
    _(Shlb, "shlb", Unknown, 0, 0, kNoCond, "shl", 1)                                         \
    _(Shlw, "shlw", Unknown, 0, 0, kNoCond, "shl", 2)                                         \
    _(Shlq, "shlq", Unknown, 0, 0, kNoCond, "shl", 8)                                         \
    _(Shrb, "shrb", Unknown, 0, 0, kNoCond, "shr", 1)                                         \
    _(Shrw, "shrw", Unknown, 0, 0, kNoCond, "shr", 2)                                         \
    _(Shrq, "shrq", Unknown, 0, 0, kNoCond, "shr", 8)                                         \
    _(Sarb, "sarb", Unknown, 0, 0, kNoCond, "sar", 1)                                         \
    _(Sarw, "sarw", Unknown, 0, 0, kNoCond, "sar", 2)                                         \
    _(Sarq, "sarq", Unknown, 0, 0, kNoCond, "sar", 8)                                         \
    _(Addsd, "addsd", Rmw, N | X, 0, kNoCond, "addsd", 8)                                     \
    _(Subsd, "subsd", Rmw, N | X, 0, kNoCond, "subsd", 8)                                     \
    _(Mulsd, "mulsd", Rmw, N | X, 0, kNoCond, "mulsd", 8)                                     \
    _(Divsd, "divsd", Rmw, N | X, 0, kNoCond, "divsd", 8)                                     \
    _(Addss, "addss", Rmw, N | X, 0, kNoCond, "addss", 0)                                     \
    _(Subss, "subss", Rmw, N | X, 0, kNoCond, "subss", 0)                                     \
    _(Mulss, "mulss", Rmw, N | X, 0, kNoCond, "mulss", 0)                                     \
    _(Divss, "divss", Rmw, N | X, 0, kNoCond, "divss", 0)                                     \
    _(Pxor, "pxor", Rmw, N | Z | X, 0, kNoCond, "pxor", 0)                                    \
    _(Xorps, "xorps", Rmw, N | Z | X, 0, kNoCond, "xorps", 0)                                 \
    _(Xorpd, "xorpd", Rmw, N | Z | X, 0, kNoCond, "xorpd", 0)                                 \
    _(Andpd, "andpd", Rmw, N | X, 0, kNoCond, nullptr, 0)                                     \
    _(Andps, "andps", Rmw, N | X, 0, kNoCond, nullptr, 0)                                     \
    _(Sal, "sal", Rmw, S, 0, kNoCond, nullptr, 0)                                             \
    _(Rol, "rol", Rmw, S, 0, kNoCond, nullptr, 0)                                             \
    _(Ror, "ror", Rmw, S, 0, kNoCond, nullptr, 0)                                             \
    _(Addq, "addq", Rmw, E | I | R, 8, kNoCond, "add", 8)                                     \
    _(Subq, "subq", Rmw, E | I | R | Z, 8, kNoCond, "sub", 8)                                 \
    /* A row the MASM spelling alone knew, nothing writing it: see OptTable.cpp's note. */      \
    _(Nop, "nop", Unknown, 0, 0, kNoCond, "nop", 0)                                           \
    /* x87 compares set the flags; the rest of the x87 leaves them alone. */                  \
    _(Fucomip, "fucomip", X87, 0, 0, kNoCond, nullptr, 0)                                     \
    _(Fcomip, "fcomip", X87, 0, 0, kNoCond, nullptr, 0)                                       \
    _(Fucomi, "fucomi", X87, 0, 0, kNoCond, nullptr, 0)                                       \
    _(Fcomi, "fcomi", X87, 0, 0, kNoCond, nullptr, 0)                                         \
    _(Je, "je", Jcc, 0, 0, CcE, "je", 0) _(Jne, "jne", Jcc, 0, 0, CcNe, "jne", 0)             \
    _(Jz, "jz", Jcc, 0, 0, CcZ, "jz", 0) _(Jnz, "jnz", Jcc, 0, 0, CcNz, "jnz", 0)             \
    _(Jl, "jl", Jcc, 0, 0, CcL, "jl", 0) _(Jge, "jge", Jcc, 0, 0, CcGe, "jge", 0)             \
    _(Jle, "jle", Jcc, 0, 0, CcLe, "jle", 0) _(Jg, "jg", Jcc, 0, 0, CcG, "jg", 0)             \
    _(Jb, "jb", Jcc, 0, 0, CcB, "jb", 0) _(Jae, "jae", Jcc, 0, 0, CcAe, "jae", 0)             \
    _(Jbe, "jbe", Jcc, 0, 0, CcBe, "jbe", 0) _(Ja, "ja", Jcc, 0, 0, CcA, "ja", 0)             \
    _(Js, "js", Jcc, 0, 0, CcS, "js", 0) _(Jns, "jns", Jcc, 0, 0, CcNs, "jns", 0)             \
    _(Jp, "jp", Jcc, 0, 0, CcP, "jp", 0) _(Jnp, "jnp", Jcc, 0, 0, CcNp, "jnp", 0)             \
    _(Jo, "jo", Jcc, 0, 0, CcO, "jo", 0) _(Jno, "jno", Jcc, 0, 0, CcNo, "jno", 0)             \
    _(Sete, "sete", Setcc, E | W | T, 0, CcE, "sete", 0)                                      \
    _(Setne, "setne", Setcc, E | W | T, 0, CcNe, "setne", 0)                                  \
    _(Setz, "setz", Setcc, E | W | T, 0, CcZ, "setz", 0)                                      \
    _(Setnz, "setnz", Setcc, E | W | T, 0, CcNz, "setnz", 0)                                  \
    _(Setl, "setl", Setcc, E | W | T, 0, CcL, "setl", 0)                                      \
    _(Setge, "setge", Setcc, E | W | T, 0, CcGe, "setge", 0)                                  \
    _(Setle, "setle", Setcc, E | W | T, 0, CcLe, "setle", 0)                                  \
    _(Setg, "setg", Setcc, E | W | T, 0, CcG, "setg", 0)                                      \
    _(Setb, "setb", Setcc, E | W | T, 0, CcB, "setb", 0)                                      \
    _(Setae, "setae", Setcc, E | W | T, 0, CcAe, "setae", 0)                                  \
    _(Setbe, "setbe", Setcc, E | W | T, 0, CcBe, "setbe", 0)                                  \
    _(Seta, "seta", Setcc, E | W | T, 0, CcA, "seta", 0)                                      \
    _(Sets, "sets", Setcc, E | W | T, 0, CcS, "sets", 0)                                      \
    _(Setns, "setns", Setcc, E | W | T, 0, CcNs, "setns", 0)                                  \
    _(Setp, "setp", Setcc, E | W | T, 0, CcP, "setp", 0)                                      \
    _(Setnp, "setnp", Setcc, E | W | T, 0, CcNp, "setnp", 0)                                  \
    _(Seto, "seto", Setcc, E | W | T, 0, CcO, "seto", 0)                                      \
    _(Setno, "setno", Setcc, E | W | T, 0, CcNo, "setno", 0)                                  \
    _(Cvtsi2sdq, "cvtsi2sdq", Move, M | C | X, 0, kNoCond, "cvtsi2sd", 8)                     \
    _(Cvtsi2ssq, "cvtsi2ssq", Move, M | C | X, 0, kNoCond, "cvtsi2ss", 8)                     \
    _(Cvttsd2si, "cvttsd2si", Move, M | C | X, 0, kNoCond, "cvttsd2si", 0)                    \
    _(Cvttss2si, "cvttss2si", Move, M | C | X, 0, kNoCond, "cvttss2si", 0)                    \
    _(Cvtss2sd, "cvtss2sd", Move, M | C | X, 0, kNoCond, "cvtss2sd", 0)                       \
    _(Cvtsd2ss, "cvtsd2ss", Move, M | C | X, 0, kNoCond, "cvtsd2ss", 0)                       \
    _(Fld, "fld", X87, N, 0, kNoCond, nullptr, 0) _(Fld1, "fld1", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fldz, "fldz", X87, N, 0, kNoCond, nullptr, 0) _(Fldt, "fldt", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fldl, "fldl", X87, N, 0, kNoCond, nullptr, 0) _(Flds, "flds", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fldcw, "fldcw", X87, N, 0, kNoCond, nullptr, 0) _(Fnstcw, "fnstcw", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fstp, "fstp", X87, N, 0, kNoCond, nullptr, 0) _(Fstpl, "fstpl", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fstps, "fstps", X87, N, 0, kNoCond, nullptr, 0) _(Fstpt, "fstpt", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fildq, "fildq", X87, N, 0, kNoCond, nullptr, 0) _(Fistpq, "fistpq", X87, N, 0, kNoCond, nullptr, 0) \
    _(Faddp, "faddp", X87, N, 0, kNoCond, nullptr, 0) _(Fsubp, "fsubp", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fsubrp, "fsubrp", X87, N, 0, kNoCond, nullptr, 0) _(Fmulp, "fmulp", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fdivp, "fdivp", X87, N, 0, kNoCond, nullptr, 0) _(Fchs, "fchs", X87, N, 0, kNoCond, nullptr, 0) \
    _(Fxch, "fxch", X87, N, 0, kNoCond, nullptr, 0)

// **The rows by name**: `mn::Mov` is the row index of "mov", and an OpId made from one compares as an integer.
namespace mn {
enum Mn : unsigned {
    None = 0,
#define OPT_ROW_ENUM(id, att, kind, flags, width, cc, masm, mw) id,
    OPT_OPCODES(OPT_ROW_ENUM)
#undef OPT_ROW_ENUM
    Count
};
}

// The entry for a mnemonic; an unknown one gets `Unknown`, with the flags a prefix family carries (mov*, set*, cvt*, f*) so the passes see it as before.
const Opcode &opcodeOf(const std::string &m);

// **A mnemonic as an index into the one table** (M3a.1, M3-IR.md 2.3): a row's index - mn::Mov and its kin, which a
// pass compares as integers, never as text (M3a.3) - or one interned after the rows for a mnemonic only a prefix family
// or nothing catches, its spelling kept and counted (CPP11_MIR_STRICT=1). The one string door is the walker's `ins`.
class OpId {
public:
    OpId() : id_(0) {}                  // the empty mnemonic
    OpId(mn::Mn m) : id_(m) {}          // a row, by name
    explicit OpId(const std::string &m);   // a spelling is interned where it is written
    const std::string &name() const;
    const Opcode &row() const;
    bool listed() const;                // a row of the table's own, not a prefix family's or none
    unsigned index() const { return id_; }
    bool is(std::initializer_list<mn::Mn> rows) const {
        for (mn::Mn r : rows) if (id_ == r) return true;
        return false;
    }
private:
    unsigned id_;
};
inline bool operator==(const OpId &a, const OpId &b) { return a.index() == b.index(); }
inline bool operator!=(const OpId &a, const OpId &b) { return a.index() != b.index(); }
inline bool operator==(const OpId &a, mn::Mn b) { return a.index() == b; }
inline bool operator!=(const OpId &a, mn::Mn b) { return a.index() != b; }
inline const Opcode &opcodeOf(const OpId &m) { return m.row(); }

// The move families the passes recognise: a plain copy at eight bytes (mov, movq), at four too (movl), and every
// plain width (movw, movb); push and pop by either spelling.
inline bool isMovQ(const OpId &m) { const Opcode &r = m.row(); return r.has(Opcode::kPlainMov) && (r.width == 0 || r.width == 8); }
inline bool isMovQL(const OpId &m) { return isMovQ(m) || m == mn::Movl; }
inline bool isMovAny(const OpId &m) { return m.row().has(Opcode::kPlainMov); }
inline bool isPush(const OpId &m) { return m.row().kind == Opcode::Push; }
inline bool isPop(const OpId &m) { return m.row().kind == Opcode::Pop; }

// The condition a jcc or setcc tests, and its opposite; the jcc and the setcc that test a condition.
inline Cond conditionOf(const OpId &m) { return m.row().cc; }
Cond inverse(Cond cc);
OpId jcc(Cond cc);
OpId setcc(Cond cc);

}
