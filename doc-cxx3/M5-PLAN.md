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

### M5.0, the Mac baseline, 2026-11-13 (lane m5b, measured at `cd01e16b`)

The Mac (arm64-darwin, clang++, 12 processors), every job through `boxkit/mac-run.sh`; `make -j8` 20 s.
- **run.sh, the walker**: -O0 1154 passed, 5 failed; -O2 1154 passed, the same 5 failed. All five are M3b.7's
  SSA-path cases, never run on arm64 before (the Mac rested): `ssa-aggregate-member-temporary`,
  `ssa-aggregate-temporary-members`, `ssa-aggregate-value-temporaries`, `ssa-new-arguments-outlive` (the ledger
  unbalanced on unwind) and `ssa-census-last-bodies` (`?:` of lvalues bound to `const Slot &` binds a copy: 0 for
  clang's 1). Each is the walker's defect already registered for tms6747 as `tests/open/walker-*.cpp` with clang's
  answer; each case's `.notarget` now names arm64-darwin beside tms6747, pointing there. After: -O0 and -O2 1154
  passed, 0 failed (812 printed, 340 refused). M5.1-M5.3 close them by taking arm64 off the walker.
- **tests/open.sh on the Mac**: 30 differ, 2 agree - `array-static-tail-defaults` and
  `lowering-handler-return-class` are the SSA path's (x86) defects the walker does not have; they stay open.
- **tools/self-host, target 4, -O0** (`SELFHOST_JOBS=8`): stages 0, 1, 2, 3 compiled 100 of 100 sources in 15, 62,
  64, 62 s; **stages 0 = 1 = 2: 100 of 100 the same object; 2 = 3: 100 of 100**. No tools/ change was needed.
- **<thread>/<mutex>**: two cases include them (`thread-pool`, `thread-sleep-yield`), with `thread-local` and
  `thread-local-extern`: each passes in both run.sh runs and 10 of 10 repeated runs at -O0 and at -O2.
- **R6's Mac leg: done.** The walker's arm64 output, held by byte identity since M3b.7, runs right on the Mac
  outside the five registered walker defects above, and self-hosts to a fixed point.
- Owed to the wave gate: emit.sh's arm64-darwin list skips the five cases now; its golden count, unmeasured here.
