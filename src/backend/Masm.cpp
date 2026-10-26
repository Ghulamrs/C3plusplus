#include "Masm.h"

#include "../Mangle.h"
#include "../Name.h"
#include "../optimizer/Mir.h"
#include "../optimizer/OptTable.h"
#include "../encoder/X64.h"
#include "CodeView.h"

#include "../Source.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>

namespace {

const char kMasmNameChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_?@$";

[[noreturn]] void give_up(const std::string &what, const std::string &why) {
    std::fprintf(stderr, "%s: masm: %s\n  for: %s\n", program::kName, why.c_str(),
                 what.c_str());
    compileFailed();
}

bool isReservedInMasm(const std::string &name) {
    static const char *const kReserved[] = {
        "ah","al","ax","eax","rax","bh","bl","bx","ebx","rbx",
        "ch","cl","cx","ecx","rcx","dh","dl","dx","edx","rdx",
        "si","esi","rsi","di","edi","rdi","bp","ebp","rbp","sp","esp","rsp",
        "r8","r9","r10","r11","r12","r13","r14","r15",
        "cs","ds","es","fs","gs","ss","st","flat","eip","rip",
        "byte","word","dword","qword","tbyte","oword","ptr","offset",
        "length","lengthof","size","sizeof","type","typedef","this",
        "near","far","short","proc","endp","segment","ends","assume",
        "public","extern","externdef","end","align","even","org","dup",
        "mask","width","low","high","lowword","highword",
        "and","or","xor","not","mod","shl","shr","eq","ne","lt","le","gt","ge",
        "if","else","endif","macro","endm","rept","irp","exitm","local",
        "label","comment","include","includelib","name","group","record",
        "struc","struct","union","db","dw","dd","dq","dt","page","title",

        "aaa","aad","aam","aas","adc","add","bound","bsf","bsr","bt","btc",
        "btr","bts","call","cbw","cdq","clc","cld","cli","cmc","cmp","cmps",
        "cmpsb","cmpsd","cmpsw","cqo","cwd","cwde","daa","das","dec","div",
        "enter","esc","hlt","idiv","imul","in","inc","ins","int","into","iret",
        "ja","jae","jb","jbe","jc","jcxz","je","jg","jge","jl","jle","jmp",
        "jna","jnb","jnc","jne","jng","jnl","jno","jnp","jns","jnz","jo","jp",
        "jpe","jpo","js","jz","lahf","lds","lea","leave","les","lock","lods",
        "lodsb","lodsd","lodsw","loop","loope","loopne","loopnz","loopz",
        "mov","movs","movsb","movsd","movss","movsw","movsx","movsxd","movzx",
        "mul","neg","nop","out","outs","pop","popa","popf","push","pusha",
        "pushf","rcl","rcr","rep","repe","repne","repnz","repz","ret","retf",
        "retn","rol","ror","sahf","sal","sar","sbb","scas","scasb","scasd",
        "scasw","seta","setae","setb","setbe","sete","setg","setge","setl",
        "setle","setna","setne","setnz","setz","sgdt","shld","shrd","sidt",
        "stc","std","sti","stos","stosb","stosd","stosw","sub","test","wait",
        "xadd","xchg","xlat","xlatb",
        "fabs","fadd","fchs","fcos","fdiv","fild","finit","fist","fld","fmul",
        "fnop","fprem","fptan","frndint","fscale","fsin","fsqrt","fst","fstp",
        "fsub","ftst","fwait","fxam","fxch",
        "addpd","addps","addsd","addss","andpd","andps","cvtsd2ss","cvtsi2sd",
        "cvtss2sd","cvttsd2si","divsd","divss","maxsd","minsd","movapd","movaps",
        "movd","movq","mulsd","mulss","orpd","orps","pxor","sqrtsd","subsd",
        "subss","ucomisd","ucomiss","xorpd","xorps",
        nullptr,
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(c));
    for (const char *const *p = kReserved; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

bool canUnreserve(const std::string &name) {
    static const char *const kNeverEmitted[] = {
        "fabs","fadd","fchs","fcos","fdiv","fild","finit","fist","fld","fmul",
        "fnop","fprem","fptan","frndint","fscale","fsin","fsqrt","fst","fstp",
        "fsub","ftst","fwait","fxam","fxch",
        nullptr,
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(c));
    for (const char *const *p = kNeverEmitted; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

const char *ptrFor(int bytes) {
    switch (bytes) {
    case 1: return "BYTE PTR ";
    case 2: return "WORD PTR ";
    case 4: return "DWORD PTR ";
    case 8: return "QWORD PTR ";
    default: return "";
    }
}

// **The MASM name and PTR width come from the one opcode table** (M3a.3): a row with no MASM name is one this
// spelling does not know, as its own 156-row table once said by leaving it out.
opt::OpId masmRow(const std::string &m) {
    const opt::OpId id(m);
    if (id.row().masm == nullptr) give_up(m, "an instruction this spelling does not know");
    return id;
}

}

std::string MasmSpelling::mangle(const std::string &name) {
    // A name past ASCII - [lex.name]'s `caf\u00e9` - is not renamed: an import of one could not link.
    for (char c : name)
        if (static_cast<unsigned char>(c) >= 0x80)
            give_up(name, "a name past ASCII is not supported in the MASM spelling - no MASM "
                          "takes one, and the default GNU syntax, -masm=gnu, writes it");
    // A dot, or a character no MASM name holds (`<unnamed-type-$S1>`), only in an internal symbol.
    if (name.find_first_not_of(kMasmNameChars) != std::string::npos) {
        std::string out = "$";
        for (char c : name) out += std::strchr(kMasmNameChars, c) != nullptr ? c : '_';
        return out;
    }
    if (!isReservedInMasm(name)) return name;
    if (defined_.find(name) != defined_.end()) return "$" + name;
    if (!canUnreserve(name))
        give_up(name, "'" + name + "' is a name ml64 reserves and this file "
                      "emits, so an imported symbol cannot be spelled it");
    unreserved_.insert(name);
    return name;
}

MasmSpelling::Rendered MasmSpelling::render(const Op &x) {
    switch (x.kind) {
    case Op::Reg: {

        Str name = x.text.substr(1);
        return { std::string(name), false, false, name.startsWith("xmm") };
    }
    case Op::Imm: {
        if (!x.immNumeric) return { std::string(x.text), false, true, false };
        std::string v = x.immNeg ? "-" + std::to_string(x.uimm)
                                 : std::to_string(x.uimm);
        return { v, false, true, false };
    }
    case Op::Mem: {

        std::string t = "[" + std::string(x.text.substr(1));
        long long d = x.hasDisp ? x.disp : 0;
        // **The whole frame moved, so every offset into it moves with it.** This
        // target takes rbp after the allocation, exactly frameSize lower, so every
        // `[rbp + d]` becomes `[rbp + d + frameSize]` - the positive ones included.
        if (std::string(x.text.substr(1)) == "rbp") d += frameSize_;
        if (x.scale != 0) t += "+" + std::string(x.index.substr(1)) + "*" + std::to_string(x.scale);
        if (d != 0) {
            if (d < 0) t += std::to_string(d);
            else       t += "+" + std::to_string(d);
        }
        t += "]";
        return { t, true, false, false };
    }
    case Op::Rip: {

        std::string sym(x.text);
        referenced_.insert(sym);
        return { "[" + mangle(sym) + "]", true, false, false };
    }
    case Op::Ind:
        return { std::string(x.text.substr(1)), false, false, false };
    case Op::Lbl: {
        std::string sym(x.text);
        referenced_.insert(sym);
        return { mangle(sym), false, false, false };
    }
    // Sized here: ml64 cannot size `gs:[88]` from the register it is moved to (A2024, measured).
    case Op::Seg:
        return { "QWORD PTR " + std::string(x.text.substr(1)) + ":[" + std::to_string(x.disp) + "]", false, false, false };
    case Op::Tls: {
        // **The project's masm relocates `[base + SECTIONREL x]` as SECREL** (6bcb5d2, as ml64 relocates its
        // `mov r32, SECTIONREL x`); `lea eax, SECTIONREL x` would be REL32 and wrong.
        std::string sym(x.text);
        referencedData_.insert(sym);
        return { "[" + std::string(x.index.substr(1)) + " + SECTIONREL " + mangle(sym) + "]", true, false, false };
    }
    }
    return {};
}

// The operand kinds render() answers isMem for, which is what decides where a PTR goes.
static bool memLike(Op::Kind k) { return k == Op::Mem || k == Op::Rip || k == Op::Tls; }

// The register's MASM name in upper case, from the spelling's `%rax`.
static std::string upperReg(Str r) {
    std::string s(r.substr(1));
    for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// **render(), as data**: the same cases, the same arithmetic (`[rbp + d]` moves by the frame), the same bookkeeping
// of what is referenced - so the preamble's EXTERN lines do not depend on which door an instruction took. The
// segment bytes and the `[88]` of a Seg are what the text parser makes of `gs:[88]`; a label is an O_MEM the unit types.
bool MasmSpelling::structuredOperand(const Op &x, masm::Operand &o, std::string &sym) {
    masm::clearOperand(o);
    sym.clear();
    switch (x.kind) {
    case Op::Reg:
    case Op::Ind:
        return masm::registerNamed(upperReg(x.text), o);
    case Op::Imm: {
        o.kind = masm::O_IMM;
        if (x.immNumeric) {
            o.value = static_cast<long long>(x.uimm);
            if (x.immNeg) o.value = -o.value;
            o.wide = x.uimm >= 0x100000000ULL;
            return true;
        }
        // A textual immediate is one the walker spelled itself; only a plain decimal is a number to the MASM lexer.
        const std::string t(x.text);
        if (t.empty() || t.find_first_not_of("0123456789") != std::string::npos) return false;
        o.value = std::strtoll(t.c_str(), nullptr, 10);
        o.wide = static_cast<unsigned long long>(o.value) >= 0x100000000ULL;
        return true;
    }
    case Op::Mem: {
        o.kind = masm::O_MEM;
        masm::Operand base;
        if (!masm::registerNamed(upperReg(x.text), base) || base.size != 64) return false;
        o.base = base.reg;
        long long d = x.hasDisp ? x.disp : 0;
        if (std::string(x.text.substr(1)) == "rbp") d += frameSize_;
        if (x.scale != 0) {
            masm::Operand index;
            if (!masm::registerNamed(upperReg(x.index), index) || index.size != 64 || index.reg == 4) return false;
            o.index = index.reg;
            o.scale = x.scale;
        }
        o.value = d;
        return true;
    }
    case Op::Rip:
    case Op::Lbl: {
        std::string s(x.text);
        referenced_.insert(s);
        o.kind = masm::O_MEM;
        o.label = true;
        sym = mangle(s);
        return true;
    }
    case Op::Seg: {
        const std::string r(x.text.substr(1));
        o.kind = masm::O_MEM;
        o.size = 64;
        o.ptr = true;
        o.seg = r == "gs" ? 0x65 : r == "fs" ? 0x64 : 0;
        o.value = x.disp;
        return o.seg != 0;
    }
    case Op::Tls: {
        std::string s(x.text);
        referencedData_.insert(s);
        masm::Operand base;
        if (!masm::registerNamed(upperReg(x.index), base) || base.size != 64) return false;
        o.kind = masm::O_MEM;
        o.base = base.reg;
        o.secrel = true;
        sym = mangle(s);
        return true;
    }
    }
    return false;
}

// The PTR width the text would have carried goes on the operand (`ptr`, so a label's own type does not override it);
// the operands are in MASM's order, the destination first, as the parse would refer to their symbols.
bool MasmSpelling::structuredIns(const std::string &m, const char *masmName, int width, const Op *a, const Op *b) {
    if (structured_ == nullptr) return false;
    masm::Instruction ins;
    std::string name = masmName;
    ins.prefix = 0;
    if (name.compare(0, 4, "rep ") == 0) { ins.prefix = 0xF3; name = name.substr(4); }
    for (char &c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ins.name = name;
    const Op *order[2] = { b, a };    // b is the destination, written first; a alone is the one operand
    if (b == nullptr) { order[0] = a; order[1] = nullptr; }
    for (int i = 0; i < 2; i++) {
        if (order[i] == nullptr) continue;
        masm::Operand o;
        std::string sym;
        if (!structuredOperand(*order[i], o, sym)) return false;
        ins.ops.push_back(o);
        ins.syms.push_back(sym);
    }
    if (width != 0) {
        // One memory operand takes the PTR, as render's isMem decides: the source where it is memory, else the destination - but not movsxd's.
        std::size_t at = ins.ops.size();
        if (b != nullptr && memLike(a->kind)) at = 1;
        else if (b != nullptr && memLike(b->kind) && m != "movsxd") at = 0;
        else if (b == nullptr && memLike(a->kind)) at = 0;
        if (at < ins.ops.size() && !ins.ops[at].ptr) {
            ins.ops[at].size = width * 8;
            ins.ops[at].ptr = true;
        }
    }
    structured_->push_back(ins);
    o_ += "  \001"; appendNum(o_, static_cast<long long>(structured_->size() - 1)); o_ += '\n';
    return true;
}

void MasmSpelling::ins(const std::string &m) {
    afterCall_ = false;
    const opt::Opcode &r = masmRow(m).row();
    if (structuredIns(m, r.masm, r.masmWidth, nullptr, nullptr)) return;
    o_ += "  "; o_ += r.masm; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a) {
    const opt::Opcode &r = masmRow(m).row();
    afterCall_ = r.kind == opt::Opcode::Call;
    if (structuredIns(m, r.masm, r.masmWidth, &a, nullptr)) return;
    Rendered x = render(a);
    std::string t = x.text;
    if (x.isMem && r.masmWidth != 0) t = std::string(ptrFor(r.masmWidth)) + t;
    o_ += "  "; o_ += r.masm; o_ += ' '; o_ += t; o_ += '\n';
}

// The 64-bit name of a register any width names (`ecx`, `cl` -> `rcx`), or "" for none.
static std::string reg64(const std::string &n) {
    static const char *const kRows[][4] = {
        {"rax","eax","ax","al"}, {"rcx","ecx","cx","cl"}, {"rdx","edx","dx","dl"}, {"rbx","ebx","bx","bl"},
        {"rsi","esi","si","sil"}, {"rdi","edi","di","dil"}, {"rbp","ebp","bp","bpl"}, {"rsp","esp","sp","spl"},
        {"r8","r8d","r8w","r8b"}, {"r9","r9d","r9w","r9b"}, {"r10","r10d","r10w","r10b"}, {"r11","r11d","r11w","r11b"}};
    for (const auto &row : kRows)
        for (const char *w : row)
            if (n == w) return row[0];
    return "";
}

// **ml64 takes SECTIONREL only after a 32-bit register** (measured 2026-10-25: `mov r32, SECTIONREL x` and
// `add eax, ...` relocate as SECREL; `lea rax, [rax + SECTIONREL x]` and `cmp BYTE PTR [...]` are A2024). The offset
// goes into a register neither operand names, borrowed by push and pop - the -O2 allocator may hold any of them.
void MasmSpelling::ml64Tls(const std::string &m, const Op &a, const Op &b) {
    const Op &t = a.kind == Op::Tls ? a : b, &other = a.kind == Op::Tls ? b : a;
    if (other.kind == Op::Mem && reg64(std::string(other.text.substr(1))) == "rsp")
        give_up(std::string(t.text), "a thread_local beside an rsp operand in the ml64 spelling, whose push would move it");
    const std::string base(t.index.substr(1)), near = other.kind == Op::Reg || other.kind == Op::Mem
                                                         ? reg64(std::string(other.text.substr(1))) : "";
    static const char *const kScratch[][3] = {{"r11","r11d","%r11"}, {"r10","r10d","%r10"}, {"rdx","edx","%rdx"},
                                              {"rcx","ecx","%rcx"}};
    const char *const *s = nullptr;
    for (const auto &c : kScratch)
        if (base != c[0] && near != c[0]) { s = c; break; }
    std::string sym(t.text);
    referencedData_.insert(sym);
    o_ += "  push "; o_ += s[0]; o_ += "\n  mov "; o_ += s[1]; o_ += ", SECTIONREL "; o_ += mangle(sym); o_ += '\n';
    const Op via = memIndexed(0, false, t.index, Str(s[2]), 1);
    if (a.kind == Op::Tls) ins(m, via, b); else ins(m, a, via);
    o_ += "  pop "; o_ += s[0]; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    afterCall_ = false;
    const opt::OpId id = masmRow(m);
    const opt::Opcode &r = id.row();
    if (!comdat_ && (a.kind == Op::Tls || b.kind == Op::Tls)) { ml64Tls(m, a, b); return; }

    // Both checks are about the operands' kinds, so both doors meet them before either is taken.
    if (r.masmWidth == 0 && memLike(b.kind) && a.kind == Op::Imm)
        give_up(m, "a store of an immediate with no width to infer");
    // `movq` between an xmm register and anything is the SSE move of that name, not a plain `mov`.
    const bool xmm = (a.kind == Op::Reg && a.text.substr(1).startsWith("xmm")) ||
                     (b.kind == Op::Reg && b.text.substr(1).startsWith("xmm"));
    const char *masmName = id == opt::mn::Movq && xmm ? "movq" : r.masm;
    if (structuredIns(m, masmName, r.masmWidth, &a, &b)) return;

    Rendered src = render(a), dst = render(b);

    const char *name = masmName;

    std::string sTxt = src.text, dTxt = dst.text;
    if (r.masmWidth != 0) {
        if (src.isMem) sTxt = std::string(ptrFor(r.masmWidth)) + sTxt;
        else if (dst.isMem) dTxt = std::string(ptrFor(r.masmWidth)) + dTxt;
    }

    o_ += "  "; o_ += name; o_ += ' '; o_ += dTxt; o_ += ", "; o_ += sTxt; o_ += '\n';
}

void MasmSpelling::defLabel(const std::string &l) {
    if (seg_ == Code) {
        // Internal: the Walker's own (.L.), and under -g the function's bounds (.Lfunc.) and its
        // blocks' (.L.blk.), which the CodeView records measure (1.29).
        if (l.compare(0, 2, ".L") != 0)
            give_up(l, "a label in code that is not an internal one");
        defined_.insert(l);
        o_ += mangle(l); o_ += ":\n";
        return;
    }
    defined_.insert(l);
    flushPending();
    pending_ = mangle(l);
}

// `mergeable` is what a COMDAT would be told, and ml64 has no directive that
// reaches the COMDAT bit - see CoffSpelling, which exists for that reason.
void MasmSpelling::functionBegin(const std::string &name, bool exported,
                                 bool mergeable) {
    inFunction_ = true;
    lastLoc_.clear();
    lastLocFile_ = lastLocLine_ = 0;
    linesRun_.clear();
    defined_.insert(name);
    if (exported) exported_.insert(name);
    flushPending();
    closeDataBlock();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
    mergeable_ = comdat_ && mergeable;
    if (mergeable_) o_ += ".text$mn SEGMENT ALIGN(16) 'CODE' COMDAT(" + mangle(name) + ")\n";

    // **`PROC`, not `PROC FRAME`, and the unwind data written by hand.** No MASM
    // directive reaches the handler *data* inside UNWIND_INFO, and cl writes
    // $pdata$ and $unwind$ itself. Done for every function, so one path is walked.
    fnName_ = name;
    o_ += mangle(name); o_ += " PROC\n";
    o_ += "$LNbeg$"; o_ += mangle(name); o_ += ":\n";
}

// See Spelling::stateLabel; cl writes `npad 1` here.
void MasmSpelling::stateLabel(const std::string &l) {
    if (afterCall_) ins("nop");
    defLabel(l);
}

void MasmSpelling::raw(const std::string &text) {
    flushPending();
    o_ += text;
}

void MasmSpelling::prologue(int frameSize, const std::string &lsda, int outgoing, int realign) {
    frameSize += outgoing;
    afterCall_ = false;
    // **The LSDA name says a landing pad exists, which is not the same
    // question.** A Microsoft FuncInfo follows only where one is written, and
    // the code generator says so through `noteHasEh` before `functionEnd`.
    (void)lsda;
    frameSize_ = frameSize;
    const std::string m = mangle(fnName_);

    // **The frame pointer is taken *after* the allocation on this target**, which is the whole difference from the
    // Itanium prologue: every displacement in an FH3 table is an unsigned offset up from the establisher. Measured
    // against cl. Each instruction goes through ins(), so the structural door (M4.4) carries it too; the text is the same.
    ins("push", reg("%rbp"));
    o_ += "$LNpush$" + m + ":\n";
    // rbx as a second pushed nonvolatile, the realignment after the prologue - see CoffSpelling.
    if (realign > 0) { ins("push", reg("%rbx")); o_ += "$LNpushb$" + m + ":\n"; }
    // A frame of a page or more is probed first - cl's rule, measured: 4056 is
    // a plain sub, 4120 calls __chkstk. Unprobed, the first write at the bottom
    // skips the guard page and is an access violation, not a stack overflow.
    if (frameSize >= 4096) {
        ins("movl", imm(frameSize), reg("%eax"));
        ins("call", lbl("__chkstk"));
        ins("sub", reg("%rax"), reg("%rsp"));
    } else if (frameSize > 0) {
        ins("sub", imm(frameSize), reg("%rsp"));
    }
    o_ += "$LNalloc$" + m + ":\n";
    ins("mov", reg("%rsp"), reg("%rbp"));
    o_ += "$LNfp$" + m + ":\n";
    // **The callee-saved registers an optimizer took, stored up from rbp**, as
    // the COFF prologue does: each slot is `disp` below the frame's top - which render() adds for an rbp operand.
    for (std::size_t i = 0; i < saves_.size(); ++i) {
        ins("movq", reg(saves_[i].reg), mem(saves_[i].disp, "%rbp"));
        o_ += "$LNsave" + std::to_string(i) + "$" + m + ":\n";
    }
    o_ += "$LNprolog$" + m + ":\n";
    if (realign > 0) { ins("and", imm(-realign), reg("%rsp")); ins("mov", reg("%rsp"), reg("%rbx")); }
    afterCall_ = false;

    // Last instruction first, which is the order an unwinder undoes them in:
    // the saves, SET_FPREG, the allocation, the push.
    unwindCodes_ = 0;
    unwindData_.clear();
    // UWOP_SAVE_NONVOL is 4 with the register in the high nibble, and the
    // slot's offset up from rsp - which rbp equals - in eights.
    for (std::size_t i = saves_.size(); i-- > 0;) {
        const int id = opt::parseReg(saves_[i].reg).id;
        char op[8];
        std::snprintf(op, sizeof op, "0%02XH", (id << 4) | 4);
        unwindData_ += "  DB $LNsave" + std::to_string(i) + "$" + m + "-$LNbeg$" + m + "\n";
        unwindData_ += std::string("  DB ") + op + "\n";
        unwindData_ += "  DW " + std::to_string((frameSize + saves_[i].disp) / 8) + "\n";
        unwindCodes_ += 2;
    }
    // UWOP_SET_FPREG is 3; the frame offset lives in the header, and it is
    // zero because rbp is set to rsp exactly.
    unwindData_ += "  DB $LNfp$" + m + "-$LNbeg$" + m + "\n";
    unwindData_ += "  DB 03H\n";
    unwindCodes_ += 1;
    // UWOP_ALLOC_SMALL is 2, with (size/8 - 1) in the high nibble, and
    // reaches 128 bytes; past that UWOP_ALLOC_LARGE is 1 with a slot of its
    // own holding size/8.
    if (frameSize > 0) {
        unwindData_ += "  DB $LNalloc$" + m + "-$LNbeg$" + m + "\n";
        if (frameSize <= 128 && frameSize % 8 == 0) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%02XH",
                          ((frameSize / 8 - 1) << 4) | 2);
            unwindData_ += "  DB 0"; unwindData_ += buf; unwindData_ += "\n";
            unwindCodes_ += 1;
        } else {
            unwindData_ += "  DB 01H\n";
            unwindData_ += "  DW " + std::to_string((frameSize + 7) / 8) + "\n";
            unwindCodes_ += 2;
        }
    }
    // UWOP_PUSH_NONVOL is 0 with the register in the high nibble - rbx is 3, rbp is 5.
    if (realign > 0) {
        unwindData_ += "  DB $LNpushb$" + m + "-$LNbeg$" + m + "\n  DB 030H\n";
        unwindCodes_ += 1;
    }
    unwindData_ += "  DB $LNpush$" + m + "-$LNbeg$" + m + "\n";
    unwindData_ += "  DB 050H\n";
    unwindCodes_ += 1;
}

// **Every offset here is a label difference, not a counted byte.** An unwind code
// records where its instruction *ends*, and `sub rsp, 40` is four bytes where
// `sub rsp, 400` is seven - the assembler chooses, so the assembler is asked.
void MasmSpelling::functionEnd(const std::string &name) {
    inFunction_ = false;
    const std::string m = mangle(name);
    // **The dot is the whole of it.** A segment called `pdata` is a segment called
    // pdata; the linker builds the image's exception directory from `.pdata`, and
    // without it the runtime finds no unwind record - measured with dumpbin.
    o_ += "$LNend$" + m + ":\n";
    o_ += m + " ENDP\n";
    if (mergeable_) o_ += ".text$mn ENDS\n";

    // READONLY and the alignment, or the linker finds two .pdata
    // sections with different attributes and says so.
    o_ += "\n.pdata SEGMENT READONLY ALIGN(4) 'DATA'" + associative() + "\n";
    o_ += "$pdata$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o_ += "  DD imagerel $LNend$" + m + "\n";
    o_ += "  DD imagerel $unwind$" + m + "\n";
    o_ += ".pdata ENDS\n";

    // UNWIND_INFO.
    o_ += ".xdata SEGMENT READONLY ALIGN(8) 'DATA'" + associative() + "\n";
    // Version 1, and the flags in the top five bits. 0x19 is UNW_FLAG_EHANDLER and
    // UNW_FLAG_UHANDLER, which is what cl writes for a function with a `try`.
    // Without them the runtime unwinds past the frame and never reads the FuncInfo.
    o_ += "$unwind$" + m + (hasEh_ ? " DB 019H\n" : " DB 01H\n");
    o_ += "  DB $LNprolog$" + m + "-$LNbeg$" + m + "\n";
    o_ += "  DB " + std::to_string(unwindCodes_) + "\n";
    o_ += "  DB 05H\n";
    o_ += unwindData_;
    // The codes are padded to an even count, and the handler pointers go
    // after that padding rather than after the codes.
    if (unwindCodes_ % 2 != 0) o_ += "  DW 0\n";
    if (hasEh_) {
        referenced_.insert("__CxxFrameHandler3");
        o_ += "  DD imagerel __CxxFrameHandler3\n";
        o_ += "  DD imagerel $cppxdata$" + m + "\n";
    }
    o_ += ".xdata ENDS\n\n";
    o_ += ".CODE\n";

    unwindData_.clear();
    unwindCodes_ = 0;
    hasEh_ = false;
    saves_.clear();
}

// **-g through GRIDE's masm** (1.29, the PDB line, as c90 does it): `.FILE n, "path"` names source file n, and masm writes the
// CodeView line table from the `.LINE`s that follow. The path arrives as codeViewPath spelt it for clang - absolute, backslashes
// and quotes escaped - and a MASM string undoes that, doubling a quote instead.
void MasmSpelling::fileEntry(int n, const std::string &name) {
    std::string path;
    for (std::size_t i = 0; i < name.size(); i++) {
        if (name[i] == '\\' && i + 1 < name.size()) i++;
        if (name[i] == '"') path += '"';
        path += name[i];
    }
    o_ += ".FILE "; appendNum(o_, n); o_ += ", \""; o_ += path; o_ += "\"\n";
}

// Whether text holds an instruction: a line indented, and not a directive.
static bool holdsInstruction(const std::string &text) {
    for (std::size_t at = 0; at < text.size();) {
        if (text.compare(at, 2, "  ") == 0 && at + 2 < text.size() && text[at + 2] != '.') return true;
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return false;
}

// `.LINE line, file` before the instructions of a line, by CoffSpelling::location's rules for
// `.cv_loc`: one entry per run of a line, no second run of it in a function (cdb refuses `bp` on
// a line entered twice), and one no instruction follows replaced by the next.
void MasmSpelling::location(int file, int line, int) {
    if (!inFunction_) return;
    if (file == lastLocFile_ && line == lastLocLine_) return;
    if (!linesRun_.insert(std::make_pair(file, line)).second) return;
    lastLocFile_ = file;
    lastLocLine_ = line;
    if (!lastLoc_.empty() && lastLocAt_ + lastLoc_.size() <= o_.size() &&
        o_.compare(lastLocAt_, lastLoc_.size(), lastLoc_) == 0 &&
        !holdsInstruction(o_.substr(lastLocAt_ + lastLoc_.size())))
        o_.erase(lastLocAt_, lastLoc_.size());
    lastLocAt_ = o_.size();
    o_ += "  .LINE "; appendNum(o_, line); o_ += ", "; appendNum(o_, file); o_ += '\n';
    lastLoc_ = o_.substr(lastLocAt_);
}

// **A funclet PROC's lines are a run of their own** (1.29): masm makes one table per PROC, so a line the
// function had may come again in its handler; the function's state waits and comes back after.
void MasmSpelling::funcletLinesBegin() {
    LineState s;
    s.lastLoc = lastLoc_; s.lastLocAt = lastLocAt_; s.lastLocFile = lastLocFile_; s.lastLocLine = lastLocLine_;
    s.linesRun = linesRun_;
    outerLines_.push_back(s);
    lastLoc_.clear(); lastLocAt_ = 0; lastLocFile_ = lastLocLine_ = 0;
    linesRun_.clear();
}

void MasmSpelling::funcletLinesEnd() {
    if (outerLines_.empty()) return;
    const LineState &s = outerLines_.back();
    lastLoc_ = s.lastLoc; lastLocAt_ = s.lastLocAt; lastLocFile_ = s.lastLocFile; lastLocLine_ = s.lastLocLine;
    linesRun_ = s.linesRun;
    outerLines_.pop_back();
}

// masm names no function in a `.LINE`: the PROC the instruction is in is the one described.
std::string MasmSpelling::locationDirective(int, int file, int line, int) const {
    std::string o = "  .LINE ";
    appendNum(o, line); o += ", "; appendNum(o, file); o += '\n';
    return o;
}

// Every name in an expression as this spelling writes it: `.Lcv.r3.e-.Lcv.r3.b` and the like.
std::string MasmSpelling::mangleNames(const std::string &expr) {
    std::string out;
    for (std::size_t i = 0; i < expr.size();) {
        const char c = expr[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' || c == '?' || c == '@') {
            std::size_t j = i;
            while (j < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_' ||
                                       expr[j] == '.' || expr[j] == '$' || expr[j] == '?' || expr[j] == '@'))
                j++;
            out += mangle(expr.substr(i, j - i));
            i = j;
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

// **The CodeView writeCodeView makes for clang, in MASM** - one writer, two spellings, as c90's MasmSpelling::codeView has it. `.debug$S` and `.debug$T` become SEGMENTs (a mergeable function's records ASSOCIATIVE with its COMDAT),
// `.long`, `.short`, `.byte` and `.asciz` DD, DW and DB, `.secrel32` and `.secidx` SECTIONREL and SECTION, a label its MASM name.
// The line table, the file checksums and the string table are left to masm, which makes them from the `.LINE`s and appends them to the plain `.debug$S`.
void MasmSpelling::codeView(const std::string &gnu) {
    flushPending();
    closeDataBlock();
    std::string open;
    for (std::size_t at = 0; at < gnu.size();) {
        std::size_t nl = gnu.find('\n', at);
        if (nl == std::string::npos) nl = gnu.size();
        const std::string line = gnu.substr(at, nl - at);
        at = nl + 1;
        const std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const std::string t = line.substr(b);
        if (b == 0 && t[t.size() - 1] == ':') {
            const std::string name = t.substr(0, t.size() - 1);
            defined_.insert(name);
            o_ += mangle(name); o_ += ":\n";
            continue;
        }
        const std::size_t sp = t.find_first_of(" \t");
        const std::string d = t.substr(0, sp);
        const std::string rest = sp == std::string::npos ? std::string() : t.substr(t.find_first_not_of(" \t", sp));
        if (d == ".section") {
            if (!open.empty()) o_ += open + " ENDS\n";
            open = rest.compare(0, 8, ".debug$T") == 0 ? ".debug$T" : ".debug$S";
            o_ += "\n" + open + " SEGMENT";
            const std::size_t assoc = rest.find(",associative,");
            if (assoc != std::string::npos) o_ += " ASSOCIATIVE(" + mangle(rest.substr(assoc + 13)) + ")";
            o_ += "\n";
        } else if (d == ".p2align") {
            o_ += "  ALIGN "; appendNum(o_, 1LL << std::atoi(rest.c_str())); o_ += '\n';
        } else if (d == ".long") {
            o_ += "  DD " + mangleNames(rest) + "\n";
        } else if (d == ".short") {
            o_ += "  DW " + mangleNames(rest) + "\n";
        } else if (d == ".byte") {
            o_ += "  DB " + rest + "\n";
        } else if (d == ".asciz") {
            // the GNU string's escapes undone, MASM's one escape - a doubled quote - made
            std::string s;
            for (std::size_t k = 1; k + 1 < rest.size(); k++) {
                if (rest[k] == '\\' && k + 2 < rest.size()) k++;
                if (rest[k] == '"') s += '"';
                s += rest[k];
            }
            o_ += s.empty() ? "  DB 0\n" : "  DB \"" + s + "\", 0\n";
        } else if (d == ".secrel32") {
            o_ += "  DD SECTIONREL " + mangleNames(rest) + "\n";
        } else if (d == ".secidx") {
            o_ += "  DW SECTION " + mangleNames(rest) + "\n";
        } else if (d != ".cv_linetable" && d != ".cv_filechecksums" && d != ".cv_stringtable") {
            give_up(line, "a CodeView directive the MASM spelling has no words for");
        }
    }
    if (!open.empty()) o_ += open + " ENDS\n";
    // The next code or data reopens its segment.
    seg_ = None;
}

void MasmCodeGen::writeDebug(const std::vector<DwarfFunction> &fns,
                             const std::vector<DwarfGlobal> &globals) {
    std::string gnu;
    writeCodeView(gnu, target_, CodeViewLanguage::Cpp,
                  codeViewPath(compDir(), lineSource()->files().front() + ".obj", false), fns, globals,
                  [](const std::string &s) { return s; });
    masm_.codeView(gnu);
}

void MasmSpelling::globl(const std::string &name) { exported_.insert(name); }

// A function's own name, or a second label in it, is covered by the COMDAT
// functionBegin opened.
void MasmSpelling::weakDefinition(const std::string &name) {
    if (!comdat_ || seg_ == Code) return;
    flushPending();
    closeDataBlock();
    pendingComdat_ = name;
}

// A segment of its own: a COMDAT's, or - with no COMDAT pending - one whose
// ALIGN() the simplified .DATA, .CONST and .DATA? (PARA, 16) cannot give an
// `alignas(64)` object; its name carries the alignment, since attributes may not change.
void MasmSpelling::openDataBlock(int align) {
    const bool comdat = !pendingComdat_.empty();
    // A COMDAT's block takes the classic name its section is made from: `.data` alone is the .DATA directive.
    std::string name = comdat ? (seg_ == Bss ? "_BSS" : seg_ == Data ? "_DATA" : "CONST")
                              : (seg_ == Bss ? ".bss" : seg_ == Data ? ".data" : ".rdata");
    if (!comdat) name += "$a" + std::to_string(align);
    o_ += std::string("\n") + name + " SEGMENT" + (seg_ == Const ? " READONLY" : "") +
          " ALIGN(" + std::to_string(align < 16 ? 16 : align) + ")" +
          (seg_ == Bss ? " 'BSS'" : " 'DATA'") +
          (comdat ? " COMDAT(" + mangle(pendingComdat_) + ")" : "") + "\n";
    pendingComdat_.clear();
    dataBlock_ = name;
    dataBlockUsed_ = false;
}

void MasmSpelling::closeDataBlock() {
    if (!pendingComdat_.empty()) openDataBlock(16);    // an object that never aligned
    if (!dataBlock_.empty()) o_ += dataBlock_ + " ENDS\n";
    dataBlock_.clear();
    dataBlockUsed_ = false;
}

void MasmSpelling::textSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
}

void MasmSpelling::rodataSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Const) { o_ += "\n.CONST\n"; seg_ = Const; }
}

void MasmSpelling::dataSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Data) { o_ += "\n.DATA\n"; seg_ = Data; }
}

void MasmSpelling::bssSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Bss) { o_ += "\n.DATA?\n"; seg_ = Bss; }
}

// **`.tls$`, with the dot**: ml64 names a `_TLS SEGMENT` `_TLS` and the linker builds no template
// from it - measured with dumpbin.
void MasmSpelling::tlsDataSection() {
    flushPending();
    if (seg_ == Tls) return;
    closeDataBlock();
    o_ += "\n.tls$ SEGMENT ALIGN(16) 'DATA'\n";
    dataBlock_ = ".tls$";
    seg_ = Tls;
}

void MasmSpelling::tlsInitialiserEntry(const std::string &fn) {
    flushPending();
    closeDataBlock();
    o_ += "\n.CRT$XDU SEGMENT READONLY ALIGN(8) 'DATA'\n" + mangle(fn + "$initializer$") + " DQ ";
    o_ += mangle(fn);
    o_ += "\n.CRT$XDU ENDS\n.drectve SEGMENT INFO\n  DB \" /include:__dyn_tls_init\"\n.drectve ENDS\n";
    seg_ = None;
}

void MasmSpelling::objectType(const std::string &) {}
void MasmSpelling::objectSize(const std::string &, int) {}

// ALIGN 16, the code segment's own: MASM has no "pad only if it would cross" form.
void MasmSpelling::loopAlign(int) { flushPending(); o_ += "  ALIGN 16\n"; }

void MasmSpelling::align(int n) {
    flushPending();
    // The TLS template is one segment, every object in it: nothing to close or reopen.
    if (seg_ != Code && seg_ != Tls) {
        if (!dataBlock_.empty() && dataBlockUsed_) closeDataBlock();
        // An ALIGN past 16 inside .DATA is refused: such an object gets a segment of its own.
        if (!pendingComdat_.empty() || n > 16) openDataBlock(n);
        dataBlockUsed_ = true;
    }
    o_ += "  ALIGN "; appendNum(o_, n); o_ += '\n';
}

void MasmSpelling::zero(int n) {
    if (n == 0) return;
    items("DB", { std::to_string(n) +
                  (seg_ == Bss ? " DUP (?)" : " DUP (0)") });
}

void MasmSpelling::dataInt(int size, long long v) {
    const char *dir = size == 1 ? "DB" : size == 2 ? "DW"
                    : size == 4 ? "DD" : "DQ";
    items(dir, { std::to_string(v) });
}

void MasmSpelling::dataSym(const std::string &sym, long long off) {
    // **A symbol named by data is a use, and MASM needs it declared.**
    referenced_.insert(sym);
    std::string t = mangle(sym);
    if (off > 0) t += "+" + std::to_string(off);
    else if (off < 0) t += "-" + std::to_string(-off);
    items("DQ", { t });
}

void MasmSpelling::dataBytes(const std::string &bytes) {
    std::vector<std::string> it;
    it.reserve(bytes.size());
    for (char c : bytes)
        it.push_back(std::to_string(
            static_cast<int>(static_cast<unsigned char>(c))));
    items("DB", it);
}

void MasmSpelling::flushPending() {
    if (!pending_.empty()) {
        o_ += pending_; o_ += " LABEL BYTE\n";
        pending_.clear();
    }
}

void MasmSpelling::items(const char *dir, const std::vector<std::string> &it) {
    const std::size_t kPerLine = 16;
    for (std::size_t i = 0; i < it.size(); i += kPerLine) {
        std::string chunk;
        for (std::size_t j = i; j < it.size() && j < i + kPerLine; j++) {
            if (!chunk.empty()) chunk += ", ";
            chunk += it[j];
        }
        if (i == 0 && !pending_.empty()) {
            o_ += pending_; o_ += ' '; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
            pending_.clear();
        } else {
            o_ += "  "; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
        }
    }
}

// **`.CRT$XCU`, with the dot.** cl's listing writes `CRT$XCU SEGMENT` and
// `DQ FLAT:`, neither of which is what ml64 takes - the same trap `.pdata` and
// `.text$x` set. Measured on the box: this runs the function before main.
void MasmSpelling::initialiserEntry(const std::string &fn, bool) {
    flushPending();
    closeDataBlock();
    o_ += "\n.CRT$XCU SEGMENT READONLY ALIGN(8) 'DATA'\n  DQ ";
    o_ += mangle(fn);
    o_ += "\n.CRT$XCU ENDS\n";
    seg_ = None;
}

void MasmSpelling::predefine(const std::vector<std::string> &names) {
    for (const std::string &n : names) defined_.insert(n);
}

void MasmSpelling::preamble(std::ostream &sink) {
    sink << "; Generated by cxx1 for x86_64-windows, in MASM syntax for ml64.\n";
    sink << "; The instruction selection is the same one the Linux backend makes;\n";
    sink << "; only the spelling is Microsoft's. See src/backend/Masm.cpp.\n\n";

    // **DOTNAME lets a segment be called `.pdata`, and NOSCOPED lets the unwind
    // data name the labels inside a procedure.** Both are needed for one reason:
    // the tables live outside the function and measure into it.
    sink << "OPTION DOTNAME\n";
    sink << "OPTION NOSCOPED\n";
    // **MASM exports every PROC unless told otherwise**, a `static` included.
    sink << "OPTION PROC:PRIVATE\n\n";

    for (const std::string &g : unreserved_)
        sink << "OPTION NOKEYWORD:<" << g << ">\n";
    if (!unreserved_.empty()) sink << "\n";

    for (const std::string &e : exported_) {
        const std::string spelt = mangle(e);
        // Renaming an exported name would break the link; only a dot was ever renamed in one.
        if (e.find_first_not_of(std::string(kMasmNameChars) + ".") != std::string::npos)
            give_up(e, "an exported name holds a character no MASM name can spell");
        sink << "PUBLIC " << spelt << "\n";
    }
    for (const std::string &r : referenced_)
        if (defined_.find(r) == defined_.end() && referencedData_.find(r) == referencedData_.end())
            sink << "EXTERN " << mangle(r) << ":PROC\n";
    for (const std::string &r : referencedData_)
        if (defined_.find(r) == defined_.end())
            sink << "EXTERN " << mangle(r) << ":BYTE\n";
    sink << "\n";
}

void MasmSpelling::postamble(std::ostream &sink) {

    if (!pending_.empty())
        give_up(pending_, "a data label left dangling at the end of the file");
    // The chunks are written by now, so a block still open is closed here.
    const std::size_t mark = o_.size();
    closeDataBlock();
    sink << o_.substr(mark);
    o_.resize(mark);
    sink << trailer_;
    trailer_.clear();
    sink << "\nEND\n";
}

// **The chain the Microsoft ABI wants before anything may be thrown** - ??_R0H@8,
// _CT??_R0H@84, _CTA1H, _TI1H - measured from cl's listing, `imagerel` throughout
// and in cl's segments. Then -2, the scratch word saying nothing is entered yet.
int MasmCodeGen::establisherOffset(int slot) const {
    return masm_.frameSize_ - slot;
}

// **Through the assembler rather than as a raw string**, so the renderer's `+ frameSize_` is the
// only place the frame moves: `[rbp-slot]` written here comes out as `[rbp + frameSize - slot]`
// there, which is the same arithmetic establisherOffset does for the tables.
void MasmCodeGen::storeUnwindHelp(int slot) {
    if (passesRun()) optimizer_->sharedSlot(-slot, 8);
    a_->ins("movq", imm(-2), mem(-slot, "%rbp"));
}

// The funclet's symbol is cl's shape, `fn$catch$0`; the spelling writes its head and tail (M3a.4), and closeFunclet is
// the base's. `inFunclet_` is left as it stands here, as this code generator never set it: a call inside a handler
// finds the funclet's 32 bytes at the floor, which is what it emitted before. The .pdata goes to MasmSpelling::trailer_.
std::string MasmCodeGen::beginFunclet(std::size_t pos, int number, bool cleanup) {
    funcletOuter_.push_back(std::make_pair(funcletSymbol_, inFunclet_));
    funcletSymbol_ = masm_.mangledName() + funcletKind_ + std::to_string(funcletIndex_++);
    a_->funcletBegin(funcletSymbol_, funcletEntryLocation(pos));
    funcletDebugBegin(pos, number, cleanup);
    return funcletSymbol_;
}

void MasmCodeGen::emitExceptionTables(const Function &fn) {
    if (msStates().empty()) {
        if (!callSites().empty()) emitLsda(fn.symbol(), false);
        out_ += a_->takeFunclets();
        funcletIndex_ = 0;
        return;
    }

    const std::string m = masm_.mangledName();
    const std::vector<MsState> &states = msStates();
    const std::vector<MsTryBlock> &tries = msTryBlocks();
    std::string o;
    o += a_->takeFunclets();
    funcletIndex_ = 0;

    // **Every offset is measured from the establisher frame**, so a slot at
    // [rbp-N] is at frameSize-N; a cleanup is a state whose action is its funclet.
    // Associative with an inline function, or a copy the linker discards would leave these naming its labels.
    o += "\n.xdata SEGMENT READONLY ALIGN(8) 'DATA'" + masm_.associative() + "\n";
    o += "$cppxdata$" + m + " DD 019930522H\n";
    o += "  DD " + std::to_string(states.size()) + "\n";
    o += "  DD imagerel $stateUnwindMap$" + m + "\n";
    o += "  DD " + std::to_string(tries.size()) + "\n";
    o += tries.empty() ? "  DD 00H\n" : "  DD imagerel $tryMap$" + m + "\n";
    o += "  DD " + std::to_string(1 + msIpRows().size() + msFuncletRows().size()) + "\n";
    o += "  DD imagerel $ip2state$" + m + "\n";
    o += "  DD " + std::to_string(establisherOffset(msUnwindHelpSlot())) + "\n";
    o += "  DD 00H\n";                      // no exception specification
    o += "  DD 01H\n";                      // EHFlags: compiled with /EHsc

    o += "$stateUnwindMap$" + m;
    for (std::size_t k = 0; k < states.size(); k++) {
        o += (k == 0 ? " DD " : "  DD ");
        o += (states[k].toState < 0 ? std::string("0ffffffffH")
                                    : std::to_string(states[k].toState)) + "\n";
        o += states[k].action.empty() ? "  DD 00H\n"
                                      : "  DD imagerel " + states[k].action + "\n";
    }

    if (!tries.empty()) o += "$tryMap$" + m;
    for (std::size_t k = 0; k < tries.size(); k++) {
        o += (k == 0 ? " DD " : "  DD ") + std::to_string(tries[k].tryLow) + "\n";
        o += "  DD " + std::to_string(tries[k].tryHigh) + "\n";
        o += "  DD " + std::to_string(tries[k].catchHigh) + "\n";
        o += "  DD " + std::to_string(tries[k].handlers.size()) + "\n";
        o += "  DD imagerel $handlerMap$" + std::to_string(k) + "$" + m + "\n";
    }

    for (std::size_t k = 0; k < tries.size(); k++) {
        o += "$handlerMap$" + std::to_string(k) + "$" + m;
        for (std::size_t i = 0; i < tries[k].handlers.size(); i++) {
            const MsHandlerRow &h = tries[k].handlers[i];
            // 0x40 is HT_IsCatchAll, and a catch-all names no type; 0x08 is
            // HT_IsReference, which tells the runtime to put the object's
            // address in the slot rather than a copy of it. Measured.
            o += (i == 0 ? " DD " : "  DD ");
            o += h.descriptor.empty() ? "040H\n"
                                      : std::to_string((h.byReference ? 8 : 0) |
                                                       (h.constPointer ? 1 : 0)) + "\n";
            o += h.descriptor.empty() ? "  DD 00H\n"
                                      : "  DD imagerel " + h.descriptor + "\n";
            o += "  DD " + std::to_string(h.objectSlot == 0
                                              ? 0 : establisherOffset(h.objectSlot)) + "\n";
            o += "  DD imagerel " + h.funclet + "\n";
            // dispFrame: where the funclet saved the parent's frame pointer, from
            // its own establisher - [rsp+16] at entry, past `push rbp; sub rsp,32`.
            // Read only when an exception passes *through* a catch funclet. cl: 038H.
            o += "  DD 56\n";
        }
    }

    // Where each state begins, in address order; a funclet is wholly inside
    // its own handler state.
    o += "$ip2state$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o += "  DD 0ffffffffH\n";
    for (std::size_t k = 0; k < msIpRows().size(); k++) {
        o += "  DD imagerel " + masm_.labelName(msIpRows()[k].label) + "\n";
        o += "  DD " + (msIpRows()[k].state < 0 ? std::string("0ffffffffH")
                                                  : std::to_string(msIpRows()[k].state)) + "\n";
    }
    for (std::size_t k = 0; k < msFuncletRows().size(); k++) {
        const MsIpRow &r = msFuncletRows()[k];
        o += "  DD imagerel " + (r.funclet ? r.label : masm_.labelName(r.label)) + "\n";
        o += "  DD " + std::to_string(msFuncletRows()[k].state) + "\n";
    }
    o += ".xdata ENDS\n\n.CODE\n";
    out_ += o;
}

// **Where each RTTI or throw record begins.** For the project's assembler it is a COMDAT of its own
// and public, so the linker keeps one copy where every unit that named the type wrote one - the choice
// the COFF path makes in coffRecord. For ml64, which cannot say COMDAT, the records share one plain segment: `first` opens it, and the others follow in it.
std::string MasmCodeGen::record(const char *segment, int align, const std::string &name,
                                bool first, bool writable) {
    // ml64 refuses a segment reopened with another ALIGN (A2015), and .xdata$x is opened at 8 and at 4.
    if (!masm_.comdat() && std::string(segment) == ".xdata$x") align = 8;
    const std::string open = std::string(segment) + (writable ? " SEGMENT ALIGN(" : " SEGMENT READONLY ALIGN(") +
                             std::to_string(align) + ") 'DATA'";
    if (!masm_.comdat()) return first ? open + "\n" : std::string();
    masm_.globl(name);
    return (first ? std::string() : std::string(segment) + " ENDS\n") +
           open + " COMDAT(" + name + ")\n";
}

// **The five objects the Microsoft ABI wants before it will answer a
// `dynamic_cast`**, measured from clang.
void MasmCodeGen::emitClassRtti(const Program &program) {
    if (program.rtti.empty()) return;
    std::string &o = out_;

    // The chain of every class named, closed and deduplicated: a class's array
    // lists its bases' descriptors, so a base needs the full set of records
    // even when nothing in this file names it.
    std::vector<const Type *> all;
    for (std::size_t i = 0; i < program.rtti.size(); i++)
        for (const Type *k = program.rtti[i]; k != nullptr; k = k->base()) {
            bool had = false;
            for (std::size_t j = 0; j < all.size(); j++) if (all[j] == k) had = true;
            if (!had) all.push_back(k);
        }

    for (std::size_t i = 0; i < all.size(); i++) {
        MicrosoftRtti n;
        std::string why;
        if (!microsoftClassRttiNames(all[i], &n, &why)) continue;

        int contained = 0;
        for (const Type *k = all[i]->base(); k != nullptr; k = k->base())
            contained++;

        // **The descriptor is writable** - its spare word caches the undecorated name - so
        // `.data$r`, which the linker folds into .data: a segment named `.data` is MASM's
        // .DATA directive and stops the file. The four records below it are `.rdata$r`.
        o += record(".data$r", 16, n.descriptor, true, true);
        o += n.descriptor + " DQ ??_7type_info@@6B@\n";
        o += "  DQ 0\n";
        o += "  DB '" + n.decorated + "', 00H\n";
        o += ".data$r ENDS\n";
        msDescriptors_.insert(n.descriptor);

        // Where this class sits inside itself: at the top, never virtual. Those
        // four numbers are constant because a class with a second base is
        // refused - its first base is always at offset zero.
        o += record(".rdata$r", 4, n.baseDescriptor, true);
        o += n.baseDescriptor + " DD imagerel " + n.descriptor + "\n";
        o += "  DD 0" + std::to_string(contained) + "H\n";
        o += "  DD 00H\n";              // mdisp
        o += "  DD 0ffffffffH\n";       // pdisp - -1, there is no vbtable
        o += "  DD 00H\n";              // vdisp
        o += "  DD 040H\n";             // attributes
        o += "  DD imagerel " + n.hierarchy + "\n";

        o += record(".rdata$r", 4, n.array, false);
        o += n.array + " DD imagerel " + n.baseDescriptor + "\n";
        for (const Type *k = all[i]->base(); k != nullptr; k = k->base()) {
            MicrosoftRtti b;
            if (!microsoftClassRttiNames(k, &b, &why)) break;
            o += "  DD imagerel " + b.baseDescriptor + "\n";
        }
        o += "  DD 00H\n";

        o += record(".rdata$r", 4, n.hierarchy, false);
        o += n.hierarchy + " DD 00H\n";
        o += "  DD 00H\n";              // attributes - no MI, no virtual bases
        o += "  DD 0" + std::to_string(contained + 1) + "H\n";
        o += "  DD imagerel " + n.array + "\n";

        // The locator names itself, which is how the runtime recovers the image
        // base every other field is relative to.
        o += record(".rdata$r", 4, n.locator, false);
        o += n.locator + " DD 01H\n";
        o += "  DD 00H\n";              // the vfptr's offset in the object
        o += "  DD 00H\n";              // cdOffset
        o += "  DD imagerel " + n.descriptor + "\n";
        o += "  DD imagerel " + n.hierarchy + "\n";
        o += "  DD imagerel " + n.locator + "\n";
        o += ".rdata$r ENDS\n";
    }
}

void MasmCodeGen::emitThrowInfo(const Program &program) {
    if (program.msThrows.empty()) return;
    std::string &o = out_;
    std::set<std::string> catchables, arrays;
    for (std::size_t i = 0; i < program.msThrows.size(); i++) {
        const MicrosoftThrow &n = program.msThrows[i];
        for (std::size_t k = 0; k < n.catchables.size(); k++) {
            const MicrosoftThrow::Catchable &c = n.catchables[k];
            // **cl's listing writes `FLAT:` here and ml64 rejects it.** That prefix is
            // 32-bit MASM's way of naming a flat-model address; the 64-bit assembler
            // has no such keyword, so the listing records what cl means.
            if (msDescriptors_.insert(c.descriptor).second) {
                o += record(".data$r", 16, c.descriptor, true, true);
                o += c.descriptor + " DQ ??_7type_info@@6B@\n";
                o += "  DQ 0\n";
                o += "  DB '" + c.decorated + "', 00H\n";
                o += ".data$r ENDS\n";
            }
            if (!n.thrown || !catchables.insert(c.name).second) continue;
            o += record(".xdata$x", 8, c.name, true);
            o += c.name + (c.simple ? " DD 01H\n" : " DD 00H\n");
            o += "  DD imagerel " + c.descriptor + "\n";
            o += "  DD " + std::to_string(c.mdisp) + "\n";
            o += "  DD 0ffffffffH\n";
            o += "  DD 00H\n";
            o += "  DD " + std::to_string(c.size) + "\n";
            o += c.copyCtor.empty() ? "  DD 00H\n" : "  DD imagerel " + c.copyCtor + "\n";
            o += ".xdata$x ENDS\n";
        }
        if (!n.thrown) continue;
        // **One array per name.** `int *` and `const int *` thrown in one unit have two ThrowInfos -
        // the const is the info's - over one `_CTA2PEAH`, a throw's record dropping the pointee's
        // const; the second copy was an assembler error (1.28, 2026-10-22, review of 1.25, item 9).
        const bool arrayHere = arrays.insert(n.array).second;
        if (arrayHere) {
            o += record(".xdata$x", 4, n.array, true);
            o += n.array + " DD " + std::to_string(n.catchables.size()) + "\n";
            for (std::size_t k = 0; k < n.catchables.size(); k++)
                o += "  DD imagerel " + n.catchables[k].name + "\n";
        }
        // The info follows the array in its segment, or opens one of its own where the array was
        // written with an earlier info.
        o += record(".xdata$x", 4, n.info, !arrayHere);
        o += n.info + (n.isConst ? " DD 01H\n" : " DD 00H\n");
        o += n.destructor.empty() ? "  DD 00H\n" : "  DD imagerel " + n.destructor + "\n";
        o += "  DD 00H\n";
        o += "  DD imagerel " + n.array + "\n";
        o += ".xdata$x ENDS\n";
    }
}

// The ThrowInfo names are defined in this file, so the spelling must not put
// them in the EXTERN list it writes for everything a call mentions.
void MasmCodeGen::run(const Program &program) {
    std::vector<std::string> mine;
    for (std::size_t i = 0; i < program.msThrows.size(); i++) {
        const MicrosoftThrow &n = program.msThrows[i];
        if (n.thrown) mine.push_back(n.info);
        for (std::size_t k = 0; k < n.catchables.size(); k++)
            mine.push_back(n.catchables[k].descriptor);
    }
    // **And every class descriptor this file defines**, for the same reason: the spelling writes an
    // EXTERN for each name a call mentions, and these are mentioned by the `lea` in front of every
    // __RTDynamicCast while being laid down a few lines above it.
    for (std::size_t i = 0; i < program.rtti.size(); i++)
        for (const Type *k = program.rtti[i]; k != nullptr; k = k->base()) {
            MicrosoftRtti n;
            std::string why;
            if (!microsoftClassRttiNames(k, &n, &why)) break;
            // **All five, not the descriptor alone.** The descriptor is the
            // one a `lea` mentions, so it was the only one that had to be here
            // while only instructions recorded a reference.
            mine.push_back(n.descriptor);
            mine.push_back(n.baseDescriptor);
            mine.push_back(n.array);
            mine.push_back(n.hierarchy);
            mine.push_back(n.locator);
        }
    masm_.predefine(mine);
    // **Before the code, not after it.** The base run() flushes what it has
    // built when it finishes, so anything appended afterwards goes to a buffer
    // nobody reads.
    if (!program.msThrows.empty() || !program.rtti.empty())
        out_ += "\nEXTRN ??_7type_info@@6B@:QWORD\n";
    // **A pure virtual's slot names a routine no object file here defines.**
    // GNU as takes an undeclared symbol and leaves it to the linker; ml64 will
    // not, and answers `A2006: undefined symbol : _purecall`.
    for (std::size_t g = 0; g < program.globals.size(); g++) {
        bool found = false;
        const std::vector<GlobalPiece> &pieces = program.globals[g].init;
        for (std::size_t i = 0; i < pieces.size(); i++)
            if (pieces[i].symbol == "_purecall") { found = true; break; }
        if (found) { out_ += "\nEXTRN _purecall:PROC\n"; break; }
    }
    emitClassRtti(program);
    emitThrowInfo(program);
    X86_64Linux::run(program);
}

// **A funclet's head is written when it closes**, round the body lifted out of where it was written (M3a.4); the
// slice between the mark and the end is the body exactly, whichever door - the walker's or the holder's replay - wrote it.
void MasmSpelling::funcletBegin(const std::string &symbol, const std::string &entryLoc) {
    flushPending();                  // nothing pending inside the slice
    openFunclets_.push_back(OpenFunclet{o_.size(), symbol, entryLoc});
}

// An early exit: the continuation in rax, then the funclet's epilogue, which funcletEnd labels.
void MasmSpelling::funcletLeave(const std::string &label) {
    ins("lea", rip(label), reg("%rax"));
    ins("jmp", lbl("$LNleave$" + openFunclets_.back().symbol));
}

// The funclet's own frame: `rdx` is the establisher frame, so adding the parent's frame size back recovers the rbp
// every local was written against, and a funclet *returns* where to carry on, in rax. A cleanup passes no `resume`.
void MasmSpelling::funcletEnd(const std::string &resume) {
    const OpenFunclet open = openFunclets_.back();
    openFunclets_.pop_back();
    const std::string sym = open.symbol;
    // Written here as raw text, so said to be defined: an early exit's `jmp $LNleave$` made it an EXTERN otherwise.
    predefine({ sym, "$LNbeg$" + sym, "$LNpush$" + sym, "$LNprolog$" + sym, "$LNleave$" + sym, "$LNend$" + sym });
    // The text around the body: the head, then the body as written out, then the tail with the unwind data.
    std::string head, f;
    // **`.text$x`, and the dot is the whole of it** - the same trap as `.pdata`. A segment called `text` gets data
    // attributes, so the handler faults at its own first instruction; 'CODE' is what gives it execute permission.
    head += "\n.text$x SEGMENT ALIGN(16) 'CODE'" + associative() + "\n";
    head += sym + " PROC\n";
    head += "$LNbeg$" + sym + ":\n";
    // -g: the prologue is on the `catch` line, as cl's funclet begins (1.29).
    head += open.entryLoc;
    head += "  mov QWORD PTR [rsp+16], rdx\n";
    head += "  push rbp\n";
    head += "$LNpush$" + sym + ":\n";
    head += "  sub rsp, 32\n";
    head += "$LNprolog$" + sym + ":\n";
    // **rdx is the establisher frame, and that is now exactly the parent's rbp** - the two became one thing when the
    // frame pointer moved to the bottom of the allocation, so the handler reaches the parent's locals as they are.
    head += "  mov rbp, rdx\n";
    if (!resume.empty()) f += "  lea rax, " + labelName(resume) + "\n";
    f += "$LNleave$" + sym + ":\n";
    f += "  add rsp, 32\n";
    f += "  pop rbp\n";
    f += "  ret 0\n";
    f += "$LNend$" + sym + ":\n";
    f += sym + " ENDP\n";
    f += ".text$x ENDS\n";

    // A funclet carries unwind data of its own, naming the same handler and *the parent's* FuncInfo - the two share
    // one description of the try. Its entry is in `.pdata$x`, as the GNU spelling's is: Microsoft's link does not
    // sort .pdata, and `.text$x` lands after the CRT's code (M1).
    trailer_ += "\n.pdata$x SEGMENT READONLY ALIGN(4) 'DATA'" + associative() + "\n";
    trailer_ += "$pdata$" + sym + " DD imagerel $LNbeg$" + sym + "\n";
    trailer_ += "  DD imagerel $LNend$" + sym + "\n";
    trailer_ += "  DD imagerel $unwind$" + sym + "\n";
    trailer_ += ".pdata$x ENDS\n";
    f += ".xdata SEGMENT READONLY ALIGN(8) 'DATA'" + associative() + "\n";
    f += "$unwind$" + sym + " DB 019H\n";
    f += "  DB $LNprolog$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 02H\n";
    f += "  DB 00H\n";
    // UWOP_ALLOC_SMALL for 32 bytes: the code is 2 with (size/8 - 1) in the high nibble, so (32/8 - 1) = 3 gives
    // 032H. 042H would claim 40 and leave the runtime unwinding through this funclet to the wrong place.
    f += "  DB $LNprolog$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 032H\n";
    f += "  DB $LNpush$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 050H\n";
    f += "  DD imagerel __CxxFrameHandler3\n";
    f += "  DD imagerel $cppxdata$" + mangledName() + "\n";
    f += ".xdata ENDS\n";
    flushPending();
    const std::string body = o_.substr(open.mark);
    o_.resize(open.mark);
    funclets_ += head + body + f;
}
