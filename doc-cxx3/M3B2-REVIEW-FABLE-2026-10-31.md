# M3b.2 interim review, Fable 5.1, 2026-10-31

Read-only review of M3b.2 as it stands in `~/cxxopt-build/lane-r19` (branch `m3b2-r19`): GitHub's `main`/`m3-line`
at `b060465` plus round 19's `c06f55a`, `b3ec043`, `2427a2c`, not landed. The range reviewed is `12dc8dc..m3b2-r19`,
164 commits, 2026-10-26 to 10-31. Read: `doc-cxx3/M3-PLAN.md` (the step table, the must-not-change list, the round
record to `19226cb`), `doc-cxx3/M3-IR.md` 3.6-3.12, `Compiler_Design_Final.md` section 12 and 12.1, CLAUDE.md's M2.16
and M2.17, `src/ir/Lowering.{h,cpp}`, `src/backend/SsaSelect.cpp`, `src/sema/Typing.{h,cpp}`, `src/Driver.cpp`'s
`lowerSsa`, `src/optimizer/MirAlloc.cpp`, `tests/ssa.sh`, `tests/run.sh`, `tools/self-host`, the three round-19
diffs. Nothing was built, run or changed; no box was touched. Every number below that is not marked "measured here
by reading" is the implementers' own, from the plan or a commit message.

**Verdict: on plan with conditions.** M3b.2 is being built in the shape the plan and M3-IR.md describe, at the pace
the record shows (94.8% of source bodies on Linux, 95.3% for the Windows target), and the gate that matters most -
the compiler's own sources through the lowering, stages 0 = 1 = 2 - now holds at -O0. Three things depart from the
plan as written and should be said in the plan rather than discovered at M3b.7; two gate rows the plan names have
not been run; and round 19 is not yet gated the way the record of 10-31 says every round now is. None of the
departures is a miscompile I can show from the code; the one wrong-answer risk I found is a dependency, not a
defect (finding 3). Nothing here blocks landing round 19 once its own gate (finding 7) has run.

## Findings

### 1. "No `Parser` in reach" is true of the header and not of the data - should fix (in the plan, not the code)

**Claim** (M3-IR 3.8, M3b.0's row): the lowering "is constructed with no `Parser` in reach" and "makes, from the
typed tree's scopes and nothing recorded by the parser: every frame object ... the destruction order ... the cleanup
pads and their nesting ... the handler regions"; the fifteen layout maps "are not read".

**What the code does.** `Lowering.h` includes nothing of `src/parser/` (measured here by reading: its includes are
`Ir.h`, `Typing.h`, `Syntax.h`, `Type.h`). But `Lowering.h:48-98` declares **thirty-one `set*` hooks**, and
`Driver.cpp:1354-1402` fills every one from the `Typer`, which reads the parser's maps directly - `Typing.cpp` reads
57 distinct `p_.` members (counted here). Some are resolutions, which 12.1 point 3 allows and asks for (which
constructor, which `operator=`, which destructor, the virtual slot). Others are the old path's *lowering decisions*:
- `returnBuilt_`/`namedReturnLocal` - what `buildReturned` and `nrvoCandidate` decided (NRVO placement);
- `elidedInto` - copy elision, and `Typing.cpp:650-654` infers it by comparing frame offsets:
  `c->second.result == l->second.offset` over `p_.callSlots_`, one of the fifteen maps 3.8 says are not read;
- `argumentCopies_` (`materialiseCopy`'s choice, with its `elided` bit), `convertedBuilt_`, `temporaryBuilt_`,
  `localBuilt_`, `assignCalls_`, `staticGuards_` (what `guardOnce` built), `vptrStores_`/`vttSplit_`/`baseVtt_`
  (the stores the parser's constructor made), `msVbaseInit_`, `memberBuilt_`;
- `lambdaFacts_` - another of the fifteen - for every closure.
And `Driver.cpp:1403-1427` takes `fn.hasLandingPads()`, `fn.sretSlot()`, `fn.frameAlign()` and `fn.locals()` from the
**`Ast.h` `Function` the parser built for the same body**; `Lowering.cpp:890` refuses "a local's cleanup in a
function the parser gave no landing pads", so whether a function has an LSDA is the old path's decision. The
selector still copies parameters out of the parser's `Param` slots (`SsaSelect.cpp:548-552`), the "one layout fact
borrowed" that M3b.1 said M3b.2 would re-point. Two more facts hold only while the old path runs first:
`Typer::microsoftThrowInfo` (`Typing.cpp:491-500`) answers a `_TI` name only where `Program::msThrows` already holds
the chain *the parser's body for the same function made*, and a destructor the SSA path alone would call is marked
used by nobody but the walker's body.

**Why it matters.** The plan's own judgement of 12.1 - "the frame layout, the cleanup regions and the EH tables are
not M2's: M3's lowering makes its own" - is half met: the lowering makes its allocas, its scopes' destruction order
and its pads, and takes from the parser the decisions *which* object is built where and by what. That is a workable
design (the parser's records are made in the scope the parser had, the typed tree's rule), but it is not the one
3.8 states, and it decides what M3b.7's deletion can delete: "the parser's `Ast.h`-building half and its fifteen
layout maps" cannot all go while `callSlots_`, `lambdaFacts_`, `buildReturned`, `materialiseCopy`, `guardOnce` and
the `msThrows` emission are the lowering's inputs. The record never says this; each round names its new record as a
feature.

**Fix.** Before RP4, amend M3-IR 3.8 to the design as built - a list of the hooks in `Lowering.h:48-98` sorted into
(a) resolutions the parser records (stay), (b) decisions of the old lowering the new one reads (each with the step
that re-derives it or the decision to keep the recording code after M3b.7), (c) facts taken from `Ast.h`'s `Function`
(`hasLandingPads`, `sretSlot`, `frameAlign`, the parameter slots, `msThrows`) that M3b.7 must replace. The deletion
commit's scope follows from that list; today it cannot be sized.

### 2. The Itanium call-site table is one row per `invoke`, not the walker's split regions - note, a sound departure

The must-not-change row (M3-PLAN:172-176) asks the lowering to "produce disjoint regions in the order the walker
does (`openRegion` splits the enclosing one)". The selector does something simpler and stronger: one call-site row
per `invoke`, in address order, each carrying its pad's whole action chain (`SsaSelect.cpp:389-398`), the gaps filled
by `lsdaTable` as before. Rows are disjoint by construction and need no splitting; the chain is built at guard time
by copying the enclosing pad's types (`Lowering.cpp:698-716`), with bit 1 for a filter-0 row where the chain has
types and bit 2 where the pad hands on (`Lowering.cpp:895`), and a terminate catch-all around every destructor
run while unwinding (`:922-943`) - the walker's shape of 1.3. This satisfies the row's intent (disjoint, address
order, never sorted) by a different mechanism; it should be written into M3-IR 3.6 as the design, since the
sentence there still describes region splitting. The Microsoft side sorts `ssaTryOrder_` (`SsaSelect.cpp:633`) -
the FH3 try map's inner-first order, which is not the Itanium rule the row is about.

### 3. The gates are weaker than "0 differ" sounds, in four named ways - should fix (two rows), note (two)

- **`ssa.sh`'s "lowered and verified, 0 failed" is well-formedness, not correctness.** `-check=ssa` runs the
  verifier of M3-IR 3.4 (dominance, arity, one terminator); `tests/ssa.sh` passes a case when `failed=0`. It is the
  coverage measure and says nothing about the code emitted. The record's phrase "lowered and verified" invites a
  stronger reading than the tool gives.
- **`run.sh` "1078 / 0" under the flag is ~738 running programs, not 1078.** 340 of the 1078 are refusals recorded
  in `.error` files, which the parser issues before any lowering (`run.sh:92-100`). The plan's own entries say so
  ("340 refused as recorded"); the headline numbers do not. The Windows "706 same, 0 differ" is against `.expected`
  (clang's), judged on the Mac (`run-cases.cmd:11-14`) - a real oracle - but round 19's Windows row ran one
  combination only, `-front=ssa -O2` (b3ec043's message), not -O0 and not the default pass.
- **`names.sh` under `-front=ssa` - an M3b.2 gate row (M3-PLAN:109) - has never been run and cannot be as the tools
  stand.** `tests/names.sh` has no `CXX1` of its own; `tools/mangled-names:20,149` runs `"$CXX1" -S ...`, so a flag
  cannot ride in the variable. Every `names.sh` number in the record (656/0 at M3b.1 and after) is flag-off. This
  row is exactly what would have caught round 17's `_ZTI`/`_ZTS` emitted where `parse()` had pruned them
  (`b2e438b`); the self-host caught it instead. **Fix**: give `mangled-names` a `CXX1_FLAGS`, run the row, record it.
- **The self-host comparison is the strongest gate and is -O0 only.** `tools/self-host` passes `FLAGS` to the three
  stages and to the `-S` comparison, so s0 = s1 = s2 under `-front=ssa` is a behavioural oracle over cxx3's own 84
  sources for every function the lowering reaches. At -O2 it cannot run (finding 6), so no gate exercises M3a's MIR
  passes over SSA-selected output on a whole program; `run.sh -front=ssa -O2` over the cases is what stands in.

What no gate sees: a lowered function whose only exercise is a case that prints the same under a wrong lowering
(the ledger covers lifetimes; nothing covers, say, a wrong `this` adjustment that lands on padding), and anything in
the 5% not lowered.

### 4. Round 19 spot-check: no miscompile found; three shapes the cases do not hold - note

Read against the parser's own paths, which the lowering must match:
- **A nested lambda's outer capture** (`c06f55a`): read through the call operator's `this_` by the member's offset,
  a reference member loaded first (`Lowering.cpp:1201-1230`). A nested `[this]` is refused when `f.thisType`'s class
  is not the function's own `thisType_` (`:1192-1194`) - so the shape where the inner closure would wrongly be given
  the *outer closure* as `this` is safe. The case (`ssa-lambda-outer-capture.cpp`) holds copy-of-copy,
  reference-rebound, `[=]` over a class and a class by value; it does not hold a lambda three deep, a `mutable` inner
  writing its own copy of the outer's copy, or an inner `[&]` of an outer by-value capture (writing through to the
  outer closure's member, [expr.prim.lambda]/15) - each follows from the code read but is unmeasured.
- **The Microsoft by-value catch** (`b3ec043`): the caught object guarded above the handler's `handlerLive_` mark and
  `closeScope`d at its end (`:5051-5054`), so a `return`/`break`/`goto` out of the handler (which destroy above the
  mark) and the fall-off all destroy it once. Consistent with cl. Not held by a case: a by-value catch of a class
  *with a copy constructor and a destructor* thrown as a derived class (slicing through the `_CT` record), and a
  handler that itself throws past the guarded object (a cleanup inside a handler, round 16's shape).
- **The pseudo-destructor no-op** (`:3543-3544`): `!hasDestructor()` on the object's class. `hasDestructor_` is set
  at class completion after the implicit specials are declared (`ParserType.cpp:1551-1556`, `destructorOf(type) !=
  nullptr`), so a class whose destructor is implicit-but-non-trivial has the flag and does not take the no-op; the
  parser's `explicitDestructorCall` applies the same `dtor == nullptr` rule (`ParserExpr.cpp:2763-2777`). The operand
  is evaluated before the return (`:3540`). Right by reading.
- **A call's class result bound to a reference parameter** (`:3892-3896`): the result alloca's address; `classResult`
  requires a `Call` or operator record returning a class. Sound at -O0 where allocas live to the function's end.
- **Round 18's by-value class with a destructor** (`:3940-3975`): Itanium copies into `$copy` and guards it to the
  full expression's end; Microsoft hands the source address and leaves destruction to the callee's `guardParameter`
  (`:4000-4008`), the selector copying bytes into the argument slot - CLAUDE.md's measured split. The known Windows
  gap (an argument's constructor throwing after an earlier copy) is inherited, not new.

### 5. Three structural refusals sit inside M3b.2's own scope and are not in the record's "what is left" - should fix

The M3b.2 row names "`thread_local`, varargs, x87 on x86_64-linux" as M3b.2's. All three are refused by name today:
`outside("a variadic function")` (`Lowering.cpp:1428`), `outside("an x87 long double")` (`:1444`, and x87 members
and parameters at `:36, 61, 539, 584, 1545`), `outside("a thread_local")` (`:2324`), and the Driver refuses every
function with an `alignas` local before the lowering sees it (`Driver.cpp:1415-1427`). The round entries list what
each lane left but never these; the "largest refusals left" of round 18 names three of 14/11/10 bodies. **Fix**:
the next round entry names the remaining structural reasons with their body counts (the `-check=ssa` report has
them) so that "every source body lowered" is measured against a list, not a percentage.

### 6. The -O2 allocator finding: correctly not blocking M3b.2, wrongly absent from the plan, and not yet an oracle

**The claim**: under `-front=ssa -O2` `X64Parse.cpp` takes 335 s in `mir::Allocator::colour`, so the -O2 self-host
cannot run; mem2reg (M3b.4) is the cure. **In the plan**: nothing - no "335", no entry (checked here by grep); the
record's last line reads "Owed: -O2 (running)". **In the code**: `Allocator::run` (`MirAlloc.cpp:497-519`) demotes
**one** node per failed colouring and rebuilds everything - `findUses`, `solveLiveness`, `buildInterference` over an
n×n bit matrix, `colour` - per round; `pick` (`:390-392`) scans all n pseudos per node though `nodes_[p].adj` exists.
With the -O0 lowering handing every value slot and every scalar alloca to `Locals` as a promotable web, a large
function has thousands of long-lived webs and the loop is rounds × n².

**Judgement.** The plan placed this: M3-PLAN:120 says the -O2 self-host "can be tried early; whether it holds before
M3b.4 is recorded, not required", and changing the allocator inside a coverage step is out of order. I agree it does
not block M3b.2. Two things are owed now: (a) the finding written into the record with its number and the function
it was measured on; (b) the belief that mem2reg cures it given an oracle before M3b.4 is planned on it - the pseudo
count and the demotion count on that function, since mem2reg shortens the alloca webs but leaves one value per
instruction, and the one-demotion-per-rebuild loop is the allocator's own. If the count says the loop is the cause,
batch demotion is an M3a-area change gated by `identical.sh LEVELS=0` and the bench, small enough to take before
M3b.4 and worth it for the gate it restores. `tools/self-host` should also take a per-compile time cap so the gate
reports "did not finish" rather than running for hours on a box with a 25-minute rule.

### 7. Process: the record is honest and current to `e25a828`; round 19 is not gated as the record says - should fix

The round entries match the history: every round named has its merge commit in `git log`, the dates agree, and the
record owns its faults plainly (round 17's crash in stage 1, round 18's un-self-hosted landing, the walker leak sent
to `tests/open`, round 1's held-out lane). Two things to put right:
- `19226cb` says "From here the self-host comparison is part of every round's gate before it lands". Round 19's two
  lowering commits (`c06f55a`, `b3ec043`) name Linux ssa.sh, typed.sh, run.sh and a partial Windows row, and no
  self-host. The round is unlanded, so the rule is not yet broken; it is broken if the round lands without it.
- Each entry says "`make test` exit 0" for the merged tree, and `make test` runs `adapted.sh` still (`Makefile:157`);
  `c06f55a` edits `Adapter.cpp` to refuse the outer capture - correct under 12.1's "not grown", and worth a line.

### 8. Smaller notes

- `Typer::closureSources` (`Typing.cpp:658-674`) looks each capture's name up in the Typer's own scopes at the
  lambda and keeps the closure only where the slot matches the parser's - a lookup, guarded by an offset check. It
  is at typing time, not after the file, so 12.1 point 3 holds, but it is the one place the Typer resolves a name
  itself; a record of the capture's `var` on `LambdaFacts` would remove it.
- `Lowering.cpp:6` includes `../Ast.h` for `Program`'s `strings` and `globals` (`:2475, :3528`) - the program's data,
  which 3.8 allows; the include is wider than the use.
- `tests/open` holds 15 entries; the three M3b.2 added are the walker's faults the new lowering found
  (`handler-local-past-inner-try`, `second-base-member-unqualified`, `functional-cast-statement-of-call`) - the
  right place, and a small argument for the second lowering.

## Answers to the five questions

1. **Built as the plan says?** In shape, yes: the typed tree is the input, every refusal is `outside(...)` (473
   sites, measured here) after which the walker compiles the body, so a refusal is safe; pads follow M3-IR 3.6 on
   both ABIs; the Itanium table is disjoint and address-ordered (by per-invoke rows, finding 2); the Microsoft
   funclets, FH3 states and the entry's -2 go through the walker's own doors (`msAddState`, `storeUnwindHelp` at
   `selectBody`'s entry only, `SsaSelect.cpp:562-568`). The departure is finding 1: the lowering reads the old
   path's decisions and `Ast.h`'s `Function` far past what 3.8 states, and the plan does not say so.
2. **Gates as strong as claimed?** The self-host at -O0 and the Windows cases against `.expected` are real oracles;
   `ssa.sh` is coverage and well-formedness; `run.sh`'s count is two-thirds programs; `names.sh` under the flag is
   unrun; nothing at -O2 covers a whole program (finding 3).
3. **Miscompiles in rounds 17-19?** None found by reading; the shapes the cases lack are in finding 4.
4. **The allocator finding?** Correctly deferred as to the step order; wrongly unrecorded; its cure is a belief
   without a measurement (finding 6).
5. **Record honest?** Yes, and current to the last landed commit; round 19 must take the self-host it announced
   before landing (finding 7).

## Conditions, in order

1. Before round 19 lands: `tools/self-host selfhost -front=ssa` on Linux, 84 = 84 = 84, written into its entry.
2. Before RP4: M3-IR 3.8 amended to the hooks as built (finding 1's three lists); 3.6 amended to per-invoke rows.
3. Before RP4: `names.sh` under `-front=ssa` runnable and run (finding 3).
4. Next round entry: the structural refusals of finding 5 with counts; the -O2 finding of finding 6 with its number.
5. Before M3b.4 is planned on mem2reg: the pseudo and demotion counts on `X64Parse.cpp`'s worst function.

Fable 5.1, 2026-10-31. Kept as written; what is done about it goes in `doc-cxx3/M3-PLAN.md`.
