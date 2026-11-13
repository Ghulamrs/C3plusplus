#include "Arm64Darwin.h"

#include "../Source.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>

int DarwinArm64Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                       return 1;
    case Kind::Bool:                                       return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar:   return 1;
    case Kind::Short: case Kind::UShort:                   return 2;
    case Kind::WChar:                                      return sizeOf(wcharType());
    case Kind::Char16:                                     return 2;
    case Kind::Char32:                                     return 4;
    case Kind::Int: case Kind::UInt:                       return 4;
    case Kind::Long: case Kind::ULong:                     return 8;
    case Kind::LongLong: case Kind::ULongLong:             return 8;
    case Kind::Float:                                      return 4;
    case Kind::Double:                                     return 8;

    case Kind::LongDouble:                                 return 8;
    case Kind::Pointer: case Kind::NullPtr:                                    return 8;
    default:
        std::fprintf(stderr, "target: no size for this type yet\n");
        compileFailed();
    }
}

int DarwinArm64Target::alignOf(Kind k) const { return sizeOf(k); }

static const char *const kArgRegs[] = { "x0", "x1", "x2", "x3",
                                        "x4", "x5", "x6", "x7" };
static const char *const kSseRegs[] = { "d0", "d1", "d2", "d3",
                                        "d4", "d5", "d6", "d7" };

// AAPCS64 as Apple writes it. `homogeneousFloatAggregates` is the float rule only
// this target has, and `aggregatesByReference` is deliberately **not** set: it is
// Microsoft's exact-size rule, and this target's is size <= 16 - see Abi.h.
static Abi aapcs64Apple() {
    Abi a;
    a.intRegs = kArgRegs;               a.intCount = 8;
    a.sseRegs = kSseRegs;               a.sseCount = 8;
    a.structReturnLimit = 16;
    a.scratch = "x9";                   a.scratch32 = "w9";
    a.homogeneousFloatAggregates = true;
    return a;
}

static const Abi kAapcs64AppleAbi = aapcs64Apple();

const Abi &Arm64DarwinBackend::abi() const { return kAapcs64AppleAbi; }

static const char *const kDarwinMacros[] = {
    "__aarch64__=1", "__arm64__=1", "__arm64=1",
    "__APPLE__=1", "__MACH__=1", "__LP64__=1", "_LP64=1", nullptr,
};
const char *const *Arm64DarwinBackend::identityMacros() const { return kDarwinMacros; }

std::unique_ptr<CodeGen> Arm64DarwinBackend::codegen(std::ostream &sink, Syntax) const {
    return std::unique_ptr<CodeGen>(new Arm64Darwin(sink, target_, kAapcs64AppleAbi));
}

static int alignTo(int n, int a) { return (n + a - 1) / a * a; }

void Arm64Darwin::unsupported(const char *what) {
    std::fprintf(stderr, "codegen: %s is not supported yet by the arm64-darwin "
                         "backend\n", what);
    compileFailed();
}

std::string Arm64Darwin::label(const char *kind, int id) const {
    return labelPrefix_ + kind + "." + std::to_string(id);
}

std::string Arm64Darwin::userLabel(const std::string &name) const {
    return "L." + functionName_ + ".user." + name;
}

void Arm64Darwin::push() { out_ << "  str x0, [sp, #-16]!\n"; }
void Arm64Darwin::pop(const char *reg) {
    out_ << "  ldr " << reg << ", [sp], #16\n";
}

static std::string fpReg(const Type *t, int n) {
    return std::string(t->kind() == Kind::Float ? "s" : "d") + std::to_string(n);
}

void Arm64Darwin::movImm(const char *reg, long long value) {
    unsigned long long u = static_cast<unsigned long long>(value);
    out_ << "  mov " << reg << ", #" << (u & 0xffff) << "\n";
    for (int shift = 16; shift < 64; shift += 16) {
        unsigned long long part = (u >> shift) & 0xffff;
        if (part != 0) out_ << "  movk " << reg << ", #" << part
                            << ", lsl #" << shift << "\n";
    }
}

void Arm64Darwin::narrowInt(const Type *to, int d, int s) {
    int sz = to->size(target_);
    const char *op = to->isSigned(target_) ? (sz == 1 ? "  sxtb x" : sz == 2 ? "  sxth x" : "  sxtw x")
                                           : (sz == 1 ? "  uxtb w" : sz == 2 ? "  uxth w" : "  mov w");
    if (sz == 1 || sz == 2 || sz == 4) out_ << op << d << ", w" << s << "\n";
    else if (d != s) out_ << "  mov x" << d << ", x" << s << "\n";
}

Arm64Darwin::AggPlan Arm64Darwin::planFor(const Type *t) const {
    AggPlan p;
    // A class whose copy is a constructor call is never in registers: the caller
    // owns the storage and passes its address. Asked before the homogeneous-float
    // question, which would otherwise put a class of two floats in registers.
    if (t->nonTrivialCopy() || t->hasDestructor()) {
        p.byRef = true;
        p.words = 1;
        return p;
    }
    Kind elem;
    int n = homogeneousFloatCount(t, &elem);
    if (n > 0) { p.hfa = n; p.elem = elem; return p; }

    // **An empty class is ignored in the parameter list on this platform**, and
    // `sizeof` being 1 is not the question - measured with clang. Asked as "no
    // members and no vptr": dataSize is 0 on the types the compiler synthesises.
    if (t->isStructOrUnion() && t->members().empty() && !t->polymorphic()) {
        p.words = 0;
        return p;
    }

    int size = t->size(target_);
    if (size <= 16) { p.words = (size + 7) / 8; return p; }
    p.byRef = true;
    p.words = 1;
    return p;
}

// **The tail is composed, not approximated.** A lane of 3, 5, 6 or 7 live bytes
// took the largest store that fit and left the rest as the destination held it.
// The value register is dead after its own store, so the remainder shifts in place.
void Arm64Darwin::storeWord(const char *xreg, const char *base, int k, int size) {
    const int off = k * 8;
    const int left = size - off;
    const std::string w = std::string("w") + (xreg + 1);
    if (left >= 8) {
        out_ << "  str " << xreg << ", [" << base << ", #" << off << "]\n";
        return;
    }

    int done = 0, shifted = 0;
    auto bring = [&](int want) {
        if (want == shifted) return;
        out_ << "  lsr " << xreg << ", " << xreg << ", #" << (want - shifted) * 8
             << "\n";
        shifted = want;
    };
    if (left - done >= 4) {
        out_ << "  str " << w << ", [" << base << ", #" << (off + done) << "]\n";
        done += 4;
    }
    if (left - done >= 2) {
        bring(done);
        out_ << "  strh " << w << ", [" << base << ", #" << (off + done) << "]\n";
        done += 2;
    }
    if (left - done >= 1) {
        bring(done);
        out_ << "  strb " << w << ", [" << base << ", #" << (off + done) << "]\n";
    }
}

// **A realigned frame is addressed from its floor in x19**: slot k at x19 + top - k, the top rounded
// to the alignment, so a slot the parser aligned stays aligned - see Function::frameAlign.
void Arm64Darwin::localIn(const char *dst, int slot) {
    if (realign_ > 0) {
        movImm("x9", top_ - slot);
        out_ << "  add " << dst << ", x19, x9\n";
    } else {
        movImm("x9", slot);
        out_ << "  sub " << dst << ", x29, x9\n";
    }
}

// **The TLV descriptor's getter, measured from clang**: its address from the page, its first word called with the
// descriptor in x0, the object's address answered in x0. The getter and this unit's wrappers keep every register but
// x0, x9, x16 and x17 (dyld's rule). The walker's and the selector's thread_local address.
void Arm64Darwin::tlsAddress(const std::string &symbol, bool viaWrapper) {
    if (viaWrapper) {
        out_ << "  bl _" << tlsWrapperName(symbol) << "\n";
        return;
    }
    out_ << "  adrp x0, _" << symbol << "@TLVPPAGE\n";
    out_ << "  ldr x0, [x0, _" << symbol << "@TLVPPAGEOFF]\n";
    out_ << "  ldr x9, [x0]\n  blr x9\n";
}

void Arm64Darwin::copyBlock(int size, const char *from, const char *to) {
    int off = 0;
    while (size - off >= 8) {
        out_ << "  ldr x10, [" << from << ", #" << off << "]\n";
        out_ << "  str x10, [" << to << ", #" << off << "]\n";
        off += 8;
    }
    while (size - off >= 4) {
        out_ << "  ldr w10, [" << from << ", #" << off << "]\n";
        out_ << "  str w10, [" << to << ", #" << off << "]\n";
        off += 4;
    }
    while (size - off >= 2) {
        out_ << "  ldrh w10, [" << from << ", #" << off << "]\n";
        out_ << "  strh w10, [" << to << ", #" << off << "]\n";
        off += 2;
    }
    while (size - off >= 1) {
        out_ << "  ldrb w10, [" << from << ", #" << off << "]\n";
        out_ << "  strb w10, [" << to << ", #" << off << "]\n";
        off += 1;
    }
}

void Arm64Darwin::load(const Type *t) {
    if (t->isArray() || t->isStructOrUnion()) return;
    if (t->isFloating()) {
        out_ << "  ldr " << fpReg(t, 0) << ", [x0]\n";
        return;
    }

    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    if (sz == 1)      out_ << (sign ? "  ldrsb x0, [x0]\n" : "  ldrb w0, [x0]\n");
    else if (sz == 2) out_ << (sign ? "  ldrsh x0, [x0]\n" : "  ldrh w0, [x0]\n");
    else if (sz == 4) out_ << (sign ? "  ldrsw x0, [x0]\n" : "  ldr w0, [x0]\n");
    else              out_ << "  ldr x0, [x0]\n";
}

int Arm64Darwin::stackArgSlot(const Type *t, int &at) const {
    at = alignTo(at, t->align(target_));
    int here = at;
    at += t->size(target_);
    return here;
}

int Arm64Darwin::aggStackSlot(const Type *t, const AggPlan &p, int &at) const {
    if (p.byRef) {
        at = alignTo(at, 8);
        int here = at;
        at += 8;
        return here;
    }
    int a = t->align(target_);
    if (a < 8) a = 8;
    at = alignTo(at, a);
    int here = at;
    at += alignTo(t->size(target_), 8);
    return here;
}

// **The runtime arrives here with two values in registers**, and this is the
// whole of what the backend does about it: x0 the exception object and x1 the
// selector, into frame slots the parser knows. From then on they are locals.
void Arm64Darwin::landingPad(int pointerSlot, int selectorSlot) {
    localIn("x9", pointerSlot);
    out_ << "  str x0, [x9]\n";
    localIn("x9", selectorSlot);
    out_ << "  str w1, [x9]\n";
}

// **The language-specific data area, laid out exactly as clang lays it out** -
// every number read off clang's output, since the personality routine trusts
// the header.
static const Regions::LsdaSpelling kMachOLsda = {
    "L", ".section __TEXT,__gcc_except_tab", true, "_", "@GOT"
};

void Arm64Darwin::emitLsda(const std::string &symbol) {
    out_ << lsdaTable(kMachOLsda, symbol, lsdaTypes_);
}

void Arm64Darwin::storeThrough(const Type *t, const char *addrReg) {
    if (t->isFloating()) {
        out_ << "  str " << fpReg(t, 0) << ", [" << addrReg << "]\n";
        return;
    }
    switch (t->size(target_)) {
    case 1:  out_ << "  strb w0, [" << addrReg << "]\n"; return;
    case 2:  out_ << "  strh w0, [" << addrReg << "]\n"; return;
    case 4:  out_ << "  str w0, [" << addrReg << "]\n"; return;
    default: out_ << "  str x0, [" << addrReg << "]\n"; return;
    }
}

// Darwin's va_list is a char pointer, at x0's address: va_start points it past the named stack words, va_arg steps it
// by 8 and loads the word it passed. The walker's and the selector's.
void Arm64Darwin::vaStartAt() {
    out_ << "  add x1, x29, #" << (16 + namedStackBytes_) << "\n";
    out_ << "  str x1, [x0]\n";
}

void Arm64Darwin::vaArgAt(const Type *t) {
    out_ << "  ldr x1, [x0]\n";
    out_ << "  add x2, x1, #8\n";
    out_ << "  str x2, [x0]\n";
    out_ << "  mov x0, x1\n";
    load(t);
}

void Arm64Darwin::genConversion(const Type *from, const Type *to) {
    if (to->isVoid()) return;

    bool fromF = from->isFloating(), toF = to->isFloating();

    if (fromF && toF) {

        if (fpReg(to, 0) != fpReg(from, 0))
            out_ << "  fcvt " << fpReg(to, 0) << ", " << fpReg(from, 0) << "\n";
        return;
    }
    if (!fromF && toF) {

        out_ << (from->isSigned(target_) ? "  scvtf " : "  ucvtf ")
             << fpReg(to, 0) << ", x0\n";
        return;
    }
    if (fromF && !toF) {
        out_ << (to->isSigned(target_) ? "  fcvtzs x0, " : "  fcvtzu x0, ")
             << fpReg(from, 0) << "\n";
        narrowInt(to);
        return;
    }
    narrowInt(to);
}

// **One walk of the parameter list, for the call site and the prologue alike.**
// Both made this decision by hand, and on x86 the same shape let the caller and
// the callee agree wrongly with nothing in the suite able to see it.
Arm64Darwin::Placement Arm64Darwin::placeArguments(
        const std::vector<const Type *> &types) const {
    Placement out;
    out.args.resize(types.size());
    int ints = 0, floats = 0, at = 0;
    for (std::size_t i = 0; i < types.size(); i++) {
        const Type *t = types[i];
        ArgPlace &p = out.args[i];
        if (t->isStructOrUnion()) {
            p.plan = planFor(t);
            // A homogeneous float aggregate takes float registers or none: once
            // it goes to the stack the rest of its kind follow it there, which
            // is what saturating the counter says.
            if (p.plan.hfa > 0) {
                if (floats + p.plan.hfa <= abi_.sseCount) {
                    p.inRegister = true;
                    p.firstReg = floats;
                    floats += p.plan.hfa;
                } else {
                    p.stackOffset = aggStackSlot(t, p.plan, at);
                    floats = abi_.sseCount;
                }
            } else {
                if (ints + p.plan.words <= abi_.intCount) {
                    p.inRegister = true;
                    p.firstReg = ints;
                    ints += p.plan.words;
                } else {
                    p.stackOffset = aggStackSlot(t, p.plan, at);
                    ints = abi_.intCount;
                }
            }
            continue;
        }
        if (t->isFloating()) {
            if (floats < abi_.sseCount) {
                p.inRegister = true;
                p.firstReg = floats++;
                continue;
            }
        } else if (ints < abi_.intCount) {
            p.inRegister = true;
            p.firstReg = ints++;
            continue;
        }
        p.stackOffset = stackArgSlot(t, at);
    }
    out.intsUsed = ints;
    out.floatsUsed = floats;
    out.stackBytes = at;
    return out;
}

void Arm64Darwin::defineLabel(const std::string &l) { out_ << l << ":\n"; }

void Arm64Darwin::caseBranch(long long v, const std::string &l) {
    if (v >= 0 && v < 4096) {
        out_ << "  cmp x0, #" << v << "\n";
    } else {
        movImm("x9", v);
        out_ << "  cmp x0, x9\n";
    }
    out_ << "  beq " << l << "\n";
}

// A class's address in x0 becomes the function's result: copied through the caller's pointer, or into d0-d3 or x0/x1.
void Arm64Darwin::returnAgg(const Type *t) {
    AggPlan p = planFor(t);
    if (p.byRef) {
        localIn("x9", sretSlot_);
        out_ << "  ldr x1, [x9]\n";
        copyBlock(t->size(target_), "x0", "x1");
        out_ << "  mov x0, x1\n";
    } else if (p.hfa > 0) {
        const char *w = (p.elem == Kind::Float) ? "s" : "d";
        int step = (p.elem == Kind::Float) ? 4 : 8;
        for (int k = 0; k < p.hfa; k++)
            out_ << "  ldr " << w << k << ", [x0, #" << (k * step) << "]\n";
    } else {

        static const char *const ret[2] = { "x0", "x1" };
        for (int k = p.words; k-- > 0; )
            out_ << "  ldr " << ret[k] << ", [x0, #" << (k * 8) << "]\n";
    }
}

// A class at [x9] into the registers its place names: the address itself, an HFA's lanes, or its words.
void Arm64Darwin::aggToRegisters(const AggPlan &p, int firstReg) {
    if (p.byRef) {
        out_ << "  mov " << abi_.intRegs[firstReg] << ", x9\n";
    } else if (p.hfa > 0) {
        const char *w = (p.elem == Kind::Float) ? "s" : "d";
        int step = (p.elem == Kind::Float) ? 4 : 8;
        for (int k = 0; k < p.hfa; k++)
            out_ << "  ldr " << w << (firstReg + k)
                 << ", [x9, #" << (k * step) << "]\n";
    } else {
        for (int k = 0; k < p.words; k++)
            out_ << "  ldr " << abi_.intRegs[firstReg + k]
                 << ", [x9, #" << (k * 8) << "]\n";
    }
}

// A call's class result into the object at [x9], whose address is then the call's value in x0.
void Arm64Darwin::aggResultInto(const Type *t) {
    AggPlan p = planFor(t);
    if (p.byRef) {
    } else if (p.hfa > 0) {
        const char *w = (p.elem == Kind::Float) ? "s" : "d";
        int step = (p.elem == Kind::Float) ? 4 : 8;
        for (int k = 0; k < p.hfa; k++)
            out_ << "  str " << w << k << ", [x9, #" << (k * step) << "]\n";
    } else {
        static const char *const ret[2] = { "x0", "x1" };
        for (int k = 0; k < p.words; k++)
            storeWord(ret[k], "x9", k, t->size(target_));
    }
    out_ << "  mov x0, x9\n";
}

static int p2AlignOf(int bytes) {
    int p = 0;
    while ((1 << p) < bytes) p++;
    return p;
}

void Arm64Darwin::emitGlobal(const Global &g, Segment seg) {
    int size = g.type->size(target_);
    int p2 = p2AlignOf(g.align > objectAlign(g.type, target_) ? g.align : objectAlign(g.type, target_));
    if (!g.isStatic) out_ << "  .globl _" << g.symbol << "\n";
    // A writable weak object is one object across the program - a template's
    // static member - so it may not be hidden per unit; clang writes
    // `.weak_definition` for it, in __data with `.space` rather than zerofill.
    const bool writable = seg == Segment::Data || seg == Segment::Bss;
    if (g.isInline)
        out_ << (writable ? "  .weak_definition _" : "  .weak_def_can_be_hidden _")
             << g.symbol << "\n";

    if (seg == Segment::Bss && g.isInline) {
        out_ << "  .section __DATA,__data\n  .p2align " << p2 << "\n_"
             << g.symbol << ":\n  .space " << size << "\n";
        return;
    }
    if (seg == Segment::Bss) {
        out_ << "  .zerofill __DATA,__bss,_" << g.symbol << ","
             << size << "," << p2 << "\n";
        return;
    }

    // **A thread_local is a descriptor and a template**, measured from clang: `_x` is three words in
    // __thread_vars - dyld's bootstrap getter, a key dyld fills, the template's address - and the
    // template `_x$tlv$init` is the initial image, in __thread_data or zero-filled by `.tbss`.
    const bool tls = seg == Segment::TlsData || seg == Segment::TlsBss;
    if (tls) {
        if (seg == Segment::TlsBss) out_ << ".tbss _" << g.symbol << "$tlv$init, " << size << ", " << p2 << "\n";
        out_ << "  .section __DATA,__thread_vars,thread_local_variables\n_" << g.symbol
             << ":\n  .quad __tlv_bootstrap\n  .quad 0\n  .quad _" << g.symbol << "$tlv$init\n";
        if (seg == Segment::TlsBss) return;
        out_ << "  .section __DATA,__thread_data,thread_local_regular\n";
    }
    out_ << "  .p2align " << p2 << "\n";
    out_ << "_" << g.symbol << (tls ? "$tlv$init:\n" : ":\n");
    int at = 0;
    for (const GlobalPiece &p : g.init) {
        if (p.offset > at) out_ << "  .space " << (p.offset - at) << "\n";

        if (!p.symbol.empty()) {
            out_ << "  .quad " << (p.symbol[0] == '.' ? "" : "_") << p.symbol;
            if (p.value > 0) out_ << "+" << p.value;
            else if (p.value < 0) out_ << "-" << -p.value;
            out_ << "\n";
            at = p.offset + p.size;
            continue;
        }

        switch (p.size) {
        case 1:  out_ << "  .byte " << p.value << "\n"; break;
        case 2:  out_ << "  .short " << p.value << "\n"; break;
        case 4:  out_ << "  .long " << p.value << "\n"; break;
        default: out_ << "  .quad " << p.value << "\n"; break;
        }
        at = p.offset + p.size;
    }
    if (at < size) out_ << "  .space " << (size - at) << "\n";
}

void Arm64Darwin::emitData(const Program &program) {

    for (int pass = 0; pass < 2; pass++) {
        bool wantWide = (pass == 1);
        bool opened = false;
        for (const StringLit &s : program.strings) {
            if ((s.width > 1) != wantWide) continue;
            if (!opened) {
                out_ << (wantWide ? "  .section __TEXT,__const\n"
                                  : "  .section __TEXT,__cstring,cstring_literals\n");
                opened = true;
            }
            if (s.width > 1) {
                int p2 = 0;
                while ((1 << p2) < s.width) p2++;
                out_ << "  .p2align " << p2 << "\n";
            }
            out_ << s.label << ":\n";
            out_ << "  .ascii \"";
            for (unsigned char c : s.bytes) {
                if (c == '"' || c == '\\') out_ << '\\' << c;
                else if (c == '\n')        out_ << "\\n";
                else if (c == '\t')        out_ << "\\t";
                else if (c >= 32 && c < 127) out_ << c;
                else out_ << '\\' << static_cast<char>('0' + ((c >> 6) & 7))
                          << static_cast<char>('0' + ((c >> 3) & 7))
                          << static_cast<char>('0' + (c & 7));
            }
            out_ << "\"\n";
        }
    }

    struct Bucket { Segment seg; const char *open; };
    const Bucket order[] = {
        { Segment::Const, "  .section __TEXT,__const\n" },

        { Segment::ConstRelocated, "  .section __DATA,__const\n" },
        { Segment::Data,  "  .section __DATA,__data\n" },
        { Segment::Bss,   nullptr },
        { Segment::TlsData, nullptr },
        { Segment::TlsBss, nullptr },
    };
    for (const Bucket &b : order) {
        bool opened = false;
        for (const Global &g : program.globals) {
            if (segmentFor(g) != b.seg) continue;
            if (!opened && b.open != nullptr) { out_ << b.open; }
            opened = true;
            emitGlobal(g, b.seg);
        }
    }
}

void Arm64Darwin::emitFunction(const Function &fn) {
    resetLabels();
    // Labels are built from the symbol rather than the name: two overloads
    // share a name and must not share a label.
    functionName_ = fn.symbol();
    labelPrefix_ = "L." + fn.symbol() + ".";
    returnLabel_ = "L.return." + fn.symbol();
    // Selected from SSA, the only path (M5.3): a body outside the selector's subset is refused by name.
    const ir::Function &sf = ssaChoose(fn);
    const bool pads = sf.landingPads;

    out_ << "  .section __TEXT,__text,regular,pure_instructions\n";
    if (!fn.isStatic()) out_ << "  .globl _" << fn.symbol() << "\n";
    // **An inline definition may appear in several translation units**, so the
    // linker folds the copies rather than rejecting them. Mach-O's spelling,
    // measured from clang, keeps the `.globl` beside it.
    if (fn.isInline())
        out_ << "  .weak_def_can_be_hidden _" << fn.symbol() << "\n";
    out_ << "  .p2align 2\n";
    out_ << "_" << fn.symbol() << ":\n";
    // A second name for the same code - see Function::alias. It is a label at
    // the same address rather than a copy of the body.
    if (!fn.alias().empty()) {
        if (!fn.isStatic()) out_ << "  .globl _" << fn.alias() << "\n";
        if (fn.isInline())
            out_ << "  .weak_def_can_be_hidden _" << fn.alias() << "\n";
        out_ << "_" << fn.alias() << ":\n";
    }
    if (const Source *src = lineSource()) {
        Source::Place at = src->locate(fn.pos());
        DwarfFunction d;
        d.name = fn.displayName();
        d.begin = "Lfunc.begin." + fn.symbol();
        d.end = "Lfunc.end." + fn.symbol();
        d.file = at.file + 1;
        d.line = at.line;
        d.external = !fn.isStatic();
        d.returns = fn.returns();
        d.locals = &fn.locals();
        dwarfFns_.push_back(d);
        resetBlocks(fn.blocks());
        out_ << d.begin << ":\n";
    } else if (pads) {
        // The call-site table measures from here, so the label has to exist
        // whether or not there is debug information.
        out_ << "Lfunc.begin." << fn.symbol() << ":\n";
    }
    clearCallSites();

    markLine(fn.pos());
    // **Unwind data, and it is the same three lines in every function here.**
    out_ << "  .cfi_startproc\n";
    // **A function with a landing pad names its personality and its table here**,
    // before anything else: the unwinder finds both through the CFI, and the
    // table itself is written after the body, once its labels exist.
    if (pads) {
        out_ << "  .cfi_personality 155, ___gxx_personality_v0\n";
        out_ << "  .cfi_lsda 16, Lexception." << fn.symbol() << "\n";
    }
    out_ << "  stp x29, x30, [sp, #-16]!\n";
    out_ << "  mov x29, sp\n";
    out_ << "  .cfi_def_cfa w29, 16\n";
    out_ << "  .cfi_offset w30, -8\n";
    out_ << "  .cfi_offset w29, -16\n";

    // **A frame realigned for an over-aligned local** (Function::frameAlign, or a named local's own)
    // saves x19 under the frame record, rounds sp down after the allocation - clang's `and sp, x9,
    // #0xff..c0` - and keeps the floor in x19 for localIn; the selector gives x19 to no value then.
    realign_ = fn.frameAlign() > 16 ? fn.frameAlign() : 0;
    for (const Local &l : fn.locals())
        if (l.staticName.empty() && l.type != nullptr) {
            const int a = l.align > objectAlign(l.type, target_) ? l.align : objectAlign(l.type, target_);
            if (a > 16 && a > realign_) realign_ = a;
        }
    for (std::size_t i = 0; i < sf.frame.size(); i++) {
        const int a = std::max(sf.frame[i].align, ssaTypes_->type(sf.frame[i].type)->align(target_));
        if (a > 16 && a > realign_) realign_ = a;
    }
    if (sf.frameAlign > 16 && sf.frameAlign > realign_) realign_ = sf.frameAlign;
    if (realign_ > 0) {
        out_ << "  str x19, [sp, #-16]!\n";
        out_ << "  .cfi_offset w19, -32\n";
    }
    // Below the locals, room for the callee-saved registers the selector's values took (ssaSaved_).
    const int locals = alignTo(layoutSsa(fn, sf), 16);
    if (lineSource()) dwarfFns_.back().locals = &ssaFrameLocals(fn, sf, ssaAllocaSlot_, "a64");
    int frame = locals + alignTo(8 * static_cast<int>(ssaSaved_.size()), 16);
    if (realign_ > 0) frame = top_ = alignTo(locals, realign_);
    if (realign_ > 0) {
        // `and` takes no sp as its source: the allocation is made in x9 and sp rounded from it, as clang writes it.
        char mask[32];
        std::snprintf(mask, sizeof mask, "#0x%llx", static_cast<unsigned long long>(-static_cast<long long>(realign_)));
        movImm("x9", frame);
        out_ << "  sub x9, sp, x9\n  and sp, x9, " << mask << "\n  mov x19, sp\n";
    } else if (frame > 0) {
        movImm("x9", frame);
        out_ << "  sub sp, sp, x9\n";
    }
    // The sret store, the parameters' and the body follow the saves of the registers the selector took.
    const std::string head = out_.str();
    out_.str(std::string());

    sretSlot_ = fn.sretSlot();
    if (sretSlot_ != 0) {
        localIn("x9", sretSlot_);
        out_ << "  str x8, [x9]\n";
    }

    // **The same walk the call site made, made once.** placeArguments answers
    // where each parameter is; the base is this side's, an incoming stack
    // argument sitting above the saved frame pointer and return address.
    const std::vector<Param> &ps = fn.params();
    std::vector<const Type *> ptypes;
    ptypes.reserve(ps.size());
    for (std::size_t i = 0; i < ps.size(); i++) ptypes.push_back(ps[i].type);
    const Placement plan = placeArguments(ptypes);

    for (std::size_t i = 0; i < ps.size(); i++) {
        const ArgPlace &pl = plan.args[i];
        if (ps[i].type->isStructOrUnion()) {
            const AggPlan &p = pl.plan;
            const bool inRegister = pl.inRegister;
            const int from = pl.stackOffset;

            localIn("x9", ps[i].offset);

            if (!inRegister) {
                out_ << "  mov x11, x9\n";
                out_ << "  add x9, x29, #" << (16 + from) << "\n";
                if (p.byRef) out_ << "  ldr x9, [x9]\n";
                copyBlock(ps[i].type->size(target_), "x9", "x11");
                continue;
            }

            if (p.byRef) {
                out_ << "  mov x11, x9\n";
                copyBlock(ps[i].type->size(target_), abi_.intRegs[pl.firstReg], "x11");
            } else if (p.hfa > 0) {
                const char *w = (p.elem == Kind::Float) ? "s" : "d";
                int step = (p.elem == Kind::Float) ? 4 : 8;
                for (int k = 0; k < p.hfa; k++)
                    out_ << "  str " << w << (pl.firstReg + k)
                         << ", [x9, #" << (k * step) << "]\n";
            } else {
                for (int k = 0; k < p.words; k++)
                    storeWord(abi_.intRegs[pl.firstReg + k], "x9", k,
                              ps[i].type->size(target_));
            }
            continue;
        }
        localIn("x9", ps[i].offset);

        if (!pl.inRegister) {
            out_ << "  mov x11, x9\n";
            out_ << "  add x9, x29, #" << (16 + pl.stackOffset) << "\n";
            copyBlock(ps[i].type->size(target_), "x9", "x11");
            continue;
        }

        if (ps[i].type->isFloating()) {
            out_ << "  str " << fpReg(ps[i].type, pl.firstReg) << ", [x9]\n";
        } else {
            out_ << "  mov x0, " << abi_.intRegs[pl.firstReg] << "\n";
            storeThrough(ps[i].type, "x9");
        }
    }

    namedStackBytes_ = alignTo(plan.stackBytes, 8);

    selectBody(fn, sf);
    const std::vector<std::string> saved = ssaSaved_;
    const std::string body = out_.str();
    out_.str(std::string());
    out_ << head;
    // Each register a value took is saved below the locals, where the unwinder is told to find it.
    for (std::size_t i = 0; i < saved.size(); i++) {
        const int at = locals + 8 * static_cast<int>(i + 1);
        if (at <= 255) out_ << "  stur " << saved[i] << ", [x29, #-" << at << "]\n";
        else out_ << "  mov x9, #" << at << "\n  sub x9, x29, x9\n  str " << saved[i] << ", [x9]\n";
        out_ << "  .cfi_offset " << (saved[i][0] == 'x' ? "w" : "b") << saved[i].substr(1) << ", -" << (16 + at) << "\n";
    }
    out_ << body;

    out_ << "  mov x0, #0\n";
    out_ << returnLabel_ << ":\n";
    for (std::size_t i = 0; i < saved.size(); i++) {
        const int at = locals + 8 * static_cast<int>(i + 1);
        if (at <= 255) out_ << "  ldur " << saved[i] << ", [x29, #-" << at << "]\n";
        else out_ << "  mov x9, #" << at << "\n  sub x9, x29, x9\n  ldr " << saved[i] << ", [x9]\n";
    }
    if (realign_ > 0) out_ << "  ldur x19, [x29, #-16]\n";
    out_ << "  mov sp, x29\n";
    out_ << "  ldp x29, x30, [sp], #16\n";
    out_ << "  ret\n";
    // The end label goes before .cfi_endproc and before the table, because
    // the last call-site range measures up to it.
    if (lineSource() || !callSites().empty())
        out_ << "Lfunc.end." << fn.symbol() << ":\n";
    // The records measure from x19, DWARF's register 19, up to the top of a realigned frame.
    if (lineSource() && realign_ > 0) { dwarfFns_.back().frameBaseReg = 19; dwarfFns_.back().frameBias = top_; }
    out_ << "  .cfi_endproc\n";
    if (!callSites().empty()) {
        emitLsda(fn.symbol());
        out_ << "  .section __TEXT,__text,regular,pure_instructions\n";
    }
    if (lineSource()) dwarfFns_.back().blocks = blocks();
}

void Arm64Darwin::emitLoc(int file, int line, int column) {
    out_ << "  .loc " << file << " " << line << " " << column << "\n";
}

// **The wrappers as clang writes them on Darwin**: `_ZTW` only for the objects this unit defines
// (a unit reaching another's calls the defining unit's), global and strong; `_ZTH` an alias of a
// shell round `__tls_init` keeping every register but x0, x9, x16, x17 - what its callers rely on.
void Arm64Darwin::emitTlsWrappers(const Program &program) {
    bool dynamic = false;
    for (const TlsWrapper &w : program.tlsWrappers) {
        if (!w.definedHere) continue;
        if (w.dynamicInit) dynamic = true;
        const std::string name = tlsWrapperName(w.symbol);
        out_ << "  .section __TEXT,__text,regular,pure_instructions\n";
        if (!w.internal) out_ << "  .globl _" << name << "\n";
        out_ << "  .p2align 2\n_" << name << ":\n  .cfi_startproc\n  stp x29, x30, [sp, #-16]!\n  mov x29, sp\n"
                "  .cfi_def_cfa w29, 16\n  .cfi_offset w30, -8\n  .cfi_offset w29, -16\n";
        if (w.dynamicInit) out_ << "  bl _" << tlsInitName(w.symbol) << "\n";
        out_ << "  adrp x0, _" << w.symbol << "@TLVPPAGE\n  ldr x0, [x0, _" << w.symbol << "@TLVPPAGEOFF]\n"
                "  ldr x9, [x0]\n  blr x9\n  ldp x29, x30, [sp], #16\n  ret\n  .cfi_endproc\n";
    }
    if (!dynamic) return;
    static const char *const saved[] = {
        "x1, x2", "x3, x4", "x5, x6", "x7, x8", "x10, x11", "x12, x13", "x14, x15", "x16, x17",
        "d0, d1", "d2, d3", "d4, d5", "d6, d7", "d16, d17", "d18, d19", "d20, d21", "d22, d23",
        "d24, d25", "d26, d27", "d28, d29", "d30, d31" };
    const std::string fast = "___tls_init_fast";
    out_ << "  .section __TEXT,__text,regular,pure_instructions\n  .p2align 2\n" << fast
         << ":\n  .cfi_startproc\n  sub sp, sp, #336\n  stp x29, x30, [sp, #320]\n  add x29, sp, #320\n"
            "  .cfi_def_cfa w29, 16\n  .cfi_offset w30, -8\n  .cfi_offset w29, -16\n";
    for (int i = 0; i < 20; i++) out_ << "  stp " << saved[i] << ", [sp, #" << 16 * i << "]\n";
    out_ << "  bl _" << program.tlsInitFunction << "\n";
    for (int i = 19; i >= 0; i--) out_ << "  ldp " << saved[i] << ", [sp, #" << 16 * i << "]\n";
    out_ << "  ldp x29, x30, [sp, #320]\n  add sp, sp, #336\n  ret\n  .cfi_endproc\n";
    for (const TlsWrapper &w : program.tlsWrappers)
        if (w.definedHere && w.dynamicInit) out_ << "_" << tlsInitName(w.symbol) << " = " << fast << "\n";
}

void Arm64Darwin::run(const Program &program) {
    dialect_ = program.dialect;
    definedHere_.clear();
    for (const Global &g : program.globals)   definedHere_.insert(g.symbol);
    for (const Function &f : program.functions) definedHere_.insert(f.symbol());

    if (const Source *src = lineSource()) {
        const std::vector<std::string> &names = src->files();
        // A name escaped as the string it is: a Windows host's include directory is `C:\\...`, which Apple's assembler refuses bare.
        for (std::size_t i = 0; i < names.size(); i++) {
            out_ << "  .file " << (i + 1) << " \"";
            for (const char c : names[i]) { if (c == '"' || c == '\\') out_ << '\\'; out_ << c; }
            out_ << "\"\n";
        }
    }

    emitData(program);
    for (const Function &fn : program.functions) emitFunction(fn);
    emitTlsWrappers(program);
    // Measured from clang: the init function's address in __mod_init_func.
    if (!program.initFunction.empty())
        out_ << "  .section __DATA,__mod_init_func,mod_init_funcs\n"
                "  .p2align 3, 0x0\n  .quad _" << program.initFunction << "\n";
    if (const Source *src = lineSource()) {
        for (const Global &g : program.globals) {
            // A thread_local has no address the records could hold: not described yet.
            if (g.isThreadLocal) continue;
            DwarfGlobal dg;
            dg.name = g.name;
            dg.symbol = g.symbol;
            dg.type = g.type;
            dg.external = !g.isStatic;
            dwarfGlobals_.push_back(dg);
        }
        std::string dwarf;
        writeDwarf(dwarf, kMachODwarf, target_, src->files().front(), compDir(),
                   dwarfFns_, dwarfGlobals_, dialect_);
        out_ << dwarf;
    }
    out_ << ".subsections_via_symbols\n";
    if (ssa_ != nullptr && std::getenv("CXX1_SSA_WHY") != nullptr)
        std::fprintf(stderr, "a64-select: selected=%d of %d\n", ssaSelected_, ssaSelected_);
    sink_ << out_.str();
}
