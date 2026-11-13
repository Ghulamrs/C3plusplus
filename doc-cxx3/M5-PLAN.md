# M5: arm64-darwin on the new middle end, threads over pthreads, self-hosting on targets 3 and 4

Written 2026-11-13 by the coordinator (Opus 5.5) from main `9b689e66`, M4 closed at `b19aad05`. Design section
12's row: *arm64-darwin on the new middle end (ends exception 1); library threads over pthreads; self-hosting on
targets 3 and 4* (R5.6). This note has the shape of `M3-PLAN.md` and `M4-PLAN.md`: what a step may change, how it
is judged, and "Where it stands" appended to as steps land.

## The Mac runs, for M5 only

The user, 2026-11-13: "Allow Mac runs for M5". Only M5's arm64-darwin legs run on the Mac - build, run, self-host
on target 4 - **one Mac job at a time**, through `boxkit/mac-run.sh <cap> <command>` (the lock `~/.maclock`, a cap
under 25 minutes, and a stop below 8 GB free). No compile past 9 minutes, no run past 3, no job past 25.
Everything else - x86 gates, the C6000, cl - stays on the boxes.

## What exists at the start (measured, not assumed)

- **arm64-darwin** is exception 1: `backend/Arm64Darwin.cpp` (1,331 lines) over `backend/Walker.cpp` (374) reads
  `Ast.h`, and `optimizer/A64Peep.cpp` (702) is its text peephole. It is held by byte identity (R6) since M3b.7.
- **x86's selection** is `backend/SsaSelect.cpp` (1,066): `ir::Function` into the walker's own stream, the frame
  laid above the parser's, a local MIR allocator giving registers (`opt::Local`), the backend reached through
  `Backend::setSsa`. arm64 follows the same shape - a selector over `ir::Function`, writing the stream the
  walker writes today - so the walker's output is the oracle one function at a time.
- **Threads**: `include/thread` and `include/mutex` already take pthreads on Linux and macOS (M1). What M5 owes is
  the measurement on target 4, not the code.
- **Target 3** self-hosts at stage 2 = 3 since M3b.6 (Linux, both levels). Target 4 has not self-hosted since the
  Mac rested (2026-10-26).

## Steps

| step | delivers | judged by |
| --- | --- | --- |
| M5.0 | the Mac baseline: `tests/run.sh` at -O0 and -O2 on the Mac with the walker; `tools/self-host` on target 4 at -O0, stages 0 = 1 = 2 (split by `SELFHOST_RESUME` under 25 minutes); the `<thread>`/`<mutex>` cases run on the Mac; R6's Mac leg | counts recorded with the commit; any wrong answer goes to `tests/open/` with clang's |
| M5.1 | `backend/A64Select.cpp`: arm64 selection of `ir::Function` - scalars, integer and floating arithmetic, compares, branches, phis by block arguments, loads, stores, calls under AAPCS64/Darwin - with a per-function fallback to the walker where a shape is outside its subset; a census row for arm64 | run.sh on the Mac at -O0/-O2 unchanged; the census fraction; `identical.sh ARCHS=arm64-darwin` names each changed file with its reason |
| M5.2 | the rest of the subset: aggregates by value, variadics, EH (the LSDA from `ir::Function::landingPads`), thread-locals, the allocator's registers on arm64; census 100% | as M5.1, census 0 outside |
| M5.3 | the deletion: the walker's arm64 `Ast.h` visits and `A64Peep` go in one commit; what the peephole gave is kept by MIR passes or measured as lost (bench on the Mac against the walker's build) | `tools/ast-residue` down; bench table; run.sh; self-host |
| M5.4 | self-host on target 4 at -O0 and -O2, stages 0 = 1 = 2 and 2 = 3; target 3 re-measured at the close | the counts, as M3b.6's |

**Lanes code, the coordinator gates** (`boxkit/brief-m3b5-par4.txt`). A wave's gate adds the Mac leg: run.sh at
-O0 and -O2 on the Mac, and from M5.4 the target-4 self-host, each through `mac-run.sh`.

## Where it stands
- **To undo at M5.3:** the five `arm64-darwin` lines m5b added to `.notarget` (walker defects, `tests/open/walker-*`) go when the walker goes; the selector must answer those cases as clang does.
- **R1, the probes (2026-11-13, lane r1p, from dd978ae1).** 40 smallest programs aimed at the lowering's `outside()`
  reasons in Lowering.cpp's file order (members, bases, locals, statics, initializer_list, returns, braced lists), each
  accepted by clang -std=c++11 -pedantic-errors over the tree's include/. Outcomes: **(a) 8 refused by name as cited**,
  now tests/cases/lowering-*.cpp with a .error: a static local built with default arguments; a reference member bound to
  another type; a class local initialised from a temporary of another type; an initializer_list of 'struct A' elements;
  a constructed class returned from another type; a class with a destructor returned from another type, and returned
  inside a handler; a braced list reaching a member the lowering does not place (a bit-field). **(b) 0 changed**; 26
  probes aimed at a reason compile and print clang's answer, so the shape a reason names is not the shape a program
  meets there (not counted as probed). **(c) 0 shown unreachable** this round. **One miscompile found**:
  `B() : a() {}` over `A(int = 7)` prints an uninitialised value at -O0 and -O2 (tests/open/
  lowering-member-value-init-default-argument.cpp). Four front-end refusals stand in front of a lowering reason and
  are in tests/open/ with clang's answer: a static and a thread_local array initialised at run time ("expected a
  constant initialiser", not by name), a reference member's default member initialiser, a member copied from a
  derived object (no `A::A` found). src/ lines: 0 changed. Owed: the miscompile first; the four front-end shapes; the
  reasons past Lowering.cpp:1414 and Builder*.cpp.
- **R1's finds, mended (2026-11-13, lane r1f, from d7661b32).** The miscompile and the four front-end refusals R1's
  probes put in tests/open/, each mended at its root and moved to tests/cases with clang's output (the Windows box's clang
  over the tree's include/). **The miscompile** was the parser's: `: a()` value-initialising a member whose default
  constructor is `A(int = 7)` called it with `this` alone, in both the walker's statement and the record the lowering
  reads (ParserTopLevel's value-initialised member) - now [dcl.fct.default]/4's defaults are applied and noted as a
  named entry's are (member-value-init-default-argument). **A static or thread_local array of scalars whose list does
  not fold** was laid down as data and refused "expected a constant initialiser": `leavesFold` sends it to the guarded
  run-time path, and the lowering stores it as a local array's list (staticArray), the reason "a static/thread_local
  array built at run time" kept for arrays of classes (static- and thread-local-array-runtime-initialiser). **A
  reference member's default initialiser** was refused by a check that fired exactly when an initialiser followed the
  declarator; it is gone, memberInitialiser binds the reference as `r(v)` does, and the lowering's reference branch
  takes it (reference-member-default-initialiser). **A member copied from a derived object** (`: a(d)`, A trivially
  copyable) went to overload resolution and found no `A::A`: constructMember converts to the base subobject first, and
  the lowering's `$bytes` copy takes the subobject as classLocal does (member-copied-from-derived). src/ lines:
  51 added, 21 removed over six files (ParserTopLevel, ParserInit, ParserClass, ParserType, Parser.h, ir/Lowering.cpp).
