#include "AsmUnit.h"
#include <cstdio>

namespace masm {

Unit::Unit() : line(0), pass(0), current(-1), debug(false), src_file(0), src_line(0), src_again(false)
{
}

// a new pass over the source: the sections, fixups and errors start again; the symbols keep their definitions from
// the previous pass (so a forward reference is known) and remember where they were, for moved()
void Unit::begin_pass(int n)
{
    pass = n;
    prev_sizes.clear();
    for (size_t i = 0; i < sections.size(); i++)
        prev_sizes.push_back((unsigned long)sections[i].bytes.size());
    sections.clear();
    current = -1;
    // ml64 opens .text$mn and then .data before it reads a line, empty or not, and every other section takes its
    // number from first use - the order dumpbin shows
    section(".text$mn", true, false, false, 16);
    section(".data", false, false, false, 16);
    current = -1;
    fixups.clear();
    errors.clear();
    files.clear();
    lines.clear();
    procs.clear();
    src_file = 0;
    src_line = 0;
    src_again = false;
    line = 0;
    for (size_t i = 0; i < symbols.size(); i++) {
        symbols[i].prev_section = symbols[i].section;
        symbols[i].prev_value = symbols[i].value;
    }
}

// did any section's size or any symbol's place change since the previous pass?
bool Unit::moved() const
{
    if (sections.size() != prev_sizes.size())
        return true;
    for (size_t i = 0; i < sections.size(); i++)
        if (sections[i].bytes.size() != prev_sizes[i])
            return true;
    for (size_t i = 0; i < symbols.size(); i++) {
        const Symbol &s = symbols[i];
        if (s.defined && (s.value != s.prev_value || s.section != s.prev_section))
            return true;
    }
    return false;
}

void Unit::error(const std::string &msg)
{
    char buf[32];
    snprintf(buf, sizeof buf, "line %d: ", line);
    errors.push_back(buf + msg);
}

int Unit::section(const std::string &name, bool code, bool bss, bool readonly, int align,
                  int comdat, bool associative)
{
    // a plain section is one per name; a COMDAT is one per key, however many share the name
    for (size_t i = 0; i < sections.size(); i++)
        if (sections[i].name == name && sections[i].comdat == comdat &&
            sections[i].associative == associative) {
            current = (int)i;
            return current;
        }
    Section s;
    s.name = name;
    s.code = code;
    s.bss = bss;
    s.readonly = readonly;
    s.info = false;
    s.debug = false;
    s.align = align;
    s.comdat = comdat;
    s.associative = associative;
    sections.push_back(s);
    current = (int)sections.size() - 1;
    return current;
}

Section *Unit::cur()
{
    if (current < 0) {
        error("no section open (use .CODE or .DATA)");
        return 0;
    }
    return &sections[current];
}

unsigned long Unit::here()
{
    return current < 0 ? 0 : (unsigned long)sections[current].bytes.size();
}

int Unit::find(const std::string &name) const
{
    std::unordered_map<std::string, int>::const_iterator i = symbol_index.find(name);
    return i == symbol_index.end() ? -1 : i->second;
}

int Unit::ref(const std::string &name)
{
    int i = find(name);
    if (i >= 0)
        return i;
    Symbol s;
    s.name = name;
    s.bind = B_LOCAL;
    s.defined = false;
    s.function = false;
    s.type = SYM_UNTYPED;
    s.section = -1;
    s.value = 0;
    s.line = line;
    s.pass = 0;
    s.prev_section = -1;
    s.prev_value = 0;
    s.common = false;
    s.externdef = false;
    symbol_index[name] = (int)symbols.size();
    symbols.push_back(s);
    return (int)symbols.size() - 1;
}

// an anonymous defined label for $ used as an address
int Unit::location(int sec, long long off)
{
    char buf[48];
    snprintf(buf, sizeof buf, "\001%d_%lld", sec, off);
    int i = ref(buf);
    Symbol &s = symbols[i];
    s.defined = true;
    s.type = SYM_NEAR;
    s.section = sec;
    s.value = off;
    s.pass = pass;
    return i;
}

bool Unit::define(const std::string &name, int type)
{
    if (!cur())
        return false;
    int i = ref(name);
    Symbol &s = symbols[i];
    if ((s.defined && s.pass == pass) || s.bind == B_EXTERN || (s.bind == B_CONST && s.pass == pass)) {
        error("'" + name + "' is already defined");
        return false;
    }
    if (s.bind == B_CONST) s.bind = B_LOCAL;
    s.defined = true;
    s.type = type;
    s.section = current;
    s.value = (long long)here();
    s.line = line;
    s.pass = pass;
    return true;
}

bool Unit::constant(const std::string &name, long long v)
{
    int i = ref(name);
    Symbol &s = symbols[i];
    if ((s.defined && s.pass == pass) || s.bind == B_EXTERN) {
        error("'" + name + "' is already defined");
        return false;
    }
    s.bind = B_CONST;
    s.defined = true;
    s.value = v;
    s.pass = pass;
    return true;
}

void Unit::emit8(unsigned v)
{
    Section *s = cur();
    if (s)
        s->bytes.push_back((unsigned char)(v & 0xFF));
}

void Unit::emit16(unsigned v)
{
    emit8(v);
    emit8(v >> 8);
}

void Unit::emit32(unsigned long v)
{
    emit16((unsigned)(v & 0xFFFF));
    emit16((unsigned)((v >> 16) & 0xFFFF));
}

void Unit::emit64(unsigned long long v)
{
    emit32((unsigned long)(v & 0xFFFFFFFFull));
    emit32((unsigned long)(v >> 32));
}

void Unit::fixup(unsigned long at, int sym, RelKind kind, long long addend)
{
    Fixup f;
    f.section = current;
    f.at = at;
    f.symbol = sym;
    f.sub = -1;
    f.width = 4;
    f.kind = kind;
    f.addend = addend;
    f.branch = false;
    f.line = line;
    fixups.push_back(f);
}

// a label difference to be written once both labels are known
void Unit::difference(unsigned long at, int sym, int sub, int width)
{
    fixup(at, sym, R_DIFF);
    fixups.back().sub = sub;
    fixups.back().width = width;
}

// After a statement that began at `at` of section `sec`: code put there starts a line entry for the position it came
// from - the .LINE in force, else this line of the .asm - unless the entry before it in that section says the same
// and no .LINE came since (a compiler writes a line again where a one-line function's body starts, after its prologue).
void Unit::note_line(int sec, unsigned long at)
{
    if (sec < 0 || sec != current || !sections[sec].code || sections[sec].bytes.size() <= at)
        return;
    LineEntry e;
    e.section = sec;
    e.offset = at;
    e.file = src_file;
    e.line = src_file > 0 ? src_line : line;
    const bool again = src_again && src_file > 0;
    src_again = false;
    for (size_t i = lines.size(); i-- > 0 && !again; ) {
        if (lines[i].section != sec) continue;
        if (lines[i].file == e.file && lines[i].line == e.line) return;
        break;
    }
    lines.push_back(e);
}

void Unit::resolve(const Target &t)
{
    for (size_t i = 0; i < symbols.size(); i++) {
        Symbol &s = symbols[i];
        // EXTERNDEF settles here: defined in this file, it is public; not, it is extern
        if (s.externdef && s.bind == B_LOCAL) s.bind = s.defined ? B_GLOBAL : B_EXTERN;
        if (!s.defined && s.bind != B_EXTERN) {
            line = s.line;
            error("'" + s.name + "' is not defined");
        }
    }
    for (size_t i = 0; i < fixups.size(); i++) {
        const Fixup &f = fixups[i];
        const Symbol &s = symbols[f.symbol];
        line = f.line;
        if (s.bind == B_CONST) {
            error("'" + s.name + "' is a constant, not an address");
            continue;
        }
        Section &sec = sections[f.section];
        if (f.kind == R_DIFF) {
            const Symbol &sub = symbols[f.sub];
            if (sub.bind == B_CONST) { error("'" + sub.name + "' is a constant, not an address"); continue; }
            if (!s.defined || !sub.defined) continue;
            if (s.section != sub.section) { error("labels in different sections cannot be subtracted"); continue; }
            long long d = s.value - sub.value;
            for (int b = 0; b < f.width; b++)
                d += (long long)sec.bytes[f.at + b] << (8 * b);
            long long lo = f.width == 8 ? 0 : -(1LL << (f.width * 8 - 1));
            long long hi = f.width == 8 ? 0 : (1LL << (f.width * 8)) - 1;
            if (f.width != 8 && (d < lo || d > hi)) { error("label difference does not fit"); continue; }
            for (int b = 0; b < f.width; b++)
                sec.bytes[f.at + b] = (unsigned char)((unsigned long long)d >> (8 * b));
            continue;
        }
        long long v;
        int width;
        if (t.resolve_here(*this, f, s, v, width)) {
            if (width == 1 && (v < -128 || v > 127)) {
                error("jump destination too far");
                continue;
            }
            for (int k = 0; k < width; k++)
                sec.bytes[f.at + k] = (unsigned char)((unsigned long long)v >> (8 * k));
            continue;
        }
        if (f.kind == R_REL8) {
            error("a short jump cannot leave its section");
            continue;
        }
        Reloc r;
        r.offset = f.at;
        r.symbol = f.symbol;
        r.kind = f.kind;
        r.addend = f.addend;
        sec.relocs.push_back(r);
    }
}

}
