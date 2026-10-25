# M2 closing review, Fable 5.1, 2026-10-25

Read-only review of M2 at `fbf61d3` (main): `2e79a0d` (M2.0) to `259cdc6` (M2.18) and the merge
`f755415`, against design section 12.1's six points and the requirements (R9.1, R9.5, R9.6). Nothing
in the tree was changed; every number below that is not marked "the plan's" was measured again here,
on this Mac from a clean build of `fbf61d3` (`make clean; make`), on the Windows box from the outputs
the M2.18 run left there, and on the Linux box from a fresh g++ build. Kept as written; what is done
about it goes in M2-PLAN.md.

**Verdict: agree with conditions.** M2 is closed. Six conditions are named below; none blocks M3a
(P1's MIR step), two are due before M3b lowers the typed tree, and all six are small enough to be
M3's first commits.

**M3: Claude Opus 5.5 implements it, under a plan and design I write and gates I review.** The
reasons and the review points are at the end.

## What was measured here

| check | result here | the plan's |
| --- | --- | --- |
| `make test`, clean build of `fbf61d3` | exit 0: run.sh 987/0 (645 recorded output, 340 recorded refusals), emit.sh 1924/0, names.sh 653/0, overload.sh 44/0, syntax.sh 3/0, typed.sh 647/0, adapted.sh 647/0, exclusions 126 sites 0/0, comment cap 0, memory gate held | the same |
| `-check=typed` over the 647 host cases | 8,227 functions, 7,420 with a body, 807 compiler-written; 184,964 nodes, 128,385 typed, 112,982 agree, **0 differ**, 15,212 with no Ast.h node, 157 cv, 34 lowered, 1,027 `recorded`; 656 untyped: call 228, member 191, name 123, unary 58, assign 27, binary 24, cast 4, comma 1 | identical, field for field |
| compiler-written by kind | implicit destructor 209, implicit default constructor 180, deleting destructor 144, implicit copy or move 97, complete constructor 33, complete destructor 23, virtual thunk 29, thunk 20, vector destructor 25, vector constructor 13, array destructor helper 8, init function 25, tls init 1 | identical |
| functions = bodies + compiler-written, per case | 0 cases where it does not; `CXX1_CENSUS=1` over every case: no body symbol counted twice | "0 neither" |
| the same census for x86_64-windows (`-arch x86_64-windows -masm=masm -check=adapted`, 630 cases) | 8,709 = 7,712 + 997; adapted 1,122, all the same as the parser's | not in the plan; the adapter is exercised on the primary target |
| `-check=adapted` | 1,268 adapted of 7,420, 1,268 the same, 6,152 outside; every case byte-identical with and without `-front=typed` at -O0 and -O2 | identical |
| `tools/identical.sh` a2b39e7's binary against `fbf61d3`'s | 5,352 compared, 26 differ: the 8 known (two `#error` cases naming the lib path, which differs between two trees by construction) and 18 from `247579c` (three local-class cases on x86_64-windows, both spellings, three levels - the Microsoft back-reference fix, outside M2 and its own commit). **M2's own commits: 0** | "5,352 compared, the known 8 differ" |
| emit golden on this Mac (recorded at `7342ead`) | 624 of 1,924 changed: 620 are x86_64-windows, the default spelling flipped by `125f31a`; the other 4 are `pipelined-pointers` and `pointer-strides`, whose sources `125f31a` edited (`db` is a MASM directive). None is M2's | "0 of 1,924 at a2b39e7" - true of M2's commits, by identical.sh above |
| `tools/self-host` with `-front=typed`, Mac | stage 1 built, stage 2 built, 70 of 70 the same | not run on the Mac by the plan |
| Windows, `-front=typed` and `-front=typed -O2`, MASM spelling, project's masm and LINK (the box's `m2w-t0.tgz`, `m2w-t2.tgz`, compared here with `tools/windows/compare-cases.py`) | 615 same, 0 differ; 340 refused as recorded, 0 not - 955/0 at both levels | 955/0/33 |
| Windows self-host, plain and `-front=typed` (the box's logs and scripts) | 70 of 70 each. The run that produced the logs is `m2w-selfhost-gnu.cmd` (`-masm=gnu`, 03:07); the earlier `m2w-selfhost.cmd` with `-masm=masm` (02:48) did not produce them. The s1 assembly is the GNU spelling (`.section .tls$`, `@SECREL32`) | "assembled by clang (the GNU spelling)" - consistent |
| Linux, g++ 11.5, `fbf61d3` built fresh under `-Werror` (`./build -j1`) | run.sh 989/0 under `-front=typed`, 989/0 under `-front=typed -O2`, 989/0 plain (645 recorded output, 340 recorded refusals each) | run.sh 989/0 plain and typed, both levels |
| tms6747 (vm6747, sim6747, lnk6x 7.4.4) | **not re-measured** - the C6000 does not run on the Mac and the Linux box was taken by the build above | 620/0, 629/0, 633/0 |

## The six points of 12.1

**1. Every emitted function counted - holds.** `Typer::census` (`src/sema/Typing.cpp:335-358`) takes
`program.functions.size()` as the denominator, every live `function` node that defined a symbol the
program still holds as a body, and `Function::synthesised()` by kind; `setSynthesised` is called at
15 sites. The arithmetic closes to zero on every one of 647 host cases and 630 Windows-target cases,
and no body symbol is counted twice. One thing to know: a function read and then pruned (an unused
inline) is dropped from both sides, which is right - the backends do not emit it.

**2. `-check=typed` at 0 differ, the fraction and the kinds named - holds, with three things the
report does not say.** 0 differ, 128,385 of 129,041 expression nodes typed (99.5%), the 656 named by
kind. The three:
- *What `agree` measures.* `Typer::resolvedType` (`Typing.cpp:125-161`) takes a node's type from the
  parser's record for a Local, Global, StaticMember, Enumerator, This, Member and Temporary, and a
  Call's from the recorded signature's return. For a Member the record's type *is* the Ast node's type
  (`resolvedAs(Resolved::Member, accessType, ...)`, `ParserExpr.cpp:2097`), so the agreement there is
  the record read back against the node it was copied from - consistent, not independent. The report
  counts only `Value` records as `recorded=` (`Typing.cpp:168`): 1,027. At M2.15, before any record,
  the Typer's own rules agreed on 83,323 nodes; the records took `agree` to 112,982. So about 30,000
  of the 113,000 agreements, a quarter, are the record and not a rule, and the plan's "1,027 from the
  parser's own record" understates it. This is the right design for M3 - the record is what M3 reads,
  and its consistency with Ast.h is what matters - but rule 5 asks the number to say what it measures.
- *15,212 typed nodes have no Ast.h node* (11.8% of the typed): a callee `name`, a `member` under a
  call, the parts the parser lowers away. They are typed and unchecked, and the report counts them.
- *The reasons for the 656 are mis-described.* M2-PLAN's M2.17 paragraph names "a call of a class
  temporary, `ByDigits()(a, b)`, and of an object whose `operator()` the parser reached through a shape
  with no record". Classifying every untyped `call` by its source line: **141 are member calls through
  a pointer or reference - virtual dispatch** (`pd->f()`, `e.what()`, `f.make(v)`), **54 are calls
  through a pointer to member** (`(m.*p)()`), 5 are temporaries or functors, 28 other. The 191 untyped
  `member` nodes are the callees of the first kind. The mechanism is the one the plan names - the
  parser wraps the Call in a `Comma(keepAddress, call)` and the record stays on the inner Call
  (`ParserExprCall.cpp:898-901`) - but the examples given are the rare case, and the common one is
  ordinary C++.

**3. Resolutions recorded per node at parse time; no lookup after the file - holds, except for the
calls above.** `Parser::Resolved` (`Parser.h`, 13 kinds) is appended at 17 `resolve(` sites as the
parser resolves, an index on the `Expr` copied to the syntax node by `note`. `Typing.cpp` reads
`p_.resolutions_`, `declaredTypes_`, `castTypes_`, `literalTokens_` and calls no lookup. `Adapter.cpp`
reads the record at 13 sites, `scopedName` and its tables are gone; what remains is `findMemberOwner`
and `findMember` on an object's class (`Adapter.cpp:1183-1185`), `findTypedef(tag)` (`:1475`) and
`signatureBySymbol` (`:1245-1250`) - by tag and by symbol, which do not depend on the scope the file
was in. **What is not recorded on its node**: a virtual call or a member-pointer call wrapped in the
parser's Comma - about 195 call nodes of the suite, 0.15% of expressions, and the 191 callees under
them. For M3b that is a resolution missing where [class.virtual] dispatch has to be lowered.

**4. Pending facts in `FunctionState` - holds.** `lastCleanupSlots`, `pendingReturnTemp`,
`pendingTry`, `pendingThrow`, `pendingRange`, `pendingLambda` are members of `Parser::FunctionState`
(`Parser.h:1020` on), and each of `captureFunctionState`, `restoreFunctionState` and
`clearFunctionState` (`Parser.cpp:438, 525, 613`) names all six.

**5. `-dump=ast` prints every field of a `Try` - does not hold literally.** `AstDump.cpp:149-163`
prints `pointerSlot`, `selectorSlot`, `alsoCleanup`, `unwindHelpSlot`, the types with their indices,
`msExits`, the body and the pad - and `handlers().size()` only. Each `MsHandler`'s `descriptor`,
`objectSlot`, `objectSize`, `byReference`, `constPointer` and `body` (`Ast.h`, `struct MsHandler`) are
not printed, nor is `cleanup_` (`Ast.h:635`, set at `ParserClass.cpp:502` and `:579` by the
implicit specials' guards). Two bodies differing in a handler's slot or a guard's cleanup compare the
same under `-check=adapted`. The exposure today is nil - handlers exist on x86_64-windows alone, where
the adapter adapts no `try`, and `cleanup_` belongs to compiler-written functions the adapter never
produces - but the point was written as "every field" and I wrote it.

**6. `-front=typed` in `tools/self-host` and a non-Mac leg; the adapter reads records - holds.**
`tools/self-host` takes the flag for every compile of both stages and of the comparison (`:14, :21,
:36`); `tools/verify-three`'s Linux leg runs run.sh under `-front=typed` at -O0 and -O2 and the
self-host with and without it (`:130-132`). Measured here: Mac self-host typed 70/70; the Windows
outputs 955/0 at both levels and self-host 70/70 both ways, from the box's own files. **The Windows
leg of verify-three does not run `-front=typed`**: `tools/windows/run-cases.cmd` has no such pass, and
the M2.18 Windows numbers came from hand scripts on the box (`m2w-t0.cmd`, `m2w-t2.cmd`, with
`CXX1_CASES_FLAGS`). They are right; they are not repeatable by the gate.

## Requirements

- **R9.1 (self-host on target 1, first gate, with the milestone's own front end).** Holds in the
  GNU spelling: 70 of 70 on Windows plain and `-front=typed`. It does not hold in the target's now
  default toolchain, `-masm=masm` with the project's LINK (R7.1 as amended 2026-10-25): cxx3's own
  `thread_local` (`Driver.cpp:311`) needs SECTIONREL, which the project's masm refuses by name
  (`Masm.cpp:258-264, 714-717`), and ml64 has no COMDAT. The plan says so. The design's gate text
  (section 10, 12.1) should say it too, until the project's masm gains SECTIONREL or M4's COFF writer
  lands; otherwise the first gate passes with a non-default assembler and nothing records that it does.
- **R9.5 (0 warnings).** Holds by construction: the Mac and Linux builds under `-Werror`, cl under
  `/W4 /WX` (`msvc/build.cmd:46`).
- **R9.6 (clang and g++ at `-std=c++11 -pedantic-errors` set the C++11 bound).** Not M2's subject;
  nothing in M2 changed a conformance decision. The one language gap M2 found in cxx3 - a member's own
  initialiser ended at a depth-0 comma inside a template-id - is in `tests/open/` with clang's answer,
  as R3.3 wants.
- **R6.6** held by `tools/memory-gate --check` in `make test`.

## The commits outside M2 on the same day

`125f31a` (`-masm=masm` the Windows default, the project's LINK), `39fa5be` (`-g -masm=gnu` links with
Microsoft's link), `247579c` (a local class's Microsoft back-references; one line in
`ParserTopLevel.cpp`, `microsoftForgetLocalOwners()`), `80685e6` (the TI link judged on the Linux box)
and `fbf61d3` (two `.nocl`). None touches the syntax tree, the Typer, the adapter, the record or the
census; identical.sh shows `247579c`'s 18 Windows differences and nothing else, and the 624 golden
changes are `125f31a`'s. **Two consequences for M3's gates**: the Mac's emit golden must be
re-recorded on main before the first M3 commit is judged by it, and the Windows cases now run in the
MASM spelling by default - which is what the M2.18 Windows numbers already used.

## Conditions

| # | condition | where | blocks |
| --- | --- | --- | --- |
| C1 | `-dump=ast` prints each `MsHandler`'s fields and a `Try`'s `cleanup_` | `src/AstDump.cpp:149-163` | nothing; first M3 commits |
| C2 | the record of a call the parser wraps in a Comma - virtual dispatch with a computed address, a call through a pointer to member - copied to the wrapping node, so `-check=typed` types them and M3b finds the dispatched signature; M2-PLAN's M2.17 reasons corrected to the counts above (141 virtual, 54 member-pointer, 5 temporary, 28 other) | `src/parser/ParserExprCall.cpp:898-901` and the member-pointer call site; `doc-cxx3/M2-PLAN.md` | **before M3b** lowers a function holding one |
| C3 | the `-check=typed` report counts a type taken from any record apart from one the Typer's rule gave (`recorded=` covers `Value` alone), and M2-PLAN says so | `src/sema/Typing.cpp:125-170, 434` | nothing; alongside |
| C4 | verify-three's Windows leg runs the cases under `-front=typed` (a `CXX1_CASES_FLAGS` pass in `run-cases.cmd`), so point 6's primary-target evidence is the gate's and not a hand script's | `tools/windows/run-cases.cmd`, `tools/verify-three` | nothing; alongside |
| C5 | the emit golden re-recorded on main (`tests/emit.sh --record`), 624 of 1,924 being `125f31a`'s | the Mac | the first M3 gate that reads it |
| C6 | the design's R9.1 gate text names the spelling the target-1 self-host runs in (GNU, by clang) until the project's masm has SECTIONREL or M4's COFF writer lands | `doc-cxx3/Compiler_Design_Final.md` sections 10 and 12.1 | nothing; alongside |

None blocks M3a. C2 is the one with content, and it is one line at the Comma plus a corrected paragraph.

## What is right about M2, said once

Step 1 moved 8,377 lines into six `sema` modules with 5,352 outputs byte-identical at every step; the
state moves followed; the syntax tree, the typed tree and the record are in `src/syntax/` and
`src/sema/Typing.*` at 255 and 524 lines, which is what a thin layer should weigh; the adapter was
stopped where 12.1 said and is exercised on three targets; the census found three recording faults and
fixed them, and the self-host found a gap in cxx3 and registered it. Every number the plan wrote was
re-measured here and matched, on the Mac, on the Windows box's own outputs, and on Linux from a fresh
g++ build; only the tms6747 leg was taken on the plan's word.

## Who takes M3

**Claude Opus 5.5 implements M3. Fable 5.1 writes the M3 plan and the IR definition, settles P1, and
reviews each gate.** The reasons:

1. *The writer and the judge are two.* The tree's own rule from the 1.9 round, and this review's
   evidence for it: the M2.17 paragraph naming the untyped shapes was mine, and it describes 5 of 228.
   M3's gates - `stage 2 = stage 3 at -O2`, the benchmarks, "byte-identical through MIR" - are the kind
   a writer reads as green. A reviewer who did not write the code is what catches them.
2. *Session length.* M2.13-M2.17 ran my session out before M2.18; M3 is the largest milestone in the
   table (SSA IR, the optimizer, MIR, a graph-colouring allocator, two targets, deletion of the adapter
   and the `Ast.h` path, the -O2 self-host). Opus finished M2.18 - the four-box measurement, the gap
   found and registered, the plan's table - and every number of it checked here.
3. *The design side is where my hand belongs.* Open item 2 puts P1 under my review; the IR's
   definition (`MInst`, the SSA form with block arguments, the funclet decision) has to be fixed before
   M4 can start in parallel; these are design decisions and reviews, not implementation.

**P1, decided here as its reviewer** (open item 2; the user may reverse it on return, and until then
this stands): M3 runs in P1's order - **M3a** MIR below the existing walkers, every function
round-tripping byte for byte against the oracle, allocation moved onto MIR and the x86 text optimizer
retired against identical.sh and the benchmarks; **M3b** the SSA IR above, the typed tree lowered to
it, the walkers turned into selectors from the IR into MIR, and the adapter and the parser-to-`Ast.h`
path deleted together at the end. The reason is the one P1 gives and M2 confirmed: the allocator - the
riskiest part - lands on input whose correct output is known, and M3a touches none of M2's
deliverables, so nothing is lost if the user changes the order before M3b.

**Review points**, each a stop until I have read it:
1. The M3 plan (`doc-cxx3/M3-PLAN.md`) and the IR definition - `MInst`, the SSA IR's instruction set,
   block arguments, `invoke` and the funclet pads, the opcode table carrying the allocator constraints
   - before any code lands. C1-C6 are its first commits.
2. M3a's gate: MIR round trip byte-identical over the 5,352 outputs; then the text optimizer's
   retirement, identical.sh and `tools/windows/bench-kernels.cpp` on the box.
3. M3b's first lowering: the typed tree to SSA for the adapter's subset, the `Ast.h` path's output as
   the oracle, and the record moved onto the typed tree (today it lives in `Parser::resolutions_` and
   four sibling maps, which M3's lowering would otherwise read off the parser).
4. The compiler-written functions from the typed tree - the first item 12.1 names for M3.
5. The deletion commit: the adapter, `-front=typed`, `-check=adapted`, the parser-to-`Ast.h` path.
6. M3's close: `-O2` self-host, stage 2 = stage 3, every box, R9.1 in whatever spelling C6 names.

Verdict: **agree with conditions** - M2 is closed; C1-C6 ride alongside, C2 due before M3b.
M3: **Claude Opus 5.5 implements, Fable 5.1 designs and reviews at the six points above; M3a may start.**
