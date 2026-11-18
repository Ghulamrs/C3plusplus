#include "Tms6747.h"
#include "../optimizer/C6xSched.h"
#include "../optimizer/OptDeclines.h"
#include "../optimizer/DivMagic.h"

#include "../Abi.h"
#include "../Source.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---- the target: sizes for a 32-bit C6000 (EABI) -------------------------
int Tms6747Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                     return 1;
    case Kind::Bool:                                     return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar: return 1;
    case Kind::Short: case Kind::UShort:                 return 2;
    case Kind::WChar:                                    return sizeOf(wcharType());
    case Kind::Char16:                                   return 2;
    case Kind::Char32:                                   return 4;
    case Kind::Int: case Kind::UInt:                     return 4;
    case Kind::Long: case Kind::ULong:                   return 4;
    case Kind::LongLong: case Kind::ULongLong:           return 8;
    case Kind::Float:                                    return 4;
    case Kind::Double: case Kind::LongDouble:            return 8;
    case Kind::Pointer: case Kind::NullPtr:              return 4;
    default:
        std::fprintf(stderr, "target: no size for this type yet (tms6747)\n");
        compileFailed();
    }
}
int Tms6747Target::alignOf(Kind k) const { return sizeOf(k); }

// ---- the backend ---------------------------------------------------------
// C6000 EABI argument registers, in order; the result comes back in A4.
static const char *const kTiIntRegs[] = {
    "A4", "B4", "A6", "B6", "A8", "B8", "A10", "B10", "A12", "B12"
};

// Set by name, as the host ABIs are. structReturnLimit 8: a class of 8 bytes or less
// comes back in A5:A4, a larger one through the pointer the caller hands over in A3 -
// TI's rule, measured against cl6x - and goes in by the address of a copy.
static Abi ti() {
    Abi a;
    a.intRegs = kTiIntRegs;             a.intCount = 10;
    a.positional = true;                a.structReturnLimit = 8;
    a.scratch = "A0";                   a.scratch32 = "A0";
    a.elfSymbolAttributes = true;
    return a;
}

const Abi &Tms6747Backend::abi() const {
    static const Abi kAbi = ti();
    return kAbi;
}

const char *const *Tms6747Backend::identityMacros() const {
    static const char *const kMacros[] = {
        "__tms320c6x__=1", "__TMS320C6X__=1", "__TMS320C6700__=1",
        "__TMS320C6740__=1", "__C6X__=1", "__LITTLE_ENDIAN__=1", nullptr
    };
    return kMacros;
}

std::unique_ptr<CodeGen> Tms6747Backend::codegen(std::ostream &sink, Syntax) const {
    return std::unique_ptr<CodeGen>(new Tms6747(sink, target_, abi()));
}

// ---- small helpers -------------------------------------------------------
void Tms6747::unsupported(const char *what) {
    std::fprintf(stderr, "codegen: %s is not supported yet by the tms6747 backend\n",
                 what);
    compileFailed();
}

// A 32-bit constant in two halves: MVKL takes the low 16 (sign extended), MVKH
// the high 16. Works for any A- or B-file register.
void Tms6747::movImm(const char *reg, long long value) {
    long long v = static_cast<int>(value);
    out_ << "\tMVKL\t" << v << ", " << reg << "\n";
    out_ << "\tMVKH\t" << v << ", " << reg << "\n";
}

// A symbol as the assembler sees it. The parser names string literals
// `.L.str.N`, GNU style; a leading dot reads as a directive, so it is dropped:
// `L.str.N` beside the `L.` code labels, tiSpelling respelling the rest.
std::string Tms6747::symName(const std::string &sym) {
    return sym[0] == '.' ? sym.substr(1) : sym;
}

// The same in an operand or a data word, where a name spelled like a register would be read as one (R114).
static std::string symRef(const std::string &sym) { return c6x::symbolOperand(sym[0] == '.' ? sym.substr(1) : sym); }

// The TI assembler admits no dot inside a name, `$` it does: the finished text
// is respelled once, every dot between two name characters (outside a quoted
// string, a comment, or a number) becoming `$` - `L$return$main`, `f$arr`.
static std::string tiSpelling(const std::string &text) {
    std::string out = text;
    bool quoted = false, comment = false, name = false;
    for (size_t i = 0; i < out.size(); i++) {
        char c = out[i];
        if (c == '\n') { quoted = comment = name = false; continue; }
        if (comment) continue;
        if (quoted) { if (c == '\\') i++; else if (c == '"') quoted = false; continue; }
        if (c == '"') { quoted = true; name = false; continue; }
        if (c == ';') { comment = true; continue; }
        bool word = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
        char next = i + 1 < out.size() ? out[i + 1] : ' ';
        if (c == '.' && name &&
            (std::isalnum(static_cast<unsigned char>(next)) || next == '_' || next == '$'))
            out[i] = '$';
        else if (!word) name = false;
        else if (!name && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) name = true;
    }
    return out;
}

// The TI assembler takes an undefined name for an error unless it is
// declared: the finished text is read once more, and every name it uses but
// neither defines nor declares - printf, the helpers, the runtime - gets a .ref.
static std::string tiExternals(const std::string &text) {
    std::set<std::string> defined, used;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t cut = line.find(';');
        if (cut != std::string::npos) line.erase(cut);
        if (line.compare(0, 2, "||") == 0) line = "\t" + line.substr(2);   // in a packet: not a label
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        bool label = first == 0;
        if (line[first] == '[') {                                   // a predicate
            cut = line.find(']', first);
            line = cut == std::string::npos ? std::string() : line.substr(cut + 1);
        }
        std::vector<std::string> words;
        for (size_t i = 0; i < line.size();) {
            char c = line[i];
            if (c == '"') { cut = line.find('"', i + 1); i = cut == std::string::npos ? line.size() : cut + 1; continue; }
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$' && c != '.') { i++; continue; }
            size_t j = i;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_' ||
                                       line[j] == '$' || line[j] == '.')) j++;
            words.push_back(line.substr(i, j - i));
            if (line.compare(j, 2, "+0") == 0 && c6x::symbolOperand(words.back()) != words.back()) words.back() += "+0";
            i = j;
        }
        size_t k = 0;
        if (label && !words.empty()) { defined.insert(words[0]); k = 1; }
        if (k >= words.size()) continue;
        const std::string &m = words[k];
        bool declares = m == ".global" || m == ".weak" || m == ".def" || m == ".ref" || m == ".bss";
        if (declares) { if (k + 1 < words.size()) defined.insert(words[k + 1]); continue; }
        if (m[0] == '.' && m != ".word" && m != ".ulong" && m != ".half") continue;
        for (size_t w = k + 1; w < words.size(); w++) {
            const std::string &t = words[w];
            if (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '$') continue;   // a number, an operator
            if (t.size() > 2 && t.compare(t.size() - 2, 2, "+0") == 0) { used.insert(t.substr(0, t.size() - 2)); continue; }
            if ((t[0] == 'A' || t[0] == 'B') && t.size() <= 3 && t.size() > 1 &&
                std::isdigit(static_cast<unsigned char>(t[1])) && (t.size() == 2 || std::isdigit(static_cast<unsigned char>(t[2]))))
                continue;                                           // a register
            used.insert(t);
        }
    }
    std::string out = text;
    for (const std::string &n : used)
        if (!defined.count(n)) out += "\t.ref\t" + n + "\n";
    return out;
}

// A symbol's address, the same two halves; the assembler and linker fill them.
void Tms6747::movSym(const char *reg, const std::string &sym) {
    out_ << "\tMVKL\t" << symRef(sym) << ", " << reg << "\n";
    out_ << "\tMVKH\t" << symRef(sym) << ", " << reg << "\n";
}

// dst = base + off. A large offset goes through the scratch register of the
// destination's file so the ADD stays on one side.
void Tms6747::regAdd(const char *base, int off, const char *dst) {
    if (off >= 0 && off <= 31) {
        out_ << "\tADD\t" << base << ", " << off << ", " << dst << "\n";
    } else {
        const char *tmp = dst[0] == 'B' ? "B0" : "A0";
        movImm(tmp, off);
        out_ << "\tADD\t" << base << ", " << tmp << ", " << dst << "\n";
    }
}

// B15 is the stack pointer. Small adjustments use the 5-bit immediate; larger
// ones go through a B-file scratch so the ADD/SUB stays on one register file.
void Tms6747::spAdjust(int delta) {
    if (delta == 0) return;
    int m = delta < 0 ? -delta : delta;
    const char *op = delta < 0 ? "SUB" : "ADD";
    if (m <= 31) {
        out_ << "\t" << op << "\tB15, " << m << ", B15\n";
    } else if (optimize_ > 0 && m <= 32767) {
        out_ << "\tADDK\t" << delta << ", B15\n";       // one word for three
    } else {
        movImm("B0", m);
        out_ << "\t" << op << "\tB15, B0, B15\n";
    }
}

static const int kSaveBytes = 40;   // under A15 whatever is saved, so a local's address does not wait on the body

// dst = A15 - saveBytes_ - off, past the saved registers; the stack parameters lie at A15 + 4 on.
// dst is an A-file register.
void Tms6747::localAddr(int off, const char *dst) {
    off = saveBytes_ + off;
    if (off >= 0 && off <= 31) {
        out_ << "\tSUB\tA15, " << off << ", " << dst << "\n";
    } else if (optimize_ > 0 && off % 4 == 0 && off / 4 <= 31) {
        out_ << "\tSUBAW\tA15, " << off / 4 << ", " << dst << "\n";   // one word, the .D unit's scaled form
    } else {
        movImm("A0", off);
        out_ << "\tSUB\tA15, A0, " << dst << "\n";
    }
}

// The ucst5 form holds 31 units of the access size, the register form 32767 (MVK into A0).
bool Tms6747::frameFits(int disp, int size) {
    int k = disp < 0 ? -disp : disp;
    return k % size == 0 && k / size <= 32767;
}

// The operand naming the frame slot at disp for an access of size bytes: `*-A15(k)` where k
// fits, the scaled register form past that, A0 written. The caller asked frameFits first.
std::string Tms6747::frameOperand(int disp, int size) {
    int k = disp < 0 ? -disp : disp;
    const char *sign = disp < 0 ? "-" : "+";
    if (k / size <= 31) return std::string("*") + sign + "A15(" + std::to_string(k) + ")";
    out_ << "\tMVK\t" << k / size << ", A0\n";
    return std::string("*") + sign + "A15[A0]";
}

int Tms6747::accessSize(const Type *t) const {
    return isWide(t) || inPairWide(t) ? 8 : t->size(target_);
}

// A struct copy, word by word then halfword and byte, each through A3 with
// the addresses formed in A0: the zero-offset forms, like every other access
// here. from and to are A-file registers other than A0 and A3.
void Tms6747::copyBlock(int size, const char *from, const char *to, int align, bool wide, const char *pred) {
    if (optimize_ > 0) { copyBlockBatched(size, from, to, align, wide, pred); return; }
    int off = 0;
    while (off < size) {
        int step = size - off >= 4 ? 4 : size - off >= 2 ? 2 : 1;
        if (step > align) step = align;     // a struct of chars may sit anywhere
        const char *ld = step == 4 ? "LDW" : step == 2 ? "LDH" : "LDB";
        const char *st = step == 4 ? "STW" : step == 2 ? "STH" : "STB";
        regAdd(from, off, "A0");
        out_ << "\t" << ld << "\t*A0, A3\n\tNOP\t4\n";
        regAdd(to, off, "A0");
        out_ << "\t" << st << "\tA3, *A0\n";
        off += step;
    }
}

// At -O1 and -O2 the loads of a batch go first, into B16-B23, and the stores after them, so the
// loads pipeline instead of each waiting its four slots (review B9); LDDW/STDW pairs where the caller
// vouches for both addresses being 8-aligned. B16-B23 are nobody's: the scheduler's push folding takes A16 up.
void Tms6747::copyBlockBatched(int size, const char *from, const char *to, int align, bool wide, const char *pred) {
    const std::string p = *pred ? std::string("\t[") + pred + "]" : "";   // every load and store under it; the address adds need none
    int off = 0;
    while (off < size) {
        // One batch: up to eight registers of words, or four pairs, from a base within ucst5 reach.
        const int base = off;
        std::string src = from, dst = to;
        if (base > 0) { regAdd(to, base, "A3"); regAdd(from, base, "A0"); src = "A0"; dst = "A3"; }   // A3 first: A0 is regAdd's scratch
        std::vector<std::pair<int, int> > chunks;              // (offset, step) of this batch
        int regs = 0, at = off;
        while (at < size && regs < 8) {
            int step = wide && size - at >= 8 ? 8 : size - at >= 4 ? 4 : size - at >= 2 ? 2 : 1;
            if (step > align && step != 8) step = align;
            if (step == 8 && (regs == 7 || (at - base) / 8 > 31)) step = 4;
            if ((at - base) / step > 31) break;
            chunks.push_back(std::make_pair(at, step));
            regs += step == 8 ? 2 : 1;
            at += step;
        }
        for (std::size_t k = 0, r = 16; k < chunks.size(); k++) {
            const int o = chunks[k].first - base, step = chunks[k].second;
            const std::string reg = step == 8 ? pairOf(("B" + std::to_string(r)).c_str()) : "B" + std::to_string(r);
            const char *ld = step == 8 ? "LDDW" : step == 4 ? "LDW" : step == 2 ? "LDH" : "LDB";
            out_ << p << "\t" << ld << "\t*" << (o ? "+" : "") << src << (o ? "(" + std::to_string(o) + ")" : "") << ", " << reg << "\n";
            r += step == 8 ? 2 : 1;
        }
        out_ << "\tNOP\t4\n";
        for (std::size_t k = 0, r = 16; k < chunks.size(); k++) {
            const int o = chunks[k].first - base, step = chunks[k].second;
            const std::string reg = step == 8 ? pairOf(("B" + std::to_string(r)).c_str()) : "B" + std::to_string(r);
            const char *st = step == 8 ? "STDW" : step == 4 ? "STW" : step == 2 ? "STH" : "STB";
            out_ << p << "\t" << st << "\t" << reg << ", *" << (o ? "+" : "") << dst << (o ? "(" + std::to_string(o) + ")" : "") << "\n";
            r += step == 8 ? 2 : 1;
        }
        off = at;
    }
}

// A call's area - the word the callee saves A15 in, and its stack arguments. At -O1 and -O2 the
// frame keeps that word at its bottom, so a call with nothing pushed and nothing on the stack opens none.
void Tms6747::openArea(int area, bool stackArgs) {
    int a = optimize_ > 0 && !stackArgs ? 0 : area;
    areas_.push_back(a);
    spAdjust(-a);
}
void Tms6747::closeArea() {
    spAdjust(areas_.back());
    areas_.pop_back();
}

static bool backendSkipped(const char *what) {
    const char *e = std::getenv("CPP11_C6XSKIP");
    return e != nullptr && (std::string(",") + e + ",").find(std::string(",") + what + ",") != std::string::npos;
}

// ---- doubles: a register pair, and 8-byte moves through the stack --------
// A double lives in an even:odd pair, its high word in the odd register:
// A5:A4 is the accumulator, A7:A6 the second operand, B5:B4 an argument.
bool Tms6747::isDouble(const Type *t) const {
    return t->isFloating() && t->size(target_) == 8;
}
bool Tms6747::isWide(const Type *t) const {
    return (t->isFloating() || t->isInteger()) && t->size(target_) == 8;
}
std::string Tms6747::pairOf(const char *reg) {
    int n = std::atoi(reg + 1);
    return std::string(1, reg[0]) + std::to_string(n + 1) + ":" + reg;
}

void Tms6747::load(const Type *t) { loadFrom(t, "*A4"); }
void Tms6747::loadFrom(const Type *t, const std::string &mem) {
    if (t->isArray() || t->isStructOrUnion()) return;   // the address is the value
    if (isWide(t)) { out_ << "\tLDDW\t" << mem << ", A5:A4\n\tNOP\t4\n"; return; }
    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    const char *op = sz == 1 ? (sign ? "LDB" : "LDBU")
                   : sz == 2 ? (sign ? "LDH" : "LDHU")
                             : "LDW";
    out_ << "\t" << op << "\t" << mem << ", A4\n\tNOP\t4\n";
}

void Tms6747::store(const Type *t, const std::string &mem) {
    if (isWide(t)) { out_ << "\tSTDW\tA5:A4, " << mem << "\n"; return; }
    int sz = t->size(target_);
    const char *op = sz == 1 ? "STB" : sz == 2 ? "STH" : "STW";
    out_ << "\t" << op << "\tA4, " << mem << "\n";
}

void Tms6747::narrowInt(const Type *t) {
    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    if (sz == 1)      out_ << (sign ? "\tEXT\tA4, 24, 24, A4\n" : "\tEXTU\tA4, 24, 24, A4\n");
    else if (sz == 2) out_ << (sign ? "\tEXT\tA4, 16, 16, A4\n" : "\tEXTU\tA4, 16, 16, A4\n");
    // 4 bytes and up: the value already fills the 32-bit register.
}

// ---- Regions. hooks ----
std::string Tms6747::label(const char *kind, int id) const {
    return labelPrefix_ + kind + std::to_string(id);
}
std::string Tms6747::userLabel(const std::string &name) const {
    return "L." + functionName_ + "." + name;
}
void Tms6747::defineLabel(const std::string &l) { out_ << l << ":\n"; }

// -g: the statement's source position as cl6x spells it (measured, 8.2.2: `.dwpsn file "t.c",line 2,column
// 11,is_stmt,isa 0`), so TI's assembler takes the file as it stands and vm6747 and sim6747 stop on the line.
void Tms6747::emitLoc(int file, int line, int column) {
    const std::vector<std::string> &files = lineSource()->files();
    if (file < 1 || static_cast<std::size_t>(file) > files.size()) return;
    out_ << "\t.dwpsn\tfile \"" << files[static_cast<std::size_t>(file) - 1] << "\",line " << line
         << ",column " << column << ",is_stmt,isa 0\n";
}
void Tms6747::jump(const std::string &l) { out_ << "\tB\t" << l << "\n\tNOP\t5\n"; }

// The pair by a constant: 32 or more moves a word across, less splices the
// two through A3; 0 is nothing. The right shift fills from the sign or 0.
void Tms6747::shiftPairLeft(int count) {
    if (count == 0) return;
    if (count >= 32) {
        if (count > 32) out_ << "\tSHL\tA4, " << (count - 32) << ", A5\n";
        else            out_ << "\tMV\tA4, A5\n";
        out_ << "\tZERO\tA4\n";
        return;
    }
    out_ << "\tSHL\tA5, " << count << ", A5\n\tSHRU\tA4, " << (32 - count)
         << ", A3\n\tOR\tA5, A3, A5\n\tSHL\tA4, " << count << ", A4\n";
}
void Tms6747::shiftPairRight(int count, bool sign) {
    if (count == 0) return;
    const char *shr = sign ? "SHR" : "SHRU";
    if (count >= 32) {
        if (count > 32) out_ << "\t" << shr << "\tA5, " << (count - 32) << ", A4\n";
        else            out_ << "\tMV\tA5, A4\n";
        if (sign) out_ << "\tSHR\tA5, 31, A5\n";
        else      out_ << "\tZERO\tA5\n";
        return;
    }
    out_ << "\tSHRU\tA4, " << count << ", A4\n\tSHL\tA5, " << (32 - count)
         << ", A3\n\tOR\tA4, A3, A4\n\t" << shr << "\tA5, " << count << ", A5\n";
}

// An upper bound on a function's bytes: four per instruction line, labels and directives none.
static std::size_t codeBytes(const std::string &text) {
    std::size_t n = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        std::size_t k = line.find_first_not_of(" \t|");
        if (k == std::string::npos || line[k] == '.' || line[k] == ';' || line.back() == ':') continue;
        n += 4;
    }
    return n;
}

// Past 64 KB a 16-bit scope cannot hold an offset: cl6x's pr2, 32-bit scope, the frame as byte-codes.
static const std::size_t kLongScope = 0xF000;

// The function's exception table in TI's form (lib/src/tdeh_pr_common.cpp):
// the unwind word (pr3's compact one, or pr2's byte-codes past kLongScope),
// then the scope descriptors the personality routine reads in order, and a zero.

// A descriptor: the range's length and its offset in the function (+2, so
// a return address at the range's end is in and one at its start is not),
// the low bits telling a catch from a cleanup; the pad; a catch's type.

// A row's catches come first, one per type in its chain, its cleanup last:
// phase 2 then lands once, on the pad of the barrier phase 1 found, whose
// selector walks the chain of pads the parser built.
void Tms6747::emitExceptionTable(bool noexcept_, const std::vector<std::string> &spec) {
    const std::vector<CallSite> &rows = callSites();     // the selector's: one per invoke, each whole, none joined to another
    const bool states = noexcept_ || !spec.empty();
    if (rows.empty() && !states) return;
    const std::string table = "__c6xabi_extab$" + functionName_;
    std::ostringstream pads;
    std::set<std::string> made;
    // The trampoline for (pad, selector), made once and named by both.
    auto trampoline = [&](const std::string &pad, int selector) {
        std::string l = pad + ".s" + std::to_string(selector);
        if (made.insert(l).second)
            pads << l << ":\n\tMVK\t" << selector << ", B4\n\tB\t" << pad << "\n\tNOP\t5\n";
        return l;
    };
    std::ostringstream t;
    char word[16];
    // The trampolines lie between the body and fnEnd, which the noexcept row's length reaches.
    std::size_t padLines = 0;
    for (const CallSite &c : rows) padLines += 3 * (c.types.size() + 1);
    const bool wide = codeBytes(out_.str()) + 4 * padLines >= kLongScope;
    const char *const scope = wide ? "\t.ulong\t" : "\t.half\t";
    t << "\t.sect\t\"" << ".c6xabi.extab:" << functionName_ << "\"\n\t.align\t4\n" << table << ":\n";
    for (unsigned w : wide ? longUnwindWords() : std::vector<unsigned>(1, unwindWord(true))) {
        std::snprintf(word, sizeof word, "0x%08x", w);
        t << "\t.ulong\t" << word << "\n";
    }
    for (std::size_t i = 0; i < rows.size(); i++) {
        const CallSite &c = rows[i];
        std::string len = "$EXTAB_SCOPE(" + c.end + ") - $EXTAB_SCOPE(" + c.begin + ")";
        std::string off = "$EXTAB_SCOPE(" + c.begin + ") - $EXTAB_SCOPE(" + functionName_ + ") + 2";
        if (c.terminate) {           // catch anything, and terminate: cl6x's scope over a cleanup pad
            t << scope << len << " + 1\n" << scope << off << "\n\t.ulong\t0\n\t.ulong\t0xfffffffe\n";
            continue;
        }
        for (std::size_t k = 0; k < c.types.size(); k++) {
            int ix = k < c.indices.size() ? c.indices[k] : static_cast<int>(k) + 1;
            t << scope << len << " + 1\n" << scope << off << "\n"
              << "\t.ulong\t$EXTAB_LP(" << trampoline(c.pad, ix) << ")\n"
              << "\t.ulong\t" << (c.types[k].empty() ? std::string("0xffffffff") : "$EXTAB_RTTI(" + c.types[k] + ")") << "\n";
        }
        if (c.cleanup || c.types.empty())
            t << scope << len << "\n" << scope << off << "\n"
              << "\t.ulong\t$EXTAB_LP(" << trampoline(c.pad, 0) << ")\n";
    }
    // A function's exception specification, over the whole of it and last: the types
    // `throw(T...)` allows - none for noexcept - and no pad, so TI's runtime calls
    // __cxa_call_unexpected - cl6x's row, with the offset's low bit set.
    const std::string fnEnd = "L." + functionName_ + ".fnend";
    if (states) {
        t << scope << "$EXTAB_SCOPE(" << fnEnd << ") - $EXTAB_SCOPE(" << functionName_ << ")\n" << scope << "3\n"
          << "\t.ulong\t" << spec.size() << "\n";
        for (const std::string &type : spec) t << "\t.ulong\t$EXTAB_RTTI(" << type << ")\n";
        t << "\t.symdepend\t\"__cxa_call_unexpected\", \".c6xabi.extab:" << functionName_ << "\"\n";
    }
    t << "\t.ulong\t0\n\t.symdepend\t\"__c6xabi_unwind_cpp_pr" << (wide ? 2 : 3) << "\", \".c6xabi.extab:" << functionName_ << "\"\n";
    if (wide) needsPr2_ = true;
    out_ << pads.str() << fnEnd << ":\n" << t.str() << "\t.text\n";
    if (states) needsUnexpected_ = true;
}

std::string Tms6747::pairHigh(const std::string &r) { return r.substr(0, 1) + std::to_string(std::atoi(r.c_str() + 1) + 1); }
void Tms6747::regWrite(const std::string &r, bool wide) {
    out_ << "\tMV\tA4, " << r << "\n";
    if (wide) out_ << "\tMV\tA5, " << pairHigh(r) << "\n";
}

// The C674x's own SP/DP instructions, each with its delay slots as NOPs
// (TMS6747.md has the counts); division through the EABI helpers. <= and >=
// are two compares ORed, which keeps NaN unordered where !(a > b) would not.
void Tms6747::fpBinary(BinOp op, bool dp) {
    const char *l = dp ? "A5:A4" : "A4", *r = dp ? "A7:A6" : "A6";
    const char *sfx = dp ? "DP" : "SP";
    auto op3 = [&](const char *mnem, int slots) {
        out_ << "\t" << mnem << sfx << "\t" << l << ", " << r << ", " << l
             << "\n\tNOP\t" << slots << "\n";
    };
    auto cmp = [&](const char *mnem, const char *dst) {
        out_ << "\t" << mnem << sfx << "\t" << l << ", " << r << ", " << dst
             << "\n\tNOP\t1\n";
    };
    switch (op) {
    case BinOp::Add: op3("ADD", dp ? 6 : 3); return;
    case BinOp::Sub: op3("SUB", dp ? 6 : 3); return;
    case BinOp::Mul: op3("MPY", dp ? 9 : 3); return;
    case BinOp::Div:
        openArea(8);
        out_ << "\tMV\tA6, B4\n";
        if (dp) out_ << "\tMV\tA7, B5\n";
        call(dp ? "__c6xabi_divd" : "__c6xabi_divf");
        closeArea();
        return;
    case BinOp::Eq: cmp("CMPEQ", "A4"); return;
    case BinOp::Ne: cmp("CMPEQ", "A4"); out_ << "\tXOR\t1, A4, A4\n"; return;
    case BinOp::Lt: cmp("CMPLT", "A4"); return;
    case BinOp::Gt: cmp("CMPGT", "A4"); return;
    case BinOp::Le: cmp("CMPLT", "A0"); cmp("CMPEQ", "A4"); out_ << "\tOR\tA0, A4, A4\n"; return;
    case BinOp::Ge: cmp("CMPGT", "A0"); cmp("CMPEQ", "A4"); out_ << "\tOR\tA0, A4, A4\n"; return;
    default: unsupported("this floating-point operator");
    }
}

// 64-bit integers in A5:A4 and A7:A6 with 32-bit instructions: carry by
// CMPLTU of the low words, MPY32U plus the cross products, division by the
// EABI's helpers, compares on the high word falling to the low on a tie.
void Tms6747::wideBinary(BinOp op, bool sign) {
    switch (op) {
    case BinOp::Add:
        out_ << "\tADD\tA4, A6, A4\n\tCMPLTU\tA4, A6, A0\n";   // carry: sum < addend
        out_ << "\tADD\tA5, A7, A5\n\tADD\tA5, A0, A5\n";
        return;
    case BinOp::Sub:
        out_ << "\tCMPLTU\tA4, A6, A0\n\tSUB\tA4, A6, A4\n";   // borrow: lhs < rhs
        out_ << "\tSUB\tA5, A7, A5\n\tSUB\tA5, A0, A5\n";
        return;
    case BinOp::Mul:
        out_ << "\tMPY32U\tA4, A6, A1:A0\n\tNOP\t3\n";       // the full low product
        out_ << "\tMPY32\tA4, A7, A3\n\tNOP\t3\n\tADD\tA1, A3, A1\n";
        out_ << "\tMPY32\tA5, A6, A3\n\tNOP\t3\n\tADD\tA1, A3, A1\n";
        out_ << "\tMV\tA0, A4\n\tMV\tA1, A5\n";
        return;
    case BinOp::Div: case BinOp::Mod: {
        const char *helper = op == BinOp::Div ? (sign ? "__c6xabi_divlli" : "__c6xabi_divull")
                                                  : (sign ? "__c6xabi_remlli" : "__c6xabi_remull");
        openArea(8);
        out_ << "\tMV\tA6, B4\n\tMV\tA7, B5\n";
        call(helper);
        closeArea();
        return;
    }
    case BinOp::BitAnd: out_ << "\tAND\tA4, A6, A4\n\tAND\tA5, A7, A5\n"; return;
    case BinOp::BitOr:  out_ << "\tOR\tA4, A6, A4\n\tOR\tA5, A7, A5\n";   return;
    case BinOp::BitXor: out_ << "\tXOR\tA4, A6, A4\n\tXOR\tA5, A7, A5\n"; return;
    case BinOp::Eq:
        out_ << "\tCMPEQ\tA4, A6, A0\n\tCMPEQ\tA5, A7, A4\n\tAND\tA0, A4, A4\n";
        return;
    case BinOp::Ne:
        out_ << "\tCMPEQ\tA4, A6, A0\n\tCMPEQ\tA5, A7, A4\n\tAND\tA0, A4, A4\n\tXOR\t1, A4, A4\n";
        return;
    case BinOp::Lt: case BinOp::Ge: {
        // lhs < rhs: high < high, or high == high and low <u low.
        out_ << (sign ? "\tCMPLT\tA5, A7, A0\n" : "\tCMPLTU\tA5, A7, A0\n");
        out_ << "\tCMPEQ\tA5, A7, A3\n\tCMPLTU\tA4, A6, A4\n\tAND\tA3, A4, A4\n\tOR\tA0, A4, A4\n";
        if (op == BinOp::Ge) out_ << "\tXOR\t1, A4, A4\n";
        return;
    }
    case BinOp::Gt: case BinOp::Le: {
        out_ << (sign ? "\tCMPGT\tA5, A7, A0\n" : "\tCMPGTU\tA5, A7, A0\n");
        out_ << "\tCMPEQ\tA5, A7, A3\n\tCMPGTU\tA4, A6, A4\n\tAND\tA3, A4, A4\n\tOR\tA0, A4, A4\n";
        if (op == BinOp::Le) out_ << "\tXOR\t1, A4, A4\n";
        return;
    }
    case BinOp::Shl: case BinOp::Shr: {
        // The count is in A6. A shift by 32 or more of a single word gives 0
        // (or the sign), which is what makes the splice right at a count of 0.
        int id = nextLabel();
        std::string big = label("wide", id), done = label("widend", id);
        bool left = op == BinOp::Shl;
        const char *shr = sign ? "SHR" : "SHRU";
        out_ << "\tEXTU\tA6, 26, 26, A6\n";                       // count & 63: AND takes no 6-bit constant
        movImm("A0", 32);
        out_ << "\tCMPLTU\tA6, A0, A1\n\t[!A1]\tB\t" << big << "\n\tNOP\t5\n";
        out_ << "\tSUB\tA0, A6, A0\n";                           // 32 - count
        if (left) {
            out_ << "\tSHL\tA5, A6, A5\n\tSHRU\tA4, A0, A3\n\tOR\tA5, A3, A5\n";
            out_ << "\tSHL\tA4, A6, A4\n";
        } else {
            out_ << "\t" << shr << "\tA5, A6, A3\n\tSHRU\tA4, A6, A4\n\tSHL\tA5, A0, A5\n";
            out_ << "\tOR\tA4, A5, A4\n\tMV\tA3, A5\n";
        }
        jump(done);
        defineLabel(big);
        out_ << "\tSUB\tA6, A0, A0\n";                           // count - 32: A0 is still 32 here
        if (left) out_ << "\tSHL\tA4, A0, A5\n\tZERO\tA4\n";
        else if (sign) out_ << "\tSHR\tA5, A0, A4\n\tSHR\tA5, 31, A5\n";
        else out_ << "\tSHRU\tA5, A0, A4\n\tZERO\tA5\n";
        defineLabel(done);
        return;
    }
    default: unsupported("this 64-bit operator");
    }
}

// A struct of 8 bytes or less whose copy and destruction are not calls
// travels in registers like a scalar of its size - one up to 4 bytes, the
// pair A5:A4 up to 8 - as an argument and as a result: TI's rule, measured.
bool Tms6747::inPair(const Type *t) const {
    return t->isStructOrUnion() && !t->nonTrivialCopy() && !t->hasDestructor() && t->size(target_) <= abi_.structReturnLimit;
}
bool Tms6747::inPairWide(const Type *t) const { return inPair(t) && t->size(target_) > 4; }
// A class whose copy or destruction is a call comes back through a pointer
// that is the first parameter, A4, never null; a trivially copyable one
// over 8 bytes through A3, null from a caller that discards it (cl6x).
bool Tms6747::hiddenInA4(const Type *t) const {
    return t->isStructOrUnion() && (t->nonTrivialCopy() || t->hasDestructor());
}

// A5:A4 from the struct at *A4, and the struct at *A3 from A5:A4, in pieces
// no wider than the struct's alignment: a struct of three chars sits at any
// address, so it goes byte by byte, and nothing reaches past its last byte.
void Tms6747::loadPair(int size, int align) {
    int w = align >= 4 ? 4 : align;
    out_ << "\tMV\tA4, A3\n\tZERO\tA5:A4\n";
    for (int at = 0; at < size; at += w) {
        const char *reg = at < 4 ? "A4" : "A5";
        int shift = (at % 4) * 8;
        std::string base = at == 0 ? "*A3" : "*+A3(" + std::to_string(at) + ")";
        const char *ld = w == 4 ? "LDW" : w == 2 ? "LDHU" : "LDBU";
        if (w == 4) { out_ << "\t" << ld << "\t" << base << ", " << reg << "\n\tNOP\t4\n"; continue; }
        out_ << "\t" << ld << "\t" << base << ", A0\n\tNOP\t4\n";
        if (shift > 0) out_ << "\tSHL\tA0, " << shift << ", A0\n";
        out_ << "\tOR\t" << reg << ", A0, " << reg << "\n";
    }
}

void Tms6747::storePair(int size, int align) {
    int w = align >= 4 ? 4 : align;
    for (int at = 0; at < size; at += w) {
        const char *reg = at < 4 ? "A4" : "A5";
        int shift = (at % 4) * 8;
        std::string base = at == 0 ? "*A3" : "*+A3(" + std::to_string(at) + ")";
        const char *st = w == 4 ? "STW" : w == 2 ? "STH" : "STB";
        if (shift > 0) { out_ << "\tSHRU\t" << reg << ", " << shift << ", A0\n"; reg = "A0"; }
        out_ << "\t" << st << "\t" << reg << ", " << base << "\n";
    }
}

// Conversions with a 64-bit integer on one side. Widening is a sign or zero
// fill of A5; narrowing drops it; to and from floating point is the EABI's.
void Tms6747::wideCast(const Type *from, const Type *to) {
    bool fromW = !from->isFloating() && isWide(from);
    bool toW = !to->isFloating() && isWide(to);
    const char *helper = nullptr;
    if (fromW && toW) return;
    if (toW && from->isFloating())
        helper = to->isSigned(target_) ? (isDouble(from) ? "__c6xabi_fixdlli" : "__c6xabi_fixflli")
                                       : (isDouble(from) ? "__c6xabi_fixdull" : "__c6xabi_fixfull");
    else if (fromW && to->isFloating())
        helper = from->isSigned(target_) ? (isDouble(to) ? "__c6xabi_fltllid" : "__c6xabi_fltllif")
                                         : (isDouble(to) ? "__c6xabi_fltulld" : "__c6xabi_fltullf");
    if (helper != nullptr) {
        openArea(8);
        call(helper);
        closeArea();
        return;
    }
    if (toW) out_ << (from->isSigned(target_) ? "\tSHR\tA4, 31, A5\n" : "\tZERO\tA5\n");
    else     narrowInt(to);
}

// Where a stack-passed parameter (from firstStack_ on) sits relative to the
// caller's B15 - the layout the Call visit uses. i == ps.size() names the
// word past the last parameter, where a variadic function's extras begin.
int Tms6747::stackParamOffset(const std::vector<ir::LaidParam> &ps, std::size_t i) {
    int end = 4;
    for (std::size_t k = firstStack_; k < i; k++) stackArg(laidType(ps[k].type), end);
    return i < ps.size() ? stackArg(laidType(ps[i].type), end) : end;
}
// Where the next stack argument of type t sits, as cl6x places it: a scalar
// at its own alignment in its own size (a char and a short pack), a class of
// 8 bytes or less in a word or an 8-aligned doubleword, a larger one a word.
int Tms6747::stackArg(const Type *t, int &end) {
    int size, align;
    if (isWide(t) || inPairWide(t)) { size = 8; align = 8; }
    else if (t->isStructOrUnion() || t->isArray()) { size = 4; align = 4; }
    else { size = t->size(target_); align = size; }
    end = (end + align - 1) / align * align;
    const int at = end;
    end += size;
    return at;
}
// The load or store of a stack argument of type t at *reg, in its own size.
std::string Tms6747::stackArgAccess(const Type *t, bool store, const std::string &mem) {
    if (isWide(t) || inPairWide(t)) return std::string(store ? "\tSTDW\tA5:A4, " : "\tLDDW\t") + mem + (store ? "\n" : ", A5:A4\n\tNOP\t4\n");
    int sz = t->isStructOrUnion() || t->isArray() ? 4 : t->size(target_);
    bool sign = !t->isStructOrUnion() && !t->isArray() && t->isSigned(target_);
    const char *op = store ? (sz == 1 ? "STB" : sz == 2 ? "STH" : "STW")
                           : (sz == 1 ? (sign ? "LDB" : "LDBU") : sz == 2 ? (sign ? "LDH" : "LDHU") : "LDW");
    if (store) return std::string("\t") + op + "\tA4, " + mem + "\n";
    return std::string("\t") + op + "\t" + mem + ", A4\n\tNOP\t4\n";
}

void Tms6747::setOptimizeForSize(bool size) { forSize_ = size; }
void Tms6747::setOptimize(int level) {
    optimize_ = level;
}

// The call itself: the return address into B3, the branch - to a symbol or
// through B1 - and its five delay slots. The caller has opened the area.
void Tms6747::call(const std::string &target) {
    std::string ret = label("ret", nextLabel());
    movSym("B3", ret);
    out_ << "\tB\t" << target << "\n\tNOP\t5\n";
    defineLabel(ret);
    hasCall_ = true;
}

void Tms6747::vaStartAt() {
    regAdd("A15", vaStart_, "A6");
    out_ << "\tSTW\tA6, *A4\n";
}
void Tms6747::vaArgAt(const Type *t) {
    bool byRef = t->isStructOrUnion() && !inPair(t);
    int slot = isWide(t) || inPairWide(t) ? 8 : 4;
    out_ << "\tLDW\t*A4, A6\n\tNOP\t4\n";  // A6 = ap
    if (slot == 8) out_ << "\tADD\tA6, 7, A6\n\tCLR\tA6, 0, 2, A6\n";   // round up to 8: AND puts no constant second
    out_ << "\tADD\tA6, " << slot << ", A3\n\tSTW\tA3, *A4\n";   // ap += slot
    out_ << "\tMV\tA6, A4\n";
    if (byRef) { out_ << "\tLDW\t*A4, A4\n\tNOP\t4\n"; return; }  // the struct's address
    load(t);
}

// ---- functions and the file ----------------------------------------------
// Initialised data, in TI's directives: .byte, .short and .word (32 bits;
// TI's .long is 32 bits too, so a 64-bit piece is two words, low first), a
// .word of a symbol with its addend for a relocated piece, .space for a gap.
void Tms6747::emitGlobal(const Global &g, Segment seg) {
    int size = g.type->size(target_);
    // The type's own alignment: lifting a 16-byte object to 16 is x86's rule.
    int align = g.align > g.type->align(target_) ? g.align : g.type->align(target_);
    // An inline object - a vtable, a typeinfo, a template's static member -
    // is one object however many units define it: `.weak` in place of
    // `.global`, and zero-filled ones in .data by .space, not the .bss form.
    if (g.isInline)       out_ << "\t.weak\t" << g.symbol << "\n";
    else if (!g.isStatic) out_ << "\t.global " << g.symbol << "\n";

    if (seg == Segment::Bss && !g.isInline) {
        out_ << "\t.bss\t" << g.symbol << ", " << size << ", " << align << "\n";
        return;
    }
    // **A scalar goes where TI's code can reach it through DP**: `.neardata`
    // or `.rodata`, as cl6x places every scalar; an aggregate stays in
    // `.data`/`.const`, which TI reaches by absolute address (measured).
    const bool scalar = !g.type->isStructOrUnion() && !g.type->isArray();
    if (scalar) out_ << (seg == Segment::Const || seg == Segment::ConstRelocated ? "\t.sect\t\".rodata\"\n" : "\t.sect\t\".neardata\", RW\n");
    else if (seg == Segment::Bss) out_ << "\t.data\n";
    else out_ << (seg == Segment::Data ? "\t.data\n" : "\t.sect\t\".const\"\n");

    if (align > 1) out_ << "\t.align\t" << align << "\n";
    // The word in front, where there is one: after the alignment, so that it
    // is the label and not the block that ends up aligned. A pointer here is
    // a word.
    if (!g.prefixWord.empty()) out_ << "\t.word\t" << symRef(g.prefixWord) << "\n";
    out_ << g.symbol << ":\n";
    if (seg == Segment::Bss) { out_ << "\t.space\t" << size << "\n"; return; }
    int at = 0;
    for (const GlobalPiece &p : g.init) {
        if (p.offset > at) out_ << "\t.space\t" << (p.offset - at) << "\n";
        if (!p.symbol.empty()) {
            if (p.size != 4) unsupported("a relocated piece that is not a word");
            out_ << "\t.word\t" << symRef(p.symbol);
            if (p.value > 0) out_ << "+" << p.value;
            else if (p.value < 0) out_ << "-" << -p.value;
            out_ << "\n";
        } else {
            switch (p.size) {
            case 1: out_ << "\t.byte\t" << p.value << "\n"; break;
            case 2: out_ << "\t.short\t" << p.value << "\n"; break;
            case 4: out_ << "\t.word\t" << p.value << "\n"; break;
            case 8: {
                unsigned long long v = static_cast<unsigned long long>(p.value);
                out_ << "\t.word\t" << (v & 0xffffffffu) << "\n";
                out_ << "\t.word\t" << (v >> 32) << "\n";
                break;
            }
            default: unsupported("a data piece of this size");
            }
        }
        at = p.offset + p.size;
    }
    if (at < size) out_ << "\t.space\t" << (size - at) << "\n";
}

// String literals into .const as plain .byte lists - no escape syntax to
// get wrong, and wide strings are the same form - then the globals by
// segment, each opening the section its kind belongs in (emitGlobal).
void Tms6747::emitData(const UnitData &data) {
    bool inConst = !data.strings.empty();
    if (inConst) out_ << "\t.sect\t\".const\"\n";
    for (const StringLit &s : data.strings) {
        if (s.width > 1) out_ << "\t.align\t" << s.width << "\n";
        out_ << symName(s.label) << ":\n";
        for (std::size_t i = 0; i < s.bytes.size(); i++) {
            if (i % 16 == 0) out_ << "\t.byte\t";
            out_ << static_cast<int>(static_cast<unsigned char>(s.bytes[i]));
            out_ << ((i + 1 == s.bytes.size() || i % 16 == 15) ? "\n" : ", ");
        }
    }

    struct Bucket { Segment seg; };
    const Bucket order[] = { { Segment::Const }, { Segment::ConstRelocated }, { Segment::Data }, { Segment::Bss } };
    for (const Bucket &b : order)
        for (const Global &g : data.globals)
            if (segmentFor(g) == b.seg) emitGlobal(g, b.seg);   // each names its own section
}

// Parameters arrive in the argument registers and, from the eleventh, on
// the stack; each is copied to its own frame slot so the body sees a local.
// Runs after the body has been selected, when the frame link's size is known.
void Tms6747::emitParams(const ir::Function &f) {
    const std::vector<ir::LaidParam> &ps = f.laid.params;
    for (std::size_t i = 0; i < ps.size(); i++) {
        const Type *t = laidType(ps[i].type);
        bool byRef = t->isStructOrUnion() && !inPair(t);   // the address of the caller's copy
        if (i < firstStack_) {
            const char *reg = abi_.intRegs[i + sretShift_];
            if (std::string(reg) != "A4") {
                out_ << "\tMV\t" << reg << ", A4\n";
                if (isWide(t) || inPairWide(t)) {
                    std::string hi = pairOf(reg).substr(0, pairOf(reg).find(':'));
                    out_ << "\tMV\t" << hi << ", A5\n";
                }
            }
        } else {
            // The caller's B15 was A15; its stack arguments start one word above that.
            paramInFrame_ = true;
            const int at = stackParamOffset(ps, i);
            const int sz = byRef ? 4 : accessSize(t);
            if (frameFits(at, sz)) out_ << stackArgAccess(t, false, frameOperand(at, sz));
            else { regAdd("A15", at, "A0"); out_ << stackArgAccess(t, false, "*A0"); }
        }
        if (inPair(t)) {
            // The value itself, into the parameter's slot, in its own bytes.
            paramInFrame_ = true;
            localAddr(ps[i].offset, "A3");
            storePair(t->size(target_), t->align(target_));
            continue;
        }
        if (byRef) {
            // Through A1, not A6: A6 is the third argument's register, still
            // to be read when an earlier struct parameter is being copied.
            paramInFrame_ = true;
            localAddr(ps[i].offset, "A1");
            copyBlock(t->size(target_), "A4", "A1", t->align(target_), t->align(target_) >= 8);
            continue;
        }
        // A parameter in its home register (regOf_, the selector's).
        const int key = ps[i].offset;
        if (regOf_.count(key)) { narrowInt(t); regWrite(regOf_[key], isWide(t)); continue; }
        paramInFrame_ = true;
        const int disp = -(saveBytes_ + ps[i].offset);
        if (frameFits(disp, accessSize(t))) { store(t, frameOperand(disp, accessSize(t))); continue; }
        localAddr(ps[i].offset, "A0");
        store(t, "*A0");
    }
}

// A15 points at the caller's B15 word - the ABI leaves it to the callee -
// and holds the caller's A15 there; the other saved registers sit below it
// in the order TI's unwinder pops them, so its index entry fits this frame.

// In TI's pop order; the position in the list is the word below A15.
std::vector<std::string> Tms6747::savedRegs() const {
    static const char *const kOrder[] = { "B13", "B12", "B11", "B10", "B3", "A13", "A12", "A11", "A10" };
    std::vector<std::string> r;
    r.push_back("A15");
    for (const char *reg : kOrder) {
        const bool odd = (reg[2] - '0') % 2 != 0;
        bool want = std::string(reg) == "B3" ? hasCall_ : odd ? usesSavedPairRegs_ : usesSavedArgRegs_;
        for (std::size_t k = 0; k < promoted_.size(); k++) if (promoted_[k] == reg) want = true;
        if (want) r.push_back(reg);
    }
    // A14, never used, is saved as a filler where it puts the A pairs on 8-aligned words: B3 and an even
    // number of B registers above them leave A13:A12 and A11:A10 straddling, and one word mends both.
    if (optimize_ > 0) {
        std::size_t nB = 0, at = 0;
        bool b3 = false, a13 = false, a12 = false, a11 = false, a10 = false;
        for (std::size_t k = 0; k < r.size(); k++) {
            if (r[k][0] == 'B' && r[k] != "B3") nB++;
            if (r[k] == "B3") { b3 = true; at = k + 1; }
            a13 |= r[k] == "A13"; a12 |= r[k] == "A12"; a11 |= r[k] == "A11"; a10 |= r[k] == "A10";
        }
        if (b3 && nB % 2 == 0 && ((a13 && a12) || (a11 && a10))) r.insert(r.begin() + static_cast<long>(at), "A14");
    }
    return r;
}
// The frame as the second word of TI's exception index entry (tdeh_pr_c6000):
// personality 3, SP restored from A15 (0x7f), the pop bitmask in the order
// A15, B15-B10, B3, A14-A10 from bit 12 down, and B3 the return register.
unsigned Tms6747::unwindWord(bool needFrame) const {
    static const struct { const char *reg; int bit; } bits[] = {
        { "A15", 12 }, { "B13", 9 }, { "B12", 8 }, { "B11", 7 }, { "B10", 6 }, { "B3", 5 }, { "A14", 4 },
        { "A13", 3 }, { "A12", 2 }, { "A11", 1 }, { "A10", 0 } };
    unsigned mask = 0;
    if (needFrame)
        for (const std::string &r : savedRegs())
            for (size_t k = 0; k < sizeof bits / sizeof bits[0]; k++)
                if (r == bits[k].reg) mask |= 1u << bits[k].bit;
    return 0x83000000u | (needFrame ? 0x7fu << 17 : 0u) | (mask << 4) | 7u;
}
// The same frame as pr2's byte-codes (readelf's tic6x decoder): MV FP, SP; pop the mask down
// from A15; RETURN - padded with RETURN, the extra words' count in the first word's second byte.
std::vector<unsigned> Tms6747::longUnwindWords() const {
    const unsigned mask = unwindWord(true) >> 4 & 0x1fffu;
    std::vector<unsigned> ops = { 0xd0u, 0x80u | mask >> 8, mask & 0xffu, 0xe7u };
    while (ops.size() % 4 != 2) ops.push_back(0xe7u);
    std::vector<unsigned> words(1, 0x82000000u | static_cast<unsigned>((ops.size() - 2) / 4) << 16 | ops[0] << 8 | ops[1]);
    for (std::size_t k = 2; k < ops.size(); k += 4)
        words.push_back(ops[k] << 24 | ops[k + 1] << 16 | ops[k + 2] << 8 | ops[k + 3]);
    return words;
}

// -g: parameters, then locals, in TI's .dwtag spelling for sim6747 and vm6747 (GRIDEd docs\C6747-LOCALS-DESIGN.md): a frame
// variable at its slot from A15 (DWARF register 15), one kept in a register or given no room left out, a static by its symbol.
// An inner block's variables go inside its DW_TAG_lexical_block (phase F), so a debugger lists them only while the stop is in it.
void Tms6747::emitVariables(const ir::Function &f) {
    if (lineSource() == nullptr) return;
    // ASM6x ends a string at the next quote, so a literal operator's `operator"" _km` is `operator'' _km` here.
    std::string name = f.laid.name;
    for (char &c : name) if (c == '"') c = '\'';
    out_ << "\t.dwtag\tDW_TAG_subprogram, DW_AT_name(\"" << name << "\"), DW_AT_low_pc(" << f.symbol << ")\n";
    emitScope(f, 0);
    out_ << "\t.dwendtag\n";
}

// a block's own variables (the function's, scope 0, parameters first), then each inner block that declares
// something: as a lexical block when it has code of its own, else folded into this one
void Tms6747::emitScope(const ir::Function &f, int scope) {
    for (int pass = 0; pass < 2; pass++)
        for (std::size_t n = 0; n < f.laid.locals.size(); n++) {
            const ir::LaidLocal &l = f.laid.locals[n];
            if (l.scope != scope || l.isParam != (pass == 0) || l.name.empty() || !l.type.valid()) continue;
            const Type *lt = laidType(l.type);
            std::string where;
            if (!l.staticName.empty()) where = "DW_OP_addr " + l.staticName;
            else if (n < ssaPlace_.size() && (ssaPlace_[n] >= 0 || !l.isParam || regOf_.count(l.offset))) {
                if (ssaPlace_[n] < 0) continue;                 // selected: promoted, or a parameter in its home register
                where = "DW_OP_breg15 " + std::to_string(-(saveBytes_ + ssaPlace_[n]));
            } else {
                if (regOf_.count(l.offset)) continue;
                where = "DW_OP_breg15 " + std::to_string(-(saveBytes_ + l.offset));
            }
            out_ << "\t.dwtag\t" << (l.isParam ? "DW_TAG_formal_parameter" : "DW_TAG_variable")
                 << ", DW_AT_name(\"" << l.name << "\"), DW_AT_type(\"" << lt->describe() << "\"), "
                 << "DW_AT_location[" << where << "]\n";
            noteType(lt);
        }
    const std::vector<DwarfBlock> &bs = blocks();
    for (std::size_t b = 1; b < bs.size(); b++) {
        if (bs[b].parent != scope || !declaresAnything(f, static_cast<int>(b))) continue;
        if (bs[b].begin.empty() || bs[b].end.empty()) { emitScope(f, static_cast<int>(b)); continue; }
        out_ << "\t.dwtag\tDW_TAG_lexical_block, DW_AT_low_pc(" << bs[b].begin << "), DW_AT_high_pc(" << bs[b].end << ")\n";
        emitScope(f, static_cast<int>(b));
        out_ << "\t.dwendtag\n";
    }
}

bool Tms6747::declaresAnything(const ir::Function &f, int scope) const {
    for (const ir::LaidLocal &l : f.laid.locals) if (l.scope == scope && !l.name.empty()) return true;
    const std::vector<DwarfBlock> &bs = blocks();
    for (std::size_t b = 1; b < bs.size(); b++)
        if (bs[b].parent == scope && declaresAnything(f, static_cast<int>(b))) return true;
    return false;
}

// A type and every type it is made of, each once: an array's element, what a pointer or a reference
// refers to, a class's members and its non-virtual bases.
void Tms6747::noteType(const Type *t) {
    if (t == nullptr || t->isFunction() || t->isVoid()) return;
    const Kind k = t->kind();
    if (k != Kind::Pointer && k != Kind::LValueRef && k != Kind::RValueRef && k != Kind::Array && !t->isStructOrUnion() &&
        !t->isArithmetic() && k != Kind::NullPtr)
        return;                                    // a member pointer, a template's own: nothing to show
    for (const Type *seen : debugTypes_) if (seen == t) return;
    debugTypes_.push_back(t);
    if (k == Kind::Pointer || k == Kind::LValueRef || k == Kind::RValueRef || k == Kind::Array) noteType(t->pointee());
    if (t->isStructOrUnion() && t->isComplete()) {
        for (const Type::BaseSpec &b : t->bases()) if (b.direct && !b.isVirtual) noteType(b.type);
        for (const Member &m : t->members()) noteType(m.type);
    }
}

// -g: the types, named as the variables' DW_AT_type names them, each closed by .dwendtag: a base type's size
// and encoding, a pointer's or a reference's size, an array's element and bound, a class's non-virtual bases
// (as gdb names them, <Base>) and members at their offsets (a bit-field is left out).
void Tms6747::emitTypes() {
    if (lineSource() == nullptr) return;
    std::ostringstream o;
    for (const Type *t : debugTypes_) {
        const std::string name = t->describe();
        const Kind k = t->kind();
        // A reference's size is its referent's, and a function's is none: only the kinds below ask.
        const bool sized = k != Kind::Pointer && k != Kind::LValueRef && k != Kind::RValueRef && !t->isFunction();
        const int size = sized && t->isComplete() ? t->size(target_) : 0;
        if (k == Kind::Pointer || k == Kind::LValueRef || k == Kind::RValueRef) {
            o << "\t.dwtag\tDW_TAG_pointer_type, DW_AT_name(\"" << name << "\"), DW_AT_type(\""
              << (t->pointee()->isFunction() ? std::string("void") : t->pointee()->describe()) << "\"), DW_AT_byte_size(4)\n";
        } else if (k == Kind::Array) {
            o << "\t.dwtag\tDW_TAG_array_type, DW_AT_name(\"" << name << "\"), DW_AT_type(\"" << t->pointee()->describe()
              << "\"), DW_AT_upper_bound(" << (t->length() - 1) << "), DW_AT_byte_size(" << size << ")\n";
        } else if (t->isStructOrUnion()) {
            o << "\t.dwtag\t" << (k == Kind::Union ? "DW_TAG_union_type" : t->declaredClass() ? "DW_TAG_class_type" : "DW_TAG_structure_type")
              << ", DW_AT_name(\"" << name << "\"), DW_AT_byte_size(" << size << ")\n";
            if (t->isComplete()) {
                for (const Type::BaseSpec &b : t->bases())
                    if (b.direct && !b.isVirtual)
                        o << "\t.dwtag\tDW_TAG_member, DW_AT_name(\"<" << b.type->describe() << ">\"), DW_AT_type(\"" << b.type->describe()
                          << "\"), DW_AT_data_member_location(" << b.offset << ")\n";
                for (const Member &m : t->members())
                    if (m.width == 0 && !m.name.empty() && m.inVirtualBase == nullptr && m.declaredIn == nullptr)
                        o << "\t.dwtag\tDW_TAG_member, DW_AT_name(\"" << m.name << "\"), DW_AT_type(\"" << m.type->describe()
                          << "\"), DW_AT_data_member_location(" << m.offset << ")\n";
            }
        } else {
            const bool isSigned = t->isArithmetic() && t->isSigned(target_);
            const char *enc = t->isFloating() ? "DW_ATE_float"
                            : k == Kind::Bool ? "DW_ATE_boolean"
                            : (k == Kind::Char || k == Kind::SChar || k == Kind::UChar) ? (isSigned ? "DW_ATE_signed_char" : "DW_ATE_unsigned_char")
                            : isSigned ? "DW_ATE_signed" : "DW_ATE_unsigned";
            o << "\t.dwtag\tDW_TAG_base_type, DW_AT_name(\"" << name << "\"), DW_AT_byte_size(" << size << "), DW_AT_encoding(" << enc << ")\n";
        }
        o << "\t.dwendtag\n";
    }
    file_ += o.str();
}

void Tms6747::emitFunction(const std::string &symbol) {
    resetLabels();
    // The IR's function of `fn`, which the shell reads (M6d): a refusal of the lowering is an error, so each has one.
    const ir::FuncId irId = ssa_ != nullptr ? ssa_->find(symbol) : ir::FuncId();
    if (!irId.valid()) unsupported("a function the SSA lowering built none of");
    const ir::Function &irFn = ssa_->functions[irId.v];
    // The symbol, not the name: with C++ linkage that is the mangled name, and
    // two overloads share a name but must not share a label.
    functionName_ = irFn.symbol;
    labelPrefix_ = "L." + irFn.symbol + ".";
    returnLabel_ = "L.return." + irFn.symbol;
    const ir::Laid &laid = irFn.laid;     // the parser's frame of it
    returns_ = laidType(laid.returns);

    sretSlot_ = laid.sretSlot;
    sretShift_ = sretSlot_ != 0 && hiddenInA4(returns_) ? 1 : 0;

    // Which parameters the caller put on the stack, and where a variadic
    // function's unnamed arguments start. The hidden pointer in A4 takes a
    // register from the explicit parameters.
    std::size_t regCount = static_cast<std::size_t>(abi_.intCount);
    firstStack_ = regCount - sretShift_;
    vaStart_ = 0;
    if (irFn.variadic) {
        std::size_t named = laid.params.size();
        std::size_t last = named > 0 ? named - 1 : 0;
        if (last < firstStack_) firstStack_ = last;
        vaStart_ = (stackParamOffset(laid.params, named) + 3) / 4 * 4;   // a word past a packed char
    }

    // The body first, into its own entries: what it does decides the prologue.
    regOf_.clear();
    promoted_.clear();
    paramInFrame_ = false;
    saveBytes_ = kSaveBytes;
    // Selected from SSA (M6a); a body outside the selector's subset is refused by name in ssaChoose (M6d: no walker).
    ssaPlace_.clear();
    ssaChoose(irFn);
    selectBody(irFn);
    out_.str(std::string());
    if (sretSlot_ != 0) {
        // The caller's pointer to where the result goes - A4 for a class
        // non-trivial for calls, A3 otherwise - kept in its slot for the
        // return to find, before the parameters are copied in.
        localAddr(sretSlot_, "A0");
        out_ << (sretShift_ != 0 ? "\tSTW\tA4, *A0\n" : "\tSTW\tA3, *A0\n");
    }
    emitParams(irFn);
    std::string params = out_.str();
    out_.str(std::string());

    out_ << "\t.text\n";
    // An inline definition may appear in several translation units, so the
    // linker keeps one rather than rejecting the rest: a weak definition,
    // `.weak` in place of `.global` for TI's assembler.
    if (irFn.isInline)       out_ << "\t.weak\t" << irFn.symbol << "\n";
    else if (!irFn.isStatic) out_ << "\t.global " << irFn.symbol << "\n";
    out_ << irFn.symbol << ":\n";
    // A second name for the same code (Function::alias): a label at the same
    // address, not a copy of the body.
    if (!laid.alias.empty()) {
        if (irFn.isInline)       out_ << "\t.weak\t" << laid.alias << "\n";
        else if (!irFn.isStatic) out_ << "\t.global " << laid.alias << "\n";
        out_ << laid.alias << ":\n";
    }

    int frame = frame_;
    std::ostringstream pro;
    pro.swap(out_);                                        // the prologue, scheduled with the body at -O1 and -O2
    markLine(irFn.pos);                                     // -g: the function's own line, as cl6x's first .dwpsn names it
    std::vector<std::string> saved = savedRegs();
    // A leaf whose parameters and locals are all in registers needs no frame at all.
    bool needFrame = frame > 0 || hasCall_ || (optimize_ > 0 ? paramInFrame_ : !laid.params.empty()) || sretSlot_ != 0 ||
                     saved.size() > 1 || laid.pads || irFn.variadic;
    // An odd:even pair saved side by side on an 8-aligned word (A15 is the caller's B15, 8-aligned) goes
    // in one STDW and comes back in one LDDW - one round trip to memory where there were two.
    auto pairAt = [&](std::size_t k) -> bool {
        if (optimize_ <= 0 || k + 1 >= saved.size() || (4 * (k + 1)) % 8 != 0) return false;
        const std::string &hi = saved[k], &lo = saved[k + 1];
        return hi[0] == lo[0] && hi.size() == 3 && lo.size() == 3 && (lo[2] - '0') % 2 == 0 && hi == pairHigh(lo);
    };
    if (needFrame) {
        out_ << "\tSTW\tA15, *B15\n";               // in the caller's word
        out_ << "\tMV\tB15, A15\n";
        for (size_t k = 1; k < saved.size(); k++) {   // a leaf leaves B3 alone
            if (pairAt(k)) { out_ << "\tSTDW\t" << saved[k] << ":" << saved[k + 1] << ", *-A15(" << 4 * (k + 1) << ")\n"; k++; continue; }
            out_ << "\tSTW\t" << saved[k] << ", *-A15(" << 4 * k << ")\n";
        }
        spAdjust(-(saveBytes_ + frame + (optimize_ > 0 && (hasCall_ || laid.pads) ? 8 : 0)));
    }

    pro.swap(out_);
    // The prologue holds no padding, and the frame is decided after the body.
    std::ostringstream epi;
    epi << returnLabel_ << ":\n";
    if (needFrame) {
        for (size_t k = 1; k < saved.size(); k++) {
            if (pairAt(k)) { epi << "\tLDDW\t*-A15(" << 4 * (k + 1) << "), " << saved[k] << ":" << saved[k + 1] << "\n"; k++; continue; }
            epi << "\tLDW\t*-A15(" << 4 * k << "), " << saved[k] << "\n";
        }
        epi << "\tMV\tA15, B15\n";                 // drop the frame: SP = FP
        epi << "\tLDW\t*A15, A15\n\tNOP\t4\n";      // the caller's FP, last
    }
    epi << "\tB\tB3\n\tNOP\t5\n";
    emitVariables(irFn);                                     // -g: after the label, outside the scheduled code
    // Scheduled with the body at -O1 and -O2, so a result in flight at its end lands under the return's delay slots.
    std::string declined, refused;
    const bool whole = optimize_ > 0 && !backendSkipped("pro");
    if (!whole) out_ << pro.str();
    // The body goes as its entries (C6xSelect.cpp).
    const std::string before = (whole ? pro.str() : std::string()) + params + ssaHead_;
    out_ << c6xSchedule(before, &ssaMir_, epi.str(), optimize_, declined, refused);
    // Declined (Q5 of the M6c review): the serial text stands as -O0 writes it, and tools/c6x-declines counts the line.
    if (!declined.empty()) opt::noteDecline("c6x-unscheduled", irFn.symbol + ": " + declined);
    if (!refused.empty()) unsupported(("the C6000 instruction '" + refused + "', which no unit encodes").c_str());
    // TI's index entry for a function an exception can pass through - one that calls, so saves B3 - naming the table
    // when there are handlers, holding the word itself when there are none. A function that calls nothing gets no
    // entry, as cl6x gives none: the linker covers the run it sits in with one EXIDX_CANTUNWIND.
    emitExceptionTable(irFn.noexcept_, irFn.spec);
    char word[16];
    std::snprintf(word, sizeof word, "0x%08x", unwindWord(needFrame));
    const bool states = irFn.noexcept_ || !irFn.spec.empty();
    bool calls = !callSites().empty() || states;
    for (const std::string &r : savedRegs()) if (r == "B3") calls = true;
    if (calls) anyExidx_ = true;
    if (calls)
        out_ << "\t.sect\t\".c6xabi.exidx:.text\"\n\t.align\t4\n\t.ulong\t$EXIDX_FUNC(" << irFn.symbol << ")\n\t.ulong\t"
             << (callSites().empty() && !states ? std::string(word) : "$EXIDX_EXTAB(\"__c6xabi_extab$" + tiSpelling(irFn.symbol) + "\")") << "\n\t.text\n";

    file_ += out_.str();
    out_.str(std::string());
}

void Tms6747::run(const ir::Module &module) {
    const UnitData &data = module.data;
    emitData(data);
    file_ += out_.str();
    out_.str(std::string());
    for (const std::string &s : data.order) emitFunction(s);
    emitTypes();
    // Dynamic initialisation: the init function's address in .init_array,
    // which the EABI's start-up code - and the VM6747 emulator - walk before
    // main.
    if (!data.initFunction.empty()) {
        out_ << "\t.sect\t\".init_array\"\n\t.align\t4\n\t.word\t" << data.initFunction << "\n";
        file_ += out_.str();
        out_.str(std::string());
    }
    // The personality routine the index entries name by number, which the
    // linker cannot otherwise see them need.
    if (anyExidx_)
        file_ += "\t.global\t__c6xabi_unwind_cpp_pr3\n\t.symdepend\t\"__c6xabi_unwind_cpp_pr3\", \".c6xabi.exidx:.text\"\n";
    if (needsPr2_) file_ += "\t.global\t__c6xabi_unwind_cpp_pr2\n";
    if (needsUnexpected_) file_ += "\t.global\t__cxa_call_unexpected\n";
    if (ssa_ != nullptr && std::getenv("CXX1_SSA_WHY") != nullptr)
        std::fprintf(stderr, "c6x-select: selected=%d of %d\n", ssaSelected_, ssaSelected_);
    sink_ << tiExternals(tiSpelling(file_));
}
