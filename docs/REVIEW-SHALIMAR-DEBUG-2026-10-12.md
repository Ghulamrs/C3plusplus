# Review: debug information for Shalimar

**2026-10-12. Question put:** Compiler-Si (`shc`, Shalimar 1.2, the compiler RIDE links) gives a debugger
nothing to read. ShalimarCompiler (Shalimar-Codex, Rev3) claims source debugging on two paths. Review
Codex critically, and say what, if anything, to transplant into Compiler-Si so that a Shalimar program
in RIDE can be debugged with the power a c90 or cpp11 program has there.

**Everything below was read from the three trees at these states**: Compiler-Si `main` 7f0167d,
Shalimar-Codex `main` adeb261 (the folder as copied on 2026-10-12), cpp11 `main` 8f1a8aa, RIDE
`src/debugger.*`, `src/shalimar/`, `help/08-debugging.md`. Nothing was run for this review; the match
report of the same day (`SHALIMAR-MATCH-2026-10-12.md`) is the measurement it leans on.

## 1. The requirement, as RIDE defines it

RIDE's `Debugger` (`src/debugger.h`) drives gdb, lldb and cdb with one vocabulary: break at file and
line, run, step in, over and out, a `Stop` with file, line and function, a stack of `StackFrame`s with
`selectFrame`, variables in scope with `setVariable`, watches added, edited and read back. The Debug tab
draws all of it. What feeds it per compiler (`help/08-debugging.md`):

| compiler, target | debugger | reads |
| --- | --- | --- |
| c90, cpp11 on arm64-darwin, x86_64-linux | lldb, gdb | the compiler's own DWARF (`Dwarf.cpp`, raw `.byte` sections) |
| c90, cpp11 on x86_64-windows | cdb | CodeView through the PDB, since M10 W1 to W5 (2026-10-08): `.cv_*` directives and `.debug$S` / `.debug$T`, assembled by clang in the GNU spelling |
| c90, cpp11 on tms6747 | **none** | vm6747 "is not a debugger yet"; sim6747 traces. No compiler in the house emits DWARF for the C6747 |
| shalimar, every target | **none** | the program stops itself: `shm_line(unit, line)` before every statement, a session in the debug runtime |

So "the same power as the C/C++ counterparts" means, concretely: lldb and gdb on the two Itanium
targets and cdb on Windows, reading debug information `shc` writes, with variables, frames and watches.
On the C6747 the C/C++ counterparts have no debugger either; parity there is a RIDE and SIM6747 question
before it is a compiler one (section 6).

## 2. Where Compiler-Si stands

**What it does.** The session protocol (`docs/DEBUGGING.md`): `#stop unit line depth`, breakpoints,
continue, step, next, out, where. RIDE's `src/shalimar/session.cpp` speaks it; F9, F8, F7, F6 work.
The compiler's output is identical in debug and release; only the runtime archive differs. It works
on every target, the C6747 included, because it needs nothing from a debugger; on Windows c90 and cpp11
have had cdb since M10 (2026-10-08), so there it is no longer the only way to stop a program.

**What it does not do, in its own words**: "Reading a variable. That needs something the compiler does
not emit yet - a table of a function's names against its frame slots". No stack frames, no watches; the
tab says *"a Shalimar program says where it is, not what is in it"*. `CLAUDE.md` records the decision
of 2026-08-22: "No debugger support. No `-g`, no DWARF, no CodeView, and none planned." This review is
asked because that decision is being reversed, and it should be reversed in that file first.

**What it already has, unemitted.** The table its document calls the next thing exists:
`Function::symbols_` (`src/Ast.h:603`) keeps every `Symbol` with its name, `Type` and frame slot for the
life of the AST, and `Frame::addVariable` hands slots out in declaration order. Every target addresses a
slot by one formula: x86-64 `frameOp(shadowBytes + 8*slot)`, arm64 `[sp, #8*slot]`, C6000
`*-A15(shmbase$f - 8*slot)` with `shmbase$f = 40 + 8*slots`. Globals are one block, `8*index`. Types
are four: `int` (4 bytes), `real` (8), `char`, and `Array`, which at run time is a pointer to the
runtime's `struct Array { int32_t count; int32_t element; void *data; }`, an array of arrays holding
`Array *` in `data`. That is a complete, small description for either debug format. What is missing is
not information but a writer.

**Two walls it shares with cpp11, both already breached there.** Windows: `shc` assembles with `ml64`
(`Driver.cpp:596`), which cannot carry a line table; cpp11 met the same wall and answered on 2026-09-10
by making the GNU spelling the Windows default and clang the COFF assembler, then wrote CodeView on top
(M10). Compiler-Si's design already separates `GnuSpelling` from `MasmSpelling` as "the whole of what
differs" between its two x86-64 targets, so the same move is open to it. And the C6747: neither cpp11
nor Compiler-Si emits any debug information there, and the house tools (ASM6x, LNK6x, SIM6747) carry
none; ASM6x's README lists DWARF under "not done, and not asked of it by the compilers".

## 3. What ShalimarCompiler has, read critically

Codex's `DEBUGGER_RELEASE_REQUIREMENTS.md` opens with its own verdict: "required for this revision; not
yet satisfied". What is there sits on two paths.

**3a. The C6747 path: TI-directive DWARF, through TI's assembler only.** It is in
`src/backend/legacy_port/Tms6747.cpp`, which is Compiler-Si's own C6000 emitter copied in (the
`legacy_port/` directory is the whole of Compiler-Si's backend, eight files differing by 1 to 169
lines, with a second verbatim copy under `legacy/backend_reference/`). The diff against Compiler-Si is
about ninety lines, and it is exactly the writer Compiler-Si lacks, in TI's directive dialect:

- `.dwtag DW_TAG_compile_unit`, two base types (`int` signed 4, `real` float 8), `DW_LANG_C`;
- per function `.dwtag DW_TAG_subprogram` with `low_pc`, `.dwfde`, begin and end file and line;
- per variable `.dwtag DW_TAG_formal_parameter` or `DW_TAG_variable`, type `int` or `real`,
  `DW_AT_location[DW_OP_breg15 <8*slot - (40 + 8*slots)>]`, which is Compiler-Si's own slot formula;
- `.dwpsn file, line, column, is_stmt` for lines; `.dwcfi save_reg_to_mem`, `cfa_offset` for frames;
- and it turns the exception index off (`exceptionIndex_ = false`) to make room for `.dwfde`.

Its qualification: 16 debugger scenarios in CCS 5.5 against `cl6x`-assembled code, breakpoints, scalar
parameters and locals, stack through `_c_int00`, step out. Then its own limit: "The supplied ASM6x
rejects DWARF directives; VM6747 also rejects them. Own-tool `--debug` means unoptimized IR without
DWARF." So in the toolchain RIDE ships, this writer produces text nothing assembles. It is also narrower
than Compiler-Si's needs: no `char`, no arrays, no globals, no lexical blocks.

**3b. The Windows path: a binary CodeView writer under code that compiles no program.**
`src/backend/coff/CoffWriter.cpp` writes `.debug$S` as bytes: S_OBJNAME, S_COMPILE3, S_GPROC32_ID,
S_REGREL32 against RBP with T_INT4, T_REAL64, T_CHAR, S_LOCAL with S_DEFRANGE_REGISTER_REL live ranges,
DEBUG_S_LINES with file checksums, and `.pdata`/`.xdata` for one prologue shape. It was verified with
eight cdb scenarios on its "array-selection path" and sixty `RtlVirtualUnwind` checks. That is genuine
CodeView, and cdb reads it. But it describes code from `IRSelector`, which (the match report, section
"Where the refusals come from") accepts a function only with no parameters, one `int` result and one
basic block. The objects become DLLs linked `/noentry /nodefaultlib`; there is no `main` to run. The
debug information is real; the program under it is a fixture. Its own document lists what is missing:
lexical shadowing, columns, complete types, other array forms, other prologues, optimized locations,
"all-language or four-target debugger parity".

**3c. Linux and macOS: nothing.** The C backends that once mapped `#line` through generated C are
removed in Rev3 (`--emit-c` answers "removed"); the `legacy_port` x86-64 and arm64 copies carry
Compiler-Si's code and no debug additions.

**3d. The part of Codex worth taking whole is a document.** `DEBUGGER_RELEASE_REQUIREMENTS.md`'s
acceptance contract is exactly right and is written for Shalimar: breakpoints that resolve to the
intended statement; step in, over, out without losing the stack; parameters, globals and locals by
their Shalimar names with correct types and values; array dimensions, elements and capacity, with buffer
capacity and text length kept distinct; lexical scope and shadowing honoured; unavailable values
reported honestly, never fabricated; frames inside a runtime bounds or arithmetic failure; diagnostics
tied to the source. Adopt it as Compiler-Si's acceptance contract, verbatim.

**Verdict on Codex.** Two demonstrations of two formats on two subsets, each verified on a path the
product does not use, under a code generator that cannot compile the twelve examples. Its DWARF for the
C6747 is a ninety-line patch to Compiler-Si's own file and its CodeView is a binary writer bound to a
selector that will not be transplanted. Neither is the machinery to build on. The contract is.

## 4. Where to take the machinery from: cpp11, not Codex

The RIDE package already contains a debug-information implementation that its three debuggers read,
its M10 harness compares against cl and clang in cdb, and its three boxes verify: cpp11's. It is
target-complete for the three hosts, written against the same four-target discipline Compiler-Si
follows, and in the same house style.

| piece | where | lines | what it needs from the compiler |
| --- | --- | --- | --- |
| `Dwarf.h` / `Dwarf.cpp` | cpp11 `src/backend/` | 541 | a `DwarfFunction` per function: name, begin and end labels, file, line, return type, `const std::vector<Local> *` (name, type, frame offset, isParam), lexical `DwarfBlock`s; `DwarfGlobal`s; and a `DwarfSpelling` for the section names, `kElfDwarf` and `kMachODwarf` already written |
| `CodeView.cpp` / `CodeViewTypes.cpp` | cpp11 `src/backend/` | 552 | the same `DwarfFunction` plus `symbol`, `prologEnd`, `codeEnd`, `mergeable`, `frameBias`; `.cv_file`, `.cv_func_id`, `.cv_loc` written inline by the Windows spelling; clang as the COFF assembler |
| the type description | an adapter, new | ~150 | Shalimar's four kinds mapped once: `int` DW_ATE_signed 4 / T_INT4; `real` DW_ATE_float 8 / T_REAL64; `char` DW_ATE_unsigned_char 1 / T_UCHAR; `Array` a pointer to a `DW_TAG_structure_type` `Array {count, element, data}` / LF_POINTER to LF_STRUCTURE, so a debugger shows the count and the data pointer honestly rather than guessing an extent |

The writers consume cpp11's `Type` and `Local`; Compiler-Si has `Type` with four kinds and `Symbol`
with name, type and slot. The adapter is the whole cost of the transplant: build `Local{name, type,
offset = 8*slot (+ shadowBytes on x86-64), isParam}` from `Function::symbols_` and the `Prototype`, and
a `Type` view the writers can ask `size()` and kind of. `shm_line` stays as it is: the runtime error
message and the in-process session keep their statement positions, and nothing about the emitted code
changes between debug and release except the debug sections `-g` adds, which is also how cpp11 keeps
its `-g` byte-neutral for the code.

**What not to transplant from Codex**: the IR, `IRSelector`, `CoffWriter` and `NativeBackend`. They are
a second compiler for a subset, and the match showed what that subset is.

## 5. The rungs, in the order the dependencies run

Each rung green on all three boxes before the next, measured by the same means as cpp11's M10: the
`m10.py` compare of our stop, step, frames and variables against clang's or cl's for the same program.

| | rung | what lands | what it unblocks |
| --- | --- | --- | --- |
| D1 | `-g` and line tables on arm64-darwin and x86_64-linux | `Emitter` gains a `sourceLine(unit, line)` hook beside `setLine`; the two Itanium emitters write `.loc`, and `Dwarf.cpp`'s line program, CU and subprogram records through the adapter; `.cfi_*` in the prologues if not already there (to verify) | lldb and gdb stop at a Shalimar line, step, and show the stack with function names. RIDE's `dbg_for` stops answering `DebuggerNone` for shc on those targets |
| D2 | variables and globals | `DW_TAG_formal_parameter` and `DW_TAG_variable` from `symbols_`, `DW_OP_fbreg` by slot; globals from the globals block; the `Array` structure type | the Debug tab shows names, types and values; `setVariable` writes a slot; watches evaluate. Codex's contract rows for parameters, locals, globals and honest unavailability are met |
| D3 | lexical blocks | `DwarfBlock` per `Block` the resolver opens, so a shadowed name resolves to its own slot | the contract's scope row |
| D4 | x86_64-windows | a GNU/COFF spelling for the Windows target assembled by clang, as cpp11 did on 2026-09-10 (the project's own MASM and LINK are the alternative only if MASM learns `.cv_*`, which is a decision for that repository); then `CodeView.cpp` and `CodeViewTypes.cpp` through the same adapter | cdb, and RIDE's Windows debugging for Shalimar through the same `Debugger` as c90 and cpp11. The in-process session remains for anyone without clang |
| D5 | the C6747 | section 6 | |

D1 and D2 are the bulk of the value and the smaller part of the work: the writers exist, the slot and
type facts exist, and the hooks go where `setLine` already is. A fair first estimate is the adapter and
hooks at a few hundred lines in Compiler-Si plus the two writers brought across; the M10 record says
what the Windows rung costs, since cpp11 paid it five weeks ago.

## 6. The C6747, kept separate on purpose

No compiler in RIDE has a debugger on the C6747 today; `vm6747` runs and traces, `sim6747` runs and
traces, and RIDE says so when F8 is pressed. Two things stand between a Shalimar programmer and a stop
on the DSP, and only one of them is a compiler matter.

- **A debug format the house tools carry.** Codex's `.dwtag`/`.dwpsn`/`.dwcfi` text needs TI's
  assembler. The alternative that fits the house is the one cpp11 already uses on ELF: raw
  `.section .debug_info` and `.byte` data, which asks of ASM6x only sections, bytes and label
  differences, and of LNK6x only that it keep `.debug_*`. With TI's register numbering (A0 to A15 are
  0 to 15, B0 to B15 are 16 to 31, so the frame register is 31 and `DW_OP_breg31`), `Dwarf.cpp` needs
  one more `DwarfSpelling` and nothing structural. Codex's ninety lines are the measured reference for
  what TI's own debugger expects to find, which is their worth.
- **A debugger.** SIM6747 would need a stop-and-inspect interface, a gdb remote stub being the shape
  gdb already speaks, and RIDE a fourth engine. That is a SIM6747 and RIDE round, and it serves c90 and
  cpp11 as much as Shalimar.

Until both exist, Shalimar on the C6747 keeps what it has and the others lack: the `shm_line` session,
which stops and steps on vm6747 today. Giving *that* session variables, by emitting the same
name-to-slot table into the program for the runtime to read, is a cheaper route to values on every
target including the DSP; it is not the route to "the same power as c90 and cpp11", which is the
debuggers RIDE already drives, and it would be a second implementation of what D2 emits. Noted as an
option, not recommended as the path.

## 7. What must change beside the code

- Compiler-Si `CLAUDE.md`, "Decisions that are settled": the no-debugger decision is reversed, with
  the date and this review as the reason, before the first `-g` lands.
- RIDE `help/08-debugging.md` and `toolchain.cpp`'s "shalimar writes no debug information for any
  target" lines come down rung by rung, not ahead of them.
- `tests/debug.sh` keeps its cases: the session is not removed by this, and its "a release build has no
  code for it" boundary still holds for the session. A new `tests/m10`-style suite measures the
  debuggers.
- Compiler-Si's `build.bat` was mended today (7f0167d); the Windows rung will need it to list what the
  Makefile lists, as it now does.

## 8. Summary

Compiler-Si lacks a writer, not information: its AST holds every name, type and slot a debugger needs
and its emitters know every slot's address on every target. Shalimar-Codex holds two partial writers on
paths the product does not use, over a code generator that cannot compile the programs; take its
acceptance contract and its C6747 DWARF text as a reference, and nothing else. The machinery to
transplant is cpp11's own DWARF and CodeView writers, which RIDE's three debuggers already read, through
an adapter of a few hundred lines. Two Itanium targets first, Windows by the road cpp11 took, and the
C6747 when the house has a debugger there for anyone.
