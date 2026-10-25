// **The CodeView a debugger reads this object's code by**: `.debug$S` as ml64 /Zi and cl write it - S_OBJNAME,
// S_COMPILE3, a PROC's S_GPROC32/S_FRAMEPROC/S_END, a line table per PROC and per run of code outside one, the file
// checksums and the string table; a COMDAT PROC's in a `.debug$S` of its own, associative. doc-cxx3/M4-PLAN.md has the layout.
#include "AsmUnit.h"
#include <cstdlib>
#include <map>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>

namespace masm {
#endif

typedef std::vector<unsigned char> Bytes;

namespace {

enum { SUB_SYMBOLS = 0xF1, SUB_LINES = 0xF2, SUB_STRINGS = 0xF3, SUB_CHECKSUMS = 0xF4 };
enum { S_END = 0x0006, S_FRAMEPROC = 0x1012, S_OBJNAME = 0x1101, S_LPROC32 = 0x110F, S_GPROC32 = 0x1110,
       S_COMPILE3 = 0x113C };

void put8(Bytes &b, unsigned v) { b.push_back((unsigned char)v); }
void put16(Bytes &b, unsigned v) { put8(b, v & 0xFF); put8(b, (v >> 8) & 0xFF); }
void put32(Bytes &b, unsigned long v) { put16(b, (unsigned)(v & 0xFFFF)); put16(b, (unsigned)((v >> 16) & 0xFFFF)); }
void text(Bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); put8(b, 0); }
void pad4(Bytes &b) { while (b.size() % 4) put8(b, 0); }

void reloc(std::vector<Reloc> &r, size_t at, int sym, RelKind kind, long long addend)
{
    Reloc x;
    x.offset = (unsigned long)at;
    x.symbol = sym;
    x.kind = kind;
    x.addend = addend;
    r.push_back(x);
}

// a symbol record: its length (the kind and the body, padded to four), its kind, its body
void record(Bytes &syms, unsigned kind, const Bytes &body)
{
    Bytes r = body;
    pad4(r);
    put16(syms, (unsigned)r.size() + 2);
    put16(syms, kind);
    syms.insert(syms.end(), r.begin(), r.end());
}

// a subsection after what the section holds; its data's relocations moved to the section's offsets
void subsection(Section &s, unsigned kind, const Bytes &data, const std::vector<Reloc> &relocs)
{
    pad4(s.bytes);
    const size_t base = s.bytes.size() + 8;
    for (size_t i = 0; i < relocs.size(); i++) {
        Reloc r = relocs[i];
        r.offset += (unsigned long)base;
        s.relocs.push_back(r);
    }
    put32(s.bytes, kind);
    put32(s.bytes, (unsigned long)data.size());
    s.bytes.insert(s.bytes.end(), data.begin(), data.end());
    pad4(s.bytes);
}

std::string absolute(const std::string &path)
{
#ifdef _WIN32
    char buf[4096];
    if (_fullpath(buf, path.c_str(), sizeof buf)) return buf;
    return path;
#else
    if (!path.empty() && path[0] == '/') return path;
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) return path;
    return std::string(buf) + "/" + path;
#endif
}

// the language S_COMPILE3 names, which is how a debugger evaluates an expression: the source's by its extension -
// C++, or C for C and any other a compiler names (Shalimar's variables read as C's) - and MASM for a file that
// names none
unsigned language(const Unit &u)
{
    for (size_t i = 1; i < u.files.size(); i++) {
        if (u.files[i].empty()) continue;
        const std::string &f = u.files[i];
        size_t dot = f.find_last_of('.');
        std::string ext = dot == std::string::npos ? std::string() : upper(f.substr(dot + 1));
        return ext == "CPP" || ext == "CXX" || ext == "CC" || ext == "C++" || ext == "HPP" || ext == "HXX" ? 1 : 0;
    }
    return 3;
}

void module_symbols(Bytes &syms, const std::string &obj, unsigned lang)
{
    Bytes b;
    put32(b, 0);   // the signature of a precompiled types object: none
    text(b, obj);
    record(syms, S_OBJNAME, b);
    b.clear();
    put32(b, lang);
    put16(b, 0xD0);   // CV_CFL_X64
    for (int k = 0; k < 8; k++) put16(b, k == 0 || k == 4 ? 1 : 0);   // front end 1.0.0.0, back end 1.0.0.0
    text(b, "GRIDE masm");
    record(syms, S_COMPILE3, b);
}

// S_GPROC32 or S_LPROC32, S_FRAMEPROC and S_END of one PROC; the relocations at offsets in syms
void proc_symbols(const Unit &u, const ProcInfo &p, Bytes &syms, std::vector<Reloc> &relocs)
{
    const Symbol &s = u.symbols[p.symbol];
    const unsigned long len = p.end - p.start;
    Bytes b;
    put32(b, 0); put32(b, 0); put32(b, 0);   // parent, end, next: the linker fills them in
    put32(b, len);
    put32(b, p.prolog);   // where the debugger stops on entering: after the prologue
    put32(b, len);
    put32(b, 0);   // no type: T_NOTYPE, as cppparse writes it
    reloc(relocs, syms.size() + 4 + b.size(), p.symbol, R_SECREL, 0);
    put32(b, 0);
    reloc(relocs, syms.size() + 4 + b.size(), p.symbol, R_SECTION, 0);
    put16(b, 0);
    put8(b, 0);
    text(b, s.name);
    record(syms, s.bind == B_GLOBAL ? S_GPROC32 : S_LPROC32, b);

    // the frame the unwind directives describe; locals and parameters are reached off the .SETFRAME register -
    // encoded 2 for RBP, 3 for R13 - or off RSP (1)
    const unsigned base = p.frame_reg == 5 ? 2 : p.frame_reg == 13 ? 3 : 1;
    b.clear();
    put32(b, p.alloc);
    put32(b, 0); put32(b, 0);   // no padding
    put32(b, (unsigned long)p.pushes * 8);
    put32(b, 0); put16(b, 0);   // no exception handler
    put32(b, (unsigned long)(base << 14 | base << 16));
    record(syms, S_FRAMEPROC, b);
    record(syms, S_END, Bytes());
}

// the files the lines name, in the order first named: the offset of each in the checksum table
struct Files {
    Bytes strings, checksums;
    std::map<int, unsigned long> offset;
    Files() { put8(strings, 0); }
    unsigned long entry(int file, const std::string &path)
    {
        std::map<int, unsigned long>::const_iterator it = offset.find(file);
        if (it != offset.end()) return it->second;
        const unsigned long at = (unsigned long)checksums.size();
        put32(checksums, (unsigned long)strings.size());
        text(strings, path);
        put8(checksums, 0);   // no checksum: its length 0, its kind 0
        put8(checksums, 0);
        pad4(checksums);
        offset[file] = at;
        return at;
    }
};

// one DEBUG_S_LINES: the code from `start` to `end` of a section, named by `sym` (a symbol, or the section's own as
// -(n + 1)) at `in_place` from it, and its entries in offset order
Bytes line_table(std::vector<Reloc> &relocs, int sym, unsigned long in_place, unsigned long start, unsigned long end,
                 const std::vector<LineEntry> &entries, Files &files, const std::vector<std::string> &paths)
{
    Bytes d;
    reloc(relocs, 0, sym, R_SECREL, (long long)in_place);
    put32(d, in_place);
    reloc(relocs, 4, sym, R_SECTION, 0);
    put16(d, 0);
    put16(d, 0);   // no columns
    put32(d, end - start);
    for (size_t i = 0; i < entries.size(); ) {
        size_t j = i;
        while (j < entries.size() && entries[j].file == entries[i].file) j++;
        put32(d, files.entry(entries[i].file, paths[(size_t)entries[i].file]));
        put32(d, (unsigned long)(j - i));
        put32(d, (unsigned long)(12 + 8 * (j - i)));
        for (size_t k = i; k < j; k++) {
            put32(d, entries[k].offset - start);
            put32(d, ((unsigned long)entries[k].line & 0xFFFFFFUL) | 0x80000000UL);   // a statement
        }
        i = j;
    }
    return d;
}

Section debug_section(int comdat)
{
    Section s;
    s.name = ".debug$S";
    s.code = false;
    s.bss = false;
    s.readonly = true;
    s.info = false;
    s.debug = true;
    s.align = 1;
    s.comdat = comdat;
    s.associative = comdat >= 0;
    put32(s.bytes, 4);   // CV_SIGNATURE_C13
    return s;
}

}   // namespace

bool codeview(Unit &u, const std::string &asm_path, const std::string &obj_path, std::string &err)
{
    bool named = false;
    for (size_t i = 1; i < u.files.size(); i++) if (!u.files[i].empty()) named = true;
    if (!u.debug && !named)
        return true;
    (void)err;

    // the paths the line tables name: the .asm itself as 0, then each .FILE
    std::vector<std::string> paths(u.files.size() < 1 ? 1 : u.files.size());
    paths[0] = absolute(asm_path);
    for (size_t i = 1; i < u.files.size(); i++) if (!u.files[i].empty()) paths[i] = absolute(u.files[i]);

    // with source lines named, those alone describe the code
    bool source = false;
    for (size_t i = 0; i < u.lines.size(); i++) if (u.lines[i].file > 0) source = true;
    std::vector<LineEntry> lines;
    for (size_t i = 0; i < u.lines.size(); i++)
        if (!source || u.lines[i].file > 0) lines.push_back(u.lines[i]);

    // where the plain sections' CodeView goes: the source's own .debug$S, or one of this file's
    int own = -1;
    for (size_t i = 0; i < u.sections.size(); i++)
        if (u.sections[i].debug && u.sections[i].name == ".debug$S" && u.sections[i].comdat < 0) { own = (int)i; break; }
    const size_t nsec = u.sections.size();
    Section main_sec = debug_section(-1);
    Bytes main_syms;
    std::vector<Reloc> main_sym_relocs;
    if (own < 0) module_symbols(main_syms, absolute(obj_path), language(u));

    std::vector<std::pair<Bytes, std::vector<Reloc> > > main_lines;
    std::vector<Section> comdats;
    Files files;
    for (size_t sec = 0; sec < nsec; sec++) {
        const Section &code = u.sections[sec];
        if (!code.code) continue;
        std::vector<LineEntry> here;
        for (size_t i = 0; i < lines.size(); i++) if (lines[i].section == (int)sec) here.push_back(lines[i]);
        std::vector<const ProcInfo *> procs;
        for (size_t i = 0; i < u.procs.size(); i++) if (u.procs[i].section == (int)sec) procs.push_back(&u.procs[i]);
        if (here.empty() && procs.empty()) continue;

        Bytes syms;
        std::vector<Reloc> sym_relocs;
        std::vector<std::pair<Bytes, std::vector<Reloc> > > tables;
        for (size_t p = 0; p < procs.size(); p++) {
            const ProcInfo &pr = *procs[p];
            if (own < 0) proc_symbols(u, pr, code.comdat >= 0 ? syms : main_syms, code.comdat >= 0 ? sym_relocs : main_sym_relocs);
            // its entries; one that starts before it and runs into it is carried to its start
            std::vector<LineEntry> in;
            const LineEntry *before = 0;
            for (size_t i = 0; i < here.size(); i++) {
                if (here[i].offset < pr.start) before = &here[i];
                else if (here[i].offset < pr.end) in.push_back(here[i]);
            }
            if (before && (in.empty() || in[0].offset > pr.start) && pr.end > pr.start) {
                LineEntry e = *before;
                e.offset = pr.start;
                in.insert(in.begin(), e);
            }
            if (in.empty()) continue;
            std::vector<Reloc> r;
            Bytes t = line_table(r, pr.symbol, 0, pr.start, pr.end, in, files, paths);
            tables.push_back(std::make_pair(t, r));
        }
        // code outside every PROC: a run of entries to the next PROC or the section's end, named from the section's
        // own symbol
        for (size_t i = 0; i < here.size(); ) {
            bool inside = false;
            for (size_t p = 0; p < procs.size() && !inside; p++)
                inside = here[i].offset >= procs[p]->start && here[i].offset < procs[p]->end;
            if (inside) { i++; continue; }
            unsigned long end = (unsigned long)code.bytes.size();
            for (size_t p = 0; p < procs.size(); p++)
                if (procs[p]->start > here[i].offset && procs[p]->start < end) end = procs[p]->start;
            std::vector<LineEntry> run;
            size_t j = i;
            while (j < here.size() && here[j].offset < end) run.push_back(here[j++]);
            std::vector<Reloc> r;
            Bytes t = line_table(r, -(int)sec - 1, run[0].offset, run[0].offset, end, run, files, paths);
            tables.push_back(std::make_pair(t, r));
            i = j;
        }

        if (code.comdat < 0) {
            main_lines.insert(main_lines.end(), tables.begin(), tables.end());
            continue;
        }
        Section d = debug_section(code.comdat);
        if (!syms.empty()) subsection(d, SUB_SYMBOLS, syms, sym_relocs);
        for (size_t t = 0; t < tables.size(); t++) subsection(d, SUB_LINES, tables[t].first, tables[t].second);
        comdats.push_back(d);
    }

    Section &target = own >= 0 ? u.sections[(size_t)own] : main_sec;
    if (!main_syms.empty()) subsection(target, SUB_SYMBOLS, main_syms, main_sym_relocs);
    for (size_t t = 0; t < main_lines.size(); t++) subsection(target, SUB_LINES, main_lines[t].first, main_lines[t].second);
    if (!files.checksums.empty()) {
        subsection(target, SUB_CHECKSUMS, files.checksums, std::vector<Reloc>());
        subsection(target, SUB_STRINGS, files.strings, std::vector<Reloc>());
    }
    if (own < 0) u.sections.push_back(main_sec);
    u.sections.insert(u.sections.end(), comdats.begin(), comdats.end());
    return true;
}

}
