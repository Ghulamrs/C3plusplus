# Fable 5.1 on `Compiler_Design_Final.md` Draft 2.5, with verdicts on P1 and P2

2026-10-22. Read in full: Draft 2.5 (439 lines), `docs/OPTIMIZER-IR.md` (dev repo, 121 lines), `docs/GCC-SCHEME-2026-09-24.md` (head), GRIDE's masm sources (`toolchain/Windows/MASM/src`, 3,850 lines: `asm.h`, `x64_encode.cpp`, `x64_parse.cpp`, `mscoff.cpp`, `codeview.cpp`, `unit.cpp`), the masm review and its mend ledger (`iClaude/MASM/REVIEW-2026-09-17.md`, 312 lines), my 1.25 review (`GRIDE1/docs/cpp11 1.25 critical review - Fable 5.1.md`, §1, §2, §5-§7), `MASM_LINK_Reference.html`, GRIDE Design draft 0.5 §9, and the dev repository's `tests/`, `tools/feature-sweep`, `docs/EXCLUSIONS.md`, `README.md`, `CLAUDE.md`. Experiments: a census of cpp11's target-1 MASM output over the dev repository's 899 test cases at -O0, -O1 and -O2 (1,096 + 548 listings), compared with masm's instruction tables; five behaviour probes run natively or compiled for target 1. **Every code fact below is as of cpp11 1.28** (GRIDE1's copy); 1.29's source was not available. Nothing in `_NEW_` or the source trees was changed.

## 1. Overall verdict

**Agree with conditions.** Draft 2.5 states each decision on evidence I can check, and the evidence holds in every case but the one noted in section 4 (the R3.3 "basis" is cpp11's mechanism, not its practice: cpp11 today refuses C++11 by name on purpose). The §13 correction drawn from my 1.25 review is accurate and the two silent exceptions it names are real as of 1.28 (I re-ran them). P1 and P2 are both sound in substance and I agree to both with conditions; each carries one factual error that must come out (P1's four "prerequisites" are all already met in 1.28; P2's claim that masm has no `.pdata`/`.xdata` writer is wrong, and its CodeView asset is the smaller of the two writers the project owns). The blocking items are few and textual; none changes the architecture.

## 2. Proposal P1 (section 4.1): MIR below the walkers first

### 2.1 Verdict

**Agree, subject to three conditions** (section 2.5). The order is right: the allocator, the riskiest new component, is proved against the walkers' known-good output before anything above it moves, and the stream passes keep running throughout. The text overstates what is still to do and understates what M3b rewrites.

### 2.2 Facts checked

| Claim in §4.1 | Verdict | Evidence (1.28 unless said) |
|---|---|---|
| `docs/` left out of GRIDE's copy (`toolchain/MANIFEST.md:35`) | confirmed | `MANIFEST.md:35-38` "Not copied: tests\, docs\, examples\ ..." |
| `OPTIMIZER-IR.md`: RTL-shaped IR below the walkers; GIMPLE-like IR rejected; five stages | confirmed | `OPTIMIZER-IR.md:34-61, 82-89`; the quoted sentence at 36-39 |
| "Stage 2 is in 1.28: `MirAlloc`, `Webs::splitPinned` at `Mir.h:24`" | **confirmed, and more than stated** | `Mir.h:24` `bool splitPinned();`. The pipeline runs `Webs → Locals → Allocate` at -O1/-O2 by default (`OptPipeline.cpp:91-121`, wired at 213-220 inside `FrameGroup`, gated on `fn.whole && fn.prologueAt >= 0`). Stage 1 (round trip) is the standing design: `Optimizer` is a `Spelling` decorator that holds and replays the stream (`Optimizer.h:3`, `OptIr.cpp:49` `Operand::from`, `:114` `Operand::op`). Stage 3 (SSA on MIR) is **not** in 1.28, and the dev repository's later design note dropped it: `GCC-SCHEME-2026-09-24.md:11-14` "SSA over MIR becomes webs and def-use chains". P1's "SSA above, in the SSA IR" is consistent with that |
| "P1 carries the prerequisites into section 5": (1) landing-pad edges, (2) pins per occurrence (done), (3) one opcode table, (4) inliner budget; "-O2 is 39 % larger than -O1 today" | **all four are done in 1.28; the 39 % is stale** | (1) `OptCore.h:68-69` "Eh is an exception edge from a call to its landing pad's block", `Flow::build(stream, convention, regions)` (`OptFlow.h`), `Optimizer::exceptionRegion` (`Optimizer.h`); (3) `OptTable.h:3` "One description of every x86 mnemonic the optimizer knows"; (4) `Inliner.h:5` three budgets (site, caller, unit) and GCC's badness order. Measured over 548 corpus cases on target 1: instructions O0 = 455,504, O1 = 185,796, O2 = 221,639, so **O2/O1 = 1.19**, not 1.39 |
| Funclets today are optimised apart from the function | confirmed | `Optimizer.h` `funcletBegin/funcletEnd` "held apart ... improved as a piece of its own"; `FrameGroup` gate `fn.whole` means no allocation inside a funclet |
| The walkers hold the ABI knowledge | confirmed | Abi tables `X86_64Windows.cpp:35-57`; shadow space `X86_64Linux.h:147`; x87 only for 16-byte `long double` (`X86_64Linux.cpp:30, 148-160, 357`); EH tables `Walker.h` `LsdaSpelling`, `Masm.cpp:446-478` (`.pdata`/`.xdata` as data), funclet lifting `Masm.cpp:719-729` |
| `X86_64Linux.cpp:1563` the one creator of `Optimizer` | confirmed | and the MASM path inherits it: `Masm.cpp:715, 723, 812` use `optimizer_` |

### 2.3 Technical soundness

**M3a (MIR below the walkers) is sound and small.** cpp11's stream is already a machine IR in all but one respect: operands are parsed (`OptIr.h:38-63`), registers are numbered, pseudos exist (`Mir.h`), the flow graph has EH edges, the allocator runs - but the opcode is a `std::string m` (`OptIr.h:66`) classified by name through `OptTable`. M3a is therefore: make `OptTable`'s rows the opcode enumeration, give `Instr` an opcode id and operand kinds, and keep every existing pass compiling against it. The byte-for-byte round-trip gate is the one the `Optimizer` decorator already satisfies. What M3a must **not** do is what §4.1 says: "the x86 text optimizer is retired against the oracle and the benchmarks". The stream passes (`OptPipeline.cpp`, ~5,000 lines) are the only -O1/-O2 cpp11 has on x86; retiring them in M3a means porting them to a second form only for M3b's SSA passes to replace them - the "pays twice" shape. They stay through M3a and are deleted at the end of M3b when the SSA passes match the benchmarks (condition 1).

**M3b (walkers become selectors) is half a port and half a rewrite, not "re-pointed".** A walker is a `Visitor` over `Ast.h` (`Backend.h:15`, `Walker.h`) that emits a stack machine - `push()`, `pushArg()`, `pushF()`, `pushX87()` at `X86_64Linux.h:223-240`; my 1.25 review §5 measured -O0 output as `push rax ... pop r10; idiv r10d`. A selector consumes typed SSA values and writes `MInst`s with virtual registers; it has no expression stack, no statement visits, no temporaries' slots. So the *visit layer* (expression and statement codegen) is rewritten as selection patterns, and the *ABI layer* (argument classification, shadow space, aggregates by value, varargs, x87 conversions, prologue/frame, EH table and funclet writers) is ported as the selector's helpers. The 1.25 review §5 is the right size estimate: "a new target is a new walker (~2,500 lines)"; **(inference)** roughly half of each x86 walker moves unchanged. P1's benefit survives this correction - the ABI layer is exactly what interoperates with cl, and M3a keeps it producing oracle-identical output longer - but §4.1 must say it (condition 2).

**The funclet decision is right, with two rules stated.** Allocating the whole function before outlining, with every value live into a funclet in a frame slot, is what LLVM's WinEH does (values live across funclet boundaries are demoted to stack slots; funclets are allocated with their parent). Two consequences must be in the text: an unwind edge kills every register (a funclet is entered by the runtime with only the establisher frame), so nothing is live-in through registers; and each funclet has its own callee-saved set and prologue, because it has its own `UNWIND_INFO`. With those two rules the "try inside catch" refusal (`ParserStmt.cpp:1438-1443`, nested funclets the slicing cannot nest) disappears, since nesting is just nested pads.

**R4.2 holds.** The SSA IR stays the one optimizer for all targets; the stream passes on MIR are a bounded interim. **(inference)** During M3a, R4.2's "one optimizer" is not yet true for x86 - which is also the case today.

**My round-2 conditions are untouched.** Target 3 moves with target 1 (§4.1 says "targets 1 and 3"); M2 opens with in-place extraction. One consequence P1 does not mention: M3a depends on nothing in M2 (it sits below the walkers), so it can run **alongside M2**, which keeps the M2 adapter's life to one milestone and lands the allocator earliest. Recommended, not required.

**One asymmetry to state.** For targets 1 and 3, P1 builds bottom-up (MIR under the walker first). For target 2, the agreed M6 step (a) builds top-down ("C6000 selection reads the SSA IR and still emits today's text"). Both can be right - the C6000 text passes pattern-match the walker's text shape (`C6xPipe.cpp:1-3` counted loops `[P] B head`, `ADD 1, i, i`), which argues for *bottom-up* there too (MIR that round-trips byte for byte through the text passes) - but the document should apply one principle or say why not.

### 2.4 Interaction with the agreed text

| Flag | Where | Change | Severity |
|---|---|---|---|
| P1-a | §4.1 "prerequisites" | Record all four as met in 1.28 (`OptCore.h:68`, `Mir.h:24`, `OptTable.h:3`, `Inliner.h:5`); replace "39 %" with the measured 19 % | non-blocking (factual) |
| P1-b | §4.1 M3a | "the x86 text optimizer is retired" → "the stream is typed; the stream passes carry over; the allocator lands on typed MIR". Retirement moves to the end of M3b | **blocking** (sequencing) |
| P1-c | §4.1 M3b | "kept and re-pointed, not rewritten" → "the ABI layer is ported as the selector's helpers; the visit layer is rewritten as selection" | **blocking** (sizing) |
| P1-d | §4.1 funclets | Add the two allocator rules (unwind edges kill registers; funclets own their save set) | non-blocking |
| P1-e | §12 M3 | Split into M3a/M3b; note M3a may run alongside M2 | non-blocking |
| P1-f | §12 M6 (a) | State the bottom-up / top-down asymmetry or align M6 (a) with P1 | non-blocking |

### 2.5 Sign-off on P1

> **Fable 5.1 agrees to P1, subject to:** (1) M3a types the existing stream (opcode ids from `OptTable`, operand kinds) and lands the allocator on it with the stream passes carried over, and the x86 stream passes are retired at the end of M3b, not M3a; (2) §4.1 states that the walkers' ABI layer is ported and their visit layer is rewritten as selection, and M3b is sized on that; (3) the four `OPTIMIZER-IR.md` prerequisites are recorded as met in 1.28, with 19 % in place of 39 %.

## 3. Proposal P2 (section 6.1): the encoder library out of GRIDE's masm

### 3.1 Verdict

**Agree, subject to four conditions** (section 3.5). Extracting from masm is clearly better than growing cppparse's `Coff.cpp`: masm's encoder is proven against ml64 over the compilers' real output and has the directive layer, COFF writer and relocation set cpp11 needs. Two of P2's facts are wrong (masm *does* write `.pdata`/`.xdata`; its CodeView writer is the smaller of two the project owns), and "byte-equal at once" needs one design rule to be true.

### 3.2 Facts checked

| Claim in §6.1 | Verdict | Evidence |
|---|---|---|
| masm is 3,850 lines; `x64_encode.cpp` 785 | confirmed | `wc -l` |
| String ops `:266-273`, `REP`/`REPE`/`REPNE` `x64_parse.cpp:239-240`, SSE scalar `:224-225`, `MOVSX/ZX/SXD` `:540-548` | confirmed | read at those lines |
| COMDAT key symbols and associative sections (`mscoff.cpp:82-83`, `unit.cpp:64`) | confirmed | |
| CodeView in `.debug$S` per COMDAT "as cl writes it" (`codeview.cpp:13-14`) | confirmed, **but** | masm's writer is lines + `S_GPROC32/S_FRAMEPROC/S_END` with **no checksums** (`codeview.cpp:11, 166-167`) and **no type records**. cpp11's own `backend/CodeView.cpp` + `CodeViewTypes.cpp` (553 lines) writes `S_REGREL32` locals, `S_BLOCK32`, `S_UDT`, `S_GDATA32` and `.debug$T` type records (`CodeView.cpp:9-13`, `CodeViewTypes.cpp:237`) - in GNU syntax for the clang path. And cpp11 1.28 **refuses `-g` with `-masm=masm`** on target 1: "this compiler writes no such thing for x86_64-windows in the MASM spelling" (`Driver.cpp:1087`, `X86_64Windows.cpp:68` `emitsLineTable` only for GNU). So masm's CodeView is exercised by c90/shalimar, not by cpp11 |
| "x87: none found in masm's tables" | confirmed, **and none needed** | `x64_encode.cpp` has no `f*` mnemonics (its two "x87" hits are opcode byte `0x87` for `XCHG`). cpp11 emits x87 only for a 16-byte `long double` (`X86_64Linux.cpp:30, 160`); on target 1 `long double` is 8 bytes (`X86_64Windows.cpp:25`). Census: **0** x87 instructions in 1,096 target-1 listings |
| "`.pdata`/`.xdata`, unless masm's `PROC FRAME` handling already writes them, which still needs checking" | **it does** | `x64_parse.cpp:301-303` (`.PUSHREG`, `.ALLOCSTACK`, `.SETFRAME`, `.SAVEREG`, `.SAVEXMM128`, `.PUSHFRAME`, `.ENDPROLOG`), `:510-514` opens `.pdata`/`.xdata` at the first `PROC FRAME` "as ml64", `:631` `unwind()`, `:744-755` `UNWIND_INFO` written at `ENDP`; mend ledger R10 closed `1c25cef`. **But cpp11 does not use `PROC FRAME`**: it writes `.pdata`/`.xdata` segments by hand as `DD imagerel` data with its own `FuncInfo` for `__CxxFrameHandler3` (`Masm.cpp:446-478`; the census has 63,200 `imagerel` operands and no `PROC FRAME`), and masm refuses `PROC FRAME:handler` (`x64_parse.cpp:504`). So for cpp11 the unwind data is compiler-written data in both sinks |
| "cppparse's `Coff.cpp` ... its `.pdata`/`.xdata` writer is one masm does not show" | **wrong** | masm has one (above). cppparse's is at `Coff.cpp:10-11, 574-618`; both exist |
| Coverage of cpp11's target-1 mnemonics | **72 of 72** | Census: 899 dev-repo cases, `-S -arch x86_64-windows -masm=masm` at -O0 and -O2 → 1,096 listings, 72 distinct instruction mnemonics (full list in Appendix A). masm's tables and special-cased names total 314 (including the 30 `SETcc`/`CMOVcc` forms); every one of the 72 is in them. `rep movsq` did not appear (emitted only where `copiesByString()`, `X86_64Linux.cpp:479-494`) and masm encodes `REP` + `MOVSQ`. Directive forms used: `COMDAT` 10,934, `ASSOCIATIVE` 17,050, `imagerel` 63,200, `OPTION DOTNAME/NOSCOPED/PROC:PRIVATE`, `.CONST`, `SEGMENT…ENDS`, `DB L2-L1` - all closed in the mend ledger (R9, R11, R16) |
| masm "already ships and tests" the encoder; review of 2026-09-17 | confirmed, with its history | At the review, 0 of 681 corpus files assembled and 98 of 6,634 instructions differed from ml64; the mend of 09-17/18 (ledger in the same file) reports 681/681 assembling and the corpus byte-identical to ml64 after items 17-18 (`3048d6e`, `ca7846f`), with ml64's quirks adopted: `TEST` operand order, `MOV r64, imm` by the literal's unsigned value, long-NOP alignment, `.text$mn` first, Static symbols only when referenced, timestamp 0. I did not re-run that comparison today |
| masm's language level | **C++14** | the review builds it `-std=c++14` (D10) and `cl /std:c++14` |

### 3.3 Technical soundness

**Extract from masm, not grow `Coff.cpp`: agreed.** cppparse's `Coff.cpp` is a MASM-text assembler for cppparse's own 26-form subset (`Coff.hpp:1-8`), tested only against cppparse's listings, with no `SECREL`/`SECTION` relocations (which CodeView needs) and no directive layer. masm has the full directive layer cpp11's output uses, a `Unit` with multi-pass jump relaxation (A8 closed), ml64's section and symbol conventions, `R_SECREL`/`R_SECTION` (`asm.h:17-19`), relocation addends for a future RELA writer (D13 closed), and a corpus proof. `Coff.cpp` as a cross-check is of low value; its one distinct asset, the `.pdata`/`.xdata` writer, is not distinct.

**"Byte-equal at once" is true under one rule.** masm's `Unit` decides relaxation, section order, symbol order, the Static-symbol rule and the zero timestamp; if `CoffEmitter` builds the same `Unit` with the same sequence of sections, symbols, data and instructions that the `AsmEmitter`'s text yields when parsed, the bytes are equal by construction. Three places where the two paths can still diverge, each a design rule rather than a test: (i) `MOV r64, imm` is chosen by the *literal's* unsigned value in the text path (A2: ≥ 2^32 → imm64); `CoffEmitter` has a value, not a spelling, so it must apply the same rule to the spelling `AsmEmitter` would print, or `AsmEmitter` must print what the encoder chose; (ii) `.pdata`/`.xdata`/`FuncInfo` must be emitted as data by the compiler in both sinks, as today, not by masm's `PROC FRAME` path in one and the compiler in the other; (iii) CodeView must come from one writer in both sinks. With (i)-(iii) the "semantic first" step of §6 can be dropped as P2 says; the semantic disassembly diff is still a useful *diagnostic* when the byte test fails.

**CodeView: pick cpp11's writer.** The library's CodeView component should be cpp11's `CodeView.cpp`/`CodeViewTypes.cpp` re-targeted from GNU directives to byte records (as §6 already says: "including the CodeView type records now in `CodeViewTypes.cpp`"). masm's `codeview.cpp` stays for the `.FILE`/`.LINE` consumers (c90, shalimar). A consequence worth naming as a feature: the MASM path on target 1 gains `-g`, which 1.28 refuses.

**What M4 still contains.** (1) The library split: `Unit`/`CoffWriter`/`X64Target::encode` out of the MASM parser (D13's `Target` abstraction helps); (2) `CoffEmitter` = `MInst` → `Operand` → `Unit`; (3) `AsmEmitter` printing MASM text from `MInst` (today `MasmSpelling` does it from `Op`); (4) the compiler-side fixups: COMDAT/associative sections, `imagerel`, label differences, `FuncInfo`; (5) CodeView from `MInst` locations through cpp11's writer, both sinks; (6) the GNU-syntax `AsmEmitter` for targets 3 and 4 is unaffected (clang/as assemble it); (7) tests: byte equality over the oracle corpus, links through GRIDE's `link` and Microsoft's `link.exe`. Smaller than growing `Coff.cpp`, but "mostly extraction" undersells (2)-(5).

**The library joins the compiler's source.** It must be C++11 (R5.1) and compile under cpp11 (R9.1): masm is C++14 today, so the extracted files enter the M1 gap list like any other compiler source.

### 3.4 Interaction with the agreed text

| Flag | Where | Change | Severity |
|---|---|---|---|
| P2-a | §6 bullet 2 | "including x87 and `rep`" → `rep` only; x87 is not emitted on target 1 (8-byte `long double`, `X86_64Windows.cpp:25`; census 0) | non-blocking (factual) |
| P2-b | §6.1 | Delete "its `.pdata`/`.xdata` writer is one masm does not show"; state that cpp11 writes unwind data and `FuncInfo` as data and `CoffEmitter` does the same, so masm's `PROC FRAME` path is not on cpp11's route | **blocking** (it decides how byte-equality is achieved) |
| P2-c | §6.1 / §1 / §3.3 | The library's CodeView component is cpp11's `CodeView.cpp` + `CodeViewTypes.cpp`; masm's `codeview.cpp` remains for `.FILE`/`.LINE`; the MASM path gains `-g` (refused in 1.28, `Driver.cpp:1087`) | **blocking** (two writers today; one must be chosen) |
| P2-d | §6.1, §12 M1/M4 | The extracted library is C++11 and enters the self-host gap list (masm is built as C++14) | **blocking** (R5.1/R9.1) |
| P2-e | §6.1 | "byte-equal at once" → "byte-equal by construction once `CoffEmitter` drives the same `Unit`, with the imm64-by-spelling rule shared"; §11's `-S = -c` row becomes "byte-equal" | non-blocking |
| P2-f | §6.1 | Replace "mostly extraction" with the M4 content list of 3.3 | non-blocking |
| P2-g | §15 item 7 / §6 | Name both linkers: objects from `CoffEmitter` must link through GRIDE's `link` (the toolchain's) and Microsoft's `link.exe` (the R9.3 oracle) | non-blocking (see 5.4) |

### 3.5 Sign-off on P2

> **Fable 5.1 agrees to P2, subject to:** (1) the library's CodeView component is cpp11's `CodeView.cpp`/`CodeViewTypes.cpp`, and the MASM path gains `-g`; (2) `.pdata`/`.xdata` and `FuncInfo` are emitted by the compiler as data in both sinks, as today, and the `.pdata`/`.xdata` sentence about masm is corrected; (3) the extracted library is C++11 and enters the self-host gap list; (4) §6 drops x87 for target 1.

## 4. Is each restated "basis" true?

| Decision | Stated basis | Verdict |
|---|---|---|
| C1 | gaps found are C++11 features that stay in any plan | true (section 4 of my review; braced defaults, `std::function`, `unique_ptr != nullptr`, lambda capture, qualified base `operator=`) |
| C2 | parser does parsing, semantics and lowering in one pass (`Parser.h`, `Ast.h:237-238, 510, 536`); extraction keeps self-hosting green | true; the citations hold |
| C3 | 4,300 lines of C6000 text passes whose cycles matter (`Tms6747.cpp:1009`); R9.4 | true (`C6xSched.cpp` 2,294 + `C6xPipe.cpp` 2,007; the 158k → 352k cycles note at 1009) |
| C4 | cpp11 1.28 reports 201103L (`Backend.cpp:102-104`); changing it would break R1.1 | true |
| C5 | a parser-private scope cannot settle `T::U * v`, base members, using-directives, injected-class-names | true (my concession, unchanged) |
| R3.1 basis | "cpp11 1.28 already reports 201103L" | true. The three safeguards exist as described: `EXCLUSIONS.md:21-22` "not hand-written: the inventory comes out of the source", `:33-34` 126 sites / 117 messages on 2026-10-13; `tools/exclusions` present |
| R3.3 basis | "cpp11's refuse-by-name rule and `tools/exclusions --check`, bounded at C++11" - column says "cpp11 practice" | **overstated.** The *mechanism* is cpp11's. The *bound* is not: cpp11's practice is to refuse **C++11** constructs by name on purpose - `EXCLUSIONS.md:1-5` "The language cxx1 accepts is C++03 with C++11 extensions, and this file is the list of what it does not take"; the 2026-10-08 review moved the headline *away* from C++11 for that reason (`feature-sweep:9-13`). R3.3 as amended reclassifies most of today's 126 refusal sites from "limit" to "defect, owed". That is a requirement choice (R3.1's), correctly made, but its basis is the requirement plus the oracle policy, not cpp11's practice. Say so |
| R9.6 basis | "`tools/feature-sweep --clang` and the 1.25 review's oracles" | **true with one correction**: `feature-sweep` validates with `clang++ -std=c++11 -pedantic-errors -fsyntax-only` only when `--clang` is passed (`feature-sweep:5, 18-19, 173-182`); it is an option, not the default. The 1.25 review used cl for reference output and clang for validity (§2 "each through cl (reference output), clang (validity)") - as §11.1 says. Adding g++ is new, and sound |
| §11.1 facts about cl, clang, g++ | | cl has no C++11 mode (`/std:c++14` is the lowest): **true**. cl reports 199711L unless `/Zc:__cplusplus`: **true**. cl does two-phase lookup only under `/permissive-` (or `/Zc:twoPhase`): **true**. `constexpr` member functions implicitly `const` in C++11 and not in C++14 (N3652): **true**. N3337 is the C++11 text with editorial fixes: **true**. The eight "beyond C++11" examples are all C++14: **true** (`make_unique`, `enable_if_t` are library additions in C++14). "On macOS `g++` is Apple clang": **true** on a stock Mac; the cited `CLAUDE.md:54-56` says something adjacent (a Mac cannot enforce C++14 because Apple's libc++ is permissive; "refused only when it reaches real g++") - cite it for that, not for the alias. One gap: a rejection by both clang++ and g++ can also be an implementation limit or a shared bug (rare); the N3337 row covers it if read as "disagreement **or doubt**" |
| §11 oracle row figures | | `tests/cases`: 899 `.cpp` (Draft says 896); `out-emit.golden` 4,471 files **and** `out-emit-O2.golden` 705 (Draft says "4,471" for both); `7f0cdce` is the 1.25 commit, so "1.24-1.28 ... (`7f0cdce` … `cf1e607`)" misses 1.24 by one commit. Minor |
| §13 correction's numbers from my 1.25 review | | 268 valid programs, 99 identical to cl, 134 refused, 15 silent miscompiles, "keep the banner", "a C++11 claim needs two-phase lookup": **all true** (review §1-§2) |

## 5. Technically wrong or inconsistent in Draft 2.5

| Flag | Where | Finding | Severity |
|---|---|---|---|
| D-1 | §4.1 | P1-a/b/c (section 2.4) | see 2.4 |
| D-2 | §6, §6.1 | P2-a/b/c/d (section 3.4) | see 3.4 |
| D-3 | §13 R3.3 "From: cpp11 practice" | Basis overstated (section 4). Reword to "cpp11's mechanism, bounded by R3.1 and the oracle policy" | non-blocking (wording) |
| D-4 | §11.1 | `feature-sweep` validates with clang only under `--clang` | non-blocking |
| D-5 | §13 correction, "two known silent exceptions" | There are **three** as of 1.28, and I re-ran them: (a) two-phase lookup - `template<class T> void f(T){ g(1); }` with `void g(long)` before and `void g(int)` after prints `int` (conforming: `long`); (b) `noexcept(U())` for `struct U { ~U() {} }` is **0** (a user-provided destructor without a specification is implicitly `noexcept(true)`, [except.spec]/14; `W{}` and `~X() = default` give 1 correctly; `noexcept(new int)`, a `noexcept(false)` destructor and `noexcept(throw 1)` are right - so item 17 is *partly* fixed in 1.27, the implicit-noexcept case is not); (c) comments survive into macro arguments: `#define S(x) #x` with `S(a /* c */ b)` prints `a /* c */ b` (conforming: `a b`, [lex.phases] 3) - silent through `#` and `##`. Add (c) to the silent list; the other item-7 defects (BOM, `#if` unsigned, rescan, `_Pragma`) are fixed in 1.27 per `Version.h` | **blocking** (the correction is the record R3.1 rests on; it must be complete) |
| D-6 | §13 correction, item 15 | Confirmed as of 1.28, refused by name: two polymorphic bases (`ParserClass.cpp:1924-1937`), a virtual base with virtual functions ("vtordisp fields and thunks of its own"), rethrow (`ParserExprNew.cpp:1142`), try inside catch (`ParserStmt.cpp:1438`). A function-try-block is **accepted** at `-S` on target 1 (not recorded in `Version.h`; runtime not checked). Items 18-19: not recorded as fixed in 1.26-1.28; 19 confirmed as of 1.28 (no instantiation chain in any diagnostic; first-error abort `Source.h:56`) | non-blocking (confirms the text) |
| D-7 | §3.1 | The preprocessor is kept as cpp11's line-and-string one. As of 1.28 that leaves one phase-3 defect (D-5 c). My 1.25 recommendation of a token preprocessor should **not** become a requirement: requirements say what (conformance to [lex.phases] 1-4, diagnostics that point at the macro use), the design says how. But §3.1 should (i) put comment stripping in phase 3 on the M1 gap list, (ii) list the string preprocessor as a candidate for replacement by a token phase 3/4 when `--health` or diagnostics need macro-use locations (M8+), and (iii) say the `--dump=tokens` boundary is after preprocessing | non-blocking |
| D-8 | §15 item 7 (R7.1) | Confirmed inconsistency. GRIDE ships `toolchain/Windows/LINK` (3,444 lines; PE32+; `/DEBUG` writes a PDB via `pdb.cpp`, `link.h:163-164, 189-192`; `MASM_LINK_Reference.html:27, 76, 86`); GRIDE Design draft 0.5 line 458 "link writes no PDB" is stale since `cf1e607`. R7.1's "Not built: own ... Windows linker" contradicts the toolchain. Proposed R7.1: "Kept: GRIDE's `link` (link.exe-compatible, PE32+, PDB) as the toolchain's linker; Microsoft `link.exe` as the oracle for R9.3 and for mixed links with cl objects; cpp11 2 builds no linker." §6's "link.exe ... kept (R7.1)" then names both, and §11's link rows run through both. P2 helps: GRIDE's `link` already consumes masm's objects, so `CoffEmitter`'s objects (same writer) link with it by construction | non-blocking for P2; **should be an amendment row in §13**, not only an open item |
| D-9 | §12 principle vs M2/M3 | With P1, M3a could overlap M2; if it does not, the M2 adapter lives from M2 step 2 to M3b - still within two milestones, but say so (exception 3) or overlap M3a with M2 | non-blocking |
| D-10 | §12 M6 (a) vs P1 | Bottom-up for targets 1/3, top-down for target 2 (section 2.3) | non-blocking |
| D-11 | §11.1 "What does a valid program print" | cl at `/std:c++14 /permissive- /Zc:__cplusplus` is fine. Add `/Zc:twoPhase` is implied by `/permissive-` - no change; just note cl's default of no two-phase so the oracle is run with the flag | non-blocking |
| D-12 | §0 review record, Draft 2.2-2.4 row | "`tools/feature-sweep` validates with `clang++ -std=c++11 -pedantic-errors`" → "can validate (`--clang`)" | non-blocking |

**On `__cplusplus` and the §13 correction (reporting, not reopening).** The decision stands on its basis. Two things should be on record beside it: (1) as of 1.28 the silent divergences are three, not two (D-5); (2) the two-phase divergence can be turned into a diagnostic cheaply before M8 - a replayed non-dependent name that binds to a declaration positioned *after* the template's definition is detectable from token positions during replay, and a warning there converts the silent case into an explicit one. I would add that to the M1 gap list. The feature-sweep's "87 of 96 compile" counts compilation, not behaviour; the two-phase and implicit-noexcept probes belong in the conformance suite as *run* tests.

## 6. What 1.29 could change

All of the above is as of 1.28. Before Draft 2.5 is relied on, check 1.29's `Version.h` for: (a) any MIR/opcode-table or allocator change (P1's "stage 2 in 1.28" and the stream-typing estimate); (b) `-g` on the MASM path or `.FILE`/`.LINE` emission by cpp11 (P2 condition 1 - if 1.29 made cpp11 emit `.FILE`/`.LINE`, masm's `codeview.cpp` is then on cpp11's route and the CodeView choice must be re-made); (c) the three silent items of D-5 and the four refusals of D-6; (d) `feature-sweep`'s counts; (e) any change to `Backend.cpp:104`.

## 7. Sign-off

> **Fable 5.1 agrees to `Compiler_Design_Final.md` Draft 2.5, subject to:** (1) P1 conditions (1)-(3) of section 2.5 and P2 conditions (1)-(4) of section 3.5 applied in §4.1, §6, §6.1 and §12; (2) the §13 correction naming three silent divergences as of 1.28 - two-phase lookup, implicit-`noexcept` destructors, comments reaching `#`/`##` in macro arguments; (3) R3.3's basis reworded as cpp11's *mechanism* bounded by R3.1 and the oracle policy, since cpp11's practice refuses C++11 by name; (4) R7.1 amended in §13 to name GRIDE's `link` as the toolchain linker and Microsoft `link.exe` as the R9.3 oracle. The non-blocking flags D-4, D-7, D-9 to D-12 and the figure corrections of section 4 are for Draft 2.6. - Fable 5.1, 2026-10-22

## Appendix A. The target-1 mnemonic census (cpp11 1.28, `-S -arch x86_64-windows -masm=masm`)

Inputs: the 899 `.cpp` cases of `Compiler-Cpp-Optimize/tests/cases`; 1,096 listings at -O0/-O2 (the rest are the suite's refused `.error` cases), 548 more at -O1 for the size ratio. Distinct instruction mnemonics, 72, with counts; every one is in masm's tables:

`MOV 215705, LEA 78133, PUSH 76804, POP 76434, MOVSXD 53394, ADD 43322, CALL 29323, SUB 22256, RET 14114, CMP 11090, JMP 8701, JE 6136, XOR 6023, IMUL 5490, MOVSD 4734, MOVZX 4641, MOVSX 1969, TEST 1858, JNE 1705, NOP 1678, SHL 1405, AND 1118, SETE 1068, SETNE 816, IDIV 731, NEG 687, SETL 686, JGE 637, MOVQ 628, JL 550, SAR 526, SHR 488, CVTSI2SD 447, CDQ 438, DIV 324, MOVAPD 294, CQO 293, JLE 244, SETG 233, JBE 174, CVTTSD2SI 166, MULSD 164, JAE 147, SETA 140, SETB 135, JB 132, ADDSD 121, OR 118, JG 107, PXOR 83, MOVSS 81, SETGE 78, SETLE 57, UCOMISD 54, DIVSD 30, XORPD 28, SETAE 27, MOVD 27, SUBSD 23, SETBE 22, JA 20, CVTSS2SD 20, JNS 13, SETNP 12, SETP 10, CVTTSS2SI 6, ADDSS 6, UCOMISS 5, CVTSI2SS 5, CVTSD2SS 4, SUBSS 3, XORPS 2`

x87: 0. `rep`: 0. Operand/directive forms: `imagerel` 63,200; `ASSOCIATIVE` 17,050; `COMDAT` 10,934; no `SHORT`, no `PROC FRAME`, no `.FILE`/`.LINE`. Instruction totals over the 548 cases with all three levels: O0 455,504; O1 185,796; O2 221,639.

## Appendix B. Probes (run with a clang-built cpp11 1.28; behaviour probes executed natively on arm64-darwin, target-1 probes compiled with `-S -arch x86_64-windows -masm=masm`)

| Probe | Result | Conforming |
|---|---|---|
| two-phase: `g(long)` before, `g(int)` after, `f(T){ g(1); }` | prints `int` | `long` |
| `S(a /* c */ b)` with `#define S(x) #x` | `a /* c */ b` | `a b` |
| `noexcept(new int)`, `noexcept(T())` with `~T() noexcept(false)`, `noexcept(U())` with `~U() {}`, `noexcept(throw 1)` | `0 0 0 0` | `0 0 1 0` |
| `noexcept(W())` trivial, `noexcept(U())`, `noexcept(X())` with `~X() = default` | `1 0 1` | `1 1 1` |
| `struct C : A, B` with both bases polymorphic (target 1) | refused by name (`ParserClass.cpp:1924-1937`) | accepted |
| `struct D : virtual V` with `V` polymorphic (target 1) | refused by name ("vtordisp fields and thunks") | accepted |
| function-try-block (target 1, `-S`) | accepted | accepted |
| `-g -masm=masm` (target 1) | refused: "writes no such thing for x86_64-windows in the MASM spelling" (`Driver.cpp:1087`) | - |
