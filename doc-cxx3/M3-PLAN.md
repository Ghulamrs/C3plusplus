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
| **RP4** | **The compiler-written functions**. A stop | |
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
| 4 | M3b.3 | the compiler-written functions from the typed tree | yes |
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
