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

| step | delivers | judged by | status (2026-11-15) |
| --- | --- | --- | --- |
| M6a | `backend/C6xSelect.cpp`: selection of `ir::Function` into the walker's text - 32-bit integers and pointers, arithmetic, compares, branches and block arguments, loads, stores, allocas, direct calls in registers, returns - the post-passes untouched; a per-function fallback named by reason (`CXX1_SSA_WHY`); `CXX1_C6X_WALKER` the walker all; a census row | tests/tms6747.sh and tests/asm6x.sh at -O0 and -O2 as before; sim6747 cycles of the selected cases against the walker's (equal or better, or named); `identical.sh ARCHS=tms6747` names each changed file | landed, rounds 1-6 (2026-11-13/14); its subset is what M6a.2 widens |
| M6a.2 | the rest of the subset: 64-bit and floating values, class values and stack arguments, indirect and variadic calls, `-g`, then pads and the TI unwinder's tables from `ir::Function::landingPads`; census 0 walked | as M6a | **open** - 4,207 functions walked at -O2 at `458f8974` (`tools/c6x-walked.baseline`); planned in "M6a.2's rounds" below; **before M6d** (the user, 2026-11-15, Q2) |
| M6b | selection emits MIR with schedule-aware allocation (values in registers, not words), still through the old post-passes | cycles equal or better case by case; tms6747.sh | **closed as built** (the user, 2026-11-15, Q8): homes picked in `ssaAssign`; the allocator's rounds are M6b.2, after M6d |
| M6c | the packetiser and pipeliner on MIR; `C6xSched`/`C6xPipe` text passes deleted once cycles are equal or better | the cycle table against M6a's; tms6747.sh, asm6x.sh | **closed**, wave 19 (`458f8974`); wave 18's seven `<map>`/`<set>` cases (+11,926 cycles) accepted as a library cost (the user, 2026-11-15, Q1) |
| M6d | with no walker left on `Ast.h` (M5.3 and M6a.2), its nodes and the parser's building half deleted (design 12.2) | `tools/ast-residue` to 0; every suite | waits on M6a.2 (M5.3 done); the walker and its legs stay until it (the user, 2026-11-15, Q2) |
| M6b.2 | the allocator M6b's design names (written 2026-11-13, below): the file pick counting 1X/2X per turn, `.M` pressure, the allocator's result a rewrite of `MOperand::reg` | cycles equal or better case by case; tms6747.sh | **owed, after M6d** (the user, 2026-11-15, Q8) |

**Lanes code, the coordinator gates** (`boxkit/brief-m3b5-par4.txt`). Every C6000 run is on the Linux box (TI's
tools in `~/ti6x`), never on the Mac. *Superseded, the user, 2026-11-15: everything that does not execute
x86_64-linux code runs on the Windows box (`boxkit/windows-split-2026-11-15.md`) - the C6000's suites, simulators and
TI tools among it.* *The Linux box's one lock is two (the user, 2026-11-15, lane r6x): every Linux job through `~/slot`,
the first free of `~/.boxleg.lock.1`/`.2` at -j2/JOBS=2 each, `tools/wave-gate`'s SLOT its source, two steps at once;
the Windows steps pooled within WINLANES=10 at /affinity 3FF, steps sharing a tree folder never together.*

## Where it stands

### Owed at M6c's close, 2026-11-15 (lane r6g, the M6c review's R116)

Every item an entry since `434bdf8a` (M4's owed rows to wave 19) left owed and no later entry closed, with the step that
takes it and when. A row leaves this table only by an entry below that names it; a new owed item is added here by the
entry that finds it. The user's decisions of 2026-11-15 that the rows cite are in "The user's decisions on the M6c
review" at the end of this note.

| owed | from | owner | when |
| --- | --- | --- | --- |
| M6a.2: 4,207 functions walked at -O2 (4,053 at -O0) in tests/cases, by reason in "M6a.2's rounds" | M6-PLAN rounds 16, 25; R101 | M6a.2, its rounds W1-W6 | after the review's mend wave, **before M6d** (Q2) |
| the six tms6747 `.notarget` lines of the walker's wrong answers (`handler-guards-nested`, `handler-local-past-inner-try`, `inline-static-exit-helpers`, `noexcept-set-terminate`, `ssa-own-landing-pads`, `static-reference-to-temporary`; the review's seventh, `noexcept-terminates-callee`, has an x86_64-windows line only) and the nine `tests/open/walker-*` entries | rounds 17-18, waves 13-14 | M6a.2 W1 (pads), each removed by the round that selects its function | M6a.2; the last by M6d |
| the walker's class temporaries of a static reference and a `thread_local`'s scalar (`tests/open/walker-static-reference-temporary`) | round 18, wave 14 | M6a.2 W1 | with W1; else deleted with the walker at M6d |
| operand-spelling readers inside the passes: `registersIn` over `MOperand::str()` (11 sites), `renamed` (4), six `kind == Symbol` halves; with them `readSerial` and `read` | rounds 24, 25; wave 19; R114 | M6d (the walker's text is their only reader); the symbol spelled like a register is the review's lane E | M6d |
| the inliner's unit budget: one `rotateLeft` in `rebalance` declined, seven `<map>`/`<set>` cases +11,926 cycles (accepted as a library cost, Q1) | lane sh2, wave 18 | an inliner round (`SsaInline`: a callee measured as promote leaves it), judged over every common case | after M6a.2, before M6 closes |
| `C()(a, b)` refused ("'C(...)' makes a temporary of type 'struct less<...>'", `Overload.cpp:610`), reproduced only in `OptTable.cpp` and a 36-line cut (`boxkit/sh2-r/u1.cpp`); the compiler's source worked round it (20f14d2e) | lane sh2, wave 18 | the review's lane D (`Overload.cpp`): the cut as a `tests/open` entry with clang's answer, then the mend | the mend wave; the open entry at once |
| `tests/open/namespace-unnamed-nested-leaks`: an unnamed namespace inside `n` answers a lookup at file scope | lane sh1, wave 17 | a lookup round (`src/sema/Lookup.*`, the review's lane D's files) | the mend wave if D reaches it; at the latest M8.5 (two-phase lookup) |
| the scheduler's refusal of a function it cannot spell, made a counted decline (Q5) | round 17 (D4); R108 | the review's lane E | the mend wave |
| the bench kernels' cycles under the selector (asked by M6a rounds 3-5, never recorded) | M6a rounds 3-5 | M6's cycle judge (`tools/c6x-cycles`, the review's lane B), then every M6a.2 round | the mend wave's gate |
| sim6747 in every wave gate, so the three `set_terminate` cases run on a C6000 leg (Q4); SIM6747 is not built on the Windows box yet | wave 4; R102; the Windows split | the wave gate (`tools/wave-gate`, lane A); SIM6747's Windows build (`boxkit/windows-split-2026-11-15.md`) | the mend wave's gate, and every wave gate after |
| names.sh, overload.sh, win-structure.sh in every wave gate, on the Windows box (Q9) | R103 | the wave gate (lane A); the suites' skip (lane D) | the mend wave's gate, and every wave gate after |
| target 4's self-host at a milestone close, on the Mac (Q4b); the fixed point not held since M5's close | R104 | M6's close; then every milestone's close | at M6's close |
| emit.sh's golden on Linux | M5's close, wave 4 | the wave gate | the mend wave's gate |
| a typedef's name in the debug information (`Count n` shows `unsigned int`); a file-static global shown `::ratio` where clang shows `ratio` | lldb, wave 16 | a `-g` round (`Dwarf.cpp`; the type system keeps no typedef) | after M6d |
| the allocator M6b's design names (1X/2X per turn, `.M` pressure, the `MOperand::reg` rewrite) | M6b rounds 1-3; R113 | M6b.2 (Q8) | **after M6d** |
| the Ast.h residue: five raises accepted, only down from here; any raise needs the user's word (Q7) | R111 | every round; `tools/ast-residue` | standing |
| cl 19.51 has not compiled `src/encoder/` (the box's latest is VS 2022's 14.44) | M4.6 | M4's row, re-opened when a newer cl is on the Windows box | when cl 19.51 is installed (the user's) |
| M8.0's REF, its golden and `-check=templates` | m8a, 2026-11-15 | M8.0 | when M8 starts in its plan's order |
| the early substitution's gaps: run.sh under `CXX1_TEMPLATE_SUBST`, `-check=typed` and `-g` of a substituted body (Q6: kept as a stand-in, off by default) | m8a, wave 18; R109 | the review's lane F (its gate); deleted by M8.2 | lane F in the mend wave; the stand-in at M8.2 |
| 479 refusal reasons of valid C++11, 20 reached by a test (lowering-why, 1237 files) | R1 rounds 6-9 (M5); R112 | R1's rounds; the review's lane H records them | each R1 round |

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

### The tms6747 leg of R1's cases, 2026-11-14 (lane c6n, from c1100a58)

- **The fault:** four cases R1 added or moved never ran on tms6747 and failed in wave 2's gate (`SIM=0 tests/tms6747.sh`
  at -O0): bool-increment, handler-guards-nested, static-reference-to-temporary, inline-static-exit-helpers. Each was run
  at -O0 and -O2, with the selector and with CXX1_C6X_WALKER=1: **the four answers are the same both ways**, so in each the
  wrong function is one the selector leaves to the walker; the SSA path (x86) answers clang's in all four.
- **Mended - bool-increment:** the walker's postfix `++` of a bool added 1 and stored 2 (`b++` twice printed 2);
  Tms6747::visit(Postfix) stores 1 for a bool ([expr.post.incr]/1). The prefix form was already right (compound's
  conversion to bool). src/: +2 (Tms6747.cpp).
- **Registered (the walker's; M6's selector takes them):** handler-guards-nested - an exception out of a handler's inner
  block destroys the block's object, not the handler's (gone 3 for 4) - tests/open/walker-handler-guards-nested.cpp;
  static-reference-to-temporary - the Ast binds a static reference to an ordinary temporary, destroyed at once, the
  reference left dangling (the static object staticTemporary declares is the SSA lowering's alone) -
  tests/open/walker-static-reference-temporary.cpp; inline-static-exit-helpers is the same root over two units (the
  register runs one unit, so it is named in its .notarget, not copied). Each case has a `tms6747` .notarget line naming it.
- **Seen, not this lane's:** member-pointer-condition ran 22+ minutes on vm6747 in wave 2's -O2 shard 1 (a hang past the
  3-minute run cap); noexcept-terminates-callee is the walker's known set_terminate.

## M6b's design: the C6000's MIR (written 2026-11-13 by lane m6e)

**Its own MInst, not x86's parameterised.** `optimizer/Mir.h`'s MInst is two operands in AT&T order, a register a
number and a width, a memory operand `disp(base,index,scale)`, and M4's encoder reads exactly that. A C6000
instruction has up to four operands in TI's order (`EXT src, 24, 24, dst`), no width (a register is 32 bits, a 64-bit
value a pair `A5:A4`), a predicate, and addresses that step their base. Bending one struct to both would give each
target fields the other must ignore; what they share is kept shared instead - `opt::StrId` (text as an index) and the
rule that an opcode and an operand are values, never strings. `optimizer/C6xMir.h`, namespace `c6x`:

| part | what it holds | read by M6c for |
| --- | --- | --- |
| register | 0-31 A0-A31, 32-63 B0-B31 - the bit `c6x::bitOf` already gives a name; `sideOfReg` is the file | units and cross paths: an instruction runs on its destination's side (`MInst::side`), a source of the other file takes that side's one cross path |
| `MOperand` | Register, Pair (`hi:lo`), Immediate, Memory (base, mode Base/Plus/Minus/PreInc/PreDec/PostInc/PostDec, a constant or register offset, `[]` scaled or `()` bytes), Symbol | the T path of a load or store (its data register's side), a stepped base written in E1, a frame access by its offset |
| `MInst` | kind Instruction/Label/Verbatim; the mnemonic; up to four operands, sources first; `pred`/`predNot` (`[A1]`, `[!A1]`) | the predicate as a read; `slots()` = `c6x::delaySlots`, the one table: a result lands that many cycles after issue |
| `MStream` | one function's body, serial: every instruction's delay slots padded by `NOP n` entries, as the walker's text is | C6xSched's reading: "with its NOPs removed it reads as a sequential program" |

**Delay slots are the table's, not the stream's.** The serial stream still carries `NOP n` entries because the text the
old post-passes read carries them; M6c's packetiser drops them on entry and pads to `slots()`, as `c6xSchedule` does
with lines today. A branch's five slots and a load's four are never operands.

**What the printer must write** (`c6x::write`, `MInst::str`) so C6xSched and C6xPipe read today's text: a label as
`name:`; an instruction as a tab, `[R]` or `[!R]` and a tab where predicated, the mnemonic, a tab, the operands joined
by `, ` (C6xSched's `parse` splits on exactly that); registers as `A<n>`/`B<n>`, a pair `hi:lo`, memory as `*`, the
sign or step, the base, a step after it, then `(n)` or `[n]` or a register in either; a Verbatim line as it stood. The
gate is identity of the whole text, and it is held by construction where it could not be by care: `c6x::read` makes an
instruction of a line only where `MInst::str()` spells the same bytes back, and an operand the model cannot spell back
is a Symbol holding its text (`CXX1_C6X_MIR=1` counts both per function, so what is unmodelled is a number).

**Who builds it.** `C6xSelect.cpp` builds an `MInst` for every instruction it writes itself (`ssaEmit`). The walker's
helpers it still calls - `movImm`, `movSym`, `regAdd`, `localAddr`, `narrowInt`, `loadFrom`, `jump`, `call`,
`openArea`/`closeArea`, `defineLabel`, `frameOperand`'s `MVK` - write text; `ssaAbsorb` reads what they wrote onto the
stream where it stood, before the next instruction. So the body is one `MStream` from its first label to its last
`ZERO`, and `selectBody` ends by printing it. The helpers move to `MInst`s as the walker's use of them goes (M6d).

**Where schedule-aware allocation enters (round 2).** Between selection and the printer, on the stream: today
`ssaAssign` picks a home per value before any instruction exists, by live range alone. With the stream in hand the pick
can read what the packetiser will: (1) *side* - a value takes the file of the instructions that read it, so an add of
two A values is not fed through a cross path, and two independent chains of an unrolled body take A and B and run in
one packet; (2) *unit pressure* - `.M` and `.D` are one a side, so two multiplies or two loads a turn want different
sides; (3) *the T path* - a load's destination side picks its data path, two loads a cycle need both; (4) the B side is
then a home an instruction reads directly, not a copy through A4/A6 as rounds 3-5 wrote it. The registers are numbered
so a pseudo is any number from 64 up; the allocator's result is a rewrite of `MOperand::reg`. Its gate is cycles case
by case against round 5 (`m6d-cmp.sh`), the text no longer identical by design.

### M6b, round 1 (the selection emits MIR), 2026-11-13, lane m6e

- **Landed** (`91f0d751`): `optimizer/C6xMir.{h,cpp}` (new, 76 + 175 lines) - the design section above; `backend/C6xSelect.cpp`
  675 -> 704 lines, `Tms6747.h` +7. Every instruction the selector writes is an `MInst` (`ssaEmit`; no `out_ <<` of an
  instruction is left in the file); what a walker's helper writes is read onto the stream where it stood (`ssaAbsorb`);
  `selectBody` prints the stream (`c6x::write`) where its text used to accumulate. src/ +318 -31 lines. `ssaAssign` is
  untouched: schedule-aware allocation is round 2.
- **The selected text is m6-d's, byte for byte.** Windows, `cxx3-msvc.exe` of work\m6e against work\m6d's (bbedc0dd),
  `-arch tms6747 -S` over the 1,170 files of tests/cases at -O0 and -O2, the text and the diagnostics by md5
  (boxkit/m6e-winsame.sh): 2,331 same, 9 differ - eight are four cases whose diagnostic names the tree's own directory
  (posix-headers, std-function-not-function-type-refused, ucn-identifier, windows-h-layout), one was a compile cut
  short under load; re-run with the directory's name levelled, 10 outputs, 0 differ. Linux, five cases at both levels
  (m6e-gate.sh quick): 0 differ.
- **How much of the stream is modelled** (`CXX1_C6X_MIR=1`, tests/cases at -O2, Windows): 6,742 selected functions,
  140,404 instructions, 21,794 labels, **0 verbatim lines**, 14,886 operands held as text that are no branch's target -
  symbols in `MVKL`/`MVKH` and what else `MOperand` does not spell, not broken down.
- **Gates:** cl /W4 /WX clean (Windows); exclusions 0 uncited 0 stale, comments 0 over, ide/generate.py --check up to
  date (both boxes; the project lists regenerated). Census (tools/ssa-census -arch tms6747, Windows): outside 0, 25,033
  of 34,860 functions selected (71.8%), 13 other failures - round 5's twelve and `C6xMir.cpp`, which includes
  `C6xModel.h` (`thread_local`, refused on tms6747) as `C6xSched.cpp` and `C6xPipe.cpp` do.
- **Linux gates (91f0d751's tree, ~/m6e):** build; `identical.sh ARCHS=tms6747 LEVELS="0 2"` against m6-d's cxx3.exe:
  1,660 compared, 0 differ; tms6747.sh SIM=0 -O2 783 passed / 3 failed - the walker's three noexcept cases - 192.2 M
  cycles, round 5's; exclusions 0 uncited 0 stale, comments 0 over, memory gate --check rc 0, ide/generate.py --check
  up to date.
- **Owed:** round 2, schedule-aware allocation on the stream (the design section's last paragraph); the opaque
  operands broken down by kind; the helpers' text built as `MInst`s rather than read back; sim6747, asm6x.sh, -O0
  tms6747.sh and make test (the coordinator's). For the merge with lane m6g's `-g`: boxkit/m6e-handover.md names every
  function touched.

### M6b, round 2 (the merge of -g into the MIR selector; allocation not begun), 2026-11-14, lane m6h

- **The merge** (`6cdb4b65`, m6-e into m5-wave2 + m6g): one source conflict, the end of `selectBody`. Resolved as
  `closeOpenBlocks(); ssaCounting_ = false;` and then m6e's `ssaEmit(ZERO)`, head, `c6x::write` - the block labels
  `closeOpenBlocks` writes are text in `out_`, absorbed onto the stream by the `ZERO`'s `ssaEmit`, so they stand before
  it as they did. m6e's rule holds: under -g `emittedSize` answers `ssaInsts_`, and nothing in `selectBody` reads `out_`
  between its first and last lines. Project lists regenerated (`ide/generate.py`, on the box).
- **The oracle for "the merge changed no text" is the first parent's compiler, not m6-e's.** m6-e stands 54 commits
  behind m5-wave2 (`include/algorithm`'s `upper_bound`, R1's rounds): against it 757 outputs differ, none of them the
  merge's. Against m6g's build (2b4aca6e, work\m6g), Windows, `-arch tms6747 -S` over the 1,215 files of tests/cases at
  -O0 and -O2, each text written to a file and compared with its diagnostics (boxkit/m6h-win.sh `same`): **2,430
  outputs, 1,720 texts the same, 710 refused alike, 0 differ.** With m6e's own gate (m6-e against m6-d, Linux
  identical.sh 1,660 / 0) the MIR printer writes the walker stream's text on both bases.
- **A check that checked less than it said:** on Windows `-o -` writes a file named `-`, not standard output, so
  boxkit/m6e-winsame.sh's "byte for byte" (round 1's second bullet) compared diagnostics alone; round 1's Linux
  identical.sh was the real one. m6h-win.sh writes each text to its own file.
- **-g:** tests/g-same.sh on the merged tree (Windows, cl build): 860 same, 0 differ, 0 broken, 355 not for tms6747.
- **Gates (Windows, 6cdb4b65's tree):** cl /W4 /WX clean; exclusions 0 uncited 0 stale; comments 0 over;
  ide/generate.py --check up to date; memory gate --check rc 0. Linux (~/m6h, same tree): g++ build; tests/g-same.sh 860
  same, 0 differ, 0 broken; `identical.sh ARCHS=tms6747 LEVELS="0 2"` against ~/m6g's cxx3.exe (2b4aca6e): 1,720
  compared, 0 differ. Static and tms6747.sh SIM=0 -O2 and -O0 (three shards each) were queued on the lock at the hour:
  ~/m6h-gate.out, and boxkit/m6h-handover.md's last lines.
- **Schedule-aware allocation: not begun, by the rule that a claim with no oracle is not believed.** Its gate is
  vm6747 cycles case by case, vm6747 runs on the Linux box alone, and the lock there was held by other lanes' gates for
  the hour (this lane's build got it 22 minutes in); code that moves values between the A and B files cannot land
  unrun. What was measured instead, to size the design's point (4) (a B home read through A4/A6): the -O2 text of
  tests/cases, Windows, boxkit/m6h-win.sh `bside` - 371,257 instruction lines, 2,488 `MV Bn, A4|A6` and 8,724
  `MV A4, Bn` (the walker's functions included; with `CXX1_C6X_WALKER=1` 389,273 / 3,410 / 10,974). So the copies are
  at most 3.0% of the lines, a static count, not cycles: point (4) alone is small, and round 2 should begin at points
  (1)-(3) - side by the readers' file, `.M`/`.D` pressure, the T path - on the unrolled bodies, where a packet is won.
- **Owed:** round 2 itself (start: in `ssaAssign`, a value read only by B-homed readers prefers a B home, and
  `ssaUse`/the destination rule take a B register where the instruction's other operand is B or one cross path
  serves; measure with `SHARD=k/3 CYCLES=1` against 192.2 M and boxkit/m6d-cmp.sh); what of the Linux gate is not in
  the line above; sim6747, asm6x.sh, make test (the coordinator's).

## A member pointer's null on the C6000, 2026-11-14 (lane c6m, from m5-wave2 da250ac7)

- **The fault was the test, not the store:** member-pointer-condition hung on vm6747 because the walker tested a data
  member pointer against 0 - `c = 0` already stored -1 (the parser's conversion of a null constant), and SSA, which
  selects x86, tests against nullMemberPointer. The Ast carried the member pointer bare into `if`, `while`, `?:`, `!`,
  `&&`/`||` (contextualScalar) and into `(bool)` (a Cast the C6000 narrowed to a byte: -1 read 255).
- **Mended in the shared front end:** convertNode's conversion to bool takes a data member pointer, comparing it with
  its own null (`!= -1`, the null constant converted), and contextualScalar converts one to bool - one comparison,
  every truth context; no backend changed. src/: +5 -2 (ParserOverload.cpp, ParserExprNew.cpp).
- **A neighbour that failed on every target:** `f(0)` for an `int S::*` parameter was refused ("no function called
  'f' takes these 1 argument(s)") - Overload ranked the literal 0 a pointer conversion only to a pointer; [conv.mem]/1
  makes it one to a pointer to member too. src/: +2 -1 (sema/Overload.cpp).
- **Found on the way, mended:** member-pointer-static-cast printed `d.*s` as d.z on tms6747 (base tree too):
  `static_cast<int C::*>(&D::y)` reached convert, which only retyped it, so the walker kept D's offset; staticCast
  now shrinks it by the base's through convertMemberPointer, null kept (SSA already did, Lowering's /12).
  src/: +9 (ParserExpr.cpp). docs/EXCLUSIONS.md: one citation re-pointed (Overload.cpp:584).
- tests/tms6747.sh: a run by case name stopped at `SHARD: unbound variable` (set -u) and ran nothing; SHARD defaults.
- member-pointer-condition's `.notarget` (its only line was tms6747's) is gone; member-pointer-null-neighbours holds
  nullptr and 0 assigned, returned, passed and compared, and a `for`/`do` ended by a null, with clang's output.

### M6b, round 3 (homes on both files, read where they stand), 2026-11-14, lane m6i

- **Landed** (`b3d26f1e`, from m5-wave3 7995cf0d): the design's points (1)-(3) in their first form, in `ssaAssign` and
  on the instruction as it is built. src/ +88 -20 lines (`C6xSelect.cpp` 724 -> 779, `C6xMir.{h,cpp}` +10, `Tms6747.h` +3).
  - **B24-B31 are homes** at -O1 and up (`kHome` is 32 registers; -O0 keeps the 24 and its text).
  - **A value's file** (`wants`): a pointer or reference parameter keeps its arriving register's file (B4, B6, B8 - point 2, a base
    on B is a load on `.D2`); a load and an address step take their base's (point 3, `.D2` with the T2 path); anything
    else the file of most of its homed operands, A on a tie (point 1). The pick is narrowed to that file where it has
    a free register, and is otherwise as round 5 wrote it.
  - **An instruction reads and writes a home where it stands** (`ssaRead`, `ssaPut`): built over the homes of either
    file and kept where `c6x::legal` passes it - C6xSched's own `crossings` and `unitsFor`, no second table - else its
    B sources go through A4 and A6 and its result through A4, as before. Loads and stores take either file directly.
  - `CXX1_C6X_NOSIDES=1` writes round 2's text whole, so the baseline is this compiler's.
- **vm6747 cycles, -O2, the whole suite** (Linux, ~/m6i, three shards each way, `boxkit/m6i-cmp.sh`; baseline
  `CXX1_C6X_NOSIDES=1`, 192.174 M - round 1's 192.2 M):

  | | cases | round 3 | round 2 | better | equal | worse |
  | --- | --- | --- | --- | --- | --- | --- |
  | all | 811 | **192.009 M** | 192.174 M | 53 (by 0.203 M) | 675 | 83 (by 0.038 M) |

  | the six loops | round 3 | round 2 |
  | --- | --- | --- |
  | stack-probe | 534,479 | 534,479 |
  | local-zero-init-large | 661,890 | 661,890 |
  | pipelined-data-exit | 1,414,163 | 1,458,291 |
  | fill-stores | 1,049,849 | 1,049,849 |
  | array-loop-member-copy | 3,969,492 | 3,967,524 |
  | array-loop-static | 1,934,403 | 2,008,094 |

  Best: map-set-node-stability -75,586, array-loop-static -73,691, pipelined-data-exit -44,128. Worst: array-loop-throw
  +15,808 (0.6%), array-loop-local +6,676, vector-erase-destroys +2,869, array-loop-member-copy +1,968.
- **What the first form taught** (shard 1, every parameter keeping its arriving file): 129.059 M against 129.032 M,
  better 21, worse 39. ssa-fold-access showed why: a count arriving in B4 stayed on B, each of its A readers took 1X,
  and two of them no longer shared a cycle (`SHL B25, 2, A19` alone, where `ADD || SHL` stood). So only a pointer
  keeps the B file - a base is read by `.D2`, not over a cross path - and shard 1 is 129.054 M, better 17, worse 29:
  the rest of that loss is array-loop-throw and array-loop-local, where a second base on B gives six-wide packets and
  a longer prologue. The cross path is the resource the pick does not count yet.
- **Gates.** Linux (~/m6i, the landed code) `tms6747.sh SIM=0`, three shards, -O2 and -O0 alike: 814 passed, 4 failed - noexcept-terminates,
  ssa-noexcept-throw, noexcept-terminates-callee and static-aggregate-runtime-list (`thread_local`, refused) - the same
  four under `CXX1_C6X_NOSIDES=1`; `identical.sh ARCHS=tms6747 LEVELS="0 2"` under `CXX1_C6X_WALKER=1` against ~/w3g's
  cxx3.exe: 1,736 compared, 0 differ; tests/g-same.sh 867 same, 0 differ, 0 broken; exclusions 0 uncited 0 stale, comments
  0 over, memory gate rc 0, ide/generate.py --check up to date. Windows (work\m6i, b3d26f1e's tree, against work\w3g): cl /W4 /WX clean; text and
  diagnostics of tests/cases at -O0 and -O2, 2,446 outputs, under `CXX1_C6X_NOSIDES=1` 0 differ and under
  `CXX1_C6X_WALKER=1` 0 differ; tests/g-same.sh 867 same, 0 differ, 0 broken; exclusions 0 uncited 0 stale, comments
  0 over, memory gate rc 0, ide/generate.py --check up to date.
- **Owed:** the pick counting 1X/2X per turn (a B value with two A readers in one turn
  goes back to A) and `.M` pressure, neither begun; the unrolled turns alternating files, which needs the base on
  both - the stepped pointer of point (2); sim6747, asm6x.sh, make test (the coordinator's).

## M6c's design: the schedule on the stream (written 2026-11-14 by lane m6j, from m6-i b7510429)

**What exists.** `c6xSchedule` is one text pipeline for the walker's and the selector's functions alike: the serial
text (prologue, the printed `MStream`, epilogue) is parsed into `c6x::Line`s, its `NOP`s dropped; the folds, loop
passes and the pipeliner (`pipelineLoops`, whose kernels come back as `verbatim` lines, `||` and `NOP n` already
written) edit the lines; `schedule` list-schedules each block into packets and `foldBranchNops` makes `BNOP`. M6c
moves the last two, then the pipeliner, onto `c6x::MInst`; the folds between follow the pipeliner.

**An execute packet and a delay slot on the stream.** Three fields on `MInst`, no new container - a scheduled stream
is still one `MStream`, read in order:

| field | means | printed |
| --- | --- | --- |
| `par` | the entry issues in the cycle of the entry before it; a packet is one entry and the `par` entries after it | `\|\|` before the line |
| a `NOP n` entry | n idle cycles (nine at most an entry) - the only place a delay slot is visible | `NOP n` |
| `unit` | a branch's named unit, 1 `.S1` or 2 `.S2`; set only where the packet forces the choice | after the mnemonic |
| `packed` | placed already (a pipelined kernel): the packetiser keeps it where and as it stands | nothing |

The cycle of an entry is counted, not stored: packets and `NOP` counts from its block's label. **Delay slots stay
the table's** (M6b's rule): a load's four and a branch's five are `slots()`; the stream shows them only as the idle
cycles the scheduler could not fill, and `BNOP x, k` is a branch that carries k of its own five as its last operand.
A serial (unscheduled) stream has no `par` and no `NOP`: the selector's stream as built, were its helpers' padding
not absorbed with them.

**How a pass written for text reads MInsts.** A text pass reads a `Line` three ways, and each has its reading on the
stream: (1) *what the line is* - `instr`, `isLabel`, `verbatim` - is `MInst::kind` and `packed`; (2) *its parts* -
`mnem`, `ops[k]`, `pred` - are `m`, `op[k]`, `pred`/`predNot`, and an edit is an assignment to an operand, not a
`rebuilt` line; (3) *the ISA's facts* - `readsAndWrites`, `unitsFor`, `crossings`, `delaySlots`, `makeNode` - are
C6xSched's one table, which reads spellings. While both forms exist the table is asked through the entry's spelling
(`lineOf`, as `c6x::legal` asks it since M6b round 3): one implementation of each fact, and a ported pass cannot
disagree with the text pass about a unit or a cross path. The table's readers move to operands (a register a number,
`bitOf` a shift) when the last text pass that calls them is gone - the step that deletes `Line`. The scheduler itself
is shared by construction rather than copied: `scheduleBlock` writes `Row`s (a member by its place in the block, or
idle cycles) and each form prints its rows.

**The order, and how each step is held equal.**

1. *The packetiser* (round 1): `streamOf` makes the stream from the lines the text passes leave, `packetise` and
   `foldBranchNops` (C6xPack.cpp) place it, `c6x::write` prints it. Held: the selected functions' text byte-identical
   to the text pass's - `identical.sh ARCHS=tms6747` against the round before, and `CXX1_C6X_TEXTPACK=1` runs the text
   pass in the same binary. A line the stream does not spell back the same sends its function to the text pass
   (`CXX1_C6X_MIR=1` names each), so identity is by construction and the fallback is a number to bring to 0.
2. *The pipeliner* (round 2): `pipelineLoops` over entries - its kernels written as `par`/`NOP`/`packed` entries, not
   verbatim text. Held the same way, with a `CXX1_C6X_TEXTPIPE` knob; then cycles (m6d-cmp.sh), which identity implies.
3. *The folds between* (`rounds`, `numberValues`, `foldAddressing`, the loop passes): each ported behind the same
   two checks, the stream then entering `c6xSchedule` as the selector built it, never printed and re-read.
4. *The deletion*: with the walker's functions gone (M6d's census 0) the `Line` forms and the text entry go in one
   commit; until then the walker's functions keep the text pass, and nothing of theirs may change (`CXX1_C6X_WALKER=1`
   identity).

**What must not change.** A `packed` entry is never rescheduled nor counted as naming a label (the text pass does not
read a verbatim line's operands); a label of the backend's own flow that nothing names is no block boundary
(`passThroughName`); `BNOP`'s reach is judged on the packetised length; and a branch's target is a register by the
model's reading (`sideOf`), not by the operand's kind - a symbol spelled `B2x` is read as the text pass reads it.

### M6c, round 1 (the packetiser on the MIR), 2026-11-14, lane m6j

- **Landed** (code `ff8f480c` and the comment mend after it, from m6-i b7510429): the design section above;
  `optimizer/C6xPack.cpp` (new, 137 lines) - `streamOf`, `packetise`, `foldBranchNops` on `c6x::MStream`; `MInst` gains
  `par`, `packed`, `unit` and prints them; `scheduleBlock` writes `Row`s, which the text pass and the MIR pass each
  print, so there is one list scheduler; `c6xSchedule(text, level, selected)` - a selected function's lines become a
  stream after the text passes, are packetised and folded there, and printed. src/ +205 -22 lines (C6xSched.cpp
  2,297 -> 2,322: the shared rows and the entry). The pipeliner and the folds stay on text; the walker's functions
  keep the text packetiser. `CXX1_C6X_TEXTPACK=1` keeps the text packetiser for all.
- **What round 1 is not.** The ISA's facts (`makeNode`: reads, writes, units, cross paths) are still asked by the
  entry's spelling (`lineOf`), as the design says; the stream is made from the text passes' lines, not handed down from
  the selector. What moved is the representation, the block walk, the emission and the branch fold.
- **Identity.** Windows (work\m6j against work\m6i's cxx3-msvc.exe, b3d26f1e): text and diagnostics of tests/cases at
  -O0, -O1 and -O2 by md5, 3,669 outputs (2,601 texts, 1,068 refused alike), 0 differ. `CXX1_C6X_MIR=1` at -O2: 6,835
  selected functions, **0 sent to the text pass**. Linux (~/m6j against ~/m6i's cxx3.exe): `identical.sh ARCHS=tms6747
  LEVELS="0 1 2"`: 2,604 compared, 0 differ.
- **Gates.** Linux `tms6747.sh SIM=0 CYCLES=1` -O2, three shards: 814 passed, 4 failed - round 3's four
  (noexcept-terminates, ssa-noexcept-throw, noexcept-terminates-callee, static-aggregate-runtime-list); 192.009 M
  cycles, round 3's; tests/g-same.sh 867 same, 0 differ, 0 broken; exclusions 0 uncited 0 stale, memory gate rc 0,
  comments 0 over and ide/generate.py --check up to date (both failed first on `._` files the lane's own second ship
  left in ~/m6j, removed and re-run). Windows: cl /W4 /WX clean; tests/g-same.sh 867 same, 0 differ, 0 broken; exclusions 0 uncited
  0 stale; comments 0 over (one over at C6xSched.h:9, mended, re-counted); memory gate rc 0; ide/generate.py
  regenerated (C6xPack.cpp) and --check up to date.
- **Owed:** round 2, the pipeliner on entries (design step 2); the table's readers on operands rather than spellings;
  -O0 tms6747.sh, sim6747, asm6x.sh and make test (the coordinator's).

### M6c, round 2 (the pipeliner on the stream), 2026-11-14, lane m6k

- **Landed** (from m6-j ad3ac547): `pipelineLoops(MStream &)` (C6xPipe.cpp) - a selected function's lines become a
  stream before the pipeliner, its loops are pipelined there and each kernel comes back as entries (`par` its packets,
  `NOP n` its idle cycles, `packed` what is placed, never a verbatim line), and `linesOf` (C6xPack.cpp, the inverse of
  `streamOf`) reads the stream back for the text folds that follow. `pipelineAt` is the one trial of a back branch
  (counted loop, then the byte window), read by the text driver and the stream driver alike; `namesOf` the one
  collection of labels and named operands. `CXX1_C6X_TEXTPIPE=1` keeps the text pipeliner for all; the walker's
  functions keep it always. `CXX1_C6X_MIR=1` names a function sent back to the text pipeliner (`c6x-pipe`) and counts
  the placed entries of each function with a kernel (`c6x-kernel`). `streamOf` also refuses a line whose `instr` is
  not its text's. src/ +94 -34 lines (C6xPipe.cpp 2,007 -> 2,040, C6xPack.cpp 137 -> 150).
- **What round 2 is not.** The II search, the register rotation and the loop shapes are the same code, unchanged: they
  read the stream through each entry's spelling (`linesOf`), as round 1's packetiser asks the table through `lineOf`.
  `Loop`, `Window`, the terms and the webs still hold `Line`s and register names as strings; the kernel is built as
  lines and made entries where it is spliced. What moved is where the pipeliner stands (on the stream, between two
  conversions), its splice and its kernels' representation. The readers on operands are the step that deletes `Line`.
  The stream is still rebuilt from text (twice now: before the pipeliner and before the packetiser), not handed down
  from the selector.
- **Identity.** Windows (work\m6k against work\m6j's cxx3-msvc.exe, ad3ac547's code): text and diagnostics of
  tests/cases at -O0, -O1 and -O2 by md5, 3,669 outputs (2,601 texts, 1,068 refused alike), 0 differ. `CXX1_C6X_MIR=1`
  at -O2: 6,835 selected functions, 0 sent to the text pass, **0 sent to the text pipeliner**; 106 functions hold
  kernels made on the stream, 2,727 placed entries. Linux `identical.sh ARCHS=tms6747`
  against ~/m6j's cxx3.exe: **not measured inside the lane's hour** - see Gates.
- **Gates.** Windows: cl /W4 /WX clean (the final code tree); tests/g-same.sh 867 same, 0 differ, 0 broken; exclusions
  0 uncited 0 stale; comments 0 over (one over at C6xMir.h:80, mended, rebuilt, re-counted); memory gate rc 0;
  ide/generate.py --check up to date. Linux (~/m6k, a copy of ~/m6j with the changed files laid over): g++ build
  clean; **no other Linux leg ran** - M5's closing self-host held the lock for the whole hour. The chain is queued
  under the lock (`~/m6k-gate.sh`: build, ident 0/1/2, tms2-1..3 with cycles, static, gsame; output ~/m6k-gate.out)
  and its numbers are owed to this entry. A fault of the lane's own: a file laid while make was compiling it left an
  object newer than its source, so the first two builds were one edit (the census line) behind; touched and rebuilt.
- **Owed:** the pipeliner's readers on operands (`Loop`, `Window`, the webs and terms off `Line`); the stream handed
  down from the selector; the folds between (design step 3); -O0 tms6747.sh, sim6747, asm6x.sh and make test (the
  coordinator's).

### M6c, round 3 (the packetiser's table read on operands), 2026-11-14, lane m6l

- **Landed** (from m6-k 16d69277): the ISA facts the packetiser asks - `makeNode`'s reads, writes, stepped address,
  latency, units, side, cross path, data path and frame slot, and `crossings`/`unitsFor` - read from an entry's typed
  operands (`regsOf`: a register a number, a pair high first, a base before its index; an immediate its value) in
  C6xPack.cpp. `packetise`, the BNOP fold's unit trial and `c6x::legal` (the selector's) ask them; `modelled` sends an
  entry with an operand held as text that names a register, an address or a number to the spelling (`lineOf`), as
  before. A branch's target is still read by `sideOf` on its spelling (the design's rule). src/ +186 -2 lines
  (C6xPack.cpp 150 -> 322, C6xMir.h, C6xModel.h, C6xMir.cpp).
- **Two forms, held equal.** Until `Line` goes, the table has a spelled form and an operand form; `CXX1_C6X_MIR=1`
  asks both for every modelled node and names each that differs (`c6x-node`). It found one on its first run: 2,502
  nodes, all `LDW *A15, A15` - `frameSlot` reads `*A15` as slot 0, which the operand form had missed (no text changed:
  only aliasing was more cautious). Mended; **0 nodes differ** over tests/cases at -O2. The spelled form goes with
  `Line` (design step 4).
- **Identity.** Windows (work\m6l with the final exe, against work\m6k's cxx3-msvc.exe): tests/cases at -O0, -O1,
  -O2 by md5, 3,669 outputs, 0 differ - plain, `CXX1_C6X_TEXTPACK=1` and `CXX1_C6X_TEXTPIPE=1` each. (A tree named
  `m6l2` showed 6 differ: two refused cases' caret column moves with the tree path's length - not the code.) Census:
  6,835 selected, 0 to the text pass, 0 to the text pipeliner, 106 functions with 2,727 placed entries.
- **Gates.** Windows: cl /W4 /WX clean; tests/g-same.sh 867 same, 0 differ, 0 broken; exclusions 0 uncited 0 stale;
  comments 0 over; memory gate rc 0; ide/generate.py --check up to date. Linux: none (the lock is M5's); the cycle
  run and identical.sh are the coordinator's.
- **Not done, owed:** the pipeliner's recognisers on operands (`Loop`, `Window`, the webs and terms still hold `Line`s;
  its `makeNode` calls read kernel lines); the selector's stream handed to the packetiser - not possible before the
  folds between (`rounds`, `numberValues`, `foldAddressing`, design step 3) run on the stream, since they edit lines
  after selection; -O0 tms6747.sh, sim6747, asm6x.sh and make test (the coordinator's).

## Wave 4's merge, 2026-11-14 (lane w4m, m6-wave4 from main 4a1cbac7)

- **Merged, in order**: `m6-i` (M6b round 3, B-file homes), `m6-j` (M6c round 1, the packetiser on MIR), `m6-k`
  (M6c round 2, the pipeliner on the stream behind `CXX1_C6X_TEXTPIPE`), `m6-l` (M6c round 3, the packetiser reads
  typed operands), `m5-dbg` (tests/debug.sh, gdb 5/5, the DWARF mends). The m6 branches fork from 9b689e66 and carry
  waves 1-3 as their original commits, which main holds as squashes; m6-i was therefore merged as main's tree plus
  `git diff 7995cf0d m6-i` (its own five files: C6xSelect.cpp, Tms6747.h, C6xMir.{h,cpp}, M6-PLAN.md), and main's
  later ast-residue baseline and .notarget kept. m6-j, m6-k and m6-l then merged clean; m5-dbg's one conflict was
  M5-PLAN.md, both sides kept. ide/* came in with the branches and `ide/generate.py --check` says up to date.
- **Gate, Linux** (three g++ builds from the merge, m6-l 22047329 and main 4a1cbac7; `boxkit/w4m-cmp.sh`, 1,228
  inputs each, tree paths normalised): tms6747 -O0, -O2 and -O0 -g against m6-l's compiler 1,228 same, 0 differ
  each; x86_64-linux, x86_64-windows (masm and gnu) and arm64-darwin at -O0 and -O2, non -g, against main's 1,228
  same, 0 differ each (356 refused alike, 377/376 on x86_64-windows). tests/g-same.sh 867 same, 0 differ, 0 broken;
  tests/debug.sh gdb 5 passed, 0 failed. Exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95, 0/0); comments 0
  over; memory gate rc 0; ide up to date. **ast-residue rose: ast-lines 849 -> 850**, from m5-dbg's 7bc8b402
  (`Local::declared` in src/Ast.h, a reference's own type for DWARF) - not re-recorded; the coordinator's call.
- **Gate, Windows**: cl /W4 /WX clean (work\w4m, cxx3-msvc.exe built). The full wave gate is the coordinator's.
- **ast-lines re-recorded 849 -> 850** (the coordinator): `Local::declared` (m5-dbg's dbg2) keeps a reference
  parameter's declared type for DWARF beside the slot's pointer type; `Local` is the frame record the backends
  still read, and the line goes with it when the frame moves off Ast.h.

### Wave 4 landed, 2026-11-14 (the coordinator)

m6-i (B-file homes), m6-j (the packetiser on MIR), m6-k (the pipeliner on the stream, behind CXX1_C6X_TEXTPIPE),
m6-l (the packetiser reads typed operands), m5-dbg (tests/debug.sh; DWARF ranges, bool, references, enums, bases).
- **Gate at 94d75187**: Windows winpass -O0 and -O2: 828 same / 0 differ, masm 827 / 0. Mac run.sh -O0 and -O2
  1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0 with 46 untyped, ssa 890/0, coff 0 differ,
  c90 447/0, exclusions 0/0, comments 0, memory gate rc 0); asm6x 833 / 0 at -O0 and -O2.
- **tms6747** (vm6747, SIM=0, three shards): -O0 814 passed, 393.2 M cycles; -O2 814 passed, **192.0 M cycles**
  (the M6a figure, unchanged - wave 4 moves no C6000 output). 3 failed at both levels, as in wave 3's gate:
  noexcept-terminates, noexcept-terminates-callee, ssa-noexcept-throw - vm6747's runtime has no
  `std::set_terminate`; owed to the next C6000 round (a .notarget for vm6747, or the symbol in its runtime).
- cdb 5/5 both spellings and gdb 5/5 (dbg, dbg2); emit.sh's golden on Linux still owed.

### M6c, round 4 (the pipeliner's recognisers on operands), 2026-11-14, lane m6m

- **Landed** (from main 0e8877e6): every recogniser of the pipeliner reads a line through `Src` (C6xPipe.cpp), its one
  reader - `recognise` (the counted loop), `recogniseStepped` and the terms it reads the body as (`evaluateBody`),
  `recogniseWindow` whole (the byte window: its scan, the predicate classes, the pointer search) with `trackPointer`,
  and `pipelineAt`'s back-branch trial. On the stream an entry's kind, mnemonic, predicate (a register number), its
  operands - a register, an immediate's value, a pair, a byte address by mode and offset, an operand's registers - its
  reads and writes (`readsAndWrites(MInst)`, C6xPack.cpp, by `regsOf`, in the text pass's order) and its units
  (`unitsFor(MInst)`) come from the typed entry where it is `modelled`, else from its line. On the walker's text (`Src`
  with no stream) everything comes from the line, so the walker's functions cannot change. `Window` records where each
  body instruction stands (`at`). `CXX1_C6X_MIR=1` asks every recogniser on the stream a second time on the lines and
  names a verdict, reason or recognised shape that differs (`c6x-recog`). src/ +285 -144 lines (C6xPipe.cpp 2,040 ->
  2,166, C6xPack.cpp 322 -> 335, C6xMir.h 99 -> 101).
- **What round 4 is not.** What the recognisers hand on is unchanged: `Loop` and `Window` hold the body as `Line`s
  (`body`, `pre`) and registers as names (`iv`, `bound`, `P`, `C`, `used`, a pointer's `reg`), and the terms are
  strings over names. `buildEdges` (its `makeNode` calls on body lines), the II search, `renameWebs`, the emitters
  (`pipeline`, `emitWindow`, `emitClosed`) and the kernel's construction read lines as before; they are the next step,
  then design step 3. Nothing of step 3 was begun.
- **vm6747 and std::set_terminate.** noexcept-terminates, noexcept-terminates-callee and ssa-noexcept-throw call
  `std::set_terminate`, which vm6747's runtime (VM6747 src/Runtime.cpp, another checkout, not this tree) does not
  supply - it has `_ZSt9terminatev` alone. Their vm6747 leg is skipped by name: tests/tms6747-vm.txt (the reason
  beside each, printed every run); tests/tms6747.sh still runs their sim6747 leg, and with TI's rts6740_elf_eh.lib
  all three pass there.
- **Gate** (Linux ~/m6m, a copy of ~/w4g - main 0e8877e6's code, its compiler kept as REF; Windows work\m6m).
  `identical.sh ARCHS=tms6747` against main's compiler: -O0 and -O2 1,736 compared, 0 differ; -O0 -g 868 compared, 0
  differ once the tree's own path in `.dwpsn file "..."` is normalised (91 raw, every one `/home/ec2-user/w4g/` against
  `/m6m/`; identical.sh normalises the path in diagnostics, not in a `.s`). Census at -O2 (`CXX1_C6X_MIR=1`, 1,223
  cases): 0 `c6x-recog`, 0 `c6x-node`, 0 to the text pipeliner, 0 to the text pass; 106 functions with 2,727 placed
  entries, round 3's. tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2: 814 passed, **0 failed**, 3 not run on vm6747
  (tests/tms6747-vm.txt); **192,009,106 cycles** over 811 cases (192.0 M, unchanged). The three with SIM=1 (rts6740_elf_eh.lib):
  0 failed, run on sim6747 alone. tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed, at -O0
  and -O2. cl /W4 /WX clean. Exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95); comments 0 over; memory gate
  rc 0; ast-residue rc 0; ide/generate.py --check up to date. Not run (the coordinator's): winpass, make test, ssa.sh.

### M6c, round 5 (what the counted loop hands on, as entries), 2026-11-14, lane m6n

- **Landed** (from m6-m e25a8c7d): `Loop` carries its body as the stream's entries (`bodyM`, C6xPipe.cpp) beside its
  lines, taken where the recogniser takes each line (`take`) when every body entry is `modelled`, and kept equal to the
  lines through every edit the pipeliner makes before it schedules: the moved bound in the stepped test, `foldBodyCopies`'
  erasures (`erase`) and `renameWebs`' renaming (`rename`: a register, a pair's half, an address's base or index, a
  predicate, as `renamed` spells it on text). From the entries come what the body hands on: its reads and writes
  (`rw`, in `buildEdges`, `renameWebs`, `foldBodyCopies`, `wantedAfter`), its pairs (`pairIn`, the rename units and
  the webs' links), its stepped bases (`steppedAt`) and its nodes - `makeNode(MInst)` (C6xPack.cpp, now outside the
  anonymous namespace and declared in C6xMir.h), the one table, so the II search (`scheduleAt`, `criticalPath`) runs
  on nodes read from operands. A loop read on the walker's text, or with an entry not modelled, reads its lines as
  before. `CXX1_C6X_MIR=1` holds the two forms after recognition and after renaming (`c6x-body`: mnemonic, operands,
  predicate and stepped base of each entry against its line) and names a loop on the stream read from lines
  (`c6x-loop-lines`). src/ +110 -24 lines (C6xPipe.cpp 2,166 -> 2,248, C6xMir.h 101 -> 105, C6xPack.cpp 335).
- **Where it stopped.** The registers the loop is described by (`iv`, `bound`, `P`, `C`, `used`, `written`, the
  webs' and units' names) are still names; `addressForms` (the memory edges' algebra) reads `body` and `pre` as
  lines; `Node::line` is still set from the line, since the emitters (`pipeline`'s kernel through `spelled`,
  `steppedTest`, `emitWindow`, `emitClosed`) build the kernel as lines and stay on text this round. `Window` hands on
  lines as round 4 left it (its only readers past the recogniser are the emitters). **Design step 3 not begun**:
  `rounds` is five text folds under one liveness (`forwardMoves`, `forwardPairMoves`, `forwardConstants`,
  `flipPredicates`, `removeDead`), and `numberValues` and `foldAddressing` read `computeLiveness`'s line fields; each
  wants liveness on the stream first, which is the next round's first step.
- **Gate** (Linux ~/m6n, a copy of ~/m6m with the changed files laid over; REF main 0e8877e6's compiler in ~/w4g;
  Windows work\m6n). `identical.sh ARCHS=tms6747` against main's compiler: -O0 and -O2 1,736 compared, 0 differ; -O0
  -g 868 compared, 0 differ once the tree's own path in `.dwpsn` is normalised (91 raw, all `/w4g/` against `/m6n/`).
  Census at -O2 (`CXX1_C6X_MIR=1`): 0 `c6x-body`, 0 `c6x-loop-lines`, 0 `c6x-recog`, 0 `c6x-node`, 0 to the text
  pipeliner, 0 to the text pass; 106 functions with kernels. tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2: 814
  passed, 0 failed, 3 not run on vm6747; **192,009,106 cycles** (192.0 M, unchanged). tests/g-same.sh 867 same, 0
  differ, 0 broken; asm6x.sh 833 objects, 0 failed, at -O0 and -O2. cl /W4 /WX clean. Exclusions 129 sites 0 uncited
  0 stale (SSA lowering 554/95); comments 0 over; memory gate rc 0; ast-residue rc 0; ide/generate.py --check up to
  date. Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** the loop's registers as numbers and `addressForms` on entries; the emitters and the kernel built as
  entries (then `Node::line` goes); `Window`'s body as entries with them; design step 3 (liveness on the stream, then
  `rounds`, `numberValues`, `foldAddressing`).

### M6c, round 6 (liveness on the stream, and three of the folds), 2026-11-14, lane m6o

- **Landed** (from m6-n a0a1d2ce): **liveness is one dataflow over two readers.** `liveness(LiveFacts)` (C6xSched.cpp)
  is `computeLiveness`'s block walk, flow and fixed point, reading per line or entry only what it needs (`LiveFacts`:
  instruction, label, kept, predicated, mnemonic, branch target, read and write masks); `liveFacts(Line)` reads a line,
  `liveFacts(MInst)` (C6xPack.cpp) an entry - its operands where `modelled` (`readsAndWrites(MInst)`), else its
  spelling, a placed entry as the kept line `linesOf` hands on. `isCallTo`/`isReturnTo` are `isCall`/`isReturn` on a
  mnemonic and a target. `liveness(MStream)` gives each entry its block's live-in and live-out. `CXX1_C6X_MIR=1` asks
  the stream after every `computeLiveness` of a selected function and names a line whose liveness differs
  (`c6x-live`) or a text the stream does not spell (`c6x-live-unread`). **Then the folds**: `removeDead`,
  `flipPredicates` and `forwardConstants` of `rounds` run on a selected function's stream (C6xPack.cpp), editing
  operands - an erased entry, a flipped `predNot`, an immediate assigned - with the liveness they hold as computed in a
  `LiveSpan` beside the stream (an entry made anew all live, as a line made anew is), and `foldStream` reads the stream
  back, liveness and all. `deadAfter` on entries is the text's walk on `LiveFacts`. `CXX1_C6X_TEXTFOLD=1` keeps the
  text folds; `c6x-fold` names a text the stream does not spell. src/ +270 -28 lines (C6xPack.cpp 335 -> 511,
  C6xSched.cpp 2,332 -> 2,377, C6xMir.h 105 -> 117, C6xModel.h 133 -> 142).
- **Where it stopped.** `forwardMoves` and `forwardPairMoves` (the two folds of `rounds` that rename through
  `renamed` and judge the result by `crossings`/`unitsFor` on a rebuilt line), `numberValues` and `foldAddressing`
  stay on text; the stream is still made from lines per fold and read back (`foldStream`), not held across `rounds`.
  Design step 3's step (3) - the loop's registers as operands - not begun.
- **Gate** (Linux ~/m6o, a copy of ~/m6n with the changed files laid over; REF main 0e8877e6's compiler in ~/w4g;
  Windows work\m6o against work\m6n's cxx3-msvc.exe). `identical.sh ARCHS=tms6747` against main's compiler: -O0 and
  -O2 1,736 compared, 0 differ; -O0 -g 868 compared, 0 differ once the tree's own path in `.dwpsn` is normalised (91
  raw, all `/w4g/` against `/m6o/`). Windows: tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs (2,601 texts, 1,068
  refused alike), 0 differ - plain and `CXX1_C6X_TEXTFOLD=1`. Census at -O2 (`CXX1_C6X_MIR=1`, both boxes): **0
  `c6x-live`**, 0 `c6x-live-unread`, 0 `c6x-fold`, 0 `c6x-body`, 0 `c6x-loop-lines`, 0 `c6x-node`, 0 to the text
  pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed entries). tests/tms6747.sh SHARD=k/3
  CYCLES=1 SIM=0 at -O2: 814 passed, 0 failed, 3 not run on vm6747; **192,009,106 cycles** (192.0 M, unchanged).
  tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed, at -O0 and -O2. cl /W4 /WX clean.
  Exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95); comments 0 over; memory gate rc 0; ast-residue rc 0;
  ide/generate.py --check up to date. Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** `forwardMoves`, `forwardPairMoves`, `numberValues`, `foldAddressing` on the stream; the stream held
  across `rounds` rather than made per fold; the loop's registers as operands (round 5's owed list stands).

### M6c, round 7 (the rest of the folds, and one stream across them), 2026-11-14, lane m6p

- **Landed** (from m6-o b86a89c3): **every fold between the pipeliner and the packetiser runs on a selected function's
  stream.** `forwardMoves`, `forwardPairMoves`, `numberValues` and `foldAddressing` (with `foldScaledIndex`) are
  C6xSched's on entries (C6xPack.cpp), read through `Folding`: the stream, each entry as the text reads it (`viewOf`,
  a placed entry a kept line) and its `LiveSpan` - held as computed, or for `numberValues` recomputed after an edit, as
  `deadAfter` does on text. An edit is an assignment to the entry's operands (a register renamed, a destination or a
  pair set, an address written as `MOperand`), judged by `crossings`/`unitsFor` on the entry, C6xSched's `legalForm`
  on an entry; an entry not `modelled` is edited by the text fold's rebuilt line read back. `CXX1_C6X_MIR=1` names an
  operand edit the text fold would spell otherwise (`c6x-fold-edit`). **One stream held**: `rounds` makes the stream
  once and runs its five folds on it to the fixed point, read back once (liveness then current, as the text's is);
  after the pipeliner one stream goes from `numberValues` through `rounds` and `foldAddressing` into `packetise`, never
  printed and re-read. `foldStream` and `fold` are gone. `CXX1_C6X_TEXTFOLD=1` keeps every text fold; the walker's
  functions keep them. src/ +638 -36 lines (C6xPack.cpp 511 -> 1,075, C6xSched.cpp 2,377 -> 2,411, C6xMir.h 117 ->
  119, C6xModel.h 142 -> 144); the text forms go with `Line` at design step 4.
- **Where it stopped.** Design step 3's last part - the loop's registers as operands (`iv`, `bound`, `P`, `C`, `used`,
  the webs' names), `addressForms` on entries, the emitters building the kernel as entries - not begun. The passes
  before the pipeliner (`foldCalls` to `widenFills`) and `hoistConstantPairs` stay on text, and `Folding` still asks
  each entry's mnemonic and operand spellings through its view; their readers move to operands with them.
- **Gate** (Linux ~/m6p, a copy of ~/m6o with the changed files laid over; REF main a5bc8eec's compiler built in its
  own kept tree ~/w6ref; Windows work\m6p against work\m6o's cxx3-msvc.exe). `identical.sh ARCHS=tms6747` against
  main's compiler: -O0 and -O2 1,736 compared, 0 differ; -O0 -g 868 compared, 0 differ once the tree's own path is
  normalised (91 raw, all `/w6ref/` against `/m6p/`). Windows: tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs
  (2,601 texts, 1,068 refused alike), 0 differ - plain and `CXX1_C6X_TEXTFOLD=1`. Census at -O2 (`CXX1_C6X_MIR=1`,
  both boxes): 0 `c6x-fold`, 0 `c6x-fold-edit`, 0 `c6x-live`, 0 `c6x-live-unread`, 0 `c6x-node`, 0 to the text
  pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed entries). tests/tms6747.sh SHARD=k/3
  CYCLES=1 SIM=0 at -O2: 814 passed, 0 failed, 3 not run on vm6747; **192,009,106 cycles** (192.0 M, unchanged).
  tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed, at -O0 and -O2. cl /W4 /WX clean.
  Exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95); comments 0 over; memory gate rc 0; ast-residue rc 0;
  ide/generate.py --check up to date. Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** the loop's registers as operands and `addressForms` on entries; the emitters and the kernel as entries
  (then `Node::line` goes); `Window`'s body as entries; the passes before the pipeliner on the stream; then step 4.

### M6c, round 8 (the passes before `reuseEntryLoads` on the stream), 2026-11-14, lane m6q

- **Landed** (from m6-p 04aa7c5a): **a selected function's stream is made once, as the text is parsed, and held to
  `reuseEntryLoads`.** `foldFront` (C6xPack.cpp) is `foldCalls`, `foldMvk`, `foldFrame` and `foldPushPop` (their
  liveness held as computed in a `LiveSpan`, as `LivenessHeld` holds the text's), `threadPredicates` and
  `dropUnnamedLabels` on entries; the stream then goes through `rounds` (read back, the stream still the lines'),
  `predicateArms` and its `rounds`, and `tidyStream` (`rotateLoops`, `tidyJumps`, `dropUnnamedLabels`), read back
  once. An edit is an assignment to an entry - a branch's target, a predicate set or flipped, a frame address written
  as `MOperand`, a push made a copy. `CXX1_C6X_MIR=1` runs the text passes on a copy and names the first line the
  stream's read-back spells otherwise (`c6x-front`); `CXX1_C6X_TEXTFOLD=1` keeps the text passes, the walker's
  functions keep them. src/ +364 -12 lines (C6xPack.cpp 1,075 -> 1,405, C6xSched.cpp 2,411 -> 2,429, C6xMir.h 119 -> 123).
- **Where it stopped.** Round 7's item (1) - the counted loop's registers as operands, `addressForms`, the kernel and
  the emitters on entries - not begun. **What still reads `Line`** on a selected function (the list design step 4's
  deletion needs): the parse of the printed text itself (the selector's `MStream` is printed, then re-read); the
  passes `reuseEntryLoads`, `hoistInvariants`, `inductionPointers`, `widenFills` and `hoistConstantPairs`; inside the
  pipeliner `Loop`'s `body`/`pre` and its names (`iv`, `bound`, `P`, `C`, `used`, `written`, the webs'), `addressForms`,
  `Window`'s body, `Node::line` and the emitters (`pipeline`, `steppedTest`, `emitWindow`, `emitClosed`); `Folding`'s
  views (`viewOf`) and the table readers asked of a line where an entry is not `modelled` (`lineOf`); and three
  crossings between the forms - `linesOf` before `reuseEntryLoads`, `streamOf`/`linesOf` around the pipeliner, and
  `streamOf` after `hoistConstantPairs`.
- **Gate** (Linux ~/m6q, a copy of ~/m6p with the changed files laid over; REF main a5bc8eec's compiler in ~/w6ref;
  Windows work\m6q against work\m6p's cxx3-msvc.exe). `identical.sh ARCHS=tms6747` against main's compiler: -O0 and
  -O2 1,736 compared, 0 differ; -O0 -g 868 compared, 0 differ once the tree's own path is normalised (91 raw, all
  `/w6ref/` against `/m6q/`). Windows: tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs (2,601 texts, 1,068 refused
  alike), 0 differ. Census at -O2 (`CXX1_C6X_MIR=1`, both boxes): **0 `c6x-front`**, 0 `c6x-fold`, 0 `c6x-fold-edit`,
  0 `c6x-live`, 0 `c6x-live-unread`, 0 `c6x-node`, 0 to the text pipeliner or pass; 6,835 selected functions, 106 with
  kernels (2,727 placed entries). tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2: 814 passed, 0 failed;
  **192,009,106 cycles** (192.0 M, unchanged). tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0
  failed, at -O0 and -O2. cl /W4 /WX clean. Exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95); comments 0
  over; memory gate rc 0; ast-residue rc 0; ide/generate.py --check up to date. Not run (the coordinator's): winpass,
  make test, ssa.sh.
- **Owed:** round 7's item (1) whole; `reuseEntryLoads` to `widenFills` and `hoistConstantPairs` on the stream, so one
  stream runs from the parse to the packetiser; then the selector's stream handed in unprinted; then step 4.
### M6c, round 9 (the loop passes on the stream, one stream from the parse to the packetiser), 2026-11-14, lane m6r

- **Landed** (from main 85fe99fe): **the five loop passes are one implementation over two forms.** `reuseEntryLoads`,
  `hoistInvariants`, `inductionPointers`, `widenFills` and `hoistConstantPairs` (C6xSched.cpp) read and edit through
  `LoopText` (C6xModel.h): `v` as lines, a line blanked or set, a preheader inserted (`apply`, which was `applyLoop`),
  the blanks dropped (`compact`), liveness asked (`liveIn`, `liveOut`, which were `liveInAt`/`liveOutAt`). The text's
  `LoopText` edits the lines themselves; `LoopStream` (C6xPack.cpp, `loopPass`) holds a selected function's stream,
  `v` its entries as the text reads them (`viewOf`), and makes every edit on the entry too - a line set or inserted is
  read as `streamOf` reads one, a blank erased with the entry - with liveness the stream's own (`liveness(MStream)`; the
  views' only while a blank is pending, which no pass asks). **The three hand-overs are gone**: a selected function's
  stream is made once as its text is parsed and goes through `foldFront`, the rounds, the loop passes, the pipeliner,
  `hoistConstantPairs`, `numberValues`, the rounds again and `foldAddressing` into `packetise` without `linesOf` or
  `streamOf` between (the lines are read back only where the text pipeliner takes a function, `CXX1_C6X_TEXTPIPE=1` or
  a loop not spelled). `CXX1_C6X_MIR=1` runs each loop pass on the text too and names the first line the stream's
  differs at (`c6x-loops`), and a line the stream cannot read back (`c6x-loop-unread`). src/ +152 -49 lines
  (C6xPack.cpp 1,405 -> 1,462, C6xSched.cpp 2,429 -> 2,450, C6xMir.h 123 -> 126, C6xModel.h 144 -> 166).
- **Where it stopped.** Item (1) - the counted loop's registers as operands, `addressForms`, `Window`'s body,
  `Node::line` and the emitters on entries - not begun. The loop passes' edits are spelled as text and read back
  (`LoopStream::entry`), not assigned to operands; their readers read the views. **What still reads `Line`** on a
  selected function: the parse of the printed text itself (the selector's `MStream` printed, then re-read); the loop
  passes' views and spelled edits (`LoopStream`); `Folding`'s views (`viewOf`) and the table readers asked of a line
  where an entry is not `modelled` (`lineOf`); inside the pipeliner `Loop`'s `body`/`pre` and its names (`iv`, `bound`,
  `P`, `C`, `used`, `written`, the webs'), `addressForms`, `Window`'s body, `Node::line` and the emitters (`pipeline`,
  `steppedTest`, `emitWindow`, `emitClosed`); `rounds(MStream&, lines, dirty)`, which still writes the lines back
  (read on the held path by nothing); and the census copies under `CXX1_C6X_MIR=1`.
- **Gate** (Linux ~/m6r, a copy of ~/m6q with the changed files laid over; REF main 85fe99fe's compiler built in its
  own kept tree ~/w7ref; Windows work\m6r against work\m6q's cxx3-msvc.exe). `identical.sh ARCHS=tms6747` against main's compiler: -O0 and
  -O2 1,736 compared, 0 differ; -O0 -g 868 compared, 0 differ once the tree's own path is normalised (91 raw, all
  `/w7ref/` against `/m6r/`). Windows: tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs (2,601 texts, 1,068 refused
  alike), 0 differ. Census at -O2 (`CXX1_C6X_MIR=1`, both boxes): **0 `c6x-loops`**, 0 `c6x-loop-unread`, 0
  `c6x-front`, 0 `c6x-fold`, 0 `c6x-fold-edit`, 0 `c6x-live`, 0 `c6x-live-unread`, 0 `c6x-node`, 0 to the text
  pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed entries). tests/tms6747.sh SHARD=k/3
  CYCLES=1 SIM=0 at -O2: 814 passed, 0 failed, 3 not run on vm6747; **192,009,106 cycles** (192.0 M, unchanged).
  tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed, at -O0 and -O2. cl /W4 /WX clean.
  Exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95); comments 0 over; memory gate rc 0; ast-residue rc 0;
  ide/generate.py --check up to date. Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** round 7's item (1) whole; the loop passes' edits on operands and their readers off the views; the
  selector's stream handed in unprinted; then step 4.
### M6c, round 10 (the loop passes' edits on operands, the kernel's turns spelled from entries, the held rounds write no lines), 2026-11-14, lane m6s

- **Landed** (from m6-r 867184a6): **a loop pass's edit is an assignment to the entry.** `LoopStream::edited`
  (C6xPack.cpp) makes each line a loop pass sets or inserts on the entry itself: the entry's mnemonic, predicate and
  only the operands whose spelling differs assigned (a hoisted or preheader line a fresh entry built from its fields),
  where round 9 re-read the whole line (`entry`, `streamOf` of one line). The line's spelling is the check, not the
  source: an edit the entry does not spell as the line, a placed or `||` entry, or a branch with a named unit, is read
  as before. `CXX1_C6X_MIR=1` reads each edited line too and names an entry whose kind, mnemonic, predicate or operands
  differ from the reading (`c6x-loop-edit`). **The held rounds write nothing back**: `rounds(MStream &, lines *)` takes
  no lines on a selected function's held stream (the three calls in `c6xSchedule`), where round 9 read the stream back
  into lines and recomputed their liveness for nobody; `rounds(std::vector<Line> &)` (a stream made for one call) still
  hands its lines back. **The kernel's turns from the body's entries**: `spelled` (C6xPipe.cpp), which writes body
  instruction o as turn j of the prologue, kernel and epilogue, renames each unit's register to that turn's copy on
  the entry (`bodyM`, a register's number in a register, a pair's half, an address's base or index, the predicate)
  and prints it; a loop read from lines keeps the text's (`spelledLine`, reading `body`, not the node). `Node::line` is
  no longer set on the stream (its last reader there was `spelled`; the two traces read `body`). `CXX1_C6X_MIR=1`
  spells each turn both ways and names one that differs (`c6x-spell`). src/ +77 -19 lines (C6xPack.cpp 1,462 ->
  1,485, C6xSched.cpp 2,450 -> 2,451, C6xPipe.cpp 2,248 -> 2,282).
- **Where it stopped.** The rest of item (1) of round 9's owed list - the counted loop's registers (`iv`, `bound`,
  `P`, `C`, `used`, `written`, the webs') as operands, `addressForms`, `Window`'s body, and the emitters' own lines
  (`pipeline`'s guards, counts and copies, `steppedTest`, `emitWindow`, `emitClosed`) on entries - not begun: the
  kernel is still assembled as spelled lines (`emitCycles`, `verbatim`) and taken back through `streamOf`, and
  `pipelineLoops(MStream&)` reads the stream through `linesOf` for `Src`. The loop passes still compose each edit as a `Line` (`rebuilt`, `make`) and read the stream through
  its views. **What still reads `Line`** on a selected function: the parse of the printed text (the selector's
  `MStream` printed, then re-read); the loop passes' views and the `Line`s they compose (`LoopStream`, its liveness from
  the views while a blank is pending); `Folding`'s views (`viewOf`) and the table readers asked of a line where an
  entry is not `modelled` (`lineOf`, `legal`); the whole pipeliner - `Src`'s lines beside the stream, `Loop`'s `body`/
  `pre` and its names, `addressForms`/`evalLine`, `Window`'s body, the emitters' lines (the turns' text aside), `namesOf`'s liveness,
  the `linesOf`/`streamOf` crossing around it; `rounds(std::vector<Line>&)` where a function is not held; and the
  census copies under `CXX1_C6X_MIR=1`.
- **Gate** (Linux ~/m6s, a copy of ~/m6r with the changed files laid over; REF main 85fe99fe's compiler in its kept
  tree ~/w7ref; Windows work\m6s against work\m6r's cxx3-msvc.exe; on the final commit). `identical.sh ARCHS=tms6747`
  against main's compiler: -O0 and -O2 1,736 compared, 0 differ; -O0 -g 868 compared, 0 differ once the tree's own path
  is normalised (91 raw, all `/w7ref/` against `/m6s/`). Windows: tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs
  (2,601 texts, 1,068 refused alike), 0 differ. Census at -O2 (`CXX1_C6X_MIR=1`, both boxes): **0 `c6x-loop-edit`, 0
  `c6x-spell`**, 0 `c6x-loops`, 0 `c6x-loop-unread`, 0 `c6x-front`, 0 `c6x-fold`, 0 `c6x-fold-edit`, 0 `c6x-live`, 0
  `c6x-live-unread`, 0 `c6x-body`, 0 `c6x-loop-lines`, 0 `c6x-recog`, 0 `c6x-node`, 0 to the text pipeliner or pass;
  6,835 selected functions, 106 with kernels (2,727 placed entries). tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2:
  814 passed, 0 failed, 3 not run on vm6747; **192,009,106 cycles** (192.0 M, unchanged). tests/g-same.sh 867 same, 0
  differ, 0 broken; asm6x.sh 833 objects, 0 failed, at -O0 and -O2. cl /W4 /WX clean. Exclusions 129 sites 0 uncited 0
  stale (SSA lowering 554/95); comments 0 over; memory gate rc 0; ast-residue rc 0; ide/generate.py --check up to
  date. Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** the rest of round 7's item (1) (the loop's registers as numbers, `addressForms`, `Window`, the kernel
  built as entries, then `Node::line` goes with the text pipeliner); the loop passes composing
  their edits as entries (their readers off the views); the selector's stream handed in unprinted; then step 4.
### Wave 5 landed, 2026-11-14 (the coordinator)

M6c rounds 4 (m6m: the recognisers read operands; vm6747's noexcept legs skipped by name) and 5 (m6n: the counted
loop's body as entries, buildEdges, the II search and renameWebs on operands).
- **Gate at a0a1d2ce**: Windows winpass -O0 and -O2: 828 same / 0 differ, masm 827 / 0. Mac run.sh -O0 and -O2
  1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0, c90 447/0, exclusions 0/0,
  comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, **0 failed** at -O0 (393.2 M cycles) and -O2
  (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.

### Wave 6 landed, 2026-11-14 (the coordinator)

M6c rounds 6 (m6o: liveness on the stream; removeDead, flipPredicates, forwardConstants edit operands) and 7 (m6p:
forwardMoves, forwardPairMoves, numberValues, foldAddressing on the stream; one stream across `rounds` and on into
the packetiser; foldStream and fold deleted; `CXX1_C6X_TEXTFOLD=1` keeps the text folds).
- **Gate at 04aa7c5a**: Windows winpass -O0 and -O2: 828 same / 0 differ, masm 827 / 0. Mac run.sh -O0 and -O2
  1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0, c90 447/0, exclusions 0/0,
  comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0 (393.2 M cycles) and -O2
  (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2. The merge with main a5bc8eec adds plan text only.

### Wave 7 landed, 2026-11-14 (the coordinator)

M6c round 8 (m6q: a selected function is one stream from its parse to `reuseEntryLoads`; foldCalls through
tidyJumps on entries; `c6x-front` census 0).
- **Gate at 089582e3** (c4e540c7 merged with main 7b4ab022): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0, exclusions 0/0, comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0
  (393.2 M cycles) and -O2 (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.

### M6c, round 11 (the emitters write entries, the address walk reads operands), 2026-11-14, lane m6t

- **The order, planned first.** What must change together: a kernel written as entries needs every emitter of the
  counted loop on one sink at once (a guard as a line beside a turn as an entry is two streams), so (3) is one step;
  `addressForms` with `evalLine` is a second, alone, its answer held against the lines'. The loop's registers as
  numbers (1) touch every recogniser, `renameWebs`, the units and both emitters together - they follow (3), which
  takes the last reader of the emitters' `Line`s away, and were not begun.
- **Landed** (from m6-s 32b56531): **the emitters write entries.** `Code` (C6xPipe.cpp) is what `pipeline`,
  `steppedTest`, `steppedGuard` and `emitCycles` write: lines for a loop read from text; on the stream an `MStream` -
  a guard, count or copy an entry built from its parts (`add`; `legal`, which is `legalForm` on the entry: the two
  sources commuted where both cross, `crossings` and `unitsFor` asked of the operands), a turn the body's renamed
  entry itself (`spelled` returns it, a `Turn`), the idle cycles, the kernel's label, its branch and its count's step
  entries placed as they are put (`par`, `packed`), and the loop's own compare the stream's entry (`same`). **A kernel
  no longer goes back through `streamOf`**: `pipelineLoops(MStream&)` inserts `Code::m`. The window's turns
  (`emitWindow`, `emitClosed`, `giveRegisters`) are still composed as lines over `%n` names and enter the sink by
  their fields (`push_back`). `CXX1_C6X_MIR=1` composes the lines too, reads them as round 10 did and names a loop
  whose entries differ, or a guard whose form or verdict does (`c6x-emit`). **The address walk reads operands**:
  `addressForms` and `evalLine` read `Ins` - the mnemonic, the first two operands as numbers or names, reads and
  writes, the stepped register and its step, the access's base, offset or index - made from a line's text off the
  stream and from the entry's operands on it (`Loop::addresses`; `preM` is the preheader as entries). The census
  walks the lines too and names an access whose form differs (`c6x-addr`). src/ +311 -124 lines (C6xPipe.cpp 2,282 ->
  2,469).
- **Where it stopped.** Item (1) not begun: `Loop`'s `iv`, `bound`, `P`, `C`, `used`, `written`, `predicated`, the webs
  and the units are names, and `Form`'s terms with them. Of (2), `Window`'s body is lines. Of (3),
  `pipelineLoops(MStream&)` still reads the stream through `linesOf` (`Src::v` for an entry not `modelled`, `namesOf`'s
  liveness, the window's recogniser and emitters). **What still reads `Line`** on a selected function: the parse of
  the printed text; the loop passes' views and composed edits (`LoopStream`); `Folding`'s views and the table readers
  where an entry is not `modelled`; in the pipeliner `Src`'s lines beside the stream and `namesOf`, `Loop::body`/`pre`
  kept beside `bodyM`/`preM` (the traces, `checkBody`, `testBody`'s edit), the window whole
  (`Window::body`, `trackPointer`, `affineTurn`, `emitWindow`, `emitClosed`, `giveRegisters`); `rounds(std::vector<Line>&)`
  where a function is not held; and the census copies under `CXX1_C6X_MIR=1`.
- **Gate** (Linux ~/m6t, a copy of ~/m6s with the changed files laid over; REF main 85fe99fe's compiler in its kept
  tree ~/w7ref; Windows work\m6t against work\m6s's cxx3-msvc.exe; on the final source, 4aa91c46).
  `identical.sh ARCHS=tms6747` against main's compiler: -O0 and -O2 1,736 compared, 0 differ; -O0 -g 868 compared, 0
  differ once the tree's own path is normalised (91 raw, all `/w7ref/` against `/m6t/`). Windows: tests/cases at -O0,
  -O1, -O2 by md5, 3,669 outputs (2,601 texts, 1,068 refused alike), 0 differ. Census at -O2 (`CXX1_C6X_MIR=1`, both
  boxes): **0 `c6x-emit`, 0 `c6x-addr`**, 0 `c6x-spell`, 0 `c6x-loop-edit`, 0 `c6x-loops`, 0 `c6x-loop-unread`, 0
  `c6x-front`, 0 `c6x-fold`, 0 `c6x-fold-edit`, 0 `c6x-live`, 0 `c6x-live-unread`, 0 `c6x-body`, 0 `c6x-loop-lines`, 0
  `c6x-recog`, 0 `c6x-node`, 0 to the text pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed
  entries). tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2: 814 passed, 0 failed, 3 not run on vm6747;
  **192,009,106 cycles** (192.0 M, unchanged). tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0
  failed, at -O0 and -O2. cl /W4 /WX clean. Exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95); comments 0
  over; memory gate rc 0; ast-residue rc 0; ide/generate.py --check up to date. Not run (the coordinator's): winpass,
  make test, ssa.sh.
- **Owed:** item (1) whole (the loop's registers, webs and units as numbers; `Form`'s terms); the window on entries
  (`Window::body`, its emitters over `%n` names); `Src` without the lines, so `pipelineLoops(MStream&)` drops
  `linesOf` and `Loop::body`/`pre` go; the loop passes composing their edits as entries; the selector's stream handed
  in unprinted; then step 4.
### M6c, round 12 (the counted loop's registers as numbers: the masks land, the names of the sets go), 2026-11-14, lane m6u

- **The order, written first** (each a commit of identical output with the census at 0): (S1) numbers and masks in
  `Loop` beside the names, read from the entries' operands, held against the names by a census; (S2) the sets' readers
  on the masks; (S3) the comparisons on the numbers; (S4) the sets deleted; (S5) the emitters' sink (`Code::add`,
  `legal`, `steppedTest`, `steppedGuard`, `pipeline`'s guard and count) taking `P`, `C`, `iv`, `bound`, `ivs`, `bounds`,
  `steps` as numbers; (S6) the webs and units (`Web::reg`, `renameWebs`' maps, `Unit::regs`/`copies`, `renameable`,
  `minQ`) as numbers, so `spelled` needs no `regNumber`; (S7) `addressForms`' `iv` and `Ins`' reads and writes as
  numbers, and `Form`'s terms; (S8) the recognisers' algebra (`evaluateBody`'s `env`, `recogniseStepped`) keyed by
  number, and the names deleted.
- **Landed** (from m6-t 8a9617f8), S1-S4, four commits: **S1** `Loop` holds `ivR`, `boundR`, `PR`, `CR` (kNoReg where
  a constant stands or no count is picked) and the masks `usedM`, `writtenM`, `predicatedM`, `rmwFirstM`, `pinnedM`,
  filled where the names were - the predicate from the back branch's entry (`Src::predReg`), the counter and bound
  from the compare's operands (`regAt`), the masks from each entry's operands (`Src::masks`, `Loop::masks`: the
  packetiser's `readsAndWrites` as masks, exported) - and `CXX1_C6X_MIR=1` names a loop whose number or mask is not
  its name's or its set's (`c6x-reg`, at `checkBody`'s two places). **S2** the sets' readers read the masks (the
  count's pick, the pool, what a unit may rename, `pinned`); `predicated`, `rmwFirst` and `pinned()` deleted. **S3** the
  comparisons read the numbers: P's bit at the exit and in the schedule, the gate, the count picked by number
  (`kCondR`), what the webs, the copy fold, the pool and the units keep (`fixedM`, `Loop::bit`). **S4** `used` and
  `written` deleted: `addressForms` takes `writtenM`, and the units are made from `usedM` in the names' order (the
  order a set of names gave, which is the units' and so the kernel's). src/ +79 -47 lines (C6xPipe.cpp 2,469 ->
  2,499, C6xPack.cpp 1,485 -> 1,486, C6xMir.h 126 -> 127).
- **What still reads names** in the counted loop: `Loop`'s `iv`, `bound`, `P`, `C` (the recognisers' `has`/`op ==`
  tests, `sameLoop`, the traces) and `ivs`, `bounds`, `steps`; every emitter's operand (`Code::add`/`legal` take
  spellings; `sideOf(L.P)`, `sideOf(L.bound)`); the pool and `X`, `xt`, `moved`; the webs (`Web::reg`/`name`, `cur`,
  `fixed`), the units (`Unit::regs`, `copies`, `unitOf`, `renameable`, `minQ`), `buildEdges`' maps; `Ins`'s reads,
  writes, base and index and `Form`'s terms; `evaluateBody`'s `env` and `recogniseStepped`'s terms; `steppedAt`;
  `wantedAfter` and `foldBodyCopies` (lines); and off the counted loop the window whole, `namesOf`, `Src`'s lines.
- **Gate** (Linux ~/m6u, a copy of ~/m6t with the changed files laid over; REF main 85fe99fe's compiler in its kept
  tree ~/w7ref; Windows work\m6u against work\m6t's cxx3-msvc.exe). **Each step on its own**: S1, S2, S3 and S4 each
  built and run alone on Windows - tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs (2,601 texts, 1,068 refused
  alike), 0 differ, and 0 `c6x-reg` - and on Linux 0 `c6x-reg` each (S3 and S4 also `identical.sh`, 0 differ). On the
  final source (S4, 5c2d509f): `identical.sh ARCHS=tms6747` against main's compiler, -O0 and -O2 1,736 compared, 0
  differ; -O0 -g 868 compared, 0 differ once the tree's own path is normalised (91 raw, all `/w7ref/` against
  `/m6u/`). Census at -O2 (`CXX1_C6X_MIR=1`, both boxes): **0 `c6x-reg`**, 0 `c6x-emit`, 0 `c6x-addr`, 0 `c6x-spell`,
  0 `c6x-loop-edit`, 0 `c6x-loops`, 0 `c6x-loop-unread`, 0 `c6x-front`, 0 `c6x-fold`, 0 `c6x-fold-edit`, 0 `c6x-live`,
  0 `c6x-live-unread`, 0 to the text pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed
  entries). tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2: 814 passed, 0 failed, 3 not run on vm6747;
  **192,009,106 cycles** (192.0 M, unchanged). tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0
  failed, at -O0 and -O2. cl /W4 /WX clean (four builds). Exclusions 129 sites 0 uncited 0 stale (SSA lowering
  554/95); comments 0 over; memory gate rc 0; ast-residue rc 0; ide/generate.py --check up to date. Not run (the
  coordinator's): winpass, make test, ssa.sh.
- **Owed:** S5-S8 above, in that order (S5 first: it takes the emitters' `sideOf` of a name and `Code`'s spellings;
  S6 is the largest - the webs, units and `buildEdges` together); then the window on entries, `Src` without the
  lines, the loop passes composing entries, the selector's stream handed in unprinted, step 4.
### Wave 8 landed, 2026-11-14 (the coordinator)

M6c rounds 9 (m6r: the five loop passes on the stream; one stream from the parse to packetise) and 10 (m6s: the
loop passes' edits on operands; `rounds` writes no lines on a held stream; the kernel's turns rename entries).
- **Gate at 32b56531**: Windows winpass -O0 and -O2: 828 same / 0 differ, masm 827 / 0. Mac run.sh -O0 and -O2
  1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0, c90 447/0, exclusions 0/0,
  comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0 (393.2 M cycles) and -O2
  (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.
### M6c, round 13 (the counted loop on numbers: S5-S8 of round 12's plan, the names deleted), 2026-11-14, lane m6v

- **Landed** (from m6-u af87b476), S5-S8, four commits, each of identical output: **S5** (c3981531) the emitters' sink:
  `Code::add` and `legal` take operands (`R` a register by number, `RR` a pair, `K` a constant, a label) and a
  predicate by number, `branch` writes `[P] B`; the line a loop off the stream wants is spelled from the entry
  (`lineOf`); `steppedTest`, `steppedGuard`, `pipeline`'s guard and count, the kernel's branch and step (`turnOf`) and
  `scheduleAt`'s two reserved nodes read `PR`, `CR`, `ivR`, `boundR` and the stepped counter's `ivsR`, `boundsR`,
  `stepsR`, `stepK`. **S6** (ca93c160) the webs, units and pool: `Web::reg`/`name`, `renameWebs`' `cur` and `fixed`,
  `Unit::regs`/`copies`, `unitOf`, `renameable` (a mask), `minQ`, `buildEdges`' accesses, `takeFree`, `X`, `moved`,
  `xt` are numbers, so `spelled` asks no `regNumber`; a map that was walked in the names' order still is (`ByName`:
  A10 before A2 - the units' order and the edges', which the schedule's ties keep). **S7** (6c01a0c5) the address walk:
  `Ins`' reads, writes, step, base and index are numbers, a `Form`'s term is a `Term` (the constant, the induction
  variable, a register's entry value, an object - a symbol or the frame), `addressForms` takes `ivR`;
  `readsAndWrites` on an entry lists numbers (C6xPack.cpp), the names made from that one list. **S8** (b821a54c) the
  recognisers: `evaluateBody`'s `env`, `recogniseStepped`'s writers, steps and bounds and `recognise`'s tests are keyed
  and compared by number (`Src::regs`, `regAt`); a term still spells a register by name, so the sorted sums and their
  substring tests are what they were. **Deleted**: `Loop`'s `iv`, `bound`, `P`, `C`, `ivs`, `bounds`, `steps`,
  `kCond`, `Code::opsOf`, and the `c6x-reg` census with them (it held at 0 through S5-S7; nothing is left for it to
  hold against). src/ +448 -386 lines (C6xPipe.cpp 2,499 -> 2,554, C6xPack.cpp 1,486 -> 1,492, C6xMir.h 127 -> 128).
- **What still reads names** in the counted loop: the body's lines kept beside the entries - `renameWebs`' and
  `spelledLine`'s `renamed` on a line (spelled from the numbers), the moved bound's substitution into a line,
  `wantedAfter` and `foldBodyCopies` (lines, `Loop::rw`), `steppedAt` (a name; `rmwFirstM` asks `bitOf` of it),
  `Ins` from a line where the preheader or body is not all modelled, `Loop::regs`/`Src::regs` numbering a line's
  names off the stream; the algebra's terms (strings that spell a register); `Code::push_back`/`entryOf` for the
  window's composed lines. Off the counted loop: the window whole (`Window`, `WinCode`, `Aff`), `namesOf`, `Src`'s lines.
- **Gate** (Linux ~/m6v, a copy of ~/m6u with the changed files laid over; REF main 85fe99fe's compiler in its kept
  tree ~/w7ref; Windows against work\m6u's cxx3-msvc.exe). **Each step on its own** on Windows, its own tree built by
  cl /W4 /WX clean: tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs - S5 (work\m6v) 0 differ; S6 (work\m6v6), S7
  (work\m6v7s) and S8 (work\m6v7) 3,663 same and the same 6 named, which are two refused cases (posix-headers,
  windows-h-layout) whose caret is indented by the tree's path - a longer tree name, not the compiler; S8 again in
  work\m6v (the reference's name length): 2,601 texts, 1,068 refused alike, **0 differ**. `c6x-reg` 0 at S5, S6, S7
  (deleted at S8); S5 alone on Linux too (g++ clean, `c6x-reg` 0); S6 and S7 were not built alone on Linux. On the
  final source (S8, b821a54c): `identical.sh ARCHS=tms6747` against main's compiler, -O0 and -O2 1,736 compared, 0
  differ; -O0 -g 868 compared, 91 raw (`/w7ref/` against `/m6v/`), 851 texts 0 differ with the tree path made the
  same. Census at -O2 (`CXX1_C6X_MIR=1`, both boxes): 0 `c6x-emit`, 0 `c6x-addr`, 0 `c6x-spell`, 0 `c6x-loop-edit`,
  0 `c6x-loops`, 0 `c6x-loop-unread`, 0 `c6x-front`, 0 `c6x-fold`, 0 `c6x-fold-edit`, 0 `c6x-live`, 0
  `c6x-live-unread`, 0 to the text pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed
  entries). tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2: 814 passed, 0 failed, 3 not run on vm6747;
  **192,009,106 cycles** (192.0 M, unchanged). tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0
  failed, at -O0 and -O2. cl /W4 /WX clean (five builds), g++ -Werror clean. Exclusions 129 sites 0 uncited 0 stale
  (SSA lowering 554/95); comments 0 over; memory gate rc 0; ast-residue rc 0; ide/generate.py --check up to date. Not
  run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** the body's lines beside the entries (the list above) - then the window on entries, `Src` without the
  lines, the loop passes composing entries, the selector's stream handed in unprinted, step 4. Not begun here:
  `Window`, `pipelineLoops`' `linesOf`.

### Wave 9 landed, 2026-11-14 (the coordinator)

M6c rounds 11 (m6t: the counted loop's emitters write entries through `Code`; `addressForms`/`evalLine` read
`Ins`) and 12 (m6u: steps S1-S4 of the register plan - `ivR`, `boundR`, `PR`, `CR` and the masks; the name sets gone).
- **Gate at af87b476** (09ca5bbb merged with main ed1eaba2): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0, exclusions 0/0, comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0
  (393.2 M cycles) and -O2 (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.
### M6c, round 14 (the pipeliner reads the stream alone; the window's recogniser and the counted loop's body off the lines), 2026-11-14, lane m6w

- **The order, written first** (each a commit of identical output, the census at 0): (P1) `pipelineLoops(MStream&)` without
  `linesOf` - `Src` on a stream holds no lines, its liveness the stream's (`liveness`), the names read from the entries;
  a reader that still wants text asks `Src::line` for one entry, and `c6x-line` names it; (W1) the window's recogniser
  on numbers - `Window`'s predicate, exits and pointers, `trackPointer`; (C1) the counted loop's body and preheader
  on the stream as entries alone; (C2) `steppedAt` a number; (W2) `WinCode::Op` an entry whose register is a number,
  a virtual one above 63, so `giveRegisters` maps numbers; (W3) `emitClosed`/`emitWindow` reading `Window::bodyM`
  (`Aff`, `value`, `renamedBy`, `byteOperand` on operands); (W4) their output through `Code::add`, so
  `Code::push_back`/`entryOf` go; (C3) the algebra's terms keyed by number (`evaluateBody`'s sums are still strings).
- **Landed** (from m6-v 4039ef42), four commits, each of identical output: **P1** (53ac6e53) the pipeliner on the
  stream makes no vector of lines: `Src` reads the entries, the block liveness is `liveness(s)` (`liveIn`, `liveAt`),
  labels and named operands are collected from the entries, `pipeline`, `Code::same` and `pipelineAt` take `Src`;
  `Src::line(i, who)` makes the one line a reader asks (`viewOf`, exported from C6xPack.cpp) and, under
  `CXX1_C6X_MIR=1`, `c6x-line: who` counts each entry so read; the census's text recognisers read `Src::text()`
  (`linesOf`, census only). **W1** (97313c69) `Window::PR`/`backNot`, `Exit::pred`/`no`, `Ptr::reg` are numbers;
  `recogniseWindow` compares predicates by number (`Src::predicated`, `predNot`), reads `Src::regs`, and
  `trackPointer`, `Src::regsIn` and `Src::byteAddress` are keyed by number; the emitters spell a name from the number
  (`Exit::name`, `regName`). A predicate that names no register is refused by name. **C1** (b88b8295) `Loop` on the
  stream keeps `bodyM` and `preM` only: `renameWebs`, `foldBodyCopies`, `wantedAfter`, the moved bound, the nodes,
  the units and the traces read `Loop::size`/`mnem`/`ops`/`op`/`raw`; a line stands beside an entry only where an
  entry of the body or the preheader is not modelled, or under the census (which holds entries against lines as
  before: `c6x-body`, `c6x-addr`, `c6x-spell`, `c6x-recog`). **C2** (39b8e820) `steppedR`, a number; `steppedAt` deleted.
  src/ +256 -183 lines (C6xPipe.cpp 2,554 -> 2,622, C6xPack.cpp 1,492 -> 1,496, C6xMir.h 128 -> 129).
- **`c6x-line`, the number to bring to 0** (entries of a selected function read as a line by the pipeliner, -O2 over
  tests/cases): at P1 10,882 on Windows - window 7,977, pre 2,352, body 553; after C1 8,211 on Windows and 8,206 on
  Linux, **all `window`** (`recogniseWindow` keeping `W.body` for every candidate it reads, taken or not). `untyped`
  (an entry `modelled` refuses) and `same` are 0: no selected function's pipeliner reads an entry it cannot model.
- **What still reads `Line` in the C6000 path** - M6c's deletion list. *The pipeliner* (C6xPipe.cpp): the window whole
  past its recogniser - `Window::body`, `WinCode::Op` (spellings, `%N` a virtual register), `giveRegisters`,
  `affineTurn`'s `Aff`, `emitClosed`, `emitWindow`, their `rebuilt` branches, and `Code::push_back`/`entryOf` that
  read each composed line back as an entry; in the counted loop only what stands for the text - `Loop::body`/`pre`,
  `insOf(const Line &)`, `spelledLine`, `Src`'s `v` and its `line` fallbacks, `Code::lines`/`text()`, `legalForm` in
  `Code::legal`, `namesOf` and `pipelineLoops(std::vector<Line> &)` - reached by a walker's function or the census,
  never by a selected one; the algebra's terms are strings but no lines. *The packetiser's file* (C6xPack.cpp):
  `lineOf`/`viewOf` wherever an entry is not `modelled` (`makeNode`, `liveFacts`, `crossingsOf`, `unitsOf`,
  `readsAndWrites`), `Folding::v` (the folds of `rounds`, `numberValues` and `foldAddressing` read a view of each entry and edit
  the entry), `LoopStream` (the loop passes are C6xSched's text passes over `LoopText`,
  each edit read back by `streamOf`), `streamOf`, `linesOf`, `checkLiveness`. *The driver* (`c6xSchedule`,
  C6xSched.cpp): a selected function still arrives as text - parsed to lines, `streamOf` to enter the stream - and
  `linesOf(after, lines)` after `foldAddressing` only to hand the text pass a fallback; every `CXX1_C6X_TEXT*` knob
  and the census's reference runs. *The table* (C6xSched.cpp, C6xModel.h): `Node::line`, `makeNode`,
  `readsAndWrites`, `unitsFor`, `crossings`, `legalForm`, `steppedRegister`, `parse`, `rebuilt` and the text passes
  themselves, which the walker's functions keep until M6d. C6xMir.cpp: `legal` on an entry not modelled, and `read` (a
  helper's text parsed onto the stream).
- **Gate** (Linux ~/m6w, a copy of ~/m6v with the changed files laid over; REF main 85fe99fe's compiler in its kept
  tree ~/w7ref; Windows against work\m6v's cxx3-msvc.exe). **Each step on its own** on Windows, its own tree built by
  cl /W4 /WX clean: tests/cases at -O0, -O1, -O2 by md5, 3,669 outputs - P1 (work\m6w) 2,601 texts, 1,068 refused
  alike, **0 differ**; W1 (work\m6w1) and C1 (work\m6w2) 3,663 same and the 6 round 13 named (posix-headers and
  windows-h-layout, refused, the caret indented by the tree's longer name); C2, the final source (39b8e820), again in
  work\m6w: **0 differ**. Census at each step 0 `c6x-recog`, 0 `c6x-emit`, 0 `c6x-addr`, 0 `c6x-spell`, 0
  `c6x-loop-edit`, 0 `c6x-loops`, 0 `c6x-loop-unread`, 0 `c6x-front`, 0 `c6x-fold`, 0 `c6x-fold-edit`, 0 `c6x-live`, 0
  `c6x-live-unread`, 0 to the text pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed
  entries). P1, W1 and C1 were not built alone on Linux (the lock was the coordinator's). On the final source on
  Linux: g++ -Werror clean; `identical.sh ARCHS=tms6747` against main's compiler, -O0 and -O2 1,736 compared, 0
  differ; -O0 -g 868 compared, 91 raw (`/w7ref/` against `/m6w/`), 851 texts 0 differ with the tree path made the same; census
  0 as above and 0 `c6x-body`, 0 `c6x-loop-lines`. tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2: 814 passed, 0
  failed, 3 not run on vm6747; **192,009,106 cycles** (192.0 M, unchanged). tests/g-same.sh 867 same, 0 differ, 0 broken; asm6x.sh 833
  objects, 0 failed, at -O0 and -O2. cl /W4 /WX clean (four builds). Exclusions 129 sites 0
  uncited 0 stale (SSA lowering 554/95); comments 0 over; memory gate rc 0; ast-residue rc 0; ide/generate.py --check
  up to date (both boxes). Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** W2-W4 above - the window's emitters on entries, which is what `c6x-line` still counts and the last text
  the pipeliner composes; C3; then `Src` without `v`, the loop passes composing entries (`LoopStream`), the selector's
  stream handed in unprinted (the driver's parse and `streamOf`), step 4.

### Wave 10 landed, 2026-11-14 (the coordinator)

M6c round 13 (m6v: S5-S8 of the register plan - the emitters' sink, webs, units, the pool, the address walk and the
recognisers on register numbers; `iv`, `bound`, `P`, `C`, `ivs`, `bounds`, `steps` deleted).
- **Gate at 4039ef42** (6ad62714 merged with main 3d292471): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0, exclusions 0/0, comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0
  (393.2 M cycles) and -O2 (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.

### M6c, round 15 (the window's emitters on entries; the stepped counter's algebra on numbered terms), 2026-11-14, lane m6x

- **Landed** (from m6-w 7823b524), each of identical output. **W2-W4** (041e0247, one commit: the three are one
  representation) - `WinCode::Op` holds operands as an entry's (`Arg`: the `MOperand` and, beside it, its register and
  index as numbers, a virtual one `kVirt` = 256 and up, since an operand's byte cannot count the window's virtuals);
  `Window::bodyM` replaces `Window::body` (`Src::entry`: the stream's own entry, a walker's line as the entry of its
  parts; one an entry does not model is refused by name); `affineTurn`'s `Aff`, `emitClosed` and `emitWindow` read
  operands (`renamedBy`, `byteAt`, `indexed`, `byteAddress` on an operand - the one `Src::byteAddress` now asks);
  `committed`, `cur`, `step`, the pool and the predicates are numbers, walked in the names' order (`RegSet`);
  `giveRegisters` maps numbers and makes each entry, `legalForm`'s commute and unit test on the entry; the emitters
  hand back a stream that `Code::put` takes. `Code::push_back`, `Code::entryOf`, `renamedBy`/`byteOperand` on
  spellings, `sideOfAny` and `Exit::name` are deleted; `Code::same` copies the stream's entry. **C3** (559a6142,
  comment mended 91c449f7) - `Terms`: each term of the stepped counter's algebra made once in a table (entry value,
  symbol, constant, `op`, flat sorted sum, lt, ltu, eq, and, or), equal where the numbers are, `contains` for what
  `find` asked of a spelling; `sumOf`, `splitTop`, `joinSorted` deleted, `str` spells a term for the trace alone.
  One known narrowing: `contains` asks for the bound as a term, where `find("s:A1")` also met `s:A10`.
  src/ +494 -437 lines (C6xPipe.cpp 2,622 -> 2,679).
- **`c6x-line` is 0** on both boxes (8,211 / 8,206 at round 14, all `window`): no selected function's pipeliner reads
  an entry as a line.
- **What still reads `Line` in the C6000 path.** *The pipeliner* (C6xPipe.cpp), none of it for a selected function
  outside the census: `Src`'s `v`, `line` (the `untyped`, `body`, `pre` fallbacks), `text()` and `entry` on a
  walker's line; `insOf(const Line &)`; `Loop::body`/`pre` and what stands beside them (`raw`, `spelledLine`, the
  line branches of `renameWebs` and the moved bound, `checkBody`); `Code::lines`/`text()`, `legalForm` in
  `Code::legal`, `Code::placed` where a turn has no entry, `Turn::raw`; `sameLines`, `namesOf`,
  `pipelineLoops(std::vector<Line> &)`. Reached on the stream too: `lineOf` for the two extra nodes of `scheduleAt`
  (`makeNode(lineOf(entry(...)))`), for `turnOf`'s `raw` and for a refusal's reason. *The packetiser's file, the
  driver, the table, C6xMir.cpp*: as round 14 listed them, unchanged.
- **Gate** (Linux ~/m6x, a copy of ~/m6w with the changed files laid over; REF main 85fe99fe's compiler in ~/w7ref;
  Windows against work\m6w's cxx3-msvc.exe). **Each step on its own**, cl /W4 /WX clean (three builds): tests/cases
  at -O0, -O1, -O2 by md5, 3,669 outputs - W2-W4 (work\m6x) 2,601 texts, 1,068 refused alike, **0 differ**; C3
  (work\m6x1) 3,663 same and the 6 path-length carets round 13 named; the final source (91c449f7) again in work\m6x
  **0 differ**. Census at each step: `c6x-line` 0, and 0 `c6x-recog`, `c6x-emit`, `c6x-addr`, `c6x-spell`, `c6x-reg`,
  `c6x-loop-edit`, `c6x-loops`, `c6x-loop-unread`, `c6x-front`, `c6x-fold`, `c6x-fold-edit`, `c6x-live`,
  `c6x-live-unread`, 0 to the text pipeliner or pass; 6,835 selected functions, 106 with kernels (2,727 placed
  entries). Linux, g++ -Werror clean at each: W2-W4 alone census 0, `identical.sh ARCHS=tms6747` -O0 and -O2 1,736
  compared 0 differ, static rc 0; C3 (559a6142) the same 1,736 / 0, -O0 -g 868 compared, 91 raw (the tree's path),
  851 texts 0 differ with the path made the same; tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 814 passed, 0
  failed, 3 not run on vm6747, **192,009,106 cycles** (192.0 M, unchanged); g-same 867 same, 0 differ, 0 broken;
  asm6x.sh 833 objects, 0 failed, at -O0 and -O2. The final source (C3 and a comment) on Linux: build clean,
  identical 1,736 / 0, exclusions 129 sites 0 uncited 0 stale (SSA lowering 554/95), comments 0 over (C3 alone had
  1: the algebra's old two lines left above the new), memory gate rc 0, ast-residue rc 0, ide/generate.py --check up
  to date (both boxes). Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** the lines that stand for a walker's function only (`Loop::body`/`pre`, `Src::v`, `Code::lines`, the text
  `pipelineLoops`) go with the walker's path in M6d; before that `makeNode` on the two extra entries and `Turn`
  without `raw`; then the loop passes composing entries (`LoopStream`), the selector's stream handed in unprinted
  (the driver's parse and `streamOf`), step 4.
### Wave 11 landed, 2026-11-14 (the coordinator)

M6c round 14 (m6w: P1 `pipelineLoops(MStream&)` without `linesOf`, W1 the window's recogniser on numbers, C1 the
counted loop's body and preheader as entries alone, C2 `steppedAt` a number; census `c6x-line` 10,882 -> ~8,200,
all the window's).
- **Gate at 7823b524** (eae6a9c9 merged with main c504e20a): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0, exclusions 0/0, comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0
  (393.2 M cycles) and -O2 (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.
### M6c, round 16 (who still reaches the text passes, measured; the deletion's plan; its first three steps), 2026-11-14, lane m6y

- **Measured** (from m6-x 32d47563, its compiler in work\m6x; tests/cases, 868 files). The selector takes **6,835 of
  10,984 functions at -O2 (62.2%)**; **4,149 are walked, in 862 cases** (-O0: 6,988 selected, 3,996 walked). By the
  selector's reason (`CXX1_SSA_WHY`, -O2): landing pads 1,312; variadic call 860; class result 399; class or 64-bit
  parameter 398; floating value 243; `memcpy` 209; floating or 64-bit result 174; indirect call 140; 64-bit value 124;
  `memset` 101; class result of a call 60; class argument 36; exception specification 31; outside the SSA lowering
  24; `smulh` 20; variadic 16; aggregate value 2. M6a.2's "census 0 walked" is not reached, and M6d waits on it.
  Before this round the text passes were reached by: (a) those 4,149 walked functions at -O1 and -O2 (at -O0
  `c6xSchedule` returns its text), every one by `c6xSchedule(..., selected = false)`; (b) a selected function whose
  text the stream does not spell - **0** (`c6x-pack`, `c6x-pipe`, `c6x-fold` 0 of 6,835); (c) the knobs
  `CXX1_C6X_TEXTFOLD`, `TEXTPIPE`, `TEXTPACK` (no gate runs them) and `CXX1_C6X_WALKER=1` (all 10,984 walked; the
  wave gates' script has `wtms0`/`wtms2` legs, and waves 11 and 12 did not run them); (d) the census copies under
  `CXX1_C6X_MIR=1` (the text run beside the stream to name a difference: `c6x-front`, `-loops`, `-live`, `-recog`,
  `-emit`, `-body`, `-addr`, `-spell`, `-node`, `-line`, each 0 since round 15).
- **What the deletion would cost, measured.** Nothing in output: with every function's text sent through the stream
  passes (the walker's text read by `streamOf` as the selector's is), tests/cases at -O0, -O1 and -O2 is **2,601
  texts, 0 differ** against m6-x's compiler (the 6 named are the refused cases' carets, the tree's longer name), and
  **0** functions go to the text pass, the text pipeliner or the text folds; kernels on the stream go from 106
  functions (2,727 placed entries) to 219 (9,122) - the walker's loops, pipelined on entries to the same text. So no
  case's output or cycles change, and the walker legs keep their text.
- **The plan, each step of identical output.** **D1** the three `TEXT*` knobs and `c6xSchedule`'s census copies
  (`liveCensus`, `checkLiveness`). **D2** the census copies in C6xPipe.cpp and C6xPack.cpp (`checkRecognisers`,
  `sameLoop`, `sameWindow`, `sameLines`, `checkEmitted`, `checkBody`, `Src::text`, `Code::text`, `sameFacts`, the
  `c6x-addr`, `-spell`, `-node`, `-fold-edit`, `-loop-edit` comparisons). **D3** `c6xSchedule` without `selected`:
  every function is a stream where it spells its text; the text passes stand only behind a line the stream does not
  spell. **D4** the text-only passes deleted - the front (`foldCalls`, `foldMvk`, `foldFrame`, `foldPushPop`,
  `threadPredicates`, `dropUnnamedLabels`, `predicateArms`, `rotateLoops`, `tidyJumps`), the five folds and
  `rounds(lines)`, `numberValues(lines)`, `foldAddressing(lines)` with `foldScaledIndex`, `schedule` and the text
  `foldBranchNops`, the text `pipelineLoops` with `Src::v`, `Loop::body`/`pre`, `spelledLine`, `Code::lines`,
  `namesOf` (about 840 lines of C6xSched.cpp by their bodies alone, and the pipeliner's line halves); a line the
  stream does not spell is then refused by name (an internal error), which D3's census shows is 0. **D5** the loop
  passes compose entries (`LoopText`/`LoopStream` gone; five passes, 526 lines, shared today through views). **D6**
  the table's readers on operands (`readsAndWrites`, `unitsFor`, `crossings`, `makeNode`), the selector's stream
  handed in unprinted; `Line`, `lineOf`/`viewOf`/`linesOf` deleted. The walker's text is still read (`parse`,
  `streamOf`) until M6d removes the walker.
- **Landed: D1** (7ae77575), **D2** (2366076d, mended 23e061ae - the first did not compile), **D3** (3f29eef7).
  src/ +75 -237 lines (C6xSched.cpp 2,451 -> 2,423, C6xPipe.cpp 2,679 -> 2,578, C6xPack.cpp 1,496 -> 1,465).
- **Gate.** Windows, cl /W4 /WX clean at each (work\m6y D1, m6y3 D2, m6y4 D3), against work\m6x's cxx3-msvc.exe,
  tests/cases at -O0, -O1, -O2 by md5: D1 2,601 texts, 1,068 refused alike, **0 differ**, and the same under
  `CXX1_C6X_WALKER=1`; D2 and D3 3,663 same and the 6 path-length carets round 13 named, plain and under
  `CXX1_C6X_WALKER=1` alike; census at each 0 to the text pass, the text pipeliner and the text folds, 6,835
  selected; kernels on the stream 106 functions / 2,727 entries at D1 and D2, **219 / 9,122 at D3**; exclusions 129
  sites 0 uncited 0 stale, comments 0 over, memory gate rc 0, ide/generate.py --check up to date at each. Linux
  (~/m6y, a copy of ~/m6x with the changed files laid over; REF main 85fe99fe's compiler in ~/w7ref), g++ -Werror
  clean, **D2 and D3 each**: `identical.sh ARCHS=tms6747` -O0 and -O2 1,736 compared, 0 differ; -O0 -g 868 compared,
  91 raw (the tree's path), 851 texts 0 differ with the path made the same; tests/tms6747.sh SHARD=k/3 CYCLES=1
  SIM=0 at -O2 814 passed, 0 failed, 3 not run on vm6747, **192,009,106 cycles** (192.0 M, unchanged); g-same 867
  same, 0 differ, 0 broken; exclusions 0 uncited 0 stale (SSA lowering 554/95), comments 0 over, memory gate rc 0,
  ast-residue rc 0, generate up to date. asm6x.sh at D3: 833 objects, 0 failed, at -O0 and -O2 (not run at D2). Not run (the
  coordinator's): winpass, make test, ssa.sh.
- **Owed:** D4 next (one commit of deletions, the suite's census 0 its licence; the fallback's five sites become one
  refusal), then D5, D6. The census tags left name fallbacks, not copies: `c6x-pack`, `c6x-pipe`, `c6x-fold`,
  `c6x-fold-edit` unread, `c6x-loop-unread`. M6a.2's rest - the 4,149 walked functions above, landing pads first -
  is what M6d waits on; it no longer holds the text passes.
- **Questions for the user.** (1) `CXX1_C6X_WALKER=1` and the gate script's `wtms0`/`wtms2` legs: after D3 the knob
  no longer reaches a text pass (the walker's text takes the stream's, 0 differ under the knob), so D4 does not need
  it gone. Kept until M6d removes the walker (recommended: it is the only all-walker identity while 4,149 functions
  are walked), or retired now? (2) D4 turns "a line the stream does not spell" from a silent fall to the text pass
  into an internal error by name: 0 over tests/cases, Compiler++ and the CCS samples not measured here. Measure those
  two corpora first (recommended), or delete on the suite's 0?

### Wave 12 landed, 2026-11-14 (the coordinator)

M6c round 15 (m6x: W2-W4 the window's emitters on entries with numbered and virtual registers, `Code::push_back` and
`entryOf` deleted; C3 the stepped counter's algebra on numbered terms; census `c6x-line` **0** for selected functions).
- **Gate at 32d47563** (16f1f14a merged with main 8dc65fbb): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0, exclusions 0/0, comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0
  (393.2 M cycles) and -O2 (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.

### M6c, round 17 (the wider measure: 0; D4 - the text-only passes deleted, the fallback a refusal by name), 2026-11-14, lane m6z

- **The coordinator's answers to round 16's questions:** `CXX1_C6X_WALKER=1` and the `wtms0`/`wtms2` legs stay until
  M6d (the wave gate runs them again from wave 13); measure before D4.
- **The wider measure** (D3's compiler, m6-y 3f29eef7's code: Windows work\m6y4's `cxx3-msvc.exe` and Linux
  `~/m6y/cxx3.exe`, both; boxkit/m6z-measure.sh, m6z-wmeasure.sh). Compiler++ - its 16 files one by one and the
  amalgamated one (`dist/compilerpp_amalgamated.cpp`, 202,617 lines of assembly at -O2) - and the CCS samples (Sample
  and SampleExt, `Math.cpp` with their headers; SampleExt1 is not in RIDE 4.7's tree) through `-arch tms6747` at -O0,
  -O1 and -O2 under `CXX1_C6X_MIR=1`: **57 texts, every one rc 0, 1,732,389 lines, 8,665 selected functions, 52
  functions with kernels on the stream; lines the stream does not spell: 0** (`c6x-fold`, `c6x-pipe`, `c6x-pack`,
  `c6x-fold-edit` unread, `c6x-loop-unread`: 0 each, on both boxes). No shape to name; the stream needed nothing.
- **D4 landed** (e40885ee, 9fcb7703, comments c1ff2b3f). C6xSched.cpp: the text front (`foldCalls`, `foldMvk`,
  `foldFrame`, `foldPushPop` with the text `isPush`/`isPop`, `threadPredicates`, `dropUnnamedLabels`, `predicateArms`,
  `rotateLoops`, `tidyJumps`), the five text folds and `rounds(lines)`, `numberValues(lines)`, `foldAddressing(lines)`
  with `foldScaledIndex`, `schedule`, the text `foldBranchNops`, the text `deadAfter`/`deadAfterAsComputed`,
  `LivenessHeld` (nothing opened one) and `isReturn(Line)` are gone; `c6xSchedule` is one path - `streamOf`, the
  stream passes, `write` - and `rounds` takes the stream alone. C6xPipe.cpp: the text `pipelineLoops` and `namesOf`,
  `Src::v` and every `!s` half of `Src`, the walker's entry made of a line's parts, `Code::lines` and the `!from`
  halves of `put`/`legal`/`placed`/`same`, `Loop::onStream`, `pipeline`'s `src`. C6xPack.cpp: `linesOf`.
  **The fallback is a refusal by name**: `c6xSchedule(text, level, spelled)` hands the text back with `spelled`
  false where `streamOf` or the stream pipeliner cannot hold a line, and `Tms6747` says "codegen: a line the
  scheduler's stream does not spell is not supported yet by the tms6747 backend" and fails the compile. No case
  reaches it (0 over tests/cases, Compiler++ and the CCS samples), so none is recorded. `CPP11_C6XLIVECHECK` now
  checks nothing (its check was the text `deadAfter`'s; tests/tms6747.sh still sets it - to retire with D5).
- **Not landed: the rest of the pipeliner's line halves, and D5.** `Loop::body`/`pre`, `spelledLine` and the
  `typed(i) ? ... : line(i)` halves of `Src` are not text-only: they are the path of an entry the model does not
  hold (a placed or verbatim one - "the whole body from its lines"), so they go with D6's readers on operands, not
  with D4. D5 (the five loop passes composing entries, `LoopText`/`LoopStream` gone) was not begun: `LoopText` is
  now built only as a `LoopStream`, its `lines` constructor and the liveness cache behind `liveInAt`/`liveOutAt`
  (`ensureLiveness`, `computeLiveness`) are what D5 removes.
- src/ +78 -1,216 lines (C6xSched.cpp 2,423 -> 1,356, C6xPipe.cpp 2,578 -> 2,528, C6xPack.cpp 1,465 -> 1,452).
- **Gate.** Windows, cl /W4 /WX clean at each of e40885ee (work\m6z), 9fcb7703 (m6z2) and the final code f47c410f
  (m6z3), against D3's compiler (work\m6y4), tests/cases at -O0, -O1, -O2 by md5: **2,601 texts same, 1,068 refused
  alike, 0 differ**, plain and under `CXX1_C6X_WALKER=1` (at work\m6z the 6 path-length carets of round 13, the
  tree's shorter name); census 0 to the text pass, pipeliner and folds, 6,835 selected, kernels 219 functions /
  9,122 entries; the wider corpus D3 against D4 (m6z-wsame.sh): **57 compared, 0 differ, none refused**; exclusions
  129 sites 0 uncited 0 stale, comments 0 over, memory gate rc 0, generate up to date (f47c410f). Linux (~/m6z, a
  copy of ~/m6y with the changed files laid over; REF main 85fe99fe's compiler in ~/w7ref), g++ -Werror clean, at
  c1ff2b3f (f47c410f changes one comment in C6xSched.h after it): `identical.sh ARCHS=tms6747` -O0 and -O2 **1,736
  compared, 0 differ**; -O0 -g 868 compared, 91 raw (the tree's path), **851 texts 0 differ** with the path made the
  same; tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 **814 passed, 0 failed, 192,009,106 cycles** (192.0 M,
  unchanged); g-same 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed, at -O0 and -O2; exclusions,
  memory gate, ast-residue, generate rc 0 (comments 1 over at c1ff2b3f, C6xSched.h:9, mended in f47c410f).
  On the final tree f47c410f (Linux): g++ clean, `identical.sh` -O0 and -O2 1,736 compared, 0 differ again; exclusions,
  comments (0 over), memory gate, ast-residue, generate rc 0; the walker legs `CXX1_C6X_WALKER=1` at -O2, three
  shards: **812 passed, 2 failed** - `static-reference-scalar-temporary` and `static-reference-member-scalar-temporary`
  (252 printed), the two wave 13's gate found on main 85fe99fe's compiler as well, so not this round's; no other.
  Not run on the final tree after the one-comment change: tms2, -g, g-same, asm6x (their numbers are c1ff2b3f's).
  Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** D5 (the loop passes compose entries), then D6 with the rest of the pipeliner's line halves; retire
  `CPP11_C6XLIVECHECK` from tests/tms6747.sh; the two walker-only -O2 wrong answers above (older than this branch).
### Wave 13 landed, 2026-11-14 (the coordinator)

M6c round 16 (m6y: the reach measured - 6,835 of 10,984 functions selected, 0 of them to a text pass; D1-D3 of the
deletion plan: the TEXTFOLD/TEXTPIPE/TEXTPACK knobs and the census copies removed, `selected` dropped from
`c6xSchedule`; src/ +75 -237). The plan's two questions taken as recommended: the walker knob and its legs stay
until M6d, and Compiler++ and the CCS samples are measured before D4 (lane m6z).
- **Gate at eeaa4fd0** (621507cd merged with main 0911f14e): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0 (393.2 M) and -O2 (**192.0 M**,
  unchanged); asm6x 833 / 0 at -O0 and -O2.
- **The walker legs, back in the wave gate**: `CXX1_C6X_WALKER=1` -O0 814 passed, 0 failed (447.8 M cycles); -O2
  812 passed, **2 failed** - static-reference-scalar-temporary prints 252 for 5, static-reference-member-scalar-
  temporary 252 for 7. Main 85fe99fe's compiler gives the same 252 on the walker and the right answers on the
  selector: a walker-only wrong answer that predates this wave, unseen since the legs left the gate after wave 3.
  **Owed** before M6d retires the walker, or with it: fix, or a by-name walker skip with its reason.

### M6c, round 18 (the walker's two wrong answers, the dead live check, D5's first step), 2026-11-14, lane m6aa

- **The walker's two -O2 wrong answers: a parser root, not an -O2 pass.** The walker's text for `const int &r = 5;`
  (and `const int &S::cr = 7;`) stored the 5 in the init function's frame (`STW A4, *-A15(44)`, then `r = A15 - 44`)
  at -O0 as well: `staticTemporary` made `_ZGR1r_` and its `.bss`, but `bindStaticReference` still handed the init to
  `bindReference`, whose temporary is a frame slot - the selector's lowering reads `g.temporary` and was right. -O0
  passed by luck (main's frame never reached the dead slot); -O2's smaller frames reached it and printed 252.
  **Fixed** in the parser: `bindStaticReference` hands `bindReference` the static object's name, and its last branch
  copies a scalar into that object (`(T = init, &T)`) instead of a frame slot (a `thread_local`'s not yet). Not
  changed: the class temporaries of tests/open/walker-static-reference-temporary.cpp and
  static-reference-to-temporary's `.notarget` (the walker still binds those to a frame slot; the selector leaves them
  to it - M6), and nothing the selector reads (its text at every level the same).
- **`CPP11_C6XLIVECHECK` retired** (the smaller of the two): nothing in src/ read it since round 17; tests/tms6747.sh's
  setting, tests/tms6747-nolivecheck.txt (the two cases it excused, and their 30 s limit) and README's mention gone.
- **D5, first step**: `LoopText` is one struct, the stream's (`LoopStream` folded into it, no virtuals): the text
  `LoopText` (`own`, the lines constructor, the text `apply`/`compact`/`blank`/`set`), the liveness cache behind it
  (`liveInAt`, `liveOutAt`, `ensureLiveness`, `LiveStamp`, `textEdits` and `Line`'s counting copies),
  `computeLiveness` and `Line`'s `liveIn`/`liveOut` are gone; a blank pending is answered from the lines' `liveFacts`
  as before. **Not landed**: the five passes still read `t.v` as `Line`s (`blank`/`set`/`apply` keep the entries in
  step); composing entries is D5's rest, with D6.
- src/ +92 -153 lines at the final b9818dd2 (C6xSched.cpp 1,356 -> 1,308, C6xPack.cpp 1,452 -> 1,447, C6xModel.h
  155 -> 143, ParserInit.cpp 1,910 -> 1,913, ParserExpr.cpp +5 -4, Parser.h +1 -1). 17f209c8's helper of its own
  (`staticScalarTemporary`) raised the Ast residue (ExprPtr 2,016 -> 2,024) and was folded into `bindReference`.
- **Gate.** Windows, cl /W4 /WX clean at 64416d2f (work\m6aa2) and the final 17f209c8 (m6aa3), against round 17's
  compiler (work\m6z3), tests/cases at -O0, -O1, -O2 by md5: plain 2,598 texts same, 1,062 refused alike, 9 differ -
  the 6 path-length carets and static-reference-to-temporary's three (its walked init now stores its scalars in
  `_ZGR`; the case is `.notarget` for tms6747); under `CXX1_C6X_WALKER=1` 15 differ - the carets and the three
  static-reference cases at each level, the fix; census -O2 0 to any text pass, 6,835 selected, kernels 219 / 9,122;
  exclusions 129 sites 0 uncited 0 stale, comments 0 over, memory gate rc 0 (1,056 at 64416d2f, mended by
  `make_owned` in 17f209c8), generate up to date. Linux (~/m6aa, ~/m6z with the changed files laid over; REF main
  85fe99fe's compiler in ~/w7ref), g++ -Werror clean at 64416d2f: `identical.sh ARCHS=tms6747` -O0 and -O2 1,736
  compared, 2 differ (static-reference-to-temporary, as above); -O0 -g 868 compared, 851 normalised, 1 differ (the
  same case); tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 **814 passed, 0 failed, 192,009,106 cycles**
  (unchanged); **walker legs** `CXX1_C6X_WALKER=1` at -O2 **814 passed, 0 failed** (round 17: 812/2);
  g-same 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed at -O0 and -O2; walker legs at -O0 **814
  passed, 0 failed** (447.8 M cycles). On the final tree b9818dd2 (1139c136 re-points one citation): Windows
  (work\m6aa4) cl clean, walker same as 17f209c8's (the carets and the three static-reference cases), comments 0
  over, memory gate rc 0, generate up to date; Linux g++ clean, exclusions 0 uncited 0 stale (after the
  re-pointing), comments 0, memory gate, ast-residue and generate rc 0, `identical.sh` -O0 and -O2 1,736 compared, the same
  2 differ. The other legs above are 64416d2f's compiler:
  b9818dd2 builds the same Ast by a different route (not re-run there: tms2, walker legs, -g, g-same, asm6x).
  Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** D5's rest (the five passes composing entries, `t.v` gone), then D6; the walker's class temporaries of a
  static reference (tests/open/walker-static-reference-temporary.cpp) and a `thread_local` one's scalar, with M6d.
### Wave 14 landed, 2026-11-14 (the coordinator)

M6c round 17 (m6z: Compiler++ and the CCS samples measured, 57 texts with 0 lines the stream does not spell; D4 -
C6xSched.cpp's text-only passes deleted, 2,423 -> 1,356 lines, src/ +78 -1,216; the fallback a refusal by name).
- **Gate at a2b329d8** (1931b1ad merged with main d556f3ff): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0 (393.2 M) and -O2 (**192.0 M**,
  unchanged); walker -O0 814 / 0 (447.8 M), walker -O2 812 / 2 - the same two owed since wave 13, no new one;
  asm6x 833 / 0 at -O0 and -O2.

### M6c, round 19 (D5 and D6 planned step by step; the first step of each), 2026-11-14, lane m6ab

- **What the two steps are made of, measured** (m6-aa 1d5526c0). `Line` is named 101 times in three files:
  C6xSched.cpp 55 (the table's readers and the five loop passes), C6xPack.cpp 35 (`lineOf`, `viewOf`, `Folding::v` -
  the five folds read lines too, which the deletion plan did not list - `LoopText`, `streamOf`), C6xPipe.cpp 11
  (`insOf`, `Src::line`, `Loop::body`/`pre`, `spelledLine`). The loop passes are 549 lines of reads by spelling
  (`ops[o].find("++")`, `renamed`, `rebuilt`), not 5 loops over a view: each is its own rewrite.
- **The step order, each of identical output.** **D5.1** `LoopText` reads entries (`instr`, `label`, `rw`: a blanked
  entry no instruction, registers from operands where `modelled`); `reuseEntryLoads` on them. **D5.2**
  `hoistInvariants`, **D5.3** `inductionPointers`, **D5.4** `widenFills`: each composes its preheader and its edits
  as entries (`set(k, MInst)`, `apply(h, MStream, i)`), legality by `legal(MInst)`. **D5.5** `hoistConstantPairs`,
  the one pass that meets placed entries (a `||` line is a label to `isLabel`: kept so by `label`). **D5.6**
  `LoopText::v`, `set(Line)`, `edited`, `entry` and `c6x-loop-unread` gone; a pending blank's liveness from the
  entries. **D6.1** `Node::line`. **D6.2** `Folding::v` (the five folds read `s[k]`; `c6x-fold-edit` gone).
  **D6.3** the pipeliner: `Loop::body`/`pre`, `spelledLine`, `Src::line`/`made`, `insOf(Line)`, its `lineOf` - a
  placed entry read by its operands with its mark. **D6.4** the table on operands only: `makeNode(Line)`,
  `readsAndWrites(Line)`, `unitsFor(Line)`, `crossings(Line)`, `legalForm`, `rebuilt`, `make`, C6xPack's
  `lineOf`/`viewOf` and the `!modelled` halves (an operand held as text naming a register is then refused by name).
  **D6.5** `c6xSchedule` reads the walker's text with `read` alone; `parse`, `streamOf` and `Line` deleted.
- **Landed: D5.1** (547ad9c6) and **D6.1** (ec361783): `reuseEntryLoads` no longer reads `t.v`; `Node` holds
  `callp` in place of a `Line` (the scheduler asked it one question, twice). **Not landed**: D5.2-D5.6, D6.2-D6.5 -
  the round was briefed for 35 minutes and the four larger passes are a rewrite each.
- **What still uses `Line`, and why.** The four loop passes above (`t.v`); the five folds (`Folding::v`); the
  pipeliner's line halves (an entry the model does not hold - placed or verbatim); the table's `Line` readers
  (behind `!modelled` entries, and the walker's text through `parse`/`streamOf`). The walker itself needs none of
  it: it hands text to `c6xSchedule`, so after D6.5 `read` is all it keeps until M6d.
- src/ +42 -29 lines (C6xSched.cpp 1,308 -> 1,315, C6xPack.cpp 1,447 -> 1,450, C6xModel.h 143 -> 146): D5.1's
  readers come before the lines they will delete.
- **Gate.** Windows, cl /W4 /WX clean at 547ad9c6 (work\m6ab) and the final code 4c9343d0 (work\m6ab2), against
  round 18's compiler (work\m6aa4), tests/cases at -O0, -O1, -O2 by md5: **2,601 texts same, 1,068 refused alike, 0
  differ**, plain and under `CXX1_C6X_WALKER=1` (static-reference-to-temporary among them, against round 18's own
  build); census -O2 0 to any text pass, 0 `c6x-loop-unread`, 6,835 selected, kernels 219 / 9,122; exclusions 129
  sites 0 uncited 0 stale, comments 0 over (1 over at 547ad9c6, mended), memory gate rc 0, generate up to date.
  Linux (~/m6ab, ~/m6aa with the three files laid over; REF main 85fe99fe's compiler in ~/w7ref), g++ -Werror clean,
  at ec361783's code: `identical.sh ARCHS=tms6747` -O0 and -O2 1,736 compared, 2 differ (static-reference-to-temporary,
  round 18's fix); -O0 -g 868 compared, 851 normalised, 1 differ (the same case); tests/tms6747.sh SHARD=k/3
  CYCLES=1 SIM=0 at -O2 **814 passed, 0 failed, 192,009,106 cycles** (unchanged); walker legs at -O2 **814 / 0**;
  g-same 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed at -O0 and -O2; census 0. On the final file
  (4c9343d0, a function moved): g++ clean, exclusions 0/0, comments 0 over, memory gate, ast-residue, generate rc 0,
  `identical.sh` the same 1,736 / 2; walker legs at -O0 **814 / 0** (447,770,677 cycles). Not run (the
  coordinator's): winpass, make test, ssa.sh.
- **Owed:** D5.2-D5.6, then D6.2-D6.5, in the order above - a round each for the loop passes' three large ones is the
  honest size; the round ran past its hour waiting on a dropped ssh, with no job left running.
### Wave 15 landed, 2026-11-14 (the coordinator)

M6c round 18 (m6aa: the walker's two -O2 wrong answers fixed at their root - `bindStaticReference` bound a scalar
temporary in the init function's frame instead of `_ZGR`, so `bindReference` now copies it to the static object;
class and `thread_local` temporaries registered in tests/open/walker-static-reference-temporary.cpp, owed with M6d;
`CPP11_C6XLIVECHECK` removed; D5's first step, one stream-backed `LoopText`; src/ +92 -153).
- **Gate at 1d5526c0** (6f1a08b4 merged with main 522e4acf): Windows winpass -O0 and -O2: 828 same / 0 differ, masm
  827 / 0. Mac run.sh -O0 and -O2 1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0). tms6747: 814 passed, 0 failed at -O0 (393.2 M) and -O2 (**192.0 M**, unchanged); **walker -O0 and -O2
  814 / 0** (the two owed since wave 13 now pass); asm6x 833 / 0 at -O0 and -O2 (rerun after the ssh dropped).

### Wave 16 landed, 2026-11-14 (the coordinator)

M6c round 19 (m6ab: D5.1 `LoopText`'s readers on entries with reuseEntryLoads, D6.1 `Node` holds `callp`; nine of
the round's eleven steps left - D5.2-D5.6, D6.2-D6.5) with M5's lldb lane (m5-lldb).
- **Gate at 6530b725**: Windows winpass -O0 and -O2: 828 same / 0 differ, masm 827 / 0; cdb 5/5 both spellings. Mac
  run.sh -O0 and -O2 1215 / 0; lldb 5/5. Linux make test's suites all passing (run 1217/0, emit 3399/0, typed 860/0,
  ssa 890/0, coff 0 differ, c90 447/0) - its comment cap stopped it at one group in Dwarf.h (lldb's member split
  `funclet`'s comment from its line), mended in 6530b725 and the cap, memory gate, ast-residue and exclusions then
  rerun rc 0 on the rebuilt tree; gdb 5/5. tms6747 814 / 0 at -O0 (393.2 M) and -O2 (**192.0 M**); walker 814 / 0 at
  both; asm6x 833 / 0 at both.

### M6c, round 20 (D5.2-D5.4: the three rotated-loop passes read and compose entries), 2026-11-15, lane m6ac

- **Landed, each its own commit of identical output: D5.2** `hoistInvariants` (4de1e16f), **D5.3** `inductionPointers`
  (e70fe6b2), **D5.4** `widenFills` (41edb042). None reads `t.v`: an entry is asked by `LoopText`'s `instr`, `label`
  and `rw`, its mnemonic, predicate and operands; a rename is made on the operand (`MOperand` of the renamed
  spelling, as `edited` made it), a preheader is an `MStream` of `MInst`s, legality is `legal(MInst)` and C6xPack's
  `legalForm(MInst &)` (out of its unnamed namespace, declared in C6xMir.h); widenFills' labels are read onto the
  stream (`read`), its branches made with their predicate's number.
- **`LoopText`**: `set(k, MInst)` and `apply(h, MStream, i)` are the edits; the `Line` forms make the entry
  (`edited`) and call them, kept for `hoistConstantPairs` alone. `v` is still held in step (`viewOf` of each entry
  set or inserted) for that pass and a pending blank's liveness - D5.6's.
- **One reader of each fact the three passes shared** (C6xSched.cpp): `namesOf` (the labels, the operands named),
  `loopHead` (the head one predicated back edge names once), `loopBody` (a body that leaves by exits alone) - each
  was written out in every pass; `steppedRegister(MInst)` beside the line's, which `makeNode(Line)` keeps to D6.4.
- **Not changed, and why it matters**: every test still asks the spelling the line gave (`op[o].str()` against a
  name; `sideOf` of a pair's text is still its first register's), so an operand the model holds as text takes the
  path it took; `inductionPointers`' address is `*T` by the operand's mode where the line's `substr(1)` could never
  name a register otherwise - the one test stated on fields.
- **What still reads `Line`**: `hoistConstantPairs` (`t.v`, D5.5) and `LoopText::v`, `set(Line)`, `apply(Line)`,
  `edited`, `entry` (D5.6); the five folds (`Folding::v`, D6.2); the pipeliner's line halves (D6.3); the table's
  `Line` readers, `lineOf`/`viewOf` and the `!modelled` halves (D6.4); `parse`, `streamOf` and `c6xSchedule`'s read
  of the walker's text (D6.5). `Line` is named 30 times in C6xSched.cpp (52 at main), 35 in C6xPack.cpp, 11 in
  C6xPipe.cpp.
- src/ +230 -176 lines at 41edb042 (C6xSched.cpp 1,315 -> 1,356, C6xPack.cpp 1,450 -> 1,460, C6xModel.h 146 ->
  148, C6xMir.h 128 -> 129): the three shared readers and the entry forms of `set`/`apply` come before the `Line`
  forms they replace are deleted (D5.6, D6.4).
- **Gate.** Windows, cl /W4 /WX clean at each commit (work\m6ac 4de1e16f, m6ac2 e70fe6b2, m6ac3 41edb042), against
  main 3b8a80fb's compiler (work\w16g), tests/cases at -O0, -O1, -O2 by md5: **2,601 texts same, 1,068 refused alike,
  0 differ** at 4de1e16f; at e70fe6b2 and 41edb042 2,601 same and 6 differ, plain and under `CXX1_C6X_WALKER=1` -
  the path-length carets alone (posix-headers, windows-h-layout: a tree name one letter longer); census -O2 0 to
  any text pass, 0 `c6x-loop-unread`, 6,835 selected, kernels 219 / 9,122; exclusions 129 sites 0 uncited 0 stale,
  comments 0 over, memory gate rc 0, generate up to date, at all three. Linux, g++ -Werror clean, REF main
  85fe99fe's compiler (~/w7ref), at e70fe6b2 (~/m6ac) and again at 41edb042 (~/m6ac4; each ~/w16g with the four
  files laid over): `identical.sh ARCHS=tms6747` -O0 and -O2 1,736 compared, 2 differ; -O0 -g 868 compared, 1
  differ once the tree path in `.dwpsn` is normalised (91 by that alone) - static-reference-to-temporary, round
  18's fix, its text at -O0, -O2 and -O0 -g the same as main 3b8a80fb's own build; tests/tms6747.sh SHARD=k/3
  CYCLES=1 SIM=0 at -O2 **814 passed, 0 failed, 192,009,106 cycles** (unchanged); walker legs **814 / 0** at -O2
  (195,919,053) and at -O0 (447,770,677); g-same 867 same, 0 differ, 0 broken; asm6x.sh 833 objects, 0 failed at
  -O0 and -O2; census 0; exclusions 0/0, comments 0 over, memory gate, ast-residue, generate rc 0 (at 41edb042).
  The rewrites the identity covers: 212 `inductionPointers` accesses and 9 `widenFills` loops over tests/cases at
  -O2 (`CPP11_INDUCT`, `CPP11_FILL`). Not run (the coordinator's): winpass, make test, ssa.sh.
- **Owed:** D5.5 `hoistConstantPairs` (the one pass that meets placed entries), D5.6, then D6.2-D6.5, in round 19's
  order.

### Wave 17 landed, 2026-11-15 (the coordinator)

M6c round 20 (m6ac: D5.2 hoistInvariants, D5.3 inductionPointers, D5.4 widenFills on entries; src/ +230 -176).
- **Gate at 4ee0964c**: Windows winpass -O0 and -O2: 828 same / 0 differ, masm 827 / 0; cdb 5/5 both spellings. Mac
  run.sh -O0 and -O2 1215 / 0; lldb 5/5. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0,
  c90 447/0, comments 0); gdb 5/5. tms6747 814 / 0 at -O0 and -O2 (**192.0 M**); walker 814 / 0 at both (447.8 M,
  195.9 M); asm6x 833 / 0 at both.
- **Found after this gate, not by it** (lane m8a's census): main's cxx3 compiles 100 of its own 103 sources - three C6x
  files refused (C6xPack.cpp:209, C6xPipe.cpp:539, C6xSelect.cpp:420); 102 / 102 at M5's close. The wave gate never
  self-compiled. Lane sh1 mends the compiler and library; from wave 18 the gate compiles every src/*.cpp with cxx3.
### The self-host regression of the M6c waves, 2026-11-15, lane sh1

At M5's close the Linux self-host compiled 102 of 102 sources; main 3b8a80fb's cxx3 refused three of its own C6000
sources, each a shape g++ and cl take and clang takes too. Mended in the compiler and the library, none in the C6x
files (lane m6ad's):
- `C6xPack.cpp:209` (c1601e30, round 3) and `:380` (b25b87fa, round 7): `crossings(i, m)` from `c6x::crossings`, the
  two-argument one in c6x's unnamed namespace, and the reverse from inside it. **Root:** `resolveOverload` took the
  first key lookup answered as the whole set, so a namespace's own functions hid its unnamed namespace's
  ([namespace.unnamed]/1 makes them one scope), and an argument's namespace was asked only when nothing else
  answered ([basic.lookup.argdep] adds it always). **Mend:** one set from the three keys (`src/sema/Overload.cpp`).
- `C6xPipe.cpp:539` (ca93c160, round 13) and `:1842` (041e0247, round 15): `std::map<int, int, ByName>`,
  `std::set<int, ByName>`. **Root:** `<map>` and `<set>` had no comparator parameter. **Mend:** `Compare =
  std::less<K>`, read through `__tree`'s KeyOf (`less`); a comparator that keeps state is not taken (it is made where
  it is asked).
- `C6xSelect.cpp:420` (73dd2103, M6b round 3): a `[&]` lambda inside a `[&]` lambda of a member function naming a
  member. **Root:** in the outer closure's call operator the inner scan asked the closure, not the class, whether
  the name was a member, so the inner lambda never took `this`. **Mend:** `closureOuterClass()`, read by
  `namesOwnMember` and `capturedThisClass` (`src/parser/ParserExprLambda.cpp`).
- Cases (clang's output): `namespace-unnamed-overload-set`, `map-set-comparator`, `lambda-nested-this-default`.
  Open: `namespace-unnamed-nested-leaks` - an unnamed namespace inside `n` answers a lookup at file scope (the
  directive is kept in one file-wide list), so `::f` defined after `n`'s unnamed `f` is taken for it.
- src/ 94,414 -> 94,450 lines (include/ +32 -23).
- A fourth refusal met on the way: `C()(a, b)` in the first `less` is a functional-cast temporary of a template
  class, which cxx3 refuses ("not supported yet"), so `OptTable.cpp` (a `std::map<std::string, ...>`) fell; the
  comparator is a named object (20f14d2e) and `map-set-comparator` takes a `std::string` key.
- **Gate at 20f14d2e:** self-host stage 0 at -O0 **103 of 103** sources on Windows (cxx3-msvc) and on Linux
  (`tools/self-host`, 197 s; stage 1 built); cl /W4 /WX clean. Windows winpass -O0 **831 same / 0 differ, masm 830
  / 0** (828 / 827 + the three cases). Linux run.sh 1220 / 0; typed 863 / 0; ssa 893 / 0; syntax 3 / 0; coff 1,698
  objects, 0 differ; c90 447 / 0; overload, names and win-structure skipped (no clang++ on the box). emit.sh (on
  Windows, against main 3b8a80fb's golden): **36 of 3,399 changed, 12 added** - the 12 are the three cases on four
  targets; the 36 are nine cases that include `<map>` or `<set>` (deque-both-ends, include-map-set,
  library-self-compile-pieces-2, map-move-only-mapped, map-set-node-stability, member-template-body-unchosen,
  range-for-vector, return-self-referential, static-local-braced-list) on four targets: each instantiation's name
  carries the comparator now. Static: exclusions 129 sites 0 uncited 0 stale (three citations re-pointed),
  comments 0 over, memory gate, ast-residue, ide/generate.py --check rc 0; open.sh 31 differ (one new entry).
### M6c, round 21 (D5.5 and D5.6: the last loop pass on entries; `LoopText` holds no lines), 2026-11-15, lane m6ad

- **Landed, each its own commit of identical output: D5.5** `hoistConstantPairs` (72dd28a8) reads and composes
  entries - the pair by `sameOperands`, its web's readers renamed on the operand (`MOperand` of the renamed
  spelling) and judged by `legal(MInst)`, the hoisted pair two fresh `MInst`s of the spellings (as `edited` made
  them from no entry); **D5.6** (7f863b93) `LoopText::v`, `set(Line)`, `apply(lines)`, C6xPack's `edited` and `entry`
  and the `c6x-loop-unread` census line deleted. No loop pass names `Line`.
- **The one pass that meets placed entries.** To the lines a placed entry was no instruction, and a label where it
  starts in the first column: the kernel's own label (a `Label`, placed) by its name, a `||` turn by a name no
  branch spells. `LoopText::label` answers no for both, so the pass asks `placedLabel` (C6xSched.cpp), which reads
  the placed entry's spelling: a region holding a `||` turn alone is not refused for it, one holding any other
  placed entry is, and a back edge may name a kernel's label - all as before.
- **What `v` was still deciding, kept on entries**: `apply` dropped a line of no text with the blanked - an entry
  that spells nothing (a blank line kept verbatim) is dropped still, by its spelling; a pending blank's liveness
  reads the blanked entry as nothing (`LiveFacts()`, what the blank line's facts were) and every other by
  `liveFacts(MInst)`, which reads a placed entry through `viewOf` as before.
- **What still reads `Line`**: the five folds (`Folding::v`, D6.2); the pipeliner's line halves (D6.3); the table's
  `Line` readers, `lineOf`/`viewOf`, `legalForm(Line &)`, `renameable`, `rebuilt`, `make` and the `!modelled` halves
  (`LoopText::rw` among them) (D6.4); `parse`, `streamOf` and `c6xSchedule`'s read of the walker's text (D6.5).
  `Line` is named 22 times in C6xSched.cpp (30 at round 20), 29 in C6xPack.cpp (35), 11 in C6xPipe.cpp.
- src/ +76 -104 lines (C6xSched.cpp 1,356 -> 1,369, C6xPack.cpp 1,460 -> 1,422, C6xModel.h 148 -> 145).
- **Gate.** Windows, cl /W4 /WX clean at each commit (work\m6ad1 72dd28a8, m6ad2 7f863b93), against round 20's
  compiler (work\m6ac3, 41edb042), tests/cases at -O0, -O1, -O2 by md5: **2,601 texts same, 1,068 refused alike, 0
  differ**, plain and under `CXX1_C6X_WALKER=1`, at both; census -O2 0 to any text pass, 6,835 selected, kernels
  219 / 9,122; exclusions 129 sites 0 uncited 0 stale, comments 0 over, memory gate rc 0, generate up to date, at
  both. The rewrite the identity covers: `hoistConstantPairs` changes 79 of the walker's texts at -O2
  (`CPP11_C6XSKIP=pairs` against them, boxkit/m6ad-fired.sh). Linux (~/m6ad, a fresh tree of 7f863b93; REF main
  85fe99fe's compiler in ~/w7ref), g++ -Werror clean: `identical.sh ARCHS=tms6747` -O0 and -O2 1,736 compared, 2
  differ; -O0 -g 1 differ once the tree path in `.dwpsn` is normalised (91 by that alone) -
  static-reference-to-temporary, round 18's fix; every text against round 20's own (~/m6ac4-ident, -identg: 1,702
  and 851 compared) 0 differ; tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 **814 passed, 0 failed,
  192,009,106 cycles** (unchanged); walker legs at -O2 **814 / 0** (195,919,053, unchanged); g-same 867 same, 0
  differ, 0 broken; asm6x.sh 833 objects, 0 failed at -O0 and -O2; census 0; exclusions 0/0, comments 0 over,
  memory gate, ast-residue, generate rc 0. **Not finished at the hour**: the walker legs at -O0 (the chain was
  still running behind wave 17's gate on the one lock; ~/m6ad-gate.out holds them). Not run (the coordinator's):
  winpass, make test, ssa.sh.
- **The lane's cxx3 on the files it changed** (the coordinator's ask; `./cxx3.exe -std=c++11 -O0 -Isrc -c`):
  C6xSched.cpp compiles, 0 errors; C6xPack.cpp is refused at 209:40 (`c6x::crossings` with two arguments), the
  refusal round 20's file meets at the same place (lane sh1's) - what lies behind that first error is not seen.
- **Owed:** D6.2-D6.5, in round 19's order.
### M6c, round 22 (D6.2: the folds read and edit entries; `Folding` holds no lines), 2026-11-15, lane m6ae

- **Landed, one commit of identical output: D6.2** (6a73601c). `Folding::v` - a `Line` beside every entry, remade at
  each edit - is deleted with `edited` (the text fold's rebuilt line read back) and the `c6x-fold-edit` census line.
  `forwardMoves`, `forwardPairMoves`, `numberValues`, `foldScaledIndex` and `foldAddressing` ask the entry: `instr`
  (an instruction not placed), `m`, `n`, `op`, `last`, `plain` (no predicate), `blockEnd`, `label`, `passThrough`,
  `rw`. An edit is made on the operand - `MOperand` of the new spelling, or `renameIn`: the register by number where
  the entry is `modelled`, else `renamed` in the spelling the operand is held as (what the read-back line made).
- **What `v` was still deciding, kept on entries**: a placed entry and a kept line are no instruction and have no
  mnemonic, operand or predicate (`m`, `n` and `plain` answer so); a placed entry that starts in the first column
  is a label to `numberValues` and to a pass-through - `placedName` (C6xPack.cpp), the one reader, which C6xSched's
  `placedLabel` now calls too; `numberValues` drops an entry that spells nothing by its spelling, as
  `LoopText::apply` does.
- **`forwardConstants`** asked `renameable` and `crossings` of the entry's line: it asks `mayRename(MInst)` and
  `crossingsOf`, the readers the other folds use. `removeDead` is unchanged (its `!modelled` half is D6.4's).
- **Assumed, and what would show it wrong**: a kept (`Verbatim`) line that `parse` would read as an instruction was
  an instruction to `v`; to the folds now it is none, as it has been to `LoopText` since D5.1. `read` keeps a line
  verbatim only where it does not spell back, and no case's text has one (the identity below, and the census).
- **What still reads `Line`**: the pipeliner's line halves (D6.3); the table's `Line` readers, `lineOf`/`viewOf`,
  `legalForm(Line &)`, `renameable(Line)`, `rebuilt`, `make` and the `!modelled` halves - `rwOf`, `crossingsOf`,
  `unitsOf`, `memSide`, `removeDead`, `liveFacts`, `labelLike`, `LoopText::rw` (D6.4); `parse`, `streamOf` and
  `c6xSchedule`'s read of the walker's text (D6.5). `Line` is named 9 times in C6xPack.cpp (29 at round 21), 22
  in C6xSched.cpp, 11 in C6xPipe.cpp.
- src/ +233 -234 lines (C6xPack.cpp 1,422 -> 1,426, C6xSched.cpp 1,369 -> 1,363, C6xMir.h 129 -> 130): the
  folds' reads are respelled one for one; the lines come out with D6.4's readers.
- **Gate.** Windows, cl /W4 /WX clean (work\m6ae1, 6a73601c), against round 21's compiler (work\m6ad2, 7f863b93),
  tests/cases at -O0, -O1, -O2 by md5: **2,601 texts same, 1,068 refused alike, 0 differ**, plain and under
  `CXX1_C6X_WALKER=1`; census -O2 0 to any text pass, 6,835 selected, kernels 219 / 9,122; exclusions 129 sites 0
  uncited 0 stale, comments 0 over, memory gate rc 0, generate up to date. Linux (~/m6ae, a fresh tree of 6a73601c;
  REF main 85fe99fe's compiler in ~/w7ref), g++ -Werror clean: `identical.sh ARCHS=tms6747` -O0 and -O2 1,736
  compared, 2 differ; -O0 -g 1 differ once the tree path in `.dwpsn` is normalised (91 by that alone) -
  static-reference-to-temporary, round 18's fix; every text against round 21's own (~/m6ad-ident, -identg: 1,702
  and 851 compared) 0 differ; tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 **814 passed, 0 failed,
  192,009,106 cycles** (unchanged); walker legs at -O2 **814 / 0** (195,919,053, unchanged); g-same 867 same,
  0 differ, 0 broken; asm6x.sh 833 objects, 0 failed at -O0 and -O2; census 0; exclusions 0/0, comments 0 over,
  memory gate, ast-residue, generate rc 0. **Not finished at the hour**: the walker legs at -O0 - shard 1/3 270
  passed, 0 failed (177,239,396 cycles); shards 2/3 and 3/3 were queued on the one lock behind another lane's
  `make test` (~/m6ae-gate3.out holds them). Not run (the coordinator's): winpass, make test, ssa.sh.
- **The lane's cxx3 on the files it changed** (`./cxx3.exe -std=c++11 -O0 -Isrc -c`): C6xSched.cpp compiles, 0
  errors; C6xPack.cpp is refused at 219:40 (`c6x::crossings` with two arguments - round 21's file at 209:40, lane
  sh1's). Behind it, measured: with the unnamed namespace's three readers renamed in a copy (boxkit/m6ae-probe.sh,
  not committed), this round's C6xPack.cpp and round 21's both compile, 0 errors - the round adds no refusal.
- **Owed:** D6.3-D6.5, in round 19's order. D6.3 was not started: the round's hour went to D6.2 and its gate.

### M6c, round 23 (D6.3: the pipeliner reads and writes entries only), 2026-11-15, lane m6af

- **Landed, identical output: D6.3** (10a6af7f, and df189144 - an index nothing read any longer, which g++ refused
  and cl passed). `Loop::body` and `pre` (a `Line` beside every entry where one of the body or the preheader was not
  `modelled`), `fromLines`, `preTyped`, `spelledLine`, `Src::line` with its `made` and `have`, `insOf(Line)`, the
  pipeliner's own `lineOf` and `verbatim`, and `Turn::typed` are deleted. `Loop` holds `bodyM` and `preM` whatever
  the entries are; a turn is always the body's entry renamed and placed (`Code::placed` takes the entry; its
  `streamOf` of a kept line is gone); `Code::legal` asks C6xPack's `legalForm(MInst &)`; the kernel's two extra
  nodes are made from entries. `Line` is named nowhere in C6xPipe.cpp (11 times at round 22).
- **An entry that is not `modelled`** (an operand held as text naming a register) is read where it stands: its
  operand by the spelling it is held as (`Src::reg`, `number`, `pair`, `regAt`, `side`, `regsIn`, `byteAddress`,
  `insOf`, `pairIn`), renamed in that spelling (`renameReg`, one for `Loop::rename` and `spelled`), and its table
  facts through C6xPack's readers, now declared in C6xMir.h: `rwOf`, `crossingsOf`, `unitsOf`, `labelOf` (the folds'
  `label`, shared) and `steppedOf`. Their `!modelled` halves are the only `Line` the pipeliner still reaches, and
  they are D6.4's to delete.
- **Assumed, and what would show it wrong**: as rounds 21 and 22 - a kept line or a placed entry is no instruction
  to `Src` (no mnemonic, operand, predicate, read or write), where `parse` might have read a kept line as one;
  an unplaced body entry carries no `||` and no named unit (a turn of one went through `spelledLine`); and a
  body with a `!modelled` entry now places renamed entries, where it placed kept lines read back by `streamOf`.
  No case's text has any of the three: the census's 0 `!modelled` reads and the identity below.
- **What still reads `Line`**: the table's `Line` readers, C6xPack's `lineOf`/`viewOf`, `legalForm(Line &)`,
  `renameable(Line)`, `rebuilt`, `make` and the `!modelled` halves - `rwOf`, `crossingsOf`, `unitsOf`, `steppedOf`,
  `labelLike`, `memSide`, `removeDead`, `liveFacts`, `LoopText::rw`, `makeNode` (D6.4); `parse`, `streamOf` and
  `c6xSchedule`'s read of the walker's text (D6.5). `Line` is named 9 times in C6xPack.cpp, 22 in C6xSched.cpp.
- src/ +132 -265 lines (C6xPipe.cpp 2,528 -> 2,383, C6xPack.cpp 1,426 -> 1,429, C6xMir.h 130 -> 139).
- **Gate** (df189144). The base moved under the round (wave 18's 12 cases differ from round 22's and main
  85fe99fe's texts at every level, -O0 too), so the base itself, b6edce1a, was built on both boxes and is the
  reference. Windows, cl /W4 /WX clean (work\m6af1 against work\m6af0), tests/cases at -O0, -O1, -O2 by md5:
  **2,613 texts same, 1,068 refused alike, 0 differ**, plain and under `CXX1_C6X_WALKER=1`; census -O2 0 to any
  text pass, 7,012 selected, kernels 219 / 9,122; exclusions 129 sites 0 uncited 0 stale, comments 0 over, memory
  gate rc 0, generate up to date. Linux (~/m6af against ~/m6af0), g++ -Werror clean (10a6af7f was refused for one
  unused variable): `identical.sh ARCHS=tms6747` -O0 and -O2 **1,744 compared, 0 differ**; -O0 -g 872 compared,
  0 differ once the tree path in `.dwpsn` is normalised. Against main 85fe99fe's compiler (~/w7ref): 26 differ
  (13 cases: static-reference-to-temporary and wave 18's 12), -g 10 after normalising; against round 22's texts
  18 and 9 differ, wave 18's cases alone. tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 **818 passed, 0
  failed, 192,802,451 cycles** (wave 18's four new cases over round 22's 814 and 192,009,106; every text is the
  base's); g-same 871 same, 0 differ, 0 broken; asm6x.sh 837 objects, 0 failed at -O0 and -O2; census 0;
  exclusions 0/0, comments 0 over, memory gate, ast-residue, generate rc 0. Walker legs at -O2 **818 passed, 0
  failed**. **Not finished at the hour**: the walker legs' three -O0 shards,
  queued on the one lock behind the coordinator's wave 18 gate (~/m6af-gate3.out holds them). Not run (the
  coordinator's): winpass, make test, ssa.sh.
- **The lane's cxx3 on every C6x file** (`./cxx3.exe -std=c++11 -O0 -Isrc -c`, Linux): C6xPack.cpp, C6xPipe.cpp,
  C6xSched.cpp, C6xSelect.cpp and C6xMir.cpp each compile, 0 errors.
- **Owed:** D6.4 and D6.5, in round 19's order; D6.4 not started.
### M6c, round 24 (D6.4: the scheduler's table reads operands only), 2026-11-15, lane m6ag

- **Landed, identical output: D6.4** (56371e9a). C6xSched.cpp: `make`, `rebuilt`, `readsAndWrites(Line)`,
  `steppedRegister(Line)`, `isLabel`, `labelName`, `blockEnd`, `isCall(Line)`, `liveFacts(Line)`, `crossings(Line)`,
  `unitsFor(Line)`, `legalForm(Line &)`, `renameable`, `touches`, `passThrough(Line)`, the text `frameSlot` and
  `makeNode(Line)` are deleted. C6xPack.cpp: `lineOf`, `viewOf`, `renameIn` and the `!modelled` halves of
  `makeNode`, `liveFacts`, `removeDead`, `memSide`, `LoopText::rw` and the fills' reader; `rwOf`, `crossingsOf` and
  `unitsOf` were then synonyms of `readsAndWrites`, `crossings` and `unitsFor` and are gone (the pipeliner's
  `regsOf`/`masksOf` are one line each). `labelLike` and a placed entry's liveness read the entry, not a line of it.
- **The refusal by name.** `onOperands(MInst)` is what `modelled` asked of the operands (text naming a register, an
  address or a number is not read); `onOperands(MStream)` asks it of every instruction, a placed one too, and
  `c6xSchedule` asks it twice - after `streamOf` and before `packetise` - handing the text back with `spelled`
  false, so `Tms6747` refuses as round 17's fallback does ("a line the scheduler's stream does not spell"; the one
  site, its citation unmoved). Under `CXX1_C6X_MIR` the entry is printed as `c6x-line:`. `legal` answers no for
  such an entry, so the selector never builds one. No case reaches it: 0 `c6x-line` over tests/cases at -O2 on both boxes, and no case's text moved.
- **Assumed, and what would show it wrong**: a placed instruction (the kernel's, read with `par` and its unit
  through `lineOf` until now) reads the same on its operands - `foldBranchNops`' packet nodes and the pipeliner's
  reads of a kernel; no pass composes an entry `onOperands` refuses between the two checks; a `Verbatim` entry is
  never an instruction to `parse` (`streamOf` refuses one). The identity and the per-case cycles below hold all three.
- **What still reads `Line`**: `parse` (C6xSched.cpp), `streamOf` (C6xPack.cpp), `read`'s one call of `parse`
  (C6xMir.cpp) and `c6xSchedule`'s read of the walker's text - D6.5, all of it. `Line` is named 3 times in
  C6xPack.cpp, 4 in C6xSched.cpp, once in C6xMir.cpp, not at all as a type in C6xPipe.cpp. The pipeliner's
  `typed(i) ? ... : op(i, k)` halves still read a placed entry by its spelling: they ask `modelled`, not the table.
- src/ +67 -357 lines (C6xSched.cpp 1,363 -> 1,143, C6xPack.cpp 1,429 -> 1,390, C6xPipe.cpp 2,383 -> 2,372,
  C6xModel.h 145 -> 130, C6xMir.h 139 -> 138, C6xMir.cpp 185 -> 181).
- **Gate** (56371e9a; the reference is round 23's base build, b6edce1a, whose texts round 23's are). Windows, cl /W4 /WX
  clean (work\m6ag1 against work\m6af0), tests/cases at -O0, -O1, -O2 by md5: **2,613 texts same, 1,068 refused
  alike, 0 differ**, plain and under `CXX1_C6X_WALKER=1`; census -O2 0 to any text pass, 0 `c6x-line`, 7,012
  selected, kernels 219 / 9,122; exclusions 129 sites 0 uncited 0 stale, comments 0 over, memory gate rc 0, generate
  up to date. Linux (~/m6ag against ~/m6af0), g++ -Werror clean: `identical.sh ARCHS=tms6747` -O0 and -O2 **1,744
  compared, 0 differ**; -O0 -g 0 differ once the tree path in `.dwpsn` is normalised. Against main 85fe99fe's
  compiler (~/w7ref): 26 differ and -g 10 after normalising, round 23's numbers (the base's 13 cases).
  tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 **818 passed, 0 failed, 192,802,451 cycles**, and **per case
  (cycles.txt of each shard against round 23's, 815 cases) 0 differ**; walker legs **818 / 0** at -O2 and at -O0;
  g-same 871 same, 0 differ, 0 broken; asm6x.sh 837 objects, 0 failed at -O0 and -O2; census 0; exclusions 0/0,
  comments 0 over, memory gate, ast-residue, generate rc 0. Not run (the coordinator's): winpass, make test, ssa.sh.
- **The lane's cxx3 on every C6x file** (`./cxx3.exe -std=c++11 -O0 -Isrc -c`, Linux): C6xPack.cpp, C6xPipe.cpp,
  C6xSched.cpp, C6xSelect.cpp and C6xMir.cpp each compile, 0 errors.
- **Owed:** D6.5 (`parse`, `streamOf`, `Line`), then the pipeliner's spelling halves of a placed entry.
### M6c, round 25 (D6.5: the scheduler takes entries; `Line`, `parse` and `streamOf` deleted), 2026-11-15, lane m6ah

- **Landed, identical output: D6.5** (3075c4d2; 9198b442 a comment). `c6xSchedule(before, selected, after, level,
  spelled)`: a selected function's body reaches the scheduler as the selector's own entries (`Tms6747::ssaMir_`, by `addSerial`) - it is no
  longer written out and read back; a walked function's text, and the prologue, parameter copies and epilogue the
  walker's helpers write round either body, are read once by `readSerial`. Both leave the -O0 padding (`NOP`) out and
  answer false for a line that is an instruction to the scheduler (a tab, a predicate, a capital) and is not spelled
  back the same, so the refusal by name is where it was. `entryOf` (C6xMir.cpp) is the one reader of a line - `parse`'s
  fields go straight into an `MInst` - and `read` (a helper's text in a selected body, `ssaAbsorb`) is a loop over it.
- **Deleted**: `struct Line`, `parse` (C6xSched.cpp), `streamOf` (C6xPack.cpp), `c6xSchedule`'s vector of lines, and
  the pipeliner's spelling halves - `Src::typed` and the `!typed` arms of `reg`, `number`, `pair`, `regAt`, `side`,
  `regsIn`, `byteAddress`, with the text `byteAddress`. `c6xSchedule` asks `onOperands` before `pipelineLoops` too, so
  the pipeliner never meets an operand it would have read by its spelling. `Line` is named nowhere in src/.
- **What the walker keeps until M6d, exactly**: its text. A walked function is `out_` text handed to `c6xSchedule` and
  read by `readSerial`; a selected one still has the walker's prologue, parameter copies (`emitParams`), `earlyExit`
  and epilogue as text on either side of its entries, and its helpers' text inside the body through `ssaAbsorb`/`read`.
  So `entryOf`, `read`, `readSerial`, `MOperand`'s parse of a spelling, `write`/`MInst::str`, and the refusal "a line
  the scheduler's stream does not spell" stay until the walker emits entries or goes (M6d). At -O0 a selected body is
  still written as text (`c6xSchedule` returns it unscheduled).
- **Assumed, and what would show it wrong**: a selected entry reads the same from the stream as from its spelling read
  back (`MInst::str` then `entryOf` gives the entry: kinds, immediates, a Symbol's text); no placed or kept entry
  reached the pipeliner's `!typed` arms with an answer the operand does not give. The identity below holds both.
- **What is left of the text passes: none.** No pass holds a line; every pass reads and edits entries. What remains
  of *spelling* is inside operands, not lines, and is not a text pass: `registersIn` over `MOperand::str()` at 11
  sites and `renamed` at 4 (the folds and loop passes in C6xPack.cpp/C6xSched.cpp reading an address's registers),
  and 6 `kind == Symbol` halves (`isAddress`, `isPair`, `renameReg`, `Ins`'s `name[]`) that `onOperands` now makes
  unreachable for a register, an address or a number. They are a tidy round's, or go with M6d.
- src/ +111 -153 lines (C6xSched.cpp 1,143 -> 1,114, C6xPack.cpp 1,390 -> 1,366, C6xPipe.cpp 2,372 -> 2,336,
  C6xModel.h 130 -> 119, C6xMir.cpp 181 -> 229, C6xMir.h 138 -> 140, C6xSched.h 10 -> 13, Tms6747.cpp +5 -2).
- **M6c closed, in code.** Nothing of the text passes is left: the packetiser, the pipeliner, the folds and the loop
  passes read and write entries, `Line` is deleted, and the fallback to text is a refusal by name that no case
  reaches. Since M6c began (m6-i b7510429) the seven C6x optimizer files took +3,310 -2,687 lines: C6xSched.cpp 2,297
  -> 1,114, C6xPipe.cpp 2,007 -> 2,336, C6xPack.cpp 0 -> 1,366 (the table on operands, the folds, the stream's
  liveness), 4,694 -> 5,317 in all - the text passes went, their readers on operands came.
- **The row's judge.** tms6747.sh and asm6x.sh below. The cycle table case by case at -O2: against round 24, 815
  cases, 0 differ; against wave 18's gate (`~/cyc-w18g.txt`), 815 cases, 0 worse, 0 better; against round 22's
  (`~/cyc-m6ae.txt`, M6a's 192,009,106 on its 811 cases) **802 equal, 9 worse, 0 better** - deque-both-ends,
  include-map-set, library-self-compile-pieces-2, map-move-only-mapped, map-set-node-stability,
  member-template-body-unchosen, range-for-vector, return-self-referential, static-local-braced-list, +777,307
  cycles between them. All nine came with wave 18's base (include/, lane sh2's to mend), none from a C6x pass: every
  M6c round is 0 differ in text against its own base's build. The judge is whole when sh2's mend lands.
- **Gate** (3075c4d2; the reference is round 24's own build, cecf1d27's code). Windows, cl /W4 /WX clean
  (work\m6ah1 against work\m6ag1), tests/cases at -O0, -O1, -O2 by md5: **2,613 texts same, 1,068 refused alike, 0
  differ**, plain and under `CXX1_C6X_WALKER=1`; census -O2 0 to any text pass, 0 `c6x-line`, 7,012 selected, kernels
  219 / 9,122; exclusions 129 sites 0 uncited 0 stale, comments 0 over (1 over at 3075c4d2, mended by 9198b442),
  memory gate rc 0, generate up to date. Linux (~/m6ah against ~/m6ag), g++ -Werror clean: `identical.sh
  ARCHS=tms6747` -O0 and -O2 **1,744 compared, 0 differ**; -O0 -g 0 differ once the tree path in `.dwpsn` is
  normalised. Against main 85fe99fe's compiler (~/w7ref): 26 differ and -g 10 after normalising, rounds 23 and 24's
  numbers (the base's cases). tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 **818 passed, 0 failed, 192,802,451
  cycles**, per case as above; walker legs **818 / 0** at -O2 and at -O0; g-same 871 same, 0 differ, 0 broken;
  asm6x.sh 837 objects, 0 failed at -O0 and -O2; census 0 (`c6x-line` 0); exclusions 0/0, comments 0 over, memory
  gate, ast-residue, generate rc 0. Not run (the coordinator's): winpass, make test, ssa.sh.
- **The lane's cxx3 on every C6x file** (`./cxx3.exe -std=c++11 -O0 -Isrc -c`, Linux): C6xPack.cpp, C6xPipe.cpp,
  C6xSched.cpp, C6xSelect.cpp and C6xMir.cpp each compile, 0 errors.
- **Owed:** the nine cases' cycles (lane sh2, include/); the operand-spelling readers above (a tidy round, or
  M6d); M6d itself - the walker, and with it `readSerial`, `read` and the text round a selected body.

## Wave 18's map/set cycles, 2026-11-15 (lane sh2, from wave18 4639c9af)

- **The rise, measured** (tms6747 -O2, vm6747 cycle.CPU, round 22's ~/m6ae against wave 18's ~/w18g): 192,009,106 ->
  192,786,413 over the 811 common cases; nine rose, and all nine include `<map>` or `<set>` (deque-both-ends and
  range-for-vector among them). One root, sh-1's comparator: every key comparison in `__tree` went through
  `KeyOf::less`, which made a `std::less<K>` object and called its `operator()` - two calls the inliner had to take
  where round 22 wrote `<`.
- **The mend** (include/ only): `__tree_order<T, C>` in `<__tree>` is the order of two keys, and its specialisation for
  `std::less<T>` is `a < b` itself; `__map_key` and `__set_key` name their comparator as `key_compare`, and the tree
  reads `__tree_order<key_type, key_compare>` once. A named comparator is still made where it is asked (`C c;`).
  deque-both-ends and range-for-vector are back to round 22's count exactly; the total is 192,021,032.
- **What is left, and why** (7 cases above round 22, +11,926 in all; map-set-node-stability 7,620,285 -> 7,630,335,
  include-map-set +631, static-local-braced-list +533, library-self-compile-pieces-2 +364, map-move-only-mapped
  +153, member-template-body-unchosen +117, return-self-referential +78): the inliner's **unit budget**. Each
  comparison is one inlined call more than round 22's `<` (`__tree_order::less`, growth > 0 in SsaInline's measure
  of an unpromoted body), so the unit's 40% is spent a few sites sooner and one `rotateLeft` in `rebalance` is
  declined (`CPP11_DECLINES`: 74 -> 75 `inline-unit-budget-spent`); the code is otherwise the same instruction for
  instruction (the unused weak `__tree_order::less` moved nothing: removing it from the .s leaves the count).
  Tried and dropped: SsaInline not counting the loads of a parameter's slot (21 cases up, unordered-map +17,525); one
  comparison a level in `place` (map-set-node-stability +159,396). Removing the rest wants either the comparison with
  no call at all for `std::less` (one `__tree` per order, which is duplication) or an inliner that measures a callee as
  promote leaves it - its own round, judged over all 811.
- **Not mended: the `C()(a, b)` refusal** ("'C(...)' makes a temporary of type 'struct less<...>', which is not supported
  yet", src/sema/Overload.cpp:610). It reproduces in src/optimizer/OptTable.cpp with `C()(a, b)` put back in `<map>`,
  and in a 36-line cut of it (boxkit/sh2-r/u1.cpp: `opcodeOf`'s static map plus the unnamed namespace before it), but
  in no standalone case written without OptTable.h (a template's functional cast, in a namespace, an unnamed
  namespace, a static local's lambda, a declared-then-defined overload, an alias named `K`: all compile). Owed: the
  reduction past OptTable.h, then a tests/cases entry with clang's answer.
- src/ unchanged; include/ +21 -10 (`<__tree>` 324 -> 337, `<map>` 145 -> 144, `<set>` 100 -> 99).
- **Gate at 5984a7c3** (include/ only; the compiler is wave 18's): tests/tms6747.sh SHARD=k/3 CYCLES=1 SIM=0 at -O2 on
  Linux (~/sh2), **818 passed, 0 failed; 815 cases, 811 common with round 22: 192,009,106 -> 192,021,032, 7 up (above),
  0 down** (wave 18: 192,786,413); map-set-comparator 15,388 -> 15,293. Windows (work\sh2w): cl /W4 /WX clean;
  winpass -O0 **832 same / 0 differ, masm 831 / 0**; self-host stage 0 at -O0 **103 of 103**; exclusions 129 sites 0
  uncited 0 stale, comments 0 over, memory gate rc 0, generate up to date. Linux: run.sh 1221 / 0; make test rc 0
  (emit 3415 / 0, typed 864 / 0, ssa 894 / 0, syntax 3 / 0, coff 1,700 objects 0 differ). emit.sh against wave 18's
  golden (work\sh2w, recorded with work\w18g's compiler): **40 of 3,415 changed**, 0 added - the ten cases that include
  `<map>` or `<set>` (the nine above and map-set-comparator) on four targets: no comparator object, one order.

### Wave 18 landed, 2026-11-15 (the coordinator)

The self-host mend (sh-1: overload sets over a namespace, its unnamed namespace and the arguments' namespaces;
`<map>`/`<set>` comparators; `this` in a nested `[&]` lambda), its cycles mend (sh-2: `std::less` ordered by `<`),
M6c round 21 (m6ad: D5.5, D5.6 - no loop pass reads `Line`) and M8's first piece (m8a: the instantiation census and
substitution for the simplest function template, behind CXX1_TEMPLATE_SUBST).
- **Self-host: cxx3 compiles 103 of its 103 sources** (100 at main 505b68bb); the wave gate compiles every src/*.cpp
  with cxx3 from now on.
- **Gate at 4639c9af** (Windows winpass -O0 and -O2 832 / 0, masm 831 / 0; cdb 5/5; Mac 1219 / 0 and lldb 5/5; Linux
  make test rc 0 - run 1221/0, emit 3415/0, typed 864/0, ssa 894/0; gdb 5/5) **and at 6b27e9de** (winpass -O0 832 / 0,
  self-compile 103/103 on both boxes, Linux make test rc 0, run 1221/0; tms6747 818 / 0 at -O0 and -O2 on the
  selector and the walker; asm6x 837 / 0 at both; Mac 1219 / 0, lldb 5/5; cdb 5/5; gdb 5/5).
- **Cycles, per case** (tms6747 -O2, the 811 cases common with round 22): 192,009,106 -> 192,021,032. **Owed: 7 cases
  above round 22 by +11,926** (map-set-node-stability +10,050 and six of a few hundred) - the inliner's unit budget
  declines one `rotateLeft` in `rebalance`; two mends tried made it worse and were reverted. Also owed: cxx3's
  refusal of `C()(a,b)` inside OptTable.cpp (no standalone case yet), and tests/open/namespace-unnamed-nested-leaks.
  New baseline: the 811 common cases at 192,021,032 plus the four new ones (818 passed; 192,802,451 is wave 18 before sh-2).

### Wave 19 landed - **M6c closed**, 2026-11-15 (the coordinator)

M6c rounds 22-25 (m6ae D6.2 the folds on operands, m6af D6.3 the pipeliner on entries, m6ag D6.4 the table on
operands, m6ah D6.5 `Line`, `parse` and `streamOf` deleted): no text pass is left in the C6000 optimizer; the
walker's text is read once into entries until M6d (round 25's entry names what it keeps).
- **Gate at e37e175a** (m6-ah ff2bc12a merged with main 1001aa4b): Windows winpass -O0 and -O2: 832 same / 0 differ,
  masm 831 / 0; cdb 5/5 both spellings. Mac run.sh -O0 and -O2 1219 / 0; lldb 5/5. Linux make test rc 0 (run
  1221/0, emit 3415/0, typed 864/0, ssa 894/0, c90 447/0, comments 0); gdb 5/5; **self-compile 103 / 103**.
  tms6747 818 / 0 at -O0 and -O2 on the selector and the walker; asm6x 837 / 0 at both.
- **M6c's judge, cycles case by case against round 22** (tms6747 -O2, 811 common cases): 802 equal, 7 above (+11,926,
  wave 18's library change, owed there), 0 below; no case moved by a C6x pass. 815 cases total 192,036,975.
- **Owed into the review and M6d**: operand-spelling readers left inside passes (`registersIn` over
  `MOperand::str()` 11 sites, `renamed` 4, `kind == Symbol` halves unreachable for registers); the seven C6x
  optimizer files grew 4,694 -> 5,317 lines over M6c (+3,310 -2,687) - the text passes were replaced, not removed.

## The M6c review's mends

### Lane r6a - the wave gate in the tree (R105, R107, R104's script half), 2026-11-15

- **`tools/wave-gate <name>`** (new; `tools/verify-three wave <name>` is the same command). The fixed list of
  steps, each printing PASS, FAIL or NOT RUN with its reason; the last line fails on a failure or on a step not
  run with no `-n <step>=<reason>` on the command line. It ships HEAD (`git archive`, the commit printed), and
  chooses a box per step (the user, 2026-11-15: the Windows box relieves Linux):
  - Linux (what executes x86_64-linux code): `build static gdb run1 maketest selfc fixed0 fixed2`, one detached
    chain, each step a box job under the lock and the 25-minute cap.
  - Windows (/affinity 3FF, WINLANES): `wbuild winpass0 winpass1 winpass2 cdb wselfc names overload winstruct
    tms0 tms1 tms2 wtms0 wtms1 wtms2 cycles sim asm0 asm2 wfixed0 wfixed2`.
  - Mac (one job at a time, -j2, under the caps): `mbuild mrun0 mrun2 lldb`; `mfixed` only under `-a mfixed`.
  `-k` runs only the steps with no verdict yet, in the trees already built; `-r` prints the report again.
- **The user's decisions on the review, written as steps**: Q4 - `sim` (tms6747.sh -O2, SIM=1, six shards) is owed
  by every wave; Q4b - `mfixed` (target 4's fixed point on the Mac) is not a wave step, run at a milestone's close
  with `-a mfixed`; Q9 - `names`, `overload`, `winstruct` run on the Windows box in every wave.
- **R107**: `tms1`/`wtms1` (tests/tms6747.sh at `CXX1_FLAGS=-O1`, selector and walker), `run1` and `winpass1` are
  steps; `tools/verify-three`'s C6000 loop takes -O1 too. tests/tms6747.sh needed no change.
- **R104's script half**: `selfc`/`wselfc` (stage 0 over every source) are owed by every wave. **The fixed point**
  (`fixed0`, `fixed2` on Linux by tools/self-host; `wfixed0`, `wfixed2` on Windows by self-host.cmd in the MASM
  spelling: stages 0 = 1 = 2 and 2 = 3) is owed at a milestone's close and by any wave that deletes a code path
  or changes what stage 0 is built from; any other wave must say `-n fixed=<why>`, so it is never silent.
- **The run, `tools/wave-gate r6a` at c7770572 (458f8974's src/, tools/ alone differ)**: 6 passed, 3 failed, 24 not
  run, each with its reason; the report is `~/cxxopt-build/wave-gate/r6a-report.txt` on the Mac.
  - **The -O1 leg (R107), first execution, on the Windows box**: tms6747.sh -O1 selector **818 passed, 0 failed**;
    walker **818 / 0** (vm6747, SIM=0). x86 at -O1: winpass **832 same / 0 differ, masm 831 / 0**.
  - Windows PASS: wbuild (cl /W4 /WX), cdb 5/5 in both spellings, wselfc 103 of 103.
  - **Windows FAIL - what wave 19 did not run (R103), found by the first run**: names.sh 711 passed, **161 failed**;
    overload.sh 43 agreed, **1 differed**; win-structure.sh layout 747 compared, **557 differ**, names 96
    differing. Not root-caused here: whether each is the range's regression or the Windows box's environment
    (Git bash, clang 19.1.5 from BuildTools) is lanes C and D's to say. Logs beside the report.
  - NOT RUN: Linux build, gdb, run1, maketest, selfc - the box lock was held by lanes B and C for the lane's whole
    hour, and the chain was stopped by PID as its build began; static was run by hand in its tree (exclusions 0
    uncited 0 stale, comments 0 over the cap, memory-gate rc 0, ide up to date). winpass0/2, tms0/2, wtms0/2,
    asm0/2 (wave 19's numbers stand; the C6000 steps moved to Windows mid-run); cycles (lane B's tool); sim
    (SIM6747 is not built on the Windows box yet); the four fixed-point steps (lane C); the Mac's four.
- **Found in the script by its own run, and mended**: `start /b /wait` loses a suite's exit status - the first
  names step passed on names-overload.cmd's "no clang++" message; a suite's status is now a line it prints.
- **Owed**: SIM6747 built on the Windows box (then `sim` runs there); `tilink`, `identical`, `emit`, `census`
  and `release-check` are not steps yet; the Mac steps and the Linux chain's steps past `build` have not been
  executed by this script once; stopping a Linux chain by its PID leaves the step's children (kill those too).
- **Question for the user**: none new. src/ lines unchanged (no src/ file touched).
### R106: M6's judge in the tree - `tools/c6x-cycles` and `tests/tms6747-cycles.txt`, 2026-11-15, lane r6b

From main 458f8974. The review's R106: "cycles equal or better, case by case" was a hand comparison between files
in the Linux box's home (`~/cyc-m6ae.txt`, `~/cyc-w19g.txt`). No src/ line changed (src/ lines before and after: the same).
- **The tool.** `tools/c6x-cycles` runs `tests/tms6747.sh`'s shards (CYCLES=1; SHARDS=3; LEVELS="-O2 -O0"), keeps each
  level's counts in `tests/out-c6x-cycles/cycles<level>.txt` and compares them case by case with
  `tests/tms6747-cycles.txt`: one line for every case that is not equal (`better`, `worse`, `WORSE`, `new`, `MISSING`,
  `STALE`), then the counts and both totals. It fails on a case worse and neither owed nor accepted, on one above its
  cap, on a baseline case that gave no count, on an `owed`/`accepted` line no longer needed, and on a shard that
  failed. `--record` writes the baseline (a column not run keeps what it held); `--check <cycles.txt> [level]`
  compares a collected file and runs nothing; `--baseline <file>` takes another baseline, a collected file included.
- **The baseline's form.** A row a case, a column a level; `measured <column> <commit> <date>`; and
  `owed|accepted <case> <column> <cap> <reason>` - the column holds the count the case stands above, the cap is how
  far. **Q1 (the user, 2026-11-15): wave 18's seven are accepted as a library cost** - their -O2 column is round
  22's count, their cap wave 19's, the cause "the inliner's unit budget declines rotateLeft in rebalance".
- **Q4 (the user): sim6747 in every wave gate.** SIM=1 runs tms6747.sh's sim6747 leg and judges sim6747's own count
  (`sim6747 -c` on the linked TI program) as the columns `<level>-sim`; SIM=0 says "NOT RUN" aloud. The hook in
  tests/tms6747.sh: with CYCLES=1 the sim leg passes `-c`, cuts the count into `<case>.simcycles` and collects
  `cycles-sim.txt`.
  The hook is behind its own switch (SIMCYCLES=1, set by the tool), so the sim leg every other caller runs is the
  line it was. **Owed: the hook has not run once and no `-sim` column is recorded** - the Linux lock was held past
  the lane's hour, and the user's split of 2026-11-15 sends SIM=1 to the Windows box, where SIM6747 is not built yet
  (boxkit/windows-split-2026-11-15.md says how). First step there: one case with CYCLES=1 SIM=1 SIMCYCLES=1 to see
  that `sim6747 -c --run` prints the CYCLES line, then `SIM=1 tools/c6x-cycles --record`.
- **Checked against the box's files** (Linux, `--check`, nothing run): wave 19's `~/cyc-w19g.txt` against round
  22's `~/cyc-m6ae.txt` as the baseline: 815 cases, 811 common, **804 equal, 7 worse, 0 better**, 4 new, 192,009,106
  -> 192,021,032, exit 1 with the seven named. **The plan's "802 equal" (wave 19's entry, and this lane's brief) is
  a slip**: 802 was 811 less the nine before sh-2; with seven above it is 804, and 804 + 7 = 811. Against wave
  18's `~/cyc-w18g.txt`: 805 equal, 10 better, 0 worse (192,802,451 -> 192,036,975).
- **The baseline, recorded by the tool itself** (`COMMIT=458f8974 tools/c6x-cycles --record` on the Linux box, vm6747,
  SIM=0, in ~/r6b: wave 19's tree and cxx3.exe, its src, include, lib, tests, tools and Makefile hash-equal to
  458f8974's blobs): 818 passed, 0 failed at each level; **815 cases; -O2 192,036,975 cycles** (wave 19's total
  exactly), **-O0 397,274,101**. With the seven's column set to round 22's count the -O2 column sums to 192,025,049.
  The tool on its own collected files: -O2 808 equal, 0 better, 7 worse (7 accepted), 0 new, 0 missing, exit 0;
  -O0 815 equal, exit 0; on wave 19's file the same line. A case raised by hand (+5) and an accepted case one cycle
  past its cap: both `WORSE`, exit 1. Round 22's own file: 811 equal, 4 missing, 7 stale, exit 1.
- **Left**: the `-sim` columns (above); `tools/wave-gate`'s `cycles` step is lane r6a's to call
  (`tools/c6x-cycles`, or `SIM=1 tools/c6x-cycles` once the columns exist); the baseline was taken on Linux and
  each `measured` line says so - a Windows run compares against it (the two vm6747 builds agreed cycle for cycle
  at the qualification).
- **Static gates** (the Windows box, work\r6b, affinity 3FF, the lane's tree): tools/exclusions --check 129 sites, 0
  uncited, 0 stale; comment-lines 0 groups over the cap; ide/generate.py --check up to date. tools/memory-gate does
  not start on Windows (`signal.SIGPIPE`, tools/memory-gate:30 - a defect for the split); no src/ file changed, so
  its count is wave 19's.
### Lane r6c: R102, R103 and R104 measured at 458f8974, 2026-11-15 (box runs, no source change)

Every number below is of `458f8974`'s tree, built afresh on each box (Linux g++ `cxx3.exe`; Windows cl `cxx3-msvc.exe`,
/W4 /WX clean). The coordinator's re-route at 16:54 ("use the Windows box to relieve Linux load") moved the C6000
legs to Windows; Linux keeps the self-host.
- **R102, TI's own tools.** `tests/tms6747.sh` SIM=1 CYCLES=1 at -O2, four shards on Windows (SIM6747 1.12 built
  there with cl from the Linux box's sources, `work\ti6x\SIM6747`): **814 passed, 4 failed** (208/0, 201/1, 199/2,
  206/1), every other pass also on sim6747 with TI's `rts6740_elf_eh.lib`; cycles **192,036,975**, wave 19's total
  to the cycle. The four: `inheriting-constructors`, `temporary-member-call` and `pipelined-data-exit`, which asm6x
  refuses at -O2 ("EXT's source and destination are on one side", EXTU for the third) - vm6747 never assembled
  them; and `long-double-literal-exact-digits`, whose five numbers differ from .expected in the last digits on
  sim6747 (TI's runtime prints them) and not on vm6747. `tests/ti-link.sh` -O2 on Linux (TI's lnk6x 7.4.4):
  **828 linked, 3 failed** - the same three asm6x refusals - 34 not for this target; at -O0 on Windows (the same
  lnk6x 7.4.4) **831 linked, 0 failed**. `release-check-box.py` on
  Windows: **PASS, 34 programs x O1 O2 Os, 166 s**, sim ok and emu ok on every one.
- **R104, the fixed point.** Windows, MASM spelling, -O2: **103 of 103 the same object from stages 0, 1 and 2,
  and 103 of 103 from stages 2 and 3** (36 minutes, stage 0's cl build included). Linux -O0: stage 0 103/103 in 199 s, stage 1 103/103 (one job
  cut at its 25 minutes at 91, resumed with SELFHOST_RESUME=1, 527 s more), stage 2 built; stage 2's compile was
  running at the hour. The -O0 stages compile slowly because each is a -O0 build of cxx3: a whole -O0 stage passes
  25 minutes on 2 cores, so the job is split. Linux -O2 and Windows -O0 were queued behind them. Windows -O0's first
  try failed ("stage 0 did not build"): `self-host.cmd` stage 0 calls `msvc\build.cmd`, which relinks
  `cxx3-msvc.exe` - a self-host in the tree a suite is running from takes the compiler from under it (below).
- **R103, the clang oracle on Windows** (clang 19.1.5, BuildTools). `tools/windows/names-overload.cmd` cannot start
  on this box: it names VS **Community**'s LLVM directory and the box has **BuildTools**; the suites were run
  under Git's bash with BuildTools' clang. **overload.sh: 43 agreed, 1 differed** - `base-rvalue-reference`, which
  includes `<utility>`: clang -std=c++11 cannot read MSVC 14.44's STL (a box limit). **names.sh: 711 passed, 161
  failed** (872 cases; 83 first refused were the self-host relink above, re-run one by one: 58 pass). Of the 161:
  77 hit the box's header limit (no Linux or Darwin headers for clang; MSVC's STL at C++11), 54 of them with a
  difference as well; **45 show only extra `C1`/`D1` symbols on Itanium** (cxx1 emits the complete-object
  constructor or destructor where clang emits none); 4 are clang 19's new `TnT_` mangling of a non-type parameter
  of dependent type (`_Z3valIiTnT_Li9EES0_v` beside cxx1's `_Z3valIiLi9EET_v`); 35 are other differences (the
  `__cxx1_vec_del_*` helpers, Microsoft lambda names `$_0` against `<lambda_0>`, `??_R17` beside `??_R4`). Not the
  `.nonames` reasons only: the review's verify fails here, and a Mac or Linux clang must say which of the 45 and 35
  are new. **win-structure.sh: names 504 agreeing, 96 differing, 225 under a .nonames**; its layout and unwind
  checks cannot run on this box - `python3` in Git's bash is the Store stub, and with a shim the tools stop on
  `signal.SIGPIPE`, which Windows' Python lacks - so its "557 differ" is the tools failing, not a measurement.
### Lane r6d: R103 (names, overload, win-structure on Windows) and R110 (ADL), 2026-11-15, from 458f8974

- **What made the three suites run on Windows** (their home, the user's Q9): `tools/windows/names-overload.cmd`
  finds clang++ under whichever VS 2022 is installed (the box has the Build Tools; the script named Community's
  LLVM) and a real python (Git's bash finds the Store's `python3` stub first), and now runs win-structure too;
  win-structure takes `$PYTHON`, falling back to `python`; the four python tools guard `signal.SIGPIPE`, which
  Windows' python lacks; `ulimit -t` is quiet in Git's bash. A missing oracle is **NOT RUN, status 3** in all
  three - never rc 0; `make test` lets 3 through only without `REQUIRE_ORACLE=1`, which a gate sets.
- **R110 mended.** `Lookup::lookupKeys` takes every argument's type ([basic.lookup.argdep]/2; the two-operand form
  delegates to it), and no argument-dependent lookup is added where a block-scope function declaration of the
  name is live (/3; `Parser::blockFunctions_`, popped by `leaveScope`). Cases `adl-every-argument` (clang 1 2)
  and `adl-after-block-declaration` (clang 2; cxx3 answered 1 before), in tests/cases and tests/overload.
- **names.sh on Windows**: 712 passed / 161 failed at 458f8974 + the ADL mend; **731 / 143** at r6d. The 143 by
  case: box headers (clang refused: no Linux/Darwin headers, MSVC's STL at -std=c++11, a clang crash for
  arm64-darwin) 78; C1/D1 and other inline special members on ELF (clang's constructor aliases; emission, not a
  name) 44; runtime calls each compiler picks (`__cxa_free_exception`, `_Unwind_Resume`, `_Init_thread_abort`)
  5; clang 19's `TnT_`/`TpTn` 6; an unused internal constant or exit helper emitted 2; the exe busy under my own
  winpass 3 (all three passed in the first run). **Real, 5 cases, in tests/open/names-register.txt**: closures
  met during a lambda's return deduction emitted as `$deduced_N` (2), and a lifetime-extended temporary's name
  (`?$S1@@3HB` where clang writes `?$RT1@r@@3AEBHEB`, Itanium `_ZGR2ir_`; 3). Moved out of "other" this round:
  30 .nonames rows citing reasons already recorded (static locals, closures, the unnamed namespace, RTTI emitted
  where the class is defined, cl's `_TI2CPEAD` - measured with cl 19.44, clang writes `_TIC2PEAD`); and
  tools/mangled-names drops file-local `__cxx1_vec_*` / `__cxx_global_array_dtor` and memset/memcpy/bzero
  (48 rows differed by these alone).
- **win-structure** at r6d (python working; it was 557 of 747 layouts "differing" under the Store stub): layout
  749 compared, **0 differ**, 12 under a recorded names difference; names 528 agree / 53 differ / 246 .nonames - the
  53 are MSVC's inline UCRT stdio alone 42, and the 11 the names classes above hold; unwind 0 faults but **8
  `dispUnwindHelp 0`** in three functions whose try cannot throw (nested-parse-state -O2, ssa-ms-try -O0/-O2,
  ssa-try-catch -O0, both spellings) - registered in tests/open/names-register.txt, not mended.
- **overload.sh** 45 agreed / 1 differed (base-rvalue-reference: clang cannot read MSVC's <utility> at -std=c++11).
- **Gates at r6d**: run.sh on Linux (g++ -Werror build, the new box) 1223 passed / 0 failed; Windows self-compile
  103 of 103 by stage 0; exclusions 0 uncited / 0 stale (three citations re-pointed), comment cap 0 over, memory
  gate and ast-residue not above their baselines, ide/generate.py --check clean; cl /W4 /WX build clean.
  winpass -O0 started at one lane beside the suites and had not finished at the hour - owed to the wave gate.
  src/: +25 -7 lines (Lookup, Overload, Parser.h/.cpp, ParserClass).
## The M6c review's mends - lane r6e (R108, R117, R119; four wrong answers of the sim6747 leg), 2026-11-15

From main 458f8974, branch r6e. Every number below is the Windows box's (cl-built cxx3, ASM6x, LNK6x, VM6747, SIM6747 1.12,
TI's rts6740_elf_eh.lib); nothing ran on Linux or the Mac.

- **Three wrong answers, one root: the selector wrote `EXT`/`EXTU` across the register files.** `selectInst`'s
  SExt/ZExt read its source through `x(0)` (the A side) and wrote the result's home, which M6b round 3 may place on
  the B side: `EXT A4, 24, 24, B26`. The constant form of EXT/EXTU has no cross path, so no unit encodes it; VM6747
  ran the text and asm6x refused it (inheriting-constructors, temporary-member-call, pipelined-data-exit at -O1 and
  -O2 - where `forwardMoves` could fold the copy in front it had hidden the fault). **Mend** (`C6xSelect.cpp`): the
  source is read at its home where that is the result's side, through A4 for an A-side result, and for a B-side
  result of an A-side source copied first and widened in place. A first mend through `ssaPut` was withdrawn: it was
  legal but changed over 70 outputs of other cases for the worse (identical.sh, stopped there). **ssa-layout at
  -O1 was a fourth** (`EXTU A4, 24, 24, B26` in `hashes`), found by identical.sh: no leg had assembled it at -O1.
- **The class is caught in the compiler now**: `c6xSchedule` asks `crossings` of every instruction not yet placed,
  as read and again before `packetise`, and the function is refused by name - "the C6000 instruction '...', which
  no unit encodes". The suite at -O2 passes through it with 0 refusals. The sim6747 leg (asm6x) stays the second
  witness, and it must run at -O1 as well as -O2: ssa-layout's fault was -O1's alone.
- **long-double-literal-exact-digits is TI's printf, not cxx3 and not VM6747**: the data is exact (`.word 0,
  1139802112` is 2^64) and the case's last line prints 1; rts6740's `%.17g` writes `1.8446744073709557e+19` where
  the correctly rounded digits are `...552`. cl6x 7.4.4's own build of the same five `printf`s, linked with rts6740,
  prints the same five lines on sim6747. Entered in tests/tms6747-sim.txt (rts6740 only). Not run under CCS's tisim.
- **R108 by Q5 (the user's): a function the stream cannot take is emitted unscheduled and counted.** `c6xSchedule`
  returns the serial text with a reason in `declined` at each of its four checks; `Tms6747` writes `decline
  c6x-unscheduled <function>: <reason>: <entry>` under CPP11_DECLINES and compiles on. `pipelineLoops` declines one
  loop (`decline c6x-pipeline-unmodelled`) and no longer the function. **tools/c6x-declines** counts both over
  tests/cases at -O1 and -O2 against tools/c6x-declines.baseline, which only goes down; `--check` also requires
  that `CXX1_C6X_UNSPELL=ADD` (the debug knob: one mnemonic taken as unread) declines something, so the path runs.
  The serial text is right as it stands: shard 1/8 of the suite at -O2 under the knob passes on both machines.
- **R119**: the serial text is composed only where it is returned (-O0, or a decline). **R117**: said beside
  `addSerial`'s test - nothing writes a NOP that is meant. **R118**: not touched.
- **R114 probed, not mended**: globals `A4`, `A10` and `extern "C" B3` break at every level, -O0 too - `MVKL A10, A4`
  is refused by asm6x and by vm6747, and at -O1 the new check refuses `MVKL A4, B11` by name. cl6x spells the symbol
  `||A4||`; whether ASM6x and VM6747 read that spelling is the first question of the mend. Owed.
- **Gate, on Windows, at the lane's tree** (src/ 94,210 -> 94,256 lines, +80 -34): the five cases (the three, the new
  tests/cases/c6x-ext-home-side, long-double) pass at -O0, -O1 and -O2 on vm6747 and sim6747. tms6747.sh SIM=1 -O2:
  **819 passed / 0 failed** (asm6x and lnk6x take every one; 6 in tms6747-sim.txt), 192,037,675 cycles over 816
  cases; **-O1: 819 / 0** on both machines. identical.sh ARCHS=tms6747 against main: 2619 compared, **9 differ, all named** - inheriting-constructors,
  temporary-member-call, pipelined-data-exit and the new case at -O1 and -O2, ssa-layout at -O1; -O0 0 differ.
  Cycles of the five (vm6747 -c, -O2, main / lane): 654 / 654, 202 / 202, 1,414,163 / 1,414,571 (+408: main's count
  is of text asm6x refuses), 22,793 / 22,793, new case 290 / 292. **tools/c6x-declines --check: 0 declines,
  baseline 0**, the knob declines 3 functions; shard 1/8 at -O2 under `CXX1_C6X_UNSPELL=ADD` 105 / 0 on both
  machines. Walker (CXX1_C6X_WALKER=1) -O2 SIM=1, the Windows half: shard 4/6 136 / 0; 5/6 and 6/6 were running at
  the lane's hour (boxkit/r6e-handover.md has their numbers). Self-compile of the six C6x files changed, -O0 and -O2: 12 / 0. cl /W4 /WX clean; exclusions 129 sites
  0 uncited 0 stale; comment cap 0 over; memory gate's files untouched (the tool does not run on Windows: SIGPIPE).
- **Not run in the lane** (the wave gate's): tools/c6x-cycles (lane r6b's, not merged here), the Linux half of the
  shards (k=1-3 of 6), -O0 whole suite, ti-link.sh. tools/wave-gate does not call tools/c6x-declines yet: its
  owner adds `tools/c6x-declines --check` beside the census.
## The user's decisions on the M6c review, 2026-11-15

The review (Fable 5.2, `fable-m6c-review.md`, read at `458f8974`) asked nine questions; these are **the user's answers,
2026-11-15**, relayed by the coordinator and written here by lane r6g. None is a lane's or the coordinator's choice.

- **Q1 (the user, 2026-11-15):** wave 18's seven `<map>`/`<set>` cases (+11,926 cycles) are **accepted as a library
  cost**; M6c stays closed; the inliner's budget (the `rotateLeft` in `rebalance`) is owed (the owed table).
- **Q2 (the user, 2026-11-15):** **confirmed** - the walker (`CXX1_C6X_WALKER=1` and its legs) stays until M6d, and
  **M6a.2 (R101) comes before M6d**.
- **Q3 (the user, 2026-11-15):** release-check **keeps its 16 KB stack** (`tools/c6747/release-check.cmd`; the heap
  stays 2 KB).
- **Q4 (the user, 2026-11-15):** **sim6747 runs in every wave gate** (`SIM=1`, sharded); the time it takes is accepted.
  (Since the same day's Windows split it runs on the Windows box, once SIM6747 is built there.)
- **Q4b (the user, 2026-11-15):** target 4's self-host **may run on the Mac at every milestone close**: one job, through
  `boxkit/mac-run.sh`, under the caps; never in a wave. Written also in `M5-PLAN.md`'s "The Mac runs".
- **Q5 (the user, 2026-11-15):** a function the scheduler cannot spell is no longer a compile error: **emit the
  unscheduled text and count the decline** (R108); the decline count is a **ratchet that only goes down**.
- **Q6 (the user, 2026-11-15):** M8's early sibling substitution is **kept as a stand-in, off by default**, and deleted
  when M8.2 lands. Written in `M8-PLAN.md`.
- **Q7 (the user, 2026-11-15):** the five raises of the Ast.h residue's baseline are **accepted as recorded**; from now
  on it **only goes down**, and **any raise needs the user's word**.
- **Q8 (the user, 2026-11-15):** **M6b is accepted as built** (homes picked in `ssaAssign`, no allocator rewriting the
  stream); the allocator's rounds - 1X/2X per turn, `.M` pressure, the rewrite of `MOperand::reg` - are **planned as
  owed, after M6d** (the steps table's M6b.2).
- **Q9 (the user, 2026-11-15):** **the Windows box is the standing home of names.sh, overload.sh and
  win-structure.sh in every wave gate**; nothing is installed on the Linux box.
- **The order (the user, 2026-11-15):** all the review's mends (Fable reworks; at most two Fable lanes, three lanes) ->
  one gate (`tools/wave-gate`) -> land -> a short mend check -> **M6a.2** -> **M6d**.

## M6a.2's rounds (planned 2026-11-15 by lane r6g from `458f8974`; the review's R101)

**The measure** (`CENSUS_SOURCES=0 CENSUS_FLAGS=-O2 tools/ssa-census -arch tms6747`, Windows box, 458f8974's cl
build, tests/cases' 1,227 files): **at -O2 the selector takes 6,992 of 11,199 functions; 4,207 are walked, in 866 of
the 1,227 cases**; at -O0 7,146 of 11,199 selected, 4,053 walked in the same 866 cases (m6-x 32d47563 read 4,149 and 3,996 over 868 files). By the selector's reason (`CXX1_SSA_WHY`, -O2):

| reason | -O2 | -O0 | round |
| --- | --- | --- | --- |
| landing pads | 1,329 | 1,329 | W1 |
| exception specification | 31 | 31 | W1 |
| variadic call | 860 | 735 | W2 |
| variadic (the callee) | 16 | 16 | W2 |
| class result | 418 | 418 | W3 |
| class or 64-bit parameter | 398 | 398 | W3 (class), W4 (64-bit) |
| class result of a call | 65 | 62 | W3 |
| class argument | 36 | 35 | W3 |
| aggregate value | 2 | 28 | W3 |
| floating value | 246 | 267 | W4 |
| floating or 64-bit result | 178 | 178 | W4 |
| 64-bit value | 126 | 125 | W4 |
| `smulh` | 20 | 0 | W4 |
| `memcpy` | 215 | 203 | W5 |
| `memset` | 103 | 92 | W5 |
| indirect call | 140 | 112 | W6 |
| outside the SSA lowering | 24 | 24 | W6 (R1's) |
| **walked** | **4,207** | **4,053** | |

**The ratchet.** `tools/c6x-walked.baseline` holds the walked count at -O0 and -O2 (458f8974's); `tools/ssa-census
-arch tms6747` prints the count against it after its own rows and fails when it rises; when it falls, it says so
and the round lowers the baseline in the same commit. `CENSUS_SOURCES=0` counts tests/cases alone (about two minutes
a level on the Windows box at three jobs; the full census adds cxx3's own sources at tms6747, past 25 minutes).
M6a.2 is done when both lines read 0 - M6d's licence. Run once against the new file (Windows, work\r6g, -O2): "walked 4207 functions in 866
of 1227 cases; baseline 4207".

**The rounds**, largest reason first, the pads their own round (each about one lane; a round that cannot finish its
reason takes the largest shape of it and names the rest):

| round | takes | the shape | its own judge, beside the common one |
| --- | --- | --- | --- |
| W1 | landing pads, exception specifications | the selector writes pads from `ir::Function::landingPads`, the call-site rows and TI's unwinder tables (`.c6xabi.exidx`/`.extab`, as the walker's `-O0` text writes them) for a selected function; a function with a handler is selected | TI's linker and runtime unwind it (`tests/ti-link.sh`, release-check); the six tms6747 `.notarget` lines of the owed table removed as their functions are selected; `tests/open.sh` names the `walker-*` entries that agree, and they move to tests/cases |
| W2 | variadic calls | arguments past the named ones on the stack as TI's C6000 EABI places them; `va_*` in a selected variadic function | cl6x's `-S` of the same calls (the ABI's oracle); a selected caller linked with a cl6x-built callee |
| W3 | class values | a class result through the hidden pointer (A3), a class argument by its copy's address, a class result of a call, aggregate values | the same cl6x oracle; the lifetime ledger 0 in each case it selects |
| W4 | 64-bit and floating values | register pairs (A5:A4 and the rest), `long long` and `double` arithmetic and compares, the C674x's floating instructions, TI's runtime helpers where cl6x calls one, `smulh` | cl6x's `-S`; sim6747 results equal the walker's in every case that prints a double |
| W5 | `memcpy`, `memset` | the copy and the fill as calls into TI's runtime, or inline where cl6x inlines them | cycles per case not worse than the walker's copy loop |
| W6 | indirect calls; what remains | a call through a register (B3 as the return); the 24 bodies outside the SSA lowering go with R1's rounds, which own the lowering's reasons | the census: 0 walked at -O0 and -O2 |

**The common judge, every round** (the steps table's "as M6a", made exact): tests/tms6747.sh at -O0, -O1 and -O2 with
`SIM=1` (Q4), sharded, 0 failed; asm6x.sh 0 failed; `identical.sh ARCHS=tms6747` names each changed file, and every
change is a function the round newly selects; cycles case by case (`tools/c6x-cycles`) equal or better, or named;
the walker legs (`CXX1_C6X_WALKER=1`) 0 failed while the walker stands; the census below its baseline, the baseline
lowered in the round's commit.
### The M6c review's lane H (R111, R112, R113, R115, R120), 2026-11-15, lane r6h
- **R111 + Q7** (the user's): the five Ast-residue raises of `434bdf8a..458f8974` are accepted as recorded, listed in
  the new `tools/ast-residue.start` (M6's start, written by the coordinator, never inside a milestone). `--check`
  prints each count's delta from it and refuses a baseline line above it without the mark
  `user-approved YYYY-MM-DD <why>`; `--record` refuses any count above the baseline. Only the coordinator writes the
  mark, on the user's word.
- **R112**: `tools/exclusions --check` prints the refusals of valid C++11 against their baselines - outside() sites
  (only down), arm64-darwin's own `unsupported()` sites (new key `arm64`, `--arm64` lists them) - and how many owed
  reasons a case reaches, from `tools/exclusions.reached` (lowering-why.py now takes the executable, so Linux measures
  it). At this lane: 554 outside() sites (baseline 554), 477 reasons, **20 reached by a case, 457 not**; arm64's own
  4 (baseline 4). The reasons themselves stay owed to a standing lane after M6d, as the review says.
- **R113**: answered by Q8 (M6b accepted as built; the allocator rounds 1X/2X, `.M` pressure and the rewrite of
  `MOperand::reg` owed after M6d - lane r6g's row). Named here as the exception it is: `c6x::MInst` is a second MIR
  beside the optimizer's `Mir*` (design section 4, "one optimizer") until those rounds or M6d fold it.
- **R115**: CLAUDE.md's status pointer names M6-PLAN; its gates line names the residue rule (Q7), its boxes the split
  and Q4b/Q9; five dated sections written into docs/CLAUDE-history.md and the index brought to them.
- **R120**: arm64's census row counts the functions offered (`ssaOffered_`) and is printed before a refusal, so it
  can show one not selected (the format `ssa-census` reads is kept).
- **Gate (Linux, the lane's tree)**: make rc 0; exclusions, comments (0 over), memory gate, ast-residue all rc 0,
  ast-residue +0 on every kind against the start; an unmarked test raise (baseline ExprPtr 2017) refused rc 1, the
  same line marked accepted, `--record` over a lower baseline refused rc 1; run.sh 1221 / 0; self-compile 103 / 103.

### Lane r6w - the wave gate balanced over both boxes, 2026-11-15 (the user's split, second form)

From r6a 7ee2291a, r6b merged (for `tools/c6x-cycles`). Only `tools/wave-gate`, `tools/verify-three` (one line) and this
entry changed; src/ lines before and after: the same.
- **One table, `BOXTAB`**, at the top of the script: every step's box - `linux` (what runs x86_64-linux code, -j4,
  JOBS=4), `windows` (winpass, cdb, the self-host, names/overload/win-structure under `REQUIRE_ORACLE=1` per r6d,
  release - by hand), `split` (tms0-2, wtms0-2, sim: SHARD=k/6, k=1-3 on Linux and 4-6 on Windows, at once) and
  `idle` (identical ARCHS=tms6747 against ~/w19g's main build, emit, census -arch tms6747 -O2, declines (r6e's R108),
  asm0/asm1/asm2 (r6e: -O1 too), cycles - each claimed by an atomic mkdir by the first leg free, the report names it).
  `-B step=box` or WAVEGATE_BOXES overrides a row; `-t <commit>` gates another commit's tree with this script; `-p`
  prints each box's list. subst (r6f's tests/subst.sh, R109) is a Linux step. A split step's verdict is the join of
  both halves (counts summed, either half failing fails it); cycles waits for both tms2 halves, then judges the two
  boxes' cycles.txt concatenated (R106's tool and baseline copied beside it when the gated commit predates them).
- **Disk**: every verdict written while its box is above 70% full is a FAIL (Linux 29%, Windows 66% at the start).
- **The run, `tools/wave-gate r6w -t 458f8974`** (-n fixed, mac, winpass0/2): **38 minutes** wall for the main
  driver, 48 for the whole run with the Windows halves' rerun (18:02-18:50), against r6a's 44 for 9 verdicts;
  here **19 passed, 7 failed, 14 not run, 0 without a reason** (report `~/cxxopt-build/wave-gate/r6w-report.txt`).
  - Linux PASS: build, static, gdb 5/5, run1 1221/0, maketest rc 0, selfc 103/103; idle on Linux: identical
    1744 compared 0 differ, emit (no golden on the box), census outside=0, asm2 837/0.
  - Windows PASS: wbuild, winpass1 832/0 (masm 831/0), cdb 5/5 x2, wselfc 103/103; idle on Windows: cycles 815
    cases, 808 equal, 7 worse all owed or accepted, 0 missing - the joined file from both boxes' tms2 halves.
  - Split, joined: tms2 818/0, wtms0 818/0, wtms1 818/0, wtms2 818/0 (407 + 411 each).
  - FAIL, found: **sim joined 814 passed, 4 failed** - temporary-member-call and inheriting-constructors ("EXT's
    source and destination are on one side"), pipelined-data-exit (EXTU, the same), long-double-literal-exact-digits
    (sim6747's output differs) - the first three are r6e's c6x-ext-home-side; names 161, overload 1, winstruct 557
    (R103, r6d's, as r6a found).
  - FAIL, this lane's own faults, not the compiler's: tms0's and tms1's Windows halves - the first Windows half
    failed for want of tools/wave-gate in a commit before r6a (wbuild now copies it); the rerun's first shard was
    counted with orphans of a stopped run (494 for 411) and its -O1 half met "Device or resource busy" spawning
    cxx3-msvc.exe while the first driver ran in the same tree. asm0 on Windows: python3 is the Store's stub there
    (837 failed); a shim to Python312 now comes first. Owed: tms0/tms1 Windows halves and asm0 again, alone.
- **Owed**: the walked ratchet has no recorded value (WAVEGATE_WALKED); emit has no golden on the boxes; two
  drivers in one Windows tree collide - one driver a tree.
## The M6c review's mends

### R106: M6's judge in the tree - `tools/c6x-cycles` and `tests/tms6747-cycles.txt`, 2026-11-15, lane r6b

From main 458f8974. The review's R106: "cycles equal or better, case by case" was a hand comparison between files
in the Linux box's home (`~/cyc-m6ae.txt`, `~/cyc-w19g.txt`). No src/ line changed (src/ lines before and after: the same).
- **The tool.** `tools/c6x-cycles` runs `tests/tms6747.sh`'s shards (CYCLES=1; SHARDS=3; LEVELS="-O2 -O0"), keeps each
  level's counts in `tests/out-c6x-cycles/cycles<level>.txt` and compares them case by case with
  `tests/tms6747-cycles.txt`: one line for every case that is not equal (`better`, `worse`, `WORSE`, `new`, `MISSING`,
  `STALE`), then the counts and both totals. It fails on a case worse and neither owed nor accepted, on one above its
  cap, on a baseline case that gave no count, on an `owed`/`accepted` line no longer needed, and on a shard that
  failed. `--record` writes the baseline (a column not run keeps what it held); `--check <cycles.txt> [level]`
  compares a collected file and runs nothing; `--baseline <file>` takes another baseline, a collected file included.
- **The baseline's form.** A row a case, a column a level; `measured <column> <commit> <date>`; and
  `owed|accepted <case> <column> <cap> <reason>` - the column holds the count the case stands above, the cap is how
  far. **Q1 (the user, 2026-11-15): wave 18's seven are accepted as a library cost** - their -O2 column is round
  22's count, their cap wave 19's, the cause "the inliner's unit budget declines rotateLeft in rebalance".
- **Q4 (the user): sim6747 in every wave gate.** SIM=1 runs tms6747.sh's sim6747 leg and judges sim6747's own count
  (`sim6747 -c` on the linked TI program) as the columns `<level>-sim`; SIM=0 says "NOT RUN" aloud. The hook in
  tests/tms6747.sh: with CYCLES=1 the sim leg passes `-c`, cuts the count into `<case>.simcycles` and collects
  `cycles-sim.txt`.
  The hook is behind its own switch (SIMCYCLES=1, set by the tool), so the sim leg every other caller runs is the
  line it was. **Owed: the hook has not run once and no `-sim` column is recorded** - the Linux lock was held past
  the lane's hour, and the user's split of 2026-11-15 sends SIM=1 to the Windows box, where SIM6747 is not built yet
  (boxkit/windows-split-2026-11-15.md says how). First step there: one case with CYCLES=1 SIM=1 SIMCYCLES=1 to see
  that `sim6747 -c --run` prints the CYCLES line, then `SIM=1 tools/c6x-cycles --record`.
- **Checked against the box's files** (Linux, `--check`, nothing run): wave 19's `~/cyc-w19g.txt` against round
  22's `~/cyc-m6ae.txt` as the baseline: 815 cases, 811 common, **804 equal, 7 worse, 0 better**, 4 new, 192,009,106
  -> 192,021,032, exit 1 with the seven named. **The plan's "802 equal" (wave 19's entry, and this lane's brief) is
  a slip**: 802 was 811 less the nine before sh-2; with seven above it is 804, and 804 + 7 = 811. Against wave
  18's `~/cyc-w18g.txt`: 805 equal, 10 better, 0 worse (192,802,451 -> 192,036,975).
- **The baseline, recorded by the tool itself** (`COMMIT=458f8974 tools/c6x-cycles --record` on the Linux box, vm6747,
  SIM=0, in ~/r6b: wave 19's tree and cxx3.exe, its src, include, lib, tests, tools and Makefile hash-equal to
  458f8974's blobs): 818 passed, 0 failed at each level; **815 cases; -O2 192,036,975 cycles** (wave 19's total
  exactly), **-O0 397,274,101**. With the seven's column set to round 22's count the -O2 column sums to 192,025,049.
  The tool on its own collected files: -O2 808 equal, 0 better, 7 worse (7 accepted), 0 new, 0 missing, exit 0;
  -O0 815 equal, exit 0; on wave 19's file the same line. A case raised by hand (+5) and an accepted case one cycle
  past its cap: both `WORSE`, exit 1. Round 22's own file: 811 equal, 4 missing, 7 stale, exit 1.
- **Left**: the `-sim` columns (above); `tools/wave-gate`'s `cycles` step is lane r6a's to call
  (`tools/c6x-cycles`, or `SIM=1 tools/c6x-cycles` once the columns exist); the baseline was taken on Linux and
  each `measured` line says so - a Windows run compares against it (the two vm6747 builds agreed cycle for cycle
  at the qualification).
- **Static gates** (the Windows box, work\r6b, affinity 3FF, the lane's tree): tools/exclusions --check 129 sites, 0
  uncited, 0 stale; comment-lines 0 groups over the cap; ide/generate.py --check up to date. tools/memory-gate does
  not start on Windows (`signal.SIGPIPE`, tools/memory-gate:30 - a defect for the split); no src/ file changed, so
  its count is wave 19's.

## The M6c review's mends - lane r6i (R114, R118), 2026-11-15

From r6e f56adfed, branch r6i, with r6b merged for `tools/c6x-cycles`. src/: +31 -9 in five files.

- **R114: a symbol spelled like a register is written `A4+0` wherever an operand or a data word names it.** cl6x
  writes `||A4||` (measured, 7.4.4: every `A`/`B` with digits, `SP`, `DP`, `FP`, `PC` and the control registers, in
  either case). The project's ASM6x has no such quoting - `.global ||A4||` is "names symbols", a label `||A4||:` is
  "|| needs an instruction just before it", an operand "a value expected, not '||'" - and it is not this lane's to
  change. What ASM6x 1.5, VM6747 and LNK6x all take (probed, work\r6i-q): the bare name at `.global`, at the label,
  in `.ref`, inside `$EXTAB_SCOPE(...)`, `$EXIDX_FUNC(...)` and `DW_AT_low_pc(...)` (they refuse `A3+0` there), and
  `name+0` in an instruction's operand and a `.word` - an expression, so no reader takes it for a register, and the
  relocation is the same symbol with addend 0. One function says which names (`c6x::symbolOperand`, C6xMir.cpp, beside
  `regNumber`); `Tms6747::movSym`, both `call` sites (the walker's used to pass a function named `B1` as the register),
  the two `.word` writers and the selector's call go through it; `registersIn` (the scheduler's and `onOperands`'
  reader) skips a register name followed by `+0`, so liveness and renaming do not see one and the table reads the
  instruction; `tiExternals` gives such a name its `.ref`. **Owed to ASM6x: `||name||`**, then this spelling can be
  cl6x's.
- **The case** `tests/cases/c6x-register-named-symbols` (objects A4, A31, a4, B31, a static B0, a pointer to function
  A0; functions B3, A10, and A3 with a `try`), its .expected clang's (the Windows box's, 19.1.5).
- **R118: `entryOf` answers "an instruction not read" (`is` 2)** for a tab line whose predicate has no `]` and for
  one whose first word is neither a capital, a directive's dot nor a comment - the scheduler declines such a function
  by name as it does a line it cannot spell, where it used to keep the line and leave it out of liveness.
- **Gate.** The case at -O0, -O1, -O2 on vm6747 and sim6747 (asm6x, lnk6x), the walker at -O2 and `-g`: passes on
  Linux; tests/ti-link.sh (TI's lnk6x 7.4.4, Linux) 833 linked, 0 failed, at -O0 and at -O2. tests/tms6747.sh SIM=1 (vm6747 and
  sim6747): shards 1-3/6 on Linux 413 passed 0 failed at each of -O0, -O1, -O2 and under the walker at -O2, and SIM=0
  -O2 the same; shards 4-6/6 on Windows 407 passed 0 failed at each of -O0, -O1, -O2, under the walker at -O2 and
  at SIM=0 -O2, and the same shards on Linux as well, 407 / 0 at every leg. identical.sh ARCHS=tms6747 against r6e's
  build (Windows): 2622 compared, 6 differ, all named - the new case at three levels (r6e's build writes it wrong)
  and array-list-default-args at three (its global `sp` is now `sp+0`, 20 lines at -O0; its cycles are equal).
  tools/c6x-cycles --check over the six -O2 shards: 817 cases, 807 equal, 0 better, 8 worse - 7 accepted, and
  pipelined-data-exit 1414163 -> 1414571, which is r6e's own count (work\r6e-g-cycles-O2.txt), not this lane's; 2 new.
  tools/c6x-declines --check: baseline 0 holds. Self-compile of the four changed C6x files at -O0 and -O2: 8 / 0.
  run.sh on Linux at -O0 and -O2: 1223 passed, 0 failed; the case on x86_64-windows in both spellings at -O0 and -O2
  agrees with clang. cl /W4 /WX and g++ -Werror clean; exclusions 129 sites 0 uncited 0 stale; comment cap 0 over;
  memory gate holds; ide/generate.py up to date.

### Wave 20 landed - the M6c review's mends, 2026-11-15 (the coordinator)

The review's lanes r6a (tools/wave-gate, the -O1 leg, the fixed-point step), r6b (tools/c6x-cycles, the baseline),
r6c (the runs never run: sim6747, ti-link, release-check, the fixed point, the clang suites on Windows), r6d (the clang
suites run on Windows, R110 ADL), r6e (four wrong answers: the EXT home side; R108 per Q5, the decline count),
r6f (R109, tests/subst.sh), r6g (the user's decisions in the plans, M6a.2's rounds, the walked ratchet, the owed
table), r6h (the residue's user mark, the refusal ratchet, CLAUDE.md's pointer and the history), r6i (R114
register-named symbols, R118), r6w (tools/wave-gate over both boxes); one residue raise in the merge removed.
- **Gate: tools/wave-gate w20g at e91cacd4** - 30 passed, 5 failed, 5 not run (each with its reason). Linux: build,
  static, gdb 5/5, run1 1226/0, make test rc 0, subst, self-compile 103/103. Windows: cl /W4 /WX, winpass -O0/-O1/-O2
  837 / 0 and masm 836 / 0, cdb 5/5, self-compile 103/103. Joined over both boxes: tms6747 -O0/-O1/-O2, the walker
  at all three and sim6747 -O2, each 823 passed / 0 failed. asm6x 842 / 0 at -O0/-O1/-O2; declines baseline 0
  holds. Mac: run.sh 1224 / 0 at -O0 and -O2, lldb 5/5. **release-check by hand on Windows: PASS, 34 programs, 226 s.**
- **The five failures, each explained:** names 141 / overload 1 / win-structure - the classified set, owed to lane
  r6n; identical 12 - all intended (the new cases adl-*, c6x-ext-home-side, c6x-register-named-symbols, and the
  mended inheriting-constructors, pipelined-data-exit, temporary-member-call, array-list-default-args); cycles 1 -
  template-subst 448 -> 949, its source widened by r6f, re-recorded (f9e35d30). pipelined-data-exit +408 accepted
  by the user as the correct-code baseline (79b00a6e).

## The mend check's N1-N5 in tools/wave-gate - lane g1, 2026-11-15

Lane g1 (Opus), branch g1 = `82bd61a9` + r6n `827e85f4` + r6x `4ecec004` (`~/slot`, the Windows pool). No src/ change.
**The user, 2026-11-15, QA: a wave lands only when tools/wave-gate shows 0 failed, every expected difference named in
a file the gate reads; nothing explained by hand.** QC: register-named symbols stay `A4+0`.
- **N1** `census`: tools/ssa-census's own status at -O2 (outside= and walked) and -O0 (walked, CENSUS_SOURCES=0);
  both walked lines in the verdict against tools/c6x-walked.baseline; `WAVEGATE_WALKED` gone. A cut is rc 124, a FAIL.
  **Main's walked count at `82bd61a9` + r6n (Linux): -O0 4070, -O2 4224 - above 4053 / 4207 by 17 each**, and none of it a
  function that left the selector: the five cases wave 20 added walk 13 (adl-every-argument 4, template-subst-refused
  5, c6x-register-named-symbols 2, adl-after-block-declaration 1, c6x-ext-home-side 1) and template-subst, widened by
  r6f (32 to 46 lines), the other 4 (by difference - no per-case count was kept at `458f8974`). Re-recorded with that
  reason in the file. A per-case baseline (the check's fix) is left: owed to M6a.2's W1, beside its own re-record.
- **N2** `-w <base>` names the wave's base (main at its start); `build` builds it beside the gated tree on Linux in
  the other slot. `identical` (tms6747) compares against it at -O0, -O1, -O2 and passes only when the differing set is
  exactly `tests/wave-expected.txt`'s rows (`<case> <target> <level> <reason>`) - an unnamed difference and a named one
  that did not differ both fail. `cycles`: a WORSE row passes only for a case named there at `tms6747 2`; owed and
  accepted rows stay in tests/tms6747-cycles.txt, which the gate reads. `baselines` (a report step): the judges' files
  at the Mac's HEAD must be the gated commit's, or the report fails.
  `cycles` held by hand on Linux (its box side, w20g's joined -O2 cycles.txt, 820 cases): as measured **PASS** (807
  equal, 8 accepted, 5 new, 0 WORSE - template-subst's re-record holds); access-through-base raised 132 -> 5132
  **FAIL** "WORSE and NOT NAMED: access-through-base tms6747 2"; the same with that row named **PASS**.
- **N3** `identlnx`, `identwin` (both spellings), `identa64` - the same judge over the other targets; `ssa1`, `ssa2`
  (tests/ssa.sh at SSA_LEVEL 1, 2); `run2` (run.sh -O2 on Linux).
- **N4** the fixed point is owed when `-m <milestone>` is given, or base..commit adds or deletes a src/ file or takes
  100 src/ lines away net; else its four steps are NOT RUN with that check as their reason. `-n fixed...` is refused.
  **The fixed points on record:** Windows MASM -O2 at `458f8974` (r6c, entered above); **Linux -O0 and -O2 at r6c's
  tree, read now from `~/r6c-sh0c.log`, `~/r6c-sh2c.log`: 103 of 103 the same from stages 0, 1, 2 and from 2, 3 at
  each level** (finished 13:01 and 13:15 box time). **Windows -O0: not on record** (`work\r6c-sh0.log` has no
  comparison line); owed before M6d deletes a path.
- **N5** `emit`: the base records the -O2 golden in the run, the gated tree is compared to it, changed files judged
  by the same expected file; no golden is a FAIL. `release`: tools/c6747/release-check-box.py on the Windows box at
  /affinity 3FF under the 25-minute cap, PASS only on its "PASS in" line - no standing excuse.
- **The gate, `tools/wave-gate g1b -w 82bd61a9` at `906a313f`** (17 minutes): **17 passed, 0 failed, 30 not run, 0
  without a reason.** PASS: build (and the base), static, run2 1226/0, ssa1 and ssa2 899/0, identical 2631 / identlnx
  2631 / identwin 5262 / identa64 2631 compared, 0 differ, 0 named; emit 0 of 842 changed against the base's golden;
  census outside=0, -O2 4224, -O0 4070 at their baselines; wbuild; **names 877/0 (20 OPEN rows owed), overload 46/0,
  winstruct layout 844/0** - r6n's rows make all three green; release PASS 368 s; baselines. NOT RUN: the four fixed
  points (not owed, checked); cycles, the tms/wtms/sim shards, winpass, cdb, the selfc pair, asm, declines, gdb, run1,
  maketest, subst - steps g1 did not change, not re-run in the hour, owed by the next wave gate; the Mac's four.
- **The deliberate failure**: the same `identical` step (the box side of the script, in g1b's tree) with wave 19's
  build (`~/w19g`) as the base and nothing named: **FAIL - 21 differ, 0 named, NOT NAMED: adl-after-block-declaration
  tms6747 0, ...** (the cases and C6000 mends wave 20 made).
## M6a.2 round W1 - landing pads through the C6000 selector, 2026-11-15 (lane w1, m6a2-w1 from wave20 79b00a6e)

**What changed** (`a3134937`; src/ +53 lines, 94,324 -> 94,377; the baseline lowered in `dc878fb5`):
- `Tms6747::ssaOutside` no longer leaves a function with Itanium pads, or a `noexcept` one, to the walker. A pad is
  taken where it has the exception and the selector as its two parameters and a `catchpad`/`cleanuppad` first; an
  invoke is checked as a call is. `throw(T...)` stays walked by name ("exception specification": the lowering's
  filter has no row here) - none is left in tests/cases under that reason.
- **An invoke is one call-site row** (`ssatry`/`ssatryend` round the call, in address order, disjoint), with its pad's
  chain as arm64's selector reads it; `emitExceptionTable` joins no rows of a selected body (`ssaRows_`): a row is
  one call, so TI's personality, which goes on after a cleanup from the next scope at the address that threw, finds
  none. **The pad's entry** (`ssaland`) stores A4 and the trampoline's B4 into the block's parameters and takes B15
  back, as `landingPad` does; nothing falls into it. **A pad that only ends the program** (a call of
  `_ZSt9terminatev` in its block) is cl6x's terminate scope, the walker's row (`0xfffffffe`, no landing) - with an
  honest landing vm6747 prints its own line where clang's answer has none (dtor-throws-unwinding).
- **A value live into a pad has no register**: it stays in its word, or is made again. The scheduler's liveness
  (C6xSched.cpp) has no edge from a call to a landing, so a register read only in a pad would be dead to it.
- **The lowering resumes by `Target::resumeTakesException`**: `__cxa_end_cleanup()` for TI, `_Unwind_Resume(e)`
  elsewhere (Lowering.cpp `resumeFrom`). vm6747 took `_Unwind_Resume`; TI's rts6740 on sim6747 did not (twelve
  cases read a null control block in `__TI_Unwind_Resume`) - the judge the plan named found it.

**The census** (Linux, g++ build, CENSUS_SOURCES=0, 1,232 cases): -O2 walked **3,527** (was 4,207), -O0 **3,313**
(was 4,053); selected 7,724 and 7,938 of 11,251. "landing pads" and "exception specification" read 0 at both
levels: of the 1,360, 680 are selected and 680 are now named by their next reason (variadic call 1,149, class
result 493, class or 64-bit parameter 413, floating value 308, memcpy 269, ... - W2 to W6's).

**Gates** (a3134937's tree). Linux: tests/tms6747.sh shards 1-3/6, SIM=1 and SIM=0, 410 passed 0 failed at each of
-O0, -O1, -O2, and under the walker at -O2; tests/ti-link.sh 836 linked 0 failed at -O0 and at -O2; asm6x.sh 842 / 0
at -O1 and -O2; tools/c6x-declines --check holds; identical.sh ARCHS=tms6747 against a build of 79b00a6e: 2,631
compared, 594 differ, in 202 cases, each named in the lane's log (a case with a function newly selected);
exclusions 129 sites 0 uncited 0 stale, comment cap 0 over, memory gate holds; g++ -Werror and cl /W4 /WX clean.
Windows: shards 4-6/6 SIM=1 at -O0 413 passed 0 failed; the rest of the Windows half as the handover records it.

**Cycles - the user's question, not accepted here.** Against 79b00a6e's own counts (Linux, vm6747, 820 cases): at
-O2 649 equal, **147 better, 24 worse**, 192,038,836 -> 191,958,981; at -O0 640 equal, **163 better, 17 worse**,
397,279,749 -> 395,942,111. The largest worse: -O2 array-loop-throw +72,173, constructor-unwinds +31,247,
array-loop-local +15,141, map-set-node-stability +11,356, ssa-catch-by-value-throw-out +10,317; -O0 local-in-handler
+39,403, ssa-catch-by-value +38,366, local-try-states +38,027, ssa-catch-by-value-throw-out +37,932. The -O0 cause
was read: a wrapped body's blocks were written in `f.blocks` order, so a selected `noexcept` or pad function jumped
to its body and back (two `B` + `NOP 5`, 12 cycles a call; tests/cases/noexcept's `quiet`). **Mended in the lane's
last commit**: `ssaBlockOrder` at -O0 for those functions alone - -O0 then reads 640 equal, **175 better, 5 worse**
(397,279,749 -> 395,759,751): the four catch-by-value cases above, each still +38,000, and template-class +3. That
commit is gated on Linux only (tms6747.sh SIM=1 -O0, all six shards, 823 passed 0 failed; comment cap, memory gate);
it changes -O0 text alone, and the Windows build and gate above are of the commit before it. The -O2 cause and the
four +38,000 are not read (the tables are shorter than the walker's, so it is not the scan). Two accepted lines went stale (return-self-referential,
map-move-only-mapped: no longer worse).

**What remains, by reason.** The six `.notarget` lines: handler-guards-nested now passes selected (vm6747 and
sim6747, -O0 and -O2) and still fails under `CXX1_C6X_WALKER=1`, so its line and tests/open/walker-handler-guards-
nested stay while the walker's leg stands; the other five still fail, their functions walked for W2-W6's reasons.
release-check's box half and the Windows shards at -O1, -O2 and the walker: see boxkit/w1-handover.md.

## M6a.2, round W2: variadic calls and variadic definitions selected - lane w2, 2026-11-15

From wave20 79b00a6e, branch m6a2-w2. src/: +33 -8 in three files (C6xSelect.cpp 788 -> 806, Tms6747.cpp 2481 -> 2486,
Tms6747.h 309 -> 311).

- **The ABI, from cl6x 7.4.4's `-S`** (`-mv6740 --abi=eabi`; the source is `boxkit/w2va.c`'s shape - printf, `take(1, 2,
  x, p, h, c)`, `only(c, x)`, `sum(int n, ...)`, `pick(const char *, short, ...)`): the named arguments before the last
  go in the registers as ever; **the last named argument is stored at `*+SP(4)` in its own size** (`STW`, `STH` for a
  short, `STB` for a char) **and every argument after it a word each from `*+SP(8)`**; the callee's `va_start` is the
  address past the last named one rounded up to a word (`ADD 8, FP` after its `STW FP, *SP--(8)`), `va_arg` an
  `ADD 4` and `LDW *-A(4)`. This is what the walker's `visit(Call)` and `vaStart_` already write.
- **The selector** (`C6xSelect.cpp`): `ssaOutside` no longer refuses a variadic function or a call of one; a call
  writes the last named argument and the rest as words above the reserved word (`openArea` with its stack arguments,
  `STW v, *+B15(4 + 4k)`), then the named ones before it into the registers; `VaStart` and `VaArg` are the walker's
  own text, split out as `Tms6747::vaStartAt` and `vaArgAt` (one implementation, the list's address in A4);
  `VaEnd` writes nothing. A variadic function's frame link, its stack parameter and `vaStart_` were the prologue's
  already (`emitParams`, `needFrame`).
- **What stays walked, by name.** A call whose callee is a member (`hasThis`), has no named parameter, or has more
  than ten keeps the reason "variadic call": **one function in tests/cases, `main` of `template-sfinae-fallback` at
  -O2** (`f(...)`, no named parameter - cl6x's placement of that shape was not measured). A variadic call with a
  double or a 64-bit argument is walked for W4's reasons (floating value, 64-bit value), a variadic function with a
  handler for W1's (landing pads); neither is counted here any more.
- **The census** (`CENSUS_SOURCES=0 tools/ssa-census -arch tms6747`, Linux, g++ build): **-O2 4,207 -> 3,383 walked
  (7,874 of 11,257 selected, 704 of 1,233 cases), -O0 4,053 -> 3,341 (7,916 selected)**; "variadic" 16 -> 0, "variadic
  call" 860 -> 1 at -O2 and 735 -> 0 at -O0. `tools/c6x-walked.baseline` lowered in its own commit (9f6b1c1b).
- **The case** `tests/cases/c6x-variadic-selected` (clang's `.expected`, the Windows box's clang over cxx3's library):
  four variadic definitions (a loop of `va_arg`, pointer and unsigned extras, `va_copy`, five named and nine extras)
  and their callers with printf of TI's `rts6740` - a selected caller against a cl6x-built variadic callee. 6 of 6
  functions selected at -O0 and -O2; passes at -O0, -O1, -O2 on vm6747 and sim6747 and under the walker.
- **Cycles (`tools/c6x-cycles --check`, -O2, six shards on Linux, vm6747): NOT clean - the user's question.** 821
  cases, 458 equal, **293 better, 64 worse of which 7 accepted, so 57 worse and unaccepted**, 6 new; the sum 192,025,049
  -> 192,015,699. The 57 are named in `boxkit/w2-worse.txt`: array-loop-member-copy +13,809 (of 3,969,492),
  fill-stores +8,132 (of 1,049,849), template-subst +501, divide-by-constant-signs +405, reference-collapsing +101,
  const-overload +61, member-pointer-condition +52, forwarding-pack +43, va-arg-shapes +38, and 48 of +34 or less.
  One cause is read (switch-condition-declaration, +5): `%2 = bitcast(addr @str)` made before an earlier call and
  read by printf crosses that call, so `ssaAssign` gives it a saved register (A10, saved and restored) where the
  walker remakes the address at the call. **A mend tried and withdrawn**: counting such a bitcast as remade made 129
  cases worse (array-loop-member-copy +385,015) - a loop then rebuilds the address every turn. The larger cases'
  causes are not read. `return-self-referential`'s accepted line is stale (no longer worse). Nothing was accepted
  or recorded by this lane.
- **Gate (a coding lane's; the wave gates it fully).** Linux (g++, ~/w2, the committed sources): tests/tms6747.sh
  shards 1-3/6 at SIM=1 -O0, -O1, -O2, under the walker at -O2 and at SIM=0 -O0, -O1, -O2: 409 passed, 0 failed at
  every leg; shards 4-6/6 there as well, every leg 415 passed, 0 failed. asm6x.sh at -O1 and -O2: 843 objects, 0
  failed. Windows (cl /W4 /WX clean, work\w2, built at cfbf1b2f - the same sources): shards 4-6/6 at SIM=1 -O2, -O0,
  -O1: 415 passed, 0 failed each; the walker at -O2 4/6 and 5/6 0 failed, **its 6/6 and SIM=0 -O2 4-6/6 still
  running when the hour ended** (work\w2-gateB.log). identical.sh ARCHS=tms6747 against the base's cl build:
  2,634 compared, 1,421 differ over 603 cases, each named in `boxkit/w2-ident.log` (a case that calls printf has a
  newly selected function; not read one by one). tools/c6x-declines --check: baseline 0 holds. Self-compile of the
  two changed C6x files at -O0 and -O2: 4 / 0. exclusions 0 uncited 0 stale; comment cap 0 over; memory gate
  holds; ide/generate.py up to date. **Not done**: cl6x 8.2.2's `-S` (7.4.4 alone was asked), a cl6x-built caller
  against a selected callee, SIM=0 at -O0/-O1 on Windows, run.sh, and the cycle question above.

## M6a.2, round W2's cycles: the 57 worse cases read and three causes mended - lane w2b, 2026-11-15

From m6a2-w2 055920a9, branch m6a2-w2b. src/: +35 -5 in two files (C6xSelect.cpp 806 -> 836, Tms6747.h one comment).
Measured as W2 was: `tools/c6x-cycles --check`, -O2, six shards on Linux (g++, ~/w2b), vm6747, against
tests/tms6747-cycles.txt (458f8974). **57 worse and unaccepted -> 37; 293 better -> 323; 448 equal; no case worse that
W2 did not have worse; sum 192,015,699 -> 192,010,879.** Nothing accepted or recorded by this lane.

- **Cause A, mended: a cast of a remade value held from its definition.** `%2 = bitcast(addr @str)` is made where
  the argument list begins; a call between it and its own call (`printf("..", f())`) gave it a saved register, and
  with none free a frame word. `ssaAssign` marks such a cast `ssaFused_ == 2` - every reader a call no deeper in
  loops than the definition, or no register won - and `ssaLoad` makes its operand afresh at the reader. A loop's
  cast keeps its register (W2's withdrawn mend remade those too: 129 worse).
- **Cause B, mended in part: the first block's label.** `L$f$ssa1:` stood between the prologue and the text, so the
  packer could not pair the first instructions with the frame's (+2 a `main`). It is written now only where an edge
  reaches it or a parameter has a home. **Dropped everywhere it gave 515 better but five cases +1 to +3** (move-only,
  raw-string-literal, return-copy-balance, return-parameter-moves, member-init-class: the parameter's move is then
  folded away and `MVKL x, A4` can no longer pair beside the load through it) - so it stays where a parameter has
  a home; the fold is the next thing to read (about 190 cases would gain).
- **Cause C, mended: a variadic call's words all through A4.** They are made through A4, A6 and A8 in turn (each an
  argument register, loaded after), so two strings' halves pair (raw-string-literal).
- **Not W2's: template-subst +501.** The base 79b00a6e's own text measures 949 (identical text, `main` walked);
  the rise came between 458f8974 and 79b00a6e.
- **A mend tried and withdrawn: the word stored where it is made** (`boxkit/w2b-earlyword.patch`). A variadic call's
  word whose value is made before an earlier call of its block was stored at once, the call's words opened there as
  the walker opens them. It gained six of the group below (throw-class +8 -> +3, throw-class-pointer-ms +16 -> +2,
  local-class-two-functions +8 -> +3) and made five cases newly worse - array-loop-local +31,773, ssa-for-init-object
  +20,410, nested-handler-exits +11,208, handler-exit-end-catch +10,904, ssa-catch-by-value +10,838 - the same with
  it kept out of loops' blocks, so the cost is elsewhere in those functions; not read. Every shard still passed.
- **Still worse, 36 of W2's, by cause** (base against this tree, `boxkit/w2b-class.sh`; each is the user's question):
  - *The SSA inliner's body in a newly selected caller* - a callee inlined before its own calls were folded, so the
    caller writes more calls than the walker's one, or inlined loops whose values overflow the eight saved registers
    into frame words: fill-stores +7,651 (three fills inlined in `main`, 2 -> 11 frame loads), array-loop-member-copy
    +13,803 (`show` with three sums inlined, `lfReport`), reference-collapsing +95, const-overload +48,
    forwarding-pack +39, member-template-class-pack +32, ssa-destructor-through-base +31,
    global-scope-qualifier-expression +28 (`cc::use()` is `MVK 107` alone, three calls inside `main`),
    ssa-functor-member +20, namespace-unnamed +19, namespace-nested-class-member-out-of-line +18,
    member-value-init-default-argument +10, ssa-call-through-star +6, template-keyword-member +6. (fill-stores,
    global-scope-qualifier-expression and reference-collapsing read; the rest grouped by the calls written changing.)
    `SsaInline` is not this lane's file (the plan's inliner round).
  - *A call's result held for a later call's word* - the walker stores it into the open outgoing words at once, the
    selector keeps it in a saved register (a save and a restore) or a frame word until the call: member-pointer-condition
    +50 (2 -> 14 frame loads), member-pointer-models +24, throw-class-pointer-ms +16, ssa-volatile +15, throw-class
    +8, local-class-two-functions +8, ssa-try-catch +5, ssa-nested-braced-list +4,
    static-array-runtime-initialiser +3, rethrow-through-handlers +3, point-of-declaration-static +3,
    switch-condition-declaration-enum +1. (Grouped by the saved registers or frame loads rising with the calls
    unchanged; static-array-runtime-initialiser and stdin-gets read line by line, the rest by those counts.)
  - *A `?:` of two addresses through a block parameter* (a branch and a move where the walker wrote a predicated
    pair), +2 or +3: stdin-gets, switch-condition-declaration, lambda-reads-static-local, template-body-namespace.
  - *Division by a constant left to the EABI helper*: divide-by-constant-signs +30 (was +405) - nine `__c6xabi_divu`
    and nine `__c6xabi_remu` in the selected `unsignedRow` where the walker multiplied.
  - *Not read*: va-arg-shapes +38, mem-init-two-rules +16, ssa-pack-small-shapes +8, call-nullptr-static-ssa +6,
    template-sfinae-fallback +3 (`main` walked in each; calls and saved registers not up).
- **Gate at 91ea9ffc (a coding lane's; the wave gates it fully).** Linux (g++, ~/w2b): tests/tms6747.sh six shards
  at SIM=1 -O0, -O1, -O2, under the walker at -O2 and at SIM=0 -O0, -O1, -O2 - every leg 0 failed; asm6x.sh at -O1
  and -O2: 843 objects, 0 failed; tools/c6x-declines --check: baseline 0 holds; census -O2 walked 3,383 = the
  baseline (this lane selects nothing new: no baseline commit); self-compile of C6xSelect.cpp at -O0 and -O2: 2 / 0;
  identical.sh ARCHS=tms6747 against W2's build: 2,634 compared, 1,486 differ over 552 cases, named in
  `boxkit/w2b-ident.log` (a function with a string argument or no parameter's home changes; not read one by one);
  comment cap 0 over, memory gate holds, exclusions 0 uncited 0 stale. Windows (cl /W4 /WX clean, work\w2b): shards
  4-6/6 at SIM=1 -O2 and under the walker at -O2, 0 failed. **Not done**: Windows at -O0/-O1 and SIM=0,
  identical.sh and self-compile on Windows, c6x-cycles at -O0, ide/generate.py --check, run.sh.
  `return-self-referential`'s accepted line is still stale (W2's note); the baseline file is untouched.
- **A fourth mend, its own commit after the gate above: a cast's remade operand made in the cast's register**, not in
  A4 and moved (C6xSelect.cpp 836 -> 838). Cycles, same measure (Linux, ~/w2b-x): **327 better, 444 equal, 37 worse and
  unaccepted, none new**; sum 192,008,228; ssa-volatile +15 -> +6, fill-stores +7,651 -> +7,507, stdin-gets +3 -> +2
  (the lists above carry the counts before it). Its legs: Linux SIM=1 -O0, -O1, -O2 and SIM=0 -O2 six shards and
  asm6x -O2, comment cap, memory gate, exclusions; Windows cl clean, SIM=1 -O2 and the walker 4-6/6 - the numbers in
  `boxkit/w2b-handover.md`'s last lines. The walker, SIM=0 -O0/-O1, identical.sh and self-compile were not re-run on it.

### M6a.2 W1's slower cases, accepted - the user, 2026-11-15

**The user's decision**, given directly in the coordinator's session ("Yes, write them"): W1's slower cases are accepted
as owed - at -O2 the 23 cases tests/tms6747-cycles.txt now lists as `accepted ... (the user, M6a.2 W1)` plus
map-set-node-stability's cap raised to 7,641,691; at -O0 the 5 such lines. template-subst is not W1's (its source was
widened, re-recorded at f9e35d30). The two lines return-self-referential and map-move-only-mapped (the user's Q1) are
out: both are now below their columns, and c6x-cycles fails a stale line.
**Owed - a planned round, "frame slots shared by scope"** (after M6a.2's W1-W6, before M6d): the selector's frame
layout gives each alloca its own word where the walker shared one between sibling scopes, which makes the lifetime
ledger (tests/cases/lifetime.h) scan longer - the cause of every case over +1,000; with it, the two pass rules lane w1b
named (SsaPromote keeps a handler-live local in memory; SsaInline declines a call in or near a pad) and
map-set-node-stability's +11,356 (two __tree insertUnique calls, +5,685 each), to be diagnosed in that round.
**The user's standing rule for cycles** (2026-11-15) is now in the cycle file's header: the coordinator may accept a
round's worse cases itself only when the total falls at the level judged, no case is newly worse, and every worse
case's cause is named there as owed with its round; anything else is the user's.

### Wave 22's merge: M6a.2 W2 onto W1, with p7 and rh1 - lane mg22, 2026-11-15

From wave22 b76b033b (g1 027251b9 + m6a2-w1c d5fa2715): `git merge m6a2-w2b`, `p7`, `rh1`, `w1-accept`. W2's four
conflicts in C6xSelect.cpp were each one round's selection against the other's refusal; both are selected now - the
refusal list holds neither "landing pads" nor "variadic" (W1's `throw(T...)` refusal kept by name), the op switch takes
CatchPad/CleanupPad and VaStart/VaArg/VaEnd, a pad's landing label sits beside W2's entry label (only where reached),
and an invoke's try label stands before W2's variadic stack words. **tests/cases/c6x-variadic-pad** holds both: a
variadic definition with a pad and a variadic invoke, all four functions selected at -O0/-O1/-O2.
**Walked (ssa-census -arch tms6747, CENSUS_SOURCES=0, Linux)**: -O0 3313 -> 2390, -O2 3527 -> 2442 (e573cbb9).
**Cycles (Linux 1-3, Windows 4-6, against the file after w1-accept)**: -O2 192,025,550 -> 191,867,154, 404 better, 54
worse (11 owed or accepted), 43 not owed: 30 W2's own (the 37 the user accepted as owed), 11 bodies with both pads and
a variadic call (selected only on the merge; try-body-local +61,762 and ssa-ctor-delegate +20,354 the largest, both
lifetime-ledger cases, W1's frame-slot shape by its owed reading, not diagnosed here), member-default-init and array-loop-throw past W1's caps for the same bodies.
-O0 397,272,350 -> 385,994,327, 120 not owed (117 W2's - W2 was never measured at -O0 - and 3 both-feature bodies).
16 W1 lines at -O2 and 2 at -O0 are stale (W2 made them equal or better). Lists: boxkit/mg22-worse-O2.txt, -O0.txt;
the intended identical changes against g1 (1,829 tms6747 outputs, and p7's adl-block-declaration-outlives-nested on
the three other targets) in boxkit/mg22-expected.txt. No accepted line is written here: that is the user's.


### Lane g2 - two faults of tools/wave-gate that failed wave 21, 2026-11-15

Lane g2 (Opus), branch g2 from g1 `027251b9`. Only tools/wave-gate changed; no src/ change. Wave 21's gate (`w21g`):
41 passed, 1 failed, 5 not run.
- **`wtms2@4-6` failed 3 cases with `./cxx3-msvc.exe: Device or resource busy`**: `wselfc` started at 22:30:20 beside
  it in the same Windows tree, and tools/windows/self-compile.cmd calls msvc\build.cmd, which relinks cxx3-msvc.exe
  while the walker shard runs it. Read, step by step: **wselfc** (self-compile.cmd) and **wfixed0/wfixed2**
  (self-host.cmd's stage 0) call build.cmd - they write the compiler; **winpass*** (each wincases job's run-cases.cmd
  `:tools`) and **wfixed*** (`run-cases.cmd :tools`) rebuild winout-tools\masm.exe and link.exe, which **cdb** links
  with; release, names, overload, winstruct, the tms halves and the idle steps only run it. On Linux **selfc**,
  **fixed*** (tools/self-host stage 0) and **maketest** run `make cxx3.exe`, a no-op after `build`.
  **The mend:** `alone()` - wselfc, wfixed0, wfixed2 start only on an empty Windows pool and nothing starts beside
  them; winpass*, cdb and wfixed* are one family (`wintools`, winout-tools); on Linux those three steps FAIL by name
  unless `make -q cxx3.exe` holds (the relink would happen under the other slot). `-p` prints both lines.
- **`cycles` "NOT RUN no verdict came back (no reason given)"** though cycles-joined.txt (820 lines) was written:
  `cycles_join` counted its wait in `t`, the same variable `linux_leg` held the second claimed step in, so after its
  four-minute wait for tms2's halves the chain ran "asm2 240" (`240 FAIL wave-gate --box knows no step 240` in the
  box's verdicts) and cycles never ran. **The mend:** cycles_join's variables are `cj_*`; a wait past an hour is
  `NOT RUN tms2's halves: n of w reported` written into the box's own file (`bverdict`: the Mac's poll copies the
  box's file over its own, so a Mac-written Linux verdict went there or was lost); after every Linux chain `lost`
  FAILs by name any step it ran that left no verdict; the report says "claimed, and no verdict came back" for an idle
  step, and a NOT RUN the box gave is marked as the box's reason, not "no reason given".
- **The short run, `tools/wave-gate w21g -k` against w21g's trees (wtms2@4-6, wselfc and cycles cleared):**
  `cycles` **PASS** on Linux - 820 cases, 815 in the baseline (458f8974): 807 equal, 8 worse all owed or accepted, 5
  new, 0 missing, 0 WORSE. The Windows half's first try lost its ssh (the box did not answer 22:43-22:49: "Connection
  reset by peer", then "Operation timed out") - the gate wrote FAIL "no verdict came back" and "no count", never a
  silent NOT RUN. Again, `WAVEGATE_HALVES=1 -b windows`: **wtms2@4-6 PASS 413/0 (22:50:49-22:55:41), then wselfc
  alone, PASS 103 of 103 (22:55:45-23:03:17)** - no overlap, no "Device or resource busy". With wave 21's other
  verdicts the gate is 44 passed, 0 failed, 4 not run (the fixed points, not owed, checked). Static gates: no src/,
  docs/EXCLUSIONS.md or ide/ change, so tools/exclusions, make comments and tools/memory-gate are w21g's (PASS).

### Wave 21 landed - the gate under the user's QA rule, 2026-11-15 (the coordinator)

g1 (the mend check's N1-N5: tests/wave-expected.txt and the wave's own base build, the walked ratchet on the census's
status, identity for x86_64-linux/-windows/arm64 back, emit's golden from the base, release a real step, the fixed
point's owed-check), r6n (names, overload and win-structure pass on Windows with REQUIRE_ORACLE=1), r6x (two Linux slots,
the Windows pool), g2 (wselfc and wfixed* run alone; the `wintools` family; cycles' lost verdict).
- **Gate: tools/wave-gate w21g -w 82bd61a9 at 027251b9**, then the two failed steps rerun by the gate itself (`-k`) with
  g2's script: **44 passed, 0 failed, 4 not run** (the fixed points, not owed by the script's check). The first run's one
  FAIL (wtms2 at Windows 4-6/6, "Device or resource busy") was wselfc relinking the compiler beside it, and its cycles
  step lost its verdict to a shared variable - both g2's; with them mended wtms2 passed 413/0, wselfc 103/103 alone,
  cycles PASS (807 equal, 8 owed or accepted, 5 new, 0 worse). Landed at g2 201767ca, which changes only the gate
  script: the recommended option, taken under the user's standing instruction (2026-11-15).

### Wave 22's slower cases, accepted - the user, 2026-11-15

**The user's decision**, given directly in the coordinator's session ("Yes, write them"), for the merge of M6a.2 W1 and W2
(lane mg22): at -O2 the 30 W2 cases, at -O0 the 117 W2 cases (W2 first measured at -O0 on the merge), and the 14 bodies
with a landing pad and a variadic call (11 at -O2, 3 at -O0; selected only once both rounds merged, cause not yet
diagnosed) are accepted as owed; member-default-init's and array-loop-throw's W1 caps raised; 18 W1 lines now below
their columns taken out. Owed: W2's causes D (an inliner round for newly selected callers) and E/F/G (the selector's
next tidy round); the 14 merge bodies to the round "frame slots shared by scope", diagnosed there. Totals: -O2
192,025,550 -> 191,867,154 (404 better), -O0 397,272,350 -> 385,994,327 (439 better). tests/wave-expected.txt holds
mg22's 1,838 intended differences against main 006deb20's build (1,829 tms6747 outputs and p7's case on three hosts).

### Resume point, 2026-11-15 ~23:45 PKT (the coordinator; the user: "Save context of all threads!")


#### Where things stand
- **main = m3-line = 006deb20** (wave 21: the wave gate under the user's QA rule). M5 closed; M6c closed (wave 19);
  the M6c review's mends landed (waves 20-21); M7 closed; M8 not started on main.
- **Wave 22 - not landed yet.** Branch `wave22`, worktree `~/cxxopt-build/w22`, head **e24b1927** = main 006deb20's
  g1 part + M6a.2 W1 (m6a2-w1c d5fa2715) + W2 (m6a2-w2b bd64531b, the four C6xSelect conflicts resolved by lane mg22)
  + p7 219a667c (N7, ADL state across nested parses) + rh1 826515fb (release-check hang: Git's find.exe) + w1-accept
  bcf9f0f3 + main 006deb20 + the user's acceptance of wave 22's slower cases (161 lines, 2 caps raised, 18 stale out)
  + tests/wave-expected.txt (1,838 rows vs 006deb20). Walked count -O0 2,390 / -O2 2,442. mg22's lane gate: every
  C6000 shard 0 failed on both boxes; run.sh 1231/0; self-compile 103/103.
- **The w22g gate** (`tools/wave-gate w22g -w 006deb20`, started 23:31, log `boxkit/w22g-wavegate.out`, verdicts in
  `~/cxxopt-build/wave-gate/w22g/{linux,windows,mac}.verdicts`): at 23:45 PASS so far - build, static, gdb 5/5,
  run1 and run2 1231/0, tms0@1-3 421/0, tms1@1-3 421/0, mac mbuild/mrun0/mrun2 1229/0/lldb 5/5, wbuild, tms0@4-6
  407/0; still running: the other C6000 shards, winpass, cdb, wselfc, names/overload/winstruct, release, identical*,
  emit, census, cycles, maketest, ssa1/2, subst, selfc. **Next session: read the verdict files; if the run finished
  with 0 failed, land wave22 at e24b1927 (push to main and m3-line, then a "Wave 22 landed" entry); if it was cut or
  failed, re-run `cd ~/cxxopt-build/w22 && sh tools/wave-gate w22g -w 006deb20` (or `-k`), land only on 0 failed.**

#### Open branches (local, not pushed)
- `wave22` e24b1927 - as above, the next landing.
- `m6a2-w3` = e24b1927, no code: lane w3's ABI findings in `boxkit/w3-handover.md` (cl6x 7.4.4: <=4-byte trivially
  copyable struct in A4 / one register; <=8 in a pair; 12-byte via A3, passed as the caller copy's address; a class
  with copy ctor or dtor through a hidden first parameter in A4); its four-step plan; scripts w3-ship.sh, w3-linux.sh.
  Check first: class-result calls are followed by a memcpy - callers freed by W3 may stay walked for W5's reason.
- `m8-b` 0c6336ce - M8.1 commit 1 (`-check=templates` count); `m8-c` = m8-b, no code: `boxkit/m8c-handover.md`
  (first make Target::sizeOf answer "dependent" by name, then commit 2, then commit 3 from boxkit/m8b-read.patch).
- Merged into wave22 and kept for reference: m6a2-w1, m6a2-w1b (causes), m6a2-w1c, m6a2-w2, m6a2-w2b, p7, rh1,
  w1-accept. Landed and done: r6a-r6i, r6w, r6x, r6n, g1, g2, mc6 (read-only), m8a.

#### Owed (recorded in doc-cxx3/M6-PLAN.md's owed table and entries)
- The round **"frame slots shared by scope"** (after M6a.2, before M6d): W1's slower cases (lifetime ledger scans),
  SsaPromote's pad rule, SsaInline near pads, map-set-node-stability +11,356+, and the 14 pad-and-variadic bodies.
- An **inliner round for newly selected callers** (W2's cause D) and the **selector's next tidy round** (E, F, G).
- tests/open: names-register.txt's 5 defects ($deduced_N closures, a lifetime temporary's MS name, unwind
  dispUnwindHelp 0, `p->~Base()` through a typedef accepted, `std::__fn_op` mangling); walker-static-reference-temporary;
  namespace-unnamed-nested-leaks; cxx3's refusal of `C()(a,b)` in OptTable.cpp.
- ASM6x taking cl6x's `||name||` (the user's QC: keep `A4+0`). Windows -O0 fixed point not on record.
  A per-case walked baseline (g1). The mend check's should-fixes N6 (no gate runs a declined function), N8-N12.

#### Next steps, in order (the user's path)
1. Finish/land wave 22 under QA (above).
2. **M6a.2 W3** (class values; a Fable lane - the hardest), then W4 (64-bit and floating), W5 (memcpy/memset),
   W6 (indirect calls + the 24 outside the lowering) - each a coding lane, gated as a wave, walked baseline lowered.
3. The owed rounds above as the plan orders them, then **M6d** (the walker's Ast.h nodes and the parser's building
   half deleted; walked count 0 first).
4. **M8.1** from m8c's note (commits 2, 3, then 4), then M8.2-M8.9.

#### Standing rules (the user's)
- **Always the recommended option** for project decisions (cycle acceptances beyond the standing rule, plan order,
  keep-or-redo a lane, gate details): take it, record it as "the recommended option, taken under the user's standing
  instruction (2026-11-15)", send one line. Ask first for money (AWS sizes), GitHub deletes/archives, lifting caps
  (lanes, Fable, the Windows share, Mac use), or no clear recommendation.
- **Cycle standing rule**: accept worse cycle cases without asking only when the total falls at the level judged, no
  case is newly worse, and every worse case's cause is written as owed with its round. **But the permission classifier
  refuses writing cycle acceptances that reach the coordinator through another session: put them to the user
  directly in this session first.**
- **QA**: a wave lands only when tools/wave-gate shows 0 failed, every expected difference named in
  tests/wave-expected.txt (emptied when a wave lands).
- **Caps**: 3 lanes, at most 2 Fable (Fable for the wave's hardest lane); 1 hour a lane; compile 9 min, run 3 min;
  job 25 min (timeout 1500) - **40 min (2400) only for the wave's hardest Fable lane** (Mac: MACRUN_FABLE=1).
  Linux (c7i.xlarge, 4 vCPU) jobs through `~/slot` (two slots, -j2 each); Windows up to 10 processors at /affinity
  3FF, shared - never kill by name; disks below 70%. Box split: boxkit/windows-split-2026-11-15.md.
- **No new tasks beyond the plan** ("No new tasks plz, to make the matters proceed"): no side lanes unless a planned
  landing needs them; owed items stay owed; report landings only.
- The user's decisions on the M6c review and after: boxkit/user-decisions-m6c-review.md.

#### Lane wf22, 2026-11-17: wave 22's blocker and its two list faults (Windows only)
- **c6x-variadic-selected on x86_64-windows** printed `1137198` for `1147066` at -O0/-O1/-O2, both spellings: `many(a,
  b, c, d, e, ...)` read `e` as its first unnamed argument. Root: `X86_64Linux::receiveParameters` set the Microsoft
  overflow pointer to `16 + intsUsed * 8`, which stops at the fifth named slot; now `16 + (intsUsed + stackWords) * 8`
  (src/ 2 lines, +1 net). Not wave 22's: main 006deb20 built on the box prints the same; clang 19.1.5 there prints the
  .expected. The case's three x86_64-windows rows are in tests/wave-expected.txt.
- **identical's 13 unnamed rows** were the `.part` cases: the gate read `X.part.tms6747.O0` as case `X`, target
  `part.tms6747`, which no row can name - tools/wave-gate now keeps `.part` in the case. **emit's "named and the
  same"** was every -O0/-O1 row (emit measures -O2 alone): emit now judges its own level, and a -O2 row emit sees
  unchanged (22, measured on Windows against main's golden: 5 cases new to the wave, 17 that differ only in
  identical's build) is identical's to judge, not emit's FAIL. No row removed: each is one identical differs on.
- Linux legs owed (the box is off limits): build, static, gdb, run1/2, maketest, ssa1/2, subst, selfc, identical*,
  emit, and the C6000 shards' Linux half.

### Wave 22 landed - W1, W2, p7, rh1 and wf22's varargs fix, 2026-11-17 (the coordinator)

- **Gate: tools/wave-gate w22h -w 006deb20 at 8283ced0** (wave22 e24b1927 + the resume entry + wf22): **43 passed, 0
  failed, 4 not run** (the fixed points, not owed by the script's check). The first run's one FAIL was Windows `names`
  cut by the 25-minute cap beside other steps with no count line; rerun alone by the gate (`-k`): 882 passed, 0 failed,
  in 4 minutes. wf22 mended the Microsoft x64 varargs overflow pointer in X86_64Linux::receiveParameters
  (`16 + (intsUsed + stackWords)*8`; main had the bug, winpass's c6x-variadic-selected found it).
- tests/wave-expected.txt emptied (1,841 rows were this wave's).

### M6a.2 W3: class values on the C6000 selector - lane w3, 2026-11-17

**What goes through the selector now** (src/backend/C6xSelect.cpp, 887 -> 934 lines; branch m6a2-w3 from e24b1927):
a class result - a trivially copyable class of 8 bytes or less in A4 or A5:A4 (`loadPair`), a larger one copied through
the pointer in A3 where the caller gave one (predicated at -O1 and above, branched round at -O0, as `visit(Return)`),
a class with a copy of its own built through the hidden first parameter in A4 (`f.sret`, read from the word
`emitFunction` keeps it in; the arguments start at B4); a class parameter - `emitParams` already writes the pair or
copies the caller's object into the parser's slot, and the IR's pointer is that slot's address; and a class result of
a call - the frame object's address in A3, or in A4 before the arguments, or the pair stored into it after the call,
and `c.sret` (a call built in this function's own result) passes the kept pointer on. cl6x 7.4.4's `-S` is the oracle
(boxkit/w3-handover.md, 2026-11-15); 8.2.2's was not run.

**Walked count** (tools/c6x-walked.baseline, Windows, cl build, 1,238 cases): -O0 2,390 -> 1,756, -O2 2,442 -> 1,823.
`class result`, `class or 64-bit parameter` (now `64-bit parameter`, W4's 163) and `class result of a call` read 0.
**Left in W3's reasons, by name:** `class argument` 112 / 111 (-O0 / -O2) - a trivially copyable class over 8 bytes
must be copied by the caller (cl6x passes the copy's address; the IR passes the object's own), which wants frame room
and a selected copy, and a small one wants `loadPair` into any argument pair; `class result of another shape` 19 - a
class with a destructor and no copy of its own: the hidden pointer is A4 but the IR returns the object's address, so
the selector owes `visit(Return)`'s unguarded copy; `aggregate value` 41 / 3. Most callers of a class-returning
function stay walked for `op memcpy` (330 / 372, W5's). Functions that need W4 as well are those the census names
`floating value`, `floating or N-bit result`, `N-bit value` and `N-bit parameter`.

**A scheduler fault the newly selected bodies met** (src/optimizer/C6xPack.cpp, `foldAddressing`): an `ADD R, k, T`
was folded into a later `LDW *T` as `*+R(k)` past a load that had rewritten R - the scan let any load into R through,
where only the folded access itself may write it. `std::map::insert` at -O2 (map-set-node-stability) read through a
null pointer; one condition mends it.

**Case** tests/cases/c6x-class-values (clang's .expected; ledger built=9 gone=9, live, double, phantom, over, lost 0).

**Gate, on Windows alone** (the Linux box was out of bounds, then back with my ssh refused: every Linux leg is owed):
cl /W4 /WX clean; the case right at -O0/-O1/-O2 on vm6747 and sim6747 and by the walker at -O2; tests/tms6747.sh SIM=1
shards 1-6/6 at -O2 829 passed / 0 failed and at -O0 829 / 0, on the final tree; tools/exclusions, the comment cap and
the memory gate hold, tools/c6x-declines 0, self-compile of C6xSelect.cpp and Tms6747.cpp 4 / 0 (these four before
the scheduler's one-condition mend). **Cycles** (tools/c6x-cycles --check, nothing accepted by the lane): -O2 489
better, 255 equal, 71 worse of which 21 unowned (+1 to +42; boxkit/w3-worse-O2.txt), total 191,867,154 ->
191,556,159; -O0 450 better, 180 equal, 184 worse of which 75 unowned (boxkit/w3-worse-O0.txt), total 385,994,327 ->
385,238,195. The causes are named by family, not diagnosed case by case. **Owed to the wave gate:** -O1, SIM=0 at
every level, the walker legs, asm6x at -O1 and -O2, identical.sh ARCHS=tms6747, C6xPack.cpp's self-compile, every
Linux leg (shards 1-3/6), cl6x 8.2.2's `-S`, and the remainder named above.

### M6a.2 W3 landed - class values on the C6000 selector, 2026-11-17 (the coordinator)

- **The user's decision**, given directly in the coordinator's session ("Yes, write them", 2026-11-17): W3's 96 worse
  cases (21 at -O2, +1 to +42 each; 75 at -O0) accepted in tests/tms6747-cycles.txt, owed to the round "frame slots
  shared by scope" (eaac4b06); totals fall at both levels. Four -O2 lines no longer worse taken out.
- **Gate: tools/wave-gate w3h -w 91efcd2b at e425c018**, Linux legs included: **43 passed, 0 failed, 4 not run** (the
  fixed points, not owed). The first run (w3g) failed only on its 566 unnamed tms6747 differences and the 4 stale lines.
- tests/wave-expected.txt emptied (566 rows were W3's).

### M6a.2 W4: 64-bit and floating values on the C6000 selector - lane w4, 2026-11-17

**What goes through the selector now** (src/backend/C6xSelect.cpp, 934 -> 1,166 lines; Tms6747.cpp 2,487 -> 2,486; branch
m6a2-w4 from f20e46ca, commits 48b38437, 8b3cf8e0, 43509534, 35d47c3a): a `long long` or a `double` is a register pair - A5:A4
the result, each argument and parameter its register's pair (B5:B4, A7:A6 ...), as cl6x 7.4.4's `-S` writes it (Linux,
~/w4-cl/p.asm: `mixed(int, long long, int, long long)`, `mixd(int, double, long long, float)`; 8.2.2's was not run) - and a
`float` one register, held like a word. A 64-bit value's home is one of A29:A28, A31:A30, B29:B28, B31:B30 where no call
crosses it, else eight 8-aligned bytes of the frame (`ssaPair` reads it into a pair, `ssaKeep` writes A5:A4 back); a
64-bit constant is made at each use. The instructions are the walker's own text, now taken by operator
(`fpBinary(BinOp, dp)`, `wideBinary(BinOp, sign)`, `wideCast`, `shiftPairLeft/Right`): ADDSP/ADDDP, MPYSP/MPYDP, the
CMP*SP/DP compares, INTSP/INTDP(U), SPTRUNC/DPTRUNC, SPDP/DPSP, and `__c6xabi_divf/divd`, `divlli/divull/remlli/remull`,
`fixdlli/fixdull/fixflli/fixfull`, `fltllid/fltulld/fltllif/fltullf`, `fixdu/fixfu`. Also: a variadic call's 64-bit
arguments 8-aligned in its area (as `stackArg`), `va_arg` of one, a 64-bit `switch`, pointer casts over 64 bits, a
branch's 64-bit arguments staged in pairs of B16-B23, and the 32-bit `smulh` (`MPY32` into A1:A0). A compare of 64-bit or
floating operands is never fused into its branch.

**Walked count** (tools/c6x-walked.baseline, Linux, g++ build, 1,239 cases): **-O0 1,756 -> 1,036, -O2 1,823 -> 1,117**.
`floating value`, `floating or N-bit result`, `N-bit value` read 0. **Left in W4's reasons, by name:** `64-bit smulh` 9
and `64-bit umulh` 2 (-O2 only: the SSA optimizer's divide of a 64-bit value by a constant wants the high half of a
128-bit product, which no helper gives - the optimizer should not make it for this target), `64-bit parameter past the
sixth` 1 (the pair's high register is A11, B11, A13 or B13, a home; `64-bit argument past the sixth` and `64-bit gep`
read 0 in the cases). The rest is other rounds': `op memcpy` 384 / 437 (-O0 / -O2) and `op memset` 207 / 229 (W5),
`indirect call` 216 / 254 and `outside the SSA lowering` 24 (W6), W3's `class argument` 138 / 137, `class result of
another shape` 19, `aggregate value` 45 / 3, `variadic call` 2.

**Case** tests/cases/c6x-wide-values (clang's .expected, the Windows box's clang): 55 of 55 functions selected at -O0 and -O2.

**Gate, on the final tree 35d47c3a, both boxes:** g++ -Werror (Linux) and cl /W4 /WX (Windows) clean; the case right at
-O0/-O1/-O2 on vm6747 and sim6747 and by the walker at -O2 (Linux); **tests/tms6747.sh SIM=1 at -O2 830 passed / 0
failed and at -O0 830 / 0** (shards 1-3/6 Linux, 4-6/6 Windows); tools/exclusions, the comment cap and the memory gate
hold; tools/c6x-declines 0; self-compile of C6xSelect.cpp and Tms6747.cpp at -O0 and -O2 4 / 0 (Linux).
**Cycles** (tools/c6x-cycles --check, nothing accepted by the lane): **-O0 496 better, 71 equal, 247 worse of which 99
unowned, total 397,272,350 -> 370,529,479; -O2 592 better, 129 equal, 94 worse of which 35 unowned, total 192,025,550
-> 194,586,314 - the -O2 total RISES**, by ssa-narrow alone (+2,666,266: `down`'s 64-bit sum and the optimizer's 64-bit
induction variable, each operation through A5:A4 / A7:A6 with moves; before the pair homes it was +19,332,989), then
pipelined-data-exit +333,190 and local-zero-init-large +304,422. Lists with causes by family: boxkit/w4-worse-O2.txt,
w4-worse-O0.txt; four -O2 cases were not read at all. 3 (-O2) and 15 (-O0) accepted lines are stale.
**Owed:** the user's word on the -O2 total (the standing rule wants it to fall); a round "64-bit operations in place" (an
add, a compare and a widening on the home pairs, no moves through A5:A4) and the pipelined shapes for selected floating
loops; **to the wave gate:** -O1 and SIM=0 at every level over the suite, the walker legs, asm6x, identical.sh
ARCHS=tms6747 (tests/wave-expected.txt rows), ti-link, run.sh on the hosts (Tms6747.cpp's two helpers changed
signature only), C6xPack/Sched self-compile, cl6x 8.2.2's `-S`, and the census on the Windows cl build.

### M6a.2 W4b: 64-bit operations in place on their home pairs - lane w4b, 2026-11-17

**What changed** (src/backend/C6xSelect.cpp 1,166 -> 1,259 lines, Tms6747.h +1, src/optimizer/C6xPack.cpp +1; src/ 94,745 ->
94,840; branch m6a2-w4 on 7f5e1ca4): `ssaWideInPlace` writes a 64-bit add, subtract, multiply, and/or/xor, comparison and a
widening of a word with its operands read where they live and its result in its own pair - no moves through A5:A4 / A7:A6
- where every pair is on one side; a block parameter's pair is loaded straight into its home. A sum's carry is bit 32 of
ADDU's 40-bit result (masked by AND 1, so nothing rests on what the odd register's upper bits hold), which reads neither
addend after the sum is written - the order `CMPLTU sum, addend` forced was what kept a counter's step a cycle late. A
constant of 1..15 added or taken away uses the immediate ADD and CMPGTU's 4-bit constant for the carry or borrow, held in
the other side's B0 / A0 so the compare takes that side's .L unit. `unitsFor` knows ADDU (.L). `down`'s loop in
ssa-narrow: 12 cycles a turn -> 8.

**Case** tests/cases/c6x-wide-values widened (walkUp, walkDown: sums and counters across the 32-bit boundary, x + x, a
product; clang's .expected from the Windows box): 57 of 57 selected at -O0, -O1 and -O2.

**Gate, on the final tree, both boxes:** g++ -Werror (Linux) and cl /W4 /WX (Windows) clean; the case right at -O0/-O1/-O2
on vm6747 and sim6747 and by the walker at -O2; **tests/tms6747.sh SIM=1 at -O2 830 passed / 0 failed and at -O0 830 / 0**
(shards 1-3/6 Linux, 4-6/6 Windows, tree 15f3949d; the case alone re-run after it was split); tools/exclusions, the
comment cap and the memory gate hold - **run as `python3 tools/...`: lane w4's script ran them under `sh`, which printed
syntax errors and rc 0, so W4's "static gates hold" was not measured until now**; c6x-declines 0; self-compile of
C6xSelect.cpp, Tms6747.cpp and C6xPack.cpp at -O0 and -O2 6 / 0; the census holds the baseline (1,036 / 1,117).
**Cycles** (tools/c6x-cycles --check, nothing accepted by the lane): **-O2 593 better, 129 equal, 93 worse of which 34
unowned, total 192,025,550 -> 191,920,912** (lane w4 left 194,586,314; ssa-narrow 8,054,415 -> 5,387,746, below its
baseline; no case joined the worse list); **-O0 499 / 71 / 244 of which 96 unowned, total 397,272,350 -> 364,534,575**
(w4 left 370,529,479). Against lane w4's own numbers at -O2 28 cases fell and 17 rose, each of the 17 still below the
baseline: divide-by-constant +17,326 and ternary-index-after-remake +2,425 (the packer renamed and folded the A4 a value
used to pass through; it does not fold a copy into a home), the rest +1..+45. Lists: boxkit/w4b-worse-O2.txt, -O0.txt.
20 stale accepted lines taken out of tests/tms6747-cycles.txt (3 at -O2, 17 at -O0).
**Read and not mended:** local-zero-init-large (+304,422) and pipelined-data-exit (+333,190) are loops newly selected for
smulh; C6xPipe finds II=11 where the walker's text had II=5, because the selector's text steps the counter after its
last read and the counter is not renamed ("no slot for ADD A23, 1, A23", CPP11_PIPE=3). Owed: a round that copies the
counter at the loop's head (or steps it first) before C6xPipe; the 34 unowned -O2 cases by family in the list.
**Owed to the wave gate:** as W4's entry, and ti-link on TI's own simulator for ADDU and CMPGTU's constant form.

### M6a.2 W4 landed - 64-bit and floating values on the C6000 selector, 2026-11-17 (the coordinator)

- Lanes w4 and w4b: walked -O0 1,756 -> 1,036, -O2 1,823 -> 1,117; cycles -O2 192,025,550 -> 191,920,912, -O0
  397,272,350 -> 364,534,575 (against 458f8974's baseline).
- **The user's decision**, given directly in the coordinator's session ("Yes, write them", 2026-11-17): W4's 130 worse
  cases (34 at -O2, 96 at -O0) accepted in tests/tms6747-cycles.txt, each family named as owed (ca682b67): the loop
  counter copied at the loop head ([P]: pipelined-data-exit +333,190, local-zero-init-large +304,422), W4's remainder
  ([W], [V]), and "frame slots shared by scope" ([F], -O0).
- **Gate: tools/wave-gate w4h -w f20e46ca at 936c49d0**: **43 passed, 0 failed, 4 not run** (the fixed points, not
  owed). The first run (w4g) failed only on its 823 unnamed tms6747 differences.
- tests/wave-expected.txt emptied (823 rows were W4's).

### M6a.2 W5: memcpy, memset and class arguments on the C6000 selector - lane w5, 2026-11-17

**What goes through the selector now** (src/backend/C6xSelect.cpp 1,259 -> 1,350 lines, Tms6747.h 321 -> 325, Tms6747.cpp
one line; src/ 94,840 -> 94,935; branch m6a2-w5 from 936c49d0): `memcpy` is the walker's own `copyBlock` (word, halfword or
byte at the smaller of the two pointees' alignments, LDDW/STDW pairs where both are 8-aligned, batched into B16-B23 at -O1
and up), so its text is the walker's; `memset` is stores of B16, or of B17:B16 where the object is 8-aligned, the base
moved on through A3 past an offset's reach (past 512 bytes it stays walked: `memset past 512 bytes`, 0 in the cases). A
class argument (W3's left `class argument`): a trivially copyable class over 8 bytes is copied, before the call's area
opens, into a frame slot of its own (`ssaArgCopy_`, laid out after the allocas) and the copy's address is the argument,
as the walker's `genArg` and cl6x pass it; one of 8 bytes or less is its words in the argument's register or pair, read
from the object, and a less aligned one (an empty class, chars, shorts) put together in A3 from byte or halfword loads, as
`loadPair`. cl6x's `-S` was not run this round: the shapes are the walker's, which W3 measured against cl6x 7.4.4.
**A fault the newly selected bodies met** (Tms6747.cpp): `$EXIDX_EXTAB("__c6xabi_extab$<fn>")` is inside quotes, which
`tiSpelling` leaves alone, so `_GLOBAL__sub_I_<file>.cpp` with pads named a table whose label is `...$cpp`
(static-local-braced-list, newly selected); the quoted name is respelled.

**Walked count** (tools/c6x-walked.baseline, Linux, g++ build, 1,240 cases): **-O0 1,036 -> 374, -O2 1,117 -> 389**.
`op memcpy` and `op memset` read 0. **Left, by reason** (-O0 / -O2): `indirect call` 240 / 284 and `outside the SSA
lowering` 24 (W6); `class argument of another shape` 38 / 39 - a class with a copy constructor or a destructor (the IR
passes the caller's temporary; whether TI's callee may take it as its copy is unmeasured) or a variadic call's;
`class result of another shape` 19; `aggregate value` 45 / 3; `N-bit argument past the sixth` 5; `N-bit smulh` 4 / 10,
`umulh` 1 / 2, `N-bit parameter past the sixth` 1; `variadic call` 2.

**Case** tests/cases/c6x-memory-ops (clang's .expected, the Windows box's clang): copies and fills at every alignment, past
an offset's reach, a member between guard bytes, and class arguments of each shape the callee changes; 26 of 26 selected at -O0 and -O2.

**Gate:** g++ -Werror (Linux) and cl /W4 /WX (Windows) clean; the case right at -O0/-O1/-O2 on vm6747 and sim6747 and by
the walker at -O2 (Linux); **tests/tms6747.sh SIM=1** - Linux shards 1-3/6 on 49254db5 at -O2 412/0 and -O0 412/0 (-O1
412/0 on 7c5a6e45); Windows shards 4-6/6 on 7c5a6e45 at -O2 418/1 and -O0 418/1, the one failure static-local-braced-list
(the extab name above), re-run on the final tree 49254db5 (cl build) at -O0 and -O2 with the case: 1/0 each; tools/exclusions, the comment cap and the memory gate hold
(python3); c6x-declines 0; self-compile of C6xSelect.cpp and Tms6747.cpp at -O0 and -O2 4 / 0 (Linux).
**Cycles** (tools/c6x-cycles --check, nothing accepted by the lane): **-O2 696 better, 41 equal, 77 worse of which 10
unowned, total 191,920,912 (W4b) -> 149,569,883**; **-O0 570 better, 28 equal, 215 worse of which 37
unowned, total 364,534,575 (W4b) -> 321,440,599**. Lists: boxkit/w5-worse-O2.txt, w5-worse-O0.txt -
the unowned cases are functions W5 newly selects, causes not read in the hour (initializer-list-lowered +9,243 the one
large one, +9,243 at -O2 and +31,258 at -O0). 25 (-O2) and 41 (-O0) accepted lines are now stale; the lane took none out.
**Owed to the wave gate:** SIM=0 over the suite, the walker legs, asm6x, identical.sh ARCHS=tms6747, ti-link, cl6x's `-S`
of the class-argument shapes, the census on the Windows cl build, and the causes of the unowned worse cases.

### M6a.2 W6: calls through a value on the C6000 selector - lane w6, 2026-11-17

**What goes through the selector now** (src/backend/C6xSelect.cpp 1,350 -> 1,356 lines; branch m6a2-w6 from 94d49f8d): a call
whose callee is a value - a function pointer, a pointer to member function once the lowering has read the slot, a virtual
call through the vtable - is the direct call's text with the callee, the IR's first operand, loaded into B1 after the
argument registers and before A3 or A4 takes a class result's address, then `B B1` with B3 the return (`Tms6747::call`,
the walker's own shape); every count over the call's operands (`stack arguments`, a class argument's copy slot,
`wideCall`) now starts past the callee. A variadic call with no named argument (`f(...)`) puts every argument in its
area, as the walker's `visit(Call)`. cl6x's `-S` was not run: the text is the walker's, which the suites hold.

**Walked count** (tools/c6x-walked.baseline, Linux, g++ build, 1,241 cases): **-O0 374 -> 137, -O2 389 -> 108**.
`indirect call` reads 0. **Left, by reason** (-O0 / -O2): `class argument of another shape` 42 / 43 (a class with a copy
constructor or destructor, or a variadic call's: TI's convention for the callee's copy unmeasured); `aggregate value` 45 / 3;
`outside the SSA lowering` 24 - the lowering's own refusals (R1's rounds), by reason in boxkit/w6-outside-O2.txt: a
function try block or an exception specification 6, a class result of another shape in the lowering 7 (from another
type, inside a handler, with a destructor alive, a named return value built with defaults), `std::nullptr_t` returned 2,
a constructor with bases or members to build 1, an implicit copy of a class with a bit-field or reference 1, a reference
member bound to another type 1, a static local with default arguments 1, class locals from another type 2, an
initializer_list of a class 1, a braced list past what the lowering places 1, a static member through an expression 1;
`class result of another shape` 19; `N-bit smulh` 0 / 10 and `umulh` 0 / 2 (-O2: the optimizer's 64-bit divide by a
constant); `N-bit argument past the sixth` 5, `parameter past the sixth` 1; `variadic call` 1 (va-arg-shapes: a member
function's variadic call, `hasThis`).

**Case** tests/cases/c6x-indirect-calls (clang's .expected, the Windows box's clang): calls through function pointers with
word, 64-bit, floating, small and large class arguments and results, six register arguments, a pointer to member function
(plain and virtual), virtual calls returning a word, a `long long` and a class, a call through a pointer inside a `try`,
and a table of callees in a loop; 37 of 37 selected at -O2; right at -O0/-O1/-O2 with SIM=0 and SIM=1 and by the walker at
-O2 (Windows).

**Gate:** g++ -Werror (Linux, 7755c068) and cl /W4 /WX (Windows, 5c377a8a and 7755c068) clean; **tests/tms6747.sh SIM=1**:
Linux shards 1-3/6 on the final tree 7755c068 at -O2 416/0 and -O0 416/0 (and on 5c377a8a the same); Windows shards 4-6/6 on
5c377a8a at -O2 416/0 and -O0 416/0 - the final commit changes only template-sfinae-fallback's `main`, which is in shard 1;
tools/exclusions, the comment cap and the memory gate hold (python3); c6x-declines 0; self-compile of C6xSelect.cpp,
Tms6747.cpp and C6xPack.cpp at -O0 and -O2 6 / 0 (Linux, 5c377a8a).
**Cycles** (tools/c6x-cycles --check, nothing accepted by the lane): **-O2 722 better, 12 equal, 81 worse of which 20
unowned, total 192,025,550 (458f8974) -> 149,551,568** (W5 149,569,883); **-O0 591 better, 7 equal, 216 worse of which 49 unowned (37 W5's, 12 new), total 397,272,350 -> 321,403,278** (W5 321,440,599). Lists: boxkit/w6-worse-O2.txt,
w6-worse-O0.txt - 10 of the 20 at -O2 are W5's, the other 10 cases with a call through a value now selected (ssa-layout
+24,496, complete-object-builder +9,755 and hoist-invariants +9,665 the large ones); one function read (hoist-invariants'
`called`): its loop is right and no longer than the walker's, the cost not found in the hour.
**Owed to the wave gate:** -O1 and SIM=0 over the suite, the walker legs, asm6x, identical.sh ARCHS=tms6747, ti-link, cl6x's
`-S` of a call through B1, the census on the Windows cl build, the causes of the unowned worse cases, and the remainder above.

### M6a.2 W7: class arguments and results whose copy is a call, a dead class read, and W6's worse cases read - lane w7, 2026-11-18

**What goes through the selector now** (src/backend/C6xSelect.cpp 1,356 -> 1,382 lines, Tms6747.h +1; branch m6a2-w7 from
a4467eb2): a class argument whose copy or destruction is a call - the lowering already builds the copy in a frame slot
(`byValueArgument`) and guards it; the selector passes that slot's address as a word in the argument's register, as cl6x and
the walker do. A class result with a destructor and a trivial copy, which the lowering returns as an object's address with no
result pointer: copied through the caller's hidden pointer (A4, never null) unguarded, as visit(Return). A class read nothing
uses (-O0's `os.put(c);`, the returned reference read as an object) is dropped instead of walking the function. At -O0 a 64-bit
shift by a constant is spliced in place as at -O1 - the lowering reads and writes every bit-field in `unsigned long long`, and
the variable-count sequence (two branches a shift) made ssa-bitfield-step-deref-call 2,205 cycles (W5 1,350; now 1,539).

**Walked count** (tools/c6x-walked.baseline, Linux, g++ build, 1,242 cases): **-O0 137 -> 34, -O2 108 -> 47** (commits
1fb86f07, b46345b8, e2847cb9, 24caa303, 69e4ef10; baselines 0cc71c76, ac250690). `class
argument of another shape` and `class result of another shape` read 0. **Left, by reason** (-O0 / -O2): `outside the SSA
lowering` 24 / 24 (R1's: boxkit/w6-outside-O2.txt); `N-bit argument past the sixth` 6 / 6 (was 5: one function had both reasons -
small-class-args' and stack-args-nested's main, ssa-temporary-defaults-return-braces' four; a pair's high register is a home); `N-bit parameter past the sixth` 1 (stack-args-nested `mixed`); `aggregate value` 2 / 3 (an array value:
string-literal-array-reference's `g<const char (&)[6]>`, make_shared of one in deque-both-ends; at -O2 its main too);
`smulh` 0 / 10 and `umulh` 0 / 2 (-O2's 64-bit divide by a constant: `to_string(long long)` in seven cases, ssa-divide,
new-delete-plain, a chrono duration_cast); `variadic call` 1 (va-arg-shapes, `hasThis`).

**W6's worse cases read** (boxkit/w7-worse-O2.txt, -O0.txt; W5's, the base's and walker-all cxx3 on one case at a time): three of
W6's four large -O2 ones are not W6's. **ssa-layout** +24,496 is W5's (W5 47,278, W6 47,289; walker-all 22,942): `hashes()`'s
inner loop, which the walker software-pipelines and the selector unrolls by four with each `% 26` serial - the [pipeline]
family, owed to the selector's loops reaching C6xPack's pipeliner. **hoist-invariants** +9,665: W5 46,043, W6 46,280 (main newly
selected takes six inlined loop callees), the rest [pipeline]. **initializer-list-lowered** +9,243: W5 = W6 = 65,319, walker-all
66,859 (W4b's 55,270 was walked main over selected callees) - W5's, with the [pipeline] loops. **complete-object-builder** is
W6's: +10,114 at -O2 and +37,308 at -O0, all of it main's one throw (a variant without it: +519 / -384) - the [EH table] family:
the selected main's cleanups are invokes of out-of-line destructors, each with a terminate pad and a call-site row, where the
walker inlined them under one pad.

**Case** tests/cases/c6x-class-copies (clang's .expected, the Windows box's clang): a class with a copy constructor and
destructor and one with a copy constructor alone passed by value - first, later, beside a 64-bit and a pair argument, to a member
function, through a pointer, a temporary in a loop - copies and destructions counted where no elision decides them; a class with
a destructor alone returned, of one word and of three; a returned class reference dropped. 18 of 18 selected at -O0 and -O2
(the base walks 4); right at -O0/-O1/-O2 SIM=0 and SIM=1 and by the walker (Linux).

**Gate:** g++ -Werror (Linux) and cl /W4 /WX (Windows, work\w7b at 24caa303; 69e4ef10 after it changes only the case) clean;
**tests/tms6747.sh SIM=1** on the final tree: Linux shards 1-3/6 at -O0, -O1 and -O2 421 / 0 each, the walker
(CXX1_C6X_WALKER=1) at -O2 421 / 0; Windows shards 4-6/6 at -O2 412 / 0 on b46345b8 (the later commits change no -O2 text:
the census and every selected function are the same at -O2) and at -O0 412 / 0 on 24caa303 (work\w7b); tools/exclusions, the comment cap
and the memory gate hold (python3); c6x-declines 0; self-compile of C6xSelect.cpp, Tms6747.cpp and C6xPack.cpp at -O0 and -O2
6 / 0 (Linux, final).
**Cycles** (tools/c6x-cycles --check, nothing accepted by the lane): **-O2 726 better, 10 equal, 79 worse of which 20 unowned,
total 149,566,626 - 15,058 over W6's 149,551,568**: two ledger cases, lifetime-ledger +20,902 and c6x-class-values +10,263 (main
newly selected in each, its text shorter: tests/cases/lifetime.h's `lfFind` scans all 512 rows for an address it has not seen,
and the selected main gives each by-value copy its own frame slot where the walker reused one - the owed round "frame slots
shared by scope"); without them -O2 falls by 16,107. **-O0 599 better, 8 equal, 207 worse of
which 47 unowned (W6 49), total 321,438,297 - 35,019 over W6's 321,403,278**: the same two, lifetime-ledger +75,998 and
c6x-class-values +37,253; without them -O0 falls by 78,232 (stream-number-stops -6,529, library-cppparse-pieces -4,529,
ssa-bitfield-step-deref-call 2,205 -> 1,539). Lists: boxkit/w7-worse-O2.txt, w7-worse-O0.txt. Both totals over W6's by the
ledger cases alone: the brief's "below W6's" is not met, and is the coordinator's to judge (cycle standing rule).
**Owed to the wave gate:** SIM=0 over the suite, asm6x, identical.sh ARCHS=tms6747, ti-link, the census on the Windows cl
build, cl6x's `-S` of a class argument's copy, the two ledger cases' frame slots (or their acceptance - the user's), and
the reasons left above.

### M6a.2 FS: frame slots shared by scope on the C6000 selector - lane fs, 2026-11-18

Branch m6-fslots from m6a2-w7 5150c8bb: a5b021e7 (locals, argument copies, the case), ec041621 (temporaries). src/ 94,978 ->
95,180 lines. The owed round of W1, W3, W4, W4b and W7: `layoutSsa` gave every alloca its own bytes, where the walker's frame was
the parser's - and the parser lays sibling scopes' locals over one another. Three parts, all in the frame's layout
(C6xSelect.cpp); no instruction's text changes but its frame operand.

- **A local lies where the parser laid it** (`layoutSsaLocals`). `ir::AllocaInfo::serial` is the parser's serial of the local
  (`Lowering::localAlloca` writes it from the var's record; the inliner clears it on a callee's objects). The selector finds
  the `Local` of that serial and keeps the parser's distances inside every group of locals whose bytes the parser overlapped,
  packing the groups from the frame's cursor: the sharing is the parser's own fact, the one the walker ran on, not a second
  computation of scopes. A serial on two objects, a size the parser did not lay, a parameter, a static, or an alignment the
  group cannot keep leaves the object its own bytes.
- **Every call's copied arguments in the same bytes**: a class argument over 8 bytes is copied and consumed inside its call's
  own text, so no two calls' copies are live together.
- **The objects that are no local - temporaries, an inlined callee's - share by liveness** (`layoutSsaTemps`; the IR has no
  lifetime marks). An object is live from a touch of its address (a load, store, memcpy or memset through it, a call it is
  handed to, the call it comes back from) on; one whose address never leaves those loads, stores and copies is live only up to
  its last touch. One whose address has left stays live to the function's end, **except past a call of its own class's
  destructor on the object itself** (the Itanium name read: _ZN, names by length, D1Ev or D2Ev; `this` the alloca, its pointee
  the alloca's type), until it is touched again. Two objects never live together, and never one touched while the other is
  live, take one group of bytes, in frame order, so a function with nothing to share keeps its text. Past 256 such objects each
  keeps its own. **The rule's edge, said plainly:** a program that destroys a local by an explicit destructor call and then
  reaches its bytes only through a pointer it stored elsewhere (no touch by name) would find them shared; the suite has none.
  An inlined destructor is no call, so at -O2 such a temporary stays live to the end (c6x-class-values -O2 is unmoved for it).

Not done: the -O0 "word per value" half (a value's word shared by liveness) - the selector reads a fused or remade value's
operands at its user, so the live range is not the IR's; left for a round that has the selector's own use points.

**Case** tests/cases/c6x-frame-sharing (clang's .expected, the Windows box's clang, the same three lines as g++'s): a ledger
class of 60 bytes in sibling blocks, nested blocks, a loop body, both arms of an `if`, a copy beside its source, a by-value
argument, and across a `try`, its handler and the block after. `blocks`' frame at -O2 864 -> 352 bytes, `thrown`'s 320 -> 120;
LEDGER live, double, phantom, over, lost 0; right at -O0, -O1, -O2 SIM=0 and SIM=1 and by the walker (Linux).

**Gate (coding lane):** g++ -Werror (Linux) and cl /W4 /WX (Windows, work\fsb) clean on ec041621; **tests/tms6747.sh SIM=1** on
the final tree, Linux, all six shards (a shard runs in ~35 s there; 4-6 run in a second tree ~/fs0 holding the same cxx3):
-O2 834 passed / 0 failed, -O0 834 / 0 - every ledger case's line among them - and -O1 834 / 0;
Windows shards 4-6/6 (work\fsb, the cl build) at -O2 410 / 0 and at -O0 410 / 0; walked
-O2 47 (baseline 47), -O0 34 (W7's 34): not raised; c6x-declines 0; tools/exclusions, the comment cap and the memory gate hold;
self-compile of C6xSelect.cpp, Tms6747.cpp and C6xPack.cpp at -O0 and -O2 6 / 0 (Linux). The first Windows build (work\fs) was
capped at 9 minutes on the shared box and not retried; the final tree's (work\fsb) built in about 7.
**Cycles** (tools/c6x-cycles --check, six shards from Linux; nothing accepted or written by the lane): **-O2 732 better, 10
equal, 73 worse of which 17 unowned (W7 20), total 149,298,753 - 267,873 below W7's 149,566,626**; against W7 case by case 61
better, 734 equal, 35 worse: array-loop-throw -103,671, try-body-local -61,968, lifetime-ledger 137,777 -> 116,814 (below its
116,875 of before W7), ssa-ctor-delegate -20,524, goto-out-of-handler -11,639, complete-object-builder -10,377; the worst worse
map-set-node-stability +2,017 (7,422,610 -> 7,424,627 with the same packets, from the temporaries' part; its cause not
read), array-loop-local +108, the rest under 22 each. c6x-class-values -O2 stays 105,790: its
copies' destructors are inlined, so the liveness rule keeps them apart - still owed. **-O0 605 better, 7 equal, 202 worse of
which 44 unowned (W7 47), total 320,763,325 - 674,972 below W7's 321,438,297**; against W7 63 better, 737 equal, 30 worse:
try-body-local -226,026, local-in-handler -113,607, lifetime-ledger 495,979 -> 419,855, ssa-ctor-delegate -74,436,
goto-out-of-handler -41,371, vector-erase-destroys -40,227, c6x-class-values 380,355 -> 342,604. **Three ledger cases are worse
than W7 at -O0**: ssa-reference-conditional-converted +40,301, constructor-unwinds-parts +37,755, lambda-deduction-temporaries
+10,245 (this one from the locals' part alone) - each about one more full `lfFind` scan (~37,900 cycles at -O0): the ledger
keys a row by address and class name, and an address now shared by objects of two classes is a new row for each; read from
the size of the step, not profiled. They are the coordinator's to judge under the cycle standing rule.
**Stale accepted lines** (the tool's STALE list; the coordinator takes them out): -O2 static-local-braced-list,
deduce-through-function-type, array-loop-throw, lambda-reads-static-local, ssa-ctor-delegate, member-pointer-models,
call-nullptr-static-ssa, member-template-body-unchosen, try-body-local; -O0 lambda-reads-static-local, ssa-ctor-delegate,
alias-declaration, by-value, overload-by-parameter, member-pointer-global, member-pointer-models, derived-to-base-copy,
lambda-class-scope-names, by-value-destructor, try-body-local, operator-delete-virtual, template-class,
member-pointer-overload-by-type, member-struct-return, class-temporary, namespace-relative-sibling.
**Owed to the wave gate:** SIM=0 over the suite, -O1, the walker's legs, asm6x, identical.sh ARCHS=tms6747 (frame operands
move in every selected function with locals in sibling scopes), ti-link, run.sh and winpass on the other targets (Ir.h,
Lowering.cpp and SsaInline.cpp carry the serial; no other backend reads it), ssa.sh, the census on the Windows cl build, the
three worse ledger cases' judgement, c6x-class-values -O2, and the "word per value" half.

### M6a.2 FS2: the temporaries' liveness rule made sound, and -O0's value words measured - lane fs2, 2026-11-18

Branch m6-fslots from febbef9e: 106a7670 (the rule, the case), ece4b6f5 (its comment inside the cap), a5c5147c (the block order
at -O0). src/ 95,180 -> 95,182 lines.

- **What lane fs's rule did, measured.** `layoutSsaTemps` ended an escaped object at "a call of its own class's destructor on
  the object itself" - and asked `CallInfo::hasThis` of that call. The lowering's own destructor calls (`Lowering::destroy`,
  the scope's end, a temporary's, a pad's) carry no hasThis, and every call that carries it hands `this` through a bitcast, not
  the alloca: **the end fired on no call at febbef9e**. The hole the rule's words had (`p->~T(); new (p) T(..)` and the object
  reached through a stored pointer) was therefore not reachable, and neither was the sharing of class temporaries the rule was
  written for: tests/cases/c6x-frame-reuse-placement passes at febbef9e at -O0, -O1 and -O2 (Linux, SIM=1) - it could not be
  made to fail there, and this entry does not claim it did.
- **The sound rule, and why.** An automatic object's storage lasts to its block's exit, a temporary's to its full-expression's
  end ([basic.stc.auto], [class.temporary]); a destructor called by name ends the lifetime only, and [basic.life]/5-6 lets the
  program build there again through any pointer it kept. The destructor calls the lowering itself writes sit exactly where the
  storage ends (the scope's or full-expression's exit, on the normal path and in the pad), so past one of them no stored pointer
  may be used. So: **the lowering's own destructor call on the alloca ends the object; one the program wrote by name never
  does** - `ir::CallInfo::byName`, set where `Lowering::pseudoDestructor` writes the call, kept by the inliner's copy of the
  CallInfo, printed as `byname` in the IR text. hasThis is no longer asked; the class is compared unqualified. A fold of
  same-type bitcasts would have opened the hole silently; the flag closes it by construction, not by the bitcast.
- **What it buys.** Class temporaries the lowering destroys by a call now share: the case's shared() frame 168 -> 104 at -O0,
  176 -> 112 at -O1 (176 at -O2: the inliner takes ~Cell, no call is left to end it - c6x-class-values -O2's cause, still owed).
  reuse() and extended() keep `a`'s bytes apart from every later object at each level (136/152 at -O0, 144 at -O1/-O2).
- **-O0's "word per value", measured and not kept.** At -O0 a block's values already sit in A16.. within the block (ssaAssign);
  a word goes only to a value live across a call or a block. Lending a block-local value's word to the block's later values
  (a fused compare read at the branch) passed 835 / 0 at -O0 and moved the total 320,539,886 -> 320,532,248 (-7,638, 0.002%)
  for 60 lines: dropped; the patch is boxkit/fs2-step2-lent-words.patch. The -O0 cost was elsewhere:
- **The selector's block order at -O0 too** (a5c5147c, one line). At -O0 the C6000 selector laid a body's blocks in the IR's
  order unless the body was wrapped (pads, noexcept), and the lowering makes the return block early: a block ended `B next;
  NOP 5` round it where the walker fell through. Found by the static counts of the 44 unowned -O0 cases against the walker's
  text (NOP cycles up in 32, loads and stores level or down; boxkit/fs2-hist.py). `ssaBlockOrder` - the one order of every
  selector, already this selector's at -O1/-O2 and for wrapped bodies at -O0 - is now its order at every level: -O0
  320,539,886 -> **307,814,752** (-12.7 M, -4.0%), 828 of 831 cases better than lane fs's, none worse; -O1/-O2 text unmoved.

**Cycles** (tools/c6x-cycles --check, Linux shards 1-6, a5c5147c): -O2 149,298,753 -> **149,287,156**, -O0 320,763,325 ->
**307,814,752** (320,539,886 at ece4b6f5, before the block order). Against lane fs case by case: -O2 11 better, 820 equal, 0
worse (ssa-catch-by-value-throw-out -10,289 by the rule; nine by the order, in bodies the selector writes unoptimised at -O2); -O0 828 better, 3 equal, 0 worse (at ece4b6f5: 5 better by one ledger scan each, 37,289 to 37,555 cycles, 1 worse,
by-value-lowered +1,216 - now -55,621). Unowned worse: -O2 17 as before; **-O0 44 -> 4** (aggregate-member-from-own-class,
plain-class-local, anonymous-union [inline copy], ssa-bitfield-step-deref-call [64-bit bit-field]). Each of the 17 and of
the 44 (the record at ece4b6f5) and the 4 is named in boxkit/fs2-worse-O2.txt and -O0.txt with the
baseline, lane fs's count, the all-walked count of the same tree and the static instruction counts against the walker's text:
-O2 [pipeline] 2 (M6c), [value call] 7 (M6b), [inline copy] 7 (M6b), [class argument] 1 (FS3) - ten of the 17 are under
today's all-walked count, worse only than the old walker's line; -O0 **[block order] 32** (the -O0 selector lays blocks in the
IR's order and ends a block `B next; NOP 5` round the early return block where the walker fell through - owed FS3:
ssaBlockOrder at -O0), [inline copy] 9 (M6b), [value call] 2 (M6b), [64-bit bit-field] 1 (the lowering). The three ledger cases
worse than W7 at -O0: with the block order ssa-reference-conditional-converted is under W7's count (576,523 against 589,340),
constructor-unwinds-parts +4,545 over it (350,919) and lambda-deduction-temporaries +9,453 (575,672), all under the
baseline. The frame rule left them unmoved and they are **not profiled to the function**: measured is that a miss in the ledger's table costs
37,289-37,555 cycles at -O0, so +40,301 and +37,755 are one more miss each and +10,245 hits on later rows; the candidate is
the frame's size moving a callee's object off an address the ledger had seen (layout chance, not text) - the coordinator's to
judge, FS3's to read. map-set-node-stability -O2 +2,017: unmoved, not profiled (a hypothesis in fs2-worse-O2.txt). Left for
sharing: a temporary under an outer cleanup counts live into that pad along its own constructor's unwind edge (the case's
extended()), FS3.
**Stale accepted lines** (the coordinator's to take out): -O2 lane fs's nine and ssa-catch-by-value-throw-out, mir-volatile, ssa-volatile (12,
boxkit/fs2-stale-O2.txt); -O0 **174** (boxkit/fs2-stale-O0.txt; 18 at ece4b6f5 - the block order took the rest under their
baselines).

**Gate (coding lane):** Linux g++ -Werror clean; tests/tms6747.sh SIM=1 on the final tree a5c5147c, Linux shards 1-6: -O0 835
passed / 0 failed, -O2 835 / 0, -O1 835 / 0 (every ledger case's line among them: live, double, phantom, over, lost 0); walked -O2 47,
-O0 34 (ece4b6f5; the order selects nothing): not raised; tests/g-same.sh 888 same, 0 differ; c6x-declines 0; tools/exclusions, the comment cap (0 over), the memory gate hold; self-compile of
C6xSelect.cpp, Tms6747.cpp, C6xPack.cpp at -O0 and -O2 6 / 0. The case's .expected is the Windows box's clang
(`-nostdinc++ -isystem include`), the same three lines as g++'s on Linux. Windows (work\fs2): the full cl build passed the
9-minute cap (12 minutes on the shared box; a defect, not retried - it finished by itself), then C6xSelect.cpp alone was
recompiled /W4 /WX clean and linked for ece4b6f5 and again for a5c5147c (boxkit/fs2-windelta.cmd); the case right at -O0, -O1,
-O2 SIM=1 and SIM=0 and by the walker on both trees; shards 4-6/6 SIM=1 on a5c5147c: -O2 414 / 0, -O0 414 / 0.
**Owed to the wave gate:** a full clean cl build of a5c5147c (Ir.h, IrText.cpp and Lowering.cpp were compiled /W4 /WX in the
capped build; only C6xSelect.cpp since); SIM=0 over the suite; the walker's legs; asm6x, ti-link, identical.sh ARCHS=tms6747
(every -O0 function's block order moves) and the -g legs beyond g-same (the lexical blocks' $blk$ ranges under the new order:
debug stepping on vm6747/sim6747, release-check); run.sh, winpass and ssa.sh on the other targets (CallInfo::byName is read
by the C6000 selector alone; IrText prints `byname`, in no recorded dump today); the census on the cl build; FS3's three
(a constructor's unwind edge, the ledger cases' reading, map-set-node-stability) and c6x-class-values -O2.

### The M6 wave landed - W5, W6, W7, FS, FS2 and wf7, 2026-11-18 (the coordinator)

- C6000 walked -O0 1,036 -> 34, -O2 1,117 -> 47 (memcpy, memset, class arguments and results, indirect and variadic
  calls selected; frame slots shared by scope; the -O0 block order). Cycles against 458f8974's baseline: -O2 192,025,550
  -> 149,287,156, -O0 397,272,350 -> 307,814,752. The shared Lowering.cpp/Ir.h/SsaInline.cpp mends change x86_64 and
  arm64 output too (code removed, frames renumbered; run.sh, winpass and the Mac legs pass).
- **The user's decision**, given directly in the coordinator's session ("Yes, write them", 2026-11-18): the 21 worse
  cases with no line (17 at -O2, 4 at -O0) accepted, each owed to its round (M6c's pipeliner, M6b's inliner over calls
  through a value and its copy rounds, FS3, the lowering) - f2b9b7b5.
- **Gate: tools/wave-gate m6i -w 156a6b46 at f2b9b7b5**, run as halves (WAVEGATE_HALVES: Linux+Mac, then Windows) so the
  two gates of the day did not share Windows: **every step passed**. Linux's census, declines and asm0/asm1 first failed
  on the disk at 71-73% (cleaned to 30%), then passed on the rerun; asm2 (854 objects, 0 failed) and cycles were never
  taken by `-k` (the legs had no step of their own left) and were run by hand on Linux the way the gate runs them:
  c6x-cycles --check over both halves joined (832 cases, rc 0, 70 worse all owed or accepted, 0 missing or stale). The
  fixed points were not owed (under 100 src/ lines net). Earlier gates of the same wave (m6g, m6h) failed only on unnamed
  differences, a capped GNU winpass leg, and m6h's cycles passing 17 unowned through wave-expected rows - closed in
  0e955ca8 (a worse case passes only through tests/tms6747-cycles.txt).
- Known and owed: win-structure's unwind faults (four functions with a pad and no call, on main too) are mended on m8-d
  (000dbd71) and the gate made to fail on them (6cede9be), landing with M8.1.
- tests/wave-expected.txt emptied (2,737 rows were this wave's).

### M6a.2 W8: the last walked reasons but R1's - 64-bit arguments past the sixth, an array read, a member's variadic call, a 64-bit divide - lane w8, 2026-11-18

Branch m6a2-w8 from b563f30e: 63b1959f (the selector and the divide), c3eb6e53 (the baseline), a9cf1974 (the case). src/ 95,182 ->
95,208 lines (C6xSelect.cpp, Tms6747.h, SsaDivide.cpp).

- **A 64-bit argument or parameter in the seventh register or later** (`64-bit argument past the sixth` 6 / 6, `64-bit parameter
  past the sixth` 1): the pair's high register is A11, B11, A13 or B13 - saved registers the selector gives as homes. Where one
  arrives or leaves there (`Tms6747::ssaPairPastSixth`, the call's named words from the IR; the parameter's from its type), the
  function takes no saved home at all, and a call that writes one saves A11-B13 in the prologue (`usesSavedPairRegs_`, the
  walker's own flag), as A10-B12 were for a seventh word.
- **An array read that decays** (`aggregate value` 2 / 3): `std::forward<const char (&)[6]>(a)` decayed is lowered `load const
  char [6]` then a bitcast to `const char *` - the read is its address. A non-volatile array read whose every use is a bitcast to
  a pointer is selected as that bitcast (`ssaArrayDecay`), and the frame's liveness follows the address through it as through a
  bitcast (`layoutSsaTemps`' roots and touches), so an object it points at is not taken for dead.
- **A member function's variadic call** (`variadic call` 1, va-arg-shapes' `Tally::add`): the lowering's `namedArgs` counts the
  signature's parameters, `this` among them, so the named words and the area are already the EABI's; the `hasThis` refusal went.
- **`smulh` 10 and `umulh` 2 at -O2**: the C6000 has no 64x64 high product, and cl6x 7.4.4 -O2 calls `__c6xabi_divlli` for
  `x / 10LL` and `__c6xabi_remull` for `x % 7ULL` (measured, Linux box). `SsaDivide` leaves a 64-bit divide by a constant
  that is not a power of two to the helpers on tms6747 (the target's name; a power of two is still shifts), which the selector
  already calls as the walker does.

**Walked count** (tools/c6x-walked.baseline, Linux, g++ build, 1,244 cases): **-O0 34 -> 24, -O2 47 -> 24**, every one
`outside the SSA lowering` (R1's, boxkit/w6-outside-O2.txt; 18 cases). No other reason is left at either level.

**Can M6d delete the walker?** Not yet, for three reasons the census does not count. (1) The 24 R1 functions are still walked
at both levels: until R1's rounds lower them (or they are refused by name), deleting the walker refuses 18 cases on tms6747.
(2) The selector runs inside the walker's shell: `emitFunction`, `emitParams`, `emitExceptionTable`, `emitVariables`/
`emitScope` (-g's records) and `emitData` read Ast.h's `Function` and `Program` (16 reads of `fn.` in C6xSelect.cpp: params'
offsets, the result slot, locals for the frame's sharing, `blockAt` for -g), and helpers the selector calls (`copyBlock`,
`loadPair`, `localAddr`, `wideBinary`) live beside the walker's `visit`s in Tms6747.cpp - M6d must move those reads to the IR or
the typed tree before Ast.h's nodes go. (3) The walker's switches: `CXX1_C6X_WALKER` in Driver.cpp and tools/wave-gate's
`wtms*` legs (the only user in tests/ and tools/), deleted with it; asm6x and the -g legs have no walker-only path (the selector
writes -g since M6a round 6), but their walker-all runs go too.

**Case** tests/cases/c6x-last-walked (clang's .expected, the Windows box's clang, `-nostdinc++ -isystem include`): 64-bit
arguments in the seventh, eighth and tenth registers with values live across the call, one through a function-pointer table, a
`std::forward` of an array reference decayed, a member's variadic call, 64-bit divides and remainders by 10, 7 and -1000003. 0
walked at -O0 and -O2 (the base walks 6 and 9); right at -O0, -O1, -O2 SIM=0 and SIM=1 and by the walker (Linux).

**Cycles** (tools/c6x-cycles --check, Linux shards 1-6, a9cf1974; nothing accepted or written): **-O2 149,248,103** (FS2
149,287,156), 735 better, 9 equal, 71 worse of which **18 unowned**: FS2's 17 at the same counts and string-literal-array-reference
70 -> 96 (+26, main newly selected: it calls `std::forward` and `h()` out of line where the walker walked them in place - the
[value call] family); against FS2 case by case 13 better, 818 equal, 1 worse (deque-both-ends -16,686, unordered-map -13,720,
ssa-divide -6,749 - the helper over the serial high product). **-O0 307,812,480** (FS2 307,814,752), 781 better, 2 equal, 31 worse
of which **4 unowned**, FS2's 4 at the same counts; against FS2 4 better, 826 equal, 2 worse under their baselines
(string-literal-array-reference +6, va-arg-shapes +9). boxkit/w8-worse-O2.txt, w8-worse-O0.txt.

**Gate (coding lane):** Linux g++ -Werror clean; cl /W4 /WX clean on C6xSelect.cpp, Tms6747.cpp and SsaDivide.cpp, linked
(work\w8, a delta over lane fs2's objects: Tms6747.h gained two non-virtual members only - the full clean cl build is owed);
**tests/tms6747.sh SIM=1** on the final tree, Linux shards 1-6: -O2 836 passed / 0 failed, -O0 836 / 0; the walker
(CXX1_C6X_WALKER=1) -O2 shards 1-3 417 / 0; Windows shards 4-6/6 (work\w8, the cl build) -O2 419 / 0 and -O0 419 / 0; census -O2 24, -O0 24 (Linux); c6x-declines 0;
tools/exclusions, the comment cap and the memory gate hold (python3); self-compile of C6xSelect.cpp, Tms6747.cpp and C6xPack.cpp
at -O0 and -O2 6 / 0.
**Owed to the wave gate:** a full clean cl build; SIM=0 and -O1 over the suite; the walker's other legs; asm6x, ti-link,
identical.sh ARCHS=tms6747 (the newly selected functions, and every 64-bit divide by a non-power constant at -O2 now a helper
call); run.sh, winpass and ssa.sh on the other targets (SsaDivide changes only tms6747's IR; C6xSelect is the C6000's alone); the
census on the cl build; string-literal-array-reference's +26 at -O2 with the [value call] family; R1's 24 and the shell's Ast
reads above before M6d.

### M6a.2 R1: the lowering's 24 refusals on the C6000 - 22 lowered, 2 named - lane r1, 2026-11-18

From m6a2-w8 1d2df724. **Walked -O0 24 -> 2, -O2 24 -> 2** (tools/ssa-census -arch tms6747, CENSUS_SOURCES=0, Linux, 86d02a63: 11,516 of
11,518 functions selected in 1,246 cases); tools/c6x-walked.baseline lowered in its own commit. src/ 95,208 -> 95,280 lines
(Lowering.cpp 7,911 -> 7,973; BuilderCopy.cpp, Driver.cpp, C6xSelect.cpp).

**Lowered, by family (22 functions, 16 cases):**
- **throw(...) lists (6)**: the C6000 keeps a list in its table (`specInTable`, the FESD `emitExceptionTable` writes from `fn`), so the body is
  not wrapped: Driver's `wrappedBody` no longer counts `fn.exceptionSpec()`, and C6xSelect's decline of it is gone. x86 is untouched
  (its filter is `specFilter`).
- **named return values (3)**: built with defaults, zeroed or from braces as builtLocal builds a local; inside a handler on Itanium (the
  guard and `leaveHandlers` with the elided slot were already there). The Microsoft ABI keeps the refusal (the funclet).
- **class returns (3 of 4)**: a plain class beside a destroyed local (its bytes saved, then the destructors); a constructed class from a
  derived object (`baseSubobject`); a destroyed class out of a handler (`leaveHandlers`; outside a handler the old order, so
  ssa-return-destroyed's text is as it was).
- **constructors and implicit copies (3)**: a bit-field in a mem-initialiser (`bitStore`, `buildableLayout` takes a scalar one), a
  bit-field and a reference member in an implicit copy constructor, a reference member bound to a base.
- **std::nullptr_t (2)**: returned and called, as a parameter and a local already were.
- **class locals (2)**: from a derived temporary (the base's bytes), from another class's conversion function (`convertedLocal`).
- **the rest (3 of 4)**: an initializer_list of a trivially copied class whose elements the parser recorded none of (bytes, sliced);
  a static member through an object expression (the object evaluated, [expr.ref]/1); a braced list's bit-field.

**Left (2), each outside the SSA lowering on every target:**
- `lowering-static-local-default-arguments` (`_Z1fv`, "a static local built with default arguments"): the parser records no replay of the
  default for a static local (`memberDefaults_` has keys for members, list elements and `^.name` entries only) - a parser record, then two lines.
- `lowering-return-derived-temporary-destructor` (`_Z1fv`, "a class with a destructor returned from another type"): `return L();` into a
  base with a destructor - lowered and taken back: the parser releases the temporary and emits no `L::~L()` (undefined at link on
  x86_64-linux). It needs the parser to destroy the derived temporary (whether the walker's text destroys it was not read).

**Cases:** 13 `lowering-*` cases lose their `.error` and their tms6747 `.notarget` for clang's `.expected` (the Windows box's clang 19.1.5 over
cxx3's library; all equal cxx3's x86_64-linux output); `lowering-named-return-in-handler` and `lowering-return-destructor-in-handler` are
`.notarget` x86_64-windows. New `tests/cases/c6x-lowering-refusals` (each family but the throw lists, which vm6747 cannot unwind -
exception-spec, exception-spec-unexpected and ssa-eh-tail hold those on sim6747): right at -O0, -O1, -O2, SIM=0 and SIM=1, and by the walker.

**The other targets** (Lowering.cpp is theirs too), Linux, 86d02a63: run.sh -O0 1240 passed / 0 failed, -O2 1240 / 0; tools/identical.sh
against the base's binary (~/w8), ARCHS x86_64-linux, x86_64-windows (both spellings), arm64-darwin, LEVELS 0 2: 7,232 compared, **104
differ, all of them the 14 cases that compiled to an error before** (12 x 8, and the two handler cases x 4 - not x86_64-windows); no
other case's text moved.

**Cycles** (tools/c6x-cycles --check, Linux shards 1-6, 86d02a63; nothing accepted or written): -O2 **149,248,103**, -O0 **307,812,480** -
W8's totals to the cycle (R1 moved no case the baseline holds); unowned worse 18 and 4, W8's at the same counts. boxkit/r1-worse-O2.txt, -O0.txt.

**Gate (coding lane):** g++ -Werror clean; tests/tms6747.sh SIM=1 on the final tree, Linux shards 1-6: -O2 850 passed / 0 failed, -O0 850 / 0;
census 2 / 2; c6x-declines 0; exclusions 554 outside() sites (the base's), 0 uncited, 0 stale - the new refusals reuse listed
reasons; the comment cap and the memory gate hold; self-compile of C6xSelect.cpp, Tms6747.cpp, C6xPack.cpp at -O0 and -O2 6 / 0.
**Windows (work\r1x), on 3c6f6414 - the tree before the last commit** (86d02a63 folds refusal sites, restores one order, shortens the case):
full clean cl /W4 /WX build clean; SIM=1 shards 4-6 -O2 424 passed, 1 failed - c6x-lowering-refusals on vm6747 ("an exception
specification that allows types, which this line never writes"), the reason its throw lists came out; -O0 4/6 138 / 0, 5/6 still running at the hour, 6/6 not started (work\r1x-stop). A side compile of the
final Lowering.cpp and BuilderCopy.cpp with cl (work\r1x-o2) wrote no object in 13 minutes - a `cmd /K` left waiting, stopped by its PID, not retried.
**Owed to the wave gate:** the cl build and Windows shards 4-6 on 86d02a63; winpass on the 13 newly compiled cases (never run on
x86_64-windows); names.sh on them (their constructors are inline); SIM=0, -O1, the walker's legs, asm6x, ti-link; ssa.sh; make test.

**What M6d still needs:** the 2 functions above (or their two cases declared the walker's last and refused by name on the C6000 as on
x86); then the shell's reads of Ast.h - emitFunction, emitParams, emitExceptionTable (the FESD is now read from `fn.exceptionSpec()` for
selected bodies too: it wants the list on ir::Function), emitScope, emitData, the 16 `fn.` reads - CXX1_C6X_WALKER and the gate's wtms legs.

### M6d, first round: the walked count at 0, and the shell's first reads off Ast.h - lane m6d1, 2026-11-18

From m6-r1 e6a662bd. src/ +7 lines (Driver.cpp, Tms6747.cpp, Tms6747.h, C6xSelect.cpp, Lowering.cpp, Lowering.h).

**(1) Walked -O0 2 -> 0, -O2 2 -> 0** (tools/ssa-census -arch tms6747, CENSUS_SOURCES=0, Linux: 11,510 of 11,510 functions selected in
1,246 cases; tools/c6x-walked.baseline lowered in its own commit). The two functions R1 left are **refused by name on the C6000**, as on
x86 and arm64: Driver.cpp prints a function `outside the SSA lowering` as an error for tms6747 too, so the walker is no second path for
a refused function. `lowering-static-local-default-arguments` and `lowering-return-derived-temporary-destructor` lose their tms6747
`.notarget` lines; their `.error` (the reason, cited in docs/EXCLUSIONS.md's owed block, whose head now names the C6000) holds on all
four targets at -O0 and -O2. Neither was lowered: the first wants a parser record (`memberDefaults_` for a static local), the second
the parser's destruction of the derived temporary - both C++11, owed, on every target.

**(2) The shell, what moved** (each step of identical tms6747 text against e6a662bd's binary, tools/identical.sh ARCHS=tms6747 LEVELS "0 2"):
- **the IR's function is always there**: tms6747 lowers under `CXX1_C6X_WALKER` as well (the switch is read by `ssaChoose`, no longer by
  Driver.cpp), and `emitFunction` finds `irFn` by the symbol once - none is `unsupported`, by name. `ssaChoose(fn, f)` takes it.
- **`emitExceptionTable(noexcept_, spec)`** takes no Ast.h `Function`: the throw(...) list is `ir::Function::spec` (filled from
  `FunctionFacts::spec`, which Driver.cpp's `factsOf` copies), the table's names are `functionName_`.
- **`emitFunction`** reads `irFn` for the symbol (labels, `.global`/`.weak`, the exidx entry), `isStatic`, `isInline`, `variadic`, `pos`
  (-g's first line), `noexcept_` and `spec`: 23 lines reading `fn.` -> 12. C6xSelect.cpp 19 -> 16.

**(2) The shell, what is left on Ast.h, and why it did not move in this hour.** What `ir::Function` does not carry: the parser's frame -
`params()` (type and offset: `emitParams`, `layoutSsa`, `ssaAssign`, `selectBody`, `ssaOutside`, the variadic start), `sretSlot()`,
`returns()` as declared (`f.returns` is unqualified), `alias()`, `hasLandingPads()` (`f.landingPads` also counts the lowering's own pads:
reading it would move `needFrame` and 8 bytes of frame), `locals()` and `blocks()`/`blockAt()` (-g's `emitVariables`/`emitScope`, the
frame sharing of `layoutSsaLocals`, `ssaLocalAllocas`), `name()`; and `emitData`/`emitGlobal` read `Program`'s globals. These are
records, not nodes (`Param`, `Local`, `Function` in Ast.h), and every helper that reads them (`stackParamOffset`, `resetBlocks`,
`planParams`) is shared with the walker's path. **So the order is the other way round**: delete the walker first (it needs only the
walked count at 0, which holds), then give the shell one record of the frame - on `ir::Function`, or the parser's `Function` kept as a
record in a header of its own once its `body_` is gone - rather than a copy of each field while two paths read it.

**(3) Not done:** the walker (`Tms6747`'s `visit`s, `Walker` as its base, the plan/inline helpers), `CXX1_C6X_WALKER` (now in
C6xSelect.cpp's `ssaChoose`) and tools/wave-gate's `wtms0/1/2` legs all stand. `tools/ast-residue --count` is unmoved, the start's to the
unit: ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, visits 124, readers 14, ast-lines 850 (--check holds, +0 each).

**Cycles** (tools/c6x-cycles --check, Linux shards 1-6, final tree; nothing accepted or written): -O2 **149,248,103**, R1's to the cycle,
18 unowned worse (R1's count); -O0 on the same text (identical.sh 0 differ) - its sum is in the handover.

**Gate (coding lane), final tree 91857562, Linux (~/m6d1b):** g++ -Werror clean; identical.sh ARCHS=tms6747 LEVELS "0 2" against
e6a662bd's binary (~/r1b): 1,808 compared, **0 differ**, and 1,808 / 0 differ with CXX1_C6X_WALKER=1 on both sides (the walker's text
with the IR lowered beside it); tests/tms6747.sh SIM=1 shards 1-6: -O2 850 passed / 0 failed, -O0 in the handover; census 0 / 0;
c6x-declines 0; exclusions 554 sites, 0 uncited, 0 stale; the comment cap and the memory gate hold; self-compile -arch tms6747 of
C6xSelect.cpp, Tms6747.cpp, C6xPack.cpp at -O0 and -O2 6 / 0 (Driver.cpp is no C6000 source: `<unistd.h>` refuses the target by name).
**Windows and what the wave gate owes:** in boxkit/m6d1-handover.md's last lines (the cl /W4 /WX build and shards 4-6 as far as the hour
went); x86 and arm64 identity for Lowering.cpp's one line and Driver.cpp's (identical.sh, the other ARCHS), run.sh, winpass, ssa.sh,
SIM=0, -O1, the wtms legs, asm6x, ti-link, make test.

### M6d, second round: the C6000 walker deleted, and the frame's record on ir::Function - lane m6d2, 2026-11-18

From m6d-shell 9289ca3b. src/ 95,287 -> 93,441 lines (-1,846: 195 added, 2,041 removed over 14 files).

**(1) The walker is gone** (commit b619033a, src/ -1,942 +52). `Tms6747` derives from `Regions`, not `Walker`: its 15 `visit`s and
every helper only they reached - the plan and layout of the two walks (`planParams`, `planLocals`, `planRegisters`, `planFrame`,
`placed`, `noteUse`, `noteExtent`, `frameSlot`), the spills and pushes (`planSpills`, `chooseSpill`, `spillPush`, `spillPop`, `push`,
`pop`, `pushValue`, `popValue`, `moveValue`), inlining in place (`inlineTarget`, `walkInPlace`, the `Inliner` and `opt::Costs` it
held), `genAddr`, `genArg`, `genTruth`, `earlyExit`, `simpleOfParams`, the bit-field trio, `immediateBinary`, `constDivisor`,
`divideByConstant`, `fpConst`, `isZero`, `addOffset`, `regFor`, `regRead`, `regCandidate`, `walkBody`, the branch hooks, `low32` -
and their state. `src/backend/Walker.cpp` and `Walker.h` (505 lines: the statement visitors and `AstCodeGen`) had this one user and
go with it; `Regions`' two pure hooks a walker called (`landingPad`, `caseBranch`) are empty overrides. Tms6747.cpp 2,490 -> 1,212
lines. **A body outside the selector is refused by name** in `ssaChoose` (`<symbol>, outside the C6000 selector (<why>), is not
supported yet by the tms6747 backend`): the census has none (walked 0 / 0). `CXX1_C6X_WALKER` is read nowhere; tools/wave-gate's
`wtms0/1/2` legs, their box-table entries, group and report text are removed (tests/tms6747.sh had no walker mode of its own; nothing
else under tests/ or tools/ set the switch). `emitExceptionTable` takes the selector's rows as they are (the walker's joining of
pure cleanup rows went with `ssaRows_`). ide/ regenerated (102 sources).

**(2) The frame's record off Ast.h** (commit c29ce92b): `ir::Laid` on `ir::Function` (Ir.h: `LaidParam`, `LaidLocal`, `Laid`) - the
parser's parameters (type, offset), locals (name, static name, type, offset, scope, serial, isParam), the lexical blocks and where
each opens, the declared result type, `sretSlot`, the parser's own pads, `alias`, `name`. Driver.cpp's `laidOf` fills it from Ast.h's
`Function` once `Lowering::lower` or `Builder::build` has made the function - one place, every target. Read from it now:
`emitParams`, `emitVariables`, `emitScope`, `declaresAnything`, `stackParamOffset`, `emitFunction` (which takes the symbol alone),
`ssaChoose`/`ssaOutside`, `layoutSsa`, `layoutSsaLocals`, `ssaAssign`, `selectBody` - **no `const Function &` is left in Tms6747.h,
Tms6747.cpp or C6xSelect.cpp**. Two shared facts took the record as their one implementation rather than a copy: `ssaLocalAllocas(f)`
(Regions.cpp; x86's and arm64's `ssaFrameLocals` call it) and `blockAt`, which is `ir::Laid::blockAt` only - `Function::blockAt` is
deleted from Ast.h (x86's `ssaMark` and arm64's selector read `f.laid`; x86's `ssaFn_` went), `Function::blockPositions()` hands the
positions over. **Not moved: `emitData`/`emitGlobal`** - they read `Program::globals` (`Global`, its pieces and relocations), and
`ir::Module` holds no data at all: that is a record of its own (the module's data, for every target), not a field, and an hour did
not hold it. `run(const Program &)` reads the functions' order by symbol, the globals and `initFunction`.

**(3) tools/ast-residue** (baseline lowered in its own commit 95504b22): ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413 -
unmoved, and none of them is the C6000's (the four are counted under src/parser and src/sema: the parser's building half, `Classes`'
synthesised bodies, the constant evaluator); **visits 124 -> 0** (all 124 were the C6000 walker's; no generator visits a node now);
**ast-lines 850 -> 845** (`blockAt`); **readers 14**, unmoved - of them the C6000's is Tms6747.cpp's own include (0 node-kind names:
`Program`, `Global`, `Segment` for `emitData` and `run`), and `src/optimizer/Inliner.h` (2 node-kind names) lost its C6000 user
here: who else holds it (Optimizer.cpp, OptFunction.h) is the next round's question, not answered in this one.

**Gate (coding lane).** Each step against 9289ca3b's binary (Linux ~/m6d1b's cxx3.exe, kept as ~/m6d2-ref.exe), tools/identical.sh
LEVELS "0 2": step 1 tms6747 **1,808 compared, 0 differ**, x86_64-linux + x86_64-windows + arm64-darwin **7,232 compared, 0
differ**; step 2 the tree before `blockAt` moved (~/m6d2b) tms6747 1,808 / 0 differ, and the **final tree** (~/m6d2c) tms6747 **1,808
compared, 0 differ**, the three other targets **7,232 compared, 0 differ**. tests/g-same.sh and tests/debug.sh: g-same 888 same, 0 differ, 0 broken (358 not for tms6747); debug.sh gdb 5 passed, 0 failed; and, since
identical.sh compiles no -g, every tests/cases file's `-g -S` text whole against the REF's (boxkit/m6d2-gdiff.sh, the tree's path
normalised): tms6747 888 same, 358 refused alike, **0 differ**; x86_64-linux and arm64-darwin 903 same, 343 refused alike, **0 differ**
each (their locals and blocks now come from `f.laid`). tests/tms6747.sh SIM=1, final tree: Linux shards 1-3
-O2 425 passed / 0 failed, -O0 425 / 0 (and 4-6 there too: 850 / 0 at each level); Windows shards 4-6 (the cl build) -O2 425 / 0; -O0 4/6 138 / 0, then stopped by the lane's stop file - a
Windows shard took 3 to 6 minutes on the shared box against Linux's 20 to 60 seconds, so -O0 5/6 and 6/6 are Linux's alone. tools/c6x-cycles --check: -O2 **149,248,103**, -O0 **307,812,480**, both the base's to the cycle (the text is
identical), with the base's 18 and 4 worse cases neither owed nor accepted, so the check fails as it did at 9289ca3b; nothing
accepted or written. Census -O2 and -O0 11,510 of 11,510 selected, walked 0 / 0; c6x-declines 0 (baseline 0 holds); the two
lowering-* cases refused as their `.error` on all four targets at -O0 and -O2; self-compile -arch tms6747 of C6xSelect.cpp,
Tms6747.cpp, Regions.cpp at -O0 and -O2 6 / 0; exclusions 554 sites, --check rc 0; comment cap 0 over; memory gate holds; ide/generate.py --check
up to date; g++ -Werror clean. **The Windows cl /W4 /WX build** (work\m6d2x, `_CL_=/MP4`, /affinity 3FF, alone in its tree): step 1's
tree 6 min 33 s, step 2's 2 min 47 s, the final tree 3 min 39 s (one comment line
older than commit c29ce92b) - all inside the 9-minute cap, none retried.

**Owed to the wave gate:** Windows shards 5/6 and 6/6 at -O0 (SIM=1) on the cl build; run.sh, winpass (both spellings), ssa.sh
1/2, names.sh, make test to its end, tms6747 SIM=0 and -O1, asm6x, ti-link, release-check, lldb/cdb (-g on x86 and arm64 now reads `f.laid` for locals and blocks), the fixed point; the guide's
line on `backend/Walker.h` and tools/ssa-census's `c6x-walker` count (now always 0: a refusal is an error) are the coordinator's to
retire; `emitData` on a module data record; `Inliner`'s remaining users.

### M6d, third round: the data record on ir::Module, the Inliner's Ast.h half, and what the residue still is - lane m6d3, 2026-11-18

From m6d-shell 11ced288. src/ 93,441 -> 93,261 lines (-180). REF is 11ced288's code built on Linux (`~/m6d2c-final.exe`); every step
is the same text on four targets (tools/identical.sh, 1,808 tms6747 and 7,232 x86_64-linux, x86_64-windows and arm64-darwin compares) and
the same `-g -S` text for every case on tms6747, x86_64-linux and arm64-darwin (boxkit/m6d3-gdiff.sh, REF's tree path written as the new one).

**(1) The data record** (0b37cbad, src/ +317 -289). `src/UnitData.h` holds `Global`, `GlobalPiece`, `StringLit`, `TlsWrapper` and
`UnitData` - objects, strings, the functions' symbols in order (`order`), the Microsoft throw and RTTI lists, the init functions,
`usesDsoHandle`, the dialect - and `ir::Module::data` is it. The parser and `Classes` write `program.module.data`; `CodeGen::run`
takes the `ir::Module`, and every generator's `emitData`, `emitGlobal`, TLS wrappers and COFF/MASM RTTI read `UnitData`. The C6000
names no Ast.h type at all (`Tms6747.cpp` and `Backend.h` no longer include it); x86 and arm64 are handed the Ast.h `Function`s
their shells still read by `CodeGen::setFunctions` - the frame record they have not moved to `ir::Laid`. `Program::thrown` had no
reader and went. The header is in src/, not src/ir/: `Global::type` and `rtti` are `const Type *`, and the memory gate counts raw
pointers under src/ir - moving them to `TypeId` is the record's next step, not hidden by the directory. (The commit also carries
the removal of tools/c6x-walked.baseline, which belongs to (2).)

**(2) The Inliner's Ast.h half, and the walked count** (3444ae95, src/ +3 -158). `Inliner` has one user, the SSA inliner
(SsaInline.cpp), which asks `decide` alone: `summarize`, the node `Measurer`, `allows`, `eligible`, `size`, `leaf`, `largestFrame`
and their maps are deleted, and Inliner.h no longer includes Ast.h. tools/ssa-census no longer counts `c6x-walker` lines (there is
no walker; a body outside the selector is an error): the `walked` function, the `case` job, `CENSUS_SOURCES` and
tools/c6x-walked.baseline go, and tools/wave-gate's census leg reads the `outside=` line alone (one job, not two).

**(3) What the residue still is** (ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, readers 14 -> 10, ast-lines 845 -> 739).
No generator and no lowering reads a node: `ir::Lowering` reads the typed tree, and after e0af95aa (src/ +11 -16) not `Program`
either - its string and object lists are `module_.data`'s, Lowering.cpp does not include Ast.h. The readers of nodes left are
(a) the constant evaluator, `sema/Constexpr.cpp` (about 46 node casts: `fold` over `Expr`, a constexpr body's statements); (b) the parser's own
shape tests and folds (about 120 casts); (c) two `Visitor`s in the parser - `SymbolsNamed` (ParserClass.cpp: which functions,
objects and strings a kept body names, so the unreached are dropped - it decides what is emitted) and `SlotUses`
(ParserExprNew.cpp); (d) `Overload`, `Entities` and `Templates`, which hold a default argument's or an initialiser's `Expr` for (a).
The builders are the parser (about 1,680 of the ExprPtr words) and `Classes`' synthesised bodies (about 200). So the building half
cannot go as a deletion: a statement node is read by `SymbolsNamed`, `SlotUses`, the evaluator's walk of a constexpr body and a
handful of the parser's own casts, and those must first read the typed tree (or the IR: the reachability walk is a walk of `ir::Module` calls and `Addr`s). That is
M6d's remaining design step, and it touches ParserClass.cpp's prune, which decides output.
What nothing read went (5d5ca21a, src/ +4 -52): `Try::alsoCleanup`, `typeIndices`, `unwindHelpSlot`, `msExits` and
`Block::unwindCleanup`, with their setters and the parser's calls. The frame slots and selector indices are still taken where they
were (`msUnwindHelp()`, `typeIndexFor`), so frames are unchanged; whether a slot nobody reads may go is a change of output, owed.
Seven refusals moved a line; docs/EXCLUSIONS.md's citations moved with them.

**Still unread on Ast.h, not deleted this round** (accessors with no caller outside Ast.h): `Try::pointerSlot`/`selectorSlot`
(constructor arguments at eight sites), `Switch::cases`/`defaultCase`, `Call::namedArgs`, `Postfix::increment`, `Block::scope` and
`For::scope` (`setScope` has callers), `Function::setBody`. Readers left: Parser.h, ParserInternal.h, Classes.h, Constexpr.h,
Entities.h, Linkage.h, Overload.h, Templates.h, SemaC.cpp, backend/Dwarf.h (`Local`, for x86's and arm64's -g).

**Identical, by step** (Linux, REF 11ced288): step 1 tms6747 1,808/0 differ, x86+arm64 7,232/0, -g 888 same/358 refused alike/0
differ (tms6747), 903/343/0 (x86_64-linux), 903/343/0 (arm64-darwin); step 2 1,808/0 and 7,232/0; steps 3A+3B (the final tree,
built together) 1,808/0, 7,232/0 and -g 888/358/0, 903/343/0, 903/343/0. ast-residue after step 1 and 2: readers 11, ast-lines
769; after step 3: readers 10, ast-lines 739; ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413 unchanged; visits 0.

**Gates on the final tree** (Linux ~/m6d3, g++ -Werror clean; Windows work\m6d3x, cl /W4 /WX clean, twice): tms6747 SIM=1 -O2
shards 1-3 (Linux) 425/0 and 4-6 (Windows) 425/0; -O0 shards 1-3 (Linux) 425/0 and 4-6 (Windows) 425/0; cycles over the six shards
-O2 149,248,103 and -O0 307,812,480 = the base's, 18 and 4 worse unowned as at the base (the check fails as before, nothing written); the two cases refused
as their .error on four targets at both levels; g-same 888/0; debug gdb 5/0; self-compile of C6xSelect, Tms6747, Regions (tms6747,
-O0 and -O2) 6/0; c6x-declines 0; exclusions 0 uncited 0 stale, comment cap, memory gate, ast-residue (baseline recorded),
ide --check hold; tools/ssa-census and tools/wave-gate pass `sh -n` and were not run.

**Owed to the wave gate:** the source census (`outside=`) through the edited tools/ssa-census and wave-gate's census leg;
run.sh, winpass (both spellings: the MASM and COFF RTTI and throw records now read `UnitData`), ssa.sh 1/2, names.sh, make
test to its end, tms6747 SIM=0 and -O1, asm6x, ti-link, release-check, lldb/cdb, the fixed point. **Left of M6d:** x86's and arm64's
frame record onto `ir::Laid` (then `setFunctions`, `Backend`'s `Function` and Dwarf.h's `Local` go); `UnitData`'s `const Type *`
onto `TypeId`; the three statement readers above onto the typed tree or the IR, then the parser's and `Classes`' building half
and Ast.h's nodes - the part that is a design, not a deletion; the unread accessors listed above; the dead frame slots.

### M6d, fourth round: x86's and arm64's frame record on ir::Laid, UnitData's types as TypeId, and the design for the three statement readers - lane m6d4, 2026-11-18

From m6d-shell a2a6f501. src/ 93,261 -> 93,256 lines (-5: step 1 is +27 - the typed -g record, the order walk, two small headers - step 3 -32). REF is a2a6f501's code built on Linux (`~/m6d4-ref.exe`, m6d3's final build);
the compares are tools/identical.sh (1,808 tms6747; 7,232 x86_64-linux, x86_64-windows and arm64-darwin) and every case's
`-g -S` text on tms6747, x86_64-linux and arm64-darwin (boxkit/m6d4-gdiff.sh).

**(1) x86's and arm64's frame record** (28037132, src/ +236 -209). `X86_64Linux::emit`, `receiveParameters`, `returnThis`, `scalarsOf`,
the exception tables, `Arm64Darwin::emitFunction`, `ssaOutside` and both selectors' `layoutSsa` and `selectBody` take the
`ir::Function` and read `ir::Laid`, as the C6000's shell does; `run` walks `UnitData::order` and finds each function in the
module. `ir::Laid` gained what they asked beyond the C6000: `regSave`, `frameAlign`, `hasThis` (the parser's: a synthesised
special's `ir::Function::hasThis` is wider, Driver.cpp), `named` (a display name was given), and a local's `align` and
`declared`; the source position is `ir::Function::pos`, which was there. `CodeGen::setFunctions`, the generators'
`functions_`, `ssaFunction`, the second `Function` parameter of six functions and `Backend.h`'s `class Function` are gone.
-g reads `DwarfLocal` (Dwarf.h), built by `Regions::ssaFrameLocals` from `ir::LaidLocal`; Dwarf.h no longer includes Ast.h,
so no file under src/backend does. Two things the backends had from Ast.h by that include moved: `enum class BinOp` to
Operator.h (the C6000's wide and floating helpers switch on it), `make_owned` to src/Owned.h (the one `new`; the count is
unchanged). The typed helpers (`laidType`, `laidParams`, `FrameParam`) are in the generators' headers and Backend.h, not
Regions.*: the memory gate counts raw pointers there (89, unchanged). arm64's two refusals (a function outside the
selector, a symbol with no body) are one site, `ssaRefuse`, so tools/exclusions' arm64 count stays 4.
One message changed on a path no accepted unit reaches: arm64's "no SSA body" names the symbol, where it named the display name.

**(2) UnitData's types** (f31af70a, src/ +24 -24). `Global::type` is a `TypeId` (the constructor still takes the parser's
`const Type *`), `UnitData::rtti` a `std::vector<TypeId>`; the generators read them through `laidType`, the lowering through
`T()`, the parser's prune through `types_.type`. The header holds no `Type` pointer but `Global`'s constructor parameter.

**(3) The unread accessors** (259fa728, src/ -11): `Try::pointerSlot`/`selectorSlot`, `Switch::cases`/`defaultCase`,
`Call::namedArgs`, `Postfix::increment`, `Block::scope`, `For::scope`, `Function::setBody` - Ast.h alone. Their fields,
constructor arguments and `setScope`'s 27 calls stay: each removal moves parser lines (and EXCLUSIONS.md's citations) for
nodes the design below deletes whole. **(3b) The landing-pad hook** (907830bd, src/ -21): `Regions::landingPad` and its three overrides, the walkers' store of the exception
pointer and selector into the parser's pad slots, had no caller. **The slots themselves were not removed**: the pad's pointer and selector slots and
the unwind-help word are taken by the parser's frame allocator in the order parameters, the result slot and -g's locals are
(`ir::Laid`'s offsets); removing one is a change of layout that has to be shown identical by itself, and goes with lane S5.

**Identical, by step** (Linux, against REF). Step 1 (28037132), built alone in a second tree before two edits that change no
accepted unit's code (`laidType` made protected for MASM, arm64's refusal folded to one site): tms6747 1,808/0 differ,
x86+arm64 7,232/0, `-g` 0 differ on tms6747, x86_64-linux and arm64-darwin once that tree's path is written as the first's
(94, 208 and 208 texts named it in a header's file record and differed in nothing else). Step 2 has no build of its own: with
step 3 it is the final tree (259fa728's code), measured twice, before and after those two edits: 1,808/0, 7,232/0, `-g`
888 same/358 refused alike/0 differ (tms6747), 903/343/0 (x86_64-linux), 903/343/0 (arm64-darwin), and x86_64-windows'
CodeView text 677 same/368 refused alike, the other 201 the same once REF's tree path is written in its backslash spelling.
Step 3b (907830bd, the last tree): 1,808/0, 7,232/0, and `-g` 0 differ on the four targets the same way.

**ast-residue** (ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, visits 0 throughout): after step 1 readers 10 -> 9 (Dwarf.h)
and ast-lines 739 -> 733 (`BinOp`, `make_owned`); after step 2 the same; after step 3 ast-lines 722. Baseline recorded (8f292c96, with
the ide files for Owned.h and the memory gate's `new` 1,053 -> 1,052; raw pointers 89, unchanged).

#### The design: the three statement readers, before the building half

`Function::body()` has three readers left besides the parser's own casts. Each decides something, so each moves first.

**A. `SymbolsNamed`** - ParserClass.cpp:1859-1913, used once, :1998, by `Parser::pruneUnusedInline` (:1920-2030), called once at
the unit's end (ParserTopLevel.cpp:2269), before the driver types or lowers anything. *Reads*: every reached body's
`Var::symbol`, `StrLit::label`, `Call::symbol` and `Try::types` (the type_info symbols), through every node kind; beside them
`Global::prefixWord` and the pieces' symbols, `initFunction`, the Microsoft throw records' destructors and copy constructors.
*Decides*: which of `program.functions`, `data.globals`, `data.strings` and `data.rtti` are emitted - output. *Roots*: every
function not held (`discardableInline_`, an inline function not from a template and not an atexit helper) and every global
`heldGlobal` does not hold (:1938). *Replacement*: the same closure over `ir::Module` - a function's names are its
instructions' call symbols (`ir::CallInfo::symbol`), its address-of-symbol and string operands, its pads' type_info symbols
and its `spec` list - run by the driver, with the holding facts as records: `held` on each `UnitData::order` entry and on
`Global`, `copied` on `StringLit` (today `copiedStrings_`), the key-function test folded into `Global::held` by the parser.
*Two things fix its place.* (i) Today a held function nothing reaches is never typed and never lowered, so a body the
lowering would refuse is accepted when unused; the closure must therefore drive the lowering - a worklist in
`Driver`'s loop over `c.bodies` (Driver.cpp:1466-1525): lower the roots, read their names, lower a held function when it is
first named - never "lower all, then drop". (ii) It reads the IR as lowered, before any SSA pass: the inliner removes calls,
and the kept set may not depend on the level. *Oracle while both exist*: `-check=prune` computes both kept sets and fails
on a difference, over the suite at both levels and the self-host sources; then identical.sh on four targets; then
`SymbolsNamed` and the parser's prune are deleted in the commit that makes the driver's the only one.

**B. `SlotUses`** - ParserExprNew.cpp:15-65, used once, :928, with `constructorOfSlot` (:67-85), in the `return` operand's
third elision form (:919-945): `return T(args);` through a temporary. *Reads*: under the comma the parser itself built two
lines earlier, `Var::isLocal`/`offset` and `Call::resultSlot`, counting the names of the temporary's slot. *Decides*:
`pendingReturnBuilt_.form = 3` with the constructor's symbol and signature - the record the SSA lowering reads under
"$ret:" - and rewrites the Ast body nobody reads. *Replacement*: the count is a fact of how the temporary was built, known
where it is built: the pending temporary's record (`pendingTemps_`) keeps `builtBy` (the constructor call's `Resolved`
index) and `plain` (nothing but that call names the slot - no zeroing step, no guard), set by the one function that
materialises a class temporary; :928 asks the record. *Oracle*: while both exist the record's answer is compared with
`SlotUses`' at :928 and a difference is a compiler error naming the function; 0 over the suite and the self-host, then the
visitor, `constructorOfSlot` and the Ast rewrite (:931-937) go. Output is untouched by construction: the form is the same.

**C. The constexpr body walk** - sema/Constexpr.cpp. `singleReturnValue` (:261-270; asked at ParserTopLevel.cpp:1980) walks
`Block` to its one `Return` and hands back the operand, which `ConstexprFn::value` keeps as a `const Expr *` "owned by the
Function in the Program" (Entities.h:427) - a pointer into a body the prune may drop. `runConstexpr` (:565-572) and
`runConstexprEffect` (:574-600) run a constexpr constructor's lowered statements (`ConstexprCtor::body`, a `const Stmt *`,
Entities.h:438, registered at ParserTopLevel.cpp:1971-1978, run at Constexpr.cpp:526-545) and a declared object's
initialising statements (`foldDeclaredObject`, :634-647; asked at ParserInit.cpp:1541 and ParserStmt.cpp:20).
*Replacement, in two parts.* (C1) The statement shapes stop being Ast shapes: "one return and nothing else" is asked of the
syntax tree (the body's `syntax::NodeId`: a compound of one return), and `ConstexprFn` owns its operand (`ExprPtr value`,
moved out of the `Return`); a constructor's and a declared object's steps are kept as what they are - a list of
(member's byte offset, initialiser expression) and constructor calls - written by the parser where it builds the
mem-initialisers (`ConstexprCtor::steps`), so `runConstexpr` walks records and no `Block`, `ExprStmt` or `Comma` of
statements. After C1 no statement node is read by the evaluator; expressions still are. (C2) The expression evaluator
(`fold` and its 44 other casts) moves to the typed tree as design 12.2 rows it: Sema types one full-expression on demand
during the parse (`Typing` is run today per body by the driver, after the parse; the constant is needed inside it), from
the `Resolved` records the parser has already written for that expression; `fold(TypedId)` mirrors `fold(const Expr &)`
case for case; `-check=fold` runs both wherever a constant is asked and fails on a difference. C2 touches the template
files (`Templates*`, `ParserTemplate*`, `ParserDependent*`: a replayed expression is typed again per instantiation), so it
starts after lane m8x leaves them.

**The parser's own statement casts** (six): ParserExprNew.cpp:292, :431, :1392-1399 (`ExprStmt`/`Block` of what it just
built), ParserStmt.cpp:1473 (`Try`), :1558 (`For`), :1566 (`Case`). Each becomes a value the building function returns
beside the node (the lane reads each; none decides output by itself).

**Order, and tools/ast-residue after each** (from this round's ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, readers 9, ast-lines 722; estimates until each lane measures them):
S1 the prune on the IR (A): node-refs about -30 (the visitor's signatures), the rest unchanged. S2 the temporary's record
(B): node-refs about -30, node-casts -4. S3 the constexpr statements (C1) and the six casts: node-casts about -10,
node-refs about -6. S4 **statements deleted**: the parser and `Classes` stop building `Stmt` nodes (a function keeps its
frame record and its syntax body only), `Function::body`, the fifteen statement classes, `MsHandler` and `Try` leave Ast.h:
StmtPtr 639 -> 0, ExprPtr about -150 (the statements' operands), ast-lines about 720 -> 450. S5 the frame without the
nodes: the dead slots (pad pointer, selector, unwind help) and the setters only nodes had, each shown identical by itself.
S6 on-demand typing and `-check=fold` (C2, no deletion: the residue is unchanged and the check is the deliverable). S7
the parser's expression shape tests (about 120 casts: ParserExprCall.cpp:336-385, ParserInit.cpp:846-947,
ParserOperator.cpp:586-671, ParserExpr.cpp:2383-2546 and :3534-3549, Parser.h:885-917, Parser.cpp:47-61) onto `Resolved`
records and the typed node's kind, a family a lane: node-casts -> about 46. S8 **expressions deleted** in one commit, as
M3b.7 deleted x86's path: `fold(const Expr &)`, the expression classes, `Visitor`, `ExprPtr` (2,016 -> 0), node-casts and
node-refs 0; `Param`, `Local`, `Function` (a frame record by then) and `Program` move to a header of their own and Ast.h
is deleted: readers 0, ast-lines 0.

**What must not change.** CLAUDE.md's shapes: absolute token indices (on-demand typing keys an expression by its syntax
node and token index; a replay re-reads by index, so nothing is renumbered); `resolveOverload` returns a `Signature` by
value; a pointer to member function wears a struct's shape (the evaluator's leaves stay by byte offset); 
`unqualifiedSpecifiers` answers void for `Point::Point(`; call-site rows are registered innermost-first and split, never
sorted (the driver's prune keeps `order` as the parser wrote it and sorts nothing); a trivial special member is no
function at all (a name with no function is not a node of the closure, as today - the closure never makes one); the parser
numbers selectors as the table places them (`typeIndexFor` is called where it is called now, in the same order, when no
`Try` node is built). And this round's: a held function nothing reaches is never typed, lowered or refused; the kept set
does not depend on -O; a literal copied into an array is kept (`copiedStrings_`); the frame allocator hands out the same
offsets in the same order whether or not a node is built for the slot; a refusal keeps its words (a refusal that moves a
line moves its citation).

**As lanes.** S1 Fable (it decides what is emitted, and moves a parser decision into the driver's lowering loop; files
ParserClass.cpp :1856-2030, Driver.cpp, UnitData.h). S2 Opus (ParserExprNew.cpp). S3 Opus (Constexpr.*, Entities.h,
ParserTopLevel.cpp :1960-1990, ParserInit.cpp, ParserStmt.cpp). S2 and S3 run beside S1: no file is shared. S4 Opus, two
lanes by directory (src/parser; src/sema/Classes.*), after S1-S3. S5 Opus, after S4. S6 Fable (typing inside the parse is
a design of its own; after m8x). S7 Opus, three lanes by file family, beside S6. S8 Opus, one lane, one commit, after S6
and S7. Each lane's gate is this round's: identical.sh on four targets and the `-g` texts, 0 differ, against the commit
before it; `-check=prune` and `-check=fold` are run by the wave gate over the suite and the self-host.

**Gates on the final tree** (the builds, the compares and the static gates are of 907830bd; the simulator runs, cycles, census,
declines, g-same and gdb of the commit before it, whose tms6747 and `-g` text 3b leaves the same. Linux ~/m6d4, g++ -Werror
clean; Windows work\m6d4x, cl /W4 /WX clean - its first try refused
`Laid::pos` for hiding `blockAt`'s parameter, C4458, which is how the duplicate of `ir::Function::pos` was found): tms6747
SIM=1 -O2 shards 1-3 (Linux) 425/0 and 4-6 (Windows) 425/0; -O0 425/0 and 425/0; cycles over the six shards -O2 149,248,103
and -O0 307,812,480 = the base's, 18 and 4 worse unowned as at the base (the check fails as before, nothing written); the two refused cases as their
.error on four targets at both levels; g-same 888/0; debug.sh gdb 5/0; self-compile of C6xSelect, Tms6747, Regions,
X86_64Linux and Arm64Darwin for tms6747 at -O0 and -O2 10/0; c6x-declines 0; exclusions 129 sites, 0 uncited, 0 stale, the
lowering's and arm64's counts at their baselines; comment cap 0 over; memory gate and ast-residue recorded lower; ide
--check after regeneration. **tools/wave-gate --box linux census** - its edited leg and tools/ssa-census, run for the first
time since m6d3 changed both: PASS, `outside=0 of 102 sources and 431 C90 cases; other failures 25`, and REF's binary
fails the same 25 files (POSIX headers and the like, refused for tms6747; other.txt is byte for byte REF's)..

**Owed to the wave gate:** run.sh, winpass in
both spellings (the MASM and COFF shells, RTTI and CodeView now read `ir::Laid`, `DwarfLocal` and `TypeId`; only the `-S`
and `-g -S` texts were compared, on Linux), cdb and lldb, ssa.sh 1/2, names.sh, make test to its end, tms6747 SIM=0 and
-O1, asm6x, ti-link, release-check, and the fixed point - owed by N4, a src/ file was added (Owned.h) - with target 4 on
the Mac at the milestone's close. **Left of M6d:** lanes S1-S8 above; CLAUDE.md's "Reading this tree" still names
`backend/Walker.h`.

### M6d S1, first round: the prune's holding facts as records, `-check=prune`, and where the IR's closure is not the parser's - lane m6d-s1, 2026-11-18

From m6d-shell f21d290d. src/ 93,256 -> 93,266 (step 1) -> 93,346 (step 2: +80, the check and its record, both deleted
with the parser's prune) -> 93,350 (step 3). REF is f21d290d's code built on Linux (`~/m6d-s1-ref.exe`, m6d4's last build); the compares
are tools/identical.sh (1,808 tms6747; 7,232 x86_64-linux, x86_64-windows and arm64-darwin) and every case's `-g -S`
text on the four targets (boxkit/m6d-s1-gdiff.sh, the trees' paths written as one in both spellings).

**(1) The holding facts as records** (f487de53, src/ +10). `UnitData::held` (the functions emitted only where named:
`discardableInline_` less a template's, and every inline function not from a template and not an atexit helper),
`Global::held` (the vtable-group and vftable test, the key-function test folded in) and `StringLit::copied`
(`copiedStrings_`), written by `Parser::pruneUnusedInline` before its closure, which now reads them. `StringLit` has
constructors, so its one aggregate site is unchanged. **The design said `held` on each `UnitData::order` entry**: `order`
is written by the driver after the lowering (Driver.cpp, from `program.functions`) and read by three generators as
strings, none of them S1's files, so the record is a list beside it; it becomes a field of the entry when the driver's
prune writes `order` itself.

**(2) `-check=prune`** (0dfce9cc, src/ +80; Driver.cpp `namesOf`, `checkPrune`). The closure over `ir::Module` as lowered,
before any pass: roots are the functions not in `held` and the globals not `held`, with `initFunction` and the thrown
Microsoft records' destructors and copy constructors; a function's names are its `calls[].symbol`, its instructions'
`sym` and `syms` and its `spec`; a global's are its `prefixWord` and pieces. It is compared with what the parser's prune
did, both ways: everything the parser kept must be reached (a function, a global, a literal not `copied`, a Microsoft
class's five records), and nothing in `UnitData::dropped` (the symbols, labels and RTTI names the parser's prune dropped,
written there) may be named. **Both halves hold exactly when the two kept sets are equal, without lowering a dropped
function**: if the IR's closure names nothing dropped it is closed over the kept set. A difference is a compile error
naming the symbol. The check changes nothing unless asked.

**Identical, by step** (Linux, against REF). Step 1, built alone in `~/m6d-s1a`: tms6747 1,808/0 differ, the three
others 7,232/0, `-g` 888 same/358 refused alike/0 differ (tms6747), 903/343/0 (x86_64-linux), 903/343/0 (arm64-darwin),
x86_64-windows 677 same/368 refused alike and the other 201 the same once the tree's path is written in its backslash
spelling. Step 2 (`~/m6d-s1`): tms6747 1,808/0, the three others 7,232/0, `-g` 888/358/0, 903/343/0, 903/343/0 and x86_64-windows
878/368/0. Step 3 (89ae5c60, src/ +4: the check's early return) against step 2's binary, not REF: **`~/m6d4`, the tree
REF's header paths are compiled into, was removed from the box by another hand at about 08:45**, after which REF refuses
every case that includes a library header (a first chain against it read 186 and 1,680 "differ", each REF's refusal):
tms6747 1,808/0, the three others 7,232/0, `-g` 888/358/0, 903/343/0, 903/343/0 and 878/368/0. **ast-residue** is unchanged by both (ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, visits 0,
readers 9, ast-lines 722): nothing is deleted until the driver's prune is the only one.

**What `-check=prune` found: the design's switch is not identical as written.** Over tests/cases (1,247 units, -O0, the
check is taken before any pass; `~/m6d-s1-s3.exe`): 3 units differ on x86_64-linux, arm64-darwin and tms6747, 5 on
x86_64-windows. Two kinds:
- **A function the parser's tree calls and the IR does not** (3 units x 4 targets: the move constructor in
  conditional-const-prvalue-assign and ssa-lambda-locals, `initializer_list<int>::begin` in range-for-braced-list).
  Today the function is emitted and nothing in the text calls it (read in ssa-lambda-locals' x86_64-linux text:
  `_ZN1RC1EOS_` is defined and never called). Why the lowering names no call there was not read.
- **A Microsoft type descriptor a pad names whose class's five records the parser dropped** (2 units, x86_64-windows:
  string-stoi, unused-vtable-not-emitted): the IR's catch pad names `??_R0...`; the parser's rule looks for the five
  names among the Ast's and finds none. Where that descriptor is laid down from was not read.
**The check's first form (0dfce9cc) said 10 and 12**: seven more units, each "a literal the parser kept and nothing
names". That was the check's fault, not the design's: where nothing is held the parser's prune returns before it drops
anything, so an unread literal (`char text[] = "abc";`) stays; step 3 makes the check return there too. **It is a rule
the driver's prune has to keep and the design did not state: nothing held, nothing pruned.**
So a prune over the IR emits a function fewer than today in the first kind and five records more in the second.
**Proposed change to the design** (not taken here; the brief stops the lane at this point): the switch is two steps,
not one. (a) *Identical first*: the lowering records the function it was handed and did not call -
`ir::Function::alsoNamed`, the symbol of the `Resolved` call it elided or expanded - and the RTTI rule skips a
descriptor reached only through a pad; `-check=prune` then reads 0 over the suite and the self-host, and the driver's
worklist replaces the parser's prune with identical.sh at 0. (b) *Then the dead output goes*, by itself: `alsoNamed` is
deleted, the 3 units' texts lose a function each, named in the commit, and run.sh shows the programs unchanged. The
alternative - taking the smaller set in the switch itself - makes the switch's own compare meaningless on those units.

**Also met, for the switch** (none decided here). The census (`Typer::census`, sema/Typing.cpp:955) builds `c.bodies`
from the pruned `program.functions`, and `-check=typed`, `-dump=typed` and `-check=ssa`'s counts read it: with no parser
prune they would type and count the held functions nothing reaches - Typing.cpp is not among S1's files and has to be.
The Builder's C1 of a class with virtual bases reads its C2's typed body (`splitBodies`), which today is there because
every body is typed before any synthesised function is built: the worklist types a C2 when its C1 is reached. And the
worklist changes the order functions enter `ir::Module` (the inliner walks them by index): the generators emit by
`order`, so the text should not move, and identical.sh at -O2 is what says so.

**Gates** (run on step 2's tree, 0dfce9cc - Linux `~/m6d-s1`, g++ -Werror clean; Windows `work\m6d-s1x`, cl /W4 /WX
clean, a delta build of 5 min 12 s; step 3 changes four lines inside the check, built clean by g++ and by cl in 2 min
14 s, and was not run through the simulator): run.sh 1,240 passed, 0 failed; tms6747 SIM=1 -O2 shards 1-3 (Linux)
425/0 and 4-6 (Windows) 425/0, -O0 425/0 and 425/0; self-compile of Driver.cpp and ParserClass.cpp for x86_64-linux at
-O0 and -O2 4/0 - for tms6747 both are refused, as REF's binary refuses them (Driver.cpp's POSIX header;
`Parser::constructObject`'s stack arguments); exclusions 129 sites, 0 uncited, 0 stale (three
ParserClass.cpp citations re-pointed, +2 then +4), the lowering's 554/95 at their baselines; comment cap 0 over; memory
gate new 1,052, raw pointers 89, unchanged; ast-residue not raised; ide --check up to date (no file added).

**Left of S1:** the two causes read (why the lowering calls no constructor there; where `??_R0` is laid down from);
step (a) above (`alsoNamed`, the pad rule) until `-check=prune` reads 0 on four targets at both levels
and over the self-host sources; the driver's worklist over `c.bodies` and the synthesised functions, writing `order`
with `held` on the entry; the census off the pruned list; then the commit that deletes `SymbolsNamed`, the parser's
closure and `UnitData::dropped` (node-refs about -30); then step (b). **Owed to the wave gate:** winpass in both
spellings, ssa.sh, names.sh, make test to its end, `-check=prune` over the self-host.

### M6d S1, second round: the switch's first step - `alsoNamed`, the pad rule, and what the self-host adds - lane m6d-s1b, 2026-11-18

From 5d1e3d1c. src/ 93,350 -> 93,366 (776655d3) -> 93,369 (a5355ad0) -> 93,370 (a8dc4f02). REF is 5d1e3d1c's code built in its own
tree on Linux (`~/m6d-s1b-ref`, `~/m6d-s1b-ref.exe`); the compares are tools/identical.sh (1,808 tms6747; 7,232 on
x86_64-linux, x86_64-windows and arm64-darwin) and every case's `-g -S` text on four targets (boxkit/m6d-s1b-gdiff.sh).
**The switch was not taken**: `-check=prune` reads 0 over tests/cases and not yet over the compiler's own sources, so the
driver's worklist, the deletion of `SymbolsNamed`, the parser's closure and `UnitData::dropped`, and step (b) are left.

**The two causes, read** (a diagnostic build printed the parser closure's names by body; not committed).
- *A function the parser's tree calls and the IR does not* is an elision the lowering makes and the parser's tree does
  not. Three sites in tests/cases: a braced range walks its backing array, the list's `begin` and `end` read for their
  types alone (`rangeForStmt`); a conditional's prvalue arm is the result itself, where `Parser::buildInto` moves it into
  `$cond` and notes the move as the arm's argument copy (`conditionalArm`); an aggregate's braced return is built member
  by member in the caller's object, where the parser's `return` moves a temporary (`returnBuilt` form 4).
- *The `??_R0` a pad names* is laid down by the ThrowInfo writer from `UnitData::msThrows`' catchables (Masm.cpp and
  X86_64Linux.cpp, `msDescriptors_`), for a type caught as for one thrown - not from the class's five records. The
  parser's `MsHandler::descriptor` is a field `SymbolsNamed` never visits; the IR's CatchPad carries it in `syms`.

**(a) `ir::Function::alsoNamed` and the pad rule** (776655d3, src/ +16). The lowering writes the symbol the parser's
tree calls at each of the three sites; `namesOf` reads it, and on the Microsoft ABI no longer takes a pad's `syms` as
names. Nothing else reads `alsoNamed`: it is deleted by step (b). Over tests/cases (1,247 units, `~/m6d-s1b-a.exe`)
`-check=prune` went from 3, 5, 3, 3 units (x86_64-linux, x86_64-windows, arm64-darwin, tms6747, -O0; 5d1e3d1c) to
**0 on the four targets at -O0 and at -O2**. Identical: tms6747 1,808/0, the three others 7,232/0, `-g` 888 same/358
refused alike/0 differ (tms6747), 903/343/0, 903/343/0 and 878/368/0.

**The self-host, which the first round did not run** (`-check=prune -S -arch x86_64-linux` over the 102 `src/**/*.cpp`,
boxkit/m6d-s1b-pself.sh): **27 sources differ at 776655d3**, at -O0 and at -O2 alike. A third kind, the largest:
- *A virtual call*: the parser's `Call` names the function overload resolution chose, the IR reads the vtable slot and
  names nothing, so an inline virtual function is kept where its class's vtable is not emitted (`Spelling::location`
  named by `X86_64Linux::emitLoc`; `Target`'s, `SemaC`'s and `opt::Pass`'s virtuals). **(a2)** (a5355ad0, src/ +3):
  `alsoNamed` at the three virtual-call sites (a member call, a destructor called by name, a delete). 27 -> **7 of
  102** at -O0 and at -O2; tests/cases still 0 x 4 x 2; identical 1,808/0 and 7,232/0, `-g` 0 differ on four targets.
- *A guarded object's destructor*: the parser's constructor destroys a built member on unwinding whether or not a later
  part can throw; the lowering registers the guard and makes a pad only where a call under it can throw, so the last
  member's destructor is in the tree and not in the IR (`masm::Unit`'s, `Inliner`'s, `ir::Lowering`'s constructors,
  `MasmCodeGen`'s `MasmSpelling`). **(a3)** (a8dc4f02, src/ +1): `Lowering::guard` names its destructor. 7 -> **2 of
  102** at -O0 and at -O2; tests/cases still 0 x 4 x 2; identical 1,808/0 and 7,232/0, `-g` 0 differ on four targets.
- **Left, 2 sources, not read**: `Parser::readTemplateDeclaration` names `std::vector<sema::Shadow>`'s move
  constructor (ParserTemplate.cpp), and `sema::Lookup::refuseInlineAmbiguity` names `Token`'s copy constructor and
  destructor (Lookup.cpp) - elisions, or a copy the lowering makes as bytes; which was not established.

**ast-residue** unchanged at every commit (ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, visits 0,
readers 9, ast-lines 722): nothing is deleted before the switch.

**For the switch, read and not done.** The lowering reads the unit's lists as the parser pruned them
(`module_.data.strings` for an array's literal, Lowering.cpp `stringArray`'s lookup; `module_.data.globals` for a union
global and a `thread_local`): with the driver's prune after the lowering they are whole while it runs, which adds
entries only (what that changes was not measured). `UnitData::order` is written from `program.functions` after the lowering (Driver.cpp), so
the worklist's kept set has to reach it, and the census (`Typer::census`) still takes its bodies from the pruned list.
**The rule to keep**: each site where the lowering elides or replaces a call the parser's tree makes has to say so -
`-check=prune` over the self-host is the oracle, and it is not in any gate yet (owed to the wave gate, as a count).

**Gates** (Linux g++ -Werror clean at each commit; cl /W4 /WX clean, delta builds of 2 min 52 s, 2 min 37 s and 5 min 15 s, the first started alone): on a5355ad0's tree (`~/m6d-s1b2`, `work\m6d-s1bx`), complete: run.sh 1,240 passed, 0 failed; tms6747 SIM=1 -O2 shards 1-3 (Linux) 425/0 and 4-6
(Windows) 425/0, -O0 425/0 and 425/0; exclusions 0 uncited, 0 stale; comment cap 0 over; memory gate rc 0; ast-residue
not raised; ide --check up to date. On a8dc4f02's tree (`~/m6d-s1b`), the final one: run.sh 1,240/0; SIM=1 -O2 shards
1-3 425/0 and 4-6 425/0, -O0 shards 1-3 425/0 and 4/6 138/0 - **Windows shards 5/6 and 6/6 at -O0 were still running at the hour** (144/0 and 143/0 on a5355ad0's tree); the static gates as above;
self-compile of Driver.cpp and Lowering.cpp for x86_64-linux and of Lowering.cpp for tms6747 at -O0 and -O2 6/0 -
Driver.cpp for tms6747 stops at `<unistd.h>`'s own `#error`, as before.

**Left of S1:** the two sources above, until `-check=prune` reads 0 over the self-host; then the driver's worklist over `c.bodies` and the synthesised
functions writing `order`, the census off the pruned list, the commit that deletes `SymbolsNamed`, the parser's closure
and `UnitData::dropped`, and step (b) with its output differences named case by case. **Owed to the wave gate:** winpass
in both spellings, ssa.sh, names.sh, make test to its end, `-check=prune` over the self-host as a recorded count.

### M6d S1, third round: the self-host's last two, `tools/prune-check`, and the switch - the driver's worklist is the prune - lane m6d-s1c, 2026-11-18

From e96f33ee. src/ 93,370 -> 93,379 (9a581f3b) -> 93,379 (c5f4d9eb, a tool) -> 93,430 (the switch). REF is e96f33ee's code
built in its own tree on Linux (`~/m6d-s1c-ref`, `~/m6d-s1c-ref.exe`); the compares are tools/identical.sh (1,808 tms6747;
7,232 on x86_64-linux, x86_64-windows and arm64-darwin) and every case's `-g -S` text on four targets
(boxkit/m6d-s1c-gdiff.sh). **The switch was taken; the deletions and the dead output were not.**

**The two causes** (9a581f3b, src/ +9, Lowering.cpp alone; read from the source, no diagnostic build).
- *ParserTemplate.cpp, `std::vector<sema::Shadow>`'s move constructor*: `struct Unbind { Parser *p; std::vector<Shadow>
  undo; ~Unbind(); } guard{ this, std::vector<Shadow>() };` - an aggregate with a destructor whose member is given a
  temporary of its own class. `aggregateMember` builds `M(...)` in the member (`constructHere`, [dcl.init]/17); the
  parser's list element is the move of that temporary. The element's symbol goes to `alsoNamed` there.
- *Lookup.cpp, `Token`'s copy constructor and destructor*: `const Token &named = c ? p_.peek() : p_.tokens_[i];` - two
  class lvalues one type but for const. The lowering makes the const lvalue ([expr.cond]/3, `constApartClasses`); the
  parser's tree builds each arm into `$cond` (`buildInto`, noted as the arm's argument copy) and destroys it. Each arm's
  copy and the class's destructor go to `alsoNamed` in `lvalue`'s conditional.
`-check=prune` over the compiler's own 102 sources (x86_64-linux): **2 -> 0 at -O0 and at -O2**; over tests/cases (1,247
units) still 0 on four targets at both levels. Identical: tms6747 1,808/0, the three others 7,232/0, `-g` 888 same/358
refused alike/0 differ (tms6747), 903/343/0, 903/343/0, 878/368/0.

**The count a gate reads** (c5f4d9eb): `tools/prune-check [self|cases] [-arch <target>] [-O0|-O2]` compiles every source
of the compiler (or every case) `-check=prune -S`, JOBS at once, and ends "prune-check: <n> units, <d> with a difference,
<r> refused" with status 1 when d is not 0; differences are named in `<out>/faults.txt`, a refusal's text kept as
`<out>/<unit>.refused`. **The recorded count is 0 of 102 at -O0 and at -O2** (x86_64-linux). It is in no gate's table
yet: `tools/wave-gate` is not this lane's file - a row `prune` (linux, narrow) running `tools/prune-check self -arch
x86_64-linux` at both levels is owed.

**The switch** (232a902a, src/ +51; Driver.cpp, ParserClass.cpp, Typing.cpp/h). `lowerSsa` is a worklist: `Prune` holds
the unit's holding records (`UnitData::held`, `Global::held`) and the names reached; a body or a compiler-written
function is lowered only once a name reaches it (a function not held is reached from the start), in rounds over
`c.bodies` and the synthesised functions until a round lowers nothing; each lowered function's names (`namesOf`: calls,
addr operands, pads' type_info chains, spec list, `alsoNamed`) and each reached object's pieces feed the next. After the
last round `pruneUnit` drops what was not reached - functions (so `UnitData::order` is written from the kept list),
objects, unread literals, a Microsoft class's unnamed records - and the module's functions are put back in the unit's
order (bodies as `c.bodies` has them, then the synthesised in the program's order), so the inliner walks them as before.
`Parser::pruneUnusedInline` removes nothing now: it writes the records and its own closure's verdict into
`UnitData::dropped`, and `-check=prune` is the comparison of the two closures name by name ("the IR's closure keeps X,
which the parser's dropped", or the reverse). `Typer::census` takes `whole`: the lowering's census is over the whole
list, the typed check's and dump's over what the parser's closure kept, as before.
- **What the first build got wrong, and the rule that mended it**: 4 cases differed on the Itanium targets and tms6747
  (mem-init-template-id-base, ssa-virtual-base-parts, virtual-base-initialiser, virtual-dtor-through-virtual-base) - a
  complete constructor was built in the round before its C2's body was reached, and the Builder refused it ("building a
  virtual base from a mem-initialiser's arguments"). A C1 or D1 now waits one round and names its split body
  (`splitAt`, from `Typer::vttSplit`): reaching the one reaches the other.
- **Measured on the final code** (`~/m6d-s1c-u.exe`): identical tms6747 1,808/0, the three others 7,232/0; `-g` 0 differ on four
  targets (888/903/903/878 same); `-check=prune` (now closure against closure) 0 of 1,247 units on the four
  targets at -O0 and at -O2; `tools/prune-check self`
  102 units, 0 with a difference, 0 refused at -O0 and at -O2. Compile time of Lowering.cpp at -O0: 6.0 s REF, 5.4 s the switch (one run each).

**ast-residue**: unchanged at every commit (ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, visits 0, readers 9,
ast-lines 722): the parser's closure and `SymbolsNamed` stand until the deletions.

**Not taken, and why.**
- *The deletions* (`SymbolsNamed`, the parser's closure, `UnitData::dropped`): two readers are left. `-check=prune` is
  the two closures compared, so it goes with them (and `tools/prune-check`, `alsoNamed` and the census's `whole` too);
  and `Typer::check`/`Typer::dump` run before any lowering and count the list the parser's closure kept - with the
  closure gone they count the whole list, which changes tests/typed.sh's recorded counts, or they have to run after the
  driver's prune. That is a decision, not an edit of identical output.
- *The dead output* (a function no one calls): after the deletions.
- The lowering still reads the unit's lists whole while it runs (`stringArray`'s lookup, a union global, a
  `thread_local`): no case or source differs for it.

**Gates** (coding lane). g++ -Werror clean at each build; cl /W4 /WX clean: c1 a full build of 4 min 14 s started alone
(09:58:05-10:02:19), the switch 1 min 31 s and the final code 3 min 10 s (10:22:37-10:25:47). On 9a581f3b: Windows
tms6747 SIM=1 shards 4-6 -O2 425/0, -O0 425/0; static gates pass. On the final tree (`~/m6d-s1c2`, `work\m6d-s1cy`):
run.sh 1,240 passed, 0 failed; tms6747 SIM=1 -O2 shards 1-3 (Linux) 425/0 and 4-6 (Windows) 425/0, -O0 425/0 and 425/0; self-compile of Driver.cpp, Lowering.cpp, ParserClass.cpp and Typing.cpp for
x86_64-linux and tms6747 at -O0 and -O2 12/4 - the four are REF's (Driver.cpp for tms6747 stops at `<unistd.h>`'s own
`#error`, ParserClass.cpp's `constructObject` is outside the C6000 selector); exclusions 0 uncited, 0 stale (three citations of ParserClass.cpp re-pointed, 15 lines up); comment cap 0 over; memory
gate rc 0; ast-residue not raised; ide --check up to date. **A defect of this lane's
tree, mended before the commit**: `std::is_sorted` is not in the project's library, so Driver.cpp did not compile
itself - `tools/prune-check` named it as refused.

**Left of S1:** the decision on the typed check's census, then the deletions as one commit of identical output and the
dead output with its differences named case by case. **Owed to the wave gate:** winpass in both spellings, ssa.sh,
typed.sh, names.sh, make test to its end, and `tools/prune-check` as a row.

### M6d S1, fourth round: the typed check after the prune, the parser's closure deleted, and the dead output - lane m6d-s1d, 2026-11-18

From 9a8a4d17. src/ 93,430 -> 93,426 (daf94a96) -> 93,284 (db9c5d1f) -> 93,256 (the dead output). REF is 9a8a4d17's code
built in its own tree on Linux (`~/m6d-s1d-ref`, `~/m6d-s1d-ref.exe`); the compares are tools/identical.sh (1,808
tms6747; 7,232 on x86_64-linux, x86_64-windows and arm64-darwin; with `TYPED=1` on the first commit, 2,712 and 9,944)
and every case's `-g -S` text on four targets (boxkit/m6d-s1d-gdiff3.sh). **S1 is done: one prune, the driver's.**

**(1) The typed check and dump after the driver's prune** (daf94a96, src/ -4; Driver.cpp, Typing.cpp/h), the
coordinator's decision. `-check=typed` and `-dump=typed` are read after `lowerSsa`, so they count the list the IR's
closure kept; `Typer::census` loses `whole` and is again the census of the list it is given. **tests/typed.sh's counts are
unchanged** (11,547 functions, 10,443 bodies, 1,104 compiler-written; 256,621 nodes, 46 untyped; 896 passed). Identical:
tms6747 2,712/0 (the typed leg included); the three others 9,944 with **2 differ, both `-dump=typed` of a unit the
lowering refuses on x86_64-windows** (lowering-named-return-in-handler, lowering-return-destructor-in-handler): no prune is
taken where a function is outside, so the dump now lists the two unused inline constructors the parser's closure had
dropped. The compile is refused either way and no assembly differs; a refused unit's dump counts the whole list.
`-g` 0 differ on four targets (888/903/903/878 same).

**(2) The parser's closure deleted** (db9c5d1f, src/ -142; ParserClass.cpp, Driver.cpp, UnitData.h, Parser.h,
ParserTopLevel.cpp). `SymbolsNamed`, the closure of `Parser::pruneUnusedInline` and `UnitData::dropped` go, and
`-check=prune` (the comparison of the two closures) and `tools/prune-check` with them: there is nothing left to compare.
What the parser writes is the holding records alone, `Parser::recordHeld` (`UnitData::held`, `Global::held`,
`StringLit::copied`); `pruneUnit` keeps what the closure reached. Three citations of ParserClass.cpp in docs/EXCLUSIONS.md
re-pointed (128 lines up). Identical: tms6747 1,808/0, the three others 7,232/0; `-g` 0 differ on four targets; typed.sh
unchanged. The row `prune` owed to tools/wave-gate by the third round is owed no longer.

**(3) The dead output** (src/ -28; Lowering.cpp, Ir.h, Driver.cpp). `ir::Function::alsoNamed` goes - the names the
parser's tree called and the lowering does not, kept for the prune alone - and Lowering.cpp is again what it was before
776655d3. **Every difference is a function no longer emitted, none added**, three cases of tests/cases on every target:
- *conditional-const-prvalue-assign*: the anonymous `Value`'s move constructor (C1 and C2) - a conditional's prvalue arm
  is the result, the move elided ([class.copy]/31).
- *ssa-lambda-locals*: `R`'s move constructor (C1 and C2) - elided the same way.
- *range-for-braced-list*: `std::initializer_list<T>::begin` and `end` for `P`, `char`, `double` and `int` - the
  lowering walks a braced range's backing array and calls neither.
g++ -O0 (Linux, `-nostdinc++ -isystem include`) defines neither move constructor and does define the eight `begin`/`end`:
its code calls them, the IR's does not. Identical: tms6747 1,808 with 6 differ (the three cases at -O0 and -O2), the three
others 7,232 with 24 (the three cases, eight outputs each); `-g` 3 differ on each of the four targets, the same three;
typed.sh 11,547 -> 11,537 functions (10,443 -> 10,435 bodies, 1,104 -> 1,102 compiler-written; nodes 256,621 -> 256,581),
the same three cases. The other `alsoNamed` sites (a virtual call's function, a guarded object's destructor, an aggregate
member's move, a const-apart conditional's arms) changed nothing in tests/cases; what they change in the compiler's own
sources is not measured - the comparison that found them is deleted.

**ast-residue** (Linux): (1) unchanged (ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, visits 0, readers 9,
ast-lines 722); (2) and (3) ExprPtr 2,015, StmtPtr 637, node-refs 389, the rest unchanged - lower than the baseline, which
only the coordinator re-records. (The Windows run of the tool counts 0 under src/parser: its paths.)

**Gates** (coding lane). g++ -Werror clean at each build; cl /W4 /WX clean, a full build of the final tree started alone,
10:45:47-10:52:15 (6 min 28 s with the copy). Static gates on each commit (Linux): exclusions 0 uncited, 0 stale;
comment cap 0 over; memory gate rc 0; ide --check up to date. On the final tree (`~/m6d-s1d3`, `work\m6d-s1dx`): run.sh
1,240 passed, 0 failed; tms6747 SIM=1 -O2 shards 1-3 (Linux) 425/0 and 4-6 (Windows) 425/0, -O0 425/0 and 425/0;
self-compile of Driver.cpp, Lowering.cpp, ParserClass.cpp and Typing.cpp for x86_64-linux and tms6747 at -O0 and -O2 12/4 -
the four are REF's (Driver.cpp's `<unistd.h>` `#error` and ParserClass.cpp's `constructObject` on tms6747). **Owed to the wave gate:** winpass in both spellings, ssa.sh, names.sh, make test to its end, the self-host
(its output changes by what (3) drops), and tests/typed.sh's numbers as (3) leaves them.

### M6d S2: the temporary's record, and `SlotUses` deleted - lane m6d-s2, 2026-11-18

From m6d-shell f21d290d (907830bd's code). src/ 93,256 -> 93,188 lines (-68: step 1 +32, step 2 -100). Design B of the
fourth round, as written: REF is 907830bd's Linux build (`~/m6d-s2-ref.exe`, m6d4's last), the compares those of m6d4
(tools/identical.sh, 1,808 tms6747 and 7,232 x86_64-linux, x86_64-windows and arm64-darwin at -O0 and -O2; every case's
`-g -S` text on tms6747, x86_64-linux and arm64-darwin, REF's tree path written as the lane's).

**(1) The record and its oracle** (e7ebb71a, src/ +32). `Parser::Temporary` gained `builtBy` (the constructor call's
`Resolved` index) and `plain` (nothing but that call names the slot), set by `constructTemporary` - the one function that
materialises a class temporary with a constructor; `plain` is false where `T()`'s zeroing went in front of the call.
`checkSlotRecord`, at the third form, failed the compile with an internal error naming the function on any difference
from `SlotUses`' walk: the form, the constructor's index, and the `$ret:` defaults' nodes. 0 differences: identical.sh
1,808/0 and 7,232/0, `-g` 888/903/903 same and 0 differ (as REF:
358/343/343 refused alike), and every unit of src/ (102) compiled for x86_64-linux with 0 refused. The design's "no zeroing step, no guard" is
"no zeroing step" alone: a guard is set in another slot, and a pending temporary always has one.
**(2) The record asked** (41fc32b3, src/ +19 -119). The third form reads the pending temporary at the slot and asks `plain`;
`SlotUses`, `constructorOfSlot`, the rewrite of the Ast body (the constructor's `this` and the comma's answer made `.sret`)
and the oracle are gone. `return {}`'s defaults go under `$ret:` from the temporary's own defaults node
(`temporaryDefaults_`, which `constructTemporary` makes from the same arguments) where `noteEntryDefaults` read them off
the call: the same nodes, the oracle compared them. docs/EXCLUSIONS.md's seven ParserExprNew.cpp citations re-pointed in the
commit after (-75 and -72 lines), with the residue's baseline and this entry.

**ast-residue** (from ExprPtr 2,016, StmtPtr 639, node-casts 165, node-refs 413, readers 9, ast-lines 722): after step 1
node-refs 415 (the oracle's `const Call *`), the rest unchanged; after step 2 ExprPtr 2,013,
StmtPtr 637, node-casts 161, node-refs 382 (the visitor's signatures and `constructorOfSlot`'s four casts), readers 9,
ast-lines 722; baseline recorded. The memory gate's counts are unchanged (new 1,052, raw pointers 89).

**Gates on the final tree** (41fc32b3 with the citations; Linux ~/m6d-s2 g++ -Werror clean; Windows work\m6d-s2x cl /W4 /WX
clean, a full build of step 1 in 3 min 5 s, the delta of step 2 in 2 min 16 s, each started first and alone): identical
1,808/0 and 7,232/0; `-g` 888/903/903 same, 0 differ, and x86_64-windows' CodeView text 677 same, 368 refused alike, the
other 201 the same once REF's tree path is written in its backslash spelling, as m6d4 found; run.sh 1,240 passed, 0
failed; tests/tms6747.sh SIM=1 -O2 shards 1-3 (Linux) 425/0 and 4-6 (Windows) 425/0, -O0 425/0 and 425/0; every src/
unit (102) compiled for x86_64-linux, 0 refused; self-compile for tms6747: Parser.cpp at -O0 and -O2, and ParserExprNew.cpp
refused by REF and the lane alike (`Parser::buildInto`, stack arguments outside the C6000 selector); exclusions 0 uncited,
0 stale; comment cap 0 over; memory gate held; ast-residue lower, recorded; ide --check up to date.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test to its end, `-check=prune` (S1's)
beside this. **Left of S2:** nothing; the design's B is done. Lanes S1, S3-S8 remain as the fourth round wrote them.

### M6d S3: three of the six statement casts asked of the builders, and where design C1 is wrong - lane m6d-s3, 2026-11-18

From m6d-s2 6572c73c. src/ 93,188 -> 93,199 lines (+11: step 1 +33, step 2 -22). REF is 6572c73c built on Linux in the
lane's own tree (`~/m6d-s3-ref.exe`; S2's tree, and with it REF's baked include path, had been removed from the box, so a
REF built elsewhere refuses every case with a library header), so no path is normalised; the compares are S2's
(tools/identical.sh, 1,808 tms6747 and 7,232 x86_64-linux, x86_64-windows and arm64-darwin; every case's `-g -S` text).

**(1) The records and their oracle** (253c9246, src/ +33). The builder that makes the node says so: `tryStatement` sets
`builtTry_` at each of its three returns (false where Microsoft's exits wrap the `try` in a block), `forStatement` sets
`builtForParts_` to the parts on the path that returns the `for` itself and -1 on the hoisted and range paths,
`caseLabel` sets `builtCase_` (value, default, id). `statement()` reads each only for the kind its first token named, so
no stale value is read, and hands block() `madeTry`. `builtShapeDiffers` failed the compile with an internal error naming
the function on any difference from the three casts. 0: identical 1,808/0 and 7,232/0, `-g` 888/903/903 same and
x86_64-windows 878, 0 differ (as REF: 358/343/343/368 refused alike).
**(2) The records asked** (74b64305, src/ +3 -25): block()'s `dynamic_cast<const Try *>` (ParserStmt.cpp:1473) and
statement()'s `For` (:1558) and `Case` (:1566) casts and the oracle are gone. Identical 1,808/0 and 7,232/0; `-g` 0
differ on four targets (x86_64-windows 878 same, 368 refused alike).

**ast-residue** (from ExprPtr 2,013, StmtPtr 637, node-casts 161, node-refs 382): after step 1 StmtPtr 638, node-casts
164, node-refs 385 (the oracle's three casts and a `StmtPtr` the range path named - moved into `rangeForStatement` before
step 2 was measured); after step 2 ExprPtr 2,013, StmtPtr 637, node-casts 158, node-refs 381, readers 9, ast-lines 722;
baseline recorded. The memory gate's counts are unchanged.

**Where the design is wrong, met and not improvised past.** (i) *C1's `ConstexprFn` owning its operand* has to wait for
S1's last commit: `SymbolsNamed` (the parser's prune, still the one that decides) walks every reached body, the constexpr
function's `return` operand included, so a callee named only there would be pruned once the operand is moved out of the
`Return`. (ii) *"One return and nothing else" asked of the syntax tree* is not the same test: `singleReturnValue` walks
the Ast body `wrapUp` may have wrapped (a `noexcept` function whose `return` calls one that may throw
is made a `Try` on Itanium, and refused today), and accepts
`{ { return x; } }`, which [dcl.constexpr]/3 does not - the syntax tree answers both the other way, so the swap changes
what is accepted and is not a step of identical output; and the operand is still the `Return`'s. Proposed: C1's first
half moves to S4, where the `return` builder hands its operand to the `ConstexprFn` when no statement is built, the two
acceptance differences fixed there as their own rows (a refusal stricter than the standard, an acceptance wider).
(iii) *The constructor's and a declared object's steps* as (offset, initialiser) records written where the
mem-initialisers are built: `assemble`, `buildStaticConstruction`, `constructObject` and `emitRecordedInit` hand back
`StmtPtr`s (blocks, guards, Microsoft regions), so the record needs those builders to return their effect expressions
too - the same change as the three ParserExprNew.cpp casts (:217, :356, :1320-1327 now: `initializerListExpr`,
`aggregateTemporary`, `statementsAsExpression` turning a builder's statements into a comma chain, each with a refusal
cited in EXCLUSIONS.md). Proposed: one lane after S1, the builders returning effects beside the node; then
`runConstexpr`'s two casts and those three go together.

**Gates on the final tree** (74b64305; Linux ~/m6d-s3 g++ -Werror clean; Windows work\m6d-s3x cl /W4 /WX clean - a full
build of step 1 in 1 min 22 s, the delta of the first step 2 in 1 min 1 s and of the final one in 2 min 53 s, each
started first and alone): run.sh 1,240 passed, 0 failed; tests/tms6747.sh SIM=1 shards 1-3 on Linux and 4-6 on Windows at
-O2 425/0 and 425/0, -O0 425/0 and 425/0; self-compile of ParserStmt.cpp: x86_64-linux -O2 text the same as REF's,
tms6747 -O0 and -O2 refused by REF and the lane alike (`Parser::classRangeEnds`' lambda, stack arguments outside the C6000
selector); exclusions, comment cap, memory gate pass; ast-residue lower, recorded; ide --check up to date.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test to its end. **Left of S3:** the
other three casts and C1, as proposed above.

### M6d S3, second round: the merged tree shown, and `ConstexprFn` owning its operand - lane m6d-s3b, 2026-11-18

From m6d-s3 2663e160 (S2, S3's first half and S1 whole, merged). src/ 93,199 -> 93,196 lines. REFs are built in the lane's
own Linux tree (~/m6d-s3b: one baked include path, so no path is normalised): REF0 is m6d-s1's 23c713fc, then the merged
tree itself (`~/m6d-s3b-m.exe`, built from clean). The compares are S2's (tools/identical.sh; every case's `-g -S` text).

**(0) The merge** (d9abd80c, the baseline alone). Built from clean: g++ -Werror and cl /W4 /WX clean (cl's full build 8 min
14 s with the tar, started first and alone). Against REF0: identical tms6747 1,808/0, x86_64-linux, x86_64-windows and
arm64-darwin 7,232/0; `-g` 888/903/903 same and x86_64-windows 878, 0 differ; run.sh 1,240/0; tests/typed.sh 11,537 functions, 10,435 with a source body, 1,102 compiler-written - S1's counts,
no line different from REF0's. The S3 half changes nothing. Static gates pass (exclusions 0/0, comment cap, memory gate,
ide --check); ast-residue re-recorded on Linux: ExprPtr 2,012, StmtPtr 635, node-casts 158, node-refs 357, readers 9,
ast-lines 722.
**(1) The constexpr return operand into `ConstexprFn`** (914d70b6, src/ -3). `Return::takeValue` hands the operand over
(it replaces `hasValue` and `value`, which only the evaluator read); `Constexpr::singleReturn` finds the one `return`
as `singleReturnValue` did - the same Ast walk, so what is accepted is unchanged (C1's second half, the test asked of the
syntax tree, is S4's: it changes acceptance, as the first round found); `ConstexprFn::value` is an `ExprPtr`, and fold()
reads the operand there, not through a pointer into a body. `Block::body()` gains its non-const twin (ast-lines
unchanged). Unblocked by S1: no closure walks the constexpr body any more.
**ast-residue after (1):** ExprPtr 2,013 (+1, `ConstexprFn::value`), node-refs 355 (-2), the rest unchanged. The ExprPtr
word is the design's own (C1: "`ExprPtr value`, moved out of the `Return`"), so `--check` is over the baseline by that one
word; the lane wrote no accepted line and did not respell it (`std::unique_ptr<Expr>` would count as a node-ref and pass).
The coordinator's call: the user's mark on ExprPtr 2013, or the respelling.

**Step (1) shown** against the merged tree: identical tms6747 1,808/0, the three others 7,232/0; `-g` 888/903/903 same and
x86_64-windows 878, 0 differ (358/343/343/368 refused alike); tests/typed.sh the same counts, no line different.

**Gates on the final tree** (914d70b6; Linux ~/m6d-s3b g++ -Werror clean; Windows work\m6d-s3bx cl /W4 /WX clean, a full
build in 4 min 53 s started first and alone): run.sh 1,240 passed, 0 failed (the merge's the same); tests/tms6747.sh SIM=1
shards 1-3 on Linux and 4-6 on Windows at -O2 425/0 and 425/0, -O0 425/0 and 425/0; self-compile of Constexpr.cpp and
ParserTopLevel.cpp: x86_64-linux -O2 text the same as the merged tree's, tms6747 -O0 and -O2 refused by both alike
(`std::fma` undeclared; `Parser::topLevel`'s lambda, stack arguments outside the C6000 selector); exclusions 0/0, comment
cap, memory gate, ide --check pass; ast-residue over the baseline by the one ExprPtr word above.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test to its end, and the residue
decision.

**Not done, and why.** (2) the builders returning effect expressions (the three ParserExprNew.cpp casts, :217, :356,
:1320-1327, and `ConstexprCtor`'s statements as step records): not started - the hour went to showing the merge and step
1 on a box whose three slots were held by two other gates. It is still one lane, as the first round proposed. (3) "one
return and nothing else" asked of the syntax tree is S4's: it changes what is accepted (`{ { return x; } }`, a wrapped
`noexcept` body), so it is not a step of identical output.

### The next M6 wave landed - W8, R1, M6d rounds 1-4 (the C6000 walker deleted), S1, S2, S3's first half, 2026-11-18 (the coordinator)

- C6000 walked 34 / 47 -> 0 / 0 (W8, R1; the last two refused by name, as on every target); the C6000 walker, Walker.h and
  CXX1_C6X_WALKER deleted (M6d round 2), every backend's frame record and data on the IR (rounds 2-4), the prune on the IR
  (S1), SlotUses (S2) and three statement casts (S3a) gone; the dead output S1 dropped (two elided move constructors,
  initializer_list begin/end) named case by case. Cycles -O2 192,025,550 -> 149,248,103 against 458f8974's baseline.
- Merged with main 703407e2 (M8.1 commits 1-3) by the coordinator; lane mw9 mended the one merge fault (Templates.cpp:477,
  the dialect on UnitData) and re-pointed 17 citations; wf9 deleted four fields clang called unused (the Mac's -Werror
  build) and moved eleven new cases' constructors out of line for names.sh. 914d70b6 (ExprPtr +1) reverted in e979bb65 and
  kept as boxkit/m6d-s3b-constexpr-operand.patch, waiting on the user's word.
- **The user's decision**, given directly ("Yes, write it", 2026-11-18): string-literal-array-reference at -O2 (70 -> 96)
  accepted, owed to M6b's inliner (07f8b9e8).
- **Gate: tools/wave-gate m6k -w 703407e2 at 9e430216: 42 passed, 2 failed, 0 not run** - the fixed points ran this time
  (both boxes, -O0 and -O2: 102 of 102 the same through stage 3). The two FAILs were the case accepted after the gated
  commit: cycles (it alone unowned) and baselines (tests/tms6747-cycles.txt changed after the gated commit). 07f8b9e8 adds
  only that line, no code: tools/c6x-cycles --check over m6k's joined shards (847 cases) with 07f8b9e8's file, run by the
  coordinator on Linux the way the gate runs it - rc 0, 71 worse all owed or accepted, 0 missing or stale. Landed at
  07f8b9e8 on that. The first gate (m6j) failed mostly on disks over 70% (cleaned), plus the Mac build, names.sh and the
  unnamed differences mended above.
- tests/wave-expected.txt emptied (236 rows were this wave's). CLAUDE.md's "Reading this tree": every target goes through a
  selector, no backend walks Ast.h.

### M6d S3, third round: a `throw`'s builders return effect expressions, and what S4 inherits - lane m6d-s3c, 2026-11-18

From main 95e06424. src/ 93,572 -> 93,572 lines. REF is 95e06424 built from clean in the lane's own Linux tree
(`~/m6d-s3c-ref.exe`, one baked include path, so no path is normalised); the compares are S2's (tools/identical.sh; every
case's `-g -S` text).

**(1) A `throw`'s steps as effects** (7beb08f2, src/ +62 -62). `throwEffects` (Itanium) and `microsoftThrow` return the
steps as `Parser::Effect` - an expression, and the guard flag a temporary's destruction runs under (0: none);
`throwStatement` makes the block of them, `throwExpression` the comma chain, refusing a guarded step by the message it
gave. `flushTemporaries` is `temporaryEffects` plus `effectStatement`, the one place the guarded `If` is built.
`statementsAsExpression` and its three casts are gone (ParserExprNew.cpp:1327-1334 at 95e06424, the third of the six the
design named). EXCLUSIONS.md: four citations re-pointed.
**ast-residue** (measured on Linux, against 95e06424's tree: ExprPtr 2,012, StmtPtr 635, node-casts 158, node-refs 357,
ast-lines 717 - the recorded baseline still held d9abd80c's ast-lines 722): StmtPtr 625, node-casts 155, node-refs 353,
ExprPtr 2,012, readers 9, ast-lines 717; baseline recorded.
**Shown** against REF: identical tms6747 1,808/0, x86_64-linux, x86_64-windows and arm64-darwin 7,232/0; `-g` 888/903/903 same and x86_64-windows 878, 0 differ (358/343/343/368 refused alike).

**Not done, and why: the other two casts and `ConstexprCtor`'s steps are S4's.** `initializerListExpr` (:217) and
`aggregateTemporary` (:356) cast what `buildInitializerList` and `emitRecordedInit` wrote, and both write through
`emitInit` (`buildInitializerList` for an aggregate element, ParserClass.cpp:1168) - 42 statement pushes in
ParserInit.cpp and ParserClass.cpp, among them loops and blocks of `constructObject`; `runConstexpr`'s two casts read
`assemble`'s constructor body and `buildStaticConstruction`'s statements, the same builders. Turning each into effects
beside the node is S4's work in substance: S4 stops those builders making statement nodes at all, so each returns its
effects (or a loop's record) by then anyway, and doing it here first would write every push site twice.
**The held patch** (boxkit/m6d-s3b-constexpr-operand.patch, `ConstexprFn::value` an `ExprPtr`): its ownership is still
needed - once S4 deletes `Return`, nothing else holds the operand - but not as the patch is: `Return::takeValue` and
`singleReturn` walk nodes S4 deletes. S4's `return` builder hands the operand to `ConstexprFn` directly; the one ExprPtr
word lands in the commit that removes about 150, so no separate decision on ExprPtr 2,013 is left.

**Gates on the final tree** (7beb08f2): Linux g++ -Werror clean; Windows work\m6d-s3cx cl /W4 /WX clean, a full build in
1 min 10 s with the tar, started first and alone (of the tree before a comment-only amend); run.sh 1,240 passed, 0 failed;
tests/tms6747.sh SIM=1 shards 1-3 on Linux and 4-6 on Windows at -O2 425/0, -O0 425/0; self-compile of
ParserExprNew.cpp and ParserExprCall.cpp: x86_64-linux -O2 text the same as REF's, tms6747 -O0 and -O2 refused by both
alike (stack arguments outside the C6000 selector); exclusions 0/0, comment cap, memory gate, ide --check pass;
ast-residue lower, recorded. **Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test.

### M6d S4, first lane: emitInit's family and the constructor call return effects, and what is left of S4 - lane m6d-s4a, 2026-11-18

From m6d-s3c 662c16be. src/ 93,572 -> 93,579 lines. REF is 662c16be built from clean in the lane's own Linux tree
(`~/m6d-s4a-ref.exe`); the compares are S3c's (tools/identical.sh on four targets; every case's `-g -S` text).

**(1) emitInit's family and `buildInitializerList` write effects** (7b5f15d7, src/ 93,572 -> 93,579). The nine
builders under `emitInit` (`initConstruct`, `initStore`, `initZero`, `zeroRun`, `emitString`, `emitFill`,
`emitAggregate`, `emitInit`, `emitRecordedInit`) and `buildInitializerList` push `Parser::Effect`; `CtorInit::ilSetup`
holds effects. A caller that still keeps statements makes one `ExprStmt` of each with `appendEffects` (what
`flushTemporaries` now ends in), at the place and in the order the pushes were. `initializerListExpr`'s and
`aggregateTemporary`'s casts are gone (the last two of the six the design named), and with them two refusals no
program could reach: every step either read was an `ExprStmt` - `emitInit`'s are never guarded and the list is built
with `flush` false - so acceptance is unchanged. EXCLUSIONS.md: 19 citations re-pointed (lines moved, words kept).
**ast-residue** (Linux): ExprPtr 2,012, StmtPtr 625 -> 593, node-casts 155 -> 153, node-refs 353 -> 351, readers 9,
ast-lines 717. **Shown** against REF: identical tms6747 1,808/0, x86_64-linux, x86_64-windows and arm64-darwin
7,232/0; `-g` 888/903/903 same and x86_64-windows 878, 0 differ (358/343/343/368 refused alike).

**(2) `constructObject` and `constructLocal` return an effect** (9ceda78c, src/ 93,579 -> 93,579). Both made one
`ExprStmt` of the constructor call; they return `Parser::Effect`, and each of their six callers that keeps statements
makes it one with `effectStatement`. **ast-residue**: ExprPtr 2,012, StmtPtr 593 -> 588, node-casts 153, node-refs
351, readers 9, ast-lines 717; baseline recorded. **Shown** against REF: identical 1,808/0 and 7,232/0; `-g`
888/903/903 same and x86_64-windows 878, 0 differ (358/343/343/368 refused alike).

**Not done, and why: no statement node is deleted yet.** After (1) the parser casts no statement node at all; the last
four statement casts are the constant evaluator's (`singleReturnValue`, `runConstexpr`, Constexpr.cpp:264-274 and
:568-575), and every statement class is still built, so none leaves Ast.h in this lane. What S4 still owes, in order:
(a) "one return and nothing else" asked of the syntax tree with the `return` builder handing its operand to
`ConstexprFn` - an acceptance change, its own commit, each difference named against clang (the Ast walk follows a
chain of one-statement blocks to a `return`, so nested braces round the return, a declaration that builds no step, a
null statement, a typedef, a `static_assert` and a body the parser wraps in a `try` each need a case measured); (b)
`ConstexprCtor` and `foldDeclaredObject` over effects: `buildStaticConstruction`'s steps (`registerDestruction`,
`emitDestructors`/`destroyObject` - which reaches `Classes::memberDestructor`, the other S4 lane's file) and
`assemble`'s constructor body; (c) then `statement()` and its builders returning the syntax node instead of a
`StmtPtr`, `Function::body` and the fifteen classes deleted - one change across ParserStmt.cpp, ParserTopLevel.cpp and
ParserClass.cpp's synthesised bodies, after the Classes lane.

**Gates on the final tree** (9ceda78c): Linux g++ -Werror clean (both steps); Windows work\m6d-s4ax cl /W4 /WX clean,
started first and alone, 4 min 50 s for step 1 and 5 min 30 s for the final tree, each a full build with the tar;
run.sh 1,240 passed, 0 failed; tests/tms6747.sh SIM=1 shards 1-3 on Linux and 4-6 on Windows at -O2 425/0 and 425/0,
-O0 425/0 and 425/0; self-compile of the six changed files: x86_64-linux -O2 text the same as REF's, tms6747 -O0 and
-O2 refused by both alike (stack arguments outside the C6000 selector); exclusions 0/0, comment cap, memory gate,
ide --check pass; ast-residue lower, recorded. **Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2,
names.sh, make test.

### M6d S4, second lane: the vptr stores and the destructor registrations Classes and the parser build are effects - lane m6d-s4b, 2026-11-18

From m6d-s4a e2e57f2b. src/ 93,579 -> 93,582 lines. REF is e2e57f2b built from clean in the lane's own Linux tree
(`~/m6d-s4b-ref.exe`); the compares are S4a's (tools/identical.sh on four targets; every case's `-g -S` text).

**(1) The vptr and vbptr stores are effects** (b6131c50, src/ 93,579 -> 93,582). `Classes::storeVptrs`,
`itaniumVptrStores` and `storeVbptr` hand back `sema::Effect` - moved from `Parser` into Entities.h so `Classes` can
name it, `Parser::Effect` its typedef; their four callers (the written constructor's and destructor's assembly in
ParserTopLevel.cpp, `synthesizeDefaultCtor`, `synthesizeCopy`, `guardedVirtualBaseInit`) make the statements with
`appendEffects`, at the place and in the order they were. **ast-residue** (Linux): ExprPtr 2,012 (the word moved, not
added), StmtPtr 588 -> 575, node-casts 153, node-refs 351, readers 9, ast-lines 717. **Shown** against REF: identical tms6747 1,808/0, x86_64-linux, x86_64-windows and arm64-darwin 7,232/0; `-g` 888/903/903 same and x86_64-windows 878, 0 differ (358/343/343/368 refused alike).
**(2) The destructor registration and the base's and own destructor calls are effects** (53ac05e5, src/ 93,582).
`registerDestruction` writes `Effect`s (`__cxa_atexit`, its thread twins, `atexit` of the Microsoft helper);
`baseDestructorCall` and `selfDestructorCall` return one; `buildStaticConstruction` and `destroyObject` make the
statement where they pushed it. **ast-residue**: ExprPtr 2,012, StmtPtr 575 -> 564, node-casts 153, node-refs 351, readers 9, ast-lines 717; baseline recorded (lowered by hand to the measured count, no line raised). EXCLUSIONS.md: six citations the two steps moved (ParserClass.cpp three, ParserTopLevel.cpp two, ParserInit.cpp one) re-pointed in this entry's commit - the steps were already being gated, so neither was amended; the final tree checks 0 uncited, 0 stale. **Shown** against REF: identical tms6747 1,808/0, x86_64-linux, x86_64-windows and arm64-darwin 7,232/0; `-g` 888/903/903 same and x86_64-windows 878, 0 differ (358/343/343/368 refused alike).

**Not done, and why: no statement class leaves Ast.h yet, and acceptance is unchanged.** What (b) still owes is the
part with a shape an effect cannot hold: `Classes::memberDestructor`'s array case is `eachElement`'s `For`, and
`destroyObject`'s virtual-base case is an `If` over `$vb` - each needs a loop's or a test's record beside the effect,
not a second spelling of the statement; then `buildStaticConstruction` returns its steps as effects (all of them are
expressions once those two are records), `foldDeclaredObject` takes them, and `ConstexprCtor` keeps the constructor's
steps where `assemble` writes the mem-initialisers instead of the body. (a) and (c) are as S4a left them: (a) "one
return and nothing else" asked of the syntax tree, an acceptance change with each difference measured against clang;
(c) `statement()` returning syntax nodes and the fifteen classes deleted, after (a) and (b).

**Gates on the final tree** (53ac05e5, and the entry's commit for exclusions): Linux g++ -Werror clean (both steps);
Windows work\m6d-s4bx cl /W4 /WX clean, started first and alone - step 1 a full build in 4 min 15 s with the tar, step 2
a delta in 2 min 50 s; run.sh 1,240 passed, 0 failed; tests/tms6747.sh SIM=1 shards 1-3 on Linux and 4-6 on Windows at
-O2 425/0 and 425/0, -O0 425/0 and 425/0; self-compile of the four changed files (Classes.cpp, ParserClass.cpp,
ParserInit.cpp, ParserTopLevel.cpp): x86_64-linux -O2 text the same as REF's, tms6747 -O0 and -O2 refused by both alike
(stack arguments outside the C6000 selector); exclusions 0/0, comment cap, memory gate, ide --check pass; ast-residue
lower, recorded. **Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test.

### M6d S4, third lane: an array member's loop and a virtual base's test as records on an effect, the static object's steps as effects, and (a) measured - lane m6d-s4c, 2026-11-18

From m6d-s4b e6a79c80. src/ 93,582 -> 93,596 lines. REF is e2e57f2b built from clean in the lane's own Linux tree
(`~/m6d-s4c-ref.exe`, as the brief asked; S4b's two steps were shown against the same REF); the compares are S4a's
(tools/identical.sh on four targets; every case's `-g -S` text).

**(1) The two shapes an effect could not hold are records on it** (4cf6bcf5, src/ 93,582 -> 93,596). `sema::Effect`
gains `loop`/`count` (`$i` over [0, count)) and `when`/`above` (only while `$vb` is above `above`), and a constructor -
C++11 has no aggregate with default member initialisers. `effectStatement` is the one place both shapes are built
(`eachElement`, the `If` over `$vb`), as it already was for a guard. `Classes::memberDestructor`/`memberDestructors`,
`destroyObject`, an `emitDestructors` overload and `buildStaticConstruction` hand back effects; their callers make the
statements with `appendEffects` where they pushed them. `foldDeclaredObject` takes the effects (one implementation,
the statement overload gone): a guarded, looped or tested step is refused as the `If` or `For` it was, so acceptance is
unchanged. `foldLocalObject` takes the one step or the laid effects instead of a range of `inits`.
**ast-residue** (Linux): ExprPtr 2,012 -> 2,014, StmtPtr 564 -> 541, node-casts 153, node-refs 351 -> 350, readers 9,
ast-lines 717. **The two ExprPtr words are `Effect`'s constructor parameter and its default** - a fall by the brief's
rule (23 StmtPtr words for 2), said here; the baseline is not raised, so `--check` names ExprPtr until the coordinator
rules (or S4's statement deletion takes ExprPtr down by about 150 and absorbs it).
**Shown** against REF: identical tms6747 1,808/0, x86_64-linux, x86_64-windows and arm64-darwin 7,232/0; `-g`
888/903/903 same and x86_64-windows 878, 0 differ (358/343/343/368 refused alike). EXCLUSIONS.md: 20 citations the step
moved re-pointed in this entry's commit (lines moved, words kept); the final tree checks 0 uncited, 0 stale. Baseline
lowered by hand where the count fell (StmtPtr 541, node-refs 350); ExprPtr left at 2,012.

**(a) measured, not coded** (boxkit/m6d-s4c-probes/, RESULTS.txt). Eleven bodies, REF on x86_64-linux and
x86_64-windows against clang 19.1.5 `-std=c++11 -pedantic-errors`: one wrong acceptance (`{ { return x; } }`, clang: a
C++14 extension), six refusals stricter than [dcl.constexpr]/3 (a null statement before or after the return, a typedef,
an alias-declaration, a `static_assert`, and on Itanium only a `noexcept` body whose return calls a constexpr function -
wrapUp's `Try`); a block's using-declaration is refused by its own refusal; empty braces, two returns and a plain
`noexcept` body agree. The shape to ask: today only the return builder's plain path (`return StmtPtr(new
Return(std::move(value)))`, the last line of the `return` branch in ParserStmt.cpp) builds what `singleReturnValue`
accepts, so the record is a pointer to that operand taken there, and the test is the body's syntax node: one `Return`
child, the others `Null` or a permitted `DeclStmt` - but `typedef` reads as `ExprStmt` in `statementKindAhead`, so the
typedef row needs a kind of its own first.

**Gates on the final tree** (4cf6bcf5, and the entry's commit for exclusions and the baseline): Linux g++ -Werror
clean; Windows work\m6d-s4cx cl /W4 /WX clean, a full build with the tar in about 4 min, started first and alone (the
first try named `Effect` unqualified in an out-of-class return type; mended by amending the step before any gate ran);
run.sh 1,240 passed, 0 failed; tests/tms6747.sh SIM=1 shards 1-3 on Linux and 4-6 on Windows at -O2 425/0 and 425/0,
-O0 425/0 and 425/0; self-compile of the seven changed files (Classes.cpp, Constexpr.cpp, ParserClass.cpp,
ParserInit.cpp, ParserStmt.cpp, ParserTopLevel.cpp, ParserExprCall.cpp): x86_64-linux -O2 text the same as REF's,
tms6747 -O0 and -O2 refused by both alike (stack arguments outside the C6000 selector; `std::fma` undeclared);
exclusions 0/0 after the re-point, comment cap, memory gate, ide --check pass; ast-residue lower but for ExprPtr +2,
above.

**Not done, and why.** (a) is measured above and not coded: with step 1's gates on boxes whose slots the h1g gate shares,
a second step and its own identical run did not fit the hour. (c) waits on (a): the constant evaluator still reads
`assemble`'s constructor body (`ConstexprCtor::body`, `runConstexpr`'s `Block`/`ExprStmt`) and `singleReturnValue`'s
`Block`/`Return`; no statement class leaves Ast.h in this lane. **Owed to the wave gate:** winpass in both spellings,
ssa.sh 1/2, names.sh, make test, and the ExprPtr ruling.

### M6d S4, fourth lane: `typedef` a syntax kind of its own, and [dcl.constexpr]/3's one return asked of the syntax tree - lane m6d-s4d, 2026-11-18

From m6d-s4b a3686e4b. src/ 93,596 -> 93,635 lines. REF is a3686e4b built from clean in the lane's own Linux tree
(`~/m6d-s4d-ref.exe`); the compares are S4a's (tools/identical.sh on four targets; every case's `-g -S` text).

**(1) `syntax::Kind::Typedef`** (2acbb8aa, src/ 93,596 -> 93,613). A typedef in a block, and an alias-declaration, that
defines no class or enumeration (no `{` among its specifiers' tokens, `Parser::definesType`) is noted `Typedef` where it
was `DeclStmt` (S4c's RESULTS said `ExprStmt`: a block's typedef reaches `declaration()`, which notes `DeclStmt`; a
class or enumeration defined in one stays `DeclStmt`). The lowering takes it as the null statement it lowered to.
Shown against REF: identical tms6747 1,820/0 (the new cases refused alike), x86_64-linux, x86_64-windows and
arm64-darwin 7,280/0; `-g` 889/904/904 same and x86_64-windows 880, 0 differ (364/349/349/373 refused alike). **ast-residue**
(Linux) unchanged: ExprPtr 2,014, StmtPtr 541, node-casts 153, node-refs 350, readers 9, ast-lines 717.
**(2) The one return over syntax** (26a14a80, src/ 93,613 -> 93,635). `Constexpr::singleReturnValue(body, &why)` reads
the definition's body - the `Function` node's last `Block` child, asked once that node is noted: one `Return`, and beside it
only `Null`, `Typedef`, and `DeclStmt` whose word is `static_assert` or `using` (a using-directive; the block's
using-declaration and namespace alias are refused before this, by their own names) - [dcl.constexpr]/3's list. The
operand is the one the return builder's plain path took (`pendingPlainReturn_`, keyed to the return's node in
`statement()` as `returnBuilt_` is): a return built through `before` or an unwind is refused as it was, and so is a body
wrapped for a `throw(...)` list or a function try block. `singleReturnValue`'s Ast walk (two node casts) is gone; the
refusal names clang's words where it has them. **Acceptance**, each a case with clang 19.1.5 `-x c++ -std=c++11
-pedantic-errors`'s answer (Windows box): `constexpr-body-nested-refused` (`{ { return x; } }`, refused: "use of this
statement in a constexpr function is a C++14 extension", the .error); accepted, clang's .expected: `constexpr-body-null-before`
(6 10), `-null-after` (8 12), `-typedef` (6 21), `-alias` (9 24), `-static-assert` (10 18), `-noexcept-call` (6 22, the
Itanium body wrapUp wraps in a `Try` - the syntax is still one return). Shown against REF: identical tms6747 1,820
compared, 12 differ, x86_64-linux, x86_64-windows and arm64-darwin 7,280 compared, 44 differ - every one a new case's
(the six accepted shapes; `-noexcept-call` only where REF refused it, not on x86_64-windows); `-g` 888/903/903 same and
x86_64-windows 879, 7/7/7/6 differ, the same cases and `-nested-refused`; no other case's text moved. First committed as
9119e4ea, whose static run found StmtPtr 541 -> 542 (a `StmtPtr` local in `block()` holding the body's node): amended
before any gate counted, the body then asked of the `Function` node. **ast-residue** (Linux): ExprPtr 2,014, StmtPtr
541, node-casts 153 -> 151, node-refs 350 -> 348, readers 9, ast-lines 717; baseline lowered by hand (node-casts,
node-refs; ExprPtr left 2,012). EXCLUSIONS.md: 10 citations the two steps moved re-pointed (lines moved, words kept);
0 uncited, 0 stale.

**Gates on the final tree** (26a14a80, and the entry's commit for exclusions and the baseline): Linux g++ -Werror
clean (each step built); Windows cl /W4 /WX clean - step 1 a full build with the tar in 1 min 20 s (work\m6d-s4dx,
started first and alone), 9119e4ea a delta in 70 s, 26a14a80 a full build in 4 min 9 s (work\m6d-s4dy, beside the
stopped chain's last shard); run.sh 1,247 passed, 0 failed (the seven new cases among them); tests/tms6747.sh SIM=1
shards 1-3 on Linux at -O2 428/0 and -O0 428/0, shards 4-6 on Windows at -O2 428/0 and -O0 428/0 (work\m6d-s4dy); self-compile of the five changed files
(Constexpr.cpp, ParserStmt.cpp, ParserTopLevel.cpp, Lowering.cpp, Syntax.cpp): x86_64-linux -O2 text the same as REF's,
Lowering.cpp and Syntax.cpp also tms6747 -O0 and -O2 the same, the other three refused by both alike there (`std::fma`
undeclared; stack arguments outside the C6000 selector); exclusions 0/0 after the re-point, comment cap, memory gate,
ide --check pass; ast-residue lower but for ExprPtr 2,014 against the baseline's 2,012 (4cf6bcf5's, unchanged here).

**Not done, and why.** (3) `ConstexprCtor::body` over effects: not started. `assemble`'s member initialisers, the
delegating call and the base calls are still statements (`memberInits`, `delegateCall`, the base `Block`s and their
`guardSubobject` wraps), so the constructor's steps are not yet a list of effects to hand over; that conversion is the
next lane's first step, then `runConstexpr(const Stmt &)` goes. (4) waits on (3). No statement class leaves Ast.h in this
lane. **The ExprPtr mark**: the coordinator relayed the user's "allow +2" (ExprPtr 2,014 for 4cf6bcf5); the lane did not
write `tools/ast-residue.baseline` - the tool keeps the user-approved mark to the coordinator and `--record` refuses a raise.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test (syntax.sh among it), the mark.

### M6d S4, fifth lane: `assemble`'s constructor steps as effects, `ConstexprCtor` over them, and the first three statement classes deleted - lane m6d-s4e, 2026-11-18

From m6d-s4d f4fce1ed. src/ 93,635 -> 93,655 lines. REF is f4fce1ed built from clean in the lane's own Linux tree
(`~/m6d-s4e-ref.exe`); the compares are S4a's (tools/identical.sh on four targets; every case's `-g -S` text).

**(3) The constructor's steps are effects** (b4254ff8, src/ 93,635 -> 93,679). `memberInitialiser` puts its steps
into an `Effect` list (false where the class wrote none) and `constructMember` and `delegatingCall` return one; an
array member's loop is the effect's `loop`/`count`, made a statement under its elements' guard by `memberLoop` where
the caller takes it, as `constructMember` did. The written constructor's mem-initialisers are `MemberStep`s (effects,
or that guarded loop); `assemble` and `Classes::synthesizeDefaultCtor` make the statements, in the place and order
they were. `ConstexprCtor` keeps `steps`: each step's expression in the order the body runs it, a null where the body
has a statement no constant takes (a guard, a loop, a `$vb` test, a subobject's unwind region, a `try` round it all,
or a written block with a statement in it or wrapped by `wrapUp`); `constructConst` runs them and stops at a null, as
the walk stopped at the statement. `runConstexpr(const Stmt &)` - the last two statement casts in the constant
evaluator - is deleted. One difference from the walk, unreachable by any case: an out-of-line constexpr constructor
whose block holds an expression statement was run by the walk (C++14's rule) and is not run now (C++11's; clang
refuses the body). **ast-residue** (Linux): ExprPtr 2,014 -> 2,012, StmtPtr 541 -> 518, node-casts 151 -> 149,
node-refs 348 -> 343, readers 9, ast-lines 717. **Shown** against REF: identical tms6747 1,820/0, x86_64-linux, x86_64-windows and arm64-darwin 7,280/0; `-g` 894/909/909 same and x86_64-windows 884, 0 differ (359/344/344/369 refused alike).

**(4) `DoWhile`, `Break` and `Continue` leave Ast.h** (42ae8898, src/ 93,679 -> 93,655). With (3) nothing reads a
statement node: every `body()` reader is gone and nothing implements `Visitor`. A do-while's parts are kept in a
`Block` (its body and the condition's effect), where nothing reads them; a `break` or `continue` is an empty `Block`;
the lowering reads all three from the syntax tree, as it did. The three classes, their forward declarations and
`Visitor` slots go. **ast-residue**: ExprPtr 2,012, StmtPtr 518 -> 519 (the do-while's parts vector), node-casts 149, node-refs 343, readers 9, ast-lines 717 -> 689; baseline recorded on Linux (`--record`, every count lower than f4fce1ed's: ExprPtr 2,012 is below the start's 2,016, so its line needs no mark). **Shown** against REF: identical tms6747 1,820/0 and 7,280/0; `-g` 894/909/909 same and x86_64-windows 884, 0 differ (359/344/344/369 refused alike).

**Gates on the final tree** (42ae8898, and the entry's commit for exclusions and the baseline): Linux g++ -Werror
clean (each step built); Windows cl /W4 /WX clean, started first and alone - step 1's first full build (with the tar,
work\m6d-s4ey) found `Classes::synthesizeDefaultCtor` still calling the old `memberInitialiser`, mended by amending
the step before any gate counted it, then a delta in about 90 s; step 2 a full rebuild after Ast.h in about 3 min;
run.sh 1,247 passed, 0 failed (both steps); tests/tms6747.sh SIM=1 on the final tree, shards 1-3 on Linux at -O2
428/0 and -O0 428/0, shards 4-6 on Windows (work\m6d-s4ey) at -O2 428/0 and -O0 428/0; self-compile of the five
changed files (Constexpr.cpp, ParserClass.cpp, ParserTopLevel.cpp with step 1; ParserStmt.cpp, Classes.cpp with step
2): x86_64-linux -O2 text the same as REF's, tms6747 -O0 and -O2 refused by both alike (`std::fma` undeclared; calls
outside the C6000 selector); comment cap, memory gate, ide --check pass; exclusions 0/0 after 8 citations the steps
moved were re-pointed in this entry's commit (lines moved, words kept); ast-residue lower, recorded.

**Not done, and why.** What is left of S4 is (4)'s rest: twelve statement classes (`ExprStmt`, `Return`, `Block`,
`If`, `While`, `For`, `Case`, `Switch`, `Goto`, `Label`, `FuncletLeave`, `Try`) are still built, and two are still
read by the parser itself - `Case` (a switch's duplicate test reads `value()` off the cases it noted) and the body
`Block` a `Function` holds. The order the next lane can take: `Case` (the switch's record of values instead of the
node), then each builder returning its syntax node, `Function::body` and `StmtPtr` last. **Owed to the wave gate:**
winpass in both spellings, ssa.sh 1/2, names.sh, make test.

### M6d S4, sixth lane: an out-of-line constexpr constructor's statements refused as clang refuses them, and seven more statement classes deleted - lane m6d-s4f, 2026-11-18

From m6d-s4e 63249362. src/ 93,655 -> 93,563 lines. REF is 63249362 built from clean in the lane's own Linux tree
(`~/m6d-s4f-ref.exe`); the compares are S4a's (tools/identical.sh on four targets; every case's `-g -S` text).

**(1) The difference S4e named** (60921fb7, src/ 93,655 -> 93,667). At 63249362 an out-of-line `constexpr P::P(int a)
: x(0) { x = a; }` was accepted quietly: `P p(1);` compiled to an ordinary runtime call of `P::P` and ran, and
`constexpr P q(2);` was refused only as "its value could not be worked out" - the statement was never evaluated
(the walk that ran it under C++14's rule went with S4e's step 1). clang 19.1.5 -std=c++11 -pedantic-errors refuses
the body: "use of this statement in a constexpr constructor is a C++14 extension". The definition's block is read
off the syntax tree where the constructor's steps are kept, and a statement other than `;`, a typedef,
`static_assert` or `using` (C++11's [dcl.constexpr]/4 list) is refused by name with the in-class check's message,
now one function (`refuseConstexprConstructorStatement`). A body of only those was a null step at 63249362, so
`constexpr P q(2)` over `{ ; typedef int T; static_assert(...); }` was refused though clang takes it; now it folds
(q in .data as 2). Case `constexpr-constructor-out-of-line-refused` (.error; clang's position 5:47). **ast-residue**:
ExprPtr 2,012, StmtPtr 519, node-casts 149, node-refs 343 -> 344 (the statement node held by reference; read by value
in (3)), readers 9, ast-lines 689. **Shown** against REF: identical tms6747 1,820/0 and 7,280/0; `-g` 894/909/909
same and x86_64-windows 884, 1 differ on each target - the new case itself, accepted by REF and refused now.

**(2) `Goto`, `Label`, `FuncletLeave`, `Case` and `Switch` leave Ast.h** (c66636ef, src/ 93,667 -> 93,598). The
switch's duplicate test holds the case values it noted (`SwitchCtx::values`, `deflt` a flag) rather than the
`Case` nodes; a jump is an empty `Block` (`jumpStatement`, its label still made, so the `$ms` numbering is
unchanged); a label or a case is a `Block` holding its statement (`labelStatement`); a switch is a `Block` of its
body and its condition's effect. **ast-residue**: ExprPtr 2,012, StmtPtr 507, node-casts 149, node-refs 341,
readers 9, ast-lines 616. **Shown**: identical 1,820/0 and 7,280/0; `-g` as (1), the one differ the new case.

**(3) `While` and `For` leave Ast.h** (f171f814, src/ 93,598 -> 93,563): a loop's init, condition, step and body
are kept in a `Block` (`loopStatement`), as the do-while's are; the `For`'s scope, which nothing read, goes, and
`rangeForStatement`'s parameter with it. **ast-residue**: ExprPtr 2,012 -> 2,017 (the helper's ExprPtr
parameters), StmtPtr 512, node-casts 149, node-refs 337, readers 9, ast-lines 581. **Shown**: identical 1,820/0 and 7,280/0; `-g` as (1).
**(4)** (07524fe0, src/ 93,563) takes the condition and step as `Effect`s: ExprPtr back to 2,012, StmtPtr 512,
node-casts 149, node-refs 337, readers 9, ast-lines 581, all at or below the baseline, recorded. **Shown**: identical
1,820/0 and 7,280/0; `-g` as (1). **(5)** (528b76ac) spells each call's `Effect(...)`: cxx3 does not convert an
ExprPtr argument through `Effect`'s constructor with its defaulted second parameter (a refusal stricter than the
standard, left for its own round), so (4) made the self-compile of ParserStmt.cpp and ParserClass.cpp refuse,
REF and new alike; now both compile them, x86_64-linux -O2 text the same. Residue as (4). **Shown**: identical
1,820/0 and 7,280/0; `-g` as (1).

**Gates on the final tree** (528b76ac, and this entry's commit for exclusions and the baseline): Linux g++ -Werror
clean (each step built); Windows cl /W4 /WX clean, started first and alone: (1) a full build in about 1.5 min
(work\m6d-s4fy), (2) a delta that found `ParserTopLevel`'s `Label` left out of the commit (amended before any gate
counted it), (3) a delta that found the For's scope still set in the cleanup path (amended likewise), each a full
rebuild after Ast.h in about 4 min; (4) about 2.5 min, (5) about 1.5 min; run.sh 1,248 passed, 0 failed on each of (1)-(5) (the new case the one added);
tests/tms6747.sh SIM=1 on 07524fe0 (the final tree but (5)'s spelling), shards 1-3 on Linux at -O2 428/0 and -O0
428/0, shards 4-6 on Windows at -O2 428/0 and -O0 428/0; self-compile of the changed files (ParserTopLevel.cpp,
ParserConst.cpp with (1); ParserStmt.cpp, ParserClass.cpp with (2), (3) and (5)):
x86_64-linux -O2 text the same as REF's, tms6747 -O0 and -O2 refused by both alike (calls outside the C6000
selector); comment cap, memory gate, ide --check pass; exclusions 0/0 after 14 citations the steps moved were
re-pointed in this entry's commit (lines moved, words kept); ast-residue recorded lower.

**Not done, and why.** Left of S4: five statement classes (`ExprStmt`, `Return`, `Block`, `If`, `Try`) are still
built - `ExprStmt` and `Block` are what every deleted class became, so they go last, with `StmtPtr`; `Return` (17
builders, an ExprPtr each) and `If` (14) are the next lane's, in the shape of (2) and (3); `Try` holds the
Microsoft handlers and cleanup the parser hands on; and the body `Block` a `Function` holds is still read by the
parser itself. **Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test.

### M6d S4, seventh lane: a converting constructor with defaulted parameters, and `Return` and `If` deleted - lane m6d-s4g, 2026-11-19

Brief boxkit/brief-m6d-s4g.txt; from ad482730 on branch m6d-s4b; REF built from ad482730 on Linux (`~/m6d-s4g-ref.exe`);
the compares are S4a's (tools/identical.sh on four targets; every case's `-g -S` text).

**(1) The refusal stricter than the standard S4f found** (8f6abc05, src/ 93,563 -> 93,575). `convertingConstructor`
took only a constructor of exactly one parameter, so `Effect(ExprPtr e = ExprPtr(), int g = 0)` was no converting
constructor and `loopStatement(StmtPtr(), std::move(cond), ...)` was "no function called 'Parser::loopStatement' takes
these 4 argument(s)"; [class.conv.ctor]/1 counts a constructor callable with one argument, so the parameters past the
first may all have defaults (`overload_.leastArguments(c) > 1` is the test now). Both conversion sites - the user
conversion of an argument and the cast's - complete the call with `applyDefaults`; the user conversion records the
defaults by the value's node (`"$conv:<node>"`, `noteEntryDefaults`), and the lowering's `convertedTemporary` passes
them after the value. Case `converting-constructor-default-argument` (by value, to a const reference, copy-
initialisation, return, cast, a move-only argument, the all-default constructor; clang 19.1.5 -std=c++11
-pedantic-errors on Windows: `507 0 607 234 87 93 13 1 7`, and cxx3 for x86_64-windows prints the same). Six
citations of ParserExprNew.cpp re-pointed (lines moved, words kept). **528b76ac's spelled `Effect(...)` stays**: with
the natural spelling cxx3 now parses ParserStmt.cpp, but its lowering stops at `loopStatement`'s by-value `Effect`
- "internal error: a by-value argument whose copy the parser recorded none for": a class with a non-trivial copy
made from another type by a converting constructor, by value, is not recorded where `byValueArgument` looks
(`argumentCopy_`), with one parameter as with two (REF refuses boxkit/m6d-s4g-probe/p4.cpp alike) - owed, a `broken()`
for valid C++. **ast-residue** as the baseline: ExprPtr 2,012, StmtPtr 512, node-casts 149, node-refs 337, readers 9,
ast-lines 581. **Shown** against REF: identical tms6747 1,822 compared, 2 differ and 7,288 compared, 8 differ - each the
new case at both levels (REF refuses it); `-g` 894/909/909/884 same, 1 differ on each target, the new case; run.sh
1,249 passed, 0 failed; self-compile of ParserExprNew.cpp, ParserOverload.cpp and Lowering.cpp for x86_64-linux -O2
by REF and by (1), both compile.

**(2) `Return` and `If` leave Ast.h** (src/ 93,575 -> 93,562). Nothing read either (no visitor of a statement is
left, no cast to one): the 17 `Return` builders call `returnStatement(value)` and the 14 `If` builders
`ifStatement(cond, then, else)`, two functions in Ast.h beside `Block` that keep what the node held in a `Block`
(the value or condition as an `ExprStmt`, so every expression lives as long as it did; ConstexprFn's operand among
them); the lowering reads both from the syntax tree, as it did. **ast-residue**: ExprPtr 2,012, StmtPtr 512 -> 484,
node-casts 149, node-refs 337, readers 9, ast-lines 581 -> 568, recorded. **Shown** against REF: identical as (1),
the new case the only differ; `-g` as (1); run.sh 1,249 passed, 0 failed; self-compile of the five changed files
(ParserStmt, ParserClass, ParserInit, ParserExprCall, Classes) for x86_64-linux -O2 by (1) and by (2): text the same
for each; tms6747 refused by both alike (calls outside the C6000 selector).

**Gates on the final tree** (step 2): g++ -Werror clean (each step built on Linux); cl /W4 /WX clean, started first and
alone - a full build of (1) in 49 s (work\m6d-s4gy), a delta of (1) and one of (2) about 1 min each; tests/tms6747.sh
SIM=1 on (2), shards 1-3 on Linux at -O2 428/0 and -O0 428/0, shards 4-6 on Windows at -O2 429/0 and -O0 429/0;
comment cap 0 over, memory gate pass, ide --check up to date, exclusions 0 uncited 0 stale after (1)'s six re-pointed;
ast-residue at or below the baseline after each commit, recorded lower with (2).

**Not done, and why.** Left of S4: `ExprStmt`, `Block`, `Try`, `Function::body` and `StmtPtr`. `Try` is built at five
sites (ParserClass.cpp's cleanup regions, ParserTopLevel.cpp's function try, ParserStmt.cpp's `try`) and written
late - `setHandlers`, `setCleanup`, `setTypes` through `tryBodySegments_` - though nothing reads it back: the next
lane gives it a builder that returns its `Block` and the late writes a place of their own (or none). `ExprStmt`
and `Block` are what every deleted class became, and `Function::body`'s `Block` is read by nothing now either, so
they go with `StmtPtr` last. The by-value conversion the lowering stops at (1) is owed before 528b76ac's spelling
can go. **Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test.

### M6d S4, eighth lane: the by-value copy the lowering stopped at, and `Try` deleted - lane m6d-s4h, 2026-11-19

**(1) A by-value argument converted by a constructor whose own parameter is a class by value** (src/ 93,562 ->
93,569). `f(Effect)` given an `Own` through `Effect(Own)` copies at one argument node twice - `Own` into the
converting constructor's parameter, then the `Effect` temporary into `f`'s - and `noteArgumentCopy` kept one record
per node, where two answers leave neither. The lowering built the converted `Effect` in place (convertedTemporary),
then asked the same node for `Own`'s copy and stopped: "internal error: a by-value argument whose copy the parser
recorded none for" (REF alike; lane m6d-s4g's p4.cpp, and every natural `loopStatement(..., std::move(cond), ...)`
of ParserStmt.cpp, an ExprPtr into a sema::Effect holding one). **Mend**: the record is kept twice, by node (as
before, two answers leaving neither) and by node and class (`Parser::copyKey`); the lowering asks the node first as
it did and the class only where it stopped, so no output that compiled before changes. Building every converted
argument in place first (tried as s1b) is what clang does at -O0 ([class.copy]/31) but changed by-value-lowered,
default-argument-template-id, ssa-return-braces-nullptr and user-conversion: not identical, dropped - elision is
permitted, so that is a choice for a later round, not a defect. **Case** converting-constructor-class-parameter
(clang 19.1.5's output, `507 607 0 43 83 4`: move-only and copyable parameters, from an xvalue, a prvalue and an
lvalue). 528b76ac's spelled `Effect(...)` calls are back to `std::move(cond)` / `std::move(step)`; one citation
re-pointed. **Shown** against REF (78ee93bb): identical.sh tms6747 1,824 compared 2 differ, x86_64-linux,
x86_64-windows (both spellings) and arm64-darwin 7,296 compared 8 differ, all the new case; `-g -S` on four targets 1
differ each, the new case; run.sh 1,250 passed, 0 failed; self-compile for x86_64-linux -O2 of the five changed
.cpp files by (1): all compile (ParserStmt.cpp's natural spelling among them); tms6747 refused as before (stack
arguments). ast-residue at the baseline.

**(2) `Try` leaves Ast.h** (src/ 93,569 -> 93,480). Nothing read it: its eight constructions (wrapMsCleanups,
wrapCleanups, cleanupRegion twice, the function-try terminate pad, and tryStatement's handler region, Microsoft
`try` and Itanium `try`) call `tryRegion(body, pad)`, which keeps the statements in a `Block` as returnStatement and
ifStatement do; the late writes nothing read back go with it - `setHandlers` (and the `msHandlers` list that fed it),
`setCleanup`, `setTypes` through `tryBodySegments_` (its save, restore and reset in Parser.cpp), and the catch-type
lists only `Try` held. The frame slots its constructor took are still allocated, in the order they were, so the
frame is laid out as before. 11 citations re-pointed, two comment groups cut to the cap. **ast-residue**: ExprPtr
2,012, StmtPtr 484 -> 477, node-casts 149, node-refs 337 -> 325, readers 9, ast-lines 568 -> 535, recorded.
**Shown** against REF: identical and `-g` as (1), the new case the only differ; run.sh 1,250 passed, 0 failed;
self-compile of ParserStmt, ParserClass, ParserTopLevel and Parser.cpp for x86_64-linux -O2 by (2): all compile.

**Gates on the final tree** (step 2, ecd58059; step 1 97200f37): g++ -Werror clean (each step built on Linux); cl /W4
/WX clean, started first and alone - a full build of the base in 1 min 17 s (work\m6d-s4hy), deltas of (1) 1 min
16 s and of the final tree 6 min 30 s (the box shared); tests/tms6747.sh SIM=1 on (2), shards 1-3 on Linux at -O2
434/0 and -O0 434/0, shard 4 on Windows at -O2 139/0 (the shared box ran a shard in 6 min 30 s; shard 5 at -O2 reported in the handover, 6 and -O0 4-6 owed); comment cap 0 over, memory gate pass, ide --check up to date,
exclusions 0 uncited 0 stale after each step's re-pointing; ast-residue at or below the baseline after each commit,
recorded lower with (2).

**Not done, and why.** Left of S4: `ExprStmt`, `Block`, `Function::body` and `StmtPtr` - every deleted class became
a `Block` of `ExprStmt`s, so they go together, last; `MsHandler` (still filled for noteMsCatch, its `body` now read by
nothing) goes with them. **Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test.

### M6d S4, ninth lane: `MsHandler`'s body and the statement nodes' unread members, and the two files S4's close needs - lane m6d-s4i, 2026-11-19

Brief boxkit/brief-m6d-s4i.txt; from 2e6d91b7 on branch m6d-s4b; REF built from 2e6d91b7 on Linux (`~/m6d-s4i-ref.exe`);
the compares are S4a's (tools/identical.sh on four targets; every case's `-g -S` text).

**(1) `MsHandler`'s body** (7073006b, src/ 93,480 -> 93,480). Nothing read it: the handler's statements are kept in a
local of the catch loop beside `mh`, for the same scope, so every expression lives as long as it did. `MsHandler`
itself stays - noteMsCatch reads its descriptor, byReference and constPointer for an unnamed catch's record.
**ast-residue**: ExprPtr 2,012, StmtPtr 477, node-casts 149, node-refs 325, readers 9, ast-lines 535 -> 534 (two citations it moved left stale, re-pointed in (2)). **Shown** against REF: identical tms6747 1,824 compared, 0 differ, x86_64-linux, x86_64-windows (both spellings) and arm64-darwin 7,296 compared, 0 differ; `-g -S` 896/911/911/886 same, 0 differ on each target; run.sh 1,250 passed, 0 failed.

**(2) What nothing read of the statement nodes** (c3512c4c, src/ 93,480 -> 93,465): `Stmt::pos` and its six writers,
`ExprStmt::expr` and `release`, `Block::body`, and `Function::body()`; the body the Function holds stays, read by
nothing, because it owns the expressions `ConstexprFn::value` and `ConstexprCtor::steps` point into. `Stmt::syntax`
stays: noteStmt's caller reads it (ParserStmt.cpp's `noted->syntax()`, eight records keyed by the statement's node,
and the condition declaration's `setup.back()->syntax()`) - its comment said nothing read it. **ast-residue**: ExprPtr 2,012, StmtPtr 477, node-casts 149, node-refs 325, readers 9, ast-lines 534 -> 526, recorded; 7 citations re-pointed, exclusions 0 uncited 0 stale.
**Shown** against REF: identical tms6747 1,824/0 differ, but 7,296 compared 4 differ and `-g` x86_64-windows 1 differ - all goto-out-of-handler-ms, where the new tree segfaulted: (3)'s write after free; run.sh 1,250 passed, 0 failed; self-compile of Parser.cpp for x86_64-linux -O2 text the same, ParserStmt.cpp compiles (REF refuses the REF text over the new headers, as s4h found).

**(3) A Microsoft handler's statements are kept with its `try`** (963cf664, src/ 93,465 -> 93,466). resolveGotos fills a
goto's cleanups `Block` after the jump is placed; since ecd58059 dropped the handler list that owned them, a Microsoft
handler's block was freed when its catch's iteration ended, and a goto out of the handler wrote into it after - 2e6d91b7
and (1) survived it by luck, (2)'s smaller `Stmt` did not. The bodies now join the `try`'s region, as an Itanium
handler's do. 3 citations re-pointed. **ast-residue** as (2), exclusions 0/0. **Shown** against REF: identical 1,824/0
and 7,296/0, `-g` 896/911/911/886 same, 0 differ on each target; run.sh 1,250 passed, 0 failed.

**Gates on the final tree** (963cf664): g++ -Werror clean (each step built on Linux); cl /W4 /WX clean - a full build found ParserStmt.cpp's while-branch `pos` unused once its setPos went (fixed), then deltas of (2) in 1 min 26 s and of (3) in 2 min 14 s (work\m6d-s4iy); comment cap, memory gate, ide --check pass; tests/tms6747.sh SIM=1 shards 4-6 on Windows on (2)'s tree at -O2 424/0 and -O0 424/0 ((3) changes only the Microsoft path); Linux shards 1-3 not run by this lane.

**Not done, and why - stopped, as the brief says, on two of lane m8h1's files.** `ExprStmt`, `Block`, `StmtPtr` and
the body cannot leave Ast.h without editing `src/parser/ParserExprCall.cpp` (:282-325, `flushTemporaries`,
`appendEffects` and `effectStatement`, which build `ExprStmt`s and `Block`s) and `src/parser/ParserExprNew.cpp`
(:1421-1427, `throwStatement`) - ParserExpr* is m8h1's on branch m8-g. A shim that kept the classes for those two
files alone, or a second copy of the four functions under new names, would hide the agreement this step exists to
remove. **What the close is, for the lane after m8h1 lands**: the expressions still have to be owned as long as they
are now (ConstexprFn and ConstexprCtor point into them), so
`StmtPtr` becomes a value that owns them and is no node - `Kept` in Ast.h, a handle on a heap record of the
expressions and the kept parts (the heap record so a goto's cleanups, filled by resolveGotos after the jump is
placed, stay addressable), null where a `StmtPtr` was null; `StmtPtr(new ExprStmt(e))` is `Kept(std::move(e))`,
`StmtPtr(new Block(v))` is `Kept(std::move(v))`, `setScope` goes (no reader), and `Stmt::syntax`'s readers take
the id noteParts returns. About 480 sites over ParserStmt, ParserClass, Parser.h, ParserInit, Classes.cpp,
ParserTopLevel and the two files above - mechanical, one commit. **What tools/ast-residue reads now**: StmtPtr
477 (not 0), ast-lines 526. **S5 starts with** S4's close above, then the design's S5: the frame without the
nodes - the dead slots (pad pointer, selector, unwind help) and the setters only nodes had, each shown identical by
itself. **Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test.

**M6d's wave merged with main, 2026-11-19 (lane mw12).** `git merge origin/main` (b582a877: M8.1 commits 1-5, H1, the
review's H2 and M items, the CRLF compare) into m6d-w at 95a40efb: merge 73cb592c, EXCLUSIONS re-pointed 832950f3. Two
conflicts: `Parser::throwStatement` - M6d made it a wrapper over `throwEffects`, main put its SubstitutionFailure for a
thrown value of a dependent type at its head; the check goes at `throwEffects`' head, so a throw expression and a throw
statement both take it. docs/EXCLUSIONS.md - 13 hunks of line numbers alone: M6d's side, then 22 citations re-pointed by
tools/exclusions on Linux (0 uncited, 0 stale). The 13 src files both sides changed merged without a fault. src/ 93,466
(m6d-w) and 94,287 (main) -> 94,181. g++ -Werror and cl /W4 /WX (alone, first) clean.
**The check, against REF = main b582a877.** identical.sh on Linux, four targets at -O0 and -O2: 9240 compared, 76
differ, all M6d's own cases (constexpr-body-alias, -null-after, -null-before, -static-assert, -typedef 10 each,
-noexcept-call 6, converting-constructor-class-parameter and -default-argument 10 each). Windows, five spellings with
diagnostics: 12584 same, 106 differ - those, constexpr-body-nested-refused and constexpr-constructor-out-of-line-refused
10 each, and constexpr-function-body 10 (its diagnostic gains clang's reason, "(variable declaration in a constexpr
function is a C++14 extension)", from S4d 26a14a80; the .error still matches). Self-host -S: REF cannot compile
ParserClass.cpp and ParserStmt.cpp (`loopStatement(StmtPtr, Effect, Effect, StmtPtr)` - a class parameter by value,
which is M6d's converting-constructor-class-parameter), 99 more same and Driver's `__TIME__`, both boxes; against
m6d-w's own 95a40efb, 101 of 102 same and Driver's `__TIME__`.
**Suites.** Linux run 1263/0, typed 916/0, syntax 5/0, tms6747 SIM=1 -O2 shards 1-3 435/0; Windows names 924/0,
overload 46/0, typed 897/0, syntax 5/0, tms6747 SIM=1 -O2 shards 4-6 435/0. Static gates rc 0 on both boxes;
ast-residue ExprPtr 2012, StmtPtr 477, node-casts 149, node-refs 325, visits 0, readers 9, ast-lines 526 - each at or
below main's (2012/635/158/357/0/9/717). **Owed:** subst.sh self fails - door functions shape 8 over a record of 7,
member-templates 4 over 3: make_owned's recorded Owned.h:8:20 shape in more units, because M6d's sources call
make_owned more (the new cxx3 over main's sources holds 7 and 3); the ceilings are not re-recorded, which would raise
them. Windows run.sh compares the program's CRLF output as is (m8h1's owed fault): with diff's
--strip-trailing-cr, 1232 passed, 31 failed, the same 31 as REF's 1222/31 (x86_64-windows refusals such as
alignas-local-unwind's over-aligned local beside a try).

### M6d S4's close: `ExprStmt`, `Block`, `Function::body` and `StmtPtr` leave Ast.h for `Kept` - lane m6d-s4j, 2026-11-19

Brief boxkit/brief-m6d-s4j.txt; from 6b97ad9a on branch m6d-s4j; REF built from 6b97ad9a on Linux (`~/m6d-s4j-ref.exe`);
the compares are S4a's (tools/identical.sh one target a job, the m6dh gate holding the other slots; every case's `-g -S` text).

**(1) `Kept`, a value and no node** (src/ 94,181 -> 94,139). What the statement nodes did - own the expressions
`ConstexprFn` and `ConstexprCtor` point into - a `Kept` does: a handle on a heap `KeptRecord` (an expression and the
parts under it), null where a `StmtPtr` was null. `StmtPtr(new ExprStmt(e))` is `Kept(std::move(e))`, `StmtPtr(new
Block(v))` is `Kept(std::move(v))`, an empty block `Kept(std::vector<Kept>())`; a goto's cleanups are the
`KeptRecord *` its `LabelDef` holds, which resolveGotos appends to after the jump is placed - the record stays where it
is when the handle moves. Deleted from Ast.h: `Stmt`, `ExprStmt`, `Block` (and `setScope`, which nothing read, with the
locals that only fed it), `StmtPtr`, the Visitor's two statement visits, and `Function::body_` (the
Function keeps a `Kept`, read by nothing). `noteStmt` went: `Stmt::syntax`'s readers take the id noteParts returns -
statement()'s eight records keyed by `noted`, and the `if` condition's `decl` through `ifConditionDeclaration`'s and
declaration()'s out-parameter. **Found**: cxx3 refuses a nested class defined outside its class (`struct Kept::Held {`,
"expected a name") - every self-compile failed, so the record is `KeptRecord` at namespace scope (a gap to register for
the self-host, not mended here). 12 exclusion citations re-pointed, 0 uncited, 0 stale.
**ast-residue** on Linux: ExprPtr 2,012, StmtPtr 477 -> 0, node-casts 149, node-refs 325 -> 302, visits 0, readers 9,
ast-lines 526 -> 513, recorded. **Shown** against REF (c91b44f4): identical tms6747 1,848 compared, x86_64-linux 1,848,
x86_64-windows (both spellings) 3,696, arm64-darwin 1,848 - 0 differ on each; `-g -S` of 1,269 cases 908/923/898/923 same (the rest refused alike), 0 differ on tms6747, x86_64-linux, x86_64-windows and arm64-darwin; run.sh 1,263
passed, 0 failed; self-compile for x86_64-linux -O2 of the eight changed .cpp files: all compile; tms6747 -O2 refuses
seven by the stack-arguments refusal, Parser.cpp compiles. REF cannot read its own text over the new headers (`StmtPtr`).

**Gates on the final tree** (c91b44f4): g++ -Werror clean (Linux build); cl /W4 /WX clean, started first and alone - a
full build of the base in 2 min 42 s (work\m6d-s4jy), a delta of the first spelling in 2 min 26 s, a full build of the
final tree in 2 min 44 s (work\m6d-s4jz); tests/tms6747.sh SIM=1 -O2 shards 1-3 on Linux 148/0, 146/0, 141/0 and 4-6 on
Windows 142/0, 149/0, 144/0; comment cap 0 over, memory gate pass, ide --check up to date, exclusions 0 uncited 0 stale.

**What tools/ast-residue reads now**: StmtPtr 0 - no statement class is left in Ast.h; what is left is the expression
nodes (ExprPtr 2,012, node-casts 149, node-refs 302), which the constant evaluator and the parser's building half
read. **S5 starts with** the design's S5: the frame without the nodes - the dead slots (pad pointer, selector, unwind
help) and the setters only nodes had, each shown identical by itself; `Kept` itself goes when ConstexprFn and
ConstexprCtor read the typed tree instead of pointing into the parser's expressions. **Owed to the wave gate:** winpass
in both spellings, ssa.sh 1/2, names.sh, make test; and a register entry for cxx3's refusal of a nested class defined
outside its class.

### M6d S5: the frame without the nodes - the unwind-help word and the pad slots nothing names leave the parser's frame - lane m6d-s5, 2026-11-19

Brief boxkit/brief-m6d-s5.txt; from 2ccd4a51 on branch m6d-s5; REF built from 2ccd4a51 on Linux (`~/m6d-s5-ref.exe`);
the compares are S4j's (tools/identical.sh one target a job beside the m6dh gate; every case's `-g -S` text, four targets).

**Why a dead slot moves nothing.** The parser's frame allocator still numbers every slot, but since M6d's fourth round
the generators read only the head of its frame - the parameters' offsets, the result slot and a variadic function's
register save area (`ir::Laid`), all handed out before the body is parsed. A body's slot reaches output only as a `-g`
local's offset, and a local with a live alloca takes the selector's slot (Regions::ssaFrameLocals, Tms6747::emitScope);
one without is dropped. So a slot taken in a body moves no offset any generator reads - the design's "same offsets in the
same order" holds for every offset that is read - and each removal is shown identical by itself all the same.

**(1) The unwind-help word** (e2b40fb2, src/ 94,139 -> 94,126): `Parser::msUnwindHelp` and its three calls (a Microsoft
`try`, `wrapMsCleanups`, `cleanupRegion`) - the runtime's word is the selector's own (`ssaUnwindHelpSlot_`, SsaSelect.cpp's
layoutSsa). 6 exclusion citations re-pointed. **(2) The pad slots nothing names** (c5b21da4, src/ 94,126 -> 94,116): the
Microsoft cleanups' pointer and selector slots (`wrapMsCleanups`, `cleanupRegion`'s Microsoft arm) and the Itanium
cleanups' and noexcept wrapper's selector slots (`wrapCleanups`, `cleanupRegion`, topLevel's [except.spec]/9 wrap), whose
numbers no caller took. 8 exclusion citations re-pointed. Each: 0 uncited, 0 stale.
**Shown** against REF: commit 1 - identical tms6747 1,848 compared, x86_64-linux 1,848, x86_64-windows (both spellings)
3,696, arm64-darwin 1,848, 0 differ on each; `-g -S` of 1,269 cases 908/923/898/923 same (the rest refused alike), 0
differ on the four. Commit 2 - the same: identical 1,848, 1,848, 3,696, 1,848 compared, 0 differ; `-g -S` 908/923/898/923
same, 0 differ on the four.
**ast-residue** on Linux, both commits: ExprPtr 2,012, StmtPtr 0, node-casts 149, node-refs 302, visits 0, readers 9,
ast-lines 513 - unchanged (no Ast.h line and no node spelling moved), not re-recorded.

**Gates on the final tree**: g++ -Werror clean (both Linux builds); cl /W4 /WX clean, started first and alone - a full
build of the final source in 1 min 11 s (work\m6d-s5y); run.sh 1,263 passed, 0 failed;
tests/tms6747.sh SIM=1 -O2 shards 1-3 on Linux 148/0, 146/0, 141/0 and 4-6 on Windows 142/0, 149/0, 144/0;
self-compile by the new compiler of the three changed .cpp files: x86_64-linux -O2 all compile; tms6747 -O2 refuses each
by the stack-arguments refusal (`classRangeEnds`'s lambda), as at S4j. Comment cap 0 over, memory gate pass, ide --check
up to date. winpass is not owed: the Microsoft frame did not change (x86_64-windows identical in both spellings).

**What is left of S5**: the pad slots whose numbers still go into an expression node - a `try`'s `.ex.ptr`/`.ex.sel`
(ParserStmt.cpp's statement and its handler resume slots, `handlerResumePtr_`/`Sel_`, `tryChain*Slot_`), the Itanium
cleanups' pointer slot (`resumeUnwinding`, `cleanupPad`) and the noexcept wrap's `.ex.ptr`. Those `Var::local` nodes sit in
`Kept` records; a slot goes with the node that names it - at S8, or earlier once a lane shows the `Kept` holding it unread
(the lowering reads the syntax tree), each shown identical by itself as these two were. No Ast.h setter is left that only deleted nodes called (each `set*` in Ast.h has a caller outside it).
**What tools/ast-residue reads**: the expression nodes only (ExprPtr, node-casts, node-refs under src/parser and
src/sema, the nine readers, Ast.h's 513 lines). **Before S6**: nothing of M6d's design is owed but S6 (on-demand typing
and `-check=fold`, a Fable design waiting on the user), S7's three expression-shape lanes (Opus, beside S6) and S8.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, names.sh, make test.

### M6d S7, first family: the shape tests of ParserExprCall.cpp and ParserInit.cpp - one onto its `Resolved` record, twenty-five waiting on S6 or on another family's builders - lane m6d-s7a, 2026-11-19

Brief boxkit/brief-m6d-s7a.txt; from 0a8d3784 on branch m6d-s7a; REF built from 0a8d3784 on Linux from clean
(`~/m6d-s7a-ref.exe`); the compares are S5's (tools/identical.sh one target a job; every case's `-g -S` text, four targets).

**(1) The atexit helper's destructor** (9a941192, src/ 94,116 -> 94,116): `registerDestruction`'s record for the SSA
builder (`memberBuilt_`'s "~object") took the destructor's symbol from `dynamic_cast<const Call *>`; it reads the call's
`Resolved` record now. By construction the same: `destructorCall` returns `completeCall`'s value, a `void` call that
`useReference` leaves bare and `resolve` gives a `Resolved::Call` record carrying the symbol the `Call` was given.
**Shown** against REF: identical tms6747 1,848 compared, x86_64-linux 1,848, x86_64-windows (both spellings) 3,696,
arm64-darwin 1,848, 0 differ on each; `-g -S` of 1,269 cases 908/923/898/923 same (the rest refused alike), 0 differ.
**ast-residue** on Linux: node-casts 149 -> 148, node-refs 302 -> 301 (the `const Call *c` declarator), the rest unchanged
(ExprPtr 2,012, StmtPtr 0, visits 0, readers 9, ast-lines 513); recorded.

**What is left of this family, and why each waits** (25 casts; none decides output by a record that exists today):
- ParserExprCall.cpp `releaseTemporary` (4: Comma, Unary, Var, Call; asked by a `return`, ParserStmt.cpp, and a
  new-expression's initialiser, ParserExprNew.cpp) and `pendingTemporaryOf` (9: Comma, Conditional, Cast, Assign, Unary,
  Cast, MemberAccess, Var, Call; asked by `extendTemporary`). Both walk the shape the parser built around a temporary -
  `*(ctor(&tmp), &tmp)`, a call's `resultSlot` (set by `completeCall`, moved by `claimCallResult`), a saved base pointer
  `(p = base, c ? ... : ...)` - to its slot, during the parse. The comma, the guard and the dereference have no syntax node
  and no record, and the typed node is not there when the parser asks (the Typer runs per body after the parse). They move
  with S6's typed node, or earlier if the builders return the slot beside the node - `classTemporary`/`constructTemporary`
  (ParserExprNew.cpp), `useReference`/`bindReference` (ParserExpr.cpp): another family's files, so not this lane.
- ParserInit.cpp `stringInitialiser` (1: StrLit) - it hands back the literal node itself and its ten callers read its label,
  type and bytes: it moves when a literal is a record (S8), not as a shape test.
- `flattenScalar`'s cast strip (1: Cast) - the pointer's type before its conversions, for the base-subobject adjustment:
  the typed tree's conversions (S6).
- `foldAddress` (5: Cast, StrLit, Unary, Binary, Var) and `addressOfObject` (3: Var, MemberAccess, Unary) - the address half
  of the constant evaluator, asked during the parse (a static initialiser, a `constexpr` pointer; ParserClass.cpp and
  ParserTopLevel.cpp call it too): with `fold` at S6's C2 (`fold(TypedId)` under `-check=fold`). Their `Var` leaves could
  read a `Resolved::Global` record, but a `Var` the parser names itself (a static temporary's object, a type_info) has none,
  so the leaf moves with the walk, not before it.
- `bindStaticReference` (2: Unary, Var) - the address `bindReference` built: the user's name (which has a record) or a
  static temporary's object the parser named (none) - ParserExpr.cpp's builder returning the bound symbol, or S6.
**What tools/ast-residue reads** (node-casts 148, by file): sema/Constexpr.cpp 44, ParserExpr.cpp 19, ParserExprNew.cpp 14,
ParserExprCall.cpp 13, Parser.h 13, ParserInit.cpp 12, ParserOperator.cpp 11, ParserStmt.cpp 6, Parser.cpp 5,
ParserExprLambda.cpp 4, ParserOverload.cpp 3, and one each in Templates.cpp, Overload.cpp, Classes.cpp and
ParserDependent.cpp. S7's "about 46" after the three families holds only once S6 is in: of this family's 26, one moved
without it.

**Gates on the final tree** (9a941192's code; the plan entry changes no source): g++ -Werror clean (Linux); cl /W4 /WX
clean, a full build of the final source in 1 min 14 s (work\m6d-s7ay); run.sh 1,263 passed, 0 failed; tests/tms6747.sh
SIM=1 -O2 shards 1-3 on Linux 148/0, 146/0, 141/0 (4-6 not run: the m6dh gate's self-host held the Windows box);
names.sh 924 passed, 0 failed and overload.sh 46 agreed with clang, 0 differed - on the Windows box by the cl build (work\m6d-s7ay, clang 19.1.5; the Linux box has no clang);
self-compile of ParserInit.cpp by the new compiler: x86_64-linux -O2 compiles; tms6747 -O2 refuses `initConstruct` by the
stack-arguments refusal, as REF does on 0a8d3784's text. Exclusions 129 sites, 0 uncited, 0 stale (no line moved);
comment cap 0 over; memory gate pass; ide --check up to date.
**Owed to the wave gate:** tms6747 SIM=1 -O2 shards 4-6, winpass in both spellings, ssa.sh 1/2, make test.

### M6d: a nested class defined outside its class, `struct Outer::Held {` ([class.nest]/3) - lane m6d-nest, 2026-11-19

Brief boxkit/brief-m6d-nest.txt; from 0a8d3784 on branch m6d-nest; REF built from 0a8d3784 on Linux (`~/m6d-nest-ref.exe`).
Found by lane m6d-s4j: `struct Outer { struct Held; }; struct Outer::Held { int x; };`, legal C++03 and C++11, was
refused with "expected a name" - a refusal stricter than the standard. Not a listed exclusion (docs/EXCLUSIONS.md names
none for it); **a member class template** declared in its class (`template <class T> struct Box;` there) stays the
listed refusal (EXCLUSIONS.md, ParserTemplate.cpp), so its outside definition is not reached and has no case.

**The mend** (src/parser/ParserType.cpp, `structOrUnionSpecifier`, +21 -2): a qualified class-head followed by a body
(`{`, `:`, or `final` and one) whose qualifier names a class is read as the nested class that class declared - the
class becomes `within`, the scope a body written inside it has, so the tag, enclosing class, mangled names, lookup of
the class's own names and access to its private members are those of the inline definition. The name must be one the
class declared ([class]/11); otherwise it is refused as clang refuses it, "no struct named 'Missing' in 'Outer'". A
qualifier through a namespace alone (`struct n::S {`) or a template-id is not taken and is refused as before.

**Cases** (clang 19 on the Windows box, `-x c++ -std=c++11 -pedantic-errors`): `nested-class-outside` (the plain shape;
members, a static member and an inline body defined through both qualifiers; a private nested class defined outside;
`class Outer::Kept final : public Base`; a nested class of a class in a namespace defined in that namespace),
`nested-class-outside-lifetime` (constructors and a destructor defined out of line, the class as a member, a local, an
array, a by-value argument and a temporary under the ledger: built=8 gone=8, the rest 0) and
`nested-class-outside-refused` (the name the class never declared). REF refuses all three ("expected a name").
**Names**: names.sh (Windows clang, three ABIs) passes both new running cases; cl 19.44's external symbols for the two
match cxx3's on x86_64-windows, 7 of 7 and 8 of 8. overload.sh 46 agreed, 0 differed.
**Shown** against REF, one target a job (tools/identical.sh at -O0 and -O2; every case's `-g -S` text): identical tms6747
1,852 compared, x86_64-linux 1,852, x86_64-windows (both spellings) 3,704, arm64-darwin 1,852 - **4, 4, 8 and 4 differ,
every one `nested-class-outside` or `nested-class-outside-lifetime`** (REF refuses them), 0 outside them; `-g -S`
908/923/898/923 same, 2 differ on each target, the same two cases, 0 outside them. `nested-class-outside-refused` is
refused by both (REF with "expected a name"). **For the wave's rows**: the two running cases differ from REF on all four
targets, at -O0 and -O2, both Windows spellings, and under `-g`.
**ast-residue** on Linux: ExprPtr 2,012, StmtPtr 0, node-casts 149, node-refs 302, visits 0, readers 9, ast-lines 513 -
the baseline, unchanged, not re-recorded. 28 exclusion citations into ParserType.cpp re-pointed: 0 uncited, 0 stale.

**Gates on the final tree**: g++ -Werror clean (Linux); cl /W4 /WX clean, a full build of the final source in about 2
min (work\m6d-nesty); run.sh 1,266 passed, 0 failed; tests/tms6747.sh SIM=1 -O2 shards 1-3 on Linux 151/0, 145/0,
144/0; self-compile of ParserType.cpp by the new compiler: x86_64-linux -O2 compiles; tms6747 -O2 refuses
`structOrUnionSpecifier` ("a branch past eight arguments"), as REF refuses the base text. Comment cap 0 over, memory
gate pass, ide --check up to date. src/ 94,135 lines (+19).
**What is left**: a class declared in a namespace and defined through it from outside (`struct n::S {`), and the
outside definition of a member class template once member class templates are no longer a listed refusal.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, make test.

### M6d: a class declared in a namespace and defined through it from outside, `struct n::S {` ([namespace.memdef]/2) - lane m6d-nest2, 2026-11-19

Brief boxkit/brief-m6d-nest2.txt; from a488f489 on branch m6d-nest2; REF built from a488f489 on Linux (`~/m6d-nest2-ref.exe`).
Left by lane m6d-nest: `namespace n { struct S; } struct n::S { int x; };`, legal C++03 and C++11 ([class]/11,
[namespace.memdef]/2), was refused with "expected a name" - a refusal stricter than the standard. Not a listed exclusion.

**The mend** (src/parser/ParserType.cpp, `structOrUnionSpecifier`, +27): a qualified class-head with a body whose
qualifier names a namespace (`resolveNamespaceName`, so an alias or a relative name too) is read with that namespace as
the namespace stack for the rest of the specifier, body and inline member bodies included - the tag, `inNamespace`, the
mangled names and unqualified lookup ([basic.lookup.unqual]/7) are those of the definition written inside n. The stack
is restored on every way out. The definition must be in a namespace enclosing n, else "cannot define or redeclare 'S'
here because namespace 'b' does not enclose namespace 'a'"; the name must be a class n declared, else "no struct named
'Missing' in namespace 'n'" - both clang's words. **`struct n::Outer::Held {`** (m6d-nest's class path, the class in a
namespace) now reads its body in n as well, under the same enclosing check; at a class's or function's scope nothing changes.

**Cases** (clang 19 on the Windows box, `-x c++ -std=c++11 -pedantic-errors`): `namespace-class-outside` (the plain
shape from the global namespace; a typedef, an enumerator and a function of n used unqualified in the body; members and
a static member defined through `n::S::`; `class n::K final : public Base`; `struct n::Outer::Held {` through both
layers; `struct b::T {` and `union b::U {` defined in `a`, which encloses `a::b` and is not the global one),
`namespace-class-outside-lifetime` (constructors and a destructor defined through `lib::Part::`, the class as a member,
a local, an array, a by-value argument and a temporary under the ledger: built=7 gone=7, the rest 0),
`namespace-class-outside-refused` (a definition from `b`, which does not enclose `a`) and
`namespace-class-outside-missing` (a name n never declared), each refusal's .error clang's message. REF refuses all
four with "expected a name". **Names**: names.sh (Windows clang, three ABIs) passes both running cases and m6d-nest's
two; cl 19.44's defined external symbols match cxx3-msvc's, 8 of 8 and 7 of 7. overload.sh 46 agreed, 0 differed.
A first draft used `const int seed` in n: cxx3 emits a local symbol for it that clang folds away (names.sh), a
difference of its own and not this mend's, so the case uses an enumerator.
**Shown** against REF (tools/identical.sh at -O0 and -O2; every case's `-g -S` text): identical tms6747 1,856 compared,
x86_64-linux 1,856, x86_64-windows (both spellings) 3,712, arm64-darwin 1,856 - **4, 4, 8 and 4 differ,
every one `namespace-class-outside` or `namespace-class-outside-lifetime`** (REF refuses them), 0 outside them; `-g -S`
910/925/900/925 same, 2 differ on each target, the same two cases, 0 outside them. **For the wave's rows**: the two
running cases differ from REF on all four targets, at -O0 and -O2, both Windows spellings, and under `-g`; the two
refusals are refused by both (REF with "expected a name").
**ast-residue** on Linux: ExprPtr 2,012, StmtPtr 0, node-casts 149, node-refs 302, visits 0, readers 9, ast-lines 513 -
the baseline, unchanged, not re-recorded. 30 exclusion citations into ParserType.cpp re-pointed: 0 uncited, 0 stale.

**Gates on the final tree**: g++ -Werror clean (Linux); cl /W4 /WX clean, a full build in 68 s and the final delta in
about 2 min (work\m6d-nest2y); run.sh 1,270 passed, 0 failed; tests/tms6747.sh SIM=1 -O2 shards 1-3 on Linux 146/0,
148/0, 147/0; self-compile of ParserType.cpp by the new compiler: x86_64-linux -O2 compiles; tms6747 -O2 refuses
`structOrUnionSpecifier` ("a branch past eight arguments"), the refusal REF gives the base text (lane m6d-nest). A first
draft's `std::equal` was refused by the self-compile (not in cxx3's library) and became a prefix compare. Comment cap 0
over, memory gate pass, ide --check up to date. src/ 94,162 lines (+27).
**What is left**: `struct ::n::S {` (a leading `::`) and a class template's definition through its namespace are not
taken and are refused as before; the outside definition of a member class template stays behind its listed refusal.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, make test.

**M6d's wave landed, 2026-11-19 (gate m6dh at 6b97ad9a against main b582a877: 44 passed, 0 failed, 127 minutes).** S3c,
S4a-S4i and the merge with main (lane mw12) on main and m3-line. The first gate (m6dg, 55b3b02d) passed 40 and failed only
identical, identlnx, identwin and identa64 on the wave's own 93 differences - eight cases new to M6d (constexpr-body-*,
converting-constructor-*) that main refuses - named row by row in 6b97ad9a from the gate's lists. Cycles: 71 worse, every
one owed or accepted; 192,025,550 -> 149,248,103 at -O2. tests/wave-expected.txt emptied.

### M6d S6: on-demand typing and `-check=fold`, design C2 - lane m6d-s6, 2026-11-19

Brief boxkit/brief-m6d-s6.txt; from 4f91ee2f (m6d-w2 merged with m8-2b) on branch m6d-s6; REF built from 4f91ee2f on Linux
(`~/m6d-s6-ref.exe`). Opus 5.5, by the user's word of 2026-11-19.

**The design.** *Where a constant is asked.* `Constexpr::fold(const Expr &)` is asked during the parse - an array bound, a
case label, an enumerator, a static or `constexpr` initialiser, a template argument - and at that moment the expression's
syntax node is complete and every `Resolved` record the parser made for its names, calls and members is in
`Parser::resolutions_` (written where each was resolved, keyed by syntax node). *Typing one full-expression on demand*:
`Typer::typeExpression(syntax::NodeId)` runs the per-body Typer's own `node()` over that subtree with no scopes, so a name
is typed by its record (`named` falls back to `resolvedType`), and every other fact the Typer copies (folded literals,
sizeof's `keywordValues_`, cast types) is the parser's as it stands at the ask. One Typer and one `TypedTree` an ask: the
per-body trees the driver types after the parse are not touched, and no parser map is written. *The key* is the syntax
node of the asked expression (`Expr::syntax()`), which carries the token offset it was read at (`Node::loc`); nothing is
numbered anew, so a replay, which re-reads the same token indices and builds its own syntax nodes, asks again with its own
node. *A replayed template expression* is therefore typed again per instantiation by construction: the replay's ask names
the replay's node, whose records are the replay's (under `-templates=subst` the binding is m82b's `Typer::bound`, which an
on-demand Typer starts empty - the read body's typing stays slice 2b's, not a second path). *What `fold(TypedId)` covers
here*: literals, `sizeof`/`alignof` (their recorded values), names of constant locals, globals, enumerators and folded
static members (the same lookups `fold(const Expr &)` makes, by the record's name or symbol), parentheses, unary `+ - ! ~`,
the integer binary operators and `&& ||`, `?:`, the comma whose left folds, and a written cast between integers. Where the
Ast has an implicit conversion node the typed tree has none, so an operand is narrowed to its operator's type - the usual
arithmetic conversions, or the promoted operand of a shift - only where that type differs from its own, as the Ast's
inserted `Cast` narrows it, and the ask's result to the Ast's top type. *Later* (each counted as "not reached" by reason):
a constexpr call (the frame of parameters, a `ConstexprFn`'s operand as a typed root - S7/S8 move the body to the syntax
tree), a member of a constant object and `*p` (`foldLeaf`), addresses (`foldAddress`, `addressOfObject`,
`bindStaticReference` - s7a's list), floating operands (`foldFloating`, `foldDouble`), a comma whose left is an effect
(`runConstexprEffect`), and an expression with no syntax node (one the parser built itself). *`-check=fold`* folds every
ask from outside the fold twice and fails the unit on a difference, printing compared / agreed / differ / not reached by
reason; nothing without the flag reads what it adds.

**The code** (0a65cb71, src/ 94,315 -> 94,482, +167: Constexpr.cpp +138, Constexpr.h +12, Driver.cpp +11, Parser.h +3,
Typing.h +2, Driver.h +1). `Typer::typeExpression`; `Constexpr::fold(const TypedTree &, TypedId, ...)`; `checkFold` and
`foldReport`; `fold(const Expr &)` asks the check once at its outermost call (a depth count), so a nested fold is never
held twice. Two rules the probes asked for: a folded static member's record carries its value (serial 1, the parser's
`Num`), read as the Ast reads the `Num`; and **a template's read** (`patternBody_` or `readLayout_` at the ask) is not
compared - the parser's values for what depends on a parameter are stand-ins there (`include/deque`'s
`sizeof(T) < 32 ? ... : 16` folded to 16 by the Ast and 1 by the typed tree at the class door's read; the instantiation's
ask agrees), which M8rev L2's dependent refusal settles, not this check.
**-check=fold** (x86_64-linux `-S`). tests/cases: 16,475 asks compared, **12,243 agreed, 0 differ**, 4,232 not reached - a
template's read 3,410, a call 299, an object or an address 211, a subscript 79, inside a constexpr call's frame 68, an
operand not of integer type 42, no syntax node 37, a member 33, a cast of a floating or pointer value 27, a name with no
constant record 25, a floating operand 1; the 345 refused units end before the report. The self-host's sources (102 units of
src/): 33,657 compared, **22,575 agreed, 0 differ**, 11,082 not reached - a template's read 5,831, a call 3,378, an operand
not of integer type 645, a member 563, an object or an address 304, a name with no constant record 231, a subscript 126, a
postfix 4; every unit reported. **ast-residue** unchanged (ExprPtr 2,012, StmtPtr 0, node-casts 148, node-refs 301, visits 0,
readers 9, ast-lines 513): the check adds no Ast read beyond `fold`'s own signature.
**Gates** (REF 4f91ee2f and NEW 0a65cb71 built in one Linux tree, `~/m6d-s6`): identical.sh at -O0 and -O2 - tms6747 1,856,
x86_64-linux 1,856, x86_64-windows 3,712 (both spellings), arm64-darwin 1,856 compared, **0 differ**; `-g -S` - tms6747 912 same /
364 refused alike, x86_64-linux 927/349, arm64-darwin 927/349, x86_64-windows 902/374, **0 differ**; run.sh 1,270 passed, 0
failed, and with `-templates=subst` 1,270/0; tests/subst.sh: run, typed, census, same and bodies pass (cases 17 under 17), **self fails - door functions shape 34, above the record of 8 - and fails the same on the base**: REF on 4f91ee2f's own sources (a clean `git archive` tree, `~/m6d-s6r`) prints the same line, so it is the unchecked merge with m8-2b's (owed to the wave gate, no record written here); a mend that took the new `std::map<std::string, int>` out of Constexpr.h left it at 34 and was dropped; tests/tms6747.sh SIM=1 -O2 shards 1-3 146/0 (1 skipped), 148/0, 147/0; self-compile of Constexpr.cpp and Driver.cpp by NEW at
x86_64-linux -O2 rc 0, tms6747 -O2 refused as REF is (`std::fma`, `<unistd.h>`); static: exclusions 0 uncited 0 stale,
comments 0 over, memory gate held, ide --check up to date; g++ -Werror clean; **Windows** cl /W4 /WX clean (first and alone,
07:42:57-07:45:08, 2 min 11 s, work\m6d-s6w), names.sh 928 passed 0 failed and overload.sh 46 agreed 0 differed by that build (clang 19.1.5).
**Where it stopped, and what is next.** The check is the deliverable and it holds; no deletion. What the next lanes reach, by
the counts above: a constexpr call (the frame of parameters over a typed operand - after `ConstexprFn::value` is a syntax
root), the leaf half (`foldLeaf`: members of constant objects, `*p`, subscripts) and the address half (`foldAddress`,
`addressOfObject`, `bindStaticReference` - s7a's list), floating operands (`foldFloating`, `foldDouble`), the comma's effect;
then S8 can replace `fold(const Expr &)` by `fold(TypedId)` where the check has said "agreed" for every ask.

**m6d-w2's wave landed, 2026-11-19 (gate m6dw2h at fc26c04f against main b7a27aec: 40 passed, 0 failed; the fixed point
checked not owed).** S4's close (StmtPtr 0), S5, S7's first family and the two class definitions from outside
(nested-class-outside, namespace-class-outside) on main and m3-line. The first gate (m6dw2g, 8383c38f) failed identical
x4 on the four new running cases (48 rows, named in fc26c04f) and subst and maketest on a race of the gate's own: both
ran in tests/out-run at once (subst was not of the run family; it is now). tests/subst-self.ceiling's functions shape
8 -> 34 by the coordinator's ruling (the three recorded places alone). tests/wave-expected.txt emptied.

### M6d S7, second family: ParserOperator.cpp's and ParserExpr.cpp's shape tests - two answered by the builder beside the node, seven made one, the rest named - lane m6d-s7b, 2026-11-19

Brief boxkit/brief-m6d-s7b.txt; from 2ae0bb1f (m6d-w3) on branch m6d-s7b; REF built from 2ae0bb1f on Linux from clean
(`~/m6d-s7b-ref.exe`); the compares are S6's (tools/identical.sh one target a job; every case's `-g -S` text, four targets).

**(1) An Assign answered by its builder** (c08b4130, src/ 94,482 -> 94,490): `compound` and `assignValue` (ParserOperator.cpp) set `*bytes`
where they build the built-in assignment - the clone path's `a = a @ b` and the bytewise `=` - and their two callers read
the flag where they asked `dynamic_cast<const Assign *>` of the result: the compound operator's lvalue mark (:1159) and a
base's `B::operator=(x)` called by name inside a member (ParserExpr.cpp:1879). By construction the same: every other return
of the two is `zeroOf`'s `Num`, `memberCallWith`'s call or `Comma`, `completeCall`'s call or its dereference, the class
`operator=`'s `*call`, or the slot path's `Comma` - none an `Assign`. The design's "a value the building function returns
beside the node", for expressions.
**(2) One test of an operand with no address** (same commit): `Parser::noAddressOf` (ParserExpr.cpp) answers a bit-field
or a register object and its name; the seven copies of that test - a reference's binding (`bindReference`), `&x`, `sizeof`,
the compound assignment's slot path and postfix `++`/`--` - ask it. Two casts where there were seven; it moves to a record
in one place when a member's width is one (no `Resolved` field carries it today: a `Member` record is put on the reference
member's dereference and on a virtual base's access, so the record is not the cast's shape yet).
Citations re-pointed: ParserExpr.cpp:2342 -> 2343, :2067 -> 2068; ParserOperator.cpp:854 -> 855, :972 -> 973.
**Shown** against REF: identical tms6747 1,856, x86_64-linux 1,856, x86_64-windows (both spellings) 3,712, arm64-darwin 1,856 compared, 0 differ; `-g -S` 912/927/902/927 same (tms6747, x86_64-linux, x86_64-windows, arm64-darwin; the rest refused alike), 0 differ; -check=fold over tests/cases 16,475 compared, 12,243 agreed, 0 differ (the same not-reached counts as S6).

**What could not move, and why** (each still a cast):
- ParserOperator.cpp `clonePure` (7: Num, Var, StrLit, Cast, Unary, Binary, MemberAccess, :586-641) - it copies Ast nodes
  to build Ast nodes (a compound assignment's read-back, `*(a = b, &a)`, a call's object named twice): it goes with S8,
  which deletes what it builds; `requireAssignable`'s `Var` (:672) - the read-only flag of a local has no record.
- ParserExpr.cpp: `decltype`'s `MemberAccess` (:2419) and `StrLit` (:2430), `bindReference`'s `Conditional` (:2473),
  `StrLit` (:2489) and `Assign` (:2501), the `Conditional` at :2584, `&`'s `Unary`/`Conditional`/`Call` (:3584-3599): each
  asks the shape the parser built, which no `Resolved` record matches (a `Member` record sits on a reference member's
  dereference and on a virtual base's access; a `?:` has none) and the syntax kind does not either (a wrapper the
  parser adds has no node): they wait for the typed node of the whole expression (S6 types an asked constant only). `overloadArgOf`'s `Num` (:838, a placeholder's serial), the literal's `StrLit` (:1112, S8's
  literal record), the callee's `Call` (:1133), sizeof's folded `Num` (:3388, the builder has no syntax node yet).
- s7a's 25 (ParserExprCall.cpp, ParserInit.cpp): unchanged - S6's `typeExpression` reaches integer constants only; the
  temporary walks, `foldAddress`, `addressOfObject` and `bindStaticReference` wait as s7a named.
**ast-residue** on Linux: node-casts 148 -> 141, node-refs 301 -> 298, the rest unchanged (ExprPtr 2,012, StmtPtr 0, visits 0, readers 9, ast-lines 513); recorded. **Left of S7**: ParserOperator.cpp 8 and ParserExpr.cpp 14 as named above, s7a's 25, Parser.h 13, Parser.cpp 5, ParserExprNew.cpp 14 and the rest of s7a's by-file list.
**Gates** (c08b4130): g++ -Werror clean; cl /W4 /WX clean (work\m6d-s7bw, a full build of the first edit 08:16-08:17 and a delta build of the final source 08:20:44-08:22:45, about 2 min); names.sh 928 passed, 0 failed and overload.sh 46 agreed, 0 differed (Windows, clang 19.1.5); tests/tms6747.sh SIM=1 -O2 shards 1-3 146/0 (1 skipped), 148/0, 147/0; self-compile of ParserExpr.cpp and ParserOperator.cpp at x86_64-linux -O2 rc 0, ParserExpr.cpp at tms6747 -O2 refused as REF is (`overloadedBinary`, stack arguments); static: exclusions 0 uncited 0 stale, comments 0 over, memory gate pass, ide --check up to date. **run.sh was still running at the hand-back** (~/m6d-s7b-run-n1.log): owed to the wave gate with winpass, ssa.sh, make test and shards 4-6.

**m6d-w3's wave landed, 2026-11-19 (gate m6dw3h at 8fdc673b against main 0155813c: 40 passed, 0 failed; the fixed point
checked not owed).** M6d S6 (on-demand typing, `fold(TypedId)`, `-check=fold`: 16,475 compared over tests/cases, 0 differ),
S7's second family (m6d-s7b: node-casts 141) and M8.2's slices 2a-2b and 2c's first steps (M8-PLAN, m82a-m82e) on main and
m3-line. Its first gate (m6dw3g, a559360a) failed static (the memory gate's raw pointers 89 -> 92, Typer::bound) and
maketest (four stale EXCLUSIONS citations after the merges) - both mended by lane m82e (cf14f98e, 8fdc673b); the lanes had
passed them because one static script printed `tail`'s exit status. m6dw3h's one failure was maketest's ssa.sh volatile
counts, a race of the gate's own: maketest (the run family) ran beside ssa1 and ssa2, all three in tests/out-ssa. Its
verdict cleared, maketest alone on the same tree (`-k`): PASS. The gate's fix (maketest apart from ssa1/ssa2) is owed to
the next wave.

### M6d: `::X` inside a namespace that has its own `X` - the constructor's key re-qualified, `::S::k`, and `vector::value_type` - lane m6d-glob, 2026-11-19

Brief boxkit/brief-m6d-glob.txt; from 7774bb42 on branch m6d-glob; REF built from 7774bb42 on Linux (`~/m6d-glob-ref.exe`).
Found by lane m82f's self-compile: inside `namespace sema`, `::Local(...)` and `typedef ::Local X;` built `sema::Local`.
**Not a listed exclusion** - a wrong answer. The type was always found right (`findGlobalTypedef`, ParserType.cpp's and
ParserExpr.cpp's `::` branches); the fault was one step later: a class's constructors are resolved under its key
`tag::local` ("X::X"), and `Lookup::qualifyForLookup` read that key as a name written in the source, so inside `n` it
tried `n::X::X` first - n's constructor was called on the global X's storage (a segfault when the arity matched, "no
function called 'n::X::X' takes these 1 argument(s)" when it did not). Every road that names a class by its tag went
through it: a declaration `::X a(1)`, `::X(2)`, a typedef of `::X`, `new ::X(5)`, `::X f = 6`, a base `::X`.

**The mend** (src/sema/Lookup.cpp, `qualifyForLookup`, +4): a qualified name that is a class's constructor key - the class
found under that exact key with that tag, and the key present in the table asked - is returned as it stands, since a
tag is absolute. **`::S::k`** (src/parser/ParserExpr.cpp, the `::` branch, +9): a static data member of a class found from
the global namespace, one level deep, through `staticMemberRef` as `S::k` is; `::S::f()`, `::S::T::k` and `::f<int>()`
stay behind the listed refusal, its entry re-worded and re-pointed. **include/vector** gains `typedef T value_type;`:
`std::vector< ::Local>::value_type` was refused because cxx3's vector had no such member, not because of the `::`.
A first draft compared the lookup's member-function pointer (`exists == &Lookup::hasFunctionNamed`); the self-compile of
Lookup.cpp refused it ("a 'binary' the typer did not type") and the test became a call through it.

**Cases** (clang 19 on the Windows box, `-x c++ -std=c++11 -pedantic-errors -nostdinc++ -isystem include`):
`global-qualified-class-in-namespace` (a type, a functional cast, a typedef, `std::vector< ::X>` and its `value_type`,
`::X::count` beside `n::X::count`, under the ledger: built=7 gone=7, the rest 0; `.nonames` for <vector>, the
standard-library reason), `global-qualified-constructor-same-arity` (n's constructor takes the same arguments: REF
compiles it and the program segfaults), `global-qualified-class-only-in-namespace` (`::Y` with only `n::Y`: refused,
"'::Y' was not declared in the global namespace", clang "no type named 'Y' in the global namespace"; REF the same).
**Names**: names.sh (Windows clang, three ABIs) on the final tree: 929 passed, 1 failed - the same-arity case on
arm64-darwin, where cxx1 names __Unwind_Resume (the cleanup of `new ::X(5)`) and clang does not, now its `.nonames`;
re-run over the three new cases, 2 passed, 0 failed. cl 19.44's defined external symbols match cxx3-msvc's for the
same-arity case, 10 of 10 (cl and dumpbin in work\m6d-globc). overload.sh 46 agreed, 0 differed.
**Shown** against REF (tools/identical.sh at -O0 and -O2, n3): x86_64-linux 1,860 compared, x86_64-windows (both
spellings) 3,720, arm64-darwin 1,860, tms6747 1,860 - **4, 8, 4 and 4 differ, every one `global-qualified-class-in-namespace`
or `global-qualified-constructor-same-arity`**, 0 outside them; `-g -S` x86_64-linux 927 same / 350 refused alike / 2
differ, x86_64-windows 902 / 375 / 2, the same two cases. **For the wave's rows**: those two cases differ from REF on all
four targets, at -O0 and -O2, both Windows spellings, and under `-g` (REF refuses the first and miscompiles the
second); the refusal case is refused by both.
**ast-residue** on Linux: ExprPtr 2,012, StmtPtr 0, node-casts 141, node-refs 298, visits 0, readers 9, ast-lines 513 -
the baseline, not raised. Exclusions 0 uncited, 0 stale (four ParserExpr.cpp citations re-pointed).

**Gates on the final tree**: g++ -Werror clean (Linux); cl /W4 /WX clean (work\m6d-globw: a full build in 68 s, the final
delta 10:46:44-10:48:21); run.sh 1,273 passed, 0 failed; tests/tms6747.sh SIM=1 -O2 shards 1-3 on Linux 149/0, 148/0,
143/0; self-compile of Lookup.cpp and ParserExpr.cpp by the new compiler at x86_64-linux -O2: both compile. Comment
cap 0 over, memory gate pass (raw-pointers 89), ide --check up to date. src/ 94,682 -> 94,695 lines (+13).
**What is left**: `: ::X(a)` - a base named with a leading `::` in a mem-initializer - is refused ("expected a member or
base to initialise"); the read is ParserTopLevel.cpp's, which lane m82g holds, so it is registered as
tests/open/global-qualified-base-initialiser with clang's answer. Templates.cpp's field-by-field fill of `::Local`
(lane m82f's work-round) can go back to `::Local(...)` once m82g lets go of the file.
**Owed to the wave gate:** winpass in both spellings, ssa.sh 1/2, make test.

### M6d: tests/run.sh on the Windows box - the CRLF compare, the host's `.notarget` rows, a `.nomasm` case's spelling - lane m6d-runcr, 2026-11-19

Brief boxkit/brief-m6d-runcr.txt; from 7b816998 on branch m6d-runcr; REF built from 7b816998 on Linux (`~/m6d-runcr-ref.exe`).
Three commits, all in tests/run.sh; no src/ line changed. Windows numbers are `CXX1=cxx3-msvc.exe` (work\m6d-runcrw, built
from 7b816998, the same compiler at every commit) over a copy of tests/ holding each commit's run.sh.

**Before** (7b816998): Windows 353 passed, 920 failed - the program's CRLF output compared as is, so only the 349 refusals,
2 printed cases and 2 of the 4 checks passed. Linux 1273 passed, 0 failed.
**4eabfce7, (1)**: a case's output and its `.expected` compared with `tr -d '\r'` on both sides, as syntax.sh and subst.sh do;
the host `-g` check likewise. No `.expected` holds a CR, so Linux and the Mac read the same. Windows: 1243 passed, 30 failed.
**c276689e**: the 30 were not 30 defects. run.sh named every host but the Mac `x86_64-linux`, so on the Windows box (Git's
bash, `MINGW64_NT`) it read the Linux `.notarget` rows and none of the 36 `x86_64-windows` rows; it names x86_64-windows
there now, as subst.sh does. Windows: 1239 passed, 2 failed, 35 skipped each with its reason; windows-h-layout, skipped
before by its x86_64-linux row, runs and passes. The 28 this skips (26 that did not compile; string-stoi, which aborts where `stoi` throws, and typeid,
which prints the Microsoft type_info names against an Itanium `.expected`) are each already recorded with its reason: Microsoft-ABI refusals by name
(a polymorphic base after the first, a polymorphic virtual base, a class-typed throw, a funclet's realigned frame, a class
returned inside a handler), the throw-from-a-funclet terminate rows, cl's own rules (UCRT has no `gets`, no POSIX headers,
`throw(...)` ignored, a cv-decorated variable name). No row was added or changed.
**a20c0cad**: the last two were run.sh's own. ucn-identifier has a `.nomasm` (no MASM takes a name past ASCII): run.sh now
spells such a case `-masm=gnu` on x86_64-windows, as emit.sh, subst.sh and win-structure.sh do (it compiles and prints its
`.expected` there). The quoted-pattern check failed because Git's bash cannot set `ulimit -t` and says so on stderr, which
the check sends to the stdout it wants empty; the complaint is dropped (the Windows box keeps no CPU limit, as before).
Windows: 1241 passed, 0 failed (888 printed their recorded output, 349 refused as recorded), 35 skipped. Linux 1273 passed, 0 failed at each commit.

**Gates.** identical.sh REF against the tip's build, four targets at -O0 and -O2: x86_64-linux 1860, x86_64-windows 3720,
arm64-darwin 1860, tms6747 1860 compared, 0 differ (a test-file change changes no compiler output). Static gates at the tip:
exclusions 129 sites, 0 uncited, 0 stale; comments 0 over the cap; memory gate rc 0 (raw-pointers 89); ast-residue rc 0
(ExprPtr 2012, StmtPtr 0, node-casts 141, node-refs 298, visits 0, readers 9, ast-lines 513). winpass not run (no compiler
change). **Left:** no new defect for tests/open, and no row added; the 36 rows were read, not re-measured. Three of them,
older than this lane, record a wrong answer of cxx3 for x86_64-windows rather than a refusal - catch-copy-throws and
dtor-throws-unwinding (no funclet marked terminating: a handler catches where the standard says terminate) and
noexcept-local-terminates (no noexcept wrap on that target) - owed as defects, not as targets; array-at-throws,
vector-at-throws and string-stoi cite "a class-typed throw is refused on that target", to be re-measured; Git's bash runs the Windows cases without a CPU limit.
