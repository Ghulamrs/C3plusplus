# M6: tms6747 on the new middle end (ends exception 2)

Written 2026-11-13 by lane M6a (Opus 5.5) from `cd01e16b`. Design section 12's M6 row: *target 2 on the new middle
end, in steps each gated by sim6747 cycles*. The shape is `M5-PLAN.md`'s: what a step may change, how it is judged,
and "Where it stands" appended to as steps land.

## What exists at the start (measured, not assumed)

- **tms6747** is exception 2: `backend/Tms6747.cpp` (2,458 lines) over `backend/Walker.cpp` reads `Ast.h` - a serial
  stack machine over A4, NOPs in the delay slots - and its text goes through `optimizer/C6xSched.cpp` (2,297) and
  `optimizer/C6xPipe.cpp` (2,007) at -O1 and -O2. It was lowered to SSA only under `-dump=ssa`/`-check=ssa`.
- **x86's selection** (`backend/SsaSelect.cpp`) and arm64's (lane m5a, `backend/A64Select.cpp`) have the shape this
  follows: `Backend::setSsa` hands the module over, a function inside the subset is written from `ir::Function` into
  the walker's own stream, and the rest stay on the walker, one function at a time, each with its reason.

## Steps

| step | delivers | judged by |
| --- | --- | --- |
| M6a | `backend/C6xSelect.cpp`: selection of `ir::Function` into the walker's text - 32-bit integers and pointers, arithmetic, compares, branches and block arguments, loads, stores, allocas, direct calls in registers, returns - the post-passes untouched; a per-function fallback named by reason (`CXX1_SSA_WHY`); `CXX1_C6X_WALKER` the walker all; a census row | tests/tms6747.sh and tests/asm6x.sh at -O0 and -O2 as before; sim6747 cycles of the selected cases against the walker's (equal or better, or named); `identical.sh ARCHS=tms6747` names each changed file |
| M6a.2 | the rest of the subset: 64-bit and floating values, class values and stack arguments, indirect and variadic calls, `-g`, then pads and the TI unwinder's tables from `ir::Function::landingPads`; census 0 walked | as M6a |
| M6b | selection emits MIR with schedule-aware allocation (values in registers, not words), still through the old post-passes | cycles equal or better case by case; tms6747.sh |
| M6c | the packetiser and pipeliner on MIR; `C6xSched`/`C6xPipe` text passes deleted once cycles are equal or better | the cycle table against M6a's; tms6747.sh, asm6x.sh |
| M6d | with no walker left on `Ast.h` (M5.3 and M6a.2), its nodes and the parser's building half deleted (design 12.2) | `tools/ast-residue` to 0; every suite |

**Lanes code, the coordinator gates** (`boxkit/brief-m3b5-par4.txt`). Every C6000 run is on the Linux box (TI's
tools in `~/ti6x`), never on the Mac.

## Where it stands

### M6a, round 1 (WIP), 2026-11-13, lane m6a

- **Landed (WIP):** `backend/C6xSelect.cpp` (new, ~330 lines); `Tms6747.{h,cpp}` +31; `Driver.cpp` 3 lines (tms6747 lowers
  and hands the module over, `CXX1_C6X_WALKER` the walker all, `level` passed to the lowering); `tools/ssa-census` 2 lines
  (the `-select: selected=` row); probe `tests/cases/c6x-select-scalars` (its .expected is the walker's and checked by
  hand - clang's from the Windows box is owed).
- **Subset:** words only (integers and pointers to 4 bytes), arithmetic, compares, branches, switch, block arguments
  through B16-B23, loads/stores (frame operands folded), allocas, direct calls of up to ten arguments in registers, returns.
  A function falls back to the walker with its reason under `CXX1_SSA_WHY` (`c6x-walker <sym>: <why>`).
- **Census** (tools/ssa-census -arch tms6747, before the last fix): 25,335 of 34,712 functions selected (73.0%); 12 other
  failures are the front end's target refusals (unistd.h, thread_local, realpath), not the selector's.
- **tests/tms6747.sh, vm6747 only (SIM=0), -O0:** walker 783 passed / 3 failed (the three noexcept cases, base); selector
  778 / 8 - five new wrong answers (namespace, namespace-relative-sibling, qualified-base-member-template,
  ssa-converted-arguments, using-declaration). The root found: a call passing a class of 8 bytes or less by value - TI
  passes it in registers, the IR a pointer to a copy; the refusal ("class argument", signature's class parameter) is
  written in the handover, not built. A function returning such a class was the first such fault, fixed and built.
- **Cycles:** worse, named - every value goes through its frame word (LDW + 4 NOPs), where the walker keeps A4: probe
  5,502 against 3,523 at -O0 and 1,760 against 746 at -O2; suite -O0 611.0 M against the walker's 447.7 M. Registers are
  M6b's; the cycle gate is not met by M6a round 1.
- **Gates:** cl /W4 /WX clean (Windows, before the last fix); exclusions 0 uncited 0 stale, comments 0 over, memory gate
  rc 0, ide/generate.py --check up to date. **Owed:** the class-argument refusal, then tms6747.sh with sim6747 at -O0/-O2
  (each run under 25 minutes: the SIM=1 leg reached the cap twice), asm6x.sh (C6XDIFF set in boxkit/m6a-gate.sh),
  identical.sh ARCHS=tms6747 by name (2,122 of 2,490 differed - every file with a selected function), -O2 cycles.

### M6a, round 2, 2026-11-13, lane m6a2

- **Landed:** `backend/C6xSelect.cpp` +2 (343 -> 345 lines): a direct call whose signature takes a class by value stays
  on the walker ("class argument") - TI passes a class of 8 bytes or less in registers, the IR a pointer to a copy. No
  other src/ line changed.
- **The five newly wrong answers of round 1 had this one root:** namespace, namespace-relative-sibling,
  qualified-base-member-template (a closure passed by value), ssa-converted-arguments, using-declaration - each a
  selected caller passing a small class to a walked callee. All five pass on vm6747 at -O0 after the refusal.
- **Gates (Linux, 94903ffa's tree):** build; exclusions 0 uncited 0 stale, comments 0 over, memory gate --check rc 0,
  ide/generate.py --check up to date; cl /W4 /WX clean on Windows. tms6747.sh SIM=0 (vm6747): -O0 783 passed /
  3 failed, -O2 783 / 3 - the walker's three noexcept cases, no selector failure left. Cycles (vm6747): -O0 611.0 M
  (unchanged, round 1's cost), -O2 311.7 M. Census (tools/ssa-census -arch tms6747):
  24,930 of 34,716 functions selected (71.8%; round 1 25,335 of 34,712 before its last fix), 12 other failures as before.
- **Owed (the coordinator's):** the sim6747 cycles leg, identical.sh ARCHS=tms6747, asm6x.sh; registers are M6b's.

### M6a, round 3 (registers), 2026-11-13, lane m6b

- **Landed** (`d3faa75b`): `backend/C6xSelect.cpp` 345 -> 605 lines, `Tms6747.h` +6. `ssaAssign`, as arm64's: a linear scan
  over live ranges (block liveness to a fixed point, weight by loop depth, the heavier range taking a lighter one's
  register). A range that crosses a call or the EABI's divide helper takes A10-A13 or B10-B13 - the registers the walker's
  planner promotes locals to, saved by the same prologue through `promoted_` - and any other A16-A31; a function with a
  seventh argument, its own or a call's, leaves A10, B10, A12 and B12 alone. A parameter reaches its home through
  `emitParams` (`regOf_`). An A-side home is an operand and a destination as it stands; a B-side one is copied through
  A4 or A6, so no instruction reads two registers across the files. A compare read only by its block's branch is written
  by the branch (`[A1]` or `[!A1]`, no XOR); a constant of 5 bits goes in ADD, SUB, AND, OR, XOR, the shifts and CMPEQ,
  a multiply by a power of two is a shift, a 16-bit constant is one MVK; block arguments move one by one where no move
  writes what another reads, else through B16-B23. `CXX1_C6X_HOMES` 0 gives no homes, 1 the saved registers alone.
- **What C6xSched and C6xPipe see:** homes only in registers the walker's own text already keeps locals in (its planner's
  A10-A13/B10-B13, its leaf's A16-A23) and A24-A31; no value moves between memory and a register unseen - a value has a
  home or a frame word for its whole life. Neither pass was changed.
- **Cycles, vm6747 (tests/tms6747.sh SIM=0 CYCLES=1, 780 cases):**

  | level | walker | selector, round 2 | selector, round 3 | cases better / equal / worse |
  | --- | --- | --- | --- | --- |
  | -O0 | 447.7 M | 611.0 M | 393.2 M | 376 / 116 / 288 (worse by 0.17 M in all; thread-pool 156 k of it) |
  | -O2 | 195.9 M | 311.7 M | 201.4 M | 336 / 319 / 125 (worse by 6.17 M in all) |

  Probe c6x-select-scalars: -O0 2,750 (walker 3,523; round 1 5,502), -O2 743 (walker 746; round 1 1,760).
- **The cycle gate is met in sum at -O0 and not at -O2 (+2.8%), and case by case at neither.** The -O2 loss is six loops:
  stack-probe +1.60 M, local-zero-init-large +1.02 M, pipelined-data-exit +0.92 M, fill-stores +0.64 M,
  array-loop-member-copy +0.47 M, array-loop-static +0.37 M - loops C6xPipe pipelines (and the fill pass stores as
  doublewords) in the walker's text and leaves as written in the selector's. Read from the text, not traced: the guard
  branches to the loop's head, and `recognise` refuses a head named twice ("the head is entered from elsewhere").
  **Tried and reverted:** edge stubs written after the next jump, so the guard falls into its loop - right (783/3 at both
  levels) and slower, -O2 202.9 M, stack-probe 4.0 M. The next step is CPP11_PIPE's trace on those six.
- **Gates (Linux, d3faa75b's tree):** build; tms6747.sh SIM=0 -O0 783 passed / 3 failed, -O2 783 / 3 - the walker's three
  noexcept cases; exclusions 0 uncited 0 stale, comments 0 over, memory gate rc 0, ide/generate.py --check up to date;
  cl /W4 /WX clean on Windows.
  Census (tools/ssa-census -arch tms6747, d3faa75b): 24,778 of 34,526 functions selected (71.8%), 13 other failures -
  the thirteenth was `C6xSelect.cpp` itself, `std::upper_bound` not being in cxx3's library; the following commit writes
  the search as a loop: cl /W4 /WX clean, and cxx3-msvc.exe then compiles the file for tms6747. Its Linux build and the
  proof that it writes d3faa75b's text over every case (boxkit/m6b-same.sh) were queued behind the lock at the hour.
- **Owed:** m6b-same.sh's verdict and the census re-run after that commit; the bench kernels' cycles; sim6747, asm6x.sh and identical.sh
  ARCHS=tms6747 (the coordinator's).
