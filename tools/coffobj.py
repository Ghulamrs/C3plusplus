"""A reader for the x86-64 COFF objects cxx3 writes - what the structural Windows checks share.

`tools/win-unwind-check` and `tools/win-layout-check` both need the same four things of an object: its
sections with their bytes and COMDAT marks, its symbols, its relocations, and a relocation resolved to
the place it names. They are here once, in the shape the PE/COFF specification gives them, with no
dependence on an external dumper: llvm-objdump prints names and not resolved places, and a check that
measures where a table entry lands has to resolve the relocation itself.

    import coffobj; obj = coffobj.Coff(path)

Everything is a plain attribute; nothing here judges the object.
"""
import struct

IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SCN_LNK_COMDAT = 0x1000
IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000

# IMAGE_REL_AMD64_*, the kinds cxx3 and clang write into a Windows object.
REL_ABSOLUTE, REL_ADDR64, REL_ADDR32, REL_ADDR32NB, REL_REL32 = 0, 1, 2, 3, 4
REL_SECTION, REL_SECREL = 0xA, 0xB
REL_NAMES = {0: "ABSOLUTE", 1: "ADDR64", 2: "ADDR32", 3: "ADDR32NB", 4: "REL32", 5: "REL32_1",
             6: "REL32_2", 7: "REL32_3", 8: "REL32_4", 9: "REL32_5", 0xA: "SECTION", 0xB: "SECREL",
             0xC: "SECREL7", 0xD: "TOKEN", 0xE: "SREL32", 0xF: "PAIR", 0x10: "SSPAN32"}

IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC, IMAGE_SYM_CLASS_LABEL = 2, 3, 6
IMAGE_SYM_DTYPE_FUNCTION = 0x20


class Reloc(object):
    __slots__ = ("offset", "symbol", "type")

    def __init__(self, offset, symbol, type_):
        self.offset, self.symbol, self.type = offset, symbol, type_


class Section(object):
    def __init__(self, index, name, vsize, size, data, flags):
        self.index = index          # 1-based, as a symbol's section number counts
        self.name = name
        self.vsize = vsize
        self.size = size
        self.data = data
        self.flags = flags
        self.relocs = []            # sorted by offset once read
        self.comdat_selection = 0   # from the section symbol's aux record, 0 if none
        self.comdat_assoc = 0
        self.symbol = None          # the section symbol, when there is one

    @property
    def is_code(self):
        return bool(self.flags & IMAGE_SCN_CNT_CODE)

    @property
    def is_comdat(self):
        return bool(self.flags & IMAGE_SCN_LNK_COMDAT)

    @property
    def base_name(self):
        """`.pdata$x` and `.pdata` are one section to the linker: the part before the `$`."""
        return self.name.split("$", 1)[0]

    @property
    def align(self):
        a = (self.flags >> 20) & 0xF
        return 1 << (a - 1) if a else 0

    def reloc_at(self, offset):
        for r in self.relocs:
            if r.offset == offset:
                return r
        return None

    def u8(self, off):
        return self.data[off]

    def u16(self, off):
        return struct.unpack_from("<H", self.data, off)[0]

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def i32(self, off):
        return struct.unpack_from("<i", self.data, off)[0]

    def u64(self, off):
        return struct.unpack_from("<Q", self.data, off)[0]


class Symbol(object):
    __slots__ = ("index", "name", "value", "section", "type", "scl", "aux")

    def __init__(self, index, name, value, section, type_, scl, aux):
        self.index, self.name, self.value = index, name, value
        self.section, self.type, self.scl, self.aux = section, type_, scl, aux

    @property
    def defined(self):
        return self.section > 0

    @property
    def is_function(self):
        return (self.type >> 4) == 2   # IMAGE_SYM_DTYPE_FUNCTION in the complex-type nibble


class Place(object):
    """Where a relocation lands: a section and an offset in it, or a name this object does not define."""
    __slots__ = ("section", "offset", "name", "symbol")

    def __init__(self, section, offset, name, symbol):
        self.section, self.offset, self.name, self.symbol = section, offset, name, symbol

    @property
    def defined(self):
        return self.section is not None

    def __repr__(self):
        if self.section is None:
            return "<extern %s>" % self.name
        return "%s+0x%x (%s)" % (self.section.name, self.offset, self.name)


class Coff(object):
    def __init__(self, path):
        with open(path, "rb") as f:
            self.raw = f.read()
        self.path = path
        (machine, nsections, _stamp, symtab, nsyms, optsize, _chars) = struct.unpack_from("<HHIIIHH", self.raw, 0)
        if machine != IMAGE_FILE_MACHINE_AMD64:
            raise ValueError("%s: not an x86-64 COFF object (machine 0x%x)" % (path, machine))
        self.strings_at = symtab + 18 * nsyms
        self.sections = []
        at = 20 + optsize
        for i in range(nsections):
            (name, vsize, _vaddr, size, rawptr, relptr, _lnptr, nrel, _nln, flags) = struct.unpack_from("<8sIIIIIIHHI", self.raw, at)
            at += 40
            name = self._section_name(name)
            data = self.raw[rawptr:rawptr + size] if rawptr else b"\0" * size
            s = Section(i + 1, name, vsize, size, data, flags)
            s.rawptr, s.relptr = rawptr, relptr      # where the bytes and the relocations sit in the file
            if flags & IMAGE_SCN_LNK_NRELOC_OVFL and nrel == 0xFFFF:
                nrel = struct.unpack_from("<I", self.raw, relptr)[0] - 1
                relptr += 10
            for k in range(nrel):
                off, sym, type_ = struct.unpack_from("<IIH", self.raw, relptr + 10 * k)
                s.relocs.append(Reloc(off, sym, type_))
            s.relocs.sort(key=lambda r: r.offset)
            self.sections.append(s)
        self.symbols = []
        i = 0
        while i < nsyms:
            rec = self.raw[symtab + 18 * i:symtab + 18 * (i + 1)]
            name = self._symbol_name(rec[:8])
            value, section, type_, scl, naux = struct.unpack_from("<IhHBB", rec, 8)
            aux = self.raw[symtab + 18 * (i + 1):symtab + 18 * (i + 1 + naux)]
            sym = Symbol(i, name, value, section, type_, scl, aux)
            self.symbols.append(sym)
            for _ in range(naux):
                self.symbols.append(None)
            if scl == IMAGE_SYM_CLASS_STATIC and 0 < section <= len(self.sections) and naux and value == 0 \
                    and name == self.sections[section - 1].name:
                sec = self.sections[section - 1]
                if sec.symbol is None:
                    sec.symbol = sym
                    _len, _nrel, _nln, _csum, assoc, sel = struct.unpack_from("<IHHIHB", aux, 0)
                    sec.comdat_assoc, sec.comdat_selection = assoc, sel
            i += 1 + naux
        self.defined = {}       # name -> [Symbol], every defined symbol of the name
        self.externs = {}       # name -> Symbol, the undefined externals
        for s in self.symbols:
            if s is None or s.scl not in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC, IMAGE_SYM_CLASS_LABEL):
                continue
            if s.section > 0:
                if s.scl == IMAGE_SYM_CLASS_STATIC and s.value == 0 and s.name == self.sections[s.section - 1].name:
                    continue        # a section symbol is not a name a program has
                self.defined.setdefault(s.name, []).append(s)
            elif s.section == 0 and s.scl == IMAGE_SYM_CLASS_EXTERNAL:
                self.externs[s.name] = s

    def _section_name(self, raw):
        name = raw.rstrip(b"\0")
        if name.startswith(b"/"):
            return self._string(int(name[1:]))
        return name.decode("latin-1")

    def _symbol_name(self, raw):
        if raw[:4] == b"\0\0\0\0":
            return self._string(struct.unpack_from("<I", raw, 4)[0])
        return raw.rstrip(b"\0").decode("latin-1")

    def _string(self, off):
        at = self.strings_at + off
        end = self.raw.index(b"\0", at)
        return self.raw[at:end].decode("latin-1")

    def section_of(self, sym):
        return self.sections[sym.section - 1] if sym and sym.section > 0 else None

    def one(self, name):
        """The one defined symbol of a name, or None where there is none or several."""
        syms = self.defined.get(name)
        return syms[0] if syms and len(syms) == 1 else None

    def resolve(self, section, reloc):
        """Where a relocation lands: the symbol's place plus the field's own value as the addend."""
        sym = self.symbols[reloc.symbol]
        if reloc.type == REL_ADDR64:
            addend = section.u64(reloc.offset)
        elif reloc.type in (REL_SECTION,):
            addend = 0
        elif reloc.type == REL_SECREL:
            addend = section.u32(reloc.offset)
        else:
            addend = section.u32(reloc.offset)
        if sym.section > 0:
            return Place(self.sections[sym.section - 1], sym.value + addend, sym.name, sym)
        return Place(None, addend, sym.name, sym)

    def place_at(self, section, offset, want=None):
        """The relocation at an offset resolved, or None where the field holds no relocation."""
        r = section.reloc_at(offset)
        if r is None:
            return None
        if want is not None and r.type != want:
            return None
        return self.resolve(section, r)

    def symbols_in(self, section):
        """The defined symbols of a section, by offset: what a disassembler would label."""
        out = [s for syms in self.defined.values() for s in syms if s.section == section.index]
        out.sort(key=lambda s: (s.value, s.name))
        return out
