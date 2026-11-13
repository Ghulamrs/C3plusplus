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
