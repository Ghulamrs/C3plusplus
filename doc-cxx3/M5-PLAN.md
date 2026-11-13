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

### M5.1, round 1 (2026-11-13, lane m5a, from `cd01e16b`)

`backend/A64Select.cpp` (426 lines; src/ +463 -8 over four files): arm64-darwin takes the module through
`Backend::setSsa` and writes a function from its `ir::Function` where the whole body is inside the selector's
subset; every other function is the walker's, **named by its reason** (`Arm64Darwin::ssaOutside`, one predicate).

- **The design.** The frame is laid above the parser's parameter slots (allocas at their alignment, then one
  8-byte slot per value); the prologue, the epilogue and `placeArguments` are the walker's, unchanged. A value is
  canonical in its slot (an integer extended as its type says, `narrowInt` after every definition); operands
  travel in x0/x1 and d0/d1, x9 addresses, x17 is an indirect callee. A branch's arguments are a parallel move
  of whole slots. A selected body gets no `a64Peephole` and no promoted locals: registers are M5.2's (the MIR
  allocator), so a selected function at -O2 is slower than the walker's today - measured at M5.3, not here.
- **The subset.** Integer, pointer and floating scalars; constants, `addr`, allocas, loads, stores, `memcpy` and
  `memset` to 4096 bytes, geps; integer and floating arithmetic, `smulh`/`umulh`, compares, every cast; `br`,
  `condbr`, `switch`, block arguments; direct and indirect calls of scalars, Darwin's `...` words included; `ret`.
- **Outside, by reason** (the cases at -O0): landing pads 1306, a class result 1067, a class parameter 319, a
  call with a class result 107, a call with a class argument 83, a value that is no scalar 19, a variadic
  function 16, `tlsaddr` 4, `va_arg` 3, an over-aligned frame 2; and `-g` (a selected body has no line records
  or locals yet) and "no SSA body". `Select` and `Trap` have no rule on x86 either.
- **The seam in the driver.** arm64-darwin lowers every unit (a body outside the lowering is no error there)
  and the passes run at the level the unit is compiled at (the `volatile` downgrade included).
  `CXX1_A64_WALKER=1` hands the generator no module: the walker alone, the oracle for a differential.
- **Census** (`CXX1_SSA_WHY`, the switch x86's uses: `a64 walker <symbol>: <why>` per function and
  `a64-select: selected=N of M` per unit; `tools/ssa-census -arch arm64-darwin` sums them): the cases **7832 of
  10758 functions (72.8%) at -O0, 7823 (72.7%) at -O2**, 334 cases selected whole, 790 with a selected function;
  cxx3's own sources and the C90 cases **24217 of 40577 (59.7%)** at -O0.
- **Run.** Cross-compiled on the Linux box (0 cases broken by the selector path at either level), each `.s`
  linked and run on the Mac, serially, through `mac-run.sh`: **785 of 790 at -O0, 784 of 789 at -O2** print
  their recorded output. The five that do not (`ssa-aggregate-member-temporary`, `ssa-aggregate-temporary-members`,
  `ssa-aggregate-value-temporaries`, `ssa-census-last-bodies`, `ssa-new-arguments-outlive`) print the same wrong
  answer from the walker alone at -O0: the baseline's, M5.0's to register in `tests/open/`.
- **Gates.** cl /W4 /WX clean; exclusions 0 uncited, 0 stale; comment cap 0 over; memory gate holds;
  `ide/generate.py --check` up to date.
- **Owed.** `identical.sh ARCHS=arm64-darwin` by name (every unit with a selected function changes: the reason
  is this round); cases with a second translation unit (`.part.cpp`) were not in the run; the Mac's own build
  and `tests/run.sh` there are the wave gate's. M5.2: class values, sret, pads, `-g`, variadic bodies, registers.

### M5.2, round 1 (2026-11-13, lane m5c, from `4646883a`)

Class values and landing pads in the arm64 selector (`A64Select.cpp` 426 -> 527 lines; src/ +190 -69 over four
files, the walker's class return, class argument and class result made three helpers both paths call).

- **A class is its address.** A class parameter is the slot the walker's prologue filled; a class result goes
  out through `returnAgg` (the caller's pointer, an HFA's lanes, or x0/x1) and a reference result is the address
  it is. At a call `ssaArgType` gives `placeArguments` the signature's class where the caller copies (x86's
  rule): an HFA or 16 bytes at most in registers by `aggToRegisters`, a larger trivial class as the address of
  a copy in **one area of the frame** all calls share (a call's operands are values already), one the parser
  passes by address as the pointer it is. A class result comes back into its alloca, or through the function's
  own result pointer (`CallInfo::sret`), x8 naming it where `planFor` says indirect; `aggResultInto` stores the rest.
- **Landing pads.** An `invoke` is one call-site row between two labels, with its pad's chain, indices and bits
  (M3-IR 3.13), registered in address order and never sorted; a pad block stores x0 and the sign-extended w1 into
  its two parameters at its landing label; the personality and the LSDA are named where `ir::Function::landingPads`
  says, and `lsdaTable` writes the table as for the walker. Pads no invoke names get the walker's empty row.
- **Still outside, by reason** (the cases at -O0): a value that is no scalar 58 (15 at -O2), a variadic function
  16, a parameter that is no scalar and no class 15, `tlsaddr` 4, an over-aligned frame 4, a result that is no
  scalar and no class 4, `va_arg` 3; `-g`; a class through `...`; a pad that is no landing pad.
- **Census**: the cases **10654 of 10758 functions (99.0%) at -O0, 10697 (99.4%) at -O2** (72.8% and 72.7%
  before), 765 and 786 cases selected whole, 809 with a selected function, 0 broken by the selector path at
  either level; cxx3's own sources and the C90 cases **40528 of 40616 (99.8%)** (59.7% before), the walker's 88
  being a value that is no scalar 59, `tlsaddr` 21, a variadic function 8.
- **Two faults of a cl-built cxx3 cross-compiling for arm64, found by the Mac run and mended** - both the
  walker's, neither seen while only g++ built the compiler that wrote arm64: `loadFpConst` cast a double's bits
  through `long` (32 bits on Windows: 3.5 became 0), and `Parser::runtimeCall` read `argSlots.size()` beside
  `std::move(argSlots)` in one argument list (cl builds the last argument first, so `namedArgs` was 0 and
  `_tlv_atexit`'s three arguments went to the stack as if through `...`).
- **Where it was measured.** The Linux box's lock was held by other lanes' gates for the whole round, so the
  build, the cross-compiles and both censuses ran on the Windows box with `cxx3-msvc.exe` (`boxkit/m5c-winprobe.sh`,
  4 at once under `/affinity 3FF`); the `.s` were linked and run on the Mac through `mac-run.sh`, two at a time.
- **Run.** Each of the 809 `.s` linked and run on the Mac: **809 of 809 at -O0 and 809 of 809 at -O2** print
  their recorded output - M5.1's five baseline failures among them, their functions the selector's now.
- **Gates.** cl /W4 /WX clean; exclusions 0 uncited, 0 stale; comment cap 0 over; memory gate holds;
  `ide/generate.py --check` up to date (the four on the Windows box's copy of the tree).
- **Owed.** The Linux leg (g++ build, the same cross-compiles and censuses there - `boxkit/m5c-probe.sh`, the
  tree is `~/m5c`) and `identical.sh ARCHS=arm64-darwin` by name are the wave gate's; `.part.cpp` cases and the
  Mac's own `tests/run.sh` as before. M5.2 round 2: the values that are no scalar (a class loaded whole, 58), a
  reference or array parameter, variadic bodies and `va_arg`, `tlsaddr`, over-aligned frames, `-g`, registers.
