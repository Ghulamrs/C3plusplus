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

### M5.2, round 2 (2026-11-13, lane m5d, from `a8f60bb2`)

m5c's last outside reasons taken into the arm64 selector (`A64Select.cpp` 527 -> 564 lines; src/ +90 -30 over three
files, the walker's thread_local address, `va_start` and `va_arg` made three helpers both paths call).

- **A class or an array as a value is its address** in its slot, as the walker's `load` leaves it (x86's rule): a
  store of one is the walker's `copyBlock`, a `ret` of one goes out through `returnAgg`, a call passes it where the
  signature takes the class by a copy, and a bitcast between it and a pointer (an array's decay) moves the address.
  Any other instruction with a class value is refused by name ("a class value into/from '<op>'"), as is one the
  callee takes by address.
- **A reference parameter** is the pointer its slot holds; **a pointer to data member** is an 8-byte scalar (a pointer
  to member function wears a struct's shape and is a class value).
- **Variadic bodies**: Darwin's `va_list` is a char pointer; `va_start`/`va_arg` are the walker's (`vaStartAt`,
  `vaArgAt`) over the list's address; `va_end` is nothing. **`tlsaddr`** is the walker's `tlsAddress` (the TLV getter,
  or the `_ZTW` wrapper where `k` says). **An over-aligned frame**: `realign_` also reads the SSA frame's allocas, and
  a slot in a realigned frame is addressed from x19's floor through `localIn`, as the walker's.
- **Census**: the cases **10758 of 10758 functions (100.0%) at -O0 and at -O2** (99.0% and 99.4% before), 811 cases
  selected whole, 0 broken by the selector path at either level; cxx3's own sources and the C90 cases **40626 of 40626
  (100.0%)**, the walker 0 (99.8% before). Still outside by rule, none met: `-g`, a class through `...`, a float
  through `...`, a pad that is no landing pad, a block past 4096 bytes; registers are not this round.
- **Where it was measured.** The Linux lock was held by other lanes' gates; the cl build, both probes and the source
  census ran on the Windows box with `cxx3-msvc.exe` (`boxkit/m5d-winprobe.sh`), the `.s` linked and run on the Mac.
- **Run.** Each of the 811 `.s` linked and run on the Mac through `mac-run.sh`, two at a time: **811 of 811 at -O0
  and 811 of 811 at -O2** print their recorded output.
- **Gates.** cl /W4 /WX clean; exclusions 0 uncited, 0 stale; comment cap 0 over; `ide/generate.py --check` up to
  date (on the Windows box's copy); memory gate quiet on the Linux box (no file it reads changed).
- **Owed.** The Linux leg (g++ build, the probes and the census there) and `identical.sh ARCHS=arm64-darwin` by name
  are the wave gate's; `.part.cpp` cases and the Mac's own `tests/run.sh`; `-g` and registers (M5.2's rest), then M5.3.

### M5.2, round 3 (2026-11-13, lane m5e, from `9d18f117`)

Registers for arm64's selected bodies at -O1 and -O2 (`A64Select.cpp` 564 -> 835 lines; src/ 92,558 -> 92,851, +397 -104
over three files). -O0 is untouched: all 811 `.s` at -O0 are byte-identical to m5d's.

- **The design: the selector allocates over the SSA values, not over text.** x86's `opt::Local` and `mir::Allocator`
  read x86 assembly (`opt::MFunction`), so arm64 cannot share them; `Arm64Darwin::ssaAssign` is a linear scan over
  `ir::Function` itself, before a line is written. Liveness is solved over the blocks (a value used outside its
  defining block is live in), a value's range is the hull of where it is live in the order the blocks are written,
  and two values share a register only where one's last read is the other's definition.
- **Register classes.** x12-x15 and d16-d23 for a range that crosses no call; **x19-x28 and d8-d15 for one that
  does** (a `call`, an `invoke` - so every value live into a pad - and `tlsaddr`), and for the others once scratch is
  gone. x0-x2 and d0/d1 stay the travelling registers, x9-x11 and x17 the walker's helpers'; a realigned frame (x19
  its floor) and `-g` keep slots. Out of registers, the lightest range in reach (uses weighted 8 to the loop depth, a
  loop being an edge to a block written no later) gives its register up where the newcomer weighs more.
- **The frame.** The walker's conventions stand: a value in a register has no slot; the registers the body took are
  saved below the locals by the walker's own loop (`saved`, with its `.cfi_offset` rows - `stur`/`ldur` where the
  offset reaches) and restored at the return label. A frame the saves could not reach (65535) keeps its slots.
- **What else a register made possible.** Operands are read where they live (`ssaUse`) and results written where
  they live (`ssaDef`): arithmetic, compares, loads, stores, geps, conversions between integers take no move. A
  compare whose only reader is its block's `condbr` is flags and `b.cc`; an edge that moves nothing is branched to
  directly and a branch to the next block falls through; a constant under 4096 is an immediate of `add`, `sub` and
  `cmp`, any other constant without a register is written where it is read (no slot); a branch's arguments are a
  parallel move of *places*, since a parameter may share its register with a dying argument.
- **Bench** (the six kernels of `tools/windows/bench-kernels.cpp`, arm64-darwin `.s` written by the cl-built cxx3 at
  -O2, linked and run on the Mac, seven rounds interleaved, median ms; every checksum equal):

  | kernel | selected, registers (this round) | walker + `A64Peep` (`CXX1_A64_WALKER=1`) | selected, slots (m5d) | Apple clang -O2 |
  | --- | --- | --- | --- | --- |
  | fib | 3 | 7 | 10 | 0 |
  | sieve | 33 | 66 | 140 | 31 |
  | matmul | 7 | 27 | 78 | 2 |
  | isort | 18 | 33 | 89 | 6 |
  | hash | 178 | 318 | 1038 | 57 |
  | virtual | 128 | 217 | 684 | 70 |
  | total | **369** | 672 | 2052 | 169 |

  The selected bodies are faster than the walker with its peephole on all six (0.55 of its total), which is what
  M5.3 needs to delete `A64Peep`; against clang 2.2 times its total, `hash` and `virtual` most of the distance.

- **A wrong answer the bench caught and the cases did not**: the fall-through form of `condbr` lacked its `return`
  and branched twice (`hash`'s checksum); mended before the gate below. First tree's other fault (an operand read
  written inside the line that used it, 76 `.s` that did not assemble) was the Mac run's.
- **Gate** (on the final tree): cross-compiled on the Windows box (`boxkit/m5e-winprobe.sh`), 0 broken at -O0 and
  -O2, census **10758 of 10758 (100.0%)** at both, 811 cases selected whole; the Mac, serially through `mac-run.sh`:
  **811 of 811 at -O0 and 811 of 811 at -O2** linked, run and print their recorded output; cl /W4 /WX clean; exclusions 0 uncited, 0 stale; comment cap 0 over; `ide/generate.py --check` up to
  date; the memory gate does not run on Windows (`SIGPIPE`) and no file it reads changed.
- **Owed.** The Linux leg (g++ build, probes, memory gate) and `identical.sh ARCHS=arm64-darwin` by name are the wave
  gate's; cxx3's own sources' census was not re-run (the subset did not change). What the table leaves to M5.3:
  the entry sequence still stores each parameter to the parser's slot and reads it back; a signed 32-bit result is
  re-extended (`sxtw`) after every operation; no addressing modes (`[x, x, lsl]`, offsets) and no `madd`; `-g`.

### M5.2, round 4 (2026-11-13, lane m5f, from `ac1c3337`)

`-g` on arm64's selector: a function compiled with `-g` is selected, not walked - the `-g` fallback in `ssaOutside` is
gone, the last thing keeping the walker for a body inside the subset (src/ 92,851 -> 92,909, +62 -4 over three files).

- **Lines and lexical blocks** as x86's selector writes them: before each instruction, its `ir::Inst::lines` (a
  statement's `markLine`, a block's `markBlock` by `Function::blockAt`) and its own `pos`; the body's end closes what
  is open (`closeOpenBlocks`). `Dwarf.cpp`, the CFI and the prologue are the walker's, unchanged.
- **Variables** (`Arm64Darwin::ssaLocals`): the parser's locals with their offsets moved to the selected frame - each at
  the slot of the alloca the lowering made for it, matched by name in declaration order (an alloca's text is its
  declaration, `const char * label`, so a whole word of it); the copies live in `ssaLocals_` for `writeDwarf`. A
  parameter with no live alloca keeps the slot the walker's prologue fills; a local with none has no record (named under
  `CXX1_SSA_WHY`, `a64 -g f: 'x' has no slot`). **At -O0, 726 have none: 694 the parser's own (`$list0`, `.lam0`,
  `.ilobject0`), 32 named** - a result built in place (NRVO: `r` of `to_string`, its home is `*sret`), an anonymous
  union's members (the union is the alloca), and locals of handlers nothing reaches. **At -O1 and -O2 a promoted local
  has no record** (its values are SSA values; a location list is what would describe them); values keep the registers
  round 3 gave them - nothing describes a value, so no slot is needed for one.
- **A Windows host's `.file`** names its include directory `C:\Users\...`, which Apple's assembler refuses as an escape;
  the name is escaped now (the walker had the same fault: no `-g` cross-compile from the cl host assembled).
- **lldb, not run: the Mac's Developer Mode is off** (`DevToolsSecurity -status`), so `debugserver` waits for an
  authorization no lane may give, and `run` hangs - for the walker's build as for this one; turning it on is the
  user's. **What was checked instead, statically** (`tests/m10/w2.cpp`, `w4.cpp`, both builds linked with
  `dsymutil`, `dwarfdump --debug-info`; `boxkit/m5f-maclldb.sh` is the lldb leg for when it can run): the `.loc` rows
  of both functions' bodies are the walker's, line for line; every parameter and local of `work`, `total`,
  `perimeter` and the constructors has a record of the walker's name and type, at `DW_OP_fbreg` of the slot the
  selected body stores it to (`work`'s `c` at -57 where the walker's is -25: the selected frame's allocas sit above
  its parameter slots, rounded to 16; `total`'s `pt` at -24, written by the body's `sub x0, x29, #24` store).
- **Gate** (on the final tree, cross-compiled with the cl-built `cxx3-msvc.exe` on the Windows box, `boxkit/m5f-winprobe.sh`):
  **-O0 -g and -O2 -g: 0 broken, census 10758 of 10758 (100.0%), 811 cases selected whole**; the Mac, through
  `mac-run.sh` at -j2: **811 of 811 at -O0 -g** assemble, link, run and print their recorded output; cl /W4 /WX clean;
  exclusions 0 uncited, 0 stale; comment cap 0 over; `ide/generate.py --check` up to date; memory gate: no file it reads
  changed (it does not run on Windows).
- **Owed.** lldb at a line on the Mac once Developer Mode is on (`m5f-maclldb.sh`: break, `frame variable`, `bt`); the
  Linux leg and `identical.sh` are the wave gate's; NRVO results (`DW_OP_fbreg` of the sret slot, then `DW_OP_deref`),
  anonymous-union members and location lists for promoted locals; x86's SSA path records the parser's offsets, not the
  selected frame's - the same mapping wants to be shared (it lives in `A64Select.cpp` today).
