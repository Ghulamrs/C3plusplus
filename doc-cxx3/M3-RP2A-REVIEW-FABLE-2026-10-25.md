# M3 review point 2a, Fable 5.1, 2026-10-25: M3a.1, MIR as data at every level

Read-only review of branch `m3a` at `3b13e77` (`fd051f0` to `3b13e77`, forked from main at `eb930cc`),
against `M3-PLAN.md`'s M3a.1 row and `M3-IR.md` part 2, both of which I wrote at review point 1. Nothing in
the tree was changed by this review; every number below that is not marked "the plan's" was measured again
here, on this Mac, from the branch's own build (`make` was a no-op on the tip) - `identical.sh`, `make test`,
`-check=mir`, `CPP11_MIR_STRICT`, `-dump=mir` twice, `tools/self-host`, a TSan run, a compile-time round. The
Windows and Linux rows are the implementer's logs, read as reported; nothing here contradicts them. The
647 `tests/cases/*.s` my TSan run wrote beside their sources were removed; the worktree is as it was.

**Verdict: agree with conditions.** M3a.1 is done and may merge to main. `MInst` is fixed, as amended
in section 4, and M4 may be written against it. Six conditions, numbered in section 5; none blocks the
merge, two are owed in the merge commit itself, the rest before RP2b. The C7 item is a separate item, not a
blocker (section 6).

## 1. What was measured here

| check | result here | the plan's |
| --- | --- | --- |
| `tools/identical.sh ~/cxxopt-build/ref-m3a/cxx3.exe ./cxx3.exe` (REF built from `9c59840` = main + `55885d0`, `b9628c6`, `40b5bca`) | **6,039 compared, 9 differ**: `posix-headers` ×6 (x86_64-windows, both spellings, three levels), `windows-h-layout` ×3 (x86_64-linux). Each is one `#error` line naming `lib/` by its tree's path - REF's binary sits in `ref-m3a/` and its lib path is `ref-m3a-src/lib`, so `55885d0`'s fold does not reach it. No `.s` differs; none exists for those nine (both sides refuse) | 6,039 / 9, the known nine |
| `make test`, this tree | exit 0: run.sh 988/0 (646 recorded output, 340 recorded refusals), emit.sh 1930/0 with the golden (recorded at `7d593d5`) **6 of 1,924 changed** - `namespace-unnamed` on three targets, `thread-local`, `thread-local-extern` and its part on x86_64-windows - names.sh 655/0, overload.sh 44/0, syntax 3/0, typed.sh 648/0 (8,230 = 7,423 + 807, 0 differ), adapted.sh 648/0, exclusions 126 sites 0/0, comment cap 0, memory-gate held at 1,063 | the same six |
| `-check=mir` at -O0, every runnable case and the 16 Compiler++ units, x86_64-linux and both Windows spellings | **1,973 units, 0 differ** | 1,973 / 0 |
| the same under `-g` (not in the plan) | **1,972 units, 0 differ**; the 1,973rd, `anonymous-union-namespace -masm=masm -g`, is refused identically past and through the holder ("`type` is a name ml64 reserves") - a MASM naming refusal older than M3a.1 | - |
| `CPP11_MIR_STRICT=1`, the same sources and targets, -O0 and -O2 | **0 mnemonics the table lacks** | 0 |
| `-dump=mir` run twice, every runnable case, x86_64-linux -O0/-O2 and x86_64-windows masm -O2 | **1,965 compared, 0 differ; 25,183 function dumps** | 1,973 / 0 |
| `tools/self-host` on this Mac | stage 1 built, stage 2 built, **71 of 71** the same | 71 of 71 |
| stage 0 against stage 1 (not in the plan: `./cxx3.exe -S` of the 71 sources, clock pinned, against `s1/`) | **71 same, 0 differ** | - |
| `make tsan` + the corpus through `obj-tsan/cxx3.exe -j 4 -S`, x86_64-linux and both Windows spellings, -O0 and -O2 (3,772 compiles) | **0 reports** | not in M3a.1's gate |
| compile time, the 71 sources one at a time with `-S`, one interleaved round under load | -O0 REF 25.01 s / tip 24.93 s; -O2 45.84 / 45.76 | medians -O0 24.12 / 24.12, -O2 45.16 / 45.32 |
| memory-gate, comment cap, exclusions | `new 1063`, 0 over the cap, 126 sites 0/0 | the same |

So the gate's numbers are the gate's, re-measured and matched where the Mac can measure them; the Windows
rows (958/0 both spellings at -O0 and -O2; self-host 71/71 in the default spelling; the GNU row by another
session) and the Linux rows are taken from the plan's record.

## 2. The code, read

**The holder at every level (fd051f0).** `X86_64Linux::setOptimize` makes the `Optimizer` at -O0 too
(`X86_64Linux.cpp:1561`, `-mir=off` the one bypass, at -O0 only); `Optimizer::improve` returns before the
manager when `passes_` is false (`Optimizer.cpp:207`), so -O0 is hold-and-replay. The trap the plan named
is honoured: `passesRun()` (`X86_64Linux.h:167`) is `holding() && runsPasses()`, and every walker decision
that used to ask `optimizing()` asks it - `tempsInSlots`, `copyBlock`, `callArguments`, `jumpOnly`,
`exceptionRegion`, `frame`, `temporaries`, `storeUnwindHelp`, `beginFunclet`/`closeFunclet` on both
code generators, `returnsPair`, the realigned-frame bypass; `holding()` is asked by `atOutput` and
`emittedSize` alone, as 2.7 point 5 says. No `optimizing()` remains. The realigned frame goes through the
holder at -O0 with `realign` on its prologue directive, and the assert is `realign == 0 || !passes_`.

One thing I looked for and found sound: `emittedSize()` is `out_.size() + held()` and feeds
`Walker::markLine`/`closeBlock`'s "was code emitted since the mark" test (`Walker.cpp:58-69`), so with
the holder in front at -O0 the measure changes from bytes to entries. Both answer "at least one
instruction"; `-g -check=mir` over every unit (above) is the measurement that they agree.

**Directives (4bb6f71).** `Directive{kind, n[4], s, withSaves, saves, deferred}` with `replay` a
`switch` making the same `Spelling` call (`OptIr.cpp`), `describe` for the dump, 26 kinds against 26
names (checked). `FinishFrame` edits the prologue directive's fields in place and asserts its kind
(`OptPipeline.cpp:155-165`); `alignLoops` makes a `LoopAlign` directive where it made a closure. Five
`Deferred` closures remain, as 2.4 said. Of `Spelling`'s virtuals the holder does not override -
`calleeSaves`, `sym`, `op` - none is called by a walker: `calleeSaves` only by the replay on `under_`,
the other two by the spellings themselves. The relro forwarding `b9628c6` added is the one that was
missing, and `-check=mir` is what found it.

**`OpId` and `StrId` (857e6d9, 80ca39f, 5252c0a).** Both registries are the shape 2.6 now describes:
written under a mutex, published by address with a release store after the record is complete, read
with an acquire load and no lock; the records live in `std::deque`s so no address moves (R6.6: the
count stays 1,063); a per-thread `unordered_map` short-circuits the lock on a repeated spelling. The
table's 165 rows are seeded under the lock before any unlisted mnemonic; `at()` seeds if asked first.
`StrId` refuses past 4,096 × 4,096 texts with a `length_error` (`OptStrings.cpp:32`); **`OpId`'s
`publish` has no such check** (`OptTable.cpp:218-219`, `kMaxMnemonics = 1 << 16`) - a write past
`gSlots` is unreachable from any walker, which writes a bounded vocabulary, but the two registries
should fail the same way (condition 1). TSan over 3,772 `-j 4` compiles reports nothing.

The new table rows carry exactly the kind and flags their prefix family gave (jcc `{Jcc,0,0}`, setcc
`E|W`, cvt `M|kCvt`, x87 `N`), so nothing a pass sees changed - which `identical.sh` says at every level.
The passes still compare `i.m == "jmp"` through `OpId::operator==(const char *)` - a string compare per
test, as before - and M3a.3 is where that goes; the compile-time row says it costs nothing measurable.

**`-dump=mir`, `-check=mir`, `-mir=off` (270067f).** `-check=mir` generates the unit twice at -O0 - past
the holder and through it - and compares the texts (`Driver.cpp:1406-1431`), naming the first differing
line and the function in or after which it falls; `-dump=mir[=pass]` goes through `Optimizer::dumpTo`
and the manager's `dumpAfter`. The dump reads as 2.10 wants (a sample: `.prologue frame=32 outgoing=0
realign=0`, dead entries `-`, `.loop-align 15`). `-mir=off` above -O0 is accepted and ignored without a
word (`X86_64Linux.cpp:1561`): a nit, condition 3's doc line covers it.

**The two things self-host found.** `40b5bca` is right on both halves and clang's names are what it
measured; the case is two units, which is the only shape that could have shown the link failure.
`tests/open/conversion-to-reference-binds.cpp` refuses here as recorded (`len(b)` with `operator const
std::string &()`), and clang accepts it under `-std=c++11 -pedantic-errors`: correctly registered.

**What looks wrong and is load-bearing, for the next reader**: `improve` clears `unsignedSpelling` on
every operand before the passes run (`Optimizer.cpp:210-211`) - that is what keeps -O1/-O2 byte-identical
to what they wrote before, and it goes at M3a.3 (departure d); the MASM code generator splits a funclet
out only where passes run, so at -O0 the funclet's entries sit in the function's stream and the five
`Deferred` text-lifting closures run in order at replay - the 958/0 Windows cases at -O0 and
`-check=mir` are what say that order is right; `held()` only grows and must (the `markLine` arithmetic).

## 3. The departures from part 2, ruled on

| | departure | ruling |
| --- | --- | --- |
| (a) | the types keep `opt::Instr`, `Operand`, `Entry`, `Directive` in `OptIr.h` until M3a.5 | **accepted.** My own documents disagreed: 2.1's file table says the content "moves to `Mir.h` ... retired at M3a.5", the M3a.1 row said "in `src/optimizer/Mir.h`". The table is the more specific statement and the implementer followed it. What RP2a fixes is the *shape* (section 4); the names change at M3a.5 by a rename M4 follows |
| (b) | `StrId` is one process-wide registry, not a per-function `StringTable` | **accepted, with 2.5 amended.** 2.5's reason was that a function's MIR be self-contained so a dump is complete; the dump prints the text and is complete, and two runs give the same bytes (1,965 / 0). What an index is worth is nothing outside the process - it never reaches a dump, an output or a symbol - and the registry grows only with distinct texts. The per-thread caches never shrink; the compile-time row says that costs nothing measurable at this size, and M3a.5 may revisit if a unit of Compiler++'s size ever says otherwise |
| (c) | `Directive` has a `RelroSection` kind 2.4's table lacks | **accepted; an omission of mine.** `Spelling::relroSection` exists and a vtable goes there on ELF since the PIE round. `hasRelro` is a query the holder forwards, not a directive, and is right where it is |
| (d) | an immediate keeps `unsignedSpelling` | **accepted for M3a.1, to be settled at M3a.3.** The walker writes `imm(unsigned long long)` past `LLONG_MAX`; at -O0 the holder must spell it back as written (byte identity), and -O1/-O2 have always spelled it signed (the `Operand` had no such field), so the field is cleared before the passes to keep those levels identical too. M3a.3 either folds the spelling into the table's widths or makes -O1/-O2 keep the walker's - a commit whose files `identical.sh` names |

Two departures not declared, found reading: `Directive::s` is a `std::string` where 2.7 point 3 said a
string argument becomes a `StringId` - right as it is (an LSDA name or a `DataBytes` payload gains
nothing from interning), amend the text; and `MEntry::Kind` has no `Deferred` member - `Deferred` is a
`Directive` kind - which is the better shape, amend the text.

## 4. `MInst` fixed - what M4 may rely on

**Fixed at RP2a, in substance** (the names until M3a.5 in parentheses):

- `MInst` (`opt::Instr`, `OptIr.h:69-82`): `OpId m; Operand a, b; int operands; RegSet args; bool exactArgs`.
  The width, kind, flags, roles and implicit registers are the row's.
- `MOperand` (`opt::Operand`, `OptIr.h:38-67`): `Kind {None, Register, Immediate, Memory, RipSymbol,
  Indirect, Label, SegMemory, TlsSymbol}`, `Reg reg, index`, `scale`, `disp`, `hasDisp`, `value`,
  `numeric`, `StrId text`, and until M3a.3 `unsignedSpelling`. `from(const Op &)` and `op()` are the two
  doors and are byte-for-byte the old ones.
- `OpId` (`OptTable.h`): an index into one registry whose first 166 entries are the empty mnemonic and the
  table's 165 rows in order; `row()`, `name()`, `listed()`. M4's mnemonic list is the 165 listed rows -
  `CPP11_MIR_STRICT=1` says the corpus and Compiler++ write nothing outside them at -O0 or -O2 on either
  x86 target - and the columns M4 will want beyond `kind/flags/width` (`masm`, `roles`, `implicit`,
  `classes`, `fixed`) arrive at M3a.2/M3a.3 as 2.6's table says.
- `MEntry` (`opt::Entry` = `EntryOf<Instr, Directive>`, `OptCore.h:56-65`): `Kind {Ins, Label, Event}`,
  `label`, `state`, `dead`; the directive kinds of `OptIr.h:86-93`, 26 of them, `Deferred` among them.

**Amendments to `M3-IR.md` part 2** (condition 3; the implementer applies them in the merge commit or the
next docs commit - I do not edit the file in a review):

1. 2.2: `StringId` → `StrId`, "into the function's string table" → "into the process's registry
   (`OptStrings.h`), written under a lock, read lock-free; an index never reaches a dump or an output".
2. 2.3: "A mnemonic the table does not hold cannot be built" → "is interned as an unlisted `OpId` whose row
   is its prefix family's or `kUnknown`, and `CPP11_MIR_STRICT=1` names it".
3. 2.4: add `RelroSection`; `Deferred` is a `Directive` kind, not an `MEntry` kind; the string argument is
   a `std::string`.
4. 2.5: strike `StringTable strings` and its sentence; keep `pseudos` (M3a.2) and `funclets` (M3a.4).
5. 2.7 point 3: "a `std::string` argument is copied whole; an operand's text is a `StrId`".
6. 2.2/2.7: note `unsignedSpelling` and that M3a.3 removes it.
7. M3-PLAN's M3a.1 row: a dated note in "Where it stands" that the types keep their names until M3a.5 per
   2.1's table (the row above the line is not edited, by the plan's own rule).

## 5. Conditions

| # | condition | where | blocks |
| --- | --- | --- | --- |
| 1 | `OpId`'s `publish` refuses past `kMaxMnemonics` as `StrId`'s `internLocked` refuses past its chunks - a `length_error` naming the registry, not a write past `gSlots` | `src/optimizer/OptTable.cpp:218-219` | the merge commit (one line) |
| 2 | `tools/determinism.sh` runs `-dump=mir` for the two x86 targets at -O0 and -O2 and counts its outputs - the gate row said "included once they exist", and today the two-run check is a hand run (the implementer's and mine) | `tools/determinism.sh` | RP2b |
| 3 | `M3-IR.md` part 2 amended as section 4 lists, and the M3a.1 row corrected by a dated note; `-mir=off`'s usage line says "at -O0" | `doc-cxx3/M3-IR.md`, `doc-cxx3/M3-PLAN.md`, `src/Driver.cpp` usage | the merge commit |
| 4 | the merge takes main's `98eeda3` (`MasmSpelling::ml64Tls`, which `m3a` forked before) and the Windows cases are run once under `-masm=ml64` after it on zeeshan - the holder now sits in front of that spelling at every level, and `ml64Tls` writes text from inside `ins`, which is the spelling's own and should be unaffected; "should" is measured there, not here | the merge; `tools/windows/run-cases.cmd` | before "merged" is written in the plan |
| 5 | `identical.sh` reads 0 as `55885d0` says it does, or the plan says why it does not: REF's binary beside its own tree (`ref-m3a/` holding `ref-m3a-src`'s lib path), or the fold taught the compiled-in lib path. Nine known differences that must be recognised by eye on every run are the shape rule 5 warns about | `tools/identical.sh` or `~/cxxopt-build` | RP2b |
| 6 | `make tsan` + `tools/tsan-check` named in RP2b's gate list and run on the Linux leg, since the registries are concurrent code the M3a.1 gate did not put under a sanitizer; the Mac run above (0 reports) stands in for M3a.1 | `doc-cxx3/M3-PLAN.md` gate table; the Linux leg | RP2b |

## 6. The C7 open item: separate, not a blocker

The note was "stage 1 writes its `-S` output beside the sources, while stage 0 writes into the current
directory". Read against the source and measured here:

- `Driver::assemblyNameFor` (`Driver.cpp:994-1000`) puts a `-S` output with no `-o` **beside its
  source**; `Driver::objectNameFor` (`:1002-1008`) strips the directory, so an object - and the
  intermediates of a build that links - go to the **current directory**. The same binary does both; the
  difference is between the two modes, not between two stages. My own TSan run showed the first: `cxx3 -j 4
  -S <646 sources>` with no `-o` left 647 `.s` beside the cases.
- `tools/self-host` and `self-host.cmd` give every `-S` an `-o`, so neither stage writes beside anything
  there; the link builds write their intermediates in the current directory for both stages.
- Stage 0 and stage 1 agree on all 71 sources' `-S` output on this Mac (section 1), which is the question
  the note would raise if the two stages did behave differently - a stage-0-against-stage-1 difference is
  exactly what the stage-1-against-stage-2 fixed point cannot see.

So my reading is that the observation was of the two modes and not of the two stages, and the Windows
probe should say the same. It is **not a blocker for RP2a**: nothing in M3a.1 touches the driver's naming,
and the round trip is measured by `-check=mir` and `identical.sh` with `-o` given. It is a separate item
with two parts: the probe's result recorded in "Where it stands", and `tools/self-host` growing a stage-0
column (`s0/` against `s1/`, as I did by hand) so that the stronger claim is the tool's and not a reviewer's.

## 7. Found on the way, not M3a.1's, recorded here

- `anonymous-union-namespace` under `-g -masm=masm` is refused - "`type` is a name ml64 reserves and this
  file emits" - with and without the holder. A MASM/CodeView naming matter older than this branch; not in
  `tests/cases/*.nomasm`, so the Windows runner's `-g` pass, if it ever has one, will meet it.
- `-mir=off` above -O0 is silently a no-op (condition 3's usage line).
- The branch lacks main's `98eeda3` and its CLAUDE.md paragraph; `git diff main..m3a` shows them as
  removals, which they are not (condition 4).
- The nine `identical.sh` differences are the REF's lib path, and `55885d0`'s claim that they read 0 holds
  only for a REF whose binary sits in its own tree (condition 5).

## 8. What is right about M3a.1, said once

The four steps were each gated at 0 assembly differences before the next began, and the record says so
commit by commit; the -O0 holder found a real -O1/-O2 fault (`b9628c6`, a vtable in `.rodata` on a PIE
target) on its first round trip, which is the argument for the round trip in one line; the strict check
turned the table's completeness from a claim into a count, and the count is what M4's list is; the
self-host found a linkage fault in cxx3 that a one-unit suite could not, and registered the other gap
with clang's answer; and every number the plan wrote that this Mac can measure came out the same here.

Verdict: **agree with conditions** - M3a.1 may merge; `MInst` is fixed as amended; M4 may start; M3a.2 and
M3a.3 may begin, as the plan's handback allowed, with conditions 1 and 3 in the merge commit and 2, 4, 5
and 6 before RP2b.
