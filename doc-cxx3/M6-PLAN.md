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

### Wave 8 landed, 2026-11-14 (the coordinator)

M6c rounds 9 (m6r: the five loop passes on the stream; one stream from the parse to packetise) and 10 (m6s: the
loop passes' edits on operands; `rounds` writes no lines on a held stream; the kernel's turns rename entries).
- **Gate at 32b56531**: Windows winpass -O0 and -O2: 828 same / 0 differ, masm 827 / 0. Mac run.sh -O0 and -O2
  1215 / 0. Linux make test rc 0 (run 1217/0, emit 3399/0, typed 860/0, ssa 890/0, c90 447/0, exclusions 0/0,
  comments 0). tms6747 (vm6747, SIM=0, three shards): 814 passed, 0 failed at -O0 (393.2 M cycles) and -O2
  (**192.0 M**, unchanged); asm6x 833 / 0 at -O0 and -O2.
