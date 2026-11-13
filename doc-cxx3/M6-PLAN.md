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

### M6a, round 4 (the loops reach the pipeliner), 2026-11-13, lane m6c

- **Traced** (CPP11_PIPE on the six, the selector's text against the walker's): three shapes, none of them the pipeliner's.
  (1) *The guard branched to the head* - `[A1] B head` and its stub after it, so `recognise` counted the head named twice
  ("the head is entered from elsewhere": stack-probe, the array-loop sums). (2) *A loop's blocks were not together* - the
  IR's order put a `while`'s exit between its head and its latch ("no exit label after the branch", "an unconditional back
  branch": pipelined-data-exit, lfSame of both array-loop cases). (3) *A block label inside the loop* - `ssaN` is not one
  of `passThrough`'s kinds ("a label inside"). Behind those, a constant made at each use wrote a scratch register in the
  body, the bound among them ("the counter is not stepped by one before the test", local-zero-init-large).
- **Landed** (`83cfdeb2`): `backend/C6xSelect.cpp` 605 -> 652 lines, `Tms6747.h` +6, `SsaSelect.cpp` 0, `C6xSched.cpp` 0.
  A conditional branch whose other way is the next block owes its edge stub until the text has jumped away (`ssaLater_`:
  after a `Br`'s jump or a `Ret`, never between a latch and its own stub - what m6b's attempt did, two branches a turn),
  so a guard falls into its loop. The blocks are written in `SsaSelect.cpp`'s order at -O1 and -O2 (`ssaBlockOrder`, the
  one implementation, out of its unnamed namespace). A block inside a loop that is neither its head nor what its latch
  falls into is labelled `ssain`, the one kind added to `passThrough` - not `ssa` itself: a guardless loop's exit label
  is unnamed, and read through it is dropped and the loop refused. A constant or a symbol's address a loop reads where
  the instruction cannot hold it keeps a free scratch register (a saved one only across a call), at no weight: a value
  no call crosses takes it back before it takes a saved register, and it is then made at each use as before.
- **The walker's text is unchanged**: `CXX1_C6X_WALKER=1` at -O1 and -O2 over the 1,170 files of tests/cases, m6b's
  cxx3-msvc.exe against this one, 0 differ (boxkit/m6c-walk.out; the walker writes no `ssain` label).
- **Cycles, vm6747 (tests/tms6747.sh SIM=0 CYCLES=1, 780 cases):**

  | level | walker | selector, round 3 | selector, round 4 | cases better / equal / worse |
  | --- | --- | --- | --- | --- |
  | -O0 | 447.7 M | 393.2 M | 393.7 M | 376 / 115 / 289 (worse by 0.18 M in all; thread-pool 160 k of it) |
  | -O2 | 195.9 M | 201.4 M | 193.6 M | 415 / 319 / 46 (worse by 1.49 M in all) |

  | the six, -O2 | walker | round 3 | round 4 |
  | --- | --- | --- | --- |
  | stack-probe | 801,623 | 4,007,269 | 534,481 |
  | local-zero-init-large | 524,782 | 1,544,790 | 1,424,382 |
  | pipelined-data-exit | 1,517,496 | 2,087,235 | 1,471,658 |
  | fill-stores | 985,949 | 1,710,695 | 1,546,793 |
  | array-loop-member-copy | 4,650,964 | 5,144,262 | 3,970,990 |
  | array-loop-static | 2,082,301 | 2,452,458 | 2,009,121 |

  Probe c6x-select-scalars: -O0 2,767 (round 3 2,750; walker 3,523), -O2 711 (round 3 743; walker 746).
- **The -O2 cycle gate is met in sum (-1.2%)**, and four of the six are under the walker. The 125 worse cases are 46, and
  1.46 M of their 1.49 M is the other two: local-zero-init-large +0.90 M, fill-stores +0.56 M; the third is
  lambda-deduction-temporaries +27 k, then nothing over 2.1 k.
- **The next cause, named: a body the SSA unroller wrote four turns of.** `dirty()` and `sum()` reach the selector as
  four turns and a counter stepped by four, which the pipeliner does not take ("no schedule under four fifths", "the
  counter is not stepped by one before the test"), and the scan gives every short range the lowest free register, so
  the four turns share A20/A21 and the scheduler cannot overlap them: 7 cycles a turn in `sum()` against the walker's 5.
  **Tried, not committed:** a rotating pick writes `sum()` in 4.5 cycles a turn, but a block argument then leaves its
  parameter's register, the latch needs a move, and the pipeliner takes none of the six loops (CPP11_PIPE: stack-probe
  1 -> 0, array-loop-member-copy 4 -> 0). The pick must rotate *and* keep an argument in its parameter's register.
- **-O0 lost 0.5 M to round 3** (393.2 -> 393.7 M; stack-probe +0.53 M, local-zero-init-large +0.18 M, 227 cases worse
  by 0.85 M in all, 148 better): the kept constants and the new stub places apply at -O0 too, where nothing schedules
  them. Not traced; the gate against the walker holds by 54 M.
- **Gates (83cfdeb2's tree):** Linux build; tms6747.sh SIM=0 -O0 783 passed / 3 failed, -O2 783 / 3 - the walker's three
  noexcept cases; the Linux compiler's -O2 text of the six is the Windows build's (md5); exclusions 0 uncited 0 stale,
  comments 0 over, memory gate rc 0, ide/generate.py --check up to date; cl /W4 /WX clean on Windows. Census
  (tools/ssa-census -arch tms6747, the same on Linux and on Windows): outside 0, 25,018 of 34,840 functions selected
  (71.8%), 12 other failures - round 3's thirteen less `C6xSelect.cpp`.
- **Owed:** why -O0 lost 0.5 M; the unrolled bodies (above); the bench kernels' cycles;
  sim6747, asm6x.sh and identical.sh ARCHS=tms6747 (the coordinator's).

### M6a, round 5 (the register pick for unrolled loops), 2026-11-13, lane m6d

- **Landed** (`bbedc0dd`): `backend/C6xSelect.cpp` 652 -> 675 lines, nothing else in src/. In `ssaAssign`, an edge's
  argument and its parameter are partners: the second of the two to be placed takes the first's register where it is in
  the pick, so the edge needs no move and the pipeliner still sees one counter. The rest rotate within a block at -O1 and
  -O2 (a cursor past the last register given, reset at each block), so the four turns the SSA unroller writes no longer
  share A20/A21; a saved register is still taken lowest first (a rotation over them only adds saves). At -O0, where
  nothing schedules, the pick stays the lowest free register and a loop's constants are made at each use as in round 3.
- **The walker's text is unchanged** by construction: `ssaAssign` runs on the selector's path only.
- **Cycles, vm6747 (tests/tms6747.sh SIM=0 CYCLES=1, 780 cases):**

  | level | walker | round 3 | round 4 | round 5 | cases better / equal / worse than the walker |
  | --- | --- | --- | --- | --- | --- |
  | -O0 | 447.7 M | 393.2 M | 393.7 M | 393.2 M | 380 / 117 / 283 (worse by 0.17 M; thread-pool 155 k of it) |
  | -O2 | 195.9 M | 201.4 M | 193.6 M | 192.2 M | 416 / 321 / 43 (worse by 0.21 M) |

  | the six, -O2 | walker | round 4 | round 5 |
  | --- | --- | --- | --- |
  | stack-probe | 801,623 | 534,481 | 534,479 |
  | local-zero-init-large | 524,782 | 1,424,382 | 661,890 |
  | pipelined-data-exit | 1,517,496 | 1,471,658 | 1,458,291 |
  | fill-stores | 985,949 | 1,546,793 | 1,049,849 |
  | array-loop-member-copy | 4,650,964 | 3,970,990 | 3,967,524 |
  | array-loop-static | 2,082,301 | 2,009,121 | 2,008,094 |

  Against round 4 case by case: -O2 210 better / 541 equal / 29 worse (by 4 k in all, map-set-node-stability +2 k);
  -O0 against round 3 252 better / 528 equal / 0 worse. Probe c6x-select-scalars: -O0 2,750 (round 4 2,767), -O2 705
  (round 4 711). The pipeliner still takes stack-probe's loop (II=2) and sumInts (II=5); the unrolled `sum()`/`dirty()`
  stay refused ("the counter is not stepped by one before the test"), now scheduled as straight code.
- **-O0's 0.5 M came back** with round 4's kept constants held at -O1/-O2 only (at -O0 nothing schedules, so a held
  constant only costs its register to a value that needs one): -O0 is round 3's to the case, and round 4's stub places,
  which stay at -O0, cost nothing there.
- **The 43 left at -O2** are 0.21 M: local-zero-init-large +137 k and fill-stores +64 k (the unrolled bodies, whose
  counter the pipeliner refuses), lambda-deduction-temporaries +6 k, then nothing over 2.7 k.
- **Gates (bbedc0dd's tree):** Linux build; tms6747.sh SIM=0 -O0 783 passed / 3 failed, -O2 783 / 3 - the walker's three
  noexcept cases; cl /W4 /WX clean on Windows; exclusions 0 uncited 0 stale,
  comments 0 over, ide/generate.py --check up to date (Windows); the same static gates on Linux rc 0, memory gate included. Census (tools/ssa-census -arch
  tms6747, Windows): outside 0, 25,023 of 34,847 functions selected (71.8%), 12 other failures - round 4's.
- **Owed:** the pipeliner over a counter stepped by four (the unrolled bodies' last 0.2 M); the bench kernels' cycles;
  sim6747, asm6x.sh and identical.sh ARCHS=tms6747 (the coordinator's).

### M6a, round 6 (-g), 2026-11-14 (lane m6g)

- **The fault:** run.sh's tms6747 -g check (`-g -O0` must equal the build without -g, minus `.dw` and `$blk$`) failed once
  M6a sent a -g function to the walker and the same function without -g to the selector.
- **What landed:** the selector's "-g" fallback is gone (C6xSelect.cpp ssaOutside). selectBody writes, before each
  instruction, the `.dwpsn` rows and lexical blocks of `ir::Inst::lines` and a row at a new `Inst::pos`, as arm64's
  selector (markLine/markBlock, closeOpenBlocks before the closing `ZERO`). Variables: each local and parameter at the slot
  of the alloca the lowering made for it (`ssaPlace_`, read by Tms6747::emitScope); a parameter with no alloca keeps the
  slot emitParams fills; **a value in a register gets no record** (a homed parameter, a promoted local - the walker's
  regOf_ rule). The local-to-alloca match is one function now, `ssaLocalAllocas` (Regions.h/.cpp), which arm64's
  ssaLocals uses too (its copy in A64Select.cpp removed).
- **The check over the suite:** tests/g-same.sh - every case at -O0, -g against none: **860 same, 0 differ, 0 broken,
  355 not for tms6747** (Windows, cl build). -g with -O stays refused by name for tms6747 (the scheduler carries no
  source position through its packets), so -O2 has nothing to compare: all 860 refuse, as run.sh expects.
- reference-to-function-member.cpp: 6 of 11 functions selected under -g (the 5 others: indirect calls).
- src/: +74 -32 (A64Select.cpp, C6xSelect.cpp, Tms6747.cpp/.h, Regions.cpp/.h).
- **For the merge with m6e** (its rule: nothing in selectBody reads out_'s position or content): Regions' block test read
  `emittedSize()`; in a selected body Tms6747::emittedSize now answers a count of selected instructions (`ssaInsts_`), so
  nothing reads out_ there - lines and block labels are written as text where the walker writes them. Touched in
  C6xSelect.cpp: ssaOutside (the -g line), selectBody (the -g setup before the block loop, the per-instruction rows, the
  count, closeOpenBlocks before the closing ZERO); Tms6747.h emittedSize/ssaPlace_; Tms6747.cpp emitScope and one line
  before ssaChoose.
