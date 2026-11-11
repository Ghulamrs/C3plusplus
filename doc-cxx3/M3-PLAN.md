# M3: the IRs, the optimizer and the allocator for targets 1 and 3

Written 2026-10-25 by Fable 5.1 as review point 1's deliverable, from `7d593d5` (main). Design section 12's M3
row and 12.1 say what M3 is; proposal P1 (section 4.1), decided by the M2 closing review
(`M2-CLOSE-REVIEW-FABLE-2026-10-25.md`, "Who takes M3"), says the order; `M3-IR.md` beside this file defines
the two IRs. Claude Opus 5.5 implements; Fable 5.1 reviews at the six points placed below. This note is in the
shape of `M2-PLAN.md`: what a commit may change, how each step is judged, and a "where it stands" section Opus
appends to as steps land - the numbers written there are what "M3 closed" is measured by.

## What M3 is, in P1's order

Section 12's row: *SSA IR, optimizer, MIR and graph-colouring allocator for targets 1 and 3 together. The adapter
and the old x86 text optimizer are selectable during M3 and deleted at its end; -O2 self-host; stage 2 = stage 3
at -O2* (R4.1, R4.2, R5.2). Section 12.1 adds what M3 takes from M2: a typed tree lowered without `Ast.h`, with
the compiler-written functions (the 807 the census counts by kind) lowered from it as M3's first item, and the
frame layout, cleanup regions and EH tables made by M3's own lowering from the typed tree's scopes.

P1 splits it, and the split is the order the risk is taken in:

- **M3a - MIR below the existing walkers.** `docs/OPTIMIZER-IR.md`'s stages 1 and 2 finished: every function of
  both x86 targets held as MIR at every level and written back byte for byte; one opcode table carrying every
  allocator constraint; register classes from a target description; allocation over the whole function with its
  funclets; the string-keyed optimizer (`OptIr.h`'s `Instr::m`) retired. Nothing above the walkers changes, so
  nothing of M2's deliverables is touched, and the allocator - the riskiest part - lands on input whose correct
  output is known.
- **M3b - the SSA IR above.** The typed tree lowered to SSA (block arguments, `invoke`, funclet pads); the -O1 and
  -O2 passes of section 4 on it; the walkers turned into instruction selectors from SSA into MIR, their ABI code
  re-pointed and not rewritten; then the adapter, `-front=typed`, `-check=adapted` and the parser-to-`Ast.h` path
  deleted in one commit; then the -O2 self-host with stage 2 = stage 3.

**What is not M3**, by section 12's two exceptions: arm64-darwin keeps its walker and `A64Peep` (M5); tms6747 keeps
its walker, `C6xSched` and `C6xPipe` (M6). Those files are not edited in M3, and every gate below checks that
their output did not move. M4 (the encoder, R4.4) starts once M3a.1 has fixed `MInst` - review point 2a - and
runs beside the rest of M3; it is not planned here.

## C1 to C6 ride alongside, and are not M3 steps

The closing review's six conditions are being done by Opus now, each its own commit, each gated as M2's commits
were (`make test`, the emit golden, `identical.sh`, `tools/self-host`). Two of them are prerequisites of a step
below and are named there: **C5** (the emit golden re-recorded on main: 624 of 1,924 at `fbf61d3` are `125f31a`'s
spelling flip) before M3a.1 is judged by the golden; **C2** (the record of a Comma-wrapped virtual or
member-pointer call copied to the wrapping node) before M3b.1 lowers a function holding one. C1, C3, C4 and C6
block nothing and are not waited for.

## What a commit may change, and how it is judged

M3a's commits change what is emitted at -O1 and -O2 on purpose and at -O0 never; M3b's change nothing until a
`-front=ssa` flag is given, and at the deletion change nothing at all. Every commit is judged by the rows that
apply, and the Windows rows are judged on the box (zeeshan, cl 19.51, `-masm=masm` with the project's masm and
LINK; the GNU spelling by clang where a row says so):

| gate | where | what must hold |
| --- | --- | --- |
| `tools/identical.sh REF NEW` | Mac | REF is a build of the base named in the step; every case and the 16 Compiler++ units (`~/cxxopt-build/cpp`) at -O0, -O1, -O2 for x86_64-windows in both spellings and x86_64-linux at -O1/-O2: 5,352 outputs. `LEVELS=0` where a step changes -O1/-O2 on purpose. **One line is owed to it first**: x86_64-linux at -O0 is left out (the `[ "$L" = 0 ] ||` line of `identical.sh`), and M3a.1's round trip wants it in |
| `tests/emit.sh` against the golden | Mac (3 targets), Linux (4, and `--o2` for tms6747) | 0 changed, or exactly the files the step says and why. arm64-darwin and tms6747 must read 0 at every M3 step |
| `make test` | Mac, Linux (g++ 11.5, `-Werror`) | run (987/0), emit, names (653/0), overload, syntax, typed (647, 0 differ), adapted, exclusions 0/0, comment cap 0, memory gate held |
| `tools/self-host`; `tools/self-host out -O2` | Mac, Linux | stage 1 = stage 2 at -O0 at every step; at -O2 from M3a's close (R5.2: stage 1 at -O2 once M3 lands) |
| Windows cases | box | `tools/windows/run-cases.cmd` both spellings at -O0 and with `CXX1_CASES_FLAGS=-O2` (955/0 each at M2.18, 33 skipped); `throw-check`, `catch-check`, `sret-check`, `winlink-check`; and with `-front=typed`/`-front=ssa` where the step says |
| Windows self-host | box | `tools/windows/self-host.cmd`, 70 of 70; the GNU spelling by clang until C6's condition lifts (`-masm=masm` cannot assemble cxx3's own `thread_local`, `Masm.cpp:258-264`) |
| bench | box | `tools/windows/bench.cmd -New <step> -Base <main at 7d593d5>`: `bench-kernels.cpp`'s six kernels, medians of interleaved rounds, checksums equal; no kernel slower than the base at -O2, and .text at -O1 not larger. The 2026-09-27 numbers to beat are in CLAUDE.md ("The four boxes measured again"): Windows total 383 ms against cl's 297 |
| determinism | Mac | `tools/determinism.sh`: two runs, same bytes; `-dump=mir` and `-dump=ssa` included once they exist |
| tms6747, arm64 | Linux, Mac | `tests/tms6747.sh` at -O0/-O2 on vm6747 and sim6747 (620/0 at M2.18), `tests/ti-link.sh`, `tests/asm6x.sh`; the C6000 and arm64 files unchanged by `git diff --stat` |
| memory gate | Mac | `tools/memory-gate --check`: R6.6, the count only goes down; new code is handles and containers, `make_owned` where an owner must be polymorphic |

A commit stays local until the Windows rows that apply have passed it; a step is called done only in "Where it
stands" below, with the numbers.

## M3a - MIR below the walkers

What exists at `7d593d5`, so the steps are read against it: the x86 optimizer holds a function as a stream of
`opt::Entry` - an `Instr` whose mnemonic is a `std::string` and whose two operands are parsed `Operand`s, a
`Label`, or an `Event` holding a closure that replays a `Spelling` call (`OptIr.h:65-83`, `Optimizer.cpp:17-64`).
It exists only at -O1 and -O2 (`X86_64Linux::setOptimize`, `X86_64Linux.cpp:1560-1566`); at -O0 the walker writes
straight into the spelling. The passes run through a GCC-shaped manager (`OptPipeline.cpp:220-247`); webs,
promotable locals and a Chaitin-Briggs allocator over the general registers are in the pipeline (`Webs`,
`LocalsPass`, `Allocate`, `OptPipeline.cpp:93-123`), so `OPTIMIZER-IR.md`'s stage 2 is in. A funclet is held as a
stream of its own and gets no frame pass (`Optimizer::funcletBegin`, `Optimizer.cpp:94-102`). Both spellings
render from the same `Op` (`GnuSpelling::op`, `Spelling.cpp:8`; `MasmSpelling::render`, `Masm.cpp:212`). The MIR
of `M3-IR.md` part 2 is this, made data all the way down, at every level, for the whole function.

| step | what | gate |
| --- | --- | --- |
| M3a.1 | **MIR as data, at every level.** `MInst`, `MOperand`, `MEntry`, `MFunction` as `M3-IR.md` 2.2-2.5 define them, in `src/optimizer/Mir.h`; the mnemonic string becomes an opcode id from the one table (2.6); every `Event` closure becomes a `Directive` carrying its arguments as data, bar the five `atOutput` text-lifting sites (`X86_64Linux.cpp:2017, 2152, 2544`, `Masm.cpp:891, 977`), which stay `Deferred` until M3a.4; the holder is created at -O0 too and runs no pass there; `-dump=mir` (R4.6) and `-check=mir`. **The trap**: `optimizing()` (`X86_64Linux.h:158`) must keep meaning "passes run" - `tempsInSlots()` (`:246`), `copiesByString()`, the inliner - and not "a holder is in front", or -O0 changes; and a realigned frame, written past the optimizer today (`Optimizer.cpp:233` asserts `realign == 0`), goes through the holder with `realign` on its prologue directive | `identical.sh` with linux -O0 added: 5,352 + the linux -O0 outputs, **0 differ** at every level; emit golden 0 of 1,924 (after C5); Windows cases 955/0 at -O0 and -O2, both spellings; `-check=mir` over the corpus reports 0 differ; the table complete: `CPP11_MIR_STRICT=1` over every case and Compiler++ at -O0/-O2 on both x86 targets meets no mnemonic the table lacks; determinism of `-dump=mir` |
| **RP2a** | **`MInst` is fixed**: Fable reads M3a.1 and `M3-IR.md`'s part 2 is marked fixed or amended; M4 may start on it. A stop | |
| M3a.2 | **The target description and register classes.** `mir::TargetDesc` for x86-64 (2.8): the general class less rsp and rbp, the xmm class, the convention from the `Abi` as `conventionOf` reads it today; a pseudo carries its class; `Webs`, `Locals` and `Allocator` colour any class, so a floating local (`Local::floating`, never promoted today, `OptPasses.h:50`) and an xmm web are allocated too; the one `PseudoInfo` vector replaces `homes` + `slots` | `identical.sh LEVELS=0` 0 differ; emit golden 0; run.sh at -O1 and -O2 on Mac and Linux; Windows cases at -O2 both spellings; bench: no kernel slower, .text at -O1 not larger; `tools/self-host out -O2` tried and its count recorded |
| M3a.3 | **One table and no string tests.** Every `isMovQ`, `m == "lea"`, `starts(m, "cvt")` and the MASM spelling's own 156-row `kRules` (`Masm.cpp:109`) read the one table (the MASM name a column of it); the "quirk" rows `OptTable.cpp:114-120` records (`addq` unknown, `sal` a shift but opaque, `testl` not explicit) each settled by a measurement, one commit per row that changes output, `identical.sh` naming the files | `identical.sh` at all levels: 0 differ for the renaming commits; for a quirk commit, the files it names and nothing else; Windows cases at -O2 both spellings |
| M3a.4 | **Funclets as blocks of their function.** P1's decision applied: the whole function is allocated before its funclets are outlined, and a value live into a funclet stays in a frame slot (`SharedSlots`, `OptPasses.h:60`, already says which). A funclet's entries become blocks of the parent's `MFunction` marked funclet, with `FuncletBegin`/`FuncletEnd`/`FuncletLeave` directives in place of the lifted text; emission writes them to `.text$x` after the function; the five `Deferred` closures go, `Optimizer::funcletBegin/End` go, `funclet-no-frame-passes` is no longer declined | Windows cases 955/0 at -O0 and -O2, both spellings; `catch-check`, `throw-check`, `winlink-check`; `identical.sh LEVELS=0` 0 differ; x86_64-linux identical at every level (it has no funclets); emit golden 0; `-g -masm=gnu` and `-g -masm=masm` on `tests/m10/*.cpp` through cdb as M10 W1-W5 measured them (the funclet's CodeView records must still be cl's shape) |
| M3a.5 | **The retirement.** `OptIr.h`, the `std::string` mnemonic, `Operand`, `Entry::Event` and the `opt::` shapes they held are gone; `Mir.h` is the one IR header; the passes are MIR passes in name and in fact; `docs/OPTIMIZER-IR.md` gains a line saying stages 1 and 2 are done and where; `CPP11_DUMP_MIR` becomes `-dump=mir` after a named pass | `identical.sh` at all levels 0 differ against the M3a.4 binary; bench against main's binary: the table recorded in "Where it stands"; `tools/self-host out -O2` on Mac and Linux, stage 1 = stage 2; Windows self-host plain and `-O2` (GNU spelling); memory gate down by the `new Optimizer` at `X86_64Linux.cpp:1563` at least |
| **RP2b** | **M3a's gate**: the round trip, the retirement, `identical.sh` and `bench-kernels` on the box. A stop | |

Two things M3a must not do, said now so they are not discovered by a red leg: it must not change what the walker
emits at -O0 (the stack machine, the push/pop discipline, the frame layout) - that is M3b's to replace; and it
must not reach into `Arm64Darwin.cpp`, `A64Peep.cpp`, `Tms6747.cpp`, `C6xSched.cpp`, `C6xPipe.cpp` or `C6xModel.h`
(exceptions 1 and 2). The one shared file those read, `OptCosts.h` (`Tms6747.h:11`), keeps its interface.

## M3b - the SSA IR above

What exists at `7d593d5`: the syntax tree (`src/syntax/`), the typed tree (`src/sema/Typing.*`: a node per syntax
node with its type and, for a name, its `var`), and the record - `Parser::Resolved` (`Parser.h:512-526`), kept in
`resolutions_` and four sibling maps by syntax node, read by the Typer and the adapter and looked up after the
file by nothing. The adapter lowers 1,268 of 7,420 source bodies to `Ast.h` and is the harness; `Ast.h` is the
oracle. The census counts 8,227 functions, 807 of them compiler-written, 0 neither.

| step | what | gate |
| --- | --- | --- |
| M3b.0 | **The record moves onto the typed tree** (`M3-IR.md` 3.10): `TypedNode` carries an index into `TypedTree::resolutions_` (the `Resolved` record moved from the parser, kind for kind), its cast type, its folded value and its literal token; the Typer fills them as it types, from the parser's maps, and the lowering of M3b.1 is built with no `Parser` in reach - its constructor takes the typed tree, the `TypeTable`, the `Target` and the program's data. The fifteen layout maps (`callSlots_`, `blockCleanups_`, `tryFacts_`, ...) are **not** moved: M3's lowering makes its own layout. **C2 is a prerequisite** | `typed.sh` 647, 0 differ, the report unchanged field for field; `adapted.sh` 647, 0 differ (the adapter may keep reading the parser's maps: it is deleted at M3b.7); `identical.sh` 0 differ |
| M3b.1 | **The SSA IR and the first lowering.** `src/ir/`: the data model of `M3-IR.md` part 3, `-dump=ssa`, a verifier (`-check=ssa`: every use dominated by its definition, block-argument arity and types, one terminator a block); the lowering typed tree → SSA for the adapter's subset at -O0 (no mem2reg: every local an `alloca`); selection SSA → MIR by the x86 walker re-pointed (3.11), through M3a's MIR and spellings; behind `-front=ssa`, a function inside the subset takes it and any other the parser's `Ast.h` | run.sh under `-front=ssa` at -O0 and -O2 (M3a's MIR passes run on the selector's output) on Mac and Linux: every case prints its `.expected`; the Windows cases under `-front=ssa` both spellings; `identical.sh` with the flag off 0 differ; the ledger cases (`lifetime.h`) balance under the flag; `-check=ssa` 0 over the corpus; `-dump=ssa` deterministic |
| **RP3** | **The first lowering**: the typed tree to SSA, the `Ast.h` path's output as the oracle (the same `.expected`, the same ledger, the same exit status, case by case), and the record on the typed tree. A stop | |
| M3b.2 | **Every source body lowered.** The subset grows kind by kind until every function with a syntax root takes the SSA path at -O0: members, namespaces, template instantiations by replay, closures, exceptions on both ABIs (pads to Itanium landing pads and to Microsoft funclets, 3.6), dynamic initialisation, `thread_local`, varargs, x87 on x86_64-linux, the frame layout and cleanup regions from the typed tree's scopes. Each growth names what it took and what is left, as M2.12a-p did | the census's `bodies` count all lowered; run.sh under `-front=ssa` at -O0 and -O2 on Mac, Linux and the Windows box; names.sh under `-front=ssa` (653/0: the symbols are Sema's and must not move); `winlink-check`, `catch-check`, `throw-check` under the flag |
| M3b.3 | **The compiler-written functions from the typed tree** - section 12.1's first item. The 807 by kind (implicit destructor 209, default constructor 180, deleting destructor 144, copy or move 97, complete constructor 33 and destructor 23, thunk 20, virtual thunk 29, vector constructor 13 and destructor 25, array destructor helper 8, init function 25, tls init 1) built by `Classes`, `ParserClass.cpp` and `ParserInit.cpp` straight into `Ast.h` today (the 15 `setSynthesised` sites) are built as SSA through an `ir::Builder`, from the class's layout and the typed tree, not from `Ast.h` | the census's `synthesised` count all lowered: `functions = bodies + synthesised`, every one through SSA under the flag; run.sh and names.sh under `-front=ssa` on every box; the two-way link cases |
| **RP4** | **The compiler-written functions**. A stop - **waived by the user 2026-11-10** ("Waive, start round 1"); no review was made | |
| M3b.4 | **-O1 on SSA** (section 4's table): mem2reg writing incoming values as branch operands, constant folding, copy propagation, algebraic simplification, GVN, dead code and dead stores, CFG simplification, jump threading, division by magic numbers (`DivMagic.h` shared); `volatile` as a flag on load and store in place of turning -O off (`Program::usesVolatile`, `Driver.cpp`) - see the open item in `M3-IR.md` 3.7 | each pass ported "when its `--dump=ssa` effect and the benchmark numbers match the old implementation's" (section 4): a probe per pass with its dump recorded; run.sh at -O1 under the flag on every box; .text of the cases at -O1 not larger than main's (bench.ps1 sums it) |
| M3b.5 | **-O2 on SSA**: inlining with budgets (`Inliner` re-pointed from `Ast.h` to `ir::Function`; its `std::map<const Function *, ...>` (`Inliner.h:55`) becomes a vector by `FuncId`, which the determinism lint of design section 2 would forbid as it stands), LICM, counter widening, unrolling; what stays on MIR (fold-index, hoist-xmm-slots, align-loops) says so | bench on the box: no kernel slower than main's -O2, the table recorded; Compiler++'s 129 cases built under `-front=ssa -O2` byte-identical to a clang build's output; run.sh -O2 on every box |
| M3b.6 | **Self-host through SSA.** `tools/self-host` and `self-host.cmd` under `-front=ssa` at -O0 and -O2; `tools/self-host` gains **stage 3** (today it builds s1 and s2, `tools/self-host:23-28`) so stage 2 = stage 3 can be measured | stage 1 = stage 2 at -O0 and -O2 on Mac, Linux and Windows (GNU spelling); stage 2 = stage 3 at -O2 on Linux and Windows |
| M3b.7 | **The deletion**, one commit: `-front=ssa` is the only path; `Adapter.*`, `-front=typed`, `-check=adapted`, `tests/adapted.sh`, `AstDump.cpp`, `Ast.h`'s node classes and `Visitor`, the Walker's statement visitors, the parser's `Ast.h`-building half and its fifteen layout maps, `Parser::alive_` and the cleanup/temporary machinery that wrote regions into `Ast.h`. `Program` keeps its data (globals, strings, rtti, throws) and holds `ir::Function`s. `-check=typed` loses its `Ast.h` oracle here: what stays is the census (`functions = bodies + synthesised`) and the typed fraction, and `tests/typed.sh` then holds that `untyped=` does not grow - the agreement count was the harness's and goes with it | `identical.sh` against the M3b.6 binary **0 differ at every level** - the deletion changes nothing emitted; `make test` with its suites rewritten (`adapted.sh` gone, `typed.sh` kept); self-host on every box; memory gate down; `tools/exclusions --check` 0/0 after the parser's refusal sites moved |
| **RP5** | **The deletion commit**. A stop | |
| M3b.8 | **M3's close**: `tools/self-host out -O2` with stage 3 on Linux and on the Windows box in the spelling C6 names; `tools/verify-three` end to end; the R1.2 measures recorded (below); `docs/OPTIMIZER-IR.md` and CLAUDE.md's optimizer sections brought to what is true | stage 2 = stage 3 at -O2 on target 1 (R9.1, R5.2); every row of the gate table green; the measures table filled |
| **RP6** | **M3's close**. A stop | |

The order of M3b.2-M3b.5 is coverage first and passes after, on purpose: a benchmark or a self-host under a flag
that still routes some functions through `Ast.h` measures a mixture, and "a pass counts as ported when its
`--dump=ssa` effect and the benchmark numbers match" needs the whole program on one path. M3a's MIR passes run on
the selector's output from M3b.1 on, so `-front=ssa -O2` is exercisable from the first lowering and the -O2
self-host can be tried early; whether it holds before M3b.4 is recorded, not required.

## The six review points, placed

| point | after | what Fable reads | stop |
| --- | --- | --- | --- |
| 1 | nothing | this plan and `M3-IR.md` | yes - done 2026-10-25 |
| 2a | M3a.1 | the round trip's numbers, `Mir.h`, the opcode table; `MInst` fixed for M4 | yes |
| 2b | M3a.5 | the retirement, `identical.sh`, the bench table | yes |
| 3 | M3b.1 (with M3b.0) | the first lowering, `src/ir/`, the record on the typed tree | yes |
| 4 | M3b.3 | the compiler-written functions from the typed tree | waived by the user 2026-11-10 |
| 5 | M3b.7 | the deletion commit | yes |
| 6 | M3b.8 | the close: -O2 self-host, stage 2 = stage 3, every box | yes |

"A stop" means Opus does not begin the next step's commits until the review has been read and answered; M3a.2
and M3a.3 are the exception named in the handback - Opus may begin them once M3a.1's gate is green, since
neither changes `MInst`'s shape, and 2a's amendments, if any, are applied before M3a.4.

## What the design requires of M3, item by item

| requirement | where it is met |
| --- | --- |
| Section 12, M3 row: SSA IR, optimizer, MIR, graph-colouring allocator for targets 1 and 3 together | MIR and allocator M3a.1-M3a.4 (both x86 targets share `X86_64Linux` and the pipeline today, `X86_64Linux.cpp:1563`); SSA IR and optimizer M3b.1, M3b.4, M3b.5 |
| the adapter and the old x86 text optimizer selectable during M3, deleted at its end | the text optimizer is replaced in place by M3a (its MIR is a reshaping of the stream it already holds, so there is no second optimizer to select; `-O0` is the control); the adapter stays selectable as `-front=typed` until M3b.7 |
| -O2 self-host; stage 2 = stage 3 at -O2 | M3b.6 builds stage 3; M3b.8 measures it on target 1 |
| 12.1: compiler-written functions lowered from the typed tree, M3's first item | M3b.3 - after M3b.1-2 because the builder they need is the lowering's |
| 12.1: frame layout, cleanup regions and EH tables made by M3's lowering from the typed tree's scopes | M3b.2; `M3-IR.md` 3.10 says what the lowering reads and what it computes |
| Section 4: block arguments, mem2reg writing branch operands, `volatile` on load/store, `invoke` + funclet pads visible to every pass, the -O1/-O2 pass list with its references | `M3-IR.md` 3.4-3.7; M3b.4-M3b.5 |
| Section 4.1 (P1): M3a before M3b; MIR round-trip gate; allocation onto MIR; the retirement against oracle and benchmarks; P1's four prerequisites (throwing-call edges - `Edge::Eh` exists, `OptCore.h:70-77`; pins on an occurrence - done, `Mir.h:24`; one opcode table - M3a.1/3; the inliner's budget - in, `OptCosts.h:33-41`); the funclet decision | M3a as a whole; M3a.4 is the funclet decision |
| Section 5: `MInst`s with operand kinds, virtual registers, register classes and files from a target description; graph colouring lifted off `RAX..R15` onto classes | `M3-IR.md` 2.2-2.8; M3a.2 |
| Section 6: the encoder turns `MInst`s into bytes; M4 starts once `MInst` is fixed; its mnemonic list is "taken from the M0 oracle's -S output, not from the source" | RP2a fixes `MInst`; M3a.1's table-completeness gate (`CPP11_MIR_STRICT`) *is* that list, measured: every row of the table is a mnemonic M4 must encode |
| Section 2: handles, creation-order numbering, a `switch` on kind, dumps at every boundary (`--dump=ssa`, `--dump=mir`), two runs same bytes | `M3-IR.md` throughout; M3a.1 and M3b.1 add the dumps; `tools/determinism.sh` at every step |
| R6.6: the counter only goes down | the gate table's last row; `new Optimizer` (`X86_64Linux.cpp:1563`) goes at M3a.5 |
| R9.1: self-host on target 1 at every milestone's end with its own front end; C6 names the spelling | M3b.8; the Windows self-host rows name the GNU spelling until the project's masm has SECTIONREL or M4 lands |
| R1.2: the measures, each with a target value | the table below; the targets are proposals for RP2b, open item 6 of the design being unsettled |

**R1.2's measures at M3's close**, and what is proposed as each one's target (to be settled at RP2b, not here):

| measure | at `7d593d5` | proposed target for M3 |
| --- | --- | --- |
| conformance count (`tools/feature-sweep`) | 87 compile, 9 refused by name, 0 naming nothing, of 96 | unchanged - M3 is not a language milestone; a row that moves is a defect |
| compile time on the self-host corpus (70 sources, `tools/self-host` wall time, -O0 and -O2) | to be measured by Opus at M3a.1 and recorded as the baseline | at -O0 not slower than the baseline by more than the MIR hold costs, measured; at -O2 not slower than the baseline's -O2 |
| x86-64 run time (`bench-kernels`, Windows box) | 383 ms total at -O2 against cl's 297 (2026-09-27) | no kernel slower than main at any step; at the close, total at or under main's |
| sim6747 cycles | the C6000 is untouched | unchanged, by construction and by the Linux leg's suites |
| `--health` findings | not M3's | - |

## What must not change, and what looks wrong but is load-bearing (rule 6)

- **`Edge::Eh` leaves the call inside its block** (`OptCore.h:68-69`): a split would take from the scalar passes what they know across the call. Keep it in MIR.
- **Rows of the Itanium call-site table are used in registration order and never sorted** (`Walker.h:119`): sorting broke nested unwinding once. The SSA lowering of M3b.2 must produce disjoint regions in the order the walker does (`openRegion` splits the enclosing one), or the table is wrong in a way the suites did not see the first time either - the `temporary-unwind`, `local-in-handler` and `try-in-try` cases are the ones to run first.
- **The pinned occurrence is split with a copy before webs are joined** (`Mir.h:16-18`); the allocator coalesces what it can. A class-aware allocator keeps this.
- **The funclet's frame is the parent's** (rbp from rdx, `X86_64Linux.cpp:2113`), and `SharedSlots` is how the parent's passes learn what the funclet touches. M3a.4 keeps a value that crosses into a funclet in its slot; it does not try to carry one in a register.
- **A Microsoft frame's unwind-help word is one per function, set at entry** (`storeUnwindHelpAtEntry`), and the inlined callee must not set it (`walkBody`, `X86_64Linux.cpp:1992`) - the 1.22 fault. Nothing in M3a moves that store.
- **The walker's temporaries at -O1/-O2 go to frame slots below every frame** (`Optimizer::kTempBase`, `temporaries()`), and at -O0 to the real stack. M3a.1 keeps both as they are.
- **A realigned frame is addressed from rbx** and walks no callee in place (`X86_64Linux.cpp:1591-1608`). M3a.1 carries `realign` through the prologue directive and M3a.2 keeps rbx out of the palette for such a function.
- **`std` is `St`, a `static` function's `L` sits on its unqualified name, a Microsoft builtin of two letters takes an argument slot** - the symbols are Sema's and move in no M3 step; `names.sh` under `-front=ssa` is the row that says so.

## Where it stands

*(Opus 5.5 appends here as each step lands: the commit, the gate rows with their numbers on the boxes they were
measured on, and what was found and not mended, registered in `tests/open/` with the oracle's answer - as
`M2-PLAN.md` was kept. Nothing above this line is edited; a correction is recorded below with its date.)*

**Gate preparation** (2026-10-25, the commit for review point 1). M2's conditions are in: C1-C4 `dae64c4`, C6
`24e8d86`, C5 the emit golden re-recorded at `7d593d5` (1,924 files; local, as the golden always is). REF is a build
of `7d593d5` kept at `~/cxxopt-build/ref-7d593d5/cxx3.exe`. `tools/identical.sh` now takes x86_64-linux at -O0:
REF against main at this commit, **6,021 outputs compared** (the 5,352 and 669 linux -O0), **9 differ, every one a
diagnostic naming `lib/` by its path** - `posix-headers` for x86_64-windows at the three levels in both spellings and
`windows-h-layout` for x86_64-linux at the three, REF's tree being another folder; no assembly differs. **Compile time,
the R1.2 baseline**: the 70 sources of the self-host corpus compiled one at a time with `-S` by a clang -O2 build of
cxx3 on the Mac (M4 Pro), best of three: **23.91 s at -O0, 44.67 s at -O2**.

**R9.1's target-1 gate on the default toolchain** (2026-10-25, outside M3, Opus 5.5). cxx3's MASM spelling now writes
`thread_local` for the project's masm (Windows/MASM `6bcb5d2` relocates `[base + SECTIONREL x]` as SECREL, LINK `959dac2`
writes the TLS directory and the PDB), so `tools/windows/self-host.cmd` no longer names `-masm=gnu`: on zeeshan, stage 1
and stage 2 built in the default `-masm=masm`, 70 of 70 sources the same. The gate-table rows above that name the GNU
spelling (lines 59 and 157) are to be read as the default spelling from here, left as written above this heading;
design section 10, item 5 says so.

**M3a.1, MIR as data at every level** (2026-10-25, Opus 5.5, branch `m3a`, commits `fd051f0` to `5252c0a`; for review
point 2a). Four steps, each measured with `identical.sh` at 0 assembly differences before the next began:
`fd051f0` the holder in front of the spelling at -O0 too, running no pass there (`passesRun()`, not `optimizing()`,
is what the funclet and temporaries paths ask); `4bb6f71` every `Event` closure a `Directive` of kind and arguments,
bar the five text-lifting `Deferred` ones; `857e6d9` the mnemonic an `OpId` into the one table, which gained the
jcc/setcc rows for all 18 conditions, six `cvt` forms and 21 x87 mnemonics; `80ca39f` an operand's text a `StrId`;
`270067f` `-dump=mir[=pass]`, `-check=mir` and `-mir=off`. Two fixes rode with it: `b9628c6`, the holder forwarding
`hasRelro`/`relroSection` (a vtable at -O1/-O2 had been going to `.rodata` since the GOT round, found by the -O0
holder's round trip); and `5252c0a`, the two registries owning their records in deques, so memory-gate stays at 1,063.
**Self-host found two gaps in cxx3, one mended**: an object in an unnamed namespace was exported, and clang writes no
`L` inside one (`40b5bca`, `unnamed-namespace-two-units.cpp`, 45 of 6,039 outputs changed, every line a `.globl`
or `PUBLIC` gone or that `L` dropped); and no reference binds through a conversion operator, so `OpId` and `StrId`
convert by `name()`/`str()` (`d31857c`, `tests/open/conversion-to-reference-binds.cpp`).

The gate, at `210054e` (`5252c0a` merged with main's `eb930cc`, and cl's C4459 fix: `Directive::describe`'s table was
`kNames`, hiding OptIr.cpp's register table, and cl's `/WX` refused it). `identical.sh` against a REF of main plus
`55885d0`, `b9628c6` and `40b5bca`: **6,039 compared, 9 differ, the known nine** (a diagnostic naming `lib/` by its
path), no assembly. Emit golden (recorded at `7d593d5`): 6 of 1,924 changed, `namespace-unnamed` on three targets and
main's three thread-local Windows files. `-check=mir` at -O0 over every case and Compiler++ for x86_64-linux and both
Windows spellings: **1,973 units, 0 differ**. `CPP11_MIR_STRICT=1` at -O0 and -O2, the same sources and targets: **0
mnemonics the table lacks**. `-dump=mir` at -O2 run twice: **1,973 units, 0 differ**. Mac `make test` green,
memory-gate 1,063. zeeshan: cl build, the cases **958/0 (31 skipped) at -O0 and at -O2 under MASM**, `self-host.cmd`
71 of 71 in the default spelling - with the project's masm 6bcb5d2 and LINK 959dac2; the first run, against an older
masm with no `SECTIONREL`, failed the two `thread_local` cases and stage 1, the tool's limit and not the compiler's.
The GNU spelling's row, which the box cannot run for want of clang, was run by another session: the tip compiled
every case `-masm=gnu` on the Mac, the Mac's clang assembled them, zeeshan's link.exe linked and ran them - 619/619
and 340/340 refusals at -O0 and -O2. Linux: g++ build, `run.sh` 990/0 at -O0 and -O2, self-host 71 of 71, `asm6x.sh`
635/0, `tms6747.sh` 621/0 at -O0 and -O2 with every pass also on sim6747, `ti-link.sh` 630 linked, 0 failed, at -O0
and -O2. **Compile time, R1.2's row**: the 70 sources of 7d593d5 one at a time with `-S`, REF and the tip both clang
-O2 builds, three interleaved rounds on the Mac: medians -O0 24.12 s against 24.12, -O2 45.16 against 45.32 (+0.4%);
best of three -O0 22.92 against 23.47, -O2 44.64 against 45.31, the first round run beside a box build. A second
session's run reads -O0 23.97 against 24.18 (+0.9%) and -O2 44.85 against 44.85.

**What part 2 of M3-IR.md says and the code does not, for RP2a to rule on**: the types keep today's names and file
(`opt::Instr`, `Operand`, `Entry`, `Directive` in `OptIr.h`), the rename into `Mir.h` being M3a.5's by the file table
of 2.1; `StrId` is one process-wide registry, written under a lock and read without one as the opcode table is, not a
per-function `StringTable` - a function's dump is still complete, since a dump prints the text; `Directive` has a
`RelroSection` kind the table in 2.4 lacks; and an immediate keeps `unsignedSpelling`, the `imm(unsigned)` past
`LLONG_MAX` the walker writes, which M3a.3 may fold into the table's widths.

**Review point 2a, Fable 5.1, 2026-10-25: agree with conditions** (`doc-cxx3/M3-RP2A-REVIEW-FABLE-2026-10-25.md`).
M3a.1 may merge, `MInst` is fixed as section 4 of the review lists, M4 may start, and M3a.2 and M3a.3 may begin. All
four departures are accepted; **M3a.1's row above says "in `src/optimizer/Mir.h`" and is read, from this date, as 2.1's
file table says: the types keep their `OptIr.h` names until M3a.5's rename**. M3-IR part 2 carries the review's seven
amendments, each marked where it stands. Of the six conditions, 1 (the `OpId` registry's bound check) and 3 (the
amendments, and `-mir=off`'s usage line saying "at -O0") are in the merge; 4 (main's `98eeda3` taken and the Windows
cases run under `-masm=ml64` on zeeshan) before the merge is called done; 2 (`tools/determinism.sh` runs
`-dump=mir`), 5 (`identical.sh` reads 0, REF beside its own tree) and 6 (TSan in RP2b's gate, run on Linux) before
RP2b. The C7 `-S` item is ruled separate: `-S` with no `-o` writes beside the source and a link build's
intermediates go to the current directory - two modes of one binary - and stage 0 and stage 1 agree on all 71
sources' `-S` output on the Mac.

**After RP2a, 2026-10-25.** Main's `98eeda3` is merged into `m3a` (`f6223ba`); on the merged tree the Mac's `make
test` is green, `-check=mir` and `-dump=mir` read 1,973 units 0 differ, and the Linux box reads `run.sh` 990/0 at
-O0 and -O2 and self-host 71 of 71. **Condition 4 is half met.** `run-cases.cmd` gained an ml64 pass
(`CXX1_CASES_SPELLINGS=ml64`, `3e0ed41`), and on zeeshan the merged tree read 895 passed, 63 failed, 31 skipped at
-O0 and at -O2: 58 `LNK2005` (ml64 cannot mark an inline definition COMDAT, so `std::exception`'s copy
constructor collides with libvcruntime's), 4 `A2043` (an identifier too long for ml64), 1 `A2008` (`k7`, a name ml64
reserves, in `const-return-name`) - none a wrong answer, every one the ml64 spelling's known limit. Main at
`98eeda3` under the same pass was cut short when zeeshan was withdrawn; on the 164 cases it finished at -O0 it gives
the same verdict as the merged tree on every one, 14 failing on each. The whole baseline at -O0 and -O2 is owed on
the GRA box before "merged" is written here. **The C7 probe on zeeshan** (stage 0 and stage 1, one and two inputs,
relative and absolute paths, no `-o`): with two inputs both stages write each `.s` beside its source; with one, both
write it to stdout and no file. The two stages agree in every shape, as the review read it.

**M3a.2, the target description and register classes** (2026-10-26, Opus 5.5, branch `m3a`, commits `43a4e8d` to
`623897f`). **No Windows box this round** - the GRA box is unreachable and zeeshan was withdrawn - so the user had M3
qualified on Linux with TMS6747 and on the Mac, and every Windows row is owed. `43a4e8d` (A): one `PseudoInfo` vector
(class, home, slot) in place of `homes` and `slots`. `b8290cb` (B): `mir::TargetDesc`, the register files the webs and
the allocator ask. Both byte-identical: `identical.sh` against a REF of `4eed537`, 6,039 compared, the known nine.
`7747b2f` (C1): the xmm class - webs over the 16 general and 16 xmm registers, a split copying xmm by `movapd`, a
pseudo's class its home's, the allocator colouring each class from its own palette (System V all sixteen, Windows
xmm0-5, no preserved xmm register offered since it would want a 16-byte save), no edge and no coalescing across
classes. `8e89161` (C2): a `float` or `double` local promoted to an xmm pseudo where every access is a scalar SSE
instruction of its width an xmm register can stand in; its moves become `movapd`, and given its slot back it takes
`movsd`/`movss` again (`movapd` to memory faulted 30 cases at -O1 and 20 at -O2 on Linux until it did). `623897f`
(C3): a function returning `float` or `double` tells the optimizer its `ret` reads xmm0 alone, so the address of the
returned local left in rax is dead and the local can leave its slot (a `void` function keeps the whole set: a
Microsoft constructor returns `this` in rax). In a probe of a dot product the sum stays in an xmm register across the
loop where it went through its slot each turn, and a polynomial's three doubles have no slot.

The gate. **Mac**: `identical.sh LEVELS=0` against a REF of `4eed537`, 2,013 compared, the known three; at -O1/-O2,
815 of 4,026 outputs changed, no diagnostic changed, all 546 changed GNU outputs (x86_64-linux and x86_64-windows)
assemble with clang; `make test` green (run.sh 988/0, emit golden the six known files, names.sh 655/0, typed and
adapted 648/0, exclusions 126 sites 0/0, comment cap 0), memory gate held at 1,063. **Linux** (g++ 11.5): run.sh
990/0 at -O0, -O1 and -O2; `tools/self-host` 71 of 71 at -O0 and at -O2; `asm6x.sh` 635/0; `tms6747.sh` 621/0 at
-O0 and -O2 with every pass also on sim6747; `ti-link.sh` 630 linked at -O0 and -O2. **Bench stand-in** (Linux,
`bench-kernels.cpp` compiled for x86_64-linux on the Mac, assembled and run there, 11 interleaved rounds, checksums
equal): no kernel slower - base and new both fib 10, sieve 94, matmul 15, isort 35, hash 327, virtual 211, total
692 ms, against g++ -O2's 533; these kernels' doubles were already held by `hoist-xmm-slots`. **.text** over every
case, assembled by clang: x86_64-linux -O1 873,528 to 871,072 (-0.28%), -O2 -0.33%; x86_64-windows -O1 951,369 to
950,538 (-0.09%), -O2 -0.11%. **Compile time**, the 71 self-host sources one at a time with `-S -arch
x86_64-linux`, three interleaved rounds on the Mac, best: -O0 21.24 s against 21.06, -O2 409.31 against 413.25
(+1.0%, the webs now spanning 32 registers). **Owed**: the Windows cases at -O2 in both spellings, `bench.cmd`.

**Found, and owed to R1.2**: the compile-time baseline recorded for M3a.1 (23.91 s at -O0, 44.67 s at -O2) must have
been measured for the Mac's own target, arm64-darwin, whose optimizer is the text peephole - for x86_64-linux the
corpus takes about 410 s at -O2, at `7d593d5` as at this commit, and four sources take 339 s of it:
`ParserTopLevel.cpp` 180 s (179 at `7d593d5`), `ParserType.cpp` 75, `ParserExpr.cpp` 48, `ParserStmt.cpp` 37. The
x86 optimizer is super-linear in a function's size somewhere; R1.2's row is to be re-recorded for x86_64-linux and
the cause found.

**No Windows box this round either** (2026-10-26, Fable 5.1, branch `m3-fable` from `478f504`): the GRA box is
unreachable and zeeshan withdrawn, so every Windows row below - `run-cases.cmd` at -O0 and -O2 in both spellings,
`catch-check`, `throw-check`, `sret-check`, `winlink-check`, `self-host.cmd`, `bench.cmd`, cdb on the -g cases - is
**owed**, written as owed and never as passed. The stand-ins the coordinator set: x86_64-linux identical at every
level, every x86_64-windows GNU output assembled by `clang -c -target x86_64-pc-windows-msvc`, `identical.sh` at
every level, and the bench kernels run on the Linux box. Each step was qualified on Linux with TMS6747 first and
then on the Mac; the Linux numbers are a leg's, run from a `git archive` of the commit named.

**R1.2's finding, mended first** (2026-10-26, `6552790`). The super-linear part was the allocator's `coalesce()`,
which recounted every node's degree over the whole interference matrix after each merge - O(merges x n x n/64) -
where a merge moves only the merged node's degree and the degrees of the neighbours the two shared.
`significant()`, the live scans of `buildInterference` and `simplify`'s selection walk set bits rather than every
node (`lowestBit` is written out: neither cl nor cxx3 has a builtin for it). The self-host corpus for
x86_64-linux at -O2, one source at a time with `-S` on the Mac (M4 Pro, clang -O2 build): **413 s to 62 s**;
`ParserTopLevel.cpp` 180 s to 7 s; -O0 unchanged (24.6 s against 24.2). `identical.sh` against a REF of
`478f504` at -O0, -O1 and -O2: 6,039 compared, 0 differ. **R1.2's compile-time row is re-recorded for x86_64-linux
at M3a.5 below.** One REF compile over `identical.sh`'s 60 s CPU limit on the old allocator
(`exception-table-large` at -O2, 33 s against 8 s) was re-run by hand and is identical; later REFs are built from
this commit or after it, so the limit is not met again.

**M3a.3, one table and no string tests** (2026-10-26, `8f79594`, `7040edf`, `f5967f9`, `a601975`). `8f79594` (A):
`OPT_OPCODES` in `OptTable.h` is the one list - a row's enumerator (`mn::Mov`), AT&T name, kind, flags, suffix
width, condition, and the MASM name and PTR width `MasmSpelling`'s own 156-row `kRules` carried - and the table and
the enumerators are made from the same macro, so neither drifts; the 38 rows only `kRules` knew (`addb`, `incq`,
`nop`...) join as `Unknown` with no flags. Three flags replace the spelling tests (`kPlainMov` for `isMovQ` and its
kin by width; `kSse` and `kEscape0F` for `OptAlign`'s size estimate); the condition is a column read by
`conditionOf`/`inverse`/`jcc`/`setcc` as `Cond` values; `OpId` has no string comparison and no `char *` constructor
left, its one string door the walker's `ins` and the strict check; every `m == "lea"`, `is(m, {...})`,
`starts(m, "cvt")`, `substr + "l"` and `"j" + cc` in the passes reads the table (131 edits); `MasmSpelling` asks
the table for the name and width it used to look up. `identical.sh` against a REF of `478f504` at all three
levels, REF beside its own tree: **6,039 compared, 0 differ** - which is RP2a's condition 5, the nine lib-path
differences gone. `tools/determinism.sh` runs `-dump=mir` for the two x86 targets at -O0 and -O2 beside each
compile (RP2a's condition 2): 16,414 outputs, 3,980 of them `-dump=mir`, 0 differ. **The quirk rows, one commit
each where output could move**: `7040edf` (B) `addq`/`subq` Rmw E|I|R(|Z) 8, `sal`/`rol`/`ror` Rmw S,
`testl`/`testq` Compare E|R at 4 and 8 - measured over every output `identical.sh` writes, none of the six is ever
written by the walker or a pass, 6,039 / 0; `f5967f9` (C) `testb` Compare E|R at 1 - 21 outputs write one, all a
bool tested against itself, 6,039 / 0 against a REF of `7b3f339`; `a601975` (D) an unsigned immediate keeps the
walker's spelling at every level (RP2a departure (d)): `improve` had cleared the mark before the passes, so -O1 and
-O2 wrote the value signed - against the same REF **6,039 compared, 92 differ, every one at -O1 or -O2, in 18
sources** (`bitfield-zero-width`, `catch-by-reference`, `constructor-template`,
`constructor-template-in-member-template`, `handler-exit-ms`, `include-c-wrappers`,
`long-double-literal-exact-digits`, `longlong-switch-bitfield`, `pipelined-reduction`, `runtime-shapes`,
`string-stoi`, `template-id-qualified-base-call`, `template-nontype-numeric`, `unsigned-from-double`,
`user-defined-literal`, Compiler++'s `Semantic.cpp` and `VM.cpp`); of their 163 changed lines every one is an
immediate whose two spellings are one value modulo 2^64 (`movabs $-9223372036854775808` to
`movabs $9223372036854775808`), checked mechanically, no file changed length, and all 62 changed GNU outputs
assemble with clang for their target. **Departures from `M3-IR.md` 2.6, each recorded there**: the `roles`,
`implicit`, `classes` and `fixed` columns are not added - the roles follow from the kind and two flags by one rule
(`rolesOf`), the implicit registers of five kinds are `effectsOf`'s `switch`, a pseudo's class is its register's and
`mir::TargetDesc` holds the files, and `kShift`/`Div` are the pins - so each column would have been a second copy
of a rule already in one place; the MASM inference for a store of an immediate with no width to take it from, and
`movq` with an xmm operand keeping its name, stay in `Masm.cpp` beside the `masm` column (2.11's open item). Mac
`make test` green at each commit; memory gate 1,063 then 1,062 (below).

**M3a.4, funclets as blocks of their function** (2026-10-26, `7b3f339`). A Microsoft handler is no longer a stream
held beside its function: the walker's `beginFunclet`, `funcletLeave` and `endFunclet` become `Spelling` calls -
`funcletBegin(symbol, entryLoc)`, `funcletLeave(label)`, `funcletEnd(resume)` - which the holder keeps as
directives in the one stream, so the funclet's instructions are blocks of the function's flow graph: `Flow::build`
begins a block entered from nowhere at `FuncletBegin` and ends one that returns at `FuncletEnd` or `FuncletLeave`
(`Block::funclet` is the nesting depth, in place of 2.5's `FuncletRange` vector), a block before a funclet falls
through to the next block of its own depth, and `removeUnreachable` comes back to the state before the funclet at
its end (the first draft dropped live `printf` calls after a `try` at -O2). The passes run over the whole, the
frame passes included (`funclet-no-frame-passes` is gone): the allocator colours the function with its funclets,
**forbids every preserved register to a pseudo a funclet names** (2.9's open question, decided as cl does: its
funclets are leaf-shaped) and asserts that no pseudo spans a funclet's boundary; the frame slots a funclet touches
are shared at flush (`SharedSlots::addAccessOf`), so a value live into one stays in its slot (P1's decision) and no
store to it is dead. The text is the spelling's: `CoffSpelling` and `MasmSpelling` compose the head, tail and
unwind data `closeFunclet` composed, lift the body out of their own output at `funcletEnd`, and hand the funclets
over through `takeFunclets()`/`takeFuncletPdata()` where `emitExceptionTables` appended them; the five text-lifting
closures, the code generators' marks and buffers, `Optimizer::funcletBegin/funcletEnd/defer` and the `Deferred`
kind are gone, and `emitTlsWrappers` is called where it stood. `MasmCodeGen`'s own copies of `funcletSymbol_`,
`funcletIndex_`, `funcletKind_` and `funcletOuter_` went with it - they shadowed the base's, which is how a
funclet's CodeView record lost its symbol in the first draft and `-g -masm=masm` aborted. **Mac**, against a REF of
`6552790`: `identical.sh` at -O0, -O1 and -O2 - 6,039 compared, **888 differ, every one x86_64-windows at -O1 or
-O2** (the funclets' bodies now through the passes: dead loads of a slot's address gone, a handler's copy in r11
where it took rcx and a reload); x86_64-linux identical at every level, both Windows spellings at -O0; all 1,304
Windows GNU outputs at -O1/-O2 assemble with clang; under `-g` at -O0 `tests/m10/*.cpp` byte-identical in both
spellings, and two funclet cases differ, both fixes - a handler whose body opens a cleanup funclet had lost its
entry location, and a handler enclosing a `try` took the inner handler's, one member having held the location for
every open funclet; `make test` green (run.sh 988/0, emit golden 0 of 1,930, names 655/0, typed and adapted 648/0,
exclusions 126 sites 0/0, comment cap 0); determinism 16,414 outputs 0 differ; memory gate 1,062, down one.
**Owed**: `run-cases.cmd` at -O0 and -O2 in both spellings, `catch-check`, `throw-check`, `winlink-check`, the -g
cases through cdb - the funclet's CodeView records are byte-identical to the base's where the base had them right,
and the two fixes are measured against the location the handler's own source line names, not against cl.

**Linux, at `a601975`** (the M3a.3 D tip; g++ 11.5, the leg from a `git archive`): `run.sh` 990/0 at -O0, -O1
and -O2; `tms6747.sh` 621/0 at -O0 and at -O2 on vm6747 with every pass also on sim6747 (2 skipped for exceptions, 7 for a 64-bit long, 19 not for this target); `ti-link.sh` 630 linked by TI's lnk6x, 0 failed, at -O0 and at -O2; `asm6x.sh` did not run - the leg script named no `C6XDIFF` (`~/ti6x/ASM6x/tests/c6xdiff.py` is where it is), mended for the next leg. `tools/self-host` at -O0 **stopped at stage 1**: stage 0 refused `OptValues.cpp`'s
`Cond cc = kNoCond;` - a default member initialiser naming a namespace-scope enumerator, which cxx3 reads with no
namespace open ([class.mem]/4; `kNone` written `n::kNone`, or in a constructor's mem-initialiser, is found). The
line M3a.3 A wrote is qualified `opt::kNoCond` at M3a.5, and the defect is registered:
`tests/open/member-initialiser-namespace-enumerator.cpp`, clang `ACCEPT -1 0`. No suite had the shape: the
self-host corpus is what met it, which is what R9.1 is for.

**M3a.5, the retirement** (2026-10-26, `df365f0`). `OptIr.h` is `Mir.h` and `OptIr.cpp` is `Mir.cpp` (`git mv`);
the types take part 2's names - `MOperand`, `MInst`, `MDirective`, `MEntry` (`EntryOf<MInst, MDirective>`, kinds
`Inst`/`Label`/`Directive`, the payload `dir`), `MStream`, `MFunction`, `RegClass` and `mir::TargetDesc` beside them
- and **stay `opt::`**, the passes' namespace (the departure from 2.2's `namespace mir`, recorded under 2.5 with its
reason: a second namespace for the data the passes hold would put a qualifier on every line of every pass for no
information; `mir::` keeps what colours - `TargetDesc`, `Webs` in `MirWebs.h`, `Locals`, `Allocator`). 847 renames
over 30 files, none a change of shape: `MInst` is as RP2a fixed it (`m`, `a`, `b`, `int operands`, `args`,
`exactArgs`). `-mir=off`, `-check=mir`, `CodeGen::setMirOff` and the bypass in `X86_64Linux::setOptimize` are gone -
`identical.sh` is the round-trip gate from here - and `CPP11_DUMP_MIR` with them: `-dump=mir=<pass>` or `=all`
through `PassManager::dumpAfter` is the one dump. The two `new`s at `X86_64Linux.cpp:1563-1565` are `make_owned`
(memory gate **1,062 to 1,060**; the pipeline's 21 `new`s in `OptPipeline.cpp`, `OptCosts.h`'s two and
`OptFunction.h`'s `new Loops` stay, `make_owned` living in `Ast.h`, which the optimizer does not include - a header
of its own is the next step down). `ide/generate.py` regenerated the projects; `docs/OPTIMIZER-IR.md` says stages 1
and 2 are done and where and that the x87 stack is not modelled (2.11), `OPTIMIZER-ARCHITECTURE.md`'s file table and
`GCC-SOURCE-STUDY.md`'s dump row name the new files and flag. **Mac**: `identical.sh` against a REF of `a601975` at
-O0, -O1 and -O2, **6,039 compared, 0 differ**; `make test` green (run.sh 988/0, emit golden 0 of 1,930, names.sh
655/0, overload 44/0, typed and adapted 648/0, exclusions 126 sites 0/0, comment cap 0); determinism 16,414 outputs
(3,980 `-dump=mir`) 0 differ; `tools/self-host` 71 of 71 sources the same from stage 1 and stage 2 at -O0 and at -O2. **Linux** (g++ 11.5, the leg from a `git archive` of
`df365f0`): **queued and not yet read at this commit** - `~/linux-leg.sh df365f0` runs on the box behind `a601975`'s TSan (`~/queue.sh`), its log `~/m3f-df365f0/leg.log`: run.sh at -O0/-O1/-O2, self-host at -O0 and -O2 (the two that stopped at stage 1 on `a601975`, now expected 71 of 71 as on the Mac), asm6x.sh, tms6747.sh and ti-link.sh at both levels. The compiler's output is byte-identical to `a601975`'s at every level (identical.sh above), so what the leg adds is the self-host and the C6000 suites on g++'s build. **Bench stand-in**: the bench kernels' x86_64-linux -O2 assembly from `df365f0` is **byte-identical to `478f504`'s** (cmp), so M3a.2's Linux numbers stand - fib 10, sieve 94, matmul 15, isort 35, hash 327, virtual 211, total 692 ms against g++ -O2's 533 (11 interleaved rounds, checksums equal); the run of `df365f0`'s against main's (`ref-7d593d5`) and g++ is queued after the leg, `~/m3f-bench/bench.log` (`bench-linux.sh`, 11 rounds, medians). **Compile time, R1.2's row re-recorded for x86_64-linux**: the 71 sources one at a time with `-S -arch x86_64-linux` on the Mac, REF4 (`a601975`) against `df365f0`: one run -O2 66.55 s against 74.53, -O0 31.17 against 30.12; the first interleaved round -O2 75.27 against 67.30, -O0 27.53 against 28.87 - the Mac was not quiet (a later REF round read 118 s), the differences run both ways, and the retirement renames and deletes and changes no algorithm, so the row is **-O2 about 67 s and -O0 about 28 s for x86_64-linux**, from 413 s and 24.6 s before `6552790`; a quiet re-measurement is owed with the Windows rows.
**TSan** (RP2a condition 6, on Linux): `make tsan` on `a601975` was building on the box at this commit (`~/m3f-a601975/leg.log`, then `tsan.log`; `tests/out-tsan/reports` empty so far); its result is to be read there and written here. The two races WS-F found and mended are the only ones the pool has had, and nothing in M3a.3-M3a.5 touches the pool or the registries' locking. **Owed**: `run-cases.cmd` at -O0 and -O2 in both spellings,
`self-host.cmd` plain and `-O2`, `bench.cmd` against main's binary.

**Review point 2b, the packet** (2026-10-26, Fable 5.1, branch `m3-fable`, `478f504` to `this commit`). What the
reviewer reads: the seven commits above (`6552790`, `8f79594`, `7040edf`, `7b3f339`, `f5967f9`, `a601975`,
`df365f0`), `src/optimizer/Mir.h`, `OptTable.h` (the one table), `OptCore.h`'s `EntryOf` and `FlowOf::build`
(the funclet blocks), `MirAlloc.cpp`'s `buildInterference` (the funclet rule), and part 2 of `M3-IR.md` with its
ten dated amendments. **M3a's gate, as the table at the top of this plan asks it**: the round trip - `identical.sh`
at every level 0 differ for every renaming commit and the retirement, the files named for the two commits whose
output moved (M3a.4's 888, all Windows funclet bodies at -O1/-O2; M3a.3 D's 92, all one immediate's spelling); the
retirement - `OptIr.h`, the string mnemonic, `Operand`, `Entry::Event` and `-mir=off`/`-check=mir` gone, `Mir.h`
the one header; `bench-kernels` - the Linux stand-in above, the Windows `bench.cmd` owed. **RP2a's conditions**: 2
(`determinism.sh` runs `-dump=mir`) in `8f79594`; 5 (`identical.sh` 0 with REF beside its own tree) at every
commit since `8f79594`; 6 (TSan on Linux) running on the box at this commit, to be read from `~/m3f-a601975/leg.log`; 4 (the ml64 pass on the Windows cases) still owed with every
other Windows row; 1 and 3 were M3a.1's. **Decisions taken here, for the review to rule on**: the IR types' namespace
(`opt::`, 2.5); the four 2.6 columns not added; `unsignedSpelling` kept at every level (RP2a departure (d)); the
funclet rule (no preserved register for a funclet's pseudo, 2.9's open question, as cl does); `Block::funclet` in
place of `FuncletRange`; the MASM width inference beside the column (2.11). **Found and not mended**, each in
`tests/open/` with clang's answer: `member-initialiser-namespace-enumerator` (above); the M3a.1 round's
`conversion-to-reference-binds` stands. **R1.2's measures at M3a's close**: the compile-time row the 71 sources one at a time with `-S -arch x86_64-linux` on the Mac, REF4 (`a601975`) against `df365f0`: one run -O2 66.55 s against 74.53, -O0 31.17 against 30.12; the first interleaved round -O2 75.27 against 67.30, -O0 27.53 against 28.87 - the Mac was not quiet (a later REF round read 118 s), the differences run both ways, and the retirement renames and deletes and changes no algorithm, so the row is **-O2 about 67 s and -O0 about 28 s for x86_64-linux**, from 413 s and 24.6 s before `6552790`; a quiet re-measurement is owed with the Windows rows; the bench
row unchanged by construction (identical assembly), the run queued; the conformance count unchanged by construction (no front-end change but one qualified
enumerator in the compiler's own source). **What M3b.1 starts from**: `MFunction` and the spellings as they are, the
Windows rows owed, and `m3b0`'s C7 and M3b.0 once merged.

**Handover at RP2b** (2026-10-26, Fable 5.1, the budget ending here; Opus 5.5 implements M3b). Branch `m3-fable`,
`478f504` to this commit, local and never pushed; no merge into `m3a` or `main`; `m4` (now `m3-m4`, M4 merged with
M3a.4) is the coordinator's to bring in after RP2b, and the rename touches files M4 also touched -
`Mir.h`/`OptIr.h`, `MirAlloc.*`, `MirWebs.cpp`, `OptEffects.cpp`, `OptFrame.cpp`, `OptPasses.h`, `OptPipeline.cpp`,
`Optimizer.*`, `Masm.cpp`, `X86_64Linux.cpp` - so that merge resolves the type names onto `M*` (847 mechanical
renames; `MEntry::Inst`/`MEntry::Directive`/`.dir` for `Entry::Ins`/`Entry::Event`/`.event`). **Still running on
the Linux box when this was written**, each to be read and written into the entries above: `~/m3f-a601975/leg.log`
(TSan, condition 6), `~/m3f-df365f0/leg.log` (M3a.5's leg) and `~/m3f-bench/bench.log` (the bench stand-in);
`~/m3f-*` are mine to delete once read, with `~/m3f-*.tar`, `~/linux-leg*.sh`, `~/queue.sh`, `~/bench-*`. On the Mac
`~/cxxopt-build/m3f-*` hold the REF builds (`m3f-ref` 478f504, `m3f-ref2` 6552790, `m3f-ref3` 7b3f339, `m3f-ref4`
a601975), the identical.sh outputs and the self-host trees. **What I would have done next**: read the three logs and
fill them in; a quiet interleaved compile-time round; `make_owned` into a header of its own so the pipeline's 21
`new`s and `OptCosts.h`'s two can go (memory gate 1,060); the `tests/open/member-initialiser-namespace-enumerator`
fix in `Parser::memberInitialiser` (set `namespaceStack_` to the class's namespace for the replay, as the 1.14
round did for out-of-line members - one guard, measured by that case and `make test`); then M3b.1 on `m3b0`. **The coordinator is mending that lookup on `fix-nsdmi-enum` (off `f5967f9`) and registered the same shape as `tests/open/member-initialiser-enumerator-in-namespace.cpp`**; mine, `member-initialiser-namespace-enumerator`, is the duplicate and goes with the fix, and `OptValues.cpp:18`'s `opt::kNoCond` may be unqualified again then. M3a.5's self-host row above was measured on the Mac with the line qualified (71 of 71 at both levels), not with the fix.

**C7, the design-conformance items the user approved at RP2a** (2026-10-26, Fable 5.1, branch `m3b0` off `f5967f9`,
one commit; the Windows rows owed, the box being absent). Five items, each where the design says it belongs:

- **`TypeId` is a wrapped index into the `TypeTable`** (`Type.h`, `Type.cpp`): the fundamental kinds take 1..N in `Kind`
  order as the table is made, and every derived type the next number as `TypeTable::adopt` interns it - the one place a
  derived type is now registered, so a copy (`withConst`'s) is renumbered there; `TypeTable::id(t)` and `type(id)` are
  each other's inverse and 0 is none. Nothing reads it yet but M3b.1's IR; `identical.sh` against a REF of `f5967f9` in
  its own tree reads **6,039 compared, 0 differ** - the nine lib-path diagnostics of earlier runs are gone because the
  REF's binary sits beside its own `lib/`, which is RP2a's condition 5 met by construction.
- **`M3-IR.md` 3.1 rewritten**: the IR's `Value`, `Inst` and `Function` carry `TypeId`; the earlier sentence that a
  wrapper bought nothing is kept in the paragraph as what it was, with the reason it was reversed. The typed tree keeps
  `const Type *` and converts at the boundary (3.8).
- **The adapter is an R6.3 exception, dated and bounded.** `src/sema/Adapter.*` holds a `Parser &`, reads the parser's
  fifteen layout maps and builds `Ast.h` nodes through raw owning pointers; it is M2's harness and is deleted whole at
  M3b.7. Until then it is excepted from R6.3's handle rule and left out of the second memory count below. The
  exception ends when M3b.7 lands; a reader finding it after that date has found a defect.
- **`tools/memory-gate` counts raw-pointer declarators** in the files the handle rule covers - `src/syntax`,
  `src/sema/Typing*`, `src/ir`, `src/optimizer/Mir*` - as `raw-pointers`, the second line of the baseline. It
  started at **38** on `f5967f9` (`--pointers` lists them: 28 `const Type *`, two `syntax::NodeId *` and two `int *`
  in the Typer, `const char *kindName` twice, and `const Operand *ops` and `long *row` twice each in the allocator),
  and like the first count may only go down. **One rule written down now rather than discovered by a red leg**: M3a.5 renames `OptIr.h` into `Mir.h` and
  may bring old pointer code under the `Mir*` prefix; that is a rename and not new pointers, so the baseline is
  re-recorded once at that merge with the renamed files' count stated here, and goes only down from there.
- **`tools/self-host` has a stage-0 column**: every source compiled `-S` by stage 0 as well, `s0/` against `s1/`, in the
  tool's exit status - the claim RP2a section 6 made by hand ("stage 0 and stage 1 agree on all 71") is the tool's
  now. `self-host.cmd` is owed the same column when the box is back.
- The opcode table read-only once initialised was done at M3a.1 (`125db80`, M3-IR 2.6); nothing to add.

**M3b.0, the record moves onto the typed tree** (2026-10-26, Fable 5.1, branch `m3b0`, one commit after C7's; for
review point 3 with M3b.1). What the row above asked, and what each part turned out to be:

- **`TypedNode` carries the parser's facts** (`src/sema/Typing.h`): `resolved`, an index into
  `TypedTree::resolutions_` (0 none); `castType` and `castXvalue`, a cast's named type with a reference cast's
  category; `folded`, what a `sizeof`, `alignof` or `noexcept` folded to and an integer or character literal's value,
  with its type; `literal`, an index into `TypedTree::literals_`, the `Token` a literal was read from, copied whole so a
  floating literal's `dvalue` and a string's text travel with it. `Typer::record` fills them as each node is typed, from
  the parser's `resolutions_`, `castTypes_`, `refCastTypes_`, `keywordValues_` and `literalTokens_` - the maps the
  parser fills at parse time in the scope it had then, so 12.1's point 3 is unchanged by the copy.
- **The record is `sema::Resolution`, field for field `Parser::Resolved`, its two types as `TypeId`** - the handle rule
  applied to the typed tree's new fields on the day the handle landed, and the reason the second memory count stayed
  at 38 rather than rising by the twelve pointers a straight copy of the struct would have added. The kinds are one
  enum, `sema::ResolutionKind::What`, that both structs derive from, so the parser's record and the tree's cannot drift
  by a kind; `Parser::Resolved` keeps its `const Type *` and every parser site is untouched. `TypeId` is exercised
  from the first commit after its own: `TypeTable::id` on the way in, `TypeTable::type` on the way out.
- **The copy has an oracle.** `-check=typed` holds every typed node's copy against the parser's maps
  (`Typer::recordDiffers`: the record's every field with the types read back through the table, the cast type and
  category, the folded value, the literal's token by position) and counts a missing or different copy as a
  difference - so `differ=0` now says the copy is complete as well as the types right. Over the 648 host cases it
  found none; the report's fields are unchanged.
- **`ir::Lowering` exists with its constructor and nothing else** (`src/ir/Lowering.h`, `.cpp`): `(const
  sema::TypedTree &, TypeTable &, const Target &, Program &)`, no `Parser` in reach - the header includes nothing of
  `src/parser/`, which is the compile-time gate the row asked for; M3b.1 fills the class in. `src/ir` is in every
  source list (`Makefile`, `msvc/build.cmd`, `tools/self-host`, `self-host.cmd`) and in the second memory count.
- **The fifteen layout maps did not move**, and the adapter still reads them and the parser's record directly - it is
  deleted at M3b.7 and excepted until then (C7 above). **C2 is confirmed in**: the Comma a virtual call with a
  computed address, a member-pointer call or a conversion-operator call is wrapped in carries the call's record at
  five sites (`ParserExprCall.cpp:787, 903`, `ParserExpr.cpp:2788, 3022, 3112`).

The gate, on the Mac (clang, `make test` green): `tests/typed.sh` **648 passed, 0 differ** (8,230 functions, 7,423
bodies, 807 compiler-written; 185,006 nodes, 128,798 typed, 273 untyped) and every one of the 648 per-case reports
**byte-identical** to the REF's but `windows-h-layout`, which does not compile for this host and names its tree's
`lib/`; `tests/adapted.sh` **648 passed, 0 differ**, 1,269 adapted and 1,269 the same; `identical.sh` against the REF of
`f5967f9` **6,039 compared, 0 differ**; run.sh 988/0, emit.sh 1,930/0, names.sh 655/0, overload.sh 44/0, syntax.sh 3/0,
exclusions 126 sites 0/0, comment cap 0, memory gate held at `new` 1,062 and `raw-pointers` 38. **Linux**: owed for both commits - the session's budget ended while the box was still building C7's leg (g++ 11.5,
`~/m3b0-c7/`, started by `~/linux-leg.sh c7`, which prints run.sh at -O0 and -O2, typed.sh, adapted.sh and the memory
gate); the M3b.0 tree is on the box as `~/m3b0-m3b0.tgz`, and `sh ~/linux-leg.sh m3b0` runs its leg once the C7 one
has finished (one build at a time there). Both are Opus 5.5's to read and record here; the leg script deletes nothing,
so `~/m3b0-c7` and `~/m3b0-m3b0` are to be removed after.
**Windows**: owed, the box being absent. **`tools/self-host` could not be run on this branch**, and the reason is not
M3b.0's: stage 0 at `f5967f9` does not build stage 1 - `src/optimizer/OptValues.cpp:18`, `Cond cc = kNoCond;`, is
refused "'kNoCond' was not declared" by the REF binary as by this one, and `make test` never compiles the compiler's
own sources. Reduced to one shape - a default member initialiser naming an enumerator of an enum declared in the
enclosing namespace, which the initialiser's per-constructor replay reads with no namespace open (a mem-initialiser,
or the enum at global scope, is found) - and registered as
`tests/open/member-initialiser-enumerator-in-namespace.cpp` with clang's `-1`. It is M3a.5's to meet, its gate being
the self-host; the stage-0 column is measured there, or on a tree whose stage 0 builds stage 1. **And one more,
older than the branch**: `-check=typed` run for `-arch x86_64-windows -masm=masm` over the 631 cases that compile for
it (the M2 close review's census target) reads 8,712 functions, 131,374 typed, 266 untyped, **differ=1** - the REF
reads the same one: `explicit-destructor-call.cpp:76`, `p->~Base()` on a virtual destructor, which the Typer types
`void` by [class.dtor] and the parser `void *`, the Microsoft deleting destructor `??_G` returning `this`. The copy's
self-check found nothing on that target either. `typed.sh` runs the host target only, so no gate reads it; it is
written here for M3b.2, which lowers that call on the Microsoft ABI and must decide which answer the typed tree gives.

**M3a closed without a Windows box** (2026-10-26, Opus 5.5, `m3-line` at `1b350ae` to `cb10ae6`). Every row the plan
sets M3a that a Mac or the Linux box can measure is measured; the Windows rows are owed and written as owed.
**Linux** (g++ 11.5, a `git archive` of `1b350ae`): `run.sh` 991/0 at -O0, -O1 and -O2; `asm6x.sh` 636/0;
`tms6747.sh` 622/0 at -O0 and -O2 on vm6747 with every pass also on sim6747; `ti-link.sh` 631 linked at both levels;
`tests/c90.sh` on the host 438/0 at -O0 and -O2; `make tsan` and `tools/tsan-check` **clean** (RP2a condition 6).
`tools/self-host` on the Linux box at `4ad06d0` (the source list given `src/encoder`): **82 of 82 the same from stage
0, 1 and 2 at -O0 and at -O2**; on the Mac, 82 of 82 at both levels. **The Wine stand-in** (`tools/wine-cases.sh`):
617 passed, 2 failed at -O0 and at -O2 - `noexcept-terminates` and `temporary-unwind`, which Wine 6.0's unwinder fails
on every tree. **bench-kernels** on the box against main's binary: total 1,105 ms against 1,106, no kernel slower
(sieve 131 against 140, isort 53 against 55, the rest equal), checksums equal. **Owed**: the R1.2 compile-time row on a
quiet Mac (the runs recorded above were taken under load); every Windows row - the cl build, `run-cases.cmd` at -O0 and
-O2 in both spellings, `catch-check`, `throw-check`, `sret-check`, `winlink-check`, `self-host.cmd` plain and -O2,
`bench.cmd`, the -g cases through cdb, and RP2a condition 4's ml64 pass.

**Found beside it, and mended apart** (`50da3f2`, outside M3's scope). The C6000's `hoistInvariants` renamed a
hoisted value's readers among the loop's body lines, and an exit or the back edge is not a body line: `while (1) {
i = i + 1; int b = (i == lim); if (b) break; }` returned 1 at -O2, the back edge left reading the exit's predicate.
c90's `lp_break_while` is what met it (M7-PLAN, M7.4); the M7 agent's bisection named `predicateArms` because skipping
that pass removes the shape, not the fault. A branch in the web whose predicate reads the value now refuses the rename.
Box: `tms6747.sh` 623/0 at -O0, -O1 and -O2 on vm6747 and sim6747, `ti-link.sh` 632 linked at -O2, `asm6x.sh` 637/0;
`tests/cases/hoist-back-edge-predicate.cpp`. **M7.3-M7.5** (`m7b`) are merged (`cb10ae6`) after a re-measurement from
a fresh copy - `make test` green, `tests/c90.sh` 446/0 at -O0 and -O2.
**M3b.1, the SSA IR and the first lowering** (2026-10-26, Fable 5.1, branch `m3b1` off `1b350ae`, commits `2a46504`,
`d5ea461`, `cb33a21` and the record's; review point 3). What the row asked, and what each part turned out to be -
`M3-IR.md` 3.12 has the design as built and the answers to 3.11; `M3-RP3-PACKET-FABLE-2026-10-26.md` is the packet:

- **`src/ir/Ir.h`, `IrText.cpp`**: the data model of M3-IR part 3 as handles over `TypeId`, `-dump=ssa` in the format of
  3.3, `-check=ssa` the verifier of 3.4 (dominance by Cooper-Harvey-Kennedy, block-argument arity and types, one
  terminator, allocas in the entry), and `tests/ssa.sh`, which runs it over every host case and is in `make test`;
  `tools/determinism.sh` compares `-dump=ssa` beside `-dump=mir`. No raw pointer in `src/ir`: the second memory count
  held at 44.
- **The lowering** (`Lowering.cpp`) reaches **the scalar core of the adapter's subset, in the adapter's scope**: free
  functions at file scope - a member's, a namespace's or a template specialization's body stays the parser's, as the
  adapter left them - over scalars, pointers, references to scalars, 1-D arrays of scalars and a struct's scalar
  members, every statement kind but `try`. What the adapter did by reading the parser's layout maps (class objects by
  their bytes, struct arguments and results, temporaries into the parser's slots, the parser's guards) it refuses by
  name, 12.1 saying M3's layout is its own; those are M3b.2's rows. Measured over the 649 host cases: **777 of 7,425
  source bodies lowered, every one verified**; the 6,648 outside are named by reason, the first being a member function
  (4,320), a template's or scoped body, a function with landing pads (402), one returning a class through a pointer (82).
- **Selection** is the walker re-pointed (`SsaSelect.cpp`): an 8-byte slot per value above the parser's parameter slots,
  the walker's canonical form in it, `Call` nodes over `Var`s for the ABI code, `genConversion` for the conversions,
  the slots handed to the MIR passes as promotable locals, an inlined callee's frame below the selector's layout.
  **The parser's parameter slots are the one layout fact borrowed** (`receiveParameters` fills them; the selector copies
  each into its value's slot), to be re-pointed at M3b.2.
- **`-front=ssa`** selects a lowered function on the two x86 targets and leaves every other to the parser's path; on
  arm64-darwin and tms6747 the lowering and verifier run and selection does not, so the Mac's run.sh under the flag
  proves the typed tree is read without a fault and nothing more.
- **The typed tree gained three facts** (`Typing.h/.cpp`, the M3 line's file, kept small): a `var` typed as declared
  with its `&`, a string literal's label, a static local's Local record on its `var`; the Typer answers the declared
  return type, whether a body is a function-try, whether it is scoped and whether its function is a specialization.
  `-check=typed` exempts the `var` record it has no parser map for; its typed count rose from 128,798 to 140,195 (the
  `var` nodes), its differ count is 0 as before.

**Three faults the gate found, each mended before the record** - and none by a Mac suite: the Wine stand-in caught the
four `nontype-*` cases, whose template bodies hold a pack expansion the typed tree records as one node (a specialization's
body is the parser's now), a call through a call's result emitted as a call of a symbol named `int (*)(int)` (a call
whose head is not a bare name is a call through the value), and `stack-probe`'s frame doubled past `UWOP_ALLOC_LARGE`'s
one slot (the selector's frame begins above the parameter slots, not the parser's whole frame); reading `walkInPlace`
found an inlined callee laid over the selector's slots at -O2.

The gate, on `cb33a21`. **Mac** (clang, `make test` green): run.sh **989/0** with the flag off and **989/0 under
`-front=ssa` at -O0 and at -O2**; emit.sh 1,933/0; names.sh 656/0; overload.sh 44/0; syntax.sh 3/0; typed.sh 649/0;
adapted.sh 649/0 (1,270 adapted); **ssa.sh 649/0, 777 lowered, 777 verified**; exclusions 126 sites 0/0; comment cap 0;
memory gate held (`new` 1,060, `raw-pointers` 44). `identical.sh` against the REF of `1b350ae` built in
`~/cxxopt-build/ref-1b350ae`, flag off: **6,048 compared, 0 differ**. `determinism.sh`: **21,368 outputs compared
(3,984 `-dump=mir`, 2,988 `-dump=ssa`), 0 differ**. **Windows, the Wine stand-in** (`tools/wine-cases.sh`, x86_64-windows
objects by `cxx3 -c`, linked by the project's LINK, run under Wine 6.0.3 on the Linux box): **617 passed, 2 failed at
-O0 and at -O2 under the flag**, the two `noexcept-terminates` and `temporary-unwind`, Wine's unwinder (the baseline's
own); recorded as a stand-in, the box's rows owed. **Linux** (g++ 11.5, `~/m3b1-b`, the tree at `d5ea461` - one commit before the record): run.sh **987 passed, 4
failed under `-front=ssa` at -O0 and at -O2**, the four the `nontype-*` pack cases that `cb33a21` returns to the
parser's path (the Wine stand-in on `cb33a21` reads them passing at both levels); the control run with the flag off
**991/0 at -O0 and -O2**; the ledger cases under the flag at -O0 **balance** (`lifetime-ledger` built=12 gone=12 live=0,
`for-init-unwinds` built=5 gone=5 live=0); ssa.sh 649/0 (799 lowered on that tree), typed.sh 649/0. **The Linux leg of
`cb33a21` itself is owed**: it was queued on the box behind the earlier leg and never ran (the waiting job was killed
with the Mac's); its one difference from `d5ea461` is the template rule, and the stand-in's 617/2 on `cb33a21` is
what says the four are mended on x86.

**Review point 3, Opus 5.5, 2026-10-26: agree with conditions** (`doc-cxx3/M3-RP3-REVIEW-OPUS-2026-10-26.md`).
Re-measured from a fresh copy of `2da6019`: `make test` green, `ssa.sh` 777 lowered and verified, `identical.sh` 6,048
compared 0 differ, determinism 0 differ; and on the Linux box at the merge `a877396` - the x86 selection the Mac
cannot exercise - run.sh under `-front=ssa` **992/0 at -O0 and -O2**. With it, `tests/c90.sh` on the box: host 447/0
and tms6747 411/0 at -O0 and -O2 on vm6747 and sim6747, `lp_break_while` passing since `50da3f2`. The conditions,
for M3b.2: the self-host is part of every step's gate (stage 0 did not build stage 1 at the merge - a block-scope
using-declaration in `SsaSelect.cpp`, and a lambda capture taken for a template of its name, both mended on `m3b2`);
the parameter slots borrowed from the parser go; `-g` under the flag before M3b.7; the `var` bound mark before the
subset grows past it; `fromTemplate` a map.
**M3b.2, the first four growths** (2026-10-26, Opus 5.5, branch `m3b2`, `88d889d` to `16c5cae`). `tests/ssa.sh`: **3,370
of 7,440 source bodies lowered and verified** (782 at M3b.1). What grew, each by its own commit: namespace bodies and
member functions (`this` the first parameter, a bare member `(*this).m` by the record's offset, a member call with
the object's address first - the record carries the call's `this` and whether it went through a value); a
template's body unless a pack was expanded in it (`Parser::packBodies_`); a reference to a class, as a parameter, a
result, a local or a global; a plain class's local and assignment - with each declarator's initialiser form on its
`var` node (`TypedNode::initForm`, the mark RP3 asked for), and `memcpy`/`memset` selected through the walker's
`copyBlock`. Refused by name, each counted: a constructor and a destructor (their mem-initialisers and member
destruction are not in the syntax; M3b.3's class-layout builder), a virtual call, a member call on a base's
function, a body with landing pads, a class returned through a pointer. **Found by the boxes, not by any Mac suite**,
and mended: a virtual call lowered as a static one (Linux, six cases); an array zeroed by `{}` left as the stack had
it, an `alignas` local laid out at its type's alignment, and a class with a written `operator=` assigned by its bytes
(Linux, three cases); and two faults the self-host met in the compiler's own source - `SsaSelect.cpp`'s block-scope
using-declaration, and a lambda's capture taken for a template of its name. **Linux** (g++ 11.5) at `16c5cae`:
run.sh under `-front=ssa` **994/0 at -O0 and -O2**, `make test`'s suites green, tms6747 624/0 at -O0 and -O2 at
`04067ef`. **Mac**: `tools/self-host` 84 of 84 at -O0 and -O2 at `88d889d`. **Windows** (GRA, cl 19.44, back
2026-10-26): cl builds the tree under `/W4 /WX` again after two fixes the box found (`AsmCodeView.cpp` outside its
namespace on Windows, a local hiding a member in `Mir.cpp`); the cases in the GNU spelling **622/622 at -O0, -O2,
`-front=typed` and `-front=typed -O2`**; the `-front=ssa` passes, the MASM pass (its masm build mended in
`run-cases.cmd`), the tms6747 legs on the box's TI tools, self-host.cmd, bench.cmd, examples and the ml64 baseline
are queued on the box and recorded when they report.

**The Windows rows, filled** (2026-10-26 evening, GRA box, cl 19.44, the tree at `4b0fb63` on `m3b2`). cl builds the
tree under `/W4 /WX`; the cases in the GNU spelling **622/622 at -O0, -O2, `-front=typed`, `-front=typed -O2`,
`-front=ssa` and `-front=ssa -O2`** - x86_64-windows selection from the SSA IR agreeing with the parser's path on every
case it reaches; `throw-check` (a cxx1 throw caught by cl), `catch-check` and `sret-check` (cl calling a cxx1 member
for its struct) pass, `winlink-check` ran; names against cl **423 agreed, 19 differed**, 170 recorded in `.nocl`. The
MASM pass did not run in this leg (masm is built on `src/encoder` since M4.3 and `run-cases.cmd` had not been told -
mended in `4b0fb63`) and is queued, with TI's lnk6x, `release-check` and `c6747-levels` on the box's TI tools,
`self-host.cmd`, `bench.cmd`, `examples/` and the ml64 baseline. The hang the Mac met under `-front=ssa -O1` on
`stack-probe` came from M3b.1's sweep binary and does not reproduce on the current tree at any level.

**M3b.2, the exception growth: `try`/`catch` on the Itanium ABI** (2026-10-26, Fable 5.1, branch `m3b2-eh` off `fbc00f7`,
local). A function with landing pads was refused whole; now a `try` whose body and handlers are in the subset is lowered
(`Lowering::tryStatement`, M3-IR 3.6's Itanium half): every call inside a try's body or a handler is an `invoke` whose unwind
successor is the innermost pad, a block marked `pad` with two parameters - the exception pointer and the selector the runtime
hands over - opening with a `catchpad` (a try's) or a `cleanuppad` (a handler's end-catch) that carries the whole action
chain (`Inst::syms`, the indices in `cases`, `k` the try's own handlers, `bits` 1 for a filter-0 row and 2 for a pad that
hands on). The chain tests the selector per handler in the parser's own first-occurrence numbering (`typeIndexFor`, per
function) and hands what nothing matched to the enclosing pad with both parameters, or to `_Unwind_Resume`; a handler
calls `__cxa_begin_catch`, binds a scalar by value (copied out of the object) or a reference (holding the pointer), and
ends its catch on every way out - falling off, `return`, `break`, `continue` (`endCatches`, the catch ended after the
returned value is computed), or a throw through its own cleanup pad, which ends the catch and goes on to the enclosing
pad or out of the frame. **Selection** (`SsaSelect.cpp`): an `invoke` is the walker's call between two labels and one
call-site row naming the pad's landing label (`ssaland.N`, where rax and edx are stored into the two parameters before
the block's own label, so a pad branching to another pad arrives past the stores) - rows registered in address order,
disjoint, never sorted, `lsdaTable` filling the gaps as it always did; a function with pads whose tries nothing can
throw through gets one empty row so the LSDA the prologue named exists. The type_info symbols are Sema's
(`Typer::typeInfoSymbol` over `Classes::typeInfoSymbolFor`, handed to the lowering as `setTypeInfo`). **Refused by name**:
a function try block or a dynamic exception specification (`FunctionFacts::wrappedBody`), a `noexcept` function with
landing pads (its terminate region), exceptions on the Microsoft ABI (funclets: FH3's state tree is not written from the
IR yet), a catch of a pointer, of a class by value, or of an unnamed type (the typed tree keeps no node for `catch (T)`
with no name), a `goto` inside a try or a handler, a `throw` (as before). **Case**: `tests/cases/ssa-try-catch.cpp` -
by value, by reference, catch-all, a handler left by return/break/continue, a try in a body, in a handler, a handler
throwing out to an enclosing try, a call outside the try in a function with one, and a try nothing throws through.
**Measured on the Linux box** (g++ 11.5, `~/fable-eh`): `tests/ssa.sh` **3,526 of 7,457 source bodies lowered and
verified, 0 failed** (3,370 before); run.sh under `-front=ssa` **996 passed, 0 failed at -O0 and at -O2** (652 printed their recorded output, 340 refused as recorded; 994/0 before this growth); the case prints clang's output at -O0
and -O2 with 8 of its 12 functions on the SSA path (`thrower` throws; see below for the other three). The first run of
the gates failed `try-in-handler` and `two-try-same-type` with `terminate`: a handler's cleanup row with no types was
registered with the filter-0 flag, and `lsdaTable` writes action 0 for a row of no types while advancing its action
counter as if the record were there - so every later row named an action past the table. The flag is asked only
where the chain has types now; `lsdaTable`'s arithmetic is left as it is, a shape the parser never produces.
`tools/comment-lines` 0 over the cap, `tools/exclusions --check` 126 sites 0/0; the Mac only syntax-checked the
changed files (`clang++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsyntax-only`), which caught a `-Wreorder` the
box would have refused. The box's lock was held by another session's full leg until 21:37 and my queued build was
dropped with it, so the hour was extended to 22:30 and the Windows cases under the flag were not run.
**Found and not mended**: a non-void function whose last statement is a `try` (or an `if`/`else`) with every path
returning is refused "a body that falls off its end without a value" - the merge block is unreachable and unterminated,
a shape older than this round; `byReference` and `throughGap` in the case take the parser's path for it. And the syntax
tree keeps no `var` for the second handler of the case's `nestedInBody` (`catch (double unused)`), `(catch (block ...))`
in `-dump=syntax`, where the same function alone keeps it - a recording fault of the parser's `noteCaught`, not probed
further; the lowering refuses the handler by name ("a handler whose caught object the syntax tree does not hold").

**M3b.2, the growths of 2026-10-26 evening to 10-27 morning** (Opus 5.5 with four Opus 5.5 workers and one Fable 5.1,
branch `m3b2`, `b4e8778` to `f05280c`, local). `tests/ssa.sh` on the Linux box: **3,490 of 7,445 source bodies lowered
and verified (47%) at `b22b257`, **5,640 of 7,626 (74.0%)** at `f05280c`**, 0 failed throughout; run.sh under `-front=ssa` 0 failed at
-O0 and at -O2 at every commit; the Windows box's cases under `-front=ssa` 0 differ at -O0 and at -O2 at every merge.
Each growth is one commit with its own case, its `.expected` clang's; in order:

- **Constructors and destructors of a simple class** (`b4e8778`): a constructor's mem-initialisers are `meminit` nodes
  in the syntax tree now, each member named set from its list, `m()` zeroed, a plain class copied; a member's own
  initialiser stays outside (`Typer::defaultInit`). With it two Windows faults the box found: a class passed by value
  is copied into a frame slot the parser gives each call on the Microsoft ABI (`argSlots`), which the selector had
  left at 0 - past the frame, over the return address; and a local of 16 bytes or more, aligned at 16 there, was
  taken for `alignas` (312 bodies).
- **A plain class returned by value, and a call returning one** (`913aa70`, `aaa67eb`): the address is what is
  carried, as a reference return's; the call's result goes into an alloca the call names (`CallInfo::resultAlloca`),
  which the walker's own call fills by every ABI rule.
- **Fable's try/catch on Itanium** (merged `858fbc1`), with the parameter scan stepping over a declarator's constants.
- **Casts to a reference** - `std::move`'s `static_cast<T &&>`, noted as an xvalue of T - and **pseudo-destructors**
  (`554b7c6`); **a `for` missing any part** (the parser records which, `forParts_`) and **a static local with a
  constant initialiser** as its global, a thread_local asked of the program's own global (`72161e7`).
- **Pointer conversions to a non-virtual base** and back down by `static_cast` (`11bd1a1`, `ad87c35`), a null kept
  null; **a base's member function called through a derived object** (`1e652e7`); **`throw` and rethrow on
  Itanium** (`112194d`); **a declaration as an `if` or `while` condition** (`9ed105f`).
- **Placement new** (`8b51170`), **a value bound to a `const T &` or `T &&` parameter** (`079457f`), **a function
  object called by its name** (`22b55ac`), **an overloaded operator lowered as the call it is** (`658cef1`, +127
  bodies: a member operator's left operand its object, postfix ++ given its 0), **a class's own destructor called by
  name** (`fb915fa`).
- **Workers, each merged after a gate of the merged tree on both boxes**: `new`/`delete` of a scalar or plain class
  (`1555402`); members of class type built and destroyed by the constructor the parser recorded (`bbf710b`); plain
  class temporaries and class values copied from any source (`a042599`); non-virtual bases (`5952ae3`); locals whose
  class has a destructor, destroyed on every way out and on unwinding, Itanium (`35b3f73`); Fable's `new T(args)` of a
  class with constructors and constructor-built temporaries (`f05280c`).

Also measured in the round: the Windows self-host (`self-host.cmd`, the project's masm and LINK) **84 of 84 at -O0
and at -O2**; the MASM spelling under `-front=ssa` 631 same, 0 differ. **Found and not mended**, each in tests/open:
a second base's member function called unqualified (`second-base-member-unqualified`), `P(make(8));` read as a
declaration (`functional-cast-statement-of-call`), a class object declared as an `if`'s bare body
(`class-declared-as-if-body`); and `currentFunction_` is stale while a member template's specialization is defined,
which is why such constructors stay outside. **What is left**, largest first on the latest lists: exceptions on the
Microsoft ABI (about 700 bodies, Windows only - funclets from the IR), constructors and destructors of a class with a
vptr or a virtual base, a class local initialised from what is not yet an object, a body with a pack expansion.

**M3b.2, cleanups on the Microsoft ABI: a local with a destructor, its funclet and its FH3 state** (2026-10-27, Fable 5.1,
branch `m3b2-ms` off `fdf7870`, local). Every function with landing pads on x86_64-windows was refused whole ("exceptions on
the Microsoft ABI", 718 bodies on the box's list), and every local a constructor builds or a destructor destroys there (122
more). Now the function-wide refusal is gone and the lowering refuses by construct: a `try` and a `throw` stay outside on
that ABI by name, and a local with a destructor is lowered as it is on Itanium but for its pad - `Lowering::closeScope`
writes the Microsoft shape of M3-IR 3.6: a `cleanuppad`, the destructor as a plain call (the runtime ends the program itself
should it throw, as the walker's cleanup funclet does), and a `cleanupret` naming the enclosing pad or none. **Selection**
(`SsaSelect.cpp`, `usesFunclets()`): every pad block is a state of the FH3 tree, numbered before the body so an `invoke` can
name it (`Walker::msAddState`, the one door the selector has to the tree, beside `msSetState`, `msAddIpRow`,
`msSetUnwindHelpSlot`); an invoke is its call between two state labels with an ip2state row at each - the pad's state in,
-1 out, so a call's own range and no more is in a state; a pad block is selected inside `openFunclet`/`endCleanupFunclet`
as the walker's cleanup funclet is, its state's `toState` the `cleanupret`'s target, its action the funclet; the frame
gets the runtime's scratch word (`layoutSsa`) and the entry writes -2 into it where the function has a pad. Inside a
funclet an alloca's address is taken afresh by `lea` rather than read from the body's value slot, which the passes may
have promoted. The four FH3 tables are the walker's own emitters, unchanged. **Refused by name**: a `try` and a `throw` on
the Microsoft ABI (a handler funclet returns the address to carry on at, and the four-object ThrowInfo chain is the
walker's), and everything the Itanium growths refuse. **Case**: `tests/cases/ssa-ms-cleanup.cpp` - one local, two in one
scope, nested scopes, a loop body's with `continue`, an early return, each left by an exception and by the normal path;
the catching and throwing functions take the parser's path, the five lowered ones' funclets are what it runs.
**Measured on the Windows box**: `-check=ssa` over every case **7,939 bodies, 5,833 lowered, 0 failed** (7,929 / 5,701 before, the ten more bodies the new case's; +132 lowered), the refusal list now reading "a try on the Microsoft ABI" 148 and "a throw" 52 where "exceptions on the Microsoft ABI" read 718; the cases under `-front=ssa` **648 same, 0 differ, 340 refused as recorded at -O0 and at -O2** (the whole suite on
the default path was not re-run in the hour; the new case was, below); `throw-check` and `catch-check` both pass (`int 7`, `double 2.5`; `before / caught 7 / after`), and the new case prints clang's output on the default path at -O0 and -O2, under `-front=ssa -O2` and in the GNU spelling. **Linux** (g++, `~/lane-ms`): run.sh
under `-front=ssa` **1019 passed, 0 failed at -O0 and at -O2** (675 printed their recorded output, 340 refused as recorded), and the default path 1017 / 2 - `downcast-ssa` and `ssa-member-ctor`, the two parser faults another lane is mending, as expected. `tools/comment-lines` 0 over the cap.

**M3b.2, a throw on the Microsoft ABI** (2026-10-27, Fable 5.1, branch `m3b2-mstry` off `f8abb56`, local). Of the two
constructs the cleanup growth left outside on x86_64-windows, the throw is the smaller and is lowered now; the `try`
stays outside by name (below). `Lowering::microsoftThrow` writes the parser's own shape of M3-IR 3.6: the exception
object in an alloca of the frame - a scalar's converted value, or a plain class lvalue's bytes - then
`_CxxThrowException(&object, &_TI...)` through `throwCall`, so inside a cleanup's region it is an invoke to that pad
and the selector's ip2state rows cover it; `throw;` is the same call with two nulls, as `Parser::rethrowCall` writes
it. **The ThrowInfo chain is the parser's, asked and never rebuilt**: `Typer::microsoftThrowInfo` spells the `_TI`
name with `microsoftThrowNames`/`microsoftThrowFinish` and answers it only where `Program::msThrows` already holds that
chain finished for a throw - the parser's body for the same function made it, and the backend emits it - so a type the
parser never threw stays outside by name. The thrown operand's rules are the Itanium path's, in one `thrownValue`:
a plain class lvalue or a non-x87 scalar (a pointer included here, its chain carrying `void *`). **One refusal the
first run on the box found**: a throw in a `noexcept` function outside any try, which the parser lowers to the value
and `abort` ([except.spec]/9) and gives no landing pad on this ABI - `noexcept-terminates` printed "caught" under
`-front=ssa`; refused by name now on both ABIs, in front of the split. Nothing in `SsaSelect.cpp` changed: a call
that does not return and an invoke to a cleanup pad were already selected. **Refused by name**: a `try` on the
Microsoft ABI - a handler is a funclet of its own that hands the resume address back, its blocks must be emitted
contiguously inside `openFunclet`/`endFunclet(resume)`, the FH3 try map wants each handler's object slot and
descriptor, and none of that is a shape the IR carries yet - a throw of a class with a constructor or a copy of its
own, and everything the Itanium growths refuse. **Case**: `tests/cases/ssa-ms-throw.cpp` (clang's output): an int, a
converted double, a `const char *`, a plain class by its bytes, a throw past one and past two locals with
destructors (the invoke in the cleanup's region, the destructors run by the runtime), and a rethrow from a function
the handler calls; the catching functions take the parser's path. **Measured on the Windows box** (cl 19.44, /W4 /WX
clean): `-check=ssa` over every case **7,990 bodies, 6,115 lowered, 0 failed** against the base `f8abb56`'s own 7,979 / 6,068 (the eleven more bodies the new case's; +47 lowered), the refusal list no longer naming "a throw on the Microsoft ABI" (52 at the base) - of those, twelve now read "a throw of a class that is not plain" and one "a throw in a noexcept function", the rest are lowered - and "a try on the Microsoft ABI" at 150; the cases under `-front=ssa` **652 same, 0 differ, 340 refused as recorded** and under `-front=ssa -O2` **652 same, 0 differ, 340 refused as recorded**; on the default path at `-O0` **652 same, 0 differ, 340 refused as recorded**; `throw-check` and
`catch-check` pass on the default path and with `-front=ssa` written into both (`int 7`, `double 2.5`; `before /
caught 7 / after`), the thrower under the flag being this lowering's. **Linux** (g++, `~/lane-mstry`): the full `make test`: run.sh **1023 passed, 0 failed** (679 printed their recorded output, 340 refused as recorded), emit 2697 / 0, typed 681 / 0, adapted 681 / 0, ssa.sh 7,687 bodies, 5,794 lowered, 0 failed, coff 1,336 / 0, c90 447 / 0, exclusions 126 sites 0 / 0, the memory gate at its baseline of 44 (a first run read 45: the one raw `const Type *` the hook added, a reference now), and the comment cap - which that run failed on one group of the throw's header, cut to three lines afterwards and read 0 by the same tool; the suite was not re-run on the comment-only change; then run.sh at `-O2` and under `-front=ssa` at -O0 and at -O2, each **1023 / 0**.
`tools/comment-lines` 0 over the cap.

**M3b.2, a constructor whose members are destroyed on unwinding, and the two lanes merged** (2026-10-27, Opus 5.5
worker on `m3b2-cunw`, `8de0e1b`; merged with `m3b2-mstry` as `729bcec`, local). In `memberInits` each base and member
with a destructor becomes live once built (`Lowering::guard`, the tail of `builtLocal` lifted out), so a later call is
an invoke whose pad destroys what was built, last first, on both ABIs; the normal way out keeps them (`closeScope`'s
`keep`, `liveFloor_`). The parser records a base's destructor beside its `^B` record (`|~B|0`), a record and no code;
two exclusion citations moved and were re-pointed. Case `constructor-unwinds-parts`, its ledger 35 built and 35 gone.
**The merged tree, gated together:** Linux `make test` exit 0 - run.sh **1024 / 0** at -O0, -O2, `-front=ssa` and
`-front=ssa -O2`; the emit golden recorded at m3-line `ecf4055` **0 of 2,693 changed**, 8 added; ssa.sh **7,705 source
bodies, 5,937 lowered and verified (77.0%), 0 failed**; coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment
cap 0, the memory gate under its baseline. Windows: **653 same, 0 differ, 340 refused as recorded** at -O0, under
`-front=ssa` and under `-front=ssa -O2`. Fable's one ungated step - `make test` after its comment-only change - is closed
by this run. **Not mended**: a destructor with landing pads that destroys members (1 body); on the Microsoft ABI a
constructor that throws itself stays the walker's while a `try` there is refused.

**M3b.2, virtual calls and a `delete` of a class with a destructor** (2026-10-27, Opus 5.5 worker on `m3b2-call`, lane
`lane-call`, from `da147b2`). The parser records the slot a virtual call reads on its call record (`Resolved::vslot`,
copied by the Typer as `Resolution::vslot`), and the lowering reads the vptr at the object's first word and the function
at that slot (`virtualCallee`), the object first converted to the slot's class as the parser converts it - one shape on
both ABIs, the parser's own. A `delete` of a class with a destructor carries the destructor the parser called - its
symbol and type, or the deleting slot it read (Itanium D0, Microsoft `??_G` given 1) - on its `NewFacts`
(`noteDeleteDestroys`); the lowering destroys under a null test ([expr.delete]/2) and frees with the global operator
delete the parser named (`deleteDestroying`). A conditional's arm that is the literal 0 is the null pointer
([conv.ptr]/1, `exprTo` in `conditional`), which was every "conversion of a number to a pointer". **Refused as before**:
a call through a member pointer (none in the suite), `delete[]` of a class with a destructor, a class's own operator
delete, and a call with defaults - a default is read again by the parser at each call and its expression has no node in
the typed tree to lower. **Case**: `tests/cases/virtual-call-delete-ssa.cpp` (clang's output). **Measured**: Windows
`-check=ssa` **8,029 bodies, 6,336 lowered, 0 failed** against `da147b2`'s 8,008 / 6,254 (the 21 more bodies the new
case's); the three reasons gone (21 bodies each), and per function the only new refusals are two `main`s that now stop
at a later statement (`delete-null`'s `new` of a class with a destructor, the case's polymorphic local). Linux (g++,
`~/lane-call`) the full `make test` exit 0 - run.sh **1025 / 0**, emit 2705 / 0, typed 683 / 0, adapted 683 / 0, ssa.sh
**7,726 source bodies, 6,025 lowered, 0 failed**, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap
0, the memory gate passing - then run.sh at -O2 and under `-front=ssa` at -O0 and -O2, each **1025 / 0**. Windows under
`-front=ssa` and under `-front=ssa -O2` **654 same, 0 differ, 340 refused as recorded**.

**M3b.2, round 1 of 2026-10-27 evening: class values, constructed returns, virtual calls and delete** (three Opus 5.5
workers off `da147b2`, merged as `0bd1089`, local). lane-val (`4e32823`): a lambda's closure built as an object from
the captures the parser recorded (Typer::closureFacts), and a class prvalue bound to a const or rvalue reference
parameter. lane-ret (`c8a1c46`): a class with constructors but no copy or destructor of its own returned from a local
or a temporary the return builds (Lowering::returnConstructed). lane-call (`b05abf5`): a virtual call through the slot
the parser recorded (Resolved::vslot), `delete` of a class with a destructor (the deleting slot where it is virtual,
nothing for a null pointer), and a literal 0 arm of a conditional converted by exprTo. **The merged tree, gated as one:**
Linux `make test` exit 0, run.sh **1027 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693
changed, ssa.sh **7,759 bodies, 6,144 lowered and verified (79.2%)**, 0 failed; Windows **656 same, 0 differ** at -O0,
`-front=ssa` and `-front=ssa -O2`. Fable's Microsoft try/catch lane (`9a0589f`) was held out of this round: under
`-front=ssa` on Windows `ssa-try-catch` printed nothing (exit 9) - a call in a catch funclet given its enclosing try's
state, fixed in `be61c7d` and taken in a later round. **Found on the way**: `winssa.sh` reads an empty flag as
`-front=ssa`, so a gate's "default" pass must name `-O0`; and a tree laid over a built one by `tar` keeps the files'
own times, so the gate touches them before `make`.

**M3b.2, round 2 of 2026-10-27 evening: pack expansions, a thrown class copied, the Itanium exception tail** (three
Opus 5.5 workers off `0bd1089`, merged as `6f1ee79`). lane-pack (`739d942`): a pack expansion's members carry their
member in the syntax tree (Tree::markMember), so a body with one is lowered and the Driver's packBody refusal is gone;
a template's arguments are dropped from the statement round them (Tree::drop); a conditional of one arithmetic type has
that type; a body that falls off its end returns a zero. lane-cbase (`28f4fbe`): a throw of a class with a copy
constructor, on both ABIs (Lowering::classThrow) - the exception object built by the copy the parser chose for the throw
(noteThrowCopy), a temporary operand built and destroyed round it, guarded where the parser made pads, Itanium's storage
freed by __cxa_free_exception on unwind. lane-ehtail (`a4a5c16`): on Itanium an unnamed handler, an ordinary function's
try block, its throw(...) list as one filter pad calling __cxa_call_unexpected, and a noexcept function with pads of its
own. **The merged tree, gated as one:** Linux `make test` exit 0, run.sh **1030 / 0** at -O0, -O2, `-front=ssa` and
`-front=ssa -O2`, the golden 0 of 2,693 changed (32 added), ssa.sh **7,796 bodies, 6,309 lowered and verified (80.9%)**,
0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its baseline;
Windows **659 same, 0 differ, 340 refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`. Before landing, every
file the round changed (29 of 30; `M3-PLAN.md` itself held round 1's newer text) was checked by md5 to be the same in
the Linux gate tree as here, since a stale job had shipped round 1's files into it while this gate ran.

**M3b.2, round 3 of 2026-10-27 evening: try and catch on the Microsoft ABI** (Fable 5.1's lane-mstry2, `9a0589f`, with
its fix `be61c7d`, merged over round 2 as `0be9c97`). A handler is a funclet of its own on that ABI: the lowering builds
a dispatch pad over the handler blocks (CatchSwitch), each handler a pad block with the parser's descriptor, its caught
object's slot and its by-reference and const marks (CatchPad), the handler's blocks tagged with their funclet, and every
way out a CatchRet to an exit that destroys and branches on (leaveHandlers); the selector emits each handler with
openFunclet/endFunclet and writes the try map and handler-map rows through two new doors in the walker. Held out of
round 1 because `ssa-try-catch` ended (0xC0000409) under `-front=ssa` on Windows: a call inside a handler that throws to
an enclosing try of the same function had been given that try's state in the funclet's ip2state rows, and
__CxxFrameHandler3 searches from a catch funclet only the tries whose handler states hold the state in force - so a call
in a handler now stays in the handler's own state, as cl writes it, and a pad of its own inside a handler is refused by
name. **The merged tree, gated as one:** Linux `make test` exit 0, run.sh **1031 / 0** at -O0, -O2, `-front=ssa` and
`-front=ssa -O2`, the golden 0 of 2,693 changed (36 added), ssa.sh **7,811 bodies, 6,323 lowered and verified
(80.9%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its
baseline; Windows **660 same, 0 differ, 340 refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`.

**M3b.2, four small groups: a static member read, a reference bound across types, a range-based `for` over an array,
and `T()`** (2026-10-27, Opus 5.5 worker on `m3b2-misc`, lane `lane-misc`, from `0bd1089`). A static data member named
in a body is the parser's record: one folded in its class (`static const int k = 5;`, `std::string::npos`) is recorded
with serial 1 and lowered as its value, any other is the object its symbol names, read or written like a global, a
thread_local one recorded as such and refused; a static member function named as a value is its address, as every
`Resolved::Function` is a plain pointer. A reference parameter bound to an lvalue of another type is
[dcl.init.ref]/5's: a scalar converted into a temporary of the frame for a const or rvalue reference
(`boundConverted`), a class's unambiguous non-virtual base subobject by its offset (`baseOfObject`). A range-based
`for` over an array object of scalars (`rangeForStmt`) is the parser's rewrite with a cursor of the frame - read once,
walked to one past the last element, the variable taking or binding each; a class range stays outside. `T()` of a
scalar is recorded as folded to 0 by `functionalCast`, which tells it from a cast whose operand went unnoted, and is
lowered as zero, null or `false` (`valueInitialised`). **Cases**: `ssa-static-member-read`, `ssa-reference-converted`,
`ssa-range-for-array` (clang's output). **Measured**: Windows `-check=ssa` **8,079 bodies, 6,574 lowered, 0 failed**
against `0bd1089`'s 8,062 / 6,453 (the 17 more bodies the new cases'): gone are "a cast not recorded" (22), "a member
named as a value" (14), every "the name '...', a static member" (77 over 44 names) and "a range-based for" (10, now 5
over what is not an array and 4 of other parts); "a reference parameter bound to another type" 18 to 11. Windows, the
76 cases whose functions' reasons moved, under `-front=ssa` and `-front=ssa -O2`: **73 same, 0 differ**. **Left**: a
static local built at run time, an `initializer_list<int>` parameter, a member through a closure's captured `this`,
and "a mem-initialiser outside a constructor" - every one a constructor of a class template, not recognised as a
constructor, the cause not yet measured.

**M3b.2, round 4 of 2026-10-27 evening: calls returning a constructed class, four small groups, and an invoke's copy
slots** (two Opus 5.5 lanes off round 3, lane-ret2 `4eb1978`/`77f06e5` and lane-misc `001e32f`, merged as `837c299`,
with lane-r4fix's `6c8f617`). Merged, the two lanes lowered one shape neither did alone - `set`'s and `map`'s range
constructors, whose `insert(first, last)` is an invoke, `tree_` having a destructor - and on the Microsoft ABI
`map-set-node-stability` printed nothing under `-front=ssa`: `layoutSsa` reserved a frame slot for a class passed by
value at a `Call` and not at an `Invoke`, so the two 16-byte iterators were copied over the saved rbp and the return
address. An invoke's arguments get their slots now; `invoke-by-value-class` is the case. **The merged tree, gated as
one:** Linux `make test` exit 0, run.sh **1035 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of
2,693 changed (52 added), ssa.sh **7,854 bodies, 6,614 lowered and verified (84.2%)**, 0 failed, coff 0 differ, c90
447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its baseline; with the fix, Linux run.sh
`-front=ssa` 1036 / 0, ssa.sh and typed.sh 694 / 0; Windows **664 same, 0 differ, 340 refused as recorded** at
`-front=ssa` and `-front=ssa -O2` (the -O0 pass, which does not reach `layoutSsa`, clean on the tree before the fix),
winlane hist 8,157 bodies, 6,962 lowered, 0 failed.

**M3b.2, round 5 of 2026-10-27 evening: calls and parameters, and constructors of class templates** (lane-call2,
Opus 5.5, `fbdb586`, and lane-ctor2, Fable 5.1, `2c5d8a8`, both off round 4's `837c299`, merged as `2add48d`).
lane-call2: a `std::nullptr_t` parameter, local or argument (a read of one is a null constant, [conv.lval]/2), a static
member function called through an object (the object evaluated, the function called by its symbol), a data member
holding a function pointer called through it, and a lambda's body reaching the members of the class whose `this` it
captured (through the closure's `$this`). lane-ctor2: the parser records which `function` nodes are constructors
(`constructorNodes_`, `Typer::isConstructor`), so a class template's constructor is recognised where its text names
the described type; a member of a class with constructors but no copy of its own, initialised from one object of its
type, is recorded as copied by bytes (`$bytes`) and lowered so; and a constructor's layout takes arrays of arrays of
scalars and anonymous unions. **The merged tree, gated as one:** Linux `make test` exit 0, run.sh **1039 / 0** at -O0,
-O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693 changed (68 added), ssa.sh **7,898 bodies, 6,723 lowered
and verified (85.1%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate
under its baseline; Windows **668 same, 0 differ, 340 refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`.
**Left, as each lane found it**: a pack's parameters (the syntax tree has one node for `Ts... rest`), a call's default
arguments (no record of the default's expression), a member's default initialiser (no node in the constructor's
syntax), virtual bases, and array members of a class.

**M3b.2, round 6 of 2026-10-27 evening: a class with a destructor returned, placement new, values taken as objects**
(lane-ret3 `deaf9bf` and lane-obj `fd15a0a`, Opus 5.5, both off `837c299`, merged over round 5 as `0d1df8f`).
lane-ret3: a class with a destructor and no copy of its own returned by value as the parser returns it - a local a
scope destroys keeps its bytes for the `ret`, `T(...)` is built in a slot no scope destroys, any other object's bytes
are saved before the destructors run (`returnDestroyed`); placement new of such a class (every `new` of
`basic_string<char>` in the list, 19). Which local escapes destruction is written to match the parser's rule rather
than read from a record. lane-obj: a static data member named by its bare name is an lvalue; a `static_cast` or
`const_cast` written to an lvalue reference names its operand (`referenceCast`, recognised from the cast's spelling
where the operand has the type the parser noted); a lambda capturing `this` in a member function, the parser noting the
capture's offset and type (`LambdaFacts::thisOffset`/`thisType`). **The merged tree, gated as one:** Linux `make test`
exit 0, run.sh **1041 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693 changed (76 added),
ssa.sh **7,928 bodies, 6,859 lowered and verified (86.5%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites
0 / 0, comment cap 0, the memory gate under its baseline; Windows **670 same, 0 differ, 340 refused as recorded** at
-O0, `-front=ssa` and `-front=ssa -O2`.

**M3b.2, round 7 of 2026-10-27 evening: operands the syntax tree left loose given their nodes** (lane-loose `84fce47`,
Opus 5.5, off round 6's `0d1df8f`, merged as `343cb40`). The record, not the lowering: a name read with a template-id's
argument list or an operator's spelling is one name - `Box<int>(n)`, `B::twice<int>(2)`, `At<4>::get()`,
`W::operator+(k)` are calls of it; `T{a, b}` of a class temporary is a call `{}` whose parts are the list's values,
built as `T(a, b)` where every constructor parameter is a scalar and refused by name otherwise; a braced argument is an
`other` `{}` over its elements, so a call keeps one part per argument, refused by name in the lowering; and `&C::k` and
`&C::f` of a static member record their operand's name. **The merged tree, gated as one:** Linux `make test` exit 0,
run.sh **1042 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693 changed (80 added), ssa.sh
**7,949 bodies, 6,943 lowered and verified (87.3%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0,
comment cap 0, the memory gate under its baseline; Windows **671 same, 0 differ, 340 refused as recorded** at -O0,
`-front=ssa` and `-front=ssa -O2`.

**M3b.2, round 8 of 2026-10-27 evening: a class with a copy of its own returned through the caller's result pointer**
(lane-sret `f108262`, Opus 5.5, off round 7's `343cb40`, merged as `90ea44e`, with `ac15cf8` restoring the brace the
merge dropped after `returnDestroyed`). The parser records what `buildReturned` made of each `return`
(`Parser::ReturnBuilt`, read by `Typer::returnBuilt`): the named return value already in the caller's object, a
temporary built there, or another object copied there by the constructor it chose; and which local `nrvoCandidate`
placed in the caller's object (`Typer::namedReturnLocal`). The lowering gives such a function its result pointer as an
IR value (`ir::Function::sret`, copied by the selector from the slot the prologue keeps it in, on both ABIs), builds the
named local there with its guard - destroyed on every way out but the `return` naming it - and lowers the three forms.
`return f();` built in place is refused by name. **The merged tree, gated as one:** Linux `make test` exit 0, run.sh
**1043 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693 changed (84 added), ssa.sh **7,963
bodies, 7,016 lowered and verified (88.1%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment
cap 0, the memory gate under its baseline; Windows **672 same, 0 differ, 340 refused as recorded** at -O0,
`-front=ssa` and `-front=ssa -O2`.

**M3b.2, round 9 of 2026-10-27 evening: a call's default arguments, a member's own initialiser, class temporaries with
destructors** (lane-rec `03a86d7` and lane-temp `93ad8c3`, Opus 5.5, merged over round 8 as `86d87bf`; `be184fd` makes
`ssa-temporary-destructor` independent of the order of a sum's operands, which the walker evaluates differently on Linux
and Windows). lane-rec: a call completed by default arguments takes them as its last syntax parts (`applyDefaults`
records the defaults' nodes, `Parser::withDefaults`), typed in no scope of the calling function ([dcl.fct.default]/5);
a member's `= v` replayed into a written constructor is a mem-initialiser node (`Typer::typeMemberDefaults`), lowered as
the entry the list did not write. lane-temp: a class temporary with a destructor and a call's class result the caller
destroys, each guarded and destroyed at the end of its full expression. **The merged tree, gated as one:** Linux
`make test` exit 0, run.sh **1046 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693 changed
(96 added), ssa.sh **7,999 bodies, 7,087 lowered and verified (88.6%)**, 0 failed, coff 0 differ, c90 447 / 0,
exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its baseline; Windows **675 same, 0 differ, 340
refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`.

**M3b.2, round 10 of 2026-10-27 evening: parameter packs, a class returned from a call built in place, array members,
cl's `??1` of a class with a virtual base** (lane-pack2 `014e4fa` and lane-vbase `462c297`, `cffdfdb`, Opus 5.5, both
off round 9's `86d87bf`, merged as `928ba8e`). lane-pack2: `Ts... rest` makes one `var` node per parameter it declares
and `rest...` at a call one `name` node per argument (`noteDeclaredVar`, renamed from `noteCaught`), so packs lower
with nothing new in the lowering; `return f();` built in place lowers the call with `CallInfo::sret`, its result slot
the function's own result object (`ssaSretSlot_`). lane-vbase: the parser records an array member's element constructor
in `memberBuilt_` and a local array's in `ctorBySerial_`; the lowering builds each element first to last, guarded per
[except.ctor]/3, and destroys them last to first; a destructor of a non-polymorphic class with a virtual base is cl's
`??1`, leaving the virtual bases to `??_D`. Constructors of a class with a virtual base stay outside on both ABIs.
**The merged tree, gated as one:** Linux `make test` exit 0, run.sh **1049 / 0** at -O0, -O2, `-front=ssa` and
`-front=ssa -O2`, the golden 0 of 2,693 changed (108 added), ssa.sh **8,062 bodies, 7,192 lowered and verified
(89.2%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its
baseline; Windows **678 same, 0 differ, 340 refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`.

**M3b.2, rounds 11 and 12 of 2026-10-27 evening, gated as one: class values as objects, handlers and throws, a functor
called by its name - and past 90%** (lane-asg `b15b1df`, lane-eh2 `346d4a4`, lane-misc3 `2e9a0d0`, Opus 5.5, each off
round 10's `928ba8e`; round 11 merged as `b8bf83f`, round 12 over it as `0f4ebbc`). lane-asg: a class assigned by the
copy or move assignment the parser called (`Parser::assignCalls_`, `Typer::assignCall`) or by bytes where it copied
bytes, an assignment as an lvalue, `T x = f()` built straight into the local (`Typer::elidedInto`), and braces reaching
a constructor as parentheses do. lane-eh2: Microsoft `catch (T)` with no name (`noteMsCatch`,
`Typer::microsoftUnnamedCaught`), a thrown polymorphic temporary built by its own constructor and its argument
temporaries destroyed before the throw, a handler's own objects destroyed on every way out (`handlerLive_`), and a
`goto` that destroys what it leaves and ends the catches it leaves. lane-misc3: **a `-front=ssa` miscompile on main
mended** - a member function calling a class object by its bare name passed `this` (`ssa-functor-member`: `6 8` where
clang prints `105 1007`); the head must now name the member function the parser reached, a data member otherwise, and
anything else is refused by name. With it: `(*p)(x)`, the composite pointer type of a `?:`, and a local reference to a
base subobject. Round 11 alone measured 8,086 bodies, 7,263 lowered (89.8%) on Linux. **The merged tree, gated as one:**
Linux `make test` exit 0, run.sh **1054 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693
changed (128 added), ssa.sh **8,132 bodies, 7,376 lowered and verified (90.7%)**, 0 failed, coff 0 differ, c90 447 / 0,
exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its baseline; Windows **683 same, 0 differ, 340
refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`. **Found, not mended**: an inherited `operator()`
called unqualified, `operator()(4)`, is refused "'operator()' was not declared" where clang takes it.

**M3b.2, round 14 of 2026-10-28 morning: conversions into temporaries, static locals built at run time, virtual-base
constructors** (lane-conv `3533320`, `26ba5e2`, lane-sloc `7521f55`, lane-vctor `489e5b5`, Opus 5.5, each off `2aefedc`,
merged as `9a2e574`; the Driver and `FunctionFacts` conflicts between lane-sloc and lane-vctor kept both sides, every
record looked up through `facts.recordKey`). lane-conv: an empty class's `T()` recorded as a temporary, the converting
constructor `userConversion` chose recorded by the argument's node (`convertedBuilt_`) and built as the temporary a
const or rvalue reference or a by-value parameter takes, an array bound to a reference to an array, a derived object
copied to its base (`baseSubobject`), `P{a, b}` temporaries, and class captures by reference. lane-sloc: a static local
built at run time from a record of what `guardOnce` built (`Parser::staticGuards_`) - the guard, the abort pad or
funclet and the destructor's registration, on both ABIs; a noexcept body the parser wrapped; a constructor's function
try block on Itanium (`tryStatement` split into `tryOpen`/`tryClose`). lane-vctor: Itanium's C2 and D2 of a class with
virtual bases (the VTT as the second parameter, vptrs stored from it, each base built and guarded with its sub-VTT;
`vttSplit_`, `baseVtt_`), members of and conversions to a virtual base through the vbase_offset, and Microsoft's
constructors of such a class under cl's most-derived flag. **The merged tree, gated as one:** Linux `make test` exit
0, run.sh **1058 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of 2,693 changed (144 added), ssa.sh
**8,199 bodies, 7,540 lowered and verified (92.0%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0,
comment cap 0, the memory gate under its baseline; Windows hist 8,502 / 7,849 / 0, and **687 same, 0 differ, 340
refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`. **Found by the M3b.2 exit checks run under
`-front=ssa` this morning, with lane-shfix:** stage 0 segfaults compiling four of the compiler's own sources (so the
self-host under the flag fails), `winlink-check`'s `this-return` (ab) prints the wrong output, and the C90 case
`ce_cast_narrows` loses a case label's narrowing cast; throw-, sret- and catch-check pass under the flag at -O0 and -O2,
every case passes at `-front=ssa -O1` on both boxes, and the `ml64` spelling fails the same 64 cases with and without
it. **Found, not mended**: the walker segfaults converting a null pointer to a virtual base (x86_64-linux at -O1 and
-O2, x86_64-windows at -O0).

**M3b.2, round 15 of 2026-10-28: the three faults the exit checks found under `-front=ssa`, and arrays from strings**
(lane-shfix `a8e494b`, Opus 5.5, and lane-brace `b68cb5c`, Fable 5.1, both off `2aefedc`, merged over round 14 as
`d05cc58`). The M3b.2 exit checks, run under `-front=ssa` for the first time on 10-28, found three faults no case had:
`winlink-check`'s `this-return` (ab) printed the wrong output - a constructor the selection wrote did not leave `this`
in RAX for cl's callers (`X86_64Linux::returnThis`, now called by both `walkBody` and `selectBody`); stage 0 segfaulted
compiling four of the compiler's own sources - `->` on a class a call returns (its `operator->`) was read as a pointer,
now refused by name in `decay()`; and C90's `ce_cast_narrows` lost a case label's narrowing cast (`foldConstant` narrows
as the parser's fold does). lane-brace: an array of characters from a string literal (`stringArray`, narrow and wide),
a scoped enumerator as a constant, and a `{}` node per nested braced list (the record only). **The merged tree, gated as
one:** Linux `make test` exit 0, run.sh **1061 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, the golden 0 of
2,693 changed (156 added), ssa.sh **8,217 bodies, 7,557 lowered and verified (92.0%)**, 0 failed, coff 0 differ, c90
447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its baseline; Windows hist 8,520 / 7,865 / 0,
and **690 same, 0 differ, 340 refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`.

**M3b.2, round 16 of 2026-10-28: exceptions nested inside a Microsoft handler** (lane-msnest `7c5e410` and lane-mstry
`3206820`, Fable 5.1, off `d05cc58`, merged as `80acf60`; `d2822b8` and `53f2d5e` from the coordinator). A cleanup and a
`goto` inside a Microsoft handler (the pad's block a cleanup funclet of its own, chained through `msEnclosingPad()`); a
`try` inside one (its handlers funclets of their own, `msAddTryBlock` deferred and ordered post-order so an inner try
precedes its outer); and a Microsoft virtual base destroyed on unwinding under the walker's `$vb` count. The gate found a
walker fault the new case reached on Linux: a handler's end-catch pad was written only when the handler itself could
throw, so a `try` inside it whose body calls nothing jumped to an undefined `$endcatch` and did not link (`d2822b8`,
`handlerResumeUsed_`; the golden's 9 changed files are that pad, written over an empty range, on the three Itanium-ABI
targets). Behind it, a walker leak on Itanium: a handler's local is not destroyed when an exception leaves a `try`
nested in that handler (the inner chain hands over to the end-catch pad, past the local's cleanup) - registered as
`tests/open/handler-local-past-inner-try`; `-front=ssa` answers clang's. **The merged tree, gated as one:** Linux `make
test` exit 0, run.sh **1064 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, ssa.sh **8,250 bodies, 7,587 lowered
and verified (92.0%)**, 0 failed, coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate
under its baseline; Windows **693 same, 0 differ, 340 refused as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`.
Also measured on 10-28: `-front=ssa` output is deterministic - 28,521 outputs over four targets at -O0 and -O2, 4,264 of
them `-dump=ssa`, 0 differ.

**M3b.2, round 17 of 2026-10-29: the self-host under `-front=ssa`, and nested braced lists** (lane-finish `m3b2-finish`,
`d0c17da` Opus 5.5 and `43fe8f0` Fable 5.1, off `d05cc58`, merged over round 16 as `93f43c9`; `b2e438b` from the
coordinator). Stage 1 built by stage 0 under `-front=ssa` crashed: `Parser::parse()` pointed `current_` at its own local
`Program` and cxx3 copies a returned object, so what the lowering emitted after the parse - a class's type_info for a
throw or a catch - went into a dead one (`Typer::emitInto`). With that, every source of `src/` emitted different
assembly from stage 0 and stage 1: a subscript's two operands and a static reference's slot and bitcast were lowered as
two arguments of one call, so their order was the host compiler's (g++ and cl right to left, clang left to right); each
pair is sequenced now, the base first, clang's order, so the Mac's output is unchanged. And a catch of a class whose key
function is another unit's (`std::exception`, `std::bad_alloc`) emitted its `_ZTI`/`_ZTS` where `parse()` had pruned
them; Sema prunes them again. lane-finish: a plain class's local from a nested braced list and braces elided into its
members ([dcl.init.aggr]/11), and `int n = {}` / `int n{}` - a value-initialised scalar left as the stack had it under
`-front=ssa`, -104 where clang prints 0 - zeroed. **The merged tree, gated as one:** Linux `make test` exit 0, run.sh
**1065 / 0** at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, ssa.sh **8,254 bodies, 7,594 lowered and verified
(92.0%)**, 0 failed, the golden 9 of 2,693 changed (round 16's end-catch pad files), coff 0 differ, c90 447 / 0,
exclusions 126 sites 0 / 0, comment cap 0, the memory gate under its baseline; Windows **694 same, 0 differ, 340 refused
as recorded** at -O0, `-front=ssa` and `-front=ssa -O2`, and the plain self-host 84 of 84. The self-host comparison
under `-front=ssa` itself is still open.

**M3b.2, round 18 of 2026-10-30/31: class values that are not plain, member-pointer calls, defaults, range-for over a
class, class throws and catches** (lane-cval `ea461bd` and lane-cobj `a9eddb5` Fable 5.1, lane-parts `8b5d273` and
lane-misc `50d256d` Opus 5.5 finished by the coordinator, all off `5bfebe3`, merged as `9678cb0`). cval: a by-value
parameter, argument and local of a class with a destructor, a copy of its own, a vptr or a base, each ABI its own way.
cobj: class prvalues materialised - a cast through a converting constructor, `->` hop by hop, copied temporaries,
reference casts to a base. parts: `(o.*p)(...)` and `(q->*p)(...)`, a member call named past an operator's spelling or a
template-id, a base or a local built with defaults typed in the declaration's scope. misc: a range-based `for` over a
class's begin and end, an array `new` of a class through the vector constructor, a throw copied by its constructor and a
class caught by value. **Found finishing lane-parts**: a member pointer converted to a derived class's at one width is
retyped in place, and its new `&` record kept the old type - `typed.sh` 1 differ on `member-pointer-models`; the record
moves with the type now. **The merged tree, gated as one:** Linux `make test` exit 0, run.sh **1077 / 0** at -O0, -O2,
`-front=ssa` and `-front=ssa -O2`, ssa.sh **8,447 bodies, 7,992 lowered and verified (94.6%)**, 0 failed, the golden 9 of
2,693 changed (the same 9 as round 17), coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory
gate under its baseline. The Windows box is down: on the Mac (gates v3) the `-arch x86_64-windows` hist **8,739 / 8,296 /
0** (round 17: 8,546 / 7,923 / 0), and the Wine stand-in over the round's 165 cases 157 passed, 1 failed
(`temporary-unwind`, Wine 6.0's own) at `-front=ssa` and `-front=ssa -O2`. Owed to the Windows box: its cases, names
against cl and the self-host. Largest refusals left: a class local initialised by what is not an object (14), a catch of
`struct E` (11), a destructor called by name whose symbol is not recorded (10).

**M3b.2, 2026-10-31: the self-host under `-front=ssa` at -O0 matches, on round 18's tree** (`e25a828`, the coordinator).
Round 18 as landed (`c3458d1`) did not self-host: lane-cval's record copy was a member function template declared in
`Typer` and defined in `Typing.cpp`, which clang and g++ take and cxx3 refuses by name, so stage 0 could not build stage
1 - the round's gate ran no self-host and did not see it. The template is defined in its class now. **Linux, under
`-front=ssa` at -O0: stage 1 built by stage 0 (3m41s), stage 2 by stage 1, and all 84 sources give the same assembly at
stages 0, 1 and 2** - the comparison round 17 left open; `make test` exit 0 on the fixed tree (run.sh 1077 / 0, ssa.sh
8,447 / 7,992 / 0). From here the self-host comparison is part of every round's gate before it lands. Owed: -O2 (running)
and the Windows box's own comparison, the box being down.

**M3b.2, round 19 of 2026-10-31: outer captures, Microsoft catches of a class, and Fable 5.1's review answered** (the
coordinator: `c06f55a`, `b3ec043`, `2427a2c`, `e1b2a53`, `99c34cc`; Opus 5.5). A lambda inside a lambda reads its
enclosing closure's capture through the call operator's `this` (a scalar, or a class copied as bytes), and a closure
marked only for a member's class with a constructor is copied as bytes; a Microsoft by-value catch of any class is
lowered, the caught object guarded as a handler local; `p->~T()` of a class with no destructor does nothing; a call's
class result binds a `const &` or `&&` parameter. **Fable 5.1 reviewed M3b.2 at this round** (`doc-cxx3/
M3B2-REVIEW-FABLE-2026-10-31.md`): *on plan with conditions*, no blocking finding, no miscompile found in rounds 17-19.
Its conditions, as met: (1) the self-host is in this round's gate, below; (2) M3-IR 3.13 states the Itanium table and
what the lowering reads as built - the hooks in three kinds, which size M3b.7; (3) `tools/mangled-names` takes
`CXX1_FLAGS`, and the names row under `-front=ssa` ran - the symbol lists of every case for the three targets with the
flag against without it, 2,193 compared, 2,173 the same and 20 moved (all runtime calls: `operator delete` in 15 - the
[expr.new]/20 cleanup the SSA path does not write, recorded here, found not fixed - `__chkstk` in 4, `_Unwind_Resume`
in 2; the same 20 as round 16), and `names.sh` itself under the flag on those 18 cases on the Windows box (Visual
Studio's Git `sh`, clang 22): 16 passed, 2 failed for the host's clang alone (`bzero` for `memset`; no Linux or Darwin
`<new>`); (4) the structural refusals inside M3b.2's scope, with counts on Linux: a variadic function (8), x87 - a cast
to `long double` (9) and an x87 long double (5) - a local with alignas (5, refused by the Driver), a thread_local (4);
(5) **the -O2 allocator, measured**: under `-front=ssa -O2` `X64Parse.cpp`'s code generation takes 335 s (plain -O2: 1 s),
and `mir::Allocator::run` reports (under `CPP11_DECLINES`) `X64Target::directive` at 5,736 nodes and **2,096 colouring
rounds** where the walker's is 2,351 nodes and 11 - one value demoted per full rebuild; the file 28,126 nodes / 7,647
rounds against 9,629 / 315. The rounds are the cost, so round 20 demotes every failed value of a round (changing -O2
output on both paths), and the -O2 self-host waits for it. `tools/self-host` caps each compile (`SELFHOST_CAP`, 1200 s).
**The gates, said as what they are:** Linux `make test` exit 0; run.sh 1078 / 0 at -O0, `-front=ssa` and `-front=ssa
-O2` - 738 programs against clang's `.expected` and 340 refusals the parser makes before any lowering; ssa.sh **8,456
bodies, 8,018 lowered (94.8%)** and verified - the IR verifier, well-formedness, not behaviour; the emit golden 9 of
2,693 changed (round 16's files), coff 0 differ, c90 447 / 0, exclusions 126 sites 0 / 0, comment cap 0, the memory gate
under its baseline; **the self-host under `-front=ssa` at -O0, all 84 sources the same at stages 0, 1 and 2** on the final
tree. Windows (cl 19.44, a 5-core VM since 17:00, our jobs on 3 cores): every case against `.expected` at `-front=ssa`
and at `-front=ssa -O2`, GNU **706 same, 0 differ** and MASM **705 same, 0 differ**, 340 refused as recorded, both levels
(the MASM spelling needs `CXX1_MASM_SRC`/`CXX1_LINK_SRC`; a pass is four jobs through `tools/windows/par.cmd`'s shard range).
Windows-target lowering 8,748 bodies, 8,335 lowered, 0 failed. Largest refusals left (Linux): a reference parameter bound
to what is not an object (9), a local's cleanup in a function the parser gave no landing pads (9), a cast to `long double`
(9), a variadic function (8), a call with defaults or too many arguments (8), and a long tail - 438 bodies outside.

**Round 20, 2026-10-31: the allocator demotes every value a round could not colour.** Fable 5.1's finding 6, measured
at round 19 and taken before M3b.4 as the review allowed: `mir::Allocator::select` goes on past a node it cannot colour
and `run` demotes every promoted local the failed nodes hold, then rebuilds once - the same values demoted, far fewer
rebuilds. `X64Parse.cpp` under `-front=ssa -O2`: 2.3 s where it took 335 s, `X64Target::directive` **2 colouring rounds
where it took 2,096**, the file 209 where it took 7,647; plain -O2 1.15 s, 197 rounds where it took 315. `CPP11_DECLINES`
prints `alloc <function> nodes= homes= rounds= demoted= fellback=` per function. It changes -O1 and -O2 output only where
a function demoted more than once. **The gates:** Linux `make test` exit 0; run.sh 1078 / 0 at -O1, -O2, `-front=ssa -O1`
and `-front=ssa -O2`; Windows every case at `-front=ssa -O2`, GNU 706 same / 0 differ and MASM 705 / 0, 340 refused as
recorded; the bench against round 19 (`bench.cmd`, 9 interleaved rounds) **.text 3,641 bytes both** - fib 12/12, sieve
97/102, matmul 19/20, isort 34/35, hash 379/394, virtual 206/201, total 748/757 ms, cl /O2 701, checksums agree; **the
self-host under `-front=ssa -O2` on Linux, stage 1 = stage 2 on all 84 sources** - M3b.6's -O2 row reached early.
**Found and recorded: at -O2 stage 0 is not stage 1, and was not at round 19 either.** Every source differs between
g++'s build and cxx3's (Lexer 1,636 lines at plain -O2 with round 19's stages; the -O0-built and -O2-built stage 1 emit
the same), so the -O2 output depends on which library built the compiler - an ordering over ties or addresses in the
optimizer that libstdc++ and `include/` break differently, not a miscompile (stage 1 = stage 2, and every program agrees
with clang). `tools/self-host` asks s0 = s1 at every level; that holds at -O0 only. It is owed before M3b.8, whose close
asks stage 2 = stage 3 and would not see it.

**Round 21, 2026-11-01: the largest groups after the allocator, in three lanes and a coordinator's run.** **Linux ssa.sh
8,535 bodies, 8,237 lowered (96.5%), 0 failed** - round 20's 8,456 / 8,018 (94.8%), the new cases adding 79 bodies, all
lowered. What lowers now: a reference member, read through its slot (a by-reference capture's among them); an anonymous
union's member at its offset; a const integral local of the enclosing function read in a lambda, as the value the parser
stood in; `o.s` and `p->s` of a static member; a function bound to a reference to a function; a class temporary built in
a `?:` arm or a `&&`/`||` operand, by a flag zeroed where its full expression begins - on Itanium only, the Microsoft
funclet having no branch for it; a scoped enumeration as a scalar with no arithmetic; a reference to an array; an Itanium
pointer catch; a class's own operator new and delete, the `new` cleanup freeing by the class's own; a class local copied
from a call's result; a case label the parser folded; `: m()` of a class member, zeroed where its constructor is implicit;
`dynamic_cast` to a pointer and to `void *` on both ABIs. Lane **pads**: a body the parser gave no landing pads takes its
own (the walker leaks a declaration's temporaries on unwind - `tests/open/declaration-temporary-unwind.cpp`,
docs/CONFORMANCE.md), the backend following `ir::Function::landingPads`. Lane **mptr**: pointers to data members. Lane
**defaults**: an initializer_list constructor called from braces of flat elements, for a local and a member (7 of its 8
bodies were that, not defaults). New records the parser makes for the lowering, at parse time: a case's value, `: m()`'s
zeroing, a dynamic_cast's walk, a `new`'s class deallocation function, a local's or member's braced list, `&S::m`'s offset.
**A gate fault of this round, owned**: its first run.sh lines under `-front=ssa` named `CXX1_FLAGS`, which run.sh does not
read - plain -O0; every run.sh line here is with the flags inside `CXX1`, and a miscompile the false gate passed (a class
with a copy constructor copied as bytes) was caught by running the cases directly and mended before any commit landed.
**The gates on the final tree**: Linux `make test` exit 0; run.sh 1081 / 0 with CXX1 "-O2", "-front=ssa" and "-front=ssa
-O2"; the self-host under `-front=ssa` at -O0 (below). The Windows box was in fault from 01:30 to about 08:50 and again from
09:12, so **the Windows rows are the Wine stand-in on the Linux box** (`boxkit/wine-lx.sh`: every case for x86_64-windows
by the final tree's own `-c`, the project's LINK, Wine 6.0.3): `-front=ssa` and `-front=ssa -O2` each 706 passed, 2 failed -
`noexcept-terminates` and `temporary-unwind`, which Wine fails from any cxx3 - 31 skipped by `.notarget`, `ucn-identifier`
not built (its `.nomasm`). The stand-in found the one fault of the merge: the operand flag inside a Microsoft cleanup
funclet (new-class-array-placement, ssa-new-built-class, ssa-new-class-array), mended above. **Owed when the Windows box
answers**: the real `-front=ssa` and `-front=ssa -O2` passes, both spellings; and `tests/cases/ssa-data-member-pointer.expected`,
recorded from g++ while no clang was reachable, re-recorded from clang. Found and left: the parser refuses `f(nullptr)` for a
member-pointer parameter, and `const std::initializer_list<T> &` as an initializer_list constructor's parameter.
**Fable 5.1 reviewed round 21 before it landed** (2026-11-01, at the owner's request): *land, with four named fixes*. Taken
in the round: one predicate for the built flag and its refusal (`canFlag`), so the two cannot drift; a lambda's const local
recorded by the parser as a constant rather than inferred from its type; and the bulk it named - `: m()`'s zeroing a field of
MemberBuilt, no flag on a `new`'s storage guard, no debugging aid in a refusal, one comment per declaration. **Carried to
round 22**: fold `fullFrom_` into the `fullAt_` stack; one decision for a braced initializer_list (ParserTopLevel
recomputes what `bracedInitArguments` decides); one idiom for a null edge (`memberPointerConversion`'s block or
`dynamicCastPointer`'s operand poke, not both); a case value read from `TypedNode::folded`, not a fourth hook; the nine
sites where the parser's pads still gate a guard (CONFORMANCE says so now); a probe of `dynamic_cast` upcast through a base
at an offset on a null. `src/` went from 85882 to 86405 lines in round 21 (+523) for 219 bodies.

**Round 22, 2026-11-01: Fable's carried items, and the over-refusal the last of them found.** Every item round 21
carried is taken. A `CondBr` carries its edges' arguments, so `logical()`, `dynamicCastPointer` and
`memberPointerConversion` share one idiom for a null edge and the extra block goes; a case label's value is the case
node's `folded`, which `Typer::record` sets from the parser's case facts, and the `caseValue` hook goes; `fullFrom_` and
`fullAt_` are one stack of open full expressions (`Full`: where its temporaries begin in `live_` and where its built flags
are zeroed). The nine sites that guarded a call's result, a built member or base, a `new`'s storage and a thrown object's
operand only where the parser gave the body landing pads now guard always: `madePads()` decides the body's pads, and
`landingPads_`, `guard()`'s `unwinds` flag and the no-pads destroy path go (CONFORMANCE's SSA-pads paragraph says so).
`bracedInitArguments` reports the initializer_list it chose, so a mem-initialiser's member list is recorded from that
choice and not decided twice. The probe, `dynamic-cast-upcast-null`, found a parser over-refusal: [expr.dynamic.cast]/5's
upcast was asked /6's polymorphism first, so `dynamic_cast<B *>` on plain classes was refused where clang takes it; the
upcast is taken first now, and the case uses plain classes so it runs on every target. `ssa-data-member-pointer.expected`,
re-asked of clang on the Windows box, is byte-identical to the g++ recording it had. **Gated**: Linux `make test` green
(run.sh 1082/0, emit.sh 2930/0, coff 0 differ, c90 447/0, exclusions 0/0, comment-lines 0) and run.sh 1082/0 at `-O2`,
`-front=ssa` and `-front=ssa -O2`; **ssa.sh 8241 of 8539 lowered, 0 failed**; Windows (the box back at 7 cores, its passes
now 7 wide) `-front=ssa` and `-front=ssa -O2` 0 differ in both spellings (GNU 710 same, MASM 709); Wine at both levels
only its two known failures; the `-front=ssa` self-host at -O0 on Linux, stages 0, 1 and 2 the same on all 84 sources.
`src/` went from 86405 to 86378 lines (-27). The varargs and x87 lanes, started from round 21's tree, join round 23
with four lanes by family of the 298 bodies left: a class value where the lowering wants an object (~40), arrays of a
class (~15), temporaries and returns (~15), and statements (~10).

**Round 23, 2026-11-01 to 11-02: six lanes, and a miscompile round 22 shipped.** Two lanes started from round 21's tree
(x87, varargs) and four from round 22's by family of the 298 bodies left (objval, temps, arrays, stmts); the weekly
limit stopped all of them at 19:00 on 11-01 and they finished the next morning. **lane-objval** (Fable): one door to a
class prvalue materialised in a slot the lowering owns (`classObjectNode`/`classObject`), so a call's result, a `?:` of
class arms, a member operator's object, `.*`, a lambda called where it is written and a class read as a scalar through
its conversion function (the parser records the call, `conversionCalls_`, as `arrowCalls_` does) all take it; +36.
**lane-x87**: a `long double` on x86_64-linux - a 16-byte slot, `fldt`/`fstpt`, the walker's own x87 sequences and
conversions, a constant written as its 80-bit parts; +33. **lane-varargs** (Fable): `va_start`/`va_arg` as
`Op::VaStart`/`Op::VaArg`, the frame laid above SysV's register save area, both ABIs through one mechanism; +21.
**lane-temps**: a local of a class with a virtual base (C1 on Itanium, cl's most-derived flag on Microsoft through one
`msFlagged`), a converting constructor on `return`, a temporary bound to a reference living to its scope's end, a base
copied as bytes, a zeroed member default; +24. **lane-stmts**: references to functions, scalar temporaries bound to
references, `++x` as an lvalue, a closure's `this`, a lambda's `__invoke`, `o.A::m`; +32. **lane-arrays**: the parser
records an array's written bound count (`VarNote::ahead`, `TypedNode::bounds`), which **mends a miscompile round 22
shipped** - `int a[] = {1}` printed 0 under `-front=ssa`, the lowering having taken the `1` for the bound - and with it
nested arrays, plain classes and unions in braced lists, `new T{...}`; +28. The merge took five conflicts (each two
lanes extending one line, both kept; every `isX87` refusal the x87 lane made unnecessary dropped) and one raw pointer
the memory gate counted only once the lanes met (`va_arg`'s type, a `TypeId` now). **Gated**: Linux `make test` green,
run.sh 1085/0 at `-O2`, `-front=ssa` and `-front=ssa -O2`; **ssa.sh 8413 of 8571 lowered (98.2%), 0 failed**; Windows
`-front=ssa` and `-front=ssa -O2` 0 differ in both spellings (GNU 713 same, MASM 712). `src/` went from 86378 to 86757
lines (+379) for 172 bodies. Two outages on the way, neither the compiler's: the Windows VM restarted on a new address
(192.168.100.207, 7 cores, run at full load), and after a macOS update the app lost Local Network permission.

**Round 24, 2026-11-02 to 11-03: five lanes, two miscompiles mended, 96 bodies left.** From round 23's tree
(`d0e52502`). **lane-storage**: array cookies, `delete[]`, nothrow `new`, static arrays of a class built by their
loop; +8. **lane-cls** (with the stopped ctors lane's patch): class values where the lowering wanted an object,
delegating constructors and defaults on members, bases and delegation, a polymorphic temporary bound to a reference
or copied on return (`polymorphicTemporary`), `abortingThrow` for a throw in a `noexcept` function, an
`initializer_list` local, a reference-to-function member; +47. **lane-flow**: a class read through its conversion in
an `if` condition, `typeid`, `p->S::~S()`, a range-for over a temporary or an array of arrays, and **two miscompiles
mended** - the stopped run's work read `b->B::get()` as a destructor call, and round 23 shipped `p->~Base()` through a
typedef calling the object's own destructor; +27. **lane-addr**: an assignment as an lvalue, a class whose only
`operator=` is not a copy, a cast to a reference, a functor at namespace scope, a while-condition's declaration noted,
a class operand converted by its conversion function typed; +28. **lane-rest** (Fable): its own groups had been taken
by cls; it left a case for a throw in a `noexcept` function. The merge kept one of each mechanism two lanes wrote
(cls's `reinterpretsReference`, `assignScalar` and `voidConditional` - which took addr's refusal of a throwing arm -
and addr's `globalFunctor` and stricter derived-temporary binding). **Gated**: Linux `make test` green; **ssa.sh 8570
of 8666 lowered (98.9%), 0 failed** - 96 bodies outside, from 158; run.sh 1091/0 at `-O2`, `-front=ssa` and `-front=ssa -O2`; Windows `-front=ssa` 0 differ (GNU
719 same, MASM 718) and `-front=ssa -O2` 0 differ (the same counts). `src/` went from 86757 to 87254 lines (+497). What is left is a few
families each needing a fact the parser does not yet record (a braced list's structure, for aggregates and member
defaults; a lambda capture's copy constructor; `!=`/`++`/`*` of a class iterator), the backend pieces `alignas` locals
and `thread_local`, constructor exception specifications, and `typeid` of an expression; the stopped lanes' unverified
work is in `boxkit/r24-{addr,flow,rest}-late.patch`. One front-end fault found and registered:
`tests/open/by-value-polymorphic-base-temporary.cpp`. The session lost five hours to a scheduled resume that stopped at
its first permission prompt.

**Round 25, 2026-11-03: four lanes, 23 bodies left.** From round 24's tree (`b7ed0b50`), run unattended by a scheduled
session that stopped at 95% of its usage window and merged by hand after it. **lane-lists** (Fable): a braced list's
structure recorded once, `Parser::ListBuilt` - each element and how the parser built it (bytes, a constructor by symbol,
zeroed first, an `initializer_list`, the tail by the default constructor) - read by the lowering for an array of a class,
local and static, an `initializer_list`'s backing array and a braced argument's temporary; +10. **lane-backend**:
`alignas` locals (an alloca at the declared alignment), `thread_local`, a constructor's exception specification and a
function try block, `typeid` of an expression, `dynamic_cast` to a reference, a zeroed temporary; the registered
`by-value-polymorphic-base-temporary` moved from `tests/open` to the suite. **lane-lambda**: a lambda capture's copy
constructor, and a class iterator's `!=`/`++`/`*` in a range-for. **lane-lvals**: bit-fields read and written in SSA,
a member of an anonymous union in a block, a class prvalue through `static_cast<T &&>`; its bit-field was kept as
`const Member *` and is a copy now (the memory gate: 44 -> 49 raw pointers, back to 44). **Gated**: Linux `make test`
green - **ssa.sh 8655 of 8678 lowered (99.7%), 0 failed**, 23 outside, from 96 - run.sh 1093/0 at `-O2`, `-front=ssa`
and `-front=ssa -O2`; Windows `-front=ssa` and `-front=ssa -O2` through `winpass.sh`, 0 differ in both spellings (GNU 721 same, MASM 720).
`src/` went from 87254 to 88143 lines (+889). **A gate that checked half**: the lanes used `winssa.sh`, which runs the
GNU spelling only - `run-cases.cmd` finds no masm under a work directory's name and says so in its log, not its summary -
so every lane's Windows result was GNU-only; `winpass.sh`, which names the project's masm and LINK, is the gate. What is
left is 23 bodies, each its own shape: default arguments in an array's or an `initializer_list`'s elements, a member's
default initialiser, an expression statement of four parts, two binaries and a call the typer leaves untyped, a
range-for over a `pair`, a conversion to a class that is not a base, a lambda as a class local's initialiser. Then the
M3b.2 closing entry, and M3b.3.

**Round 26, 2026-11-03: four lanes, every source body lowered.** From round 25's tree (`de35f7be`), the 23 bodies left
in four groups; the lanes stopped at the usage limit at 16:35 and were resumed at 20:02. **lane-dflt** (Fable): the
defaults `applyDefaults` completes a hand-built call with recorded where the parser builds the call - a braced default
wears the `{}` node a braced argument gets, an array's and a list tail's defaults become the `var` node's last parts,
a list element's, an array element's and a `new`'s go under `noteEntryDefaults` - on round 25's `ListBuilt`, no second
fact; a member's own braced initialiser recorded as a written entry is; +6. **lane-brc**: an anonymous union's members
placed in an aggregate's list as one slot of the union's type, `x = {...}` as a two-part assignment (`bracedAssign`),
a braced range-for (`bracedRange` over `backingArray`, split from `initializerListOf`), `T({...})` as the `{}` call
node `T{...}` is, and a user-defined literal's operator spelled so its call has a node; +5. **lane-typ**: a static
member template reached through an object, a class operand's conversion call noted (which mended a parser fault: `k ==
42` through a conversion was typed `int`, now `bool`), a member array of a constructed class, a named cast to a class
(`convertedCast`), and a call through a data member with `operator()`; +6. **lane-obj**: a constant local captured by a
nested lambda (`LambdaCapture::constant`), a reference cast naming its operand's temporary, a subscript as a call's head,
[conv.mem]/2 for a pointer to a member function (`memberFunctionPointerInto`), a converted range-for variable
(`noteConvertedBuilt`, which `userConversion` now shares), and an aggregate whose members have constructors
(`ListBuilt::members`, `recordedAggregate`); +6. The merge kept brc's anonymous union and obj's members past a list's end
in `aggregateFill` together and refuses, by name, an anonymous union past the list's end, which neither lane measured.
**Round 25's tree failed its memory gate** - five raw `Type *` in the lowering, 49 over a baseline of 44 - which its
merge gate never reached, `make test` having stopped at the comment cap one line before; the five are `TypeId`s now.
**Gated**: Linux `make test` green - **ssa.sh 8678 of 8678 lowered, 0 outside, 0 failed** - run.sh 1093/0 at `-O2`,
`-front=ssa` and `-front=ssa -O2`; Windows `winpass.sh` at `-front=ssa` and `-front=ssa -O2`, 0 differ in both
spellings (GNU 721 same, MASM 720). `src/` went from 88143 to 88595 lines (+452).

**M3b.2 closed, 2026-11-03.** Every source body of the suite is lowered through the SSA path and verified: ssa.sh
8678 of 8678, from 74% on 2026-10-26 and 98.2% after round 23; what each needed is recorded above, and
the shape that recurs is a fact the parser knew and did not record - a braced list's structure, the defaults it
supplied, a conversion it chose - recorded once where the parser decides and read by the typer and the lowering, never
re-derived. `-front=ssa` is not yet the default and the parser's `Ast.h` path is still the oracle; making SSA the path
and deleting the old one is later M3 work. **Next: M3b.3**, the 807 compiler-written functions - implicit special
members, thunks, the init function, deleting destructors - which have no source body and so were outside
M3b.2's count.

**M3b.3, lane 1, 2026-11-09: the Builder, and the two largest kinds.** From M3b.2's tree (`09513e40`). **The design**:
`ir::Builder` (`src/ir/Builder.*`, a friend of `Lowering`) builds a compiler-written function with the lowering's own
pieces - `beginFunction` and `finishFunction`, factored out of `lower()` so a body and a built function share one prologue
(the Function reset and named, its return type checked, `this` and a VTT as parameters) and one return block; then
`memberInits` with no entries for a default constructor and `destroyMembers` + `destroyBases` for a destructor, `closeScope`
making the [except.ctor]/3 cleanups. What those read is the parser's record, and the synthesiser now leaves one as a
written constructor's does, keyed by the synthesised symbol: `synthesizeDefaultCtor` sets `currentFunction_` for its
duration (so `constructMember`'s and `memberInitialiser`'s records land under it - `constructMember`'s `!implicit` guards
are gone), records each base's `^B` and `~B`, its defaults (`noteEntryDefaults`), its sub-VTT entry (`baseVtt_`) and its
vptr stores, and the C2 a virtual base splits off as `vttSplit_`; `synthesizeDestructor` records each base's `~B` and
`baseVtt_`, and its D2 as `vttSplit_`. No second copy of any choice: the parser decides, the builder reads. The driver's
`lowerSsa` sends every `synthesised()` function through the Builder after the bodies (`factsOf` shared), and
`-check=ssa` reports `-check=ssa synthesised: functions= lowered= outside=` with `  synthesised lowered (n): kind` and
`  synthesised outside (n): kind - why`; `ssa.sh` sums them below the bodies. **Lowered**: implicit destructor 225 of 225,
implicit default constructor 193 of 194 - 418 of 876 compiler-written functions (the suite's count; the plan's 807 was
2026-11-03's), Itanium's C2 and D2 of a class with virtual bases among them, and the inheriting constructors, whose own
parameters the Builder takes as `this` is taken and `buildBase` hands whole to the base inherited from (`forwarded_`).
**Left, by kind**: an array member built with default arguments (1, which `buildElements` refuses for a written
constructor too); a class with a virtual base **on the Microsoft ABI** - cl's flag and guard, which the
parser records only under `guardedVirtualBaseInit(..., record)` and no box could verify today; and every other kind by its
name - deleting destructor 161, copy or move 101, complete constructor 37 and destructor 28, virtual thunk 29, vector
destructor 29 and constructor 14, init function 29, thunk 20, array destructor helper 8, tls init 1.
`tests/cases/implicit-members-builder.cpp` holds both kinds under the ledger - two bases, a vptr at two levels, an array
member, a member's `= 40`, `= { 1, 2.5 }`, `{}` and `{ 9 }` - its `.expected` g++'s and the Ast.h path's (clang was on
no box this lane reached). **Gated on Linux**: ssa.sh 8695 of 8695 bodies lowered, 0 failed, and the 418 above; run.sh
1094/0 at `-O2`, `-front=ssa` and `-front=ssa -O2`; `make test` green; three `Classes.cpp` citations re-pointed in
`docs/EXCLUSIONS.md`. **The Windows box was down** all day (192.168.100.207 timed out): `winpass.sh` at `-front=ssa` and
`-front=ssa -O2` is owed before landing. `src/` went from 88595 to 88759 lines (+164).

**M3b.3, lane A, 2026-11-09: the copies, the array loops and the exit helper.** From lane 1's `f817dce2`. **The design**
is lane 1's, applied to four more kinds in a file of their own, `src/ir/BuilderCopy.cpp`, with three dispatch lines in
`Builder::build`: the synthesiser records what it chose under the synthesised symbol and the Builder reads, deriving nothing.
`Classes::synthesizeCopy` records each base's copy or move (`^B`, the parser's choice between `moveConstructorOf` and
`copyConstructorOf`, or the assignments), its `~B` through `recordBaseDestructor` - the `~B` record lane 1 wrote inline in
`synthesizeDefaultCtor`, shared now - each member's copy (`m`, the element's for an array) and its vptr stores; a member or
base with no record is trivially copyable and is copied as its bytes, which is what the synthesiser's `Assign` does, and a
base with a record covers its members (`taken`). `Parser::vectorConstructor` records the element constructor (`^element`)
and, where the class has a destructor, the loop its guard destroys the built elements by (`~elements`, the `_del_` name);
`buildStaticArrayConstruction` records the helper's loop and the array's symbol and type (`$array`). What the lowering
gained is one field: `Live::count`, the slot counting an array's elements built so far, which `destroy` hands the class's
destructor loop as the second argument - the parser's `builtSlot` cleanup, calling `vectorDestructor` with `$i`. The
loops are blocks: `cond`, `body`, `done`, the index an alloca so the guard can read it; the copy constructor's bases and
array elements are guarded as a written constructor's are ([except.ctor]/3), and the Driver's `special` test gives a copy
or move `this` and the array loops none. **Lowered**: implicit copy or move 195 of 195 on the Windows host, 106 of 110 on Linux; vector destructor 32 of 32; vector
constructor 15 of 17; array destructor helper 9 of 9 - 713 of 1097 compiler-written functions on Windows, 585 of 898 on
Linux, with lane 1's. **Left, by kind**: two vector constructors built with default arguments (the element constructor's defaults
are syntax the record does not carry; lane 1's `buildElements` refuses the same), and a copy or move of a class with a
virtual base - the C1 split (`synthesizeCompleteCtor`, "complete constructor", lane C's) and cl's flag. **Refused by
name, never reached here**: a bit-field or reference member in a copied class, an array member past 16 guarded or 64
elements, a thread_local static array's helper on Microsoft. `tests/cases/implicit-copy-move-builder.cpp` holds every
kind under the ledger: a base copied by its own copy beside one copied as bytes, a polymorphic copy and assignment, an
array member element by element, a move beside a copy, `new M[3]` and `delete[]`, a static array destroyed at exit, and
an element constructor throwing mid-array; its `.expected` is cl 19.44's, and the Ast.h path at `-O0` and `-O2` and
`-front=ssa` at both print it. **Gated on Windows**: cl `/W4 /WX` clean; `ssa.sh` 9001 of 9001 bodies, 0 failed, 736 cases passed; `winpass.sh` at
`-front=ssa` GNU 723 same / MASM 722, at `-front=ssa -O2` GNU 723 / MASM 722, 0 differ in all four. **On Linux**: g++
`-Werror` clean, `make test` green (run.sh 1095/0, emit.sh 2982/0, ssa.sh 8723 of 8723 bodies, typed.sh and adapted.sh
753/0, exclusions 126 sites 0/0, comment cap 0), run.sh 1095/0 at `-front=ssa` and 1095/0 at `-front=ssa -O2`. `src/` went from 88759
to 89008 lines (+249).
**M3b.3, lane C, 2026-11-09: the thunks and the init function.** From lane 1's tip (`f817dce2`). **The design**: the
kinds are built in `src/ir/Wrappers.cpp`, two lines of dispatch in `Builder::build`. A thunk's adjustment is what the
ABI states in its name - `_ZThn<n>_<f>` moves `this` back by n, `_ZTv<f>_n<v>_<f>` by f and then by the vcall offset
at -v from the vptr found there, Microsoft's `<f>$adj<n>` back by n - so the name is read and nothing re-derived; the
parameters go on as they arrived and the callee is called through `callTail`. The init function is each namespace-scope
object in the order the parser met it, built and registered by `staticConstruction` - a static local's own path - and a
template static under its weak guard: the parser records each `leaveInitFunction` that pushed statements (symbol, count)
and the global `VarNote` gives it its `var` node (`Parser::dynInits_`), which the Typer types as a root of its own
(`Typer::dynamicInits`), the count checked against what `finishDynamicInit` made. An array member built with default
arguments evaluates them again per element (`buildElements`; `constructMember` records an array's defaults too).
**Found on the way, on the Ast.h path**: a thunk could not forward a reference parameter - refused, and once let
through it held the address as an int and returned a reference by value; the three thunk synthesisers hold it as its
address and bind a reference result, as the complete constructor did (`tests/cases/thunk-forwarding.cpp`, the ledger).
**Lowered**: thunk 27 of 27, virtual thunk 36 of 36, init function 28 of 29, the default constructor lane 1 left - 510
of 903 compiler-written functions. **Left, by kind**: an init function holding an array built at run time with no
record of how (`aggregate-member-constructor`), tls init 1 (`__tls_init`: thread-local addresses and its guard are
not built), and Microsoft's vcall thunk (`??_9`), which no Linux case reaches and no box here could run. **-front=ssa
does not reach the C6000**: `Tms6747Backend` takes no SSA module (`setSsa` is X86_64Linux's alone), so `-arch tms6747
-front=ssa` emits what the Ast.h path does - byte for byte on the cases measured. `src/` went from 88759 to 88946 (+187).
**Gated on Linux** (`~/m3b3c-b`): ssa.sh 8726 of 8726 bodies lowered, 0 failed, and the 510 above; run.sh 1095/0 at
-O0, -O2, `-front=ssa` and `-front=ssa -O2`; emit 2981/0, typed, adapted (1652 the same), coff 0 differ, c90 447/0,
syntax green; exclusions 126 sites 0/0 (seven citations re-pointed); comment cap 0; memory gate `new` 1056 (from 1057).
tms6747.sh at -O0 and -O2: 723 passed, 2 failed - `long-double-literal-exact-digits` (sim6747) and `ssa-eh-tail`, which
09513e40's compiler fails the same way on the same run - and every pass also on sim6747. names.sh and overload.sh skip
there (no clang++); the Windows gate is the coordinator's at merge, `thunk-forwarding.notarget` naming x86_64-windows.
**M3b.3, lane B, 2026-11-09: the complete object's functions.** From lane 1's tip (`f817dce2`). **The design**: the
deleting destructor, the complete constructor and the complete destructor are built in `src/ir/CompleteObject.cpp`
(`Builder::completeObject`, one line of dispatch in `build()`) from the records their synthesisers now leave, the way lane
1's two kinds read the implicit constructor's: `synthesizeDeleting` records what frees the object (`$delete`: the class's
own `operator delete` or the platform's, read off the `Call` `deallocate` chose); `synthesizeCompleteCtor` and
`synthesizeCompleteDtor` set `currentFunction_` to their C1 or D1 (`Keyed`, hoisted) and record the part they call
(`$C2`, `$D2`, with its signature), the VTT they hand it (`$vtt`, the `_ZTT` symbol - the Builder holds its address as
`vtt_`, so `vttAt` and `baseVttArgument` serve a C1 as they serve a C2), and through `virtualBaseCalls`, now recording
whenever it is handed an order, each virtual base's constructor or destructor (`^V`, `~V` - a copy's source parameter in
the signature, which is how the Builder tells the copy constructor's C1 from the default's), its VTT entry (`baseVtt_`)
and the order built or destroyed (`msVbaseOrder_`, which `Typer::msVbaseInit` answers without a vbptr record). The deleting
destructor calls `destructor_` - D1, or cl's `??_D` - then the deallocation, cl's `??_G` under its flag's low bit and
handing `this` back; D1 calls D2 and the virtual bases' D2s last built first; C1 builds each virtual base - by default, or
out of the source's own at the same offset - guarded by its D2 for the parts after it ([except.ctor]/3), then C2 with the
VTT and the forwarded parameters. **cl's most-derived flag and guard**, lane 1's one left shape, land with it: the
synthesiser records its guard (`guardedVirtualBaseInit(..., true)`), the Builder's `ownParameters` takes the flag last, and
`memberInits` already built the guard for a written constructor.
**Lowered** (`tests/ssa.sh`, 0 verifier failures on both): on x86_64-linux deleting destructor 166 of 166, complete
destructor 39 of 39, complete constructor 46 of 55 - 251 of 260; on x86_64-windows deleting destructor 302 of 302,
complete destructor (`??_D`) 24 of 24, and the implicit default constructor's Microsoft virtual-base guard 163 of 164
(the one left lane 1's array member). **Left, by name**: a complete constructor building a virtual base from a written
mem-initialiser's arguments (9 - the entry is the written constructor's node, which the C1's facts do not reach), and
the other kinds as before. `tests/cases/complete-object-builder.cpp` holds the deleting destructor through a base pointer
(the platform's and a class's own `operator delete`), a diamond's C1 and D1, an implicit pair split by a virtual base, a
virtual base built then a member throwing, and a polymorphic class with a virtual base deleted. **Two faults the case met
are the Ast.h path's and older than this lane** - on both ABIs, at every level, and lane 1's tip and the Linux box's
earlier compiler reproduce them - so they are `tests/open/`'s now, with clang's answer beside each: the copy constructor
the compiler writes for a class with a virtual base skips the class's own member (`virtual-base-copy-own-member.cpp`,
the copy or move kind's), and a virtual base with a virtual base of its own dies when built
(`virtual-base-of-virtual-base.cpp`, the layout's or the VTT's). The complete constructor's copy branch is gated by the
suite's own diamond copies. The `.expected` and the two `.clang`s are clang-cl's on the box; the case prints
it under `-O0`, `-O2`, `-front=ssa` and `-front=ssa -O2` there. **Gated on Windows**: `winpass.sh` at `-front=ssa`
and at `-front=ssa -O2`, gnu 723 same / 0 differ and masm 722 same / 0 differ, 340 refused as recorded, 0 not, cl /W4
/WX clean. **On Linux**: ssa.sh 8758 of 8758 bodies, 0 failed; `tools/exclusions --check` 126 sites 0/0 (eight citations
re-pointed); memory gate 44, unchanged; comment cap 0.
`tests/run.sh` on Linux 1095 / 0 at `-front=ssa` and at `-front=ssa -O2`, and `tests/open.sh` names the two new entries
as open. `src/` went from 88759 to 88981 lines (+222, 177 of them `CompleteObject.cpp`).
**M3b.3, lane F, 2026-11-10: the vector constructor's defaults, `__tls_init`, an aggregate before main.** From `33e62b9d`.
**The design** is the other lanes', three kinds more in the files they already use: `vectorConstructor` records the defaults
it completes the element's call with under the loop's own key (`noteEntryDefaults("element", ...)`, `currentFunction_` the
loop's name for the call), and `Builder::vectorLoop` evaluates them again for each element, in a full expression of its
own, as `buildElements` does for an array member. `__tls_init` is the init function's per-thread twin: `leaveInitFunction`
records each object's statements whichever sink took them (`DynamicInit::tls`; `tlsInitStatements_` the count
`finishTlsInit` made one function of), `Typer::dynamicInits(out, tls)` hands either list, and `Builder::initFunction` builds
it under the thread_local byte `__tls_guard` on Itanium (none on Microsoft, whose CRT keeps the guard); `staticConstruction`
reaches a namespace-scope object through `staticAddr` - a thread_local's the thread's own (`Op::TlsAddr`, direct, as
`__tls_init` reaches it) - and registers its destruction through `atexitFor` (`__cxa_thread_atexit`, `_tlv_atexit`,
`__tlregdtor` for a thread_local). An aggregate at namespace scope whose members' classes have constructors -
`A g[2] = { { V(1), 2 }, { 3, 4 } };` - is built from the record a local's already left: `emitRecordedInit` (the six lines
the declaration path held, shared) puts it in the object's guard (`StaticGuard::elements`, `members`), and
`recordedAggregate` takes it as `given`; a member copied from a temporary of its class (`V(1)`) is its bytes through
`classValue`. **Lowered** (Linux): vector constructor 18 of 18, tls init 1 of 1, init function 31 of 31 (the new case adds one of each) - 938 of 951
compiler-written functions; **left by name**: a complete constructor building a virtual base from a mem-initialiser's
arguments (9) and a copy or move of a class with a virtual base (4), lanes B's and A's. **Refused by name, not reached by
the suite**: a vector constructor whose default argument makes a temporary with a destructor - the Ast.h path leaves that
temporary on the caller's pending list in a slot of the loop's frame (built=6 gone=3 over=2 where g++ balances), so it is
`tests/open/vector-default-temporary.cpp`, its `.clang` g++'s (no clang on the Linux box); and a thread_local reference in
`__tls_init`. **Also**: `Driver::hostGnuAssembler` tries `%ProgramFiles(x86)%` beside `%ProgramFiles%`, where Build Tools
installs, so the Windows box needs no `CPP11_AS`. `tests/cases/builder-tail.cpp` holds the kinds under the ledger - defaults
evaluated per element, an element throwing mid-array, a namespace-scope array of aggregates, a const one and a static one
from values, temporaries, lists and value-initialisation; its `.expected` is g++'s, the Ast.h path and `-front=ssa` at
`-O0` and `-O2` print it. **Gated on Linux** (`~/m3b3f`): ssa.sh 8803 of 8803 bodies lowered, 0 failed, and the 938 above; run.sh 1098/0 at -O0, -O2,
`-front=ssa` and `-front=ssa -O2`; emit 2993/0, the golden recorded from `33e62b9d` 0 of 2989 changed (4 added, the new case);
typed, adapted (1674 the same), coff 0 differ, c90 447/0, syntax green; exclusions 126 sites 0/0 (fifteen citations
re-pointed); comment cap 0; memory gate 44, unchanged. tms6747.sh 726 passed at -O0 and 725 at -O2, every pass also on
sim6747; its 2 failures at -O0 and 3 at -O2 (`long-double-literal-exact-digits`, `ssa-eh-tail`, and at -O2
`ssa-data-member-pointer`) are lane C's tree's too. names.sh and overload.sh skip there (no clang++); the Windows gate is
the coordinator's at merge. `src/` went from 89417 to 89495 lines (+78).

**M3b.3, lane E, 2026-11-10: the Microsoft shapes.** From `a723a3eb` (lanes 1, A-C, F landed), lane E's saved work
finished. **The design**: what cl does that Itanium has no twin of, in `src/ir/MsShapes.cpp` and the lowering's own
paths. The atexit helper `??__F<object>` calls the destructor the parser chose on the object it recorded
(`registerDestruction` keeps `$object` and `~object` under the helper's key), a thread_local's reached as `__tls_init`
reaches it. A jump out of nested handlers, each a funclet there, is `msLeave`: the innermost handler's objects destroyed,
a catchret to an exit its try's pad lists, and in that funclet the next handler left the same way (`msHandlers_` the
stack, a pending goto carrying it to `msGotoExit`). A handler that builds the function's result reads the caller's
pointer from a slot the entry fills (`resultPtr`); a flagged temporary is tested in a cleanup funclet's own blocks
(`canFlag` on both ABIs). The vcall thunk `??_9S@@$B7AA` is the thunk path with its slot read from the name (cl's
number: byte 8, slot 1) and the call made through the vptr (`virtualCallee`). A virtual base on Microsoft is
`vbptr + int(vbtable[n])`, n its place among the class's virtual bases (`virtualBaseAddress`, as the parser's
`microsoftVirtualBaseStep`). A constructor's by-value parameter with a destructor is destroyed in its return block, below
the parts it keeps, and by its guard's cleanup on the way out (it had been kept, a leak `closure-temporary-lifetime`
showed); a polymorphic class's `??1` with a virtual base takes the parser's vptr store like any. **Lowered** (Windows):
compiler-written 1123 of 1123 (atexit helper 25, vcall thunk 7, the new case adding two of each); source bodies 9106 of 9110. **Left by name**: a function
try block on a constructor or destructor on Microsoft (3), and a pointer to a data member in cl's virtual model (1).
`tests/cases/microsoft-shapes-builder.cpp` holds the shapes under the ledger on every target; its `.expected` is clang's
(the Windows box's); its `.nonames` the in-class C1/C2 and static-local names recorded elsewhere. **Gated on Windows**
(`m3b3e2-1`): the cases 0 differ in both spellings at `-front=ssa`, `-front=ssa -O2` and the Ast.h path (726 and 725 the
same, 340 refused as recorded); names.sh for the new case passes. **On Linux** (`~/m3b3e2`): ssa.sh 8835 of 8835 bodies,
0 failed, 943 of 956 compiler-written (the 13 left are lanes A's and B's, Itanium); run.sh 1099/0 at -O0, -O2,
`-front=ssa` and `-front=ssa -O2`; emit 2997/0, the golden recorded from `33e62b9d` 0 of 2989 changed (8 added); typed,
adapted (1681 the same), coff 0 differ, c90 447/0, syntax green; exclusions 126 sites 0/0 (one citation re-pointed);
comment cap 0; memory gate 44. names.sh and overload.sh skip there. `src/` went from 89495 to 89604 lines (+109).

**M3b.3, lane D, 2026-11-10: the virtual bases, finished (lane D2).** From `702d10d6` (lanes 1, A-C, E, F landed), lane D's
saved work (`m3b3d-wip.patch`, cut from `33e62b9d`) applied and finished. **The design** is the other lanes': the synthesiser
records what it chose, the Builder reads. A complete constructor whose written constructor names a virtual base in its list
builds it from that entry: the C1 reaches its C2's typed body by the key their records share (`setSplitBody`, the Driver's
map of each body handed a VTT by its C1), binds that body's parameters to the ones forwarded to it (`bindParameter`,
factored out of `lower()`; past the entry block its alloca is the entry's), and `buildBase` lowers the entry's arguments
and the defaults the parser completed them with (`Builder::writtenEntries`). An implicit copy or move of a class with a
virtual base is Itanium's C2 handed its VTT - `synthesizeCopy` now records the split (`vttSplit_`) and each base's sub-VTT
(`baseVtt_`), the virtual bases C1's - or cl's copy constructor, which copies them under its most-derived flag: the
synthesiser records the guard under the copy's own key (`guardedVirtualBaseInit(..., record)`), the Driver gives the copy
its flag fact, and `msVirtualBases` takes the source and copies each virtual base out of the source's own at the same
offset, handed flag 0, as `virtualBaseCalls` writes it; each base's copy is handed flag 0 too (`msFlagged`). A virtual base
with virtual bases of its own is destroyed on unwinding on Microsoft by its `??1`, the guard's `~V` record, where
`destructor_` names its `??_D`. Lane D's vbtable walk (`msVbaseSlot`) duplicated lane E's `virtualBaseAddress` and is
dropped: one implementation, E's. **The two open faults, mended on both paths and moved to `tests/cases`**: the implicit
copy took a base's range as its whole size, virtual part included, where the class's own members lie (`nvDataSize` now, in
`synthesizeCopy` and the Builder; a member of a virtual base copied whole by C1 or under the flag is not copied again) -
`virtual-base-copy-own-member`; and a virtual base of a virtual base (`virtual-base-of-virtual-base-ledger`, the old name
taken): on Itanium the implicit copy stored no vptr for a class that has one for its virtual base alone (measured: main's
compiler builds a Deep and dies copying it), and on Microsoft the flag stored the class's own vbptr and not the one inside
its virtual base W, which W's constructor reads `v` through - the layout now lists a virtual base's vbptr among the class's
(`ParserType.cpp`), so the guard stores it and `??_8Deep@@7BW@@@` is emitted, and the class's own table among several is `??_8Deep@@7B0@@`,
a back-reference (`vbtableSymbol`) - cl's names and entries, by dumpbin. Both `.expected` are
clang's on the Windows box (Build Tools clang 19.1.5, the `.clang` lane B recorded), and every run of each - both spellings
at `-O0`, `-O2`, `-front=ssa`, `-front=ssa -O2` there, the four levels on Linux - prints it. **Lowered**: Linux 8878 of 8878
bodies and 973 of 973 compiler-written (complete constructor 59 of 59, implicit copy or move 112 of 112); Windows 9149 of
9153 bodies and 1130 of 1130 compiler-written (implicit copy or move 197). **Left by name** (Windows bodies, as before): a
function try block on a constructor or destructor (3) and a pointer to a data member in cl's virtual model (1). **Refused
by name, reached by no case**: cl's implicit copy assignment of a class with a virtual base, and a virtual base holding two
vbptrs on Microsoft (`ParserType.cpp:1485`; the second vbptr's table and cl's name for it are unmeasured). **The golden**
(lane E's, from `33e62b9d`): 3 of 2989 changed, `virtual-base-construction-vtable` on its three targets - its implicit
copies now copy D's own `d` and `pad`, which the whole-size range skipped, and leave W's `w` to W's own copy: the first
fault, in a case that printed neither. **Gated on Windows** (`m3b3d2-1`): `winpass.sh` gnu 728 same / masm 727, 0 differ,
340 refused as recorded, at `-front=ssa`, `-front=ssa -O2` and the Ast.h path at -O0; cl `/W4 /WX` clean. **On Linux**
(`~/m3b3d2`): `make test`'s parts - run.sh 1101/0 at -O0, -O2, `-front=ssa` and `-front=ssa -O2`; emit 3005/0; ssa.sh
above, 0 failed; typed 759/0, adapted (1695 the same), coff 0 differ, c90 447/0, syntax green; exclusions 127 sites 0/0
(twelve `ParserType.cpp` and four `Classes.cpp` citations re-pointed, one added); comment cap 0; memory gate 44. names.sh and overload.sh skip
there; on Windows names.sh passes both new cases, their `.nonames` the C2 and `??_D` forms cxx1 emits beside the ones used.
`src/` went from 89604 to 89718 lines (+114).

**M3b.3 closed, 2026-11-10.** Every compiler-written function the suite makes is built as SSA through `ir::Builder`
and verified: **Linux 973 of 973** compiler-written and 8878 of 8878 source bodies; **Windows 1130 of 1130**
compiler-written and 9149 of 9153 bodies, the 4 refused by name - a function try block on a Microsoft constructor or
destructor (3) and a pointer to a data member in cl's virtual model (1). From 418 of 876 after lane 1 (2026-11-09), in
seven lanes: 1 (the Builder, implicit default constructor and destructor), A (copies, array loops, the exit helper), B
(deleting destructor, C1 and D1, cl's most-derived guard), C (thunks, the init function), F (vector constructor defaults,
`__tls_init`, an aggregate before main), E (cl's `??__F`, nested Microsoft handlers, `??_9`, the vbtable walk) and D (a
virtual base built from a mem-initialiser, copies of a class with a virtual base). The design held in every lane: the
synthesiser records what it chose under the synthesised symbol, and the Builder reads it, deriving nothing. **Faults of
the Ast.h path found and mended on both paths**: a thunk forwarding a reference (C), the implicit copy of a class with a
virtual base skipping its own member (`nvDataSize`, D), and a virtual base of a virtual base (Itanium's vptr in the copy,
cl's inner vbptr, D). **Refused by name, reached by no case**: a vector constructor's default making a temporary with a
destructor (`tests/open/vector-default-temporary.cpp`), a thread_local reference in `__tls_init`, cl's implicit copy
assignment of a class with a virtual base, and a virtual base holding two vbptrs on Microsoft. **Gated at `674e0520`**:
Windows `winpass.sh` gnu 728 / masm 727 same, 0 differ, at `-front=ssa`, `-front=ssa -O2` and the Ast.h path; Linux
run.sh 1101/0 at -O0, -O2, `-front=ssa` and `-front=ssa -O2`, `make test`'s parts green, exclusions 127 sites 0/0.
names.sh ran on Windows for each lane's new cases only, not the whole suite under `-front=ssa`; tms6747.sh was not
run after lane F (`-front=ssa` does not reach the C6000: lane C). `src/` went from 88595 to 89718 lines (+1123).
**Review point 4** (this plan's table: the compiler-written functions from the typed tree) is a stop before M3b.4's
commits; Fable reads only on the user's word (2026-11-10), so the review is owed and M3b.4 is planned, not begun.

**M3b.4, planned, 2026-11-10: -O1 on SSA.** From M3b.3's close (`674e0520`). **Where it stands**: `ir::Function` has
block arguments, `Inst::isVolatile` (printed by `-dump=ssa`, set by nothing) and every local an `alloca`; the -O passes
are MIR's (`src/optimizer/`), run on the selector's output, so `-front=ssa -O2` already works and is gated. **The design**
(section 4): new passes on `ir::Function` in a directory of their own, `src/ir/opt/`, each file `Ssa*.cpp` (no basename
may repeat `src/optimizer/`'s `Opt*`), one driver `ssaOptimize(Function &, level)` called in `lowerSsa` after `verify` and
re-verified after every pass under `-check=ssa`; the MIR pipeline still runs after selection at the same level, so a
round's measure is what SSA adds over today. `-dump=ssa=<pass>` writes the IR after that pass - the probe each pass is
held to. **The rounds**, one lane each, in this order because each needs the one before:
1. **mem2reg** (`SsaPromote.cpp`): dominators and frontiers (Cooper-Harvey-Kennedy), an `alloca` promoted when every use
   is a `load` or `store` of its whole type and none is volatile; the renaming writes the incoming value as an argument
   of each jump into the merge block - no phi. **Not promoted**: a slot live into a Microsoft funclet (the frame is the
   only way in, design 4.1's decision), a slot an `invoke`'s unwind edge reads on Itanium until the pads take block
   arguments, a class object, and an array. The guard flags of 3.8 are promoted here.
2. **Folding, copy propagation, algebraic simplification** (`SsaFold.cpp`): one worklist over the def-use chains;
   folding is the constant evaluator's arithmetic (one implementation), and a block argument every predecessor passes
   the same value for is that value.
3. **Dead code, dead stores, CFG simplification** (`SsaDead.cpp`, `SsaCfg.cpp`): an instruction with no use and no
   effect; a store to a non-escaping `alloca` no load can see; a block with one predecessor merged, a branch on a constant
   folded, an empty block forwarded. A pad, an `invoke`'s unwind edge and a volatile access are never removed.
4. **GVN** (`SsaValues.cpp`): dominator-tree value numbering of pure instructions; loads only within a block with no
   intervening store or call.
5. **Jump threading and division by a constant** (`SsaJumps.cpp`, `SsaDivide.cpp`): `DivMagic.h` shared with
   `OptDivide.cpp`; the threading of `OptJumps.cpp`'s shape (a block that only tests a value its predecessor set).
6. **`volatile`** (3.7's open item): the typed tree records the qualifier on the `var`'s declared type, the lowering sets
   `isVolatile`, every pass refuses to move or merge such an access, and `Program::usesVolatile`'s -O-off rule
   (`Driver.cpp:1578`) is lifted under `-front=ssa` only - 11 cases write `volatile`, each held to its `.expected` at -O1/-O2.
**Gate of each round**: `-check=ssa` 0 failed after every pass over the suite on both boxes; run.sh 0 failed at
`-front=ssa -O1` and `-front=ssa -O2` on Linux and Windows (both spellings, `winpass.sh`); its probe case's
`-dump=ssa=<pass>` recorded beside it in `tests/`; `make test` green. **Gate of M3b.4**: the row's - .text of the cases at
`-front=ssa -O1` not larger than main's `-O1` (`tools/windows/bench.ps1` sums it), and the benchmark numbers of
`tools/compilerpp-bench` recorded against main's. **Not in M3b.4**: the C6000 and arm64 (no SSA selector: lane C), the
inliner, LICM, widening and unrolling (M3b.5). **Before its first commit**: review point 4 is a stop; Fable reads only on
the user's word, so round 1 begins when the user asks for the review or waives it. **Waived 2026-11-10** - the
user: "Waive, start round 1".

**M3b.4, round 1, 2026-11-10: mem2reg on SSA.** From main `5ff21fc3` (M3b.3 closed, review point 4 waived), branch
`m3b4-promote`. **The infrastructure**: `src/ir/opt/` (`SsaOpt.h`; every file `Ssa*.cpp`, no basename repeated under
`src/`), named in the Makefile, `msvc/build.cmd`, `tools/self-host`, `tools/windows/self-host.cmd` and `ide/generate.py` -
whose projects had never listed `src/ir` and now list it and `src/ir/opt` (regenerated on the Linux box). `ssaOptimize`
runs each pass at its level on every function after `lowerSsa`'s lowering and its `verify`; under `-check=ssa` each pass
is re-verified (`ir::verify(const Function &)`), the report gains `-check=ssa passes: level= allocas= scalars= promoted=
failed=`, and a failure after a pass fails the compile; `-dump=ssa=<pass>` writes the IR after that pass, `-dump=ssa` still
the lowered IR. The level is the Driver's after the volatile rule, so a file that writes `volatile` runs no SSA pass
(round 6). The dominators are one implementation (`SsaDominators.cpp`, Cooper-Harvey-Kennedy, with frontiers): the
verifier's copy was deleted and the verifier re-pointed at it. **mem2reg** (`SsaPromote.cpp`): an alloca in the entry of a
scalar type (integer, pointer, floating; never a class or an array, never a call's `resultAlloca`) is promoted when every
use is operand 0 of a `load` or `store` of exactly its type, none volatile, all in one funclet. Liveness per slot is
computed backwards from its upward-exposed loads; **a slot live into a pad, or into any block an edge other than br,
condbr, switch or an invoke's normal return reaches** (unwind, catchswitch, cleanupret, catchret) is not promoted - which
is the plan's two exclusions at once: a slot live into a Microsoft funclet, and one an Itanium unwind edge reads. Parameters
go on the iterated frontier of the stores, pruned by liveness, after the lowering's own; a block that would need one and is
the entry or reached by such an edge refuses the slot. The renaming walks the dominator tree by an explicit stack and
appends each incoming value to the jump's arguments - no phi; a path that never stored passes an `undef` made at its end,
a load before any store becomes that `undef` in place, the blocks the entry does not reach have their loads made undefined
and their stores dropped. The dead loads, stores and allocas leave their blocks, their values lose their type (so the
selector gives them no slot), and the frame is compacted, `k` and `resultAlloca` renumbered. **The selector**
(`SsaSelect.cpp`) took block arguments on br, condbr and catchret only: an invoke's normal edge now writes them (both
ABIs), and a switch case with arguments goes through a stub after the ladder. **The probe**: `tests/cases/ssa-promote.cpp`
(a merge, a loop, a switch with a fall-through, an invoke, an escaped address, an array, a class, a slot live into a
handler; `.expected` is clang's, the Windows box's, and g++ agrees) and `ssa-promote.promote`, its
`-O1 -dump=ssa=promote` on x86_64-linux, which `tests/ssa.sh` compares byte for byte there; `ssa.sh` now runs at
`SSA_LEVEL` (default 1) and sums the counts. **Promoted**: Linux 9962 of 10984 scalar allocas (15475 frame objects);
Windows 9731 of 11257 (15740) - the difference is the funclet rule. **Gated on Linux** (`~/m3b4r1`): ssa.sh at -O1 and -O2
8888/8888 bodies and 973/973 compiler-written lowered, 0 failed after the lowering and 0 after promote, 761 passed (the
probe's dump among them); run.sh 1102/0 at `-front=ssa -O1`, `-front=ssa -O2`, `-front=ssa`, -O2 and -O0; emit 3009/0 (no
golden on that tree); typed 760/0, adapted 760/0 (1705 the same), coff 1492 objects 0 differ, c90 447/0, syntax 3/0;
exclusions 127 sites 0/0 (no citation moved); comment cap 0; memory gate 44 raw pointers, `new` 1056 (unchanged);
`ide/generate.py --check` clean. names.sh, overload.sh and win-structure.sh skip there (no clang). **On Windows**
(`m3b4r1`, cl /W4 /WX clean): ssa.sh 9159 of 9163 bodies (the 4 left are M3b.3's, by name), 1130/1130 compiler-written,
0 failed after promote; `winpass.sh` at `-front=ssa -O1` and `-front=ssa -O2` 0 differ in both spellings (729 gnu, 728
masm the same, 340 refused as recorded). **The .text** (`bench.ps1 -Cases`, new: each case compiled with `-c`, dumpbin's
.text summed; 740 cases): `-O1` 1,131,924 bytes; main's `-front=ssa -O1` 2,472,003; this round's 2,062,483 (-16.6% on
main's, 655 cases smaller, 0 larger) - still 82% over `-O1`, the accumulator selector's slot per value, which the rounds
after this one and selection are to close; recorded, not yet a gate. `src/` went from 89718 to 90188 lines (+470: +490 in
`src/ir/opt/`, the verifier -53, the Driver +18, the selector +14). **Left**: the guard flags of 3.8 are live into the pads
that test them and so stay in the frame until pads take block arguments; `tools/compilerpp-bench` not yet run against
main's (owed at M3b.4's close); names.sh under the flag not re-run (no symbol moves: the pass changes no name).
**Round 1 re-gated by the coordinator, 2026-11-10**: `winpass.sh` on a clean ship and cl build of `02b00d80` itself, gnu
729 / masm 728 same, 0 differ, at `-front=ssa -O1` and `-front=ssa -O2`. **A seventh round, added**: round 1's .text table
shows the SSA path 82% over main's `-O1` because the selector gives every value a frame slot - a cost no IR pass removes.
**7. Selection** (`SsaSelect.cpp`): an SSA value used only inside its block, or carried as a block argument, is handed to
the MIR allocator as a virtual register rather than a frame slot; M3b.4's .text gate is measured after it.

**M3b.4, round 2, 2026-11-10: folding, copy propagation and algebraic simplification on SSA.** From main `50233ba5`,
branch `m3b4-fold`. **The arithmetic is the constant evaluator's**: `Constexpr::fold`'s and `foldDouble`'s operator
switches moved whole into `src/sema/ConstArith.h` (`integerArith` - 64 bits, wrapping, the division-by-zero and
shift-range faults returned for the evaluator to refuse by its old messages; `floatingArith<F>` in F itself, a division
by zero refused; `floatingCompare<F>`), and `Lowering::normalise`'s canonical form with them (`sema::canonical`); the
evaluator calls them, so there is one copy. Where its types do not fit the IR: it reads operands as C values in 64 bits,
the selector's register reads a value of up to four bytes as 32 - so the pass widens each operand from the low 32 bits
by the operator's signedness before the call and canonicalises the result by its type; and the evaluator folds the least
value divided by -1 (a wrap) where `idiv` traps, so the pass refuses that itself. **The pass** (`SsaFold.cpp`, after
`promote` at -O1): def-use chains and a worklist of instructions and of blocks with parameters, over the blocks the entry
reaches. A folded instruction becomes an `iconst` or `fconst` in place (its operands are left for round 3's dead code); a
simplified one is replaced by its operand and leaves its block, its value given no type. Folded: integer arithmetic,
`neg`, `not`, `icmp`, the conversions by their two types (as `genConversion` reads them, whatever the operator: integer to
integer canonicalised, never to a `bool` that would hold other than 0 or 1; integer to float or double; float and double
either way; floating to integer only for a value the destination holds); `fadd`..`fdiv`, `fneg`, `fcmp` on float and
double, never x87. **Identities**: x+0, x-0, x|0, x^0, shifts by 0, x*1, x*0, x-x, x^x, x&x, x|x, `neg`/`not`/`fneg` twice,
an integer or pointer compared with itself; a replacement only where the operand has the result's exact type (the
verifier holds a branch argument to its parameter's type). **Copies**: a conversion of a type to itself. **Block
arguments**: a parameter every edge passes one value for, or itself, is that value when its definition strictly
dominates the block, or one `iconst` made at the block's head when the edges pass one constant; the parameter leaves the
block and every edge's arguments. Not touched: a pad, a block an invoke's unwind edge reaches, the entry, a volatile
access, a `condbr` on a constant (round 3; its condition is folded). **Left to run** ("kept"): division by zero, the least
value by -1, a shift past the type's width, a floating operation with a NaN in or out (the target's NaN, not the host's).
**Counts over the suite** (`-check=ssa fold:`, summed by `ssa.sh`), Linux: 7473 constants, 69 comparisons, 4923
conversions, 16 floating, 650 identities, 10913 copies, 5350 block arguments one value, 6 left to run; Windows: 7423,
69, 5093, 18, 758, 10768, 5787, 6. **The probe**: `tests/cases/ssa-fold.cpp` (constants through promoted locals, wraps,
compares signed and unsigned, conversions, floating, the identities, the three edge shapes, and the three refused
divisions and the NaN on a path never taken; `.expected` is the Windows box's clang) and `ssa-fold.fold`, its `-O1
-dump=ssa=fold` on x86_64-linux; `ssa.sh` now compares every `<case>.<pass>` probe.
**Gated on `de1b74a5`** (the code commit; this entry is the only change after it), each box from a clean ship and build.
**Linux** (`~/m3b4r2`): ssa.sh at -O1 and -O2 8893/8893 bodies and 973/973 compiler-written lowered, 0 failed after any
pass, 763 passed (both probes among them); run.sh 1103/0 at `-front=ssa -O1`, `-front=ssa -O2`, `-front=ssa`, -O2 and
-O0; `make test` to its end, rc 0; emit 3013/0 (no golden on that tree); typed 761/0, adapted 761/0 (1710 the same); coff
1494 objects 0 differ; c90 447/0; syntax 3/0; exclusions 127 sites 0/0 (one citation re-pointed, `Constexpr.cpp:732` to
`:718`); comment cap 0; memory gate 44 raw pointers, `new` 1056 (unchanged); `ide/generate.py --check` clean after its
regeneration there. **Windows** (cl /W4 /WX clean): ssa.sh at -O1 and -O2 9164 of 9168 bodies (M3b.3's four), 1130/1130
compiler-written, 0 failed after any pass, 743 passed; `winpass.sh` at `-front=ssa -O1` and `-front=ssa -O2` gnu 730 /
masm 729 same, 0 differ, 340 refused as recorded. **The .text** (`bench.ps1 -Cases`, 741 cases at `-front=ssa -O1`):
1,950,975 bytes; over the 740 both tables hold, 1,949,469 against round 1's 2,062,483 (-5.48%, 677 smaller, 12 larger -
the constants made at a block's head and the folded `iconst`s whose dead operands round 3 removes) and against main's
`-O1` 1,131,924 (+72.2%, the selector's slot per value: round 7). `src/` went from 90188 to 90731 lines (+543: SsaFold
+480, ConstArith.h +81, Constexpr -46, Lowering -6, the driver, header and pass table +34). **Left**: a `condbr` or
`switch` on a constant and the dead operands of a fold (round 3); a constant passed with `undef` on another edge is not
merged; no floating identity at all (each changes a NaN or a signed zero); names.sh under the flag not re-run (no name moves).

**M3b.4, round 3, 2026-11-10: dead code, dead stores and CFG simplification on SSA.** From main `2ddc44e3`, branch
`m3b4-dead`. **What may not move** (`fixedBlocks`, `SsaCfg.cpp`, one implementation both passes read): a pad, a Microsoft
funclet's block, the target of any edge that is no jump (`isJumpEdge`: br, condbr, switch, an invoke's normal return -
promote's test, now shared), and on the Microsoft try shape (a function with a catchswitch) every block a pad names by
index (the dispatch catchpad's exits, a handler's try pad, the catchswitch's enclosing pad and last handler block). Such a
block is never removed, merged or forwarded, its parameters never dropped. **cfg** (`SsaCfg.cpp`, rounds to a fixed point,
each removing a block or a successor): a condbr or switch on an `iconst` becomes a br, as does a condbr whose two edges
carry the same arguments to one block; the blocks neither the entry nor a fixed block reaches leave the body (so a pad
no invoke reaches any more stays, with what it branches to); a block whose one edge in is a br from a block of its
funclet merges into it, its parameters replaced by the br's arguments, its instructions keeping their source positions
(the br's goes to the first that has none); an empty block that only jumps is forwarded - each edge in re-pointed, its
parameters substituted - unless a parameter is read but by that jump. **dead** (`SsaDead.cpp`): first the stores to a
frame object whose address only whole loads and stores of its type reach (none volatile, none a call's result object)
that no load sees, by a backward liveness over every edge, unwind, catchswitch and catchret ones too; then liveness from
the effects (stores, calls, invokes, pads, terminators, volatile accesses, traps, va_*, and a division whose divisor is
not a constant other than 0 and -1) back through operands, and through an edge's argument to a live parameter; what is not
live leaves its block, its value given no type, a block parameter dropped from the block and every edge into it, an
alloca with it - the frame compacted by `dropFrameObjects`, promote's renumbering moved out of `SsaPromote.cpp` and shared.
**The driver**: promote, fold, cfg, dead, then `refold`, `recfg`, `redead` once, only where cfg or dead changed the
function (the limit: one repeat; each has its `-dump=ssa=` name). **The verifier** walks `f.blocks`, not the arena (a block
merged or removed stays there), and refuses a branch to a block not in the body. **The probe**: `tests/cases/ssa-dead.cpp`
(a branch and a switch on a folded constant, the arms and an unreached try they leave, a chain of gotos, empty arms, a
loop-carried value nothing reads, a handler's slot stored twice before an invoke and after its last read; `.expected` is
the Windows box's clang) with `ssa-dead.cfg` and `ssa-dead.dead`, its `-O1 -dump=ssa=cfg` and `=dead` on x86_64-linux,
which `ssa.sh` compares (it sums `-check=ssa cfg:` and `dead:` now). **Counts over the suite** (both rounds summed),
Linux: 69 constant branches and 4 two-way branches to one place made jumps, 213 blocks no root reaches removed, 20517
merged, 2161 forwarded; 22525 instructions, 1 block parameter, 51 stores and 46 frame objects removed. Windows: 69, 4,
139, 21086, 1025; 22654, 1, 79, 46. (Fold's own parameter rule leaves dead parameters little to do.)
**Gated on `72aee10f`** (the code commit; this entry is the only change after it), each box from a clean ship and build.
**Linux** (`~/m3b4r3`): ssa.sh at -O1 and -O2 8899/8899 bodies and 973/973 compiler-written lowered, 0 failed after any
pass, 766 passed (the three probes among them); run.sh 1104/0 at `-front=ssa -O1`, `-front=ssa -O2`, `-front=ssa`, -O2 and
-O0; `make test` to its end, rc 0; emit 3017/0 (no golden on that tree); typed 762/0, adapted 762/0 (1716 the same); coff
1496 objects 0 differ; c90 447/0; syntax 3/0; exclusions 127 sites 0/0 (no citation moved); comment cap 0; memory gate 44
raw pointers, `new` 1056 (unchanged); `ide/generate.py --check` clean after its regeneration there. **Windows** (cl /W4
/WX clean): ssa.sh at -O1 and -O2 9170 of 9174 bodies (M3b.3's four), 1130/1130 compiler-written, 0 failed after any
pass, 744 passed; `winpass.sh` at `-front=ssa -O1` and `-front=ssa -O2` gnu 731 / masm 730 same, 0 differ, 340 refused as
recorded. **The .text** (`bench.ps1 -Cases`, 742 cases at `-front=ssa -O1`): 1,837,565 bytes; over the 741 both tables
hold, 1,836,240 against round 2's 1,950,975 (-5.88%, 730 smaller, 4 larger) and over 740, 1,835,046 against main's `-O1`
1,131,924 (+62.1%, the selector's slot per value: round 7). `src/` went from 90731 to 91267 lines (+536: SsaCfg +277,
SsaDead +221, the verifier +5, the driver +12, the header +14, promote +3, the Driver's report +4). **Left**: a slot of a
`const` scalar is not promoted (its loads read the unqualified type, so round 1's whole-type test refuses it; the probe
writes `int mode`); a pad no invoke reaches is kept with its blocks (the rule above) though nothing can enter it; dead
stores are found only in frame objects whose address goes nowhere else, at the block level; names.sh under the flag not
re-run (no name moves).

**M3b.4, round 4, 2026-11-10: value numbering on SSA, and a const scalar local promoted.** From main `b668abb5`, branch
`m3b4-gvn`. **gvn** (`SsaValues.cpp`, after cfg at -O1): a walk of the dominator tree (`SsaDominators.cpp`'s, one
implementation) by an explicit stack, with a table of the pure instructions scoped by an undo log - iconst, fconst, null,
addr, integer and floating arithmetic, icmp, fcmp, the conversions, gep and select - keyed by op, type, the operands as
already replaced (add, mul, and, or, xor and an equality icmp with the two sorted; a floating operator keeps its order,
which NaN comes out), cmp, the constant (`k`, x87 `bits`) and the symbol. One found again in a block its first dominates
and in the same funclet is replaced by the first; one in another funclet shadows it for its own subtree. **Loads**, within
a block only: a load is the value of an earlier load of the same address and type, or of the store to that address whose
value has its type, with only pure instructions and loads between - any other instruction (a store elsewhere, memcpy,
memset, a call, va_*, a trap) or a volatile access forgets everything. **Not touched**: a pad's block, a volatile access,
`tlsaddr` (it may call its wrapper), `undef` and `alloca`. The replaced leave their blocks, their values given no type.
**The driver**: promote, fold, cfg, **gvn**, dead, then refold, recfg, **regvn**, redead once, where cfg, gvn or dead
changed the function; gvn sits after cfg so merged blocks give it longer runs of loads, and before dead, which takes the
stores its forwarded loads leave unseen. **Round 3's leftover**: a `const` local's initialiser is stored as a bitcast to
`const int` and loaded as `int`, so promote's exact-type test refused it; a slot now promotes when every access is in
its object's type (as before) or every access is in that type without const (`unqualified`, one function), a store's
value then read through the bitcast to its operand (`unqualifiedStored`). The bitcast is left for dead. **Counts over
the suite** (`-check=ssa gvn:`, summed by `ssa.sh`), Linux: 23439 constants and addresses, 665 arithmetic, 51
comparisons, 2552 conversions, 4043 geps, 0 selects; 98 loads given a load's value, 579 a store's. Windows: 23281, 655,
39, 2469, 4280, 0; 100, 1707. **Promoted**: Linux 10248 of 11047 scalar allocas against 10018 by round 3's compiler over
the same cases (+230, the const locals); Windows 10007 of 11322 (round 3: 9768 of 11300, one case fewer). Dead now
removes 5988 instructions on Linux (round 3 22525: the constants gvn takes first) and on Windows 1219 stores no load sees
(79: the stores whose loads gvn forwarded). **The probe**: `tests/cases/ssa-gvn.cpp` (an expression again under its
first, commuted, a comparison and a conversion again, an element address twice, a global loaded twice, after its store,
after a call and after a store elsewhere, a const local in a loop, a handler's twin of the body's value; `.expected` is
the Windows box's clang) with `ssa-gvn.gvn`, its `-O1 -dump=ssa=gvn` on x86_64-linux; `ssa-dead.dead` re-recorded (gvn
runs before dead). **Gated on `be79faa1`** (the code commit; this entry is the only change after it), each box from a
clean ship and build. **Linux** (`~/m3b4r4`): ssa.sh at -O1 and -O2 8907/8907 bodies and 973/973 compiler-written
lowered, 0 failed after any pass, 768 passed (the four probes' five dumps among them); run.sh 1105/0 at `-front=ssa
-O1`, `-front=ssa -O2`, `-front=ssa`, -O2 and -O0; `make test` to its end, rc 0; emit 3021/0 (no golden on that tree);
typed 763/0, adapted 763/0 (1724 the same); coff 1498 objects 0 differ; c90 447/0; syntax 3/0; exclusions 127 sites 0/0
(no citation moved); comment cap 0; memory gate 44 raw pointers, `new` 1056 (unchanged); `ide/generate.py --check`
clean after its regeneration there. **Windows** (cl /W4 /WX clean): ssa.sh at -O1 and -O2 9178 of 9182 bodies (M3b.3's
four), 1130/1130 compiler-written, 0 failed after any pass, 745 passed; `winpass.sh` at `-front=ssa -O1` and
`-front=ssa -O2` gnu 732 / masm 731 same, 0 differ, 340 refused as recorded. **The .text** (`bench.ps1 -Cases`, 743
cases at `-front=ssa -O1`): 1,811,449 bytes; over the 742 both tables hold, 1,809,523 against round 3's 1,837,565
(-1.53%, 363 smaller, 272 larger) and over 740, 1,807,147 against main's `-O1` 1,131,924 (+59.7%, the selector's slot
per value: round 7). **Measured and dropped**: constants numbered within their block only - +0.40% over numbering them
across blocks, so they stay global; the 272 larger (most the library cases: deque, unordered_map, map) are not traced -
the likeliest cause is the MIR allocator holding a value live across blocks that each block used to make again.
`src/` went from 91267 to 91482 lines (+215: SsaValues +186, promote +15, the driver +4, the header +6, the Driver's
report +4). **Left**: loads across blocks (the plan's rule: within a block); a store to one address forgets every other
(no alias analysis); a floating commutative operator is not canonicalised; a pad's block and `tlsaddr` are not numbered;
names.sh under the flag not re-run (no name moves).

**M3b.4, round 5, 2026-11-10: jump threading and division by a constant on SSA.** From main `88abdde5`, branch
`m3b4-jumps`. **jumps** (`SsaJumps.cpp`, OptJumps.cpp's shape): a block T that ends in a condbr or switch, has parameters,
and holds only constants, icmps and integer conversions read nowhere else, entered by a jump edge (`isJumpEdge`) whose
arguments decide the test, has that edge re-pointed to the successor S the test picks, with S's arguments (T's parameters
replaced by the edge's). The evaluation is fold's: `foldCompare` and `foldIntConversion` moved out of `Folder` into free
functions (`SsaOpt.h`), the folder calling them - one implementation. T, S and the edge's block are never fixed
(`fixedBlocks`, one fact: no pad, no funclet boundary, no unwind edge) and S is in the edge's funclet. **A parameter of T
read below it** (the loop variable a flag's test guards): where S dominates every such read, S - whose one edge in must be
T's - takes it as a new parameter, T passing it and the threaded edge passing its own value, the reads S dominates
re-pointed; a read elsewhere that S reaches refuses the edge, as does one in a fixed block. One edge a step, the uses and
`SsaDominators` made again after each, at most a body's block count of steps. **divide** (`SsaDivide.cpp`, **-O2 only**:
OptDivide's own gate, "ten instructions for three" is a speed level's, -O1 being size): sdiv, udiv, srem, urem of a 4- or
8-byte integer whose divisor is an iconst with 2 <= |d| < 2^(bits-1) - a negative signed divisor as |d| with the quotient
negated (the remainder takes |d|), more than OptDivide (positive 32-bit only) - becomes shifts for a power of two (signed:
the rounding bias `(n >>s w-1) >>u (w-k)` added, a remainder by the cleared low bits), else Hacker's Delight 10-1: signed
`smulh(n, M)`, plus n where M reads negative, `>>s s`, plus `n >>u (w-1)`; unsigned `umulh(n, M) >>u s` where an exact
multiplier exists (as OptDivide), else left. The division becomes the last step in place, so its value is read where it
was. **The IR gains `smulh` and `umulh`** (`Ir.h`, `IrText.cpp`, M3-IR 3.2's table; gvn numbers them, commutative):
the selector's one-operand `imul`/`mul` with rdx as the result, 32 and 64 bits; MIR's table gains `mul` (kind Div,
rdx:rax) so no spelling meets an unknown mnemonic - a one-operand `imul` was already opaque there. A widened 64-bit mul and
shift was the other way for 32 bits and was not taken: no 64-bit form exists, and one op serves both widths. **DivMagic.h**
is the one implementation, now of `bits` 32 or 64: `signedMagic` the same loop over a width (q1, q2 wrapping at 64 as the
book's words do), `unsignedMagic` by an incremental quotient and remainder of 2^(bits+s) (no 128-bit product); OptDivide and
Tms6747 call it unchanged at 32 - `emit.sh` with a golden recorded by round 4's compiler (`be79faa1`) on this tree: 0 of
3029 files changed. **The driver**: promote, fold, **jumps**, cfg, gvn, **divide** (-O2), dead, then refold, **rejumps**,
recfg, regvn, redead where a pass changed the function - jumps before cfg (which removes the blocks threading leaves
unreached and merges a T left one edge), divide after gvn (a division found twice is made once; the repeat gvn numbers its
constants). **Counts over the suite** (`-check=ssa jumps:`, `divide:`, summed by `ssa.sh`), Linux: 713 edges threaded past
a condbr, 0 past a switch (the state machine's switch block holds the loop's own work); at -O2 200 divisions made shifts,
319 a signed and 106 an unsigned multiply-high (167 of the 625 at 64 bits), 182 by a constant left (an unsigned divisor
with no exact multiplier - 7, 641 - one past the signed range, |d| = 1). Windows: 698, 0; 199, 316, 106 (142), 178.
**OptDivide under -front=ssa -O2** (its `imul %rdx, R` counted in every case's `-S`, Linux): round 4's compiler 44 places
in 14 cases, this round's 1 place in 1 case (a division SSA leaves; which, not traced); `-O2` on the Ast.h path,
which keeps it, 198 in 31. **The probes**: `tests/cases/ssa-jumps.cpp` (a flag tested after a loop's exit, `&&` and `||`
tested again, a state machine, a loop entered with its test known, a value read at a join, refused) with `ssa-jumps.jumps`,
its `-O1 -dump=ssa=jumps`; `tests/cases/ssa-divide.cpp` (/ and % of the four types by powers of two, multipliers, negative
divisors, divisors left, over each range's ends; a divisor a const local) with `ssa-divide.divide`, its **-O2**
`-dump=ssa=divide` (`ssa.sh` now compares a `.divide` at -O2); `.expected` the Windows box's clang, g++ agreeing; both run
right at -O0, -O1, -O2 and `-front=ssa` at each. `ssa-gvn.gvn` re-recorded: jumps runs before gvn (an edge passing the
compared value itself threaded, T merged). **Gated on `f2b2cddd`** (the code commit; this entry is the only change after
it), each box from a clean ship and build. **Linux** (`~/m3b4r5`): ssa.sh at -O1 and -O2 8921/8921 bodies and 973/973
compiler-written lowered, 0 failed after any pass, 772 passed (the six probes' seven dumps among them); run.sh 1107/0 at
`-front=ssa -O1`, `-front=ssa -O2`, `-front=ssa`, -O2 and -O0; emit 3029/0 (and 0 changed against round 4's golden);
typed 765/0, adapted 765/0 (1738 the same); coff 1502 objects 0 differ; c90 447/0; syntax 3/0; exclusions 127 sites 0/0
(no citation moved); comment cap 0; memory gate 44 raw pointers, `new` 1056 (unchanged); `ide/generate.py --check` clean
after its regeneration there. **Windows** (cl /W4 /WX clean): ssa.sh at -O1 and -O2 9192 of 9196 bodies (M3b.3's four),
1130/1130 compiler-written, 0 failed after any pass, 747 passed; `winpass.sh` at `-front=ssa -O1` and `-front=ssa -O2` gnu
734 / masm 733 same, 0 differ, 340 refused as recorded. **The .text** (`bench.ps1 -Cases`, 745 cases): `-front=ssa -O1`
(threading alone) 1,812,358 bytes; over the 743 both tables hold, 1,795,341 against round 4's 1,811,449 (-0.89%, 95
smaller, 68 larger) and over 740, 1,791,032 against main's `-O1` 1,131,924 (+58.2%, the selector's slot per value: round
7). `-front=ssa -O2` (threading and division together; round 4's compiler measured the same way): 1,823,738 against
1,816,471 (+0.40%, 92 smaller, 97 larger) - division adds about 1.3% where threading takes 0.9%: a multiply sequence is
longer than `idiv`, and each of its constants takes a slot until round 7. `src/` went from 91482 to 91900 lines (+418:
SsaJumps +226, SsaDivide +148, fold's two functions out +9, the header +13, the driver +5, the selector +9, the Driver's
report +4, DivMagic and OptDivide +3, MIR's table +1). **Left**: a switch block that does work before its test, and a
condition known on an edge because the edge's own branch tested it (no edge facts), are not threaded; T's instruction
results read below it refuse it (only parameters move); an unsigned divisor with no exact multiplier (the 33-bit `add`
form Tms6747 uses is 32-bit only and not taken); divisions of 1- and 2-byte types; names.sh under the flag not re-run (no
name moves).

**M3b.4, round 6, 2026-11-11: `volatile` on SSA.** From main `67134e78`, branch `m3b4-volatile`. **What is recorded**
(the record pattern of M3b.0, decided where the parser decides; no type change, so no name moves): the parser sets
`isVolatile` on the object it accepted the word on - `Local` (a block's declarator through `VarNote`, a parameter),
`GlobalSym` (the three object paths at file scope), `Member` (both data-member paths, bit-fields too) and
`Type::StaticMember` - and `Resolved::isVolatile` on a name's record; the Typer copies it into `Resolution::isVolatile` on
the name and on a local's `var` (`-check=typed` compares it). A data member's is read from its `Member`, which the lowering
already holds. **No pointee is recorded**: every pointer or reference to a volatile type is still refused by name (the
four `*-refused` cases), so none reaches the lowering. **The lowering** (`markVolatile`): the address of such an object -
a local's or parameter's alloca, a global's or static member's `addr`, a member's gep - is marked, and `add` marks each gep
and bitcast made from a marked address and sets `Inst::isVolatile` on every load and store through one (one place for
every access, `load`, `store` and direct `add` alike). **The passes**: each already refused a volatile access; the driver
now counts them before the passes and after each one, and under `-check=ssa` a pass other than cfg that changes the count
fails the case (`-check=ssa volatile: lowered= kept=`, summed by `ssa.sh`). **Counts over the suite**: Linux 55 lowered, 55
kept, at -O1 and -O2 (ssa-volatile 34, volatile-object 14, chrono-clocks 3, the four value-init and mem-init cases one
each, array-parameter-name 0 - its `volatile` is in a comment); Windows 41, 41 (volatile-object does not compile there,
its `.notarget`). **The selector** writes each as a memory access (`ssa-volatile.cpp -front=ssa -O1 -S` read on Linux:
three stores of `w` and `never`, four accesses to `ticks` in `twice`). **The rule**: `Program::usesVolatile` keeps -O off
only for the code generator - MIR's passes promote frame words to registers (`Locals::promote`) and forward loads
(`foldLoads`, `forwardValues`) with no volatile bit, and a body the lowering leaves takes the Ast.h path - so in a unit that
writes `volatile` the SSA passes run at the -O asked and the MIR passes at 0, under `-front=ssa`; on the Ast.h path nothing
changed. The warning says "without the code generator's -O" under the flag. **The probe**: `tests/cases/ssa-volatile.cpp`
(a local read three times, stores no load sees and one never read, an internal global read twice in a block, a member, a
whole volatile object, an array, a volatile loop counter, a parameter, a const volatile; `.expected` the Windows box's
clang, g++ agreeing) and `ssa-volatile.dead`, its `-O1 -dump=ssa=dead` on x86_64-linux: 34 volatile accesses as lowered,
34 after dead. Every case that writes `volatile` prints its `.expected` at `-front=ssa -O1` and `-O2` (the seven that
compile, and the probe). **Gated on `637d36d0`** (the code commit; this entry is the only change after it), each box from a
clean ship and build. **Linux** (`~/m3b4r6`, built from nothing): ssa.sh at -O1 and -O2 8930/8930 bodies and 973/973
compiler-written lowered, 0 failed after any pass, 774 passed (the probes' eight dumps among them, the seven older ones
unchanged); run.sh 1108/0 at `-front=ssa -O1`, `-front=ssa -O2`, `-front=ssa`, -O2 and -O0; `make test`'s suites and
gates one job each: emit 3033/0, typed 766/0, adapted 766/0 (1747 the same), coff 1504 objects 0 differ, c90 447/0, syntax
3/0, names, overload and win-structure skip (no clang); exclusions 127 sites 0/0 (34 citations re-pointed - the parser
lines moved); comment cap 0; memory gate 44 raw pointers, `new` 1056 (unchanged); `ide/generate.py --check` clean (no
file added). The box's disk is full (40G, 235M free): the tree was shipped without `obj/` and test output, and each gate's
output removed after it. **Windows** (cl /W4 /WX clean): ssa.sh at -O1 and -O2 9201 of 9205 bodies (M3b.3's four),
1130/1130 compiler-written, 0 failed after any pass, 748 passed; `winpass.sh` at `-front=ssa -O1` and `-front=ssa -O2` gnu
735 / masm 734 same, 0 differ, 340 refused as recorded. **The .text** (`bench.ps1 -Cases`): `-front=ssa -O1` over the 745
cases round 5's table holds, 1,805,542 against 1,812,358 (-0.38%, 0 larger, 5 smaller: the five that write `volatile` and
now take the SSA passes); over 740, 1,784,216 against main's `-O1` 1,131,924 (+57.6%, the selector's slot per value: round
7); `-front=ssa -O2` 1,817,139 against round 5's 1,823,738 (-0.36%, the same five). `src/` went from 91900 to 91963 lines
(+63: the lowering +16, the driver's count +18, the parser +14, the headers and the Typer +15). **Left**: a volatile local
in a range-based `for`'s declaration or a handler is not marked (only a block's declarators - a condition's too - and a function's
parameters go through `VarNote` with the word); `&v` still gives a plain `int *`, so an access through a pointer taken
from a volatile object is not volatile (the sweep's first leftover: no pointee without the type system); MIR's passes stay
off in such a unit until MIR carries the bit; names.sh under the flag not re-run (no name moves).

**M3b.4, round 7, 2026-11-11: selection - SSA values in registers.** From main `6c281528`, branch `m3b4-select`. **What
MIR receives** is the shape the Ast.h path's temporaries already had: a frame slot listed as a scalar local, which
`mir::Locals` renames to a pseudo and the one allocator colours or gives back (`Optimizer::temporaries`, `ssaScalars`) -
no second allocator, no pseudo written by the selector. Round 1's slots were listed but almost never promoted; four causes,
each measured on the box before it was mended. (1) **A value's slot is read narrower than it is written**: the walker
reads an `int` value with `movslq`, a char with `movsbq`, so the width check refused 1372 of the sampled slots. A value's
slot is now `opt::Local::value` (canonical 8 bytes), and a move into a general register reading its low 1, 2, 4 or 8 bytes
renames to the pseudo's low part (`valueReadWidth`, `OptFrame.cpp`, asked of value slots only - the Ast.h path's locals
are untouched). (2) **A frame object's address, a constant and null took a slot each**, live from the entry to their last
use and so across every call: they now take none (`ssaRemat`) and are made at each use - `lea` of the alloca's slot, the
immediate - in a register (`ssaLoadReg`) or, for the walker's call, return and va nodes, as `&$a` or a `Num`
(`ssaExpr`); the funclet's prelude of fresh `lea`s and `ssaInFunclet_` went with them. Making them at the use let
fold-offsets turn `lea; mov (%rax)` into a frame access, which met the parser's own locals the selector had still listed
at the same displacements (`movl $0, -4(%rbp)` renamed into a parser local's pseudo: seven wrong answers at
`-front=ssa -O1`, `enum-conversions` among them): under SSA `scalarsOf` lists the parser's parameters only. (3) **Two
allocator faults the SSA path exposed, shared with the Ast.h path**: the label `.Lfunc.end` after the return is a block
nothing reaches whose leave edge made every local live there, so on Itanium every local of a function with a call-site
table interfered with every other and lost its register (`passed` in exception-table-large: 25 of 25 demoted, now 7);
and a colouring whose failed nodes were all webs fell back whole, every local to its slot (`topBits` on Windows) - it
now demotes the locals those webs interfere with and colours again (`Allocator::demote`). (4) **A branch's arguments**
went through the stack; they are a parallel move now, each copy whose parameter no copy left reads first, through the
accumulator, and a cycle left over staged on the stack as before (`ssaBranchArgs`). **The pad rule** (design 4.1) holds
as MIR has it: a value live into a Microsoft funclet keeps its slot (`SharedSlots`, `Optimizer::flush`), and one live
across an Itanium unwind edge takes no caller-saved register (the pad's live-in joined at the call, `joinPads`); a
preserved register survives the unwind there, so no slot is forced - recorded, the Ast.h path's rule already. **The
probe**: `tests/cases/ssa-select.cpp` (narrow values, a swap carried round a loop - the parallel move's cycle -, values
across calls, a local's address handed out, va_list, a function pointer, a cleanup that unwinds; `.expected` the
Windows box's clang, g++ agreeing) and `ssa-select.allocate`, its `-front=ssa -O1 -dump=mir=allocate` on x86_64-linux,
which `tests/ssa.sh` now compares as it compares the passes' probes. **Gated on `9a9398d6`** (the code commit; this entry
is the only change after it), each box from a clean ship and build. **Linux** (`~/m3b4r7`, built from nothing): ssa.sh
at -O1 and -O2 8942 bodies and 973 compiler-written, 0 failed after any pass, 776 passed (the probes' nine, the eight
older unchanged); run.sh 1109/0 at `-front=ssa -O1`, `-front=ssa -O2`, `-front=ssa`, -O2 and -O0; emit 3037/0 (no golden
on the tree), typed 767/0, adapted 767/0 (1756 the same), coff 1506 objects 0 differ, c90 447/0, syntax 3/0; names,
overload and win-structure skip (no clang); exclusions 127 sites 0/0 (no citation moved); comment cap 0; memory gate 44
raw pointers, `new` 1056 (unchanged); `ide/generate.py --check` clean. **Windows** (cl /W4 /WX clean): ssa.sh at -O1 and
-O2 9213 of 9217 bodies (M3b.3's four), 1130/1130 compiler-written, 0 failed after any pass, 749 passed; `winpass.sh` at
`-front=ssa -O1` and `-front=ssa -O2` gnu 736 / masm 735 same, 0 differ, 340 refused as recorded.
**The M3b.4 gate: .text** (`bench.ps1 -Cases`, Windows):

| table | cases | base | this round | change |
|---|---|---|---|---|
| `-front=ssa -O1` against main's `-O1` | 740 | 1,131,924 | 1,131,154 | -0.07% (432 larger, 296 smaller) - **met** |
| `-front=ssa -O1` against round 6's | 746 | 1,807,529 | 1,144,335 | -36.69% (0 larger) |
| `-front=ssa -O2` against round 6's | 746 | 1,819,126 | 1,153,212 | -36.61% (0 larger) |

On Linux (`size -A`, 773 cases, the same binary for both paths): main's `-O1` 716,574; this round's `-O1` 713,118
(-0.48%, 157 smaller, 0 larger - the two allocator mends on the Ast.h path); `-front=ssa -O1` 698,004 (-2.59% on main's
`-O1`; main's `-front=ssa -O1` was 1,025,869). **The M3b.4 gate: `tools/compilerpp-bench`'s measure** (Linux, g++'s
build of each cxx3; Compiler++ from RIDE-4.7's projects, 32 files, built by main's cxx3 at `-O1`/`-O2` and by this
round's at `-front=ssa -O1`/`-O2`; inverse.cpp's bytecode and run identical from all four; 500 compiles a round, three
rounds; `boxkit/m3b4r7-cpb.sh`):

| build | .text | 500 compiles (ms, rounds 1-3) |
|---|---|---|
| main `-O1` | 362,456 | 5368, 5333, 5332 |
| this round `-front=ssa -O1` | 382,830 (+5.6%) | 5490, 5417, 5481 (+1.9%) |
| main `-O2` | 482,784 | 5547, 4217, 4176 |
| this round `-front=ssa -O2` | 408,582 (-15.4%) | 5501, 5214, 5262 (+25% on rounds 2-3) |

**The ten largest against main's `-O1`** (Windows, bytes over): deque-both-ends 1346, exception-table-large 1216,
map-set-node-stability 1123, algorithm-sort-large 990, library-cppparse-pieces 965, ssa-ms-try 917, range-for-vector
801, throw-out-of-handler 788, mem-init-two-rules 736, nested-handler-exits 726 - by cause: values live across calls
still take a slot (the allocator offers two preserved registers at -O1, the Ast.h walker keeps such temporaries in the
call's own registers; seen in `passed`, presumed and not measured for the five library cases); values live into a funclet keep theirs
(ssa-ms-try, throw-out-of-handler, nested-handler-exits: the funclet rule); a comparison is materialised by `setcc` and
tested again where the walker branched on the flags; mem-init-two-rules writes `volatile`, so MIR's passes are off and
every value is a slot. `-front=ssa -O2` is smaller and slower than main's `-O2`; why is not measured here (the inliner
and LICM are M3b.5's). `src/` went from 91963 to 91989 lines (+26: the selector -6 with the funclet prelude gone, the value reads +18 -
OptFrame +14, OptPasses.h +3, MirLocals +1 -, the allocator +10, X86_64Linux.h +4). **Left**: the gap case by case above;
the Linux `-O1` of the Ast.h path moved (the allocator mends), so emit.sh's goldens, where kept, change with it.

**M3b.4 closed, 2026-11-11: -O1 on SSA.** Seven rounds from `674e0520`, each one lane and each gated on its own code
commit after a clean build on both boxes: 1 mem2reg and the `src/ir/opt/` driver (`02b00d80`), 2 folding, copy
propagation and algebra (`de1b74a5`, the evaluator's arithmetic shared as `ConstArith.h`), 3 dead code, dead stores and
the CFG (`72aee10f`), 4 value numbering and a `const` local promoted (`be79faa1`), 5 jump threading and division by a
constant at -O2 (`f2b2cddd`, `smulh`/`umulh`, `DivMagic.h` still the one implementation), 6 `volatile` recorded on the
object and kept by every SSA pass (`637d36d0`; MIR's passes stay off in such a unit), and 7 - added after round 1's
measure - SSA values handed to MIR's one allocator as registers (`9a9398d6`). **The row's gate is met**: the cases'
.text at `-front=ssa -O1` is 1,131,154 bytes against main's `-O1` 1,131,924 (Windows, 740 cases; 2,472,003 before
round 1), and on Linux 698,004 against 716,574; `tools/compilerpp-bench`'s table is round 7's above. **Not met
elsewhere, recorded**: Compiler++ itself is 5.6% larger at `-front=ssa -O1` than main's `-O1`, and at `-front=ssa -O2`
15.4% smaller but about 25% slower than main's `-O2` - both M3b.5's to answer (the inliner, LICM, widening). **The
coordinator's check before landing round 7**, whose allocator mends move the Ast.h path: Linux run.sh 1109/0 at `-O1`
and `-O2`; Windows `winpass.sh` at `-O1` 736 / 735 same, 0 differ, and at `-O2` 735 / 734 same, **1 differ in both
spellings: `ssa-handler-exits`**, whose handlers print a destroyed exception's value wrong (`~E111` for `~E4`). Round 6's
compiler - main before round 7 - answers it wrong too, with a different value each build (`~E11`, `~E46`): an
uninitialised read on the Ast.h path at `-O2` on x86_64-windows only, older than round 7, right at `-O1` and under
`-front=ssa -O2`. It is open, not a gate this milestone held: no Windows gate since M3b.2 ran the Ast.h path at `-O2`.
`src/` went from 89718 to 91989 lines (+2271). **Next: M3b.5** (-O2 on SSA); its review point is RP5, after M3b.7.

**M3b.5, planned, 2026-11-11: -O2 on SSA.** From `33e715d8`. **Where it stands**: the SSA passes of M3b.4 run at -O1
and -O2 alike, plus `divide` at -O2; the MIR passes run after selection at the level asked. The inliner (`Inliner.h`)
decides over `Ast.h` and the walker inlines while it walks, so **under `-front=ssa` nothing is inlined** - the likely
part of Compiler++ at `-front=ssa -O2` being ~25% slower than main's `-O2` (M3b.4's close). **The rounds**, one Opus lane
each, in order:
1. **Measure first** (no pass): Compiler++'s hottest functions at main's `-O2` and `-front=ssa -O2` (perf on the Linux
   box), each gap given its cause - inlining, LICM, widening, or selection; the table recorded in the plan.
2. **The inliner on `ir::Function`** (`src/ir/opt/SsaInline.cpp`): `Inliner` re-pointed - its measures and budgets
   (`OptCosts.h`) kept as the one implementation, `measures_` a vector by `FuncId` (the determinism lint), `decided_` by
   call instruction; a callee's blocks cloned into the caller, its parameters the call's operands, its `ret` a branch with
   the value as a block argument; an `invoke` inlined with every call inside it made an invoke to the same pad - or refused
   by name, as the old eligibility refuses a callee with a landing pad, if the pad merge is past the round. Inlined before
   promote, so the callee's slots promote with the caller's.
3. **LICM and counter widening** (`SsaLoops.cpp`): natural loops from the dominators; a pure instruction whose operands
   are defined outside the loop moved to a preheader made for it; a load hoisted only with no store, call or volatile in
   the loop; an int counter compared and indexed as 64-bit where it cannot go negative (`OptLoops`'s rule, ported).
4. **Unrolling** of a counted loop with a constant trip count under the budget the old pass used (cppparse `:922`).
5. **What stays on MIR**, said in the closing entry: fold-index, hoist-xmm-slots, align-loops, and each MIR pass that SSA
   now duplicates either removed under `-front=ssa` or kept with its measured reason.
**Gate of each round**: the common gates at `-front=ssa -O1` and `-O2` (both boxes, both spellings), plus Windows winpass at
`-O2` on the Ast.h path while it stays the oracle. **Gate of M3b.5** (the row): bench on the box - no kernel slower than
main's `-O2`, the table recorded; Compiler++'s cases built under `-front=ssa -O2` print what clang's build prints; run.sh
`-O2` on every box.
