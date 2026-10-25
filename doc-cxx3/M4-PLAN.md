# M4: the encoder library, `CoffEmitter`, masm re-based, `-S` = `-c`

Written 2026-10-25 by Fable 5.1, from `b8290cb` (branch `m3a` at M3a.2's step B, where `MInst` is fixed - review
point 2a). Design section 6 and R4.4 say what M4 is; section 12's M4 row says when it starts: once M3 has fixed the
`MInst` definition, in parallel with the rest of M3. Proposal P2 (section 6.1) was left under my review; it is
settled here, in "P2, settled". This note is in the shape of `M3-PLAN.md`: what a commit may change, how each step
is judged, and a "Where it stands" section appended to as steps land. The work runs unattended on the user's word
(2026-10-25); a decision that was mine to make is made here with its reason, and not asked.

## What M4 is

Section 12's row: *Encoder library, `CoffEmitter`, masm re-based, `-S` = `-c`.* Section 6: one encoder library
turns `MInst`s into bytes, with relaxation, relocations, `.pdata`/`.xdata`, COMDAT and CodeView, with two sinks -
text and a COFF object; the project's masm re-based on the same library; after the re-base the `-S`/`-c` test is
byte equality; masm and clang become optional on target 1.

## P2, settled: the library comes out of the project's masm, and it is all of masm but `main.cpp`

**Measured before deciding** (the files are named by their line counts at MASM `6bcb5d2`):

- `x64_encode.cpp` (791) encodes a structured `Operand` - register, immediate, memory with base, index, scale,
  symbol and relocation kind - into bytes with REX, ModRM, SIB and the relocation the displacement needs; it
  covers the integer set, the SSE scalar and packed forms cxx3 writes, `MOVSX`/`MOVZX`/`MOVSXD`, the string
  instructions, `SETcc`/`CMOVcc`/`Jcc`, and no x87. cxx3 writes no x87 for x86_64-windows (`long double` is 8
  bytes there, `X86_64Windows.cpp`), so none is needed.
- `unit.cpp` (309) is the sections, symbols, fixups and the multi-pass layout (`moved()`); `mscoff.cpp` (231)
  the COFF writer with COMDAT keys and associative sections; `codeview.cpp` (323) the `.debug$S` line tables,
  files and strings appended to what the compiler wrote.
- `x64_parse.cpp` (1,240), `lexer.cpp` (132) and `expr.cpp` (349) are the MASM statement layer: tokens,
  expressions with `IMAGEREL`/`OFFSET`/`SECTIONREL`/`SECTION`, operands with `PTR` widths, and the directives
  - `SEGMENT ... COMDAT(sym) ASSOCIATIVE(sym)`, `PROC`/`ENDP`, `DB`/`DW`/`DD`/`DQ`, `ALIGN`, `PUBLIC`/`EXTERN`,
  `.FILE`/`.LINE`, `OPTION`. `PROC FRAME`'s unwind writer is there too, and cxx3 does not use it.
- Every source compiles unchanged with cxx3's own flags, `clang++ -std=c++11 -Wall -Wextra -Werror -pedantic`
  (probed 2026-10-25); only `main.cpp` writes `new`.

**What cxx3's MASM spelling writes, read for this**: `MasmSpelling` renders instructions through one table
(`kRules`, 156 rows, `Masm.cpp:109`) and one renderer (`render`, `Masm.cpp:212`) - that half maps onto the
encoder's `Operand` directly. But about half of a Windows object is composed as **text** behind the spelling's
back: the unwind data and `.pdata`/`.xdata` (`functionEnd`, `Masm.cpp:483`), the funclet head and tail with the
body lifted out of the output as a string (`closeFunclet`, `Masm.cpp:950`, through `atOutput`), the FH3 tables
(`emitExceptionTables`, `Masm.cpp:1021`), the RTTI and throw records (`emitClassRtti`, `emitThrowInfo`), the
CodeView records converted from the GNU text (`codeView`, `Masm.cpp:619`), the TLS and initialiser entries, and
the preamble's `PUBLIC`/`EXTERN` lists. M3a.4 will turn the funclet text into directives; the rest has no
directive planned. So a `CoffEmitter` that "uses the library directly" and bypasses the statement layer, as P2's
text has it, would still need that layer for every one of those sites, or a second structured writer for each -
a rewrite of `Masm.cpp` that would collide with M3a.4 on `m3a`, which another agent owns this round.

**Decided**: P2 is taken, and the library is **all of masm but `main.cpp`** - the encoder, the unit and COFF
writer, the CodeView writer, *and* the MASM statement layer - moved into the compiler tree as `src/encoder/`,
under one namespace, with the comment cap applied. `CoffEmitter` is the compiler's door to it; its first form
(M4.2) assembles the spelling's own text in process, which is byte-equal with `-S` + masm by construction, since
the two are one assembler; its second form (M4.4) hands instructions, labels and data to the library as
`Operand`s and directive calls without text, and the text sites that remain are counted and named, which is the
measured list the funclet-directive work (M3a.4) and whatever follows it must empty. masm keeps `main.cpp` - the
command line and the jobs - over the library, on its `m4-rebase` branch. Section 6's "semantic first, then byte
equality" step is not needed, as P2 said. cppparse's `Coff.cpp` stays a cross-check and is not taken.

Two things this costs, said now: the compiler links a MASM text parser, which the design did not foresee and
which M4.4 reduces to the text sites it names; and the library's files take the compiler tree's names and
namespace, so masm's `m4-rebase` reads them from the compiler tree (`ENCODER=` in its Makefile) rather than
holding a copy - one source, as "the same library" asks.

## What a commit may change, and how it is judged

M4's commits change what is emitted **never**: `-S` output is untouched at every step, and the emit golden must
read 0 changed on all three Mac targets at every commit (arm64-darwin and tms6747 are not touched at all). The
Windows rows are owed this round - no Windows box is reachable (GRA unreachable, zeeshan withdrawn) - and every
row below that needs one is marked *owed* in "Where it stands", never claimed.

| gate | where | what must hold |
| --- | --- | --- |
| byte equality | Mac | for every case in `tests/cases` that compiles for x86_64-windows and the 16 Compiler++ units (`~/cxxopt-build/cpp/Compiler++`), at -O0 and -O2: `cxx3 -c -arch x86_64-windows` and `cxx3 -S -masm=masm` + the re-based masm give objects `cmp` reports equal; `tools/coff-equal.sh` is the script |
| the external masm | Mac | the re-based masm's own `make test` (`tests/enc.sh` against ml64's recorded bytes, `errors.asm`, `refuse/`, `deep.asm`, the jobs, `tests/cpp11.sh`) passes, and the objects it writes over that corpus are byte-identical to the pre-rebase masm's (`6bcb5d2`) |
| LINK | Mac | the project's LINK, built on the Mac, links the three objects of `examples/` (`demo`, `report`, `shapes`) from `cxx3 -c` without an error; running the image is **owed** (Windows) |
| `make test` | Mac | run, emit (golden 0 changed), names, overload, syntax, typed, adapted, exclusions 0/0, comment cap 0, memory gate held (the library brings no `new`; `main.cpp`'s stay in masm) |
| emit golden | Mac | 0 of 1,924 changed at every commit |
| g++ build | Linux (`~/m4-<tag>/`, one build at a time, deleted after) | `-Werror` build, `run.sh` at -O0 and -O2, for any commit touching shared code |
| determinism | Mac | `cxx3 -c` twice gives the same object; `tools/determinism.sh` as it stands |
| Windows | **owed** | `tools/windows/run-cases.cmd` with `-c` through CoffEmitter; link.exe linking CoffEmitter's objects; cdb on `-g` objects (M10 W1-W5); self-host with `-c` |

## The steps

| step | what | gate |
| --- | --- | --- |
| M4.0 | this plan; P2 settled above | - |
| M4.1 | **The library in the compiler tree.** masm's nine sources (all but `main.cpp`) become `src/encoder/` under `namespace masm`: `AsmUnit.h/.cpp` (asm.h, unit.cpp), `X64.h`, `X64Encode.cpp`, `X64Parse.cpp`, `AsmLexer.cpp`, `AsmExpr.cpp`, `Coff.h/.cpp` (mscoff), `AsmCodeView.cpp`, `Assembler.cpp` - renamed because `lexer.cpp`, `codeview.cpp` and `main.cpp` collide with cxx3's own basenames in `msvc/build.cmd`'s one flat object directory (CLAUDE.md, "A basename may not repeat"). Comments go to `//` under the three-line cap. `Assembler` takes its lines from memory as well as from a file, and owns its target and writer without `new`. The Makefile, `msvc/build.cmd` and `ide/generate.py` glob the directory | the tree builds under `-std=c++11 -Werror` on the Mac and with g++ on Linux; `make test` green; memory gate unchanged; comment cap 0; emit golden 0 changed (nothing calls it yet) |
| M4.2 | **`CoffEmitter`, the text door, and `-c` on every host.** `src/backend/CoffEmitter.{h,cpp}`: the spelling's MASM text assembled in process by the library into the object `-c` names; the driver takes it for x86_64-windows in the `masm` syntax wherever it spawned `masm.exe` (`assembleObjects`, and the per-file step of `link`), on every host - the "cannot assemble x86_64-windows code on this machine" refusal is lifted for that syntax and `-c`; `-masm=ml64` and `-masm=gnu` keep their external tools. `tools/coff-equal.sh` is the gate's script | byte equality over the corpus at -O0 and -O2 against masm `6bcb5d2` (the pre-rebase binary, which the re-base must then match too); the examples link with LINK; `make test`; emit golden 0; determinism |
| M4.3 | **masm re-based.** Branch `m4-rebase` of `Windows/MASM`: `src/` keeps `main.cpp`; `Makefile`, `CMakeLists.txt` and `masm.vcxproj` take the library from `ENCODER` (default `../../C3++/src/encoder`, the sibling checkout); its tests unchanged | masm's `make test` passes; its objects over its own corpus byte-identical to `6bcb5d2`'s; `cxx3 -c` against the re-based masm byte-equal over the corpus |
| M4.4 | **The structural door.** `CoffEmitter` as a sink the spelling hands `Operand`s to: an instruction's mnemonic through `kRules` and its operands from the `Op`s (`render`'s arithmetic, the frame displacement included) go to `X64Encode` without text; labels, sections, `ALIGN`, `DB/DW/DD/DQ` and `PUBLIC`/`EXTERN` as calls; the raw-text sites named above go through the statement layer as text and are **counted** - the count and the sites are the step's record | byte equality of this door's object against M4.2's over the whole corpus at -O0 and -O2; `make test`; emit golden 0 |
| M4.5 | **The close**: "Where it stands" with every row's number and box; the owed Windows rows listed; CLAUDE.md's `-masm` paragraph and the design's section 6 brought to what is true | - |

M4.4 is attempted only after M4.1-M4.3 are green and recorded; if the budget ends before it, the handover says so
and names the text sites as measured by M4.2's inventory.

## What must not change, and what looks wrong but is load-bearing

- **`-S` output does not move.** The text spelling stays the thing the golden and `identical.sh` measure; the
  object is derived from it (M4.2) or must equal what is derived from it (M4.4).
- **The relaxation is masm's pass loop**, not a fixed-point of its own: a jump is short where it was never found
  out of reach, forward targets measured from the previous pass's positions (`jump_width`, the shalimar note). Any
  other algorithm reaches the same least fixed point in theory and a different one the first time it does not;
  the library keeps the loop, and `CoffEmitter` runs it.
- **`S_OBJNAME` holds the object's absolute path** (`codeview.cpp`): two objects written under different names
  differ in that string under `-g`. The byte-equality gate writes both to the same path in turn.
- **masm opens `.text$mn` and `.data` before reading a line**, in that order, as ml64 does; a section's number is
  its first use. The structural door must open them the same way.
- **The segment-override byte is emitted before the instruction's own bytes** (`X64Target::instruction`), and
  `REP`/`LOCK` prefixes before the statement they qualify - the order the text parser sees them in.
- `OPTION PROC:PRIVATE` is the only reason a `static` function is not exported; `PUBLIC` lines name the rest.

## Where it stands

*(Appended as each step lands: the commit, the gate rows with their numbers on the box they were measured on,
what was found and not mended. Nothing above this line is edited; a correction is recorded below with its date.)*

**M4.1, the library** (2026-10-25, Fable 5.1, branch `m4`, `d02b7ad`). masm `6bcb5d2`'s nine sources less `main.cpp`
are `src/encoder/` under `namespace masm`: `AsmUnit.h/.cpp`, `Assembler.h/.cpp`, `AsmLexer.cpp`, `AsmExpr.cpp`,
`X64.h`, `X64Parse.cpp`, `X64Encode.cpp`, `Coff.h/.cpp`, `AsmCodeView.cpp` - renamed where a basename collided with
cxx3's (`lexer`, `codeview`, `main`) for `msvc/build.cmd`'s one object directory; block comments made `//` runs under
the cap, six of them shortened by hand (the long forms are below, "Notes the cap would not hold"); `Assembler` owns
its `X64Target` and `CoffWriter` by value - the two abstract interfaces had one implementation each - and reads text
from memory (`runText`) as well as a file; no `new` in the library. Globbed by the Makefile, `msvc/build.cmd` and
`ide/generate.py`, which now also lists `sema/` and `syntax/` - **the ide projects had neither since M2**, which is
RIDE's Windows build's to notice. Mac: builds `-std=c++11 -Werror`; comment cap 0; memory gate held at 1,063.

**M4.3, masm re-based** (2026-10-25, `Windows/MASM` branch `m4-rebase`, `920928c`; done before M4.2's gate because it
is the library's own test). `src/` keeps `main.cpp` over `masm::Assembler`; `ENCODER` in the Makefile, a CMake cache
variable and the `Encoder` MSBuild property name the library's directory, `../../C3++/src/encoder` by default. Mac,
against the build of `6bcb5d2`: masm's `make test` passes as before - `enc.sh` 23 of 23 identical to ml64's bytes,
`cpp11.sh` 4 of 4, `errors.asm`, `refuse/`, `deep.asm`, the eight jobs - and **the 48 objects its tests write are
byte-identical** (`cmpdirs` over `build/test`). The Xcode project (`masm.xcodeproj`) lists the old files and is
RIDE's `tools/make-projects.py`'s to rewrite.

**M4.2, `CoffEmitter`, the text door** (2026-10-25, `8e6a564`). `src/backend/CoffEmitter.{h,cpp}`: the spelling's
text assembled in process; a `Job` carries the object it writes; `assembleObjects` has nothing to run and `link()`'s
per-file step nothing to spawn for the MASM spelling; the "cannot assemble x86_64-windows code on this machine"
refusal is lifted for `-c` in that spelling, so the Mac and Linux write Windows objects; the ml64 fallback for a machine
with no masm.exe is gone with the need for one (`masmReachable` deleted, `-masm=ml64` stays by name). A refusal of the
compiler's own text names the library's line and leaves the text as the .s. `tools/coff-equal.sh` is the gate.
Mac, against masm `6bcb5d2`: **-O0 and -O2, 1,342 pairs, 1,302 objects compared, 0 differ** (40 pairs refused on both
sides - `.notarget`/`.nomasm` cases); **-g at -O0, 671 pairs, 626 objects, 0 differ**; against the re-based masm
`920928c` at -O0, 671 pairs, 651 objects, 0 differ; `-c` twice on one file, the same bytes. LINK, built on the Mac,
reads the three objects of `examples/` and resolves every symbol between them; **18 unresolved, every one the CRT's**
(`??2@YAPEAX_K@Z`, `__CxxFrameHandler3`, `fwrite`, `mainCRTStartup`, ...) - this machine has no libcmt, so the link
with the CRT and the run of `demo.exe` are **owed to the Windows box**. `make test` green, the emit golden recorded by
a build of `b8290cb` reading 0 of 1,930 changed.

**M4.4, the structural door** (2026-10-25, `b99e00c`). `MasmSpelling::structuredOperand` is `render()` as data - the same
cases and arithmetic, the frame added to an rbp displacement, the segment byte of a `gs:[88]`, a label an `O_MEM` the unit
types, the referenced-symbol bookkeeping the preamble's EXTERN lines read; `structuredIns` puts the PTR width where
render's `isMem` put it, keeps MASM's operand order (destination first, as the parse refers to symbols), and leaves a
`\001k` line in the text where instruction k stood. The library's `Assembler::runText(text, structured)` encodes
`structured[k]` at that line through `X64Target::encode` - the symbols `ref`'d in operand order, a label `typed` as a
parsed one is, the REP prefix byte in front - so the object's symbol table is the text path's byte for byte. The
prologue's `push`, `sub`, `mov`, the saves, `and` and the `__chkstk` probe go through `ins()` now, the same text
(`stack-probe` and `alignas-local` against the golden). An operand with no structured form - a textual immediate that is
not a plain decimal - sends its instruction through the text; none in the corpus did. `-coff=text` is the control.
Mac, structural door against -S + masm `6bcb5d2`: **-O0 and -O2, 1,342 pairs, 1,302 objects, 0 differ; -g at -O0, 671
pairs, 626 objects, 0 differ**. The census (`-time`) over Compiler++'s `Parser.cpp` at -O2: 11,914 instructions as data,
7,306 directive lines, **889 instruction lines as text = 127 funclets x 7** (`mov [rsp+16], rdx; push rbp; sub rsp, 32;
mov rbp, rdx` ... `add rsp, 32; pop rbp; ret 0`, the head and tail `closeFunclet` composes) - the one text site left, and
M3a.4's `FuncletBegin`/`FuncletEnd` directives are what make it data; the data directives (`DB`/`DD`/`DQ`, `ALIGN`,
`.LINE`, the segments, the FH3 tables, the RTTI) stay text by this plan's decision. `make test` green (run 988/0, emit 0 of
1,930 changed, names 655/0, overload 44/0, typed and adapted 648/0, exclusions 126/0/0, comment cap 0, memory gate 1,063);
`tools/determinism.sh` 9,856 outputs, 0 differ.

**M4.5, the close** (2026-10-25). Design section 6.1 marked decided, item 2 of section 15 with it; CLAUDE.md gained "M4:
the encoder library in the tree". **Owed, every one to a Windows box, none claimed**: link.exe linking CoffEmitter's
objects with the CRT and the three-file example run; `tools/windows/run-cases.cmd` with `-c` through CoffEmitter at -O0
and -O2; cdb on `-g` objects as M10 W1-W5 measured them (the CodeView is byte-equal with masm's, which cdb read before,
so the expectation is "as before"); `tools/windows/self-host.cmd` through `-c`; `msvc/build.cmd` of the tree with
`src/encoder/` in it (the library compiles with clang and g++ under `-Werror`; cl 19.51 has not seen it). **Not done,
and said**: `tools/verify-three` does not run `coff-equal.sh` - its Mac step is the place, with the re-based masm built
beside; the Xcode project of `Windows/MASM` still lists the old files; the funclet text (above) waits for M3a.4; `-c` on
a non-Windows host writes the object but a link there is still refused, the CRT and link.exe being Windows's. The
Linux rows are recorded below as they come back.

## Notes the comment cap would not hold

The library's three long comments, cut to three lines in `src/encoder/`; the full text is here, as CLAUDE.md keeps the
optimizer's.

**`AsmCodeView.cpp`, the layout of `.debug$S`** (the CodeView a debugger reads this object's code by, as ml64 /Zi and cl
write it, for a linker's /DEBUG to make a PDB of). The `.debug$S` of the plain sections is the signature
(CV_SIGNATURE_C13, 4), then subsections of {kind, length, data, padding to four}: DEBUG_S_SYMBOLS (0xF1) - S_OBJNAME
and S_COMPILE3, then per PROC S_GPROC32 (S_LPROC32 for a private one), S_FRAMEPROC and S_END, the PROC's address a
SECREL and a SECTION relocation on its symbol; DEBUG_S_LINES (0xF2) - per PROC, and per run of code outside every PROC,
the offsets its lines start at, in a block per run of one file; DEBUG_S_FILECHKSMS (0xF4) - per file the offset of its
name in the string table (no checksum); DEBUG_S_STRINGTABLE (0xF3) - "", then the file names, absolute. A PROC in a
COMDAT section has its symbols and lines in a `.debug$S` of its own, associative to that section, as cl writes it: a
linker keeping one copy of the COMDAT keeps that copy's lines. The lines are the .asm's own (/Zi, as ml64) or, in a
file with `.LINE` directives, the source lines those name and nothing else - what a compiler's -g asks for. A source
that writes a `.debug$S` of its own - a compiler describing its variables - keeps it, and only the lines, files and
strings are added after what it wrote.

**`X64Parse.cpp`, `jump_width` - the shalimar note.** The width of a JMP or Jcc: SHORT or NEAR PTR decide; otherwise
short when the target is known to be in reach, near once it was ever found not to be (the choice only widens, pass by
pass); a target not seen yet is assumed short on the first pass. A forward target's value is the previous pass's, so
the distance to it is measured from where this jump was in that pass, not where it is now: with jumps before it
widened in between, the two differ by their growth, and a jump in easy reach read as out of it and widened for good -
shalimar's `if_chain` had 707 bytes of that, all Jcc rel32 that ml64 made rel8.

**`X64Parse.cpp`, `source_line` - `.FILE` and `.LINE`.** The source a compiler assembled this file from, for the
debugger - a directive of this assembler's own (ml64 has no words for it; documented in GRIDE's
`docs\MASM_LINK_Reference.html`): `.FILE n, "path"` - source file n (1, 2, ...) is path; `.LINE line [, n]` - the
instructions that follow come from that line of file n, the file of the `.LINE` before in the PROC when n is left
out, else file 1. A `.LINE` stays in force until the next one or the PROC's ENDP, and the code after it starts a line
entry even when it names the line already in force (a one-line function's body, after its prologue). A file that has
any is described by them alone: the .asm's own lines are not recorded for it, though /Zi asked.
