#include "Coff.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace masm {

typedef std::vector<unsigned char> Bytes;

static void u8(Bytes &b, unsigned v) { b.push_back((unsigned char)v); }
static void u16(Bytes &b, unsigned v) { u8(b, v & 0xFF); u8(b, (v >> 8) & 0xFF); }
static void u32(Bytes &b, unsigned long v) { u16(b, v & 0xFFFF); u16(b, (v >> 16) & 0xFFFF); }

static void set32(Bytes &b, size_t at, unsigned long v)
{
    for (int i = 0; i < 4; i++)
        b[at + i] = (unsigned char)(v >> (8 * i));
}

static void name8(Bytes &b, const std::string &name, Bytes &strings)
{
    if (name.size() <= 8) {
        for (size_t i = 0; i < 8; i++)
            u8(b, i < name.size() ? (unsigned char)name[i] : 0);
        return;
    }
    u32(b, 0);
    u32(b, (unsigned long)(strings.size() + 4));
    for (size_t i = 0; i < name.size(); i++)
        u8(strings, (unsigned char)name[i]);
    u8(strings, 0);
}

// section header name: 8 bytes, or /offset into the string table when longer
static void section_name8(Bytes &b, const std::string &name, Bytes &strings)
{
    if (name.size() <= 8) {
        name8(b, name, strings);
        return;
    }
    char buf[16];
    snprintf(buf, sizeof buf, "/%u", (unsigned)(strings.size() + 4));
    for (size_t i = 0; i < 8; i++)
        u8(b, i < strlen(buf) ? (unsigned char)buf[i] : 0);
    for (size_t i = 0; i < name.size(); i++)
        u8(strings, (unsigned char)name[i]);
    u8(strings, 0);
}

// IMAGE_SCN_* flags as ml64 sets them: code 60000020, data C0000040, read-only 40000040, uninitialised C0000080,
// CodeView 42000040 (discardable), plus the alignment field
static unsigned long characteristics(const Section &s)
{
    if (s.info)
        return 0x00000A00UL;
    unsigned long c = s.code ? 0x60000020UL : s.bss ? 0xC0000080UL : s.debug ? 0x42000040UL
                    : s.readonly ? 0x40000040UL : 0xC0000040UL;
    int a = 0;
    while ((1 << a) < s.align)
        a++;
    return c | ((unsigned long)(a + 1) << 20);
}

static unsigned reloc_type(RelKind k)
{
    if (k == R_ADDR64) return 0x0001;
    if (k == R_ADDR32) return 0x0002;
    if (k == R_ADDR32NB) return 0x0003;
    if (k >= R_REL32_1 && k <= R_REL32_5) return 0x0005 + (k - R_REL32_1);
    if (k == R_SECTION) return 0x000A;
    if (k == R_SECREL) return 0x000B;
    return 0x0004;
}

bool CoffWriter::write(const Unit &u, const std::string &path, std::string &err)
{
    const size_t nsec = u.sections.size();
    std::vector<long> index(u.symbols.size(), -1);
    std::vector<bool> referenced(u.symbols.size(), false);
    for (size_t i = 0; i < nsec; i++)
        for (size_t r = 0; r < u.sections[i].relocs.size(); r++)
            if (u.sections[i].relocs[r].symbol >= 0)
                referenced[u.sections[i].relocs[r].symbol] = true;
    // a COMDAT's key symbol is always written, and right after its section's symbol, which is where the linker
    // looks for the symbol a COMDAT folds on; an ASSOCIATIVE section names the section its key's COMDAT opened
    std::vector<int> keyOf(nsec, -1), assocOf(nsec, -1);
    for (size_t i = 0; i < nsec; i++) {
        const Section &s = u.sections[i];
        if (s.comdat < 0) continue;
        if (s.associative) {
            for (size_t k = 0; k < nsec; k++)
                if (u.sections[k].comdat == s.comdat && !u.sections[k].associative) { assocOf[i] = (int)k; break; }
            if (assocOf[i] < 0) { err = "ASSOCIATIVE(" + u.symbols[s.comdat].name + "): no COMDAT section is keyed on it"; return false; }
        } else {
            const Symbol &key = u.symbols[s.comdat];
            if (!key.defined || key.section != (int)i) { err = "COMDAT(" + key.name + "): the symbol is not defined in that section"; return false; }
            keyOf[i] = s.comdat;
            referenced[s.comdat] = true;
        }
    }
    long count = 0;
    std::vector<long> section_symbol(nsec);
    for (size_t i = 0; i < nsec; i++) {
        section_symbol[i] = count;
        count += 2;
        if (keyOf[i] >= 0) index[keyOf[i]] = count++;
    }
    for (size_t i = 0; i < u.symbols.size(); i++) {
        const Symbol &s = u.symbols[i];
        if (index[i] >= 0)
            continue;
        if (s.bind == B_CONST || (!s.defined && s.bind != B_EXTERN))
            continue;
        // ml64 writes a Static symbol only when a relocation refers to it - a PROC always, a label or a data name
        // only if something outside its section points at it
        if (s.bind == B_LOCAL && !s.function && !referenced[i])
            continue;
        index[i] = count++;
    }

    Bytes out;
    Bytes strings;
    u16(out, 0x8664);
    u16(out, (unsigned)nsec);
    u32(out, 0);
    size_t symptr_at = out.size();
    u32(out, 0);
    u32(out, (unsigned long)count);
    u16(out, 0);
    u16(out, 0);

    std::vector<size_t> header_at(nsec);
    for (size_t i = 0; i < nsec; i++) {
        const Section &s = u.sections[i];
        header_at[i] = out.size();
        section_name8(out, s.name, strings);
        u32(out, 0);
        u32(out, 0);
        u32(out, (unsigned long)s.bytes.size());
        u32(out, 0);
        u32(out, 0);
        u32(out, 0);
        // past 65535 relocations the count goes in a leading entry (IMAGE_SCN_LNK_NRELOC_OVFL), the header saying
        // 0xFFFF - measured with ml64: 70,000 DQ label give 70001 there
        const bool overflow = s.relocs.size() >= 0xFFFF;
        u16(out, overflow ? 0xFFFF : (unsigned)s.relocs.size());
        u16(out, 0);
        u32(out, characteristics(s) | (overflow ? 0x01000000UL : 0) | (s.comdat >= 0 ? 0x00001000UL : 0));
    }

    for (size_t i = 0; i < nsec; i++) {
        const Section &s = u.sections[i];
        if (!s.bytes.empty() && !s.bss) {
            set32(out, header_at[i] + 20, (unsigned long)out.size());
            out.insert(out.end(), s.bytes.begin(), s.bytes.end());
        }
        if (!s.relocs.empty()) {
            set32(out, header_at[i] + 24, (unsigned long)out.size());
            if (s.relocs.size() >= 0xFFFF) {
                u32(out, (unsigned long)s.relocs.size() + 1);
                u32(out, 0);
                u16(out, 0);
            }
            for (size_t r = 0; r < s.relocs.size(); r++) {
                // a negative symbol is -(n + 1): section n's own symbol (CodeView's code outside a PROC)
                const int sym = s.relocs[r].symbol;
                u32(out, s.relocs[r].offset);
                u32(out, (unsigned long)(sym < 0 ? section_symbol[(size_t)(-sym - 1)] : index[sym]));
                u16(out, reloc_type(s.relocs[r].kind));
            }
        }
    }

    set32(out, symptr_at, (unsigned long)out.size());
    struct Sym {
        static void put(Bytes &o, Bytes &str, const Unit &uu, const Symbol &s) {
            name8(o, s.name, str);
            u32(o, s.defined || s.common ? (unsigned long)s.value : 0);
            u16(o, s.defined ? (unsigned)(s.section + 1) : 0);
            // an extern carries no type, whatever it was declared as; a label in a code section is class Label (6),
            // a data name or a private PROC Static (3)
            u16(o, s.function && s.defined ? 0x20 : 0);
            const bool codeLabel = s.defined && !s.function && s.type == SYM_NEAR &&
                                   uu.sections[s.section].code;
            u8(o, s.bind != B_LOCAL ? 2 : codeLabel ? 6 : 3);
            u8(o, 0);
        }
    };
    for (size_t i = 0; i < nsec; i++) {
        const Section &s = u.sections[i];
        name8(out, s.name, strings);
        u32(out, 0);
        u16(out, (unsigned)(i + 1));
        u16(out, 0);
        u8(out, 3);
        u8(out, 1);
        // the section definition: length, relocations, line numbers, checksum, and for a COMDAT the selection - 2
        // (ANY) folded on the key that follows, 5 (ASSOCIATIVE) with the number of the section it belongs to
        u32(out, (unsigned long)s.bytes.size());
        u16(out, s.relocs.size() >= 0xFFFF ? 0xFFFF : (unsigned)s.relocs.size());
        u16(out, 0);
        u32(out, 0);
        u16(out, assocOf[i] >= 0 ? (unsigned)(assocOf[i] + 1) : 0);
        u8(out, s.comdat < 0 ? 0 : s.associative ? 5 : 2);
        u8(out, 0);
        u16(out, 0);
        if (keyOf[i] >= 0) Sym::put(out, strings, u, u.symbols[keyOf[i]]);
    }
    for (size_t i = 0; i < u.symbols.size(); i++) {
        if (index[i] < 0)
            continue;
        bool isKey = false;
        for (size_t k = 0; k < nsec && !isKey; k++) isKey = keyOf[k] == (int)i;
        if (isKey) continue;
        Sym::put(out, strings, u, u.symbols[i]);
    }
    u32(out, (unsigned long)(strings.size() + 4));
    out.insert(out.end(), strings.begin(), strings.end());

    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) {
        err = "cannot create " + path;
        return false;
    }
    f.write((const char *)&out[0], (std::streamsize)out.size());
    if (!f) {
        err = "cannot write " + path;
        return false;
    }
    return true;
}

}
