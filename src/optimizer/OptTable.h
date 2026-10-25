#pragma once

// **One description of every x86 mnemonic the optimizer knows** - what GCC keeps in its machine description and cxx1 kept as a dozen lists spread over the passes.
// Each mnemonic has a kind, which says how `effectsOf` reads it, a set of flags the passes ask about, and the width its suffix names.
// The table records what the passes recognised before it existed, quirks included: `addq` names a width but is not arithmetic here, `sal` is a shift but not arithmetic, `testl` reads only through its operands but is not marked so. Each is a candidate for a later change; none is one now.

#include <cstddef>
#include <string>

namespace opt {

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
    };
    const char *name;
    Kind kind;
    unsigned flags;
    int width;                  // the width the suffix names, in bytes; 0 where only a register can

    bool has(unsigned f) const { return (flags & f) != 0; }
};

// The entry for a mnemonic; an unknown one gets `Unknown`, with the flags a prefix family carries (mov*, set*, cvt*, f*) so the passes see it as before.
const Opcode &opcodeOf(const std::string &m);

// **A mnemonic as an index into the one table** (M3a.1, M3-IR.md 2.3): a table row's index, or one interned after the
// rows for a mnemonic only a prefix family or nothing catches, its spelling kept and counted (CPP11_MIR_STRICT=1). It
// reads like the string it was, so the passes' tests by spelling stand until M3a.3.
class OpId {
public:
    OpId() : id_(0) {}                  // the empty mnemonic
    OpId(const std::string &m);         // a spelling is interned where it is written
    OpId(const char *m);
    const std::string &name() const;
    const Opcode &row() const;
    bool listed() const;                // a row of the table's own, not a prefix family's or none
    unsigned index() const { return id_; }
    operator const std::string &() const { return name(); }
    std::size_t size() const { return name().size(); }
    bool empty() const { return name().empty(); }
    char operator[](std::size_t i) const { return name()[i]; }
    char back() const { return name().back(); }
    int compare(std::size_t pos, std::size_t n, const char *s) const { return name().compare(pos, n, s); }
    std::string substr(std::size_t pos, std::size_t n = std::string::npos) const { return name().substr(pos, n); }
private:
    unsigned id_;
};
inline bool operator==(const OpId &a, const OpId &b) { return a.index() == b.index(); }
inline bool operator!=(const OpId &a, const OpId &b) { return a.index() != b.index(); }
inline bool operator==(const OpId &a, const char *b) { return a.name() == b; }
inline bool operator!=(const OpId &a, const char *b) { return a.name() != b; }
inline bool operator==(const char *a, const OpId &b) { return b.name() == a; }
inline bool operator!=(const char *a, const OpId &b) { return b.name() != a; }
inline bool operator==(const OpId &a, const std::string &b) { return a.name() == b; }
inline bool operator!=(const OpId &a, const std::string &b) { return a.name() != b; }
inline bool operator==(const std::string &a, const OpId &b) { return b.name() == a; }
inline bool operator!=(const std::string &a, const OpId &b) { return b.name() != a; }
inline std::string operator+(const std::string &a, const OpId &b) { return a + b.name(); }
inline std::string operator+(const char *a, const OpId &b) { return a + b.name(); }
inline std::string operator+(const OpId &a, const char *b) { return a.name() + b; }
inline std::string operator+(const OpId &a, const std::string &b) { return a.name() + b; }
inline const Opcode &opcodeOf(const OpId &m) { return m.row(); }

// The move families the passes recognise by spelling: a plain copy at eight
// bytes (mov, movq), at four too (movl), and every plain width (movw, movb).
bool isMovQ(const std::string &m);    // mov, movq
bool isMovQL(const std::string &m);   // mov, movq, movl
bool isMovAny(const std::string &m);  // mov, movq, movl, movw, movb
bool isPush(const std::string &m);    // push, pushq
bool isPop(const std::string &m);     // pop, popq

// The condition a jcc or setcc tests, and its opposite; "" for anything else.
std::string conditionOf(const std::string &m);
std::string inverse(const std::string &cc);

}
