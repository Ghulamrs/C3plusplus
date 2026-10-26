#include "X64.h"

namespace masm {

static void start(Code &c)
{
    c.n = 0;
    c.disp_at = -1;
    c.sym = -1;
    c.addend = 0;
    c.kind = R_REL32;
    c.rex = false;
    c.high = false;
    c.bad = false;
}

static void put(Code &c, unsigned v)
{
    c.b[c.n++] = (unsigned char)v;
}

static void put32(Code &c, long long v)
{
    for (int i = 0; i < 4; i++)
        put(c, (unsigned)((unsigned long long)v >> (8 * i)) & 0xFF);
}

// an immediate of the operand's size: 1, 2 or 4 bytes (64-bit operands take a sign-extended 4)
static void put_imm(Code &c, int size, long long v)
{
    if (size == 8) put(c, (unsigned)v & 0xFF);
    else if (size == 16) { put(c, (unsigned)v & 0xFF); put(c, (unsigned)(v >> 8) & 0xFF); }
    else put32(c, v);
}

static bool fits8(long long v)
{
    return v >= -128 && v <= 127;
}

static bool fits32(long long v)
{
    return v >= -2147483648LL && v <= 2147483647LL;
}

// an 8-bit register operand may need a REX prefix (SPL..DIL) or forbid one (AH..BH)
static void note(Code &c, const Operand &o)
{
    if (o.kind != O_REG || o.size != 8)
        return;
    if (o.high) c.high = true;
    if (o.rexonly) c.rex = true;
}

static void rex(Code &c, int bits)
{
    if (bits || c.rex) {
        if (c.high) c.bad = true;
        put(c, 0x40 | bits);
    }
}

static void rm_code(Code &c, bool w, int regf, const Operand &m, const unsigned char *op, int opn)
{
    int bits = w ? 8 : 0;
    if (regf & 8) bits |= 4;
    if (m.kind == O_REG) {
        if (m.reg & 8) bits |= 1;
    } else {
        if (m.base >= 0 && (m.base & 8)) bits |= 1;
        if (m.index >= 0 && (m.index & 8)) bits |= 2;
    }
    rex(c, bits);
    for (int i = 0; i < opn; i++)
        put(c, op[i]);

    int r = (regf & 7) << 3;
    if (m.kind == O_REG) {
        put(c, 0xC0 | r | (m.reg & 7));
        return;
    }
    if (m.sym >= 0 && m.base < 0 && m.index < 0) {
        put(c, 0x05 | r);
        c.disp_at = c.n;
        c.sym = m.sym;
        c.addend = m.value;
        put32(c, m.value);
        return;
    }
    // a label with registers is an absolute 32-bit address (ADDR32), as ml64 makes it
    int sb = m.scale == 8 ? 3 : m.scale == 4 ? 2 : m.scale == 2 ? 1 : 0;
    if (m.base < 0) {
        put(c, 0x04 | r);
        put(c, (sb << 6) | ((m.index < 0 ? 4 : m.index & 7) << 3) | 5);
        if (m.sym >= 0) { c.disp_at = c.n; c.sym = m.sym; c.kind = m.secrel ? R_SECREL : R_ADDR32; c.addend = m.value; }
        put32(c, m.value);
        return;
    }
    int mod;
    if (m.sym >= 0) mod = 0x80;
    else if (m.value == 0 && (m.base & 7) != 5) mod = 0x00;
    else if (fits8(m.value)) mod = 0x40;
    else mod = 0x80;
    if (m.index < 0 && (m.base & 7) != 4) {
        put(c, mod | r | (m.base & 7));
    } else {
        put(c, mod | r | 4);
        put(c, (sb << 6) | ((m.index < 0 ? 4 : m.index & 7) << 3) | (m.base & 7));
    }
    if (mod == 0x40) put(c, (unsigned)m.value & 0xFF);
    else if (mod == 0x80) {
        if (m.sym >= 0) { c.disp_at = c.n; c.sym = m.sym; c.kind = m.secrel ? R_SECREL : R_ADDR32; c.addend = m.value; }
        put32(c, m.value);
    }
}

static void rm1(Code &c, bool w, int regf, const Operand &m, unsigned op)
{
    unsigned char b[1] = { (unsigned char)op };
    rm_code(c, w, regf, m, b, 1);
}

static void rm2(Code &c, bool w, int regf, const Operand &m, unsigned op1, unsigned op2)
{
    unsigned char b[2] = { (unsigned char)op1, (unsigned char)op2 };
    rm_code(c, w, regf, m, b, 2);
}

// one-byte opcode by operand size: 16 takes the 66 prefix, 8 the even opcode, 64 REX.W
static void rm(Code &c, int size, int regf, const Operand &m, unsigned op)
{
    if (size == 16) put(c, 0x66);
    rm1(c, size == 64, regf, m, size == 8 ? (op & ~1u) : op);
}

// the accumulator short forms: op with AL, op+1 with AX/EAX/RAX
static void acc(Code &c, int size, unsigned op)
{
    if (size == 16) put(c, 0x66);
    rex(c, size == 64 ? 8 : 0);
    put(c, size == 8 ? (op & ~1u) : op);
}

static void emit(Unit &u, const Code &c)
{
    if (c.bad) {
        u.error("AH, BH, CH or DH cannot be used with SPL, BPL, SIL, DIL or R8-R15");
        return;
    }
    unsigned long at = u.here();
    for (int i = 0; i < c.n; i++)
        u.emit8(c.b[i]);
    if (c.disp_at >= 0) {
        // a RIP-relative displacement followed by an immediate is REL32_n, n the bytes after it, so the linker
        // counts to the end of the instruction as the CPU does
        RelKind kind = c.kind;
        int after = c.n - (c.disp_at + 4);
        if (kind == R_REL32 && after > 0)
            kind = (RelKind)(R_REL32_1 + after - 1);
        u.fixup(at + c.disp_at, c.sym, kind, c.addend);
    }
}

// a branch with a 32-bit displacement (E8, E9, 0F 8x) or an 8-bit one (EB, 7x)
static void branch(Unit &u, int prefix, unsigned op, const Operand &o, int width)
{
    Code c;
    start(c);
    if (prefix >= 0) put(c, prefix);
    put(c, op);
    c.disp_at = c.n;
    c.sym = o.sym;
    c.addend = o.value;
    if (width == 1) {
        c.kind = R_REL8;
        put(c, (unsigned)o.value & 0xFF);
    } else {
        put32(c, o.value);
    }
    emit(u, c);
    if (!u.fixups.empty())
        u.fixups.back().branch = true;
}

struct Name {
    const char *name;
    int code;
};

static const Name arith_names[] = {
    {"ADD", 0}, {"OR", 1}, {"ADC", 2}, {"SBB", 3},
    {"AND", 4}, {"SUB", 5}, {"XOR", 6}, {"CMP", 7},
};

static const Name unary_names[] = {
    {"NOT", 2}, {"NEG", 3}, {"MUL", 4}, {"DIV", 6}, {"IDIV", 7},
};

static const Name shift_names[] = {
    {"ROL", 0}, {"ROR", 1}, {"RCL", 2}, {"RCR", 3},
    {"SHL", 4}, {"SAL", 4}, {"SHR", 5}, {"SAR", 7},
};

static const Name jcc_names[] = {
    {"JO", 0}, {"JNO", 1}, {"JB", 2}, {"JC", 2}, {"JNAE", 2},
    {"JAE", 3}, {"JNB", 3}, {"JNC", 3}, {"JE", 4}, {"JZ", 4},
    {"JNE", 5}, {"JNZ", 5}, {"JBE", 6}, {"JNA", 6}, {"JA", 7}, {"JNBE", 7},
    {"JS", 8}, {"JNS", 9}, {"JP", 10}, {"JPE", 10}, {"JNP", 11}, {"JPO", 11},
    {"JL", 12}, {"JNGE", 12}, {"JGE", 13}, {"JNL", 13},
    {"JLE", 14}, {"JNG", 14}, {"JG", 15}, {"JNLE", 15},
};

// The SSE ops the compilers emit: a mandatory prefix before any REX, one opcode after 0F, the form, and the memory width ml64 insists on (16: any).
enum SseForm { S_LOAD, S_STORE, S_FROM_INT, S_TO_INT };

struct SseOp {
    const char *name;
    unsigned char prefix;
    unsigned char op;
    int form;
    int msize;
};

static const SseOp sse_ops[] = {
    {"MOVSD", 0xF2, 0x10, S_LOAD, 8}, {"MOVSD", 0xF2, 0x11, S_STORE, 8},
    {"MOVSS", 0xF3, 0x10, S_LOAD, 4}, {"MOVSS", 0xF3, 0x11, S_STORE, 4},
    {"ADDSD", 0xF2, 0x58, S_LOAD, 8}, {"SUBSD", 0xF2, 0x5C, S_LOAD, 8}, {"MULSD", 0xF2, 0x59, S_LOAD, 8},
    {"DIVSD", 0xF2, 0x5E, S_LOAD, 8}, {"SQRTSD", 0xF2, 0x51, S_LOAD, 8}, {"MINSD", 0xF2, 0x5D, S_LOAD, 8},
    {"MAXSD", 0xF2, 0x5F, S_LOAD, 8},
    {"ADDSS", 0xF3, 0x58, S_LOAD, 4}, {"SUBSS", 0xF3, 0x5C, S_LOAD, 4}, {"MULSS", 0xF3, 0x59, S_LOAD, 4},
    {"DIVSS", 0xF3, 0x5E, S_LOAD, 4}, {"SQRTSS", 0xF3, 0x51, S_LOAD, 4}, {"MINSS", 0xF3, 0x5D, S_LOAD, 4},
    {"MAXSS", 0xF3, 0x5F, S_LOAD, 4},
    {"UCOMISD", 0x66, 0x2E, S_LOAD, 8}, {"COMISD", 0x66, 0x2F, S_LOAD, 8},
    {"UCOMISS", 0x00, 0x2E, S_LOAD, 4}, {"COMISS", 0x00, 0x2F, S_LOAD, 4},
    {"CVTSS2SD", 0xF3, 0x5A, S_LOAD, 4}, {"CVTSD2SS", 0xF2, 0x5A, S_LOAD, 8},
    {"CVTSI2SD", 0xF2, 0x2A, S_FROM_INT, 0}, {"CVTSI2SS", 0xF3, 0x2A, S_FROM_INT, 0},
    {"CVTTSD2SI", 0xF2, 0x2C, S_TO_INT, 8}, {"CVTSD2SI", 0xF2, 0x2D, S_TO_INT, 8},
    {"CVTTSS2SI", 0xF3, 0x2C, S_TO_INT, 4}, {"CVTSS2SI", 0xF3, 0x2D, S_TO_INT, 4},
    {"PXOR", 0x66, 0xEF, S_LOAD, 16}, {"XORPD", 0x66, 0x57, S_LOAD, 16}, {"XORPS", 0x00, 0x57, S_LOAD, 16},
    {"ANDPD", 0x66, 0x54, S_LOAD, 16}, {"ANDPS", 0x00, 0x54, S_LOAD, 16},
    {"ANDNPD", 0x66, 0x55, S_LOAD, 16}, {"ANDNPS", 0x00, 0x55, S_LOAD, 16},
    {"ORPD", 0x66, 0x56, S_LOAD, 16}, {"ORPS", 0x00, 0x56, S_LOAD, 16},
    {"PADDQ", 0x66, 0xD4, S_LOAD, 16}, {"PSUBQ", 0x66, 0xFB, S_LOAD, 16},
    {"PAND", 0x66, 0xDB, S_LOAD, 16}, {"POR", 0x66, 0xEB, S_LOAD, 16}, {"PANDN", 0x66, 0xDF, S_LOAD, 16},
    {"MOVAPD", 0x66, 0x28, S_LOAD, 16}, {"MOVAPD", 0x66, 0x29, S_STORE, 16},
    {"MOVAPS", 0x00, 0x28, S_LOAD, 16}, {"MOVAPS", 0x00, 0x29, S_STORE, 16},
    {"MOVUPD", 0x66, 0x10, S_LOAD, 16}, {"MOVUPD", 0x66, 0x11, S_STORE, 16},
    {"MOVUPS", 0x00, 0x10, S_LOAD, 16}, {"MOVUPS", 0x00, 0x11, S_STORE, 16},
    {"MOVDQA", 0x66, 0x6F, S_LOAD, 16}, {"MOVDQA", 0x66, 0x7F, S_STORE, 16},
    {"MOVDQU", 0xF3, 0x6F, S_LOAD, 16}, {"MOVDQU", 0xF3, 0x7F, S_STORE, 16},
    {"MOVQ", 0xF3, 0x7E, S_LOAD, 8}, {"MOVQ", 0x66, 0xD6, S_STORE, 8},
    {"MOVD", 0x66, 0x6E, S_LOAD, 4}, {"MOVD", 0x66, 0x7E, S_STORE, 4},
};

static int lookup(const Name *list, int n, const std::string &name)
{
    for (int i = 0; i < n; i++)
        if (name == list[i].name)
            return list[i].code;
    return -1;
}

static const Name plain_names[] = {
    {"RET", 0xC3}, {"NOP", 0x90}, {"CDQ", 0x99}, {"LEAVE", 0xC9}, {"INT3", 0xCC},
    {"CWDE", 0x98}, {"HLT", 0xF4}, {"CMC", 0xF5}, {"CLC", 0xF8}, {"STC", 0xF9},
    {"CLD", 0xFC}, {"STD", 0xFD}, {"PUSHFQ", 0x9C}, {"POPFQ", 0x9D},
    {"MOVSB", 0xA4}, {"MOVSD", 0xA5}, {"CMPSB", 0xA6}, {"CMPSD", 0xA7},
    {"STOSB", 0xAA}, {"STOSD", 0xAB}, {"LODSB", 0xAC}, {"LODSD", 0xAD}, {"SCASB", 0xAE}, {"SCASD", 0xAF},
};
// two-byte plain forms: 66 or REX.W in front of a string op, or 0F xx
static const Name plain2_names[] = {
    {"CWD", 0x6699}, {"CBW", 0x6698}, {"PAUSE", 0xF390}, {"UD2", 0x0F0B}, {"SYSCALL", 0x0F05},
    {"CPUID", 0x0FA2}, {"RDTSC", 0x0F31},
    {"MOVSW", 0x66A5}, {"MOVSQ", 0x48A5}, {"CMPSW", 0x66A7}, {"CMPSQ", 0x48A7},
    {"STOSW", 0x66AB}, {"STOSQ", 0x48AB}, {"LODSW", 0x66AD}, {"LODSQ", 0x48AD}, {"SCASW", 0x66AF}, {"SCASQ", 0x48AF},
};
static const Name fence_names[] = { {"LFENCE", 0xE8}, {"MFENCE", 0xF0}, {"SFENCE", 0xF8} };
// 0F xx /r with the destination register in the reg field
static const Name bitscan_names[] = { {"BSF", 0xBC}, {"BSR", 0xBD} };
static const Name cnt_names[] = { {"POPCNT", 0xB8}, {"LZCNT", 0xBD}, {"TZCNT", 0xBC} };
// 0F xx /r with the bit register in the reg field, or 0F BA /n ib with a constant
static const Name bt_names[] = { {"BT", 4}, {"BTS", 5}, {"BTR", 6}, {"BTC", 7} };

static bool same_size(Unit &u, Operand &a, Operand &b)
{
    if (a.size == 0) a.size = b.size;
    if (b.size == 0) b.size = a.size;
    if (a.size == 0) {
        u.error("size unknown; use BYTE, WORD, DWORD or QWORD PTR");
        return false;
    }
    if (a.size != b.size) {
        u.error("operand sizes differ");
        return false;
    }
    return true;
}

static bool need_size(Unit &u, const Operand &m)
{
    if (m.size == 0) {
        u.error("size unknown; use BYTE, WORD, DWORD or QWORD PTR");
        return false;
    }
    return true;
}

// the immediate must fit the operand's width, signed or unsigned; a 64-bit operand takes a signed 32
static bool imm_fits(Unit &u, int size, long long v)
{
    bool ok;
    if (size == 8) ok = v >= -128 && v <= 255;
    else if (size == 16) ok = v >= -32768 && v <= 65535;
    else if (size == 32) ok = v >= -2147483648LL && v <= 4294967295LL;
    else ok = fits32(v);
    if (ok)
        return true;
    u.error(size == 64 ? "immediate does not fit in 32 bits" : "immediate does not fit the operand");
    return false;
}

// the value as the operand width sees it, sign-extended back
static long long narrow(int size, long long v)
{
    if (size == 8) return (long long)(signed char)(v & 0xFF);
    if (size == 16) return (long long)(short)(v & 0xFFFF);
    return (long long)(int)(unsigned int)(v & 0xFFFFFFFFLL);
}

static bool is_xmm(const Operand &o)
{
    return o.kind == O_REG && o.size == 128;
}

// the memory operand's width must be what the op reads or writes; 128-bit ops take it unsized
static bool sse_size(Unit &u, const Operand &m, int msize)
{
    if (m.kind != O_MEM) return true;
    if (msize == 16 ? (m.size == 0 || m.size == 128) : m.size == msize * 8) return true;
    if (m.size == 0) u.error("size unknown; use QWORD PTR or DWORD PTR");
    else u.error("wrong memory operand size for this instruction");
    return false;
}

static bool sse(Unit &u, const std::string &name, std::vector<Operand> &ops, Code &c)
{
    const size_t count = sizeof sse_ops / sizeof sse_ops[0];
    size_t first = count;
    for (size_t i = 0; i < count; i++)
        if (name == sse_ops[i].name) { first = i; break; }
    if (first == count)
        return false;
    if (ops.size() != 2 || ops[0].kind == O_IMM || ops[1].kind == O_IMM) {
        u.error(name + " needs two operands");
        return true;
    }
    Operand &d = ops[0];
    Operand &s = ops[1];
    if (d.kind == O_MEM && s.kind == O_MEM) { u.error("two memory operands"); return true; }
    const SseOp *op = 0;
    for (size_t i = first; i < count && name == sse_ops[i].name; i++) {
        int f = sse_ops[i].form;
        if (f == S_LOAD && is_xmm(d) && !(name != "MOVQ" && name != "MOVD" && s.kind == O_REG && !is_xmm(s))) op = &sse_ops[i];
        else if (f == S_STORE && is_xmm(s) && !is_xmm(d)) op = &sse_ops[i];
        else if (f == S_FROM_INT && is_xmm(d) && !is_xmm(s)) op = &sse_ops[i];
        else if (f == S_TO_INT && d.kind == O_REG && !is_xmm(d) && (is_xmm(s) || s.kind == O_MEM)) op = &sse_ops[i];
        if (op) break;
    }
    if (!op) { u.error(name + ": bad operand combination"); return true; }
    bool w = false;
    unsigned prefix = op->prefix;
    unsigned opcode = op->op;
    if (op->form == S_FROM_INT) {
        if (s.kind == O_REG ? (s.size != 32 && s.size != 64) : (s.size != 32 && s.size != 64)) {
            u.error(name + " needs a 32- or 64-bit integer operand");
            return true;
        }
        w = s.size == 64;
    } else if (op->form == S_TO_INT) {
        if (d.size != 32 && d.size != 64) { u.error(name + " needs a 32- or 64-bit register"); return true; }
        if (!sse_size(u, s, op->msize)) return true;
        w = d.size == 64;
    } else if ((name == "MOVQ" || name == "MOVD") && (d.kind == O_REG && s.kind == O_REG) && (!is_xmm(d) || !is_xmm(s))) {
        // MOVQ/MOVD with a general register: 66 [REX.W] 0F 6E (into the xmm) or 7E (out of it)
        const Operand &g = is_xmm(d) ? s : d;
        if (g.size != (name == "MOVQ" ? 64 : 32)) { u.error(name + " needs a " + (name == "MOVQ" ? "64" : "32") + "-bit register"); return true; }
        prefix = 0x66;
        opcode = is_xmm(d) ? 0x6E : 0x7E;
        w = name == "MOVQ";
    } else {
        const Operand &m = op->form == S_LOAD ? s : d;
        if (m.kind == O_REG && !is_xmm(m)) { u.error(name + " needs XMM registers"); return true; }
        if (!sse_size(u, m, op->msize)) return true;
    }
    if (prefix) put(c, prefix);
    if (op->form == S_LOAD || op->form == S_FROM_INT || (op->form == S_TO_INT)) {
        if (op->form == S_TO_INT) rm2(c, w, d.reg, s, 0x0F, opcode);
        else rm2(c, w, d.reg, s, 0x0F, opcode);
    } else {
        rm2(c, w, s.reg, d, 0x0F, opcode);
    }
    emit(u, c);
    return true;
}

void X64Target::instruction(Unit &u, const std::string &name, std::vector<Operand> &ops)
{
    Section *sec = u.cur();
    if (!sec)
        return;
    if (!sec->code) {
        u.error("instruction outside .CODE");
        return;
    }
    size_t n = ops.size();
    Code c;
    start(c);
    for (size_t i = 0; i < n; i++)
        note(c, ops[i]);
    for (size_t i = 0; i < n; i++)
        if (ops[i].seg) {
            if (ops[i].kind != O_MEM) { u.error("a segment override needs a memory operand"); return; }
            u.emit8((unsigned)ops[i].seg);
            break;
        }
    int k;

    if (name == "INT") {
        // INT 3 is the one-byte trap, as ml64 writes it; INT3 is the spelling ml64 refuses
        if (n != 1 || ops[0].kind != O_IMM || ops[0].value < 0 || ops[0].value > 255) { u.error("INT needs a constant from 0 to 255"); return; }
        if (ops[0].value == 3) { u.emit8(0xCC); return; }
        u.emit8(0xCD);
        u.emit8((unsigned)ops[0].value);
        return;
    }
    // MOVSD and CMPSD are string ops with no operands and SSE ops with two
    k = n == 0 ? lookup(plain2_names, 17, name) : -1;
    if (k >= 0) {
        if (n != 0) { u.error(name + " takes no operands"); return; }
        u.emit8((unsigned)k >> 8);
        u.emit8((unsigned)k & 0xFF);
        return;
    }
    k = lookup(fence_names, 3, name);
    if (k >= 0) {
        if (n != 0) { u.error(name + " takes no operands"); return; }
        u.emit8(0x0F); u.emit8(0xAE); u.emit8((unsigned)k);
        return;
    }
    k = lookup(plain_names, 24, name);
    if (k >= 0 && n != 0 && (name == "MOVSD" || name == "CMPSD")) k = -1;
    if (k >= 0 || name == "CQO" || name == "CDQE") {
        if (n == 1 && name == "RET" && ops[0].kind == O_IMM) {
            if (ops[0].value < 0 || ops[0].value > 0xFFFF) { u.error("RET value does not fit"); return; }
            if (ops[0].value == 0) { u.emit8(0xC3); return; }   // ml64 writes RET 0 as RET
            u.emit8(0xC2);
            u.emit16((unsigned)ops[0].value);
            return;
        }
        if (n != 0) { u.error(name + " takes no operands"); return; }
        if (name == "CQO") { u.emit8(0x48); u.emit8(0x99); return; }
        if (name == "CDQE") { u.emit8(0x48); u.emit8(0x98); return; }
        u.emit8(k);
        return;
    }

    k = lookup(arith_names, 8, name);
    if (k >= 0 || name == "MOV" || name == "TEST") {
        if (n != 2) { u.error(name + " needs two operands"); return; }
        Operand &d = ops[0];
        Operand &s = ops[1];
        if (d.kind == O_IMM) { u.error("destination cannot be a constant"); return; }
        if (d.kind == O_MEM && s.kind == O_MEM) { u.error("two memory operands"); return; }
        bool mov = name == "MOV";
        bool test = name == "TEST";
        if (mov && d.kind == O_REG && d.size == 64 && s.kind == O_MEM && s.label && s.near && !s.ptr) {
            // mov r64, code_label or [code_label]: the address, 48 B8+r imm64 with an ADDR64
            put(c, 0x48 | ((d.reg & 8) ? 1 : 0));
            put(c, 0xB8 | (d.reg & 7));
            c.disp_at = c.n;
            c.sym = s.sym;
            c.addend = s.value;
            c.kind = R_ADDR64;
            for (int i = 0; i < 8; i++)
                put(c, (unsigned)((unsigned long long)s.value >> (8 * i)) & 0xFF);
            emit(u, c);
            return;
        }
        if (s.kind != O_IMM) {
            if (!same_size(u, d, s)) return;
            int size = d.size;
            if (test) {
                // ml64 puts the first operand in the reg field whenever it is a register: test rax,rcx is 48 85 C1,
                // and test rax,[m] the same 85 /r
                if (d.kind == O_REG) rm(c, size, d.reg, s, 0x85);
                else rm(c, size, s.reg, d, 0x85);
            } else if (d.kind == O_REG) {
                rm(c, size, d.reg, s, mov ? 0x8B : (unsigned)(k * 8 + 3));
            } else {
                rm(c, size, s.reg, d, mov ? 0x89 : (unsigned)(k * 8 + 1));
            }
            emit(u, c);
            return;
        }
        if (!need_size(u, d)) return;
        int size = d.size;
        long long v = s.value;
        if (s.sym >= 0 && !(mov && d.kind == O_REG && size == 64)) {
            u.error("OFFSET is only taken by MOV into a 64-bit register in this version");
            return;
        }
        if (mov && d.kind == O_REG && size == 64 && (s.sym >= 0 || !fits32(v) || s.wide)) {
            put(c, 0x48 | ((d.reg & 8) ? 1 : 0));
            put(c, 0xB8 | (d.reg & 7));
            if (s.sym >= 0) { c.disp_at = c.n; c.sym = s.sym; c.kind = R_ADDR64; c.addend = v; }
            for (int i = 0; i < 8; i++)
                put(c, (unsigned)((unsigned long long)v >> (8 * i)) & 0xFF);
            emit(u, c);
            return;
        }
        if (!imm_fits(u, size, v)) return;
        v = narrow(size, v);
        if (mov && d.kind == O_REG && size != 64) {
            if (size == 16) put(c, 0x66);
            rex(c, (d.reg & 8) ? 1 : 0);
            put(c, (size == 8 ? 0xB0 : 0xB8) | (d.reg & 7));
        } else if (mov) {
            rm(c, size, 0, d, 0xC7);
        } else if (test) {
            if (d.kind == O_REG && d.reg == 0) acc(c, size, 0xA9);
            else rm(c, size, 0, d, 0xF7);
        } else if (size != 8 && fits8(v)) {
            rm(c, size, k, d, 0x83);
            put(c, (unsigned)v & 0xFF);
            emit(u, c);
            return;
        } else if (d.kind == O_REG && d.reg == 0) {
            acc(c, size, (unsigned)(k * 8 + 5));
        } else {
            rm(c, size, k, d, 0x81);
        }
        put_imm(c, size, v);
        emit(u, c);
        return;
    }

    if (name == "MOVZX" || name == "MOVSX" || name == "MOVSXD") {
        if (n != 2 || ops[0].kind != O_REG || ops[1].kind == O_IMM || ops[0].size == 8) {
            u.error(name + " needs a register and a smaller register or memory operand");
            return;
        }
        Operand &d = ops[0];
        Operand &s = ops[1];
        if (!need_size(u, s)) return;
        if (name == "MOVSXD") {
            if (d.size != 64 || s.size != 32) { u.error("MOVSXD takes a 64-bit register and a 32-bit operand"); return; }
            rm1(c, true, d.reg, s, 0x63);
            emit(u, c);
            return;
        }
        if (s.size != 8 && !(s.size == 16 && d.size != 16)) { u.error(name + " needs an 8- or 16-bit operand smaller than the register"); return; }
        if (d.size == 16) put(c, 0x66);
        rm2(c, d.size == 64, d.reg, s, 0x0F, (name == "MOVZX" ? 0xB6 : 0xBE) + (s.size == 16 ? 1 : 0));
        emit(u, c);
        return;
    }

    if (name == "LEA") {
        if (n != 2 || ops[0].kind != O_REG || ops[1].kind != O_MEM || ops[0].size == 8) {
            u.error("LEA needs a register and a memory operand");
            return;
        }
        if (ops[0].size == 16) put(c, 0x66);
        rm1(c, ops[0].size == 64, ops[0].reg, ops[1], 0x8D);
        emit(u, c);
        return;
    }

    if (name == "IMUL" && (n == 2 || n == 3)) {
        // imul r, r/m (0F AF); imul r, imm is imul r, r, imm; imul r, r/m, imm is 6B ib or 69 iw/id
        if (ops[0].kind != O_REG || ops[0].size == 8 || (n == 3 && ops[2].kind != O_IMM) ||
            (n == 2 && ops[1].kind == O_IMM && false)) {
            u.error("IMUL needs a register, a register or memory operand, and at most a constant");
            return;
        }
        if (n == 2 && ops[1].kind == O_IMM) { ops.push_back(ops[1]); ops[1] = ops[0]; n = 3; }
        if (ops[1].kind == O_IMM) { u.error("IMUL needs a register or memory operand second"); return; }
        if (!same_size(u, ops[0], ops[1])) return;
        int size = ops[0].size;
        if (n == 2) {
            if (size == 16) put(c, 0x66);
            rm2(c, size == 64, ops[0].reg, ops[1], 0x0F, 0xAF);
            emit(u, c);
            return;
        }
        long long v = ops[2].value;
        if (!imm_fits(u, size, v)) return;
        v = narrow(size, v);
        if (size == 16) put(c, 0x66);
        if (fits8(v)) {
            rm1(c, size == 64, ops[0].reg, ops[1], 0x6B);
            put(c, (unsigned)v & 0xFF);
        } else {
            rm1(c, size == 64, ops[0].reg, ops[1], 0x69);
            put_imm(c, size, v);
        }
        emit(u, c);
        return;
    }

    if (name == "XCHG") {
        // the register goes in the reg field, both registers the first; with the accumulator and 16 bits or more,
        // the one-byte 90+r form
        if (n != 2 || ops[0].kind == O_IMM || ops[1].kind == O_IMM || (ops[0].kind == O_MEM && ops[1].kind == O_MEM)) {
            u.error("XCHG needs a register and a register or memory operand");
            return;
        }
        if (!same_size(u, ops[0], ops[1])) return;
        int size = ops[0].size;
        if (ops[0].kind == O_REG && ops[1].kind == O_REG && size != 8 &&
            ((ops[0].reg == 0 && !ops[0].high) || (ops[1].reg == 0 && !ops[1].high))) {
            const Operand &other = ops[0].reg == 0 && !ops[0].high ? ops[1] : ops[0];
            if (size == 16) put(c, 0x66);
            rex(c, (size == 64 ? 8 : 0) | ((other.reg & 8) ? 1 : 0));
            put(c, 0x90 | (other.reg & 7));
            emit(u, c);
            return;
        }
        if (ops[0].kind == O_REG) rm(c, size, ops[0].reg, ops[1], 0x87);
        else rm(c, size, ops[1].reg, ops[0], 0x87);
        emit(u, c);
        return;
    }

    if (name == "CMPXCHG" || name == "XADD") {
        if (n != 2 || ops[1].kind != O_REG || ops[0].kind == O_IMM) { u.error(name + " needs a register or memory operand and a register"); return; }
        if (!same_size(u, ops[0], ops[1])) return;
        int size = ops[0].size;
        if (size == 16) put(c, 0x66);
        rm2(c, size == 64, ops[1].reg, ops[0], 0x0F, (name == "CMPXCHG" ? 0xB1 : 0xC1) - (size == 8 ? 1 : 0));
        emit(u, c);
        return;
    }

    k = lookup(bitscan_names, 2, name);
    int cnt = lookup(cnt_names, 3, name);
    if (k >= 0 || cnt >= 0) {
        if (n != 2 || ops[0].kind != O_REG || ops[1].kind == O_IMM || ops[0].size == 8) { u.error(name + " needs a register and a register or memory operand"); return; }
        if (!same_size(u, ops[0], ops[1])) return;
        if (cnt >= 0) put(c, 0xF3);
        if (ops[0].size == 16) put(c, 0x66);
        rm2(c, ops[0].size == 64, ops[0].reg, ops[1], 0x0F, (unsigned)(k >= 0 ? k : cnt));
        emit(u, c);
        return;
    }

    k = lookup(bt_names, 4, name);
    if (k >= 0) {
        if (n != 2 || ops[0].kind == O_IMM || ops[0].size == 8) { u.error(name + " needs a 16-, 32- or 64-bit operand and a register or constant"); return; }
        if (ops[1].kind == O_IMM) {
            if (!need_size(u, ops[0])) return;
            if (ops[1].value < 0 || ops[1].value > 255) { u.error("bit number does not fit"); return; }
            if (ops[0].size == 16) put(c, 0x66);
            rm2(c, ops[0].size == 64, k, ops[0], 0x0F, 0xBA);
            put(c, (unsigned)ops[1].value);
        } else {
            if (ops[1].kind != O_REG) { u.error(name + " needs a register or constant bit number"); return; }
            if (!same_size(u, ops[0], ops[1])) return;
            if (ops[0].size == 16) put(c, 0x66);
            rm2(c, ops[0].size == 64, ops[1].reg, ops[0], 0x0F, 0xA3 + (unsigned)(k - 4) * 8);
        }
        emit(u, c);
        return;
    }

    k = lookup(unary_names, 5, name);
    if (k < 0 && name == "IMUL") k = 5;
    if (k >= 0 || name == "INC" || name == "DEC") {
        if (n != 1 || ops[0].kind == O_IMM) { u.error(name + " needs one register or memory operand"); return; }
        if (!need_size(u, ops[0])) return;
        if (k >= 0) rm(c, ops[0].size, k, ops[0], 0xF7);
        else rm(c, ops[0].size, name == "INC" ? 0 : 1, ops[0], 0xFF);
        emit(u, c);
        return;
    }

    k = lookup(shift_names, 8, name);
    if (k >= 0) {
        // by a constant (D1 for 1, C1 ib) or by CL (D3); the byte forms are the even opcodes
        bool by_cl = n == 2 && ops[1].kind == O_REG && ops[1].size == 8 && ops[1].reg == 1 && !ops[1].high;
        if (n != 2 || ops[0].kind == O_IMM || (ops[1].kind != O_IMM && !by_cl)) {
            u.error(name + " needs a destination and a constant count or CL");
            return;
        }
        if (!need_size(u, ops[0])) return;
        if (by_cl) {
            rm(c, ops[0].size, k, ops[0], 0xD3);
            emit(u, c);
            return;
        }
        long long v = ops[1].value;
        if (v < 0 || v > 255) { u.error("shift count does not fit"); return; }
        if (v == 1) {
            rm(c, ops[0].size, k, ops[0], 0xD1);
        } else {
            rm(c, ops[0].size, k, ops[0], 0xC1);
            put(c, (unsigned)v);
        }
        emit(u, c);
        return;
    }

    if (name == "PUSH" || name == "POP") {
        bool push = name == "PUSH";
        if (n != 1) { u.error(name + " needs one operand"); return; }
        Operand &o = ops[0];
        if (o.kind == O_REG) {
            if (o.size != 64 && o.size != 16) { u.error(name + " needs a 64-bit register"); return; }
            if (o.size == 16) u.emit8(0x66);
            if (o.reg & 8) u.emit8(0x41);
            u.emit8((push ? 0x50 : 0x58) | (o.reg & 7));
            return;
        }
        if (o.kind == O_IMM) {
            if (!push) { u.error("POP needs a register or memory operand"); return; }
            if (!fits32(o.value)) { u.error("immediate does not fit in 32 bits"); return; }
            if (fits8(o.value)) { u.emit8(0x6A); u.emit8((unsigned)o.value & 0xFF); }
            else { u.emit8(0x68); u.emit32((unsigned long)o.value); }
            return;
        }
        if (o.size != 64 && o.size != 16 && !o.label) { u.error(name + " needs QWORD PTR"); return; }
        if (o.size == 16) put(c, 0x66);
        rm1(c, false, push ? 6 : 0, o, push ? 0xFF : 0x8F);
        emit(u, c);
        return;
    }

    if (name == "CALL" || name == "JMP") {
        bool call = name == "CALL";
        if (n != 1 || ops[0].kind == O_IMM) { u.error(name + " needs a target"); return; }
        Operand &o = ops[0];
        if (o.kind == O_MEM && o.label && (o.near || o.size == 0)) {
            // a code label, or one not typed yet: a direct call or jump
            if (call) branch(u, -1, 0xE8, o, 4);
            else if (jump_width(u, o) == 1) branch(u, -1, 0xEB, o, 1);
            else branch(u, -1, 0xE9, o, 4);
            return;
        }
        // a data label or QWORD PTR: through the qword, FF /2 or FF /4, as ml64
        if (o.size != 64) { u.error(name + " needs a 64-bit register or QWORD PTR"); return; }
        rm1(c, false, call ? 2 : 4, o, 0xFF);
        emit(u, c);
        return;
    }

    k = lookup(jcc_names, 30, name);
    if (k >= 0) {
        if (n != 1 || ops[0].kind != O_MEM || !ops[0].label || (ops[0].size != 0 && !ops[0].near)) {
            u.error(name + " needs a label");
            return;
        }
        if (jump_width(u, ops[0]) == 1) branch(u, -1, 0x70 + k, ops[0], 1);
        else branch(u, 0x0F, 0x80 + k, ops[0], 4);
        return;
    }

    // SETcc r/m8 and CMOVcc r16/32/64, r/m: the Jcc condition codes
    if (name.compare(0, 3, "SET") == 0 && (k = lookup(jcc_names, 30, "J" + name.substr(3))) >= 0) {
        if (n != 1 || ops[0].kind == O_IMM) { u.error(name + " needs a byte register or memory operand"); return; }
        if (ops[0].size == 0) ops[0].size = 8;
        if (ops[0].size != 8) { u.error(name + " needs a byte operand"); return; }
        rm2(c, false, 0, ops[0], 0x0F, 0x90 + k);
        emit(u, c);
        return;
    }
    if (name.compare(0, 4, "CMOV") == 0 && (k = lookup(jcc_names, 30, "J" + name.substr(4))) >= 0) {
        if (n != 2 || ops[0].kind != O_REG || ops[1].kind == O_IMM || ops[0].size == 8) {
            u.error(name + " needs a register and a register or memory operand");
            return;
        }
        if (!same_size(u, ops[0], ops[1])) return;
        if (ops[0].size == 16) put(c, 0x66);
        rm2(c, ops[0].size == 64, ops[0].reg, ops[1], 0x0F, 0x40 + k);
        emit(u, c);
        return;
    }

    if (sse(u, name, ops, c))
        return;

    u.error("unknown instruction '" + name + "'");
}

}
