# Compiler Design - cpp11 2 (Final)

Draft 2.5, 2026-10-22. It replaces `Compiler_Design.md` (Draft 1) and meets `Compiler_Design_Requirements.md` (Draft 2) as amended in section 13. R-numbers are cited where a decision serves one.

## 0. Basis, review record and revision

**Basis.** Every decision here comes from one of two places:
- the three existing compilers:
  - **cpp11** (`toolchain/Compilers/C++Optimize`, 1.28; its development repository at 1.23 holds `docs/`, `tests/`, `tools/`);
  - **cppparse** (`examples/projects/cppparse`);
  - **compilerpp** (`examples/projects/compilerpp`);
- the requirement that the result **compiles itself** (R5, R9.1).

A decision cites the code it rests on. Where the three compilers disagree, the one whose behaviour is measured wins. For the language, that measure is the oracle policy (section 11.1).

**Revision.** Nothing here is fixed for its own sake. Any decision may be changed when a deficiency in it shows up in due course: a failing gate, a measurement or a defect report. The change cites that evidence and is recorded below.

### Review record

| Step | By | Result |
|---|---|---|
| Draft 1 (`Compiler_Design.md`) | Claude, Opus 5.5 | first synthesis of the three compilers |
| Critical review (`Compiler_Design_Review_Fable.md`) | Claude, Fable 5.1 | 45 citations checked, self-compile experiment in three waves, alternate design |
| Alternate A1 (`Compiler_Design_Alternate.md`) | Claude, Opus 5.5 | 17 of the review's citations re-checked; 5 points disputed |
| Draft 2 | Opus 5.5 | synthesis of the review and A1; the conflicts below decided on the code's evidence |
| Round 2 (`Compiler_Design_Agreement_Fable.md`) | Fable 5.1 | agree with reservations; 4 sign-off conditions, 14 further flags |
| Draft 2.1 | Opus 5.5 | all 4 conditions and 14 flags applied; each piece of evidence re-read in the code |
| Draft 2.2-2.4 | Opus 5.5 | three things added. First, `__cplusplus` kept at 201103L, as cpp11 1.28 already reports it (`Backend.cpp:104`). Second, refusals only beyond C++11, from cpp11's refuse-by-name rule (`docs/EXCLUSIONS.md`). Third, the oracle policy (section 11.1), from cpp11's existing practice: `tools/feature-sweep` validates with `clang++ -std=c++11 -pedantic-errors`, and cl is the ABI oracle in the 1.25 review. Also the facts found in cpp11's development repository and `GRIDE1/docs`, and proposals P1 (section 4.1) and P2 (section 6.1) |
| Draft 2.5 | Opus 5.5 | decisions restated on their basis in the compilers and self-hosting; sent to Fable 5.1 for review |
| M2 interim review (`M2-REVIEW-FABLE-2026-10-24.md`), section 12.1 | Fable 5.1 | agree with conditions; M2's end amended: a complete, checked typed tree, `Ast.h` the oracle until M3, the adapter not grown |

### The five conflicts of Draft 2, and what the code settled

| # | Conflict | Settled | On what evidence |
|---|---|---|---|
| C1 | Self-hosting on the old code "pays twice" | **Joint:** self-hosting is a gate at the end of every milestone, and M1 is measured first | The gaps the self-compile experiment found are C++11 features that stay in any plan (section 10) |
| C2 | How Sema leaves the parser | New syntax tree and typed tree, **opened by in-place extraction** of Sema's interfaces (section 12, M2) | cpp11's parser does parsing, semantics and lowering in one pass (`Parser.h`, `Ast.h:237-238, 510, 536`); extraction keeps self-hosting green at every step |
| C3 | When target 2 moves to the new middle end | Last, in three gated steps, with the old path released meanwhile (R2.1 amended) | 4,300 lines of C6000 text passes whose cycles matter (`Tms6747.cpp:1009`); R9.4 forbids regressions |
| C4 | `__cplusplus` (R3.1) | **201103L kept** | cpp11 1.28 already reports it (`Backend.cpp:102-104`); changing it would break R1.1 |
| C5 | How the parser knows a name's kind | The parser asks Sema's `Lookup` (section 3.2) | A parser-private scope cannot settle `T::U * v`, base-class members, using-directives or injected-class-names |

### Sign-off of round 2

> **Fable 5.1 agrees to `Compiler_Design_Final.md` as Draft 2, subject to:** (1) the R3.1/C4 text stating that cpp11 1.28 reports `__cplusplus` 201103L (`Backend.cpp:104`) and recording the revert as an R1.1 oracle exception with a `-Zc:__cplusplus`-style switch; (2) §3.2 replacing the parser-private name-kind scope with 'the parser asks Sema's `Lookup`'; (3) target 3 moving with target 1 in M3 and the two one-milestone-rule exceptions named in §12; (4) M2 opening with in-place Sema interface extraction before the handle syntax tree goes behind its flag. - Fable 5.1, 2026-10-22

| Condition | Where it is met |
|---|---|
| 1 `__cplusplus` | The fact is stated in C4 and in section 13 (R3.1). 201103L is kept, so there is no revert and no switch; Fable's round-2 text allows this ("drop the first clause") |
| 2 The parser asks Sema's `Lookup` | section 3.2 |
| 3 Target 3 moves with target 1; two exceptions named | sections 5 and 12 |
| 4 M2 opens with in-place extraction | section 12 (M2); section 14 |

Fable's sign-off covers Draft 2.1. Draft 2.5 and proposals P1 and P2 are under its review.

## 1. Shape

The product keeps cpp11's name and command line (GRIDE's `toolchain.json` calls it) and becomes version 2: one executable, one front end for C90 and C++11, one middle end and four back ends.

```
source → Preprocessor → Lexer → Parser (dialect: C90 | C++11) ⇄ Sema.Lookup ("what kind of name?")
       → Syntax tree                                   (handles; new)
       → Sema (lookup, overload, templates by substitution, classes, layout,
               mangling, RTTI, constexpr tree evaluator, checks)
       → Typed tree                                    (handles; new)
       → Lowering → SSA IR (block arguments; invoke + funclet pads)  → Optimizer
       → Target: selection → MIR (virtual registers, target-described register files)
            x86-64, arm64: graph-colouring allocation → peephole
            C6000:         pre-pass schedule → A/B allocation → packetise, modulo schedule, NOP pad
       → Encoder library → { assembly text | COFF object }   (masm re-based on the same encoder;
                                                              CodeView records written here)
VM: --run and test reference, fed from the SSA IR; not used for constexpr
```

Every arrow is a boundary with a dump (`--dump=tokens,syntax,typed,ssa,mir,asm`, R4.6). Dumps exist from M0 at the boundaries cpp11 already has.

## 2. Data model (R6)

- **New structures use handles.**
  - They are the syntax tree, typed tree, SSA IR and MIR.
  - Each arena is a `std::vector`, with one handle type per arena (`ExprId`, `TypeId`, `ValueId`, `BlockId`, ...), so a mismatch is a compile error.
  - Handles are numbered in creation order, which gives the determinism that stage 2 = stage 3 needs.
  - A node is `{kind, children range, payload}`, and a `switch` on `kind` replaces virtual dispatch.
- **Surviving cpp11 modules keep their shape:** the preprocessor, lexer, `TypeTable`, mangler and library.
  - `new` becomes `make_owned` / `std::unique_ptr` / containers, module by module.
  - The R6.6 counter starts at 1,109 (`new` outside string literals) and only goes down.
  - Ownership is converted module by module. Handles come with the component rewrites.
- **Interning:** identifiers and strings in one `StringTable`; types in the `TypeTable` (cpp11 `Type.h:55,603`).
- **Source locations** are a 32-bit offset into a file table.
- **Determinism gate:** two runs, same bytes.
  - The compiler's own source has lint rules forbidding:
    - `std::map` or `std::set` keyed by a pointer;
    - iteration over an unordered container that reaches the output;
    - use of `__DATE__`/`__TIME__`.

    The compiler keeps *defining* `__DATE__` and `__TIME__` for programs it compiles (`Backend.cpp:106`).
  - Under `-j`, each job writes its own file, as cpp11 already requires (`Driver.cpp:497`).
  - A **new rule** (not a property of cpp11 today): no output depends on thread order, so diagnostics from parallel jobs are printed in input order. Today each diagnostic is only written whole, under one lock (`Source.h:9-10`), and the order between jobs is thread order.
- **Operating system:** raw pointers only in the library's OS layer (R6.5). The library implements `<thread>`, `<mutex>` and `<atomic>` over Win32 in M1 and over pthreads in M5. There is no compiler-private `os::Pool`, so targets 3 and 4 can host the compiler (R5.6).

## 3. Front end

### 3.1 Preprocessor and lexer

These are cpp11's (`src/Preprocessor.cpp`, `src/Lexer.cpp`), which already read MSVC's and the SDK's headers. Tokens go into one `std::vector<Token>` per translation unit.

### 3.2 Parser - one grammar, two dialects (R3.2, R4.3 amended)

- **One parser takes `Dialect { C90, Cxx11 }`.**
  - C90's grammar is not a prefix of C++11's: `T(x)`, `a<b>c` and declarators all differ. So the productions take the flag; they are not layered as classes.
  - compilerpp's virtual-hook layering (`Parser.h:162-171`) does not scale. Its own C++ layer refuses templates, exceptions, namespaces and operators (`Lexer.cpp:112-141`).
  - cpp11 has no C mode today: it turns `.c` inputs away by name (`Driver.cpp:1124-1136`). So C90 is new work.
- **The parser asks Sema's `Lookup` for a name's kind** (type, template, value or namespace) and keeps no table of its own. This is Clang's shape.
  - It settles `a * b;`, `x < y > z` and `T::U * v` while parsing ([dcl.ambig.res], [temp.names]).
  - It handles qualified names, members of base classes, using-directives, `using Base::name` and injected-class-names, which need real lookup and possibly instantiation.
  - There is one lookup implementation, not two. Apart from that question the parser does no semantics.
- **C++'s semantic rules derive from C's.** `SemaC` holds the conversions, `'a'` as `int` and no overloading, and `SemaCxx : SemaC` overrides them. This is an organisation, not a language fact. C++ *tightens* C as well as extending it:
  - no implicit `void *` → `T *`;
  - no implicit `int`;
  - `'a'` is `char`;
  - `const` objects at namespace scope have internal linkage.

  So the overrides refuse as well as add.
- **Refusals** (R3.3) come from one table: feature → message → hint, in compilerpp's style with cpp11's wording.
  - **A refusal is correct only for a construct beyond C++11** (from cpp11's refuse-by-name rule, with C++11 as the bound). That means C++14 or later, or a compiler extension. The message names the standard it belongs to, e.g. "... is C++14, and this compiler is C++11".
  - **A valid C++11 program that cpp11 refuses is a defect, not a limit.** It is listed in `docs/EXCLUSIONS.md`, marked *C++11, owed*, and counted. R3.1's "C++11 complete" is the day that count reaches zero.
  - "Valid" and "beyond C++11" are decided by the oracle policy (section 11.1), not by cpp11.
  - **An ill-formed C++11 program is not a refusal.** It gets an *error*, with the rule it breaks ([dcl.init], [over.match] …), as clang and g++ give one.
  - Because `__cplusplus` is 201103L (section 13, R3.1), a header's C++11 branch can reach a feature cpp11 does not yet accept. The refusal must then name the missing feature and its entry in `docs/EXCLUSIONS.md`, so the error explains itself even inside someone else's header.
  - The table and `docs/EXCLUSIONS.md` must not drift apart. The development repository already ensures this. `docs/EXCLUSIONS.md` is "not hand-written: the inventory comes out of the source". `tools/exclusions --check` reports any refusal the source raises that the document does not cite, and any citation whose site is gone. On 2026-10-13 it counted 126 refusal sites and 117 distinct messages. Both the document and the tool come into this tree in M0.

### 3.3 Sema modules

| Module | Content | Grown from |
|---|---|---|
| `Lookup` | scopes, using-directives, ADL; answers the parser's name-kind question | cpp11 `ParserOverload.cpp:978` |
| `Overload` | implicit conversion sequences, ranking, best viable function | cpp11 `ParserOverload.cpp:601-850`; cppparse `SemaExpr.cpp:288` (`convertType`) |
| `Templates` | deduction (including partial explicit arguments), partial ordering, specialisation, instantiation | cpp11 `ParserTemplate.cpp`; cppparse `SemaExpr.cpp:616` |
| `Classes` | layout, vtables, virtual bases, vtordisp, special members, implicit moves | cpp11 `ParserClass.cpp`, `ParserVtable.cpp`; cppparse `IrGen.cpp:4988` as a reference only (it is tied to cppparse's x86 module) |
| `Mangle`, `Rtti` | MSVC and Itanium name mangling; RTTI type descriptors | cpp11 `Mangle.cpp`. CodeView type *records* are debug-info emission and belong with the encoder (section 6, from `CodeViewTypes.cpp`) |
| `Constexpr` | tree evaluator (section 3.5) | cpp11 `ParserConst.cpp:105-122, 313-324` |
| `Checks` | override/hide; access; health rules | compilerpp `Semantic.cpp:805` (`resolveOverrides`, for override/hide only); cpp11's access checks; cppparse `analyzer/report/Health` |

### 3.4 Templates

- **End state (M8).** A template body is parsed once into syntax. Sema instantiates it by substitution on the syntax tree and keeps an instantiation stack, so a diagnostic in an instantiation shows the chain that led to it. Non-dependent names are checked at the definition and dependent names at instantiation. SFINAE is a failed substitution.
- **Until then (M2-M8).** cpp11's token replay (`ParserTemplate.cpp`, `Parser.h:310-334`) is the strategy behind the same `Templates` interface. Sema re-enters the parser to replay a body, and the parser reports into Sema. That is a call stack, not a pipeline loop, and the instantiation stack is built from it.
  - Under replay, SFINAE is a trial re-parse (`Source.h:56-60`).
  - Under replay, bodies are not checked at the definition.

  Both properties hold only until substitution replaces replay.

### 3.5 Constant expressions (R4.5 amended)

- A tree evaluator in Sema on the typed tree, with the **target's** layout: `long` is 4 bytes on target 1 (`X86_64Windows.cpp:20`) and 8 on target 3, and `wchar_t` is 16-bit on tms6747 (`Tms6747.h:25`).
- It **rejects** undefined behaviour as [expr.const] requires: overflow, shifts too wide, out-of-bounds access, indeterminate reads.
- It serves `static_assert`, array bounds, non-type template arguments and `enable_if` while Sema runs.
- The VM is not used for this. It defines overflow and shift behaviour (`VM.cpp:17-20, 664-666`), which constexpr must instead reject.

### 3.6 Errors

`--keep-going` reports every error in a file. cpp11 stops at the first today (`Source.h:56`). Self-hosting work cannot be sized without it, so it comes in M0.

## 4. Middle end - one optimizer (R4.2)

**The IR is new.** It is designed with reference to cppparse's and LLVM's, not as an extension of cppparse's. cppparse's IR keeps locals in memory slots, adds SSA late, and uses setjmp/longjmp exceptions (`Ir.hpp:1-8`, `Optimizer.cpp:1248,1268`, `X86.cpp:1318`).

- Typed values (`TypeId`) and **block arguments** instead of phi nodes.
- **SSA construction:** lowering gives each local a stack slot, and `mem2reg` promotes the slots whose address is not taken. Its renaming writes the incoming values as **branch operands**: the value a slot holds at the end of a predecessor becomes an argument of the jump into the merge block. No phi is ever created, which removes the inconsistency of Draft 1, where Cytron's renaming fed block arguments.
- `volatile` is a property of a load or store; a volatile access is never moved or merged. This replaces turning -O off (`Driver.cpp:1223`).
- **Exceptions:** `invoke` with a normal and an unwind successor, plus funclet pads (`catchswitch`, `catchpad`, `cleanuppad`, `catchret`). Target 1's `__CxxFrameHandler3` funclets are visible to every pass, and targets 3 and 4 lower the same pads to Itanium landing pads.

| Level | Passes, written new on this IR; the reference and oracle in brackets |
|---|---|
| -O0 | none: straight lowering, for stage 1 and debugging |
| -O1 | constant folding, copy propagation, algebraic simplification, GVN, dead code and dead stores, CFG simplification [cppparse `Optimizer.cpp:274-605`, `Ssa.cpp:314`]; jump threading, division by magic numbers [cpp11 `OptJumps`, `OptDivide`] |
| -O2 | -O1, then inlining with budgets [cpp11 `Inliner.h`], LICM and counter widening [cpp11 `OptHoist`, `OptLoops`; cppparse `Optimizer.cpp:737`], unrolling [cppparse `:922`] |

A pass counts as ported when its `--dump=ssa` effect and the benchmark numbers match the old implementation's.

### 4.1 Proposal P1: reconcile with `docs/OPTIMIZER-IR.md` (not yet agreed)

*Proposed by Opus 5.5 in Draft 2.3 and under Fable 5.1's review. Until it is agreed, the signed-off text of sections 4, 5 and 12 stands.*

**The finding.** `docs/OPTIMIZER-IR.md` was not in the tree that the review and Draft 2.1 studied; the GRIDE copy leaves `docs\` out (`toolchain/MANIFEST.md:35`). It records an earlier design decision of cpp11's, reviewed by Fable 5.1 on 2026-09-23:
- cpp11's IR is **RTL-shaped and sits below the walkers**. The walker's instructions for a function become MIR with pseudo registers, and the IR goes through SSA, scalar passes, out of SSA, Chaitin-Briggs allocation and emission.
- A neutral GIMPLE-like IR was rejected because "cxx1's knowledge of each ABI lives in its walkers: calls, aggregates by value, exceptions, varargs, x87", and rewriting the walkers to emit a neutral IR "would throw that away".
- Its stages are:
  1. MIR round-trips byte for byte;
  2. webs and graph colouring;
  3. SSA and -O1;
  4. -O2;
  5. arm64.
- Stage 2 is in 1.28: `MirAlloc`, and `Webs::splitPinned` at `optimizer/Mir.h:24`.

Neither review knew of this document. Draft 2.1's M3 builds the target-neutral SSA IR first and puts MIR beneath it. That is the path cpp11's own design record turned down, and it re-derives the ABI knowledge instead of moving it.

**Why both IRs stay.** R4.2 requires one target-neutral SSA optimizer, so the SSA IR stays. `OPTIMIZER-IR.md` itself describes GCC as having two IRs: GIMPLE above, RTL below. Draft 2.1 also has two, SSA IR and MIR. **What P1 changes is the order they are built in, and where the ABI code goes.**

**P1 splits M3 for targets 1 and 3:**
- **M3a: MIR below the existing walkers.** This is `OPTIMIZER-IR.md`'s stages 1 and 2.
  - Gate: every function round-trips through MIR byte for byte against the M0 oracle.
  - Allocation then moves onto MIR, and the x86 text optimizer is retired against the oracle and the benchmarks.
  - The new MIR and allocator are proved on a known-good input, the walkers' own output, before anything above them changes.
- **M3b: SSA IR above.** Lowering produces the SSA IR, and the -O1/-O2 passes run there.
  - The walkers become **instruction selectors from the SSA IR into MIR**. Their ABI code for calls, aggregates by value, varargs, x87 and exceptions is kept and re-pointed, not rewritten.
  - The adapter from section 12's M2 goes away in M3b.

**P1 carries `OPTIMIZER-IR.md`'s prerequisites into section 5:**
- Every throwing call has an edge to its landing pad. This is the design's `invoke`.
- Pins are placed on an occurrence, not a web (done).
- One opcode table holds every allocator constraint.
- The inliner has a cost budget; -O2 is 39 % larger than -O1 today.

**The funclet question that document leaves open is decided:** allocate the whole function before funclets are outlined, and keep any value live into a funclet in a frame slot. This matches section 4's funclet pads, where nothing flows into a funclet except through the frame.

**Cost and benefit.** M3 gets longer by one gated step. In exchange, the riskiest part, the new allocator, lands on input whose correct output is already known.

## 5. Back ends

**MIR:** `MInst`s with operand kinds, virtual registers, and register classes and files from a target description. It is not text.
- Today cpp11's x86 optimizer works on assembly text (`OptIr.h:66-74`), and it serves **both x86 targets**. x86_64-linux is `X86_64Linux`, the only x86 code that creates the `Optimizer` (`X86_64Linux.cpp:1563`). x86_64-windows uses `MasmCodeGen : X86_64Linux` (`Masm.h:111`), or `X86_64Linux` itself with the Microsoft ABI in GNU syntax (`X86_64Windows.cpp:72-74`).
- The C6000 passes work on text lines (`C6xModel.h:20-29`).
- arm64 has a text peephole (`A64Peep`).
- All three are replaced.

**Allocation: one interface, two strategies.**
- **Graph colouring** (Chaitin-Briggs) for targets 1, 3 and 4. It is `MirAlloc`'s machinery (Briggs test `:259`, coalescing `:279`, simplify `:305`) lifted off the fixed `RAX..R15` (`OptIr.h:17`) onto target register classes.
- **Schedule-aware** for C6000:
  - pre-pass scheduling on virtual registers with a register-pressure limit;
  - allocation on the A/B files with side and cross-path constraints;
  - packetising, modulo scheduling with modulo variable expansion, and NOP padding.

| | 1 x86_64-windows | 2 tms6747 | 3 x86_64-linux | 4 arm64-darwin |
|---|---|---|---|---|
| ABI | Microsoft x64 (cpp11 `X86_64Windows.cpp` table) | as cl6x, RTS6x (`Tms6747.h:25-34`) | System V | Apple arm64 |
| Allocation | graph colouring | schedule-aware | graph colouring | graph colouring |
| Exceptions | `__CxxFrameHandler3`, funclets; rethrow and try-in-catch (R5.5) | RTS6x model | Itanium | Itanium |
| Debug info | CodeView | `.dwpsn` | DWARF | DWARF |
| Output | COFF object or MASM text | asm6x text → asm6x, lnk6x (R7.2) | GNU text | GNU text |
| Moves to the new middle end | **M3** | **M6**: old path released until then (exception 2, section 12) | **M3**, with target 1: same selection code and same text optimizer today, different ABI table | **M5**: old walker and `A64Peep` until then (exception 1, section 12) |

## 6. Encoder and emitter (R4.4)

- **One encoder library** turns `MInst`s into bytes, with relaxation, relocations, `.pdata`/`.xdata`, COMDAT and CodeView `.debug$S`, including the CodeView type records now in `CodeViewTypes.cpp`. It has two sinks: `AsmEmitter` (text) and `CoffEmitter` (target 1).
- **cppparse's `Coff.cpp` is the reference.** It has REX/ModRM/SIB encoding at `:373` and the relocation kinds at `:270`. It is extended from cppparse's MASM-text subset (`Coff.hpp:1-10`) to **at least** the 74 literal mnemonics cpp11 emits (73 in `X86_64Linux.cpp` plus `Masm.cpp`), including x87 and `rep`. It must also cover the mnemonics the walker composes at run time (`ins(op + ...)`, `ins(sign ...)`). The full list is taken from the M0 oracle's `-S` output, not from the source.
- **The project's masm is re-based on the same library.** Until then the `-S`/`-c` test is semantic: both outputs disassemble to the same instructions, relocations, unwind information and COMDATs. After the re-base it is byte equality.
- masm and clang become optional on target 1. link.exe, the CRT, the SDK and cdb are kept (R7.1).

### 6.1 Proposal P2: the encoder library comes out of the project's own masm (not yet agreed)

*Proposed by Opus 5.5 in Draft 2.3 and under Fable 5.1's review. Until it is agreed, the signed-off text above stands.*

**The finding.** The project's masm, `toolchain/Windows/MASM/src` (3,850 lines), already contains most of what the bullets above ask cppparse's `Coff.cpp` to grow into:
- an x86-64 encoder over a structured `Operand`, not text (`x64.h`, `x64_encode.cpp`: 785 lines). It covers the string instructions (`MOVSB`/`MOVSQ`/`CMPSB` ..., `:266-273`) with `REP`/`REPE`/`REPNE` prefixes (`x64_parse.cpp:239-240`), the SSE scalar forms (`:224-225`) and `MOVSX`/`MOVZX`/`MOVSXD` (`:540-548`);
- a COFF writer with COMDAT key symbols and associative sections (`mscoff.cpp:82-83`, `unit.cpp:64`);
- CodeView in `.debug$S`, one per COMDAT and associative to it "as cl writes it" (`codeview.cpp:13-14`, added in `cf1e607`);
- fixups and sections (`unit.cpp`).

The review and Draft 2.1 saw that "the project owns masm", but neither looked inside it.

**P2:**
- Section 6's encoder library is **extracted from masm**: `x64_encode.cpp`, `mscoff.cpp`, `codeview.cpp` and the fixup part of `unit.cpp`. masm keeps its MASM parser (`x64_parse.cpp`, `lexer.cpp`, `expr.cpp`) on top of that library, and `CoffEmitter` uses the library directly.
- cppparse's `Coff.cpp` becomes a cross-check: its `.pdata`/`.xdata` writer is one masm does not show.
- Since both `-S`-then-masm and `-c` go through one encoder from the start, `-S` = `-c` is **byte-equal at once**. The "semantic first" step of section 6 is no longer needed.
- M4 is mostly extraction, not new work. It still depends on M3's `MInst` for `CoffEmitter`'s input.

**What the extraction must add:**
- the x87 instructions, if target 1's output uses them; none were found in masm's tables, so the M0 oracle's `-S` listing decides;
- `.pdata`/`.xdata`, unless masm's `PROC FRAME` handling already writes them, which still needs checking;
- whatever else the M0 mnemonic list shows to be missing.

**Cost and benefit.** M4 gets smaller, and the byte-equality test is genuine from the first day. The encoder is one the toolchain already ships and tests (`masm` review of 2026-09-17, `MASM/REVIEW-2026-09-17.md`).

## 7. VM (R4.5 amended)

- compilerpp's stack machine (`Bytecode.h`, `VM.cpp`), fed from the SSA IR.
- It runs whole programs for `--run`, with natives for C library calls. It also serves as the "VM reference" test: `--run` output must equal the target program's.
- Its word width and type layout come from the selected target. That is a **change to the VM**, not a setting: today its operand stack and memory model are built on a fixed 8-byte word (`Bytecode.h:8-13, 29-45`), and target widths must be carried through the stack, loads and stores.
- It keeps one `std::vector<std::uint8_t>` of memory with offsets as addresses, and the 50-million-step cap (`Bytecode.h:55`).

## 8. Driver and library

- **Driver:** cpp11's options stay. `-j N` already exists on `std::thread` (`Driver.cpp:1297-1355`) and keeps working through the library's own `<thread>`.
- **Library** (`include\`, `lib\`): cpp11's 37 headers, then, in order:
  1. *For self-hosting (M1):*
     - `<thread>`, `<mutex>` and `<atomic>` over Win32 (pthreads follow in M5);
     - `<chrono>`, `<cerrno>`;
     - `std::function` (absent by design today: `include/functional:4-18`);
     - `make_shared`, `weak_ptr`;
     - `<unordered_map>`, built first on the existing `__tree`. This is correct, needs no change to the compiler's source, and makes iteration order deterministic, which the determinism gate wants. A hash table can replace it later.
  2. *For the conformance suite:* what it needs.
  3. *The C library:* over the UCRT for target 1 and RTS6x for target 2.

## 9. Diagnostics and health (R8)

- One sink, R8.1 format: `file:line:col: severity: message`, then a hint line and an optional fix-it.
- Codes keep cppparse's families: **S** (Sema, the largest), T, E, W and I. They add cl's number on target 1 where one matches (R8.2).
- `--health` runs cppparse's analyser rules on the typed tree, which has its `#if` already evaluated. Reports in text, HTML, PDF and SVG, as cppparse writes them.
- The compiler builds with 0 errors and 0 warnings (R9.5) under cl and `clang -std=c++11 -Wall`.

## 10. Self-hosting, bootstrap, determinism (R5, R9.1 amended)

1. **Stage 0** is built by cl `/std:c++14`. The source is checked with `clang -std=c++11` from M0 on.
   - Today that check fails on aggregates with member initialisers. Three waves found **at least five** brace-initialisation sites: `Source.cpp:140`, `Type.h:377`, `Mangle.cpp:197`, `Type.cpp:219`, `Spelling.h:70-71`.
   - The third wave had not finished, so M0 removes these five and any the clang gate finds.
2. **Stage 1** is built by stage 0 at -O0, then at -O2 once M3 lands. Stage 2 is built by stage 1, and stage 3 by stage 2.
3. **Stage 2 must equal stage 3:** identical assembly and objects.
4. **From M1 on,** every milestone ends self-hosting on target 1 with its own front end.
5. **The target-1 self-host runs in the GNU spelling, assembled by clang** (M2 close, condition C6, 2026-10-25):
   cxx3's own sources use `thread_local`, whose offset needs SECTIONREL, and cxx3's MASM spelling refuses it for the
   project's masm (`Masm.cpp`, `Op::Tls`); ml64 has no COMDAT. The project's masm gained SECTIONREL in Windows/MASM
   `6bcb5d2`, so the gate moves to the default `-masm=masm` once the MASM spelling writes it - or when M4's COFF
   writer (R4.4) lands, whichever is first.
   **Moved 2026-10-25**: the MASM spelling writes it for the project's masm (`[base + SECTIONREL x]`, the two refusals
   in `Masm.cpp` lifted), and `tools/windows/self-host.cmd` runs in the default spelling with the project's masm
   `6bcb5d2` and LINK `959dac2`. On zeeshan: stage 1 and stage 2 built, 70 of 70 sources the same; the cases 957 / 0 /
   31 at -O0 and -O2, `thread-local` and `thread-local-extern` among them.

**Baseline** (reproduced by the review):
- 2 of 60 files compile.
- The first errors are 28 aggregates, 29 headers and 1 `LineFields`. `LineFields` is a compiler bug: a qualified call to a base class's `operator=` (`C6xModel.h:36`).
- The next two waves were compiler features: braced default arguments (30 files), `std::function` (22) and `unique_ptr != nullptr` (27).
- M1's scope is the list that M0's `--keep-going` measures, not these three items.

## 11. Tests (R9)

| Suite | What | Gate |
|---|---|---|
| oracle | `-S` of cpp11 1.28 on all 4 targets at -O0/-O2, over a corpus that **already exists** in the development repository:<br>• `tests/cases`: 896 cases;<br>• `tests/out-emit.golden` and `tests/out-emit-O2.golden`: 4,471 recorded outputs at 1.23;<br>• the drivers `tests/run.sh`, `tests/corpus.sh`, `tests/emit.sh` and `tools/identical.sh`.<br>Added to it: the examples and cpp11's own 60 files. M0 brings it in and re-records it at 1.28 | differences only where a change says why (R1.1) |
| feature grid | `tools/feature-sweep`: 96 probes, each first classified by the oracle policy (section 11.1). On 2026-10-21 (1.23): 87 compile, 9 refused by name, 0 refused without a name | **C++11 rows:** every refusal is a defect (*C++11, owed*), and the count only goes down. **Beyond-C++11 rows** (`expect="refuse"`): each must be refused, naming its standard |
| determinism | two runs on every corpus | identical bytes |
| conformance | Three kinds of program, grown from the probes:<br>• **valid** C++98 and C++11 programs that print and return 0, which must compile and run as the oracles say;<br>• **ill-formed** C++11 programs, which must get an error;<br>• **beyond-C++11** programs (C++14, C++17, extensions), which must be refused by name.<br>Each program's class comes from the oracle policy (section 11.1) | all pass on targets 1 and 2; a valid program refused is a *C++11, owed* defect |
| bootstrap | section 10 | stage 2 = stage 3 |
| link compatibility, target 1 | objects mixed with cl-built objects, CRT, exceptions across the boundary (R9.3). The cl side is built with **`/Zc:__cplusplus`**. cl reports 199711L by default, and cpp11 reports 201103L, so without the switch a header shared by both could take different `#if __cplusplus` branches and give two definitions of one class or inline function | links, runs, exit 0 |
| exclusions | Every entry in `docs/EXCLUSIONS.md` has a refusal test, and every refusal in the R3.3 table has an entry. Each entry is classed either *beyond C++11* (a correct refusal, kept) or *C++11, owed* (a defect) | both lists match; the *C++11, owed* part only shrinks, and reaches zero for R3.1 |
| link compatibility, target 2 | objects mixed with cl6x-built objects and RTS6x. **Run only when `CPP11_TI` names a TI installation**, as the driver already does for linking (`Driver.cpp:170`). cl6x is outside the toolchain (R7.2) | links, runs in sim6747 |
| `-S` = `-c` | section 6 | semantic, then byte-equal |
| C6000 cycles | sim6747 cycle counts on a benchmark set (R9.4) | no regression |
| measures (R1.2) | conformance count, self-compile time, sim6747 cycles, x86-64 benchmark time, `--health` findings | each at or above its target value |
| VM reference | `--run` output vs the target program's | identical |
| memory gate | section 2 counter | only goes down |

### 11.1 Oracle policy: who decides what C++11 is

This policy is cpp11's existing practice, written down. `tools/feature-sweep` validates every probe with `clang++ -std=c++11 -pedantic-errors`, and the 1.25 review used cl as the behaviour and ABI oracle and clang as the strict-C++11 oracle. The policy adds g++ beside clang.

No single compiler is exactly ISO/IEC 14882:2011, so the bound is set in layers, with a fixed order of authority:

| Question | Who decides |
|---|---|
| Is this program valid C++11? | **clang++ and g++, both `-std=c++11 -pedantic-errors`.** Both accept: valid, and cpp11 must accept it. Both reject: either ill-formed C++11 (cpp11 gives an error) or beyond C++11 (cpp11 refuses by name) |
| clang++ and g++ disagree | **The standard text, not a compiler:** N3337 (C++11 with editorial fixes) together with the defect report's status. Where both compilers apply a defect report to C++11 mode, cpp11 follows it. Each such decision goes in a short "defect-report decisions" list beside `docs/EXCLUSIONS.md` |
| Is a construct beyond C++11? | The same two compilers: accepted by `-std=c++14` but rejected by `-std=c++11 -pedantic-errors`. Examples: aggregates with member initialisers (R5.4's case), generic lambdas, deduced return types, binary literals, digit separators, `[[deprecated]]`, `make_unique`, `enable_if_t` |
| What does a valid program print or return? | clang++ and g++ at -O0 and -O2; on target 1 also cl `/std:c++14 /permissive- /Zc:__cplusplus`. **Where C++14 changed the rule**, clang++ and g++ in C++11 mode win and cl's answer is set aside. Example: a `constexpr` member function is implicitly `const` in C++11 and not in C++14 |
| ABI: layout, name mangling, exceptions, calls | cl on target 1 (R9.3); cl6x and RTS6x on target 2, when `CPP11_TI` is set; g++ on target 3; Apple clang on target 4 |

**What cl is used for.** cl is **not** a C++11 oracle. It has no C++11 mode (`/std:` starts at `c++14`), so it accepts every C++14 form cpp11 must refuse. It reports 199711L unless `/Zc:__cplusplus` is given, and it does two-phase lookup only under `/permissive-`. It is trusted for target 1's ABI and for behaviour wherever C++11 and C++14 agree.

**Where g++ runs.** On macOS `g++` is Apple clang under another name (the dev repository's `CLAUDE.md:54-56` notes this). The g++ half of the oracle runs on the Linux box, with a real GCC.

**The tools** that apply this policy: `tools/feature-sweep --clang` (today clang only; M0 adds `--gcc`), the conformance suite's classifier, and `tools/exclusions --check`.

## 12. Milestones from cpp11 1.28

**Principle:** every milestone ends with the compiler self-hosting at -O0 on target 1 (from M1 on) and with the oracle unchanged. No layer keeps two live implementations for more than one milestone, except where named:
- **Exception 1.** arm64-darwin keeps its old walker and `A64Peep` text peephole through M3 and M4, and moves in M5.
- **Exception 2.** tms6747 keeps its old path, the serial selection plus the `C6xSched`/`C6xPipe` text passes, through M3-M5 and moves in M6. That path is the one released until the new path meets R9.4 (R2.1 amended).

| M | Delivers | Requirements |
|---|---|---|
| M0 | **One tree first.**<br>• Bring the development repository's `docs/`, `tests/` and `tools/` (at 1.23) into one tree with GRIDE's 1.28 source. Today 1.24-1.28 exist only as GRIDE commits (`7f0cdce` … `cf1e607`), and GRIDE's copy leaves the rest out (`toolchain/MANIFEST.md:35`).<br>• Re-record the golden outputs at 1.28.<br>Then:<br>• determinism gate;<br>• `--keep-going`;<br>• dumps at existing boundaries;<br>• aggregates removed and the `clang -std=c++11` gate on;<br>• R6 counter;<br>• `docs/EXCLUSIONS.md` refreshed for 1.28 and checked by `tools/exclusions --check`;<br>• `tools/feature-sweep` in the release gates | R9, R1.1, R5.1, R5.4, R3.1, R3.3 |
| M1 | Library headers (section 8, Win32); `<regex>` and `<deque>` out of the source; the compiler-feature gap list measured in M0 (braced default arguments, `std::function`, `unique_ptr`/`nullptr`, lambda capture, qualified base `operator=`, rethrow, try-in-catch, ...); **first self-host at -O0** | R5.1-R5.3, R5.5, R9.1 |
| M2 | **Step 1:** extract Sema's interfaces from cpp11's parser **in place**, one module per commit (`Lookup`, `Overload`, `Templates`, `Classes`, `Constexpr`, `Mangle`), each gated by bootstrap and oracle. **Step 2:** the parser, carrying cpp11's grammar code and token replay, builds the handle syntax tree behind a flag; Sema produces the typed tree, every name, member, call and operator in it carrying the entity Sema resolved it to, recorded at parse time in the scope it was resolved in. A thin adapter lowers the typed tree to cpp11's existing `Ast.h` for a subset, as the harness that proves the typed tree can be lowered; it is not grown past that. **M2 ends when the typed tree is complete and checked** - every function with a source body typed, `-check=typed` at 0 differ against `Ast.h`, the typed fraction reported - and `Ast.h` stays the oracle until M3, which lowers the typed tree to SSA and deletes the parser-to-`Ast.h` path and the adapter together (section 12.1, amended 2026-10-25). The grammar code and token replay (in `ParserTemplate.cpp`, 3,672 lines) are **kept** inside the new parser until M8 | R4.1-R4.3 |
| M3 | SSA IR, optimizer, MIR and graph-colouring allocator for **targets 1 and 3** together. The adapter and the old x86 text optimizer are selectable during M3 and deleted at its end; -O2 self-host; stage 2 = stage 3 at -O2 | R4.1, R4.2, R5.2 |
| M4 | Encoder library, `CoffEmitter`, masm re-based, `-S` = `-c`. It starts once M3 has fixed the `MInst` definition and then runs in parallel with the rest of M3 | R4.4 |
| M5 | arm64-darwin on the new middle end (ends exception 1); library threads over pthreads; self-hosting on targets 3 and 4 | R5.6 |
| M6 | **Target 2** on the new middle end (ends exception 2), in three steps, each gated by sim6747 cycles: (a) C6000 selection reads the SSA IR and still emits today's text through the old post-passes; (b) selection emits MIR with schedule-aware allocation, still through the old post-passes; (c) packetiser and pipeliner ported onto MIR, and the text passes deleted once cycles are equal or better | R2.1, R4.2, R9.4 |
| M7 | C90 through the parser and `SemaC`; c90 retired | R3.2 |
| M8 | Template substitution replaces token replay where the suite allows, and replay is deleted; VM `--run` and VM reference test; `--health` on the typed tree | R4.5, R4.6 |

### 12.1 M2's end, amended (Fable 5.1, 2026-10-25)

The M2 row read "at M2's end the old parser-to-`Ast.h` path is deleted" until 2026-10-25. The interim review of M2
(`M2-REVIEW-FABLE-2026-10-24.md`) measured what that would take and the user delegated the choice; this is it, with the
evidence it rests on. Under the revision rule above, it is recorded here rather than rewritten in place.

**Decision.** M2 ends at a complete, checked typed tree. `Ast.h` stays the oracle until M3. The parser-to-`Ast.h` path and
the adapter are deleted together at M3's end, when the SSA lowering reads the typed tree instead of either.

**Why not delete in M2.** The adapter built by M2.11-M2.12p lowers 1,257 of 1,986 file-scope free functions and reads
fifteen fact maps the parser records while building `Ast.h` - frame slots, cleanup regions, EH chains, temporaries' guards -
because the layout half of the parser is interleaved with its grammar: a frame slot is allocated as a declarator is read, a
cleanup region as a block closes. Deleting the old path in M2 means writing that half a second time, from the typed tree,
for every function kind the measured denominator left out - members, namespaces, template instantiations by replay, closures,
the compiler-written specials that have no syntax at all, and the Microsoft funclets - which is the size of the parser's own
back half. M3 then deletes it: a layer that exists to be deleted next milestone, kept alive beside the one it copies. The
principle at the head of this section says no layer keeps two live implementations for more than one milestone, and option B
ends M2 with three - the parser's `Ast.h`, the adapter's, and the typed tree. Its gate would also have to weaken from
identical `Ast.h` to identical behaviour, which hides a difference until a program reaches it; rule 5 of the development
record (a claim with no oracle is not allowed to be believed) is against that trade.

**What M3 needs from M2**, and so what M2 must deliver: a typed tree M3 can lower without `Ast.h`. That is not types alone.
It is, per node, the entity Sema resolved - a local by its declaration, a global or function by its symbol, a member by its
`Member`, an enumerator by its value, a call or an overloaded operator by the signature overload resolution chose - recorded
where the parser resolved it, in the scope it had then, so nothing is looked up again after the file when the scopes are
gone. The frame layout, the cleanup regions and the EH tables are not M2's: M3's lowering makes its own from the typed
tree's scopes, which is why the adapter's copies of them are not kept.

**What "M2 closed" means**, measurably:

1. Every function the backends emit is counted. One with a source body - read by the grammar, a replayed template body or a
   lambda's included - has a syntax root and a typed tree. One the compiler writes with no source (an implicit special member,
   a deleting destructor, a thunk, an init function, a closure's invoker) is counted by kind: today `Classes` and `Linkage`
   build those straight into `Ast.h`, and lowering them from the typed tree is the first item of M3, since M3 is what deletes
   `Ast.h`.
2. `-check=typed` over every emitted function at 0 differ, and the report says what fraction of the syntax nodes is typed
   and names the kinds that are not, with the reason.
3. The resolution of every name, member access, call and overloaded operator is recorded per syntax node at parse time;
   neither the Typer nor the adapter looks a name up after the file.
4. Every pending fact the parser records between a parse and the node that takes it lives in `FunctionState`, so a nested
   parse cannot leak it.
5. `-dump=ast` prints every field of a `Try`, so two bodies differing in one compare different.
6. `-front=typed` is exercised by `tools/self-host` and by a non-Mac leg, and the adapter reads recorded resolutions, not
   the tables. On target 1 the self-host is the GNU spelling's (section 10, item 5).

**What this keeps from M2.8-M2.12p**: the syntax tree and its recording, the typed tree and `-check=typed`, the adapter and
`-check=adapted` as the harness for its subset, and the fact maps the adapter needs - re-keyed into `FunctionState` where
they were singletons. **What it stops**: growing the adapter's subset.

## 13. Requirement amendments

R3.1, R3.3 and R9.6 follow from cpp11 as it stands (the basis column says how). Fable 5.1 and Opus 5.5 agreed the others in round 2. Every row stays open to revision when a deficiency shows up (section 0).

| R | Amended wording | From |
|---|---|---|
| R1.2 | "Measures: conformance count, compile time on the self-host corpus, sim6747 cycles and x86-64 run time on the benchmark set, `--health` findings on the examples; the design states a target for each." | Fable |
| R2.1 | add: "While a target moves to the new middle end, its previous path stays selectable and is the one released until the new path meets R9.4." | Fable |
| R3.1 | "`__cplusplus` is 201103L, as in cpp11 1.28 (`Backend.cpp:104`). `docs/EXCLUSIONS.md` lists every C++11 construct and library facility not yet accepted; the conformance suite's C++11 part measures that list shrinking to empty, and C++11 is complete when it is empty." **Basis:** cpp11 1.28 already reports 201103L, and changing it would break R1.1. Safeguards: (1) `docs/EXCLUSIONS.md`, which already exists and is kept by `tools/exclusions --check`, brought into the tree and refreshed in M0; (2) refusals by name that point at it (section 3.2, R3.3); (3) cl built with `/Zc:__cplusplus` in the R9.3 link test (section 11); (4) this wording | Opus and Fable (round 2) |
| R3.3 | "A construct beyond C++11 (C++14 or later, or an extension) is refused by name, naming its standard, with a suggested fix. A valid C++11 program is never refused by design: if cpp11 refuses one, that is a defect listed in `docs/EXCLUSIONS.md` as *C++11, owed*. An ill-formed C++11 program gets an error, not a refusal. Validity and standard level are decided by the oracle policy (section 11.1)." **Basis:** cpp11's refuse-by-name rule and `tools/exclusions --check`, bounded at C++11 | cpp11 practice |
| R9.6 (new) | "The C++11 bound is set by clang++ and g++ at `-std=c++11 -pedantic-errors`; the standard text (N3337 with defect-report status) settles their disagreements; cl is the ABI and behaviour oracle on target 1 only." **Basis:** `tools/feature-sweep --clang` and the 1.25 review's oracles | cpp11 practice |
| R4.3 | "One parser accepts C90 and C++11; the dialect is a parameter. C++'s semantic rules derive from C's, overriding them to tighten as well as to extend. c90's compiler retires when its tests pass." | Fable; Opus (semantic derivation) |
| R4.5 | "Constant expressions are evaluated in Sema on the typed tree, with the target's layout, rejecting undefined behaviour. A bytecode VM runs programs without linking and is a test reference." | Fable and Opus |
| R5.3 | "The compiler's source uses only headers its own library provides. The library gains `<unordered_map>`, `<thread>`, `<mutex>`, `<atomic>`, `<chrono>`, `<cerrno>`, `std::function`, `make_shared` and `weak_ptr` for the self-hosting gate; `<regex>` and `<deque>` leave the source." | Fable and Opus |
| R6.3 | "New data structures (syntax tree, typed tree, IR, MIR) use index handles. Existing structures satisfy R6.1-R6.2 through `unique_ptr` and containers, and adopt handles when rewritten." | Fable and Opus |
| R6.6 | add: "Ownership converts module by module; handle conversion follows component rewrites." | Fable |
| R9.1 | "Self-hosting on target 1 is a gate every milestone from M1 on must pass at its end, with that milestone's own front end." | Fable |
| R9.2 | "A conformance suite (C++98, C++11 parts) grown from `tools/feature-sweep`'s 96 probes and `tests/cases`; every probe passes on targets 1 and 2." No set of exactly 14 probes was found in either repository (section 15). | Fable and Opus |

**What keeping 201103L leaves open:** a header with a C++03 fallback still takes its C++11 branch and stops at a refusal, where 199711L would have let it compile.

**Correction (Draft 2.3).** Draft 2.2 said this failure is always explicit and the gap is "chiefly the library". Both statements are too strong.
- **The narrow measure.** At 1.23, on 2026-10-21, `tools/feature-sweep` found 87 of 96 C++11 feature probes compiling and 9 refused by name.
- **The broader measure.** Fable 5.1's critical review of cpp11 1.25, the same day (`GRIDE1/docs/cpp11 1.25 critical review - Fable 5.1.md`), ran 268 valid programs: 99 ran identically to cl and 134 were refused. Many of the refusals came from wrong rules, not named limits. It also found 15 silent miscompiles. Its verdict was "keep the banner": "a C++11 claim needs two-phase lookup".
- **What 1.26-1.28 fixed** (`Version.h:18-47`): the lifetime, static-initialisation, special-member and overload miscompiles, most of the wrongly-refused idioms, the 1 MB stack, the four preprocessor defects, and the library's behavioural differences.
- **What `Version.h` does not record as fixed:**
  - two-phase lookup (review item 16): a conforming program can **silently** behave differently;
  - wrong `noexcept(...)` values (item 17): also **silent**;
  - the target-1 exception gaps, and classes with two polymorphic bases or a virtual base with virtual functions (item 15): refused by name;
  - several template idioms (item 18): refused;
  - comments surviving in macro arguments (item 7).

**These lists stop at 1.28.** cpp11 has since reached **1.29** (2026-10-22), whose source was not available to this review. Items not fixed in 1.26-1.28 may have been fixed in 1.29, and they must be re-checked against 1.29's `Version.h` before this section is relied on.

So, as of 1.28, the failure is *usually* explicit, with two known silent exceptions. Both are listed in `docs/EXCLUSIONS.md`'s domain and must be named there. R3.1 stands with this correction on record. It is revisable if the silent exceptions prove harmful.

## 14. Recorded dissent and its resolution

- **C2 (Opus):** a second front end built in parallel with the 34,790-line parser was the largest risk in Draft 2. **Resolved in Draft 2.1:** M2 now opens with in-place extraction of Sema's interfaces, gated by bootstrap and oracle (step 1). The parallel period is limited to step 2, which reuses the same interfaces and cpp11's grammar code.
- **C3 (Opus):** target 2 is the second release target, and its old path is released for five milestones. **Mitigated:** sim6747 cycles on the old path are a gate in every milestone, and M6 moves it in three gated steps.
- **C5:** no longer in dispute (conceded by Fable).

## 15. Open items

1. Accept or reject each section 13 amendment other than R3.1, which is decided.
2. **Proposals P1** (section 4.1, the machine-IR order) **and P2** (section 6.1, the encoder from masm): under Fable 5.1's review with Draft 2.5.
3. **Which "14 probes" does R9.2 mean?** No set of exactly 14 exists in any of the places searched:
   - both repositories;
   - the local workspaces holding older cpp11 copies (1.2, 1.4) and the tool repositories.

   No probe directory there holds 14 programs, and no document says "14 probes". The numbered sets mentioned are 10, 11, 19, 20, 25, 46, 60, 76, 94, 104, 105, 122 and 442. The probe sets that exist in the development repository:
   - `tools/feature-sweep`: 96 C++11 probes;
   - `docs/mend-1.14/probes`: 7 `p` probes and 3 `d` probes;
   - `docs/mend-1.14/review-probes`: 63 files;
   - `tests/cases`: 896 cases.

   Section 13 proposes `feature-sweep` as the seed unless you name another set.
4. **The missing documents are found** (cited by the code, absent from GRIDE's copy):
   - `docs/OPTIMIZER-IR.md` (`Mir.h:3`) and `docs/EXCLUSIONS.md` (`include/functional:8`) are in cpp11's development repository, `Compiler-Cpp-Optimize`;
   - `TMS6747.md` (`Tms6747.h:5`) is in the `VM6747` repository. It confirms section 5's description of tms6747 today: serial, a stack machine over A4, NOPs in the delay slots, with VLIW scheduling "a later, optional concern".

   M0 brings the documents into the tree.
5. **Which tree is the source of truth?**
   - GitHub's `Compiler-Cpp-Optimize` stops at 1.23 (`b7a11bc`).
   - 1.24-1.28 were committed only inside GRIDE.
   - cpp11 **1.29** (2026-10-22) is on neither. Every review here, the experiments included, was done against **1.28**, and section 10's baseline and the M0 oracle should be re-taken at 1.29 once its source is available.
   - **Resolved 2026-10-24** (`2553d0a`): 1.29 was GRIDE1 `2d9afbb`; it is merged into this tree over M2.0-M2.6, and GRIDE copies from here again. Without -g it changes none of 5,352 outputs; the emit golden is re-recorded at `2553d0a`, and section 10's baseline at 1.29 is 66 of 66 sources self-hosting. M0 assumes they are reunited in `Compiler-Cpp-Optimize`, with GRIDE copying from it as before.
6. A target value for each R1.2 measure.
7. **R7.1 does not match the toolchain.** R7.1 says "Kept: Microsoft `link.exe` … Not built: own … Windows linker". GRIDE already ships its own `link`: it takes `link.exe`'s command line, writes the PE32+ image (`GRIDE1/docs/MASM_LINK_Reference.html`), and since `cf1e607` writes a PDB. Should R7.1 name GRIDE's `link` as the default, with Microsoft's `link.exe` kept as the reference oracle for R9.3? GRIDE Design draft 0.5 §9 still says "link writes no PDB", which `cf1e607` made out of date. **Decided 2026-10-25** (the user): yes - `-masm=masm` is x86_64-windows's default and links with the project's `LINK`; `link.exe` stays as R9.3's oracle and as the `ml64` fallback's linker. R7.1 is amended to say so.
8. C90 is new work: cpp11 refuses `.c` today (`Driver.cpp:1124-1136`). M7 should be sized with that known.
