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
  run-time path (ParserStmt now emits a dynamic list, whose Init has no value of its own), and the lowering places its values leaf by leaf (staticArray, aggregateInit), the reason "a static/thread_local
  array built at run time" kept for arrays of classes (static- and thread-local-array-runtime-initialiser). **A
  reference member's default initialiser** was refused by a check that fired exactly when an initialiser followed the
  declarator; it is gone, memberInitialiser binds the reference as `r(v)` does, and the lowering's reference branch
  takes it (reference-member-default-initialiser). **A member copied from a derived object** (`: a(d)`, A trivially
  copyable) went to overload resolution and found no `A::A`: constructMember converts to the base subobject first, and
  the lowering's `$bytes` copy takes the subobject as classLocal does (member-copied-from-derived). src/ lines:
  56 added, 23 removed over seven files (ParserTopLevel, ParserInit, ParserStmt, ParserClass, ParserType, Parser.h, ir/Lowering.cpp).
- **R1, the probes, round 2 (2026-11-13, lane r1q, from c1a54315).** **The open register first:** r1f's "14 agree" was
  Linux's count with tests/open/walker-* (arm64's, left alone); of the 24 other C++ entries two agree with clang on
  Windows at -O2 in both spellings (Linux -O0/-O2 queued at hand-back) - handler-guards-nested and handler-local-past-inner-try,
  both Ast.h-path faults that M3b.7's deletion closed - and are moved to tests/cases with clang's output; the other 22
  still differ (the c90-* five by decision, two-phase-lookup-late-name by the token-replay decision). **Then 42 probes**
  past Lowering.cpp:1414 and in BuilderCopy.cpp (statics, temporaries, operator->, conversion functions, `?:`, lambdas,
  returns and named return values, conditions, range-for, switch, implicit copies and element constructors), each
  accepted by the Windows box's clang -std=c++11 -pedantic-errors over the tree's include/. **(a) 5 refused by name as
  cited**, now tests/cases/lowering-*.cpp with a .error: a class local copy-initialised through a conversion function;
  a class returned while a local with a destructor is alive; a named return value built by defaults or a braced list;
  a named return value inside a handler; the implicit copy of a class with a bit-field. **(b) 0 changed**; 30 probes
  compile and print clang's answer (the shape a reason names is not what a program meets there). **(c) 0 shown
  unreachable.** **Wrong answers, in tests/open with clang's answer:** a static local reference to a class temporary
  destroys it at once instead of at exit (static-reference-to-temporary-destroyed-early, a miscompile); operator->
  returning a class whose own operator-> answers is refused (arrow-chain-through-class-result); a function reference
  bound to `k ? g : h` sees a pointer (function-reference-from-conditional); a declaration as a for's or a switch's
  condition is a parse error, not refused by name (for-condition-declaration, switch-condition-declaration); a static
  reference to a function is refused as binding a temporary (static-function-reference). `(k ? d : a).v` over two
  lvalues, base and derived, is lowering-conditional-derived's root and not entered twice. src/ lines: 0 changed.
  Owed: the miscompile first; the five front-end shapes; the reasons unprobed (Lowering.cpp 2290-2900 and past 3760,
  Builder.cpp).
- **Also to undo at M5.3:** 13 `lowering-*` refusal cases (R1 lanes) carry an `arm64-darwin` `.notarget` line - the walker writes what the lowering refuses until it goes.

- **R1's finds, round 2, mended (lane r1g, 2026-11-13, from m5-r1q 749b7d6d).** The six wrong answers r1q put in
  tests/open leave it. **The miscompile:** a static reference bound to a class temporary passed bindStaticReference's
  refusal because a class temporary is a dereference of its slot (a glvalue by shape, prvalue by mark); the mark is
  asked now, so `static const A &r = A(3);` is refused by name (static-reference-to-temporary-refused) instead of
  destroying the temporary at once. **Taken:** a declaration as a for's condition (made afresh each turn, as a while's;
  Lowering's forStmt reads it by condition()); a declaration as a switch's condition of integer or enumeration type
  (the switch in a block of its own, Lowering's new declaredCondition gives its value untested; a class there is
  refused by name, ParserStmt.cpp:1157); a function reference bound to `k ? g : h`; a static reference to a function
  (the guarded global holds its address). **arrow-chain-through-class-result's root was not operator->:** `P2 b = { a }`
  elided braces into the member q and asked P1's first member to take a P1 - [dcl.init.aggr]/11 initialises a member
  whole from an object of its own class, in the parser's emitFill and in Lowering's aggregateItem. Cases: the six
  moved plus aggregate-member-from-own-class, for-condition-declaration-turns, switch-condition-declaration-enum.
  Windows -O0/-O2 gnu+masm 36/36, cl /W4 /WX clean, exclusions 0 uncited 0 stale (outside 570 -> 569), comments 0,
  memory gate new 1054 -> 1053. src/ lines: +61 -17. Owed: a temporary with static storage of its own
  ([class.temporary]/5, `_ZGR...` on Itanium) so the refusal can go.

- **R1's finds, round 3 (lane r1h, 2026-11-13, from m5-r1g 6ba086ef).** **A static reference's temporary has static
  storage of its own** ([class.temporary]/5): r1g's refusal goes. The parser's staticTemporary declares the object
  beside the reference under the ABI's name - Linkage::referenceTemporary, measured: clang `_ZGR1r_`, `_ZGRN2ns2nrE_`,
  `_ZGRZ1fvE1r_`; cl `?$S<n>@@3UA@@A` at namespace scope (the scope dropped, n counting the unit's temporaries),
  `?$S<n>@?1??f@@YAHXZ@4UA@@A` in a function (a built class without its const, `HB` for a scalar), its atexit helper `??__F$S<n>@@YAXXZ` made by registerDestruction - and
  records it on the reference's guard; Lowering's staticTemporary builds it where the reference is bound (a class
  temporary by its constructor, a scalar stored, a trivially copied class's bytes), binds the reference and registers
  the destructor as a static object's is (destroyedAtExit, now the one registration staticConstruction uses too). A
  call's result with a destructor is refused by name (elidedCall returns only into an alloca); an incomplete class, an
  array or void by name in the parser (ParserInit.cpp:1815). Case static-reference-to-temporary (namespace, namespace
  member, static member, static local, inline function's, scalar, call result) with the ledger at 0. **typed.sh 92 ->
  90:** a declarator's written bounds are its `var`'s first parts, and expectBraces began at part 0, so the nested
  braces of `int a[2][2] = {{..},{..}}` were never typed - it starts past varBounds_ now. **The open register's two
  stale copies** were the Linux box's old tree (a tar over it keeps deleted files); git had moved them in r1f - the box
  tree is made afresh now. One check for an array a guard builds (builtAtRunTime) where guardedStatic and threadStatic
  each had one.

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
  run-time path (ParserStmt now emits a dynamic list, whose Init has no value of its own), and the lowering places its values leaf by leaf (staticArray, aggregateInit), the reason "a static/thread_local
  array built at run time" kept for arrays of classes (static- and thread-local-array-runtime-initialiser). **A
  reference member's default initialiser** was refused by a check that fired exactly when an initialiser followed the
  declarator; it is gone, memberInitialiser binds the reference as `r(v)` does, and the lowering's reference branch
  takes it (reference-member-default-initialiser). **A member copied from a derived object** (`: a(d)`, A trivially
  copyable) went to overload resolution and found no `A::A`: constructMember converts to the base subobject first, and
  the lowering's `$bytes` copy takes the subobject as classLocal does (member-copied-from-derived). src/ lines:
  56 added, 23 removed over seven files (ParserTopLevel, ParserInit, ParserStmt, ParserClass, ParserType, Parser.h, ir/Lowering.cpp).
- **R1, the probes, round 2 (2026-11-13, lane r1q, from c1a54315).** **The open register first:** r1f's "14 agree" was
  Linux's count with tests/open/walker-* (arm64's, left alone); of the 24 other C++ entries two agree with clang on
  Windows at -O2 in both spellings (Linux -O0/-O2 queued at hand-back) - handler-guards-nested and handler-local-past-inner-try,
  both Ast.h-path faults that M3b.7's deletion closed - and are moved to tests/cases with clang's output; the other 22
  still differ (the c90-* five by decision, two-phase-lookup-late-name by the token-replay decision). **Then 42 probes**
  past Lowering.cpp:1414 and in BuilderCopy.cpp (statics, temporaries, operator->, conversion functions, `?:`, lambdas,
  returns and named return values, conditions, range-for, switch, implicit copies and element constructors), each
  accepted by the Windows box's clang -std=c++11 -pedantic-errors over the tree's include/. **(a) 5 refused by name as
  cited**, now tests/cases/lowering-*.cpp with a .error: a class local copy-initialised through a conversion function;
  a class returned while a local with a destructor is alive; a named return value built by defaults or a braced list;
  a named return value inside a handler; the implicit copy of a class with a bit-field. **(b) 0 changed**; 30 probes
  compile and print clang's answer (the shape a reason names is not what a program meets there). **(c) 0 shown
  unreachable.** **Wrong answers, in tests/open with clang's answer:** a static local reference to a class temporary
  destroys it at once instead of at exit (static-reference-to-temporary-destroyed-early, a miscompile); operator->
  returning a class whose own operator-> answers is refused (arrow-chain-through-class-result); a function reference
  bound to `k ? g : h` sees a pointer (function-reference-from-conditional); a declaration as a for's or a switch's
  condition is a parse error, not refused by name (for-condition-declaration, switch-condition-declaration); a static
  reference to a function is refused as binding a temporary (static-function-reference). `(k ? d : a).v` over two
  lvalues, base and derived, is lowering-conditional-derived's root and not entered twice. src/ lines: 0 changed.
  Owed: the miscompile first; the five front-end shapes; the reasons unprobed (Lowering.cpp 2290-2900 and past 3760,
  Builder.cpp).
- **Also to undo at M5.3:** 13 `lowering-*` refusal cases (R1 lanes) carry an `arm64-darwin` `.notarget` line - the walker writes what the lowering refuses until it goes.

### M5.3, the deletion (2026-11-13, lane m5g, from `a353705c` with `m5-wave1` merged in for its `.notarget` lines)

Every arm64-darwin function is written from the SSA IR; the walker's arm64 half and `A64Peep` are gone in one commit, as
M3b.7 did for x86 (src/ 92,942 -> 91,683, +43 -1,302 over five files, two deleted).

- **What went.** `Arm64Darwin` derives from `Regions`, not `Walker`: its 13 `Ast.h` visits (`Num` to `Return`) and what only
  they used (`genAddr`, `addOffset`, the three bit-field helpers, `genTruth`, `jump`/`branchIfZero`/`branchIfNotZero`,
  `loadFpConst`, `storeToStack`, `pushD`/`popD`, `terminatePad`); the promoted-locals frame and `a64Peephole`'s call in
  `emitFunction`; `src/optimizer/A64Peep.{cpp,h}` (702 + 13 lines); `CXX1_A64_WALKER`. What the selector shares stays
  (`placeArguments`, `planFor`, `copyBlock`, `load`, `genConversion`, the aggregate moves, `tlsAddress`, the va helpers).
  `Walker.cpp` had nothing arm64's alone: tms6747 keeps it whole.
- **What is refused now.** The driver treats arm64-darwin as x86: a function outside the lowering is an error naming it;
  one the lowering took but the selector's `ssaOutside` does not is `codegen: 'f', <reason>, is not supported yet by the
  arm64-darwin backend` (`ssaChoose`). No case reaches the second. The volatile downgrade stays arm64's (no MIR mark).
- **`.notarget`**: the arm64-darwin line is gone from 21 files - the 13 `lowering-*` (refused by name, each `.error`
  matched) and 8 citing the walker (`noexcept-set-terminate{,-callee}`, `ssa-own-landing-pads`, five `ssa-aggregate-*`/
  `ssa-census-last-bodies`/`ssa-new-arguments-outlive`); their tms6747 lines stay.
- **`tools/ast-residue`** counted nothing in src/backend, so the deletion did not move it; it has a `visits` kind now
  (a generator's visit of a node kind, declared or defined, under src/backend): **150 -> 124** (-26: 13 declarations,
  13 definitions). The other counts are the merge's: ExprPtr 2009 -> 2012 and StmtPtr 634 -> 635 came with `m5-wave1`
  (R1's parser finds) against a baseline it had not re-recorded; recorded here. The coordinator accepts the +3/+1: they are the parser's
  own fixes of a miscompile and refusals of valid code, and the parser builds `Ast.h` until M6d deletes its building half.
- **Gate** (cl-built `cxx3-msvc.exe` on the Windows box, `boxkit/m5g-winprobe.sh`): -O0, -O2, -O0 -g, -O2 -g: **0 broken,
  0 refusals wrong, 10815 of 10815 functions selected, 821 cases compiled**; the Mac through `mac-run.sh` at -j2: **821
  of 821 at -O0 and 821 of 821 at -O2** link, run and print their recorded output. Bench (`m5e-macbench.sh`, five
  rounds, median ms): this tree 371 = m5f's selected 371 (the `.s` byte-identical), m5f's walker + `A64Peep` 675, Apple
  clang 169; checksums equal. cl /W4 /WX clean (final tree); exclusions 0 uncited, 0 stale; comment cap 0 over;
  `ide/generate.py --check` up to date; `ast-residue --check` passes (it does not run on Windows: `SIGPIPE`, as the
  memory gate); the memory gate was not run (the Linux lock was held), and no file it reads changed.
- **Owed.** The Linux leg (g++ build) and `identical.sh ARCHS=arm64-darwin` are the wave gate's; self-host on target 4
  (M5.4); the plan's design text (`Compiler_Design_Final.md` sections naming `A64Peep`) is history now.

### Wave 1 landed, 2026-11-13 (the coordinator)

`m5-wave1` = M5.0 (m5b), M5.1-M5.2 (m5a, m5c, m5d, m5e, m5f), M5.3 (m5g), R1 rounds 1-3 (r1p, r1f, r1q, r1g, r1h), one
squashed commit. **Gate**, on the final tree:
- **Windows:** winpass at -O2 and -O0: 808 same / 0 differ (gnu), 807 / 0 (masm), 351 refused as recorded, 0 not.
- **Linux** (a fresh tree - a tar over the old one had kept `A64Peep.cpp`, and the wildcards built it):
  - `ssa.sh` 1 and 2: 870 / 0.
  - `make test`: run 1193 / 0, emit 3320 / 0, typed 840 / 0 at 46 untyped (record lowered from 90), ssa 870 / 0,
    coff 1650 objects 0 differ, c90 447 / 0, exclusions 0 / 0, comments 0, memory gate and ast-residue pass.
- **Mac:** `run.sh` at -O0 and -O2, 1191 / 0.
- **`identical.sh` against M4's build:** 7623 compared, 144 differ - 16 cases x 9, each a case R1 mended or added
  that M4 refused or miscompiled.
- **ast-residue baseline re-recorded** at ExprPtr 2013, StmtPtr 638, node-casts 165, node-refs 411 (visits 124 from
  150): the rises are R1's parser fixes, made in the half M6d deletes.

**Owed:** emit.sh's golden on Linux (none recorded on that tree); lldb on the Mac (Developer Mode, the user's);
target 3's self-host re-measured at M5's close; the C6000's M6a, not in this wave (its -O2 cycle gate is unmet).

### M5.4, self-host on target 4 (2026-11-13, lane m5h, from `1f8ec47b`, every arm64 function from the selector)

The Mac through `boxkit/mac-run.sh` at -j2 (`SELFHOST_JOBS=2`), stage 0 built by Apple clang++.
- **First run, -O0: stage 0 compiled 99 of 100.** `src/backend/A64Select.cpp:323` (M5.2 round 3's linear scan) calls
  `std::upper_bound`, which `include/algorithm` did not have: clang's libc++ compiles the tree, cxx1's library did not.
  Not a selector wrong answer - a library gap the walker's sources never met. Mended: `upper_bound` in
  `include/algorithm` ([upper.bound], halving, random-access as the rest of the header), and the case
  `tests/cases/algorithm-upper-bound` (empty, one, repeated runs, both ends, a pointer range; `.expected` from the
  Windows box's clang 19 with `-nostdinc++ -isystem include`, as par4 allows; `.nocl`, `.nonames` as
  `algorithm-sort-large`'s).
- **tools/self-host, -O0**: stages 0, 1, 2, 3 compiled 100 of 100 sources in 60, 492, 490, 491 s; **stages 0 = 1 = 2:
  100 of 100 the same object; 2 = 3: 100 of 100**. At M5.0 (the walker, -j8) the stages took 15, 62, 64, 62 s: at
  -j2 the cxx3-built stages are ~2x slower per job than the walker's were - the -O0 selector's code, measured, not
  mended here.
- **tools/self-host, -O2**: stages 0, 1, 2, 3 compiled 100 of 100 sources in 67, 102, 102, 102 s; **stages 0 = 1 = 2:
  100 of 100 the same object; 2 = 3: 100 of 100**. A -O2 stage compiles cxx3 in a fifth of a -O0 stage's time (102
  against 491 s); the clang-built stage 0 is 1.5x faster than the -O2 one.
- **No tools/ change was needed.** The Mac idle after (no cxx3 or self-host process of this lane). Owed: target 3
  re-measured at M5's close (the plan's row), the wave gate's run of the new case on every target.
- **R1, round 4 (lane r1i, 2026-11-13, from m5-r1h 87f934d9).** **The open register's seven:** six are walker-* (arm64's,
  left alone); the seventh, declaration-temporary-unwind, is an `#include` of tests/cases/ssa-own-landing-pads (the
  same .expected, ok on Windows -O0/-O2 both spellings) and is the walker's defect too, so it is renamed
  walker-declaration-temporary-unwind and the case's .notarget points there. **r1h's `_ZGR` names:** Windows clang
  `-target x86_64-linux-gnu -S` writes the eleven r1h emits; it makes h's `_ZZ1hvE1q`, `_ZGVZ1hvE1q`, `_ZGRZ1hvE1q_`
  weak in COMDATs, and cl makes `?q@?1??h@@YAHXZ@4AEBUA@@EB`, `?$TSS0@...`, `?$S10@...` External - **an inline function's
  static reference was a per-unit `h.q`**: it takes the ABI's name now (uniqueStaticSymbol given its type, ParserStmt),
  and with it its guard and temporary are shared (the atexit helpers stay file-local, as a static local's always were).
  **48 probes** over Lowering.cpp 2290-2900, past 3760 and Builder*.cpp, each accepted by Windows clang -std=c++11
  -pedantic-errors: 37 compile and print clang's answer. **(b) 6 taken**: unary `+` of a pointer, and so of a
  captureless lambda (Typing and the lowering); `++` of a bool (deprecated, still C++11); a static member through `->`
  of an expression (the pointer evaluated); a data member pointer value-initialised `P()` (was broken(), "a cast not
  recorded") and as a truth value; a lambda reading its function's static local uncaptured ("'s' was not declared").
  **(a) 4 refused by name as cited**, cases with a .error: a std::nullptr_t return, a thread_local reference in a block
  and at namespace scope, a static member through `.` of a call. A virtual base with virtual functions on the Microsoft
  ABI is refused by name in the parser (vtordisp), already cited. **(c) 0.** **Wrong answers, in tests/open:**
  function-reference-through-dereference (`void (&r)() = *p` refused: the parser reads `*p` back as p) and
  member-pointer-condition (requireScalar refuses a data member pointer as `?:`'s condition, `!`, a cast to bool).
  src/ lines: +17 -7 (Lowering.cpp, Typing.cpp, ParserStmt.cpp, ParserExprLambda.cpp).

- **R1, round 5 (lane r1j, 2026-11-13, from m5-r1i deb6df1b).** **r1i's two refusals of valid programs, taken; both
  out of tests/open into tests/cases with Windows clang's answer.** member-pointer-condition: requireScalar takes a
  pointer to a data member as a truth value ([conv.bool]) except under unary `+`, and the typer types `&&`/`||` of one
  (the lowering's truth() already compared with the ABI's null: -1 on Itanium, cl's on Microsoft); the case covers `?:`,
  `!`, `(bool)`, `&&`, `||`, `if`, `while`, an offset-0 member and a base's member converted to the derived class.
  function-reference-through-dereference: bindReference takes `*p` (and `**q`) of a function pointer - the parser
  keeps the dereference as p, and its syntax node says it was one. **An inline function's atexit helpers on
  Microsoft:** cl 19.44 makes `??__F$S1@?1??h@@YAHXZ@YAXXZ` (a static reference's temporary) and `??__Fa@?1??k@@...`
  (a static local) External in COMDATs, a non-inline function's Static; registerDestruction now does (the helper
  inline and not static), and pruneUnusedInline keeps it with its object, which is always kept (the lowering, not the
  parser, registers a temporary's helper, so nothing the parser sees names it). Two units both calling h and k link
  and destroy each object once, in both spellings. **Left:** `static_cast<int D::*>(&C::y)` is "a 'cast' the typer
  did not type" (the implicit conversion is typed). Windows 32/32 at -O0 and at -O2 over 16 cases (gnu and masm), cl /W4 /WX clean; Linux (fresh ~/r1j): 12/12 at
  -O0/-O2, open.sh 23 differ / 7 agree (the walker's), typed.sh 846/0, exclusions 0/0, comments 0, memory gate holds.
  src/ lines: +15 -5 (ParserOverload.cpp, ParserExpr.cpp, ParserInit.cpp, ParserClass.cpp, Typing.cpp).

### R1, round 6, and the count (lane r1k, 2026-11-13, from m5-r1j 6fc992c7)

- **`static_cast<int D::*>(&C::y)`** was "a 'cast' the typer did not type": noteKeywordForm recorded a named cast's
  type only for a scalar or a class, and only where its text held no `&` - which an operand `&C::m` always does. A
  member pointer's cast is recorded now, refused only for a reference type-id (`&>`). The lowering's
  memberPointerConversion takes [expr.static.cast]/12's way back as well, a derived class's member pointer to a base's,
  the offset shrunk and a null kept null (cxx1 types `&D::y` as `int D::*` where clang says `int C::*`, so
  `static_cast<int C::*>(&D::y)` needed it). Case `member-pointer-static-cast` (a base at a nonzero offset, a null
  through a function), clang's output.
- **`inline-static-exit-helpers`** (+ `.part.cpp`) holds r1j's linkage: an inline function's static local and its
  static reference's temporary, called from both units, under the ledger - built 3, gone 3, every fault column 0.
- **Two stale refusals**: r1j made a static-duration reference to a temporary, so
  `static-reference-temporary-refused` and `static-reference-member-temporary-refused` compiled and failed their
  `.error`; wave 1 had already replaced them with `static-reference-scalar-temporary` and
  `static-reference-member-scalar-temporary`, so the wave 2 merge kept those and dropped this lane's renamed pair.
- **Gate:** Windows cl /W4 /WX clean; the 39 member-pointer, static-local, static-reference and named-cast cases plus the
  two renamed at -O0 and -O2, gnu and masm: all ok (member-pointer-base-on-derived is .notarget there); exclusions
  0 uncited / 0 stale (one citation re-pointed, ParserExpr.cpp:3655), comment cap 0, memory gate holds. **Linux not
  run**: 2.4 GB free on the box, below the 3 GB rule - the coordinator's wave gate owes it.
- src/ lines: +8 -2 (ParserExpr.cpp, Lowering.cpp).

**R1's count.** Every tests/cases and tests/open case compiled -S for x86_64-windows with CXX1_SSA_WHY set (1229
files; tools/windows/lowering-why.py, matched by tools/windows/lowering-why-match.py, `<...>` as any text, a line given to its most specific
reason): of the 489 reasons in the ```owed block, **17 are reached by a tests/cases case, 3 more only by tests/open,
469 by none** (one target; a reason peculiar to Itanium or the C6000 is not asked). One reached reason is a Builder's
("a constructor of a class with bases, a vptr or members to build", lowering-implicit-copy-bitfield) and is not in the
block's text; one is a broken() (array-static-tail-defaults, tests/open). The reached ones:

| reason | first case |
|---|---|
| `<...>a reference parameter bound to a '<...>' that is not an object` | reached by tests/open/class-value-in-braced-aggregate.cpp |
| `<...>of a class with a bit-field or reference member` | reached by tests/cases/lowering-implicit-copy-bitfield.cpp |
| `<...>whose default argument makes a temporary with a destructor` | reached by tests/open/vector-default-temporary.cpp |
| `a braced list reaching a member the lowering does not place` | reached by tests/cases/lowering-braced-bitfield-reference.cpp |
| `a call returning '<...>'` | reached by tests/cases/lowering-return-nullptr-t.cpp |
| `a class argument that is not an object` | reached by tests/open/braced-class-in-braced-aggregate.cpp |
| `a class local initialised from a temporary of another type` | reached by tests/cases/lowering-local-from-derived-temporary.cpp |
| `a class local initialised from another type` | reached by tests/cases/lowering-local-from-conversion-function.cpp |
| `a class returned while a local with a destructor is alive` | reached by tests/cases/lowering-return-beside-destroyed-local.cpp |
| `a class with a destructor returned from another type` | reached by tests/cases/lowering-return-derived-temporary-destructor.cpp |
| `a class with a destructor returned inside a handler` | reached by tests/cases/lowering-return-destructor-in-handler.cpp |
| `a constructed class returned from another type` | reached by tests/cases/lowering-return-derived-local.cpp |
| `a named return value built with defaults, zeroed or from a braced list` | reached by tests/cases/lowering-named-return-defaulted.cpp |
| `a named return value inside a handler` | reached by tests/cases/lowering-named-return-in-handler.cpp |
| `a reference member bound to another type` | reached by tests/cases/lowering-reference-member-derived.cpp |
| `a return type of '<...>'` | reached by tests/cases/lowering-return-nullptr-t.cpp |
| `a static local built with default arguments` | reached by tests/cases/lowering-static-local-default-arguments.cpp |
| `a static member reached through what is not a name` | reached by tests/cases/lowering-static-member-through-object.cpp |
| `a thread_local reference` | reached by tests/cases/lowering-thread-local-reference-global.cpp |
| `an initializer_list of '<...>' elements` | reached by tests/cases/lowering-initializer-list-class-elements.cpp |

Every other owed reason: not reached (469).

### M5.3, the code after the deletion (2026-11-13, lane m5i, from `ff1d1e77`)

m5e's "left for M5.3" in `backend/A64Select.cpp`, at -O1 and -O2 only (A64Select 882 -> 1,033 lines; src/ 91,824 -> 91,987,
+176 -13 over three files). -O0 is untouched: all 833 `.s` at -O0 are byte-identical to m5-h's.

- **Parameters from their incoming registers** (`ssaDirect_`): where every parameter is a scalar or a reference in a
  register, the function is not variadic, the frame not realigned and there is no -g (which reads the parser's slots),
  the prologue stores nothing and the body narrows each `xN`/`dN` straight into the value's place. Over the suite at
  -O2, `mov x0, xN` (the old staging) 4,933 -> 379.
- **No `sxtw` a reader does not need** (`ssaLow_`): a signed 32-bit result keeps the upper half its `w` operation left
  where every reader reads only the word - word arithmetic, compares, stores of 4 bytes or fewer, narrowing conversions,
  `ret`, `cbz`/`cbnz`, and block parameters whose own readers are such (to a fixed point over the edges). A call's
  argument, a `switch`, a widening `sext` and an index stay canonical. `sxtw` 31,140 -> 23,926.
- **Addressing modes** (`ssaFold_` 1): a gep whose every reader is a load or store of its block is no instruction:
  `[xB, #k]` where the access scales `k`, `[xB, xI]`, and `[xB, xI, lsl #s]` where the index's `mul`/`shl` is by the
  access's size and read nowhere else; base and index live to the access. 3,960 register-index and 13,245 (from 6,794)
  immediate-offset accesses.
- **`madd`** (`ssaFold_` 2) for a `mul` immediately before the add it alone feeds (252 in the suite). **Not `msub`**:
  measured as a loss - `hash`'s remainder by a constant went 132 -> 136 ms with it (twice, 5 and 7 rounds) - and a first
  form that fused across intervening instructions grew a range and cost `matmul`'s frame a slot.
- **Bench** (`boxkit/m5i-bench.sh`: the kernels to arm64 `.s` at -O2 by the cl-built cxx3 of each step, linked and run on
  the Mac through `mac-run.sh`, seven rounds interleaved, median ms; every checksum equal):

  | kernel | m5-h | + parameters | + no `sxtw` | + addressing | + `madd` (this tree) | Apple clang -O2 |
  | --- | --- | --- | --- | --- | --- | --- |
  | fib | 3 | 3 | 3 | 3 | 3 | 0 |
  | sieve | 33 | 33 | 33 | 32 | 32 | 31 |
  | matmul | 7 | 7 | 7 | 5 | 5 | 2 |
  | isort | 18 | 18 | 18 | 18 | 18 | 6 |
  | hash | 177 | 178 | 147 | 132 | 132 | 57 |
  | virtual | 127 | 109 | 107 | 95 | 94 | 70 |
  | total | 368 | 350 | 316 | 288 | **287** | 168 |

  0.78 of m5-h's total and 1.7 times clang's (from 2.2); `hash` (a multiply-and-xor chain), `isort` (memory order) and
  `virtual` (the call itself) are most of the distance left.
- **Gate** (final tree, cl-built `cxx3-msvc.exe`, `boxkit/m5i-winprobe.sh`): -O0, -O2, -O2 -g: **0 broken, 0 refusals
  wrong, 10869 of 10869 functions selected, 833 cases compiled**; the Mac at -j2 through `mac-run.sh`: **833 of 833 at
  -O2** link, run and print their recorded output (-O0 is the base's bytes). cl /W4 /WX clean; exclusions 0 uncited,
  0 stale; comment cap 0 over; `ide/generate.py --check` up to date; on Linux the memory gate passes;
  `ast-residue --check` reports StmtPtr 635 -> 638, node-casts 164 -> 165, node-refs 409 -> 411, all under src/parser and
  src/sema, which this lane did not touch: the base's against an unrecorded baseline, for the coordinator.
- **Owed.** `identical.sh ARCHS=arm64-darwin` by name and the g++ build are the wave gate's; the Linux box had 2.4 GB
  free (under par4's 3 GB) when the static gates ran - this lane's tree there is removed. Left: the parser's parameter
  slots are still laid out (unused) under `ssaDirect_`; negative and unscaled offsets (`ldur`); `msub` re-measured
  with a different schedule; -g's parameters from registers.

### Wave 2 landed, 2026-11-14 (the coordinator)

M5.4 (m5h: target 4 self-hosts through the selector, -O0 and -O2, 0=1=2 and 2=3 100/100), arm64 code quality (m5i:
bench 368 -> 287 ms, clang 168), M6a (the C6000 selector, m6a2-m6d, -g by m6g), R1 rounds 4-6 (r1i, r1j, r1k), and
c6n's tms6747 leg of R1's cases. **Gate**, final tree:
- **Windows** (c1100a58; da250ac7 changed only tms6747 files): winpass -O2/-O0 820/0 gnu, 819/0 masm, 355 refused.
- **Mac** (c1100a58): run.sh -O0/-O2 1207/0.
- **Linux** (da250ac7, fresh tree): ssa.sh 1/2 882/0; make test green - run 1209/0, emit 3368/0, typed 852/0, c90 447/0,
  every gate; identical.sh against main 108 differ = 12 cases (10 new, `static-reference-to-temporary` r1i's ABI name,
  `lambda-static-local` r1j's External exit helper on Windows); arm64 -O0 12 (the new cases), -O2 804 (m5i), tms6747
  728/720 (M6a).
- **tms6747.sh SIM=0, three shards a level** (vm6747 cycles): selector -O0 393.2 M vs walker 447.7 M, -O2 192.2 M vs
  195.9 M - M6a's cycle gate met; failures the walker's three noexcept cases, plus two new cases registered;
  asm6x.sh 826 objects 0 failed at both levels.

**Owed:** lldb on the Mac (Developer Mode, the user's); target 3's self-host re-measured at M5's close; the M6a
sim6747 leg (SIM=1); emit.sh's golden on Linux.

