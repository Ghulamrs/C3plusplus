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

The gate, at `5252c0a` merged with main's `eb930cc`. `identical.sh` against a REF of main plus `55885d0`, `b9628c6`
and `40b5bca`: **6,039 compared, 9 differ, the known nine** (a diagnostic naming `lib/` by its path), no assembly.
Emit golden (recorded at `7d593d5`): 6 of 1,924 changed, `namespace-unnamed` on three targets and main's three
thread-local Windows files. `-check=mir` at -O0 over every case and Compiler++ for x86_64-linux and both Windows
spellings: **1,959 units, 0 differ**. `CPP11_MIR_STRICT=1` at -O0 and -O2, the same sources and targets: **0
mnemonics the table lacks**. `-dump=mir` at -O2 run twice: **1,980 units, 0 differ**. Mac `make test` green. zeeshan:
cl build, the cases **958/0 at -O0 and at -O2 under MASM**, `self-host.cmd` 71 of 71 in the default spelling; the GNU
spelling's row is not run there, the box having no clang. Linux: g++ build, `run.sh` 988/0 at -O0 and -O2, self-host
71 of 71, `asm6x.sh` 652/0, `tms6747.sh` 571/0 and `ti-link.sh` 571 linked at -O0 and -O2.

**What part 2 of M3-IR.md says and the code does not, for RP2a to rule on**: the types keep today's names and file
(`opt::Instr`, `Operand`, `Entry`, `Directive` in `OptIr.h`), the rename into `Mir.h` being M3a.5's by the file table
of 2.1; `StrId` is one process-wide registry, written under a lock and read without one as the opcode table is, not a
per-function `StringTable` - a function's dump is still complete, since a dump prints the text; `Directive` has a
`RelroSection` kind the table in 2.4 lacks; and an immediate keeps `unsignedSpelling`, the `imm(unsigned)` past
`LLONG_MAX` the walker writes, which M3a.3 may fold into the table's widths.
