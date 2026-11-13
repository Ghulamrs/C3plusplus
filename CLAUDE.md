# CLAUDE.md

Guidance for Claude Code in this repository. **This file is the short guide; the full record is
`docs/CLAUDE-history.md`** - every section this file held until 2026-11-10, unchanged, in the order
written. Read a history section when a task touches its subject: the index at the end names each one with
its line. **New findings go in the history file under a dated `## ` heading; this guide changes only when a
rule or a decision changes.**

## Two languages, and they are not the same one

**`src/` is ISO C++11; the language `cxx1` compiles is C++03 with C++11 extensions.** What it does not take
is `docs/EXCLUSIONS.md`; the library is the subset in `include/`. Keep the two apart in every sentence -
most confusing questions here come from conflating them. The headline stays "C++03 with C++11 extensions"
until `tools/feature-sweep` and the review's Rec 1 say otherwise (history: "The headline, re-measured").

`-std=c++11 -Wall -Wextra -Werror -pedantic`. A Mac cannot enforce C++11 (Apple's libc++ leaks later names),
so a clean Mac build proves nothing: g++ on the Linux box and cl on the Windows box decide. Write `'?\?'`,
never `'??'`, in a diagnostic - trigraphs are live.

**The executable is `cxx3.exe`** (`cxx3-msvc.exe` for stage 0 on Windows); messages still say `cpp11:`,
variables `CPP11_*`, the seal is `cxx1-N.dat`. **Self-hosting is the goal**: the design is
`doc-cxx3/Compiler_Design_Final.md` (milestones M0-M8); the milestone in hand and its rounds are
`doc-cxx3/M3-PLAN.md` - its table near the top and its dated entries at the end are the project's status.

## Reading this tree

`README.md`; this guide; then the pipeline by file: `main.cpp` -> `Driver.h`/`Driver::compile` -> `Lexer.h`
-> `Preprocessor.h` -> `parser/Parser.h` (the front end's design document) -> `Ast.h` -> `Type.h` (`class
Target` is the machine) -> `Abi.h`, `backend/Backend.h` -> a code generator. M2/M3 added `src/syntax/`
(the syntax tree), `src/sema/` (the typed tree, `Classes`, `SemaC`), `src/ir/` (the SSA IR: `Lowering`,
`Builder`, the per-kind files; `ir/opt/` its passes), `src/encoder/` (the in-process COFF assembler) and
`src/optimizer/` (MIR's passes, the inliner's budgets, the `Mir*` allocator). **x86 is typed tree -> SSA ->
`backend/SsaSelect.cpp` -> MIR; arm64-darwin and tms6747 still walk `Ast.h`** (`backend/Walker.h`) -
`docs/OPTIMIZER-IR.md` says which file does what. The suites' headers (`tests/run.sh`, `emit.sh`,
`names.sh`, `overload.sh`, `ssa.sh`) say what each checks.

**A basename may not repeat anywhere under `src/`** - `msvc/build.cmd` gives cl one flat `/Fo` directory.

## Decisions already taken

- **Conform to the platform ABI**: Itanium on Linux, macOS and the C6000; Microsoft on Windows. clang and
  cl are then oracles at the object level.
- **A template is instantiated by replaying its tokens**, giving up two-phase lookup (cxx1 accepts some
  programs clang refuses). The decision to revisit first if templates feel wrong.
- **Build the constant evaluator early**; `constexpr` is that evaluator exposed.
- **A keyword recognised but not implemented is refused by name** ("'x' is not supported yet"), never a
  parse error later. A refusal stricter than the standard is a defect too.
- **M3**: x86 code generation is from SSA (`ir::Lowering`, `ir::Builder`) and nothing else; M3b.7 deleted
  the `Ast.h` path, `-front=` and the adapter, and a function outside the lowering fails the compile, named.
  arm64-darwin and tms6747 keep `Ast.h` and the walker (history: "M3b.8: the optimizer documents").
- **The x86_64-windows default spelling is `-masm=masm`**, assembled in process by `src/encoder/` and linked
  by the project's LINK (history: "-masm=masm is x86_64-windows's default again").

## How correctness is established

Differential testing against clang, g++ and cl. **A green suite proves nothing until you prove what it ran
against**: rebuild from clean, record the commit a number was measured at, and `git fetch && git log
HEAD..origin/main` before the first edit.

- **The language oracle is `clang++ -x c++ -std=c++11 -pedantic-errors`** - without `-x c++` a `.c` is C,
  without `-pedantic-errors` the C++11 rules are warnings. Every `.expected` is clang's output.
- **cl is the authority on the Microsoft ABI** (`tools/cl-measure`), clang for that target the second; cl
  has no C++11 mode, so it answers ABI questions, not language ones.
- **Three boxes**: a change is done when Mac, Linux and Windows pass (`tools/verify-three`). x86_64 code is
  executed only where the host is x86_64; one target right and another wrong is this tree's house bug.
- **`tests/emit.sh --record` keeps a golden**; every later run says how many emitted files changed. A
  refactor's "nothing changed" is shown by it, never asserted.
- **A case about lifetime uses `tests/cases/lifetime.h`** (the ledger): live, double, phantom, over and lost
  must be 0; `built`/`gone` counts are elision-dependent and not facts.
- **`tests/open/`** is the register of known wrong answers with clang's answer; `tests/open.sh` names any
  that start agreeing so they move into `tests/cases`.
- `make test` runs the suites plus three gates that fail it: `tools/exclusions --check docs/EXCLUSIONS.md`
  (0 uncited, 0 stale - a refusal that moves a line moves its citation), `make comments` (the cap below) and
  `tools/memory-gate` (raw `new` and raw-pointer declarators in src/ir, src/syntax, src/sema/Typing*,
  src/optimizer/Mir* may only go down).

## Understandable means checkable

A change helps when it makes more of the compiler checkable. Six rules (history: "Understandable means
checkable"): a measured fact stays re-measurable beside its measurement; duplication is judged by which
agreement it hides; an invariant a person must remember is a defect with a delay; refuse by name, never
accept quietly; a claim with no oracle is not believed; and what must not change is half of any finding.

**Shapes that look wrong and must not change** (each has its history section): absolute token indices
(a replay re-reads by index); `resolveOverload` returning a `Signature` by value (C-01); a pointer to
member function wearing a struct's shape; `unqualifiedSpecifiers` answering void for `Point::Point(`;
call-site rows registered innermost-first and split, never sorted after the fact; a trivial special member
being no function at all; the parser numbering selectors as `Regions::lsdaTable` places them.

## Rules for code and cases

- **Comments: at most three lines a group, one line over a single line of code** (`make comments`). The
  story goes in the history file, not beside the code.
- **No `long` in a case that prints `sizeof`** (8 on Itanium, 4 on Windows); no mutation among a call's
  arguments (evaluation order is unspecified); no `return` in a handler of a case meant for Windows;
  define a case's constructors out of line so `names.sh` compares names, not inline emission.
- `<case>.notarget` = cannot compile for that target; `.nonames` = compiles, names differ from clang for a
  recorded reason; `.nocl` = names differ from cl; `.nomasm` = the MASM spelling cannot write it. Each
  reason is printed on every run.
- A Windows script edit is never made while a run of it is in flight (`sh` reads ahead).
- cl's assembly listing shows what cl means, not always what assembles (`.pdata`, `.text$x`, `FLAT:`,
  `COMDAT`): measure with the assembler.

## The boxes

- **The Mac edits and ships only** - no build, test or check runs there; the boxes carry the load.
- **Linux**: `ec2-user@52.202.164.123`, g++, 2 cores; every job under `flock ~/.boxleg.lock` and `timeout
  1500`; TI's tools in `~/ti6x`, CCS 5.5 in `~/ti/ccsv5`, the TI runtime in `~/c6747-lib`.
- **Windows**: `ssh windows` (`xrotrxt@192.168.100.84`), shared with another session - **at most 10
  processors and 24 GB**, every job pinned with `start /affinity 3FF`, never kill a process by name;
  `C:\Users\GRA\work` is a junction to the user's work folder; VS 2022 Build Tools (cl 19.44, clang
  19.1.5), Python, Git's bash; the project's masm and LINK built from `verify-tools\MASM` and `LINK`.
- No box job past 25 minutes: split it.

## Build

```
make                 build cxx3.exe with clang++ (Mac) or g++ (Linux)
make test            the suites and the three gates
tests/emit.sh --record     record the golden before a change
tools/verify-three [linux|windows|c6747]
```

`-MMD -MP` writes header dependencies: a stale object links and corrupts the heap three passes away.
`git stash` leaves mtimes that convince make nothing changed - `make clean` before believing a crash only
one build shows.

## Index of docs/CLAUDE-history.md

Each `## ` section of the history file, with its line there. `grep -n '^## ' docs/CLAUDE-history.md` lists
them too; read a section with `sed -n '<line>,<line+N>p'` or by searching its title.

- Two languages, and they are not the same one (line 6)
- Reading this tree (line 74)
- The parser is twelve files (line 115)
- The optimizer is its own directory, 2026-09-25 (line 188)
- Refusing by name reaches declarations now, not only expressions (line 202)
- Rung 5 reopened: a real tuple, 2026-08-29 (line 245)
- Where this came from (line 332)
- What makes C++ different from C to parse (line 375)
- The ladder (line 396)
- Where this stands, 2026-09-01 (line 413)
- The audit of 2026-09-01, and what it is for (line 1299)
- The line at C++14, and the refusals that hold it (line 1999)
- Three diagnostics that pointed the wrong way, and one name the assembler refused (line 2065)
- Rung 5: templates, done (line 2109)
- Rung 6: exceptions - the plan it was built to, kept as written (line 2686)
- Rung 7: the C++11 layer - the plan it was built to, kept as written (line 3246)
- A replayed body inherited the enclosing function's live objects (line 3541)
- Three small steps the exercise needed: array references, computed arguments, the cast that was not one (line 3573)
- A class template-id as a qualifier in an expression (line 3604)
- Function-template overloading, and deducing a non-type parameter (line 3628)
- Member function templates, and the two binding layers (line 3675)
- Operator overloading, and why the names came first (line 3733)
- `friend`, and why it landed beside the operators (line 3906)
- Overload resolution is checked against clang, not against a recorded answer (line 3957)
- Local classes, the last thing standing between here and lambdas (line 3998)
- Two bugs an audit found that no suite would have (line 4075)
- Default arguments, and the scope they are read in (line 4125)
- A by-value parameter and a reference to it are the same match (line 4167)
- A const object has to be initialised, and the line is CWG 253's (line 4203)
- An empty pair of braces is value-initialisation (line 4251)
- An enumeration keeps its name, which is half of being a type (line 4301)
- A base's members were built twice, and the second time was wrong (line 4335)
- `inline`, a linkage hint on machinery that already existed (line 4375)
- Weak definitions, and the link that follows from them (line 4397)
- All sixteen of Compiler++ compile, and the link is a different question (line 4448)
- `Base::f(...)` - the version an override replaced, and who may call it (line 4497)
- A base named with its namespace in a mem-initialiser list (line 4526)
- A local named `count` lost to `std::count` (line 4546)
- Two declarators, one declaration, and a `continue` in a `do`/`while` (line 4572)
- A class template's member is instantiated only where it is called (line 4597)
- A private nested type as a member's own return type (line 4626)
- `?:` as an lvalue, which was a missing shape rather than a missing rule (line 4648)
- `long long` is a type of its own, and the streams had never heard of it (line 4674)
- A converting constructor on `return`, and the `?:` that looked like its twin (line 4698)
- Five walls out of Compiler++, and the shape they had in common (line 4753)
- A temporary during unwinding, and why the obvious fix is a regression (line 4813)
- `system()` is not a way to start a process, and one platform said so (line 4890)
- An 8 MB stack on Windows, which is what the other two targets always had (line 4950)
- A static member defined out of line is worth its value when read (line 4979)
- A static member has no `this`, and one line of Compiler++ was that (line 5039)
- Windows EH in GNU syntax, and Compiler++ links (line 5076)
- COMDAT: ml64 makes COFF and cannot mark it, and the way round is one layer deeper (line 5132)
- Compiler++ on Windows: three faults found, and the one wall left (line 5199)
- A temporary made after the full expression ended, and the function it leaked into (line 5270)
- The operand `&&` skipped, and the destructor it was owed anyway (line 5330)
- A class returned by value was never destroyed, and the two halves that hid it (line 5393)
- A declaration in a condition, and the two rules that are not one rule (line 5448)
- `mutable`, three lines and one exception that was already there (line 5502)
- An enumeration inside a class or a namespace, and the three things it wanted (line 5535)
- Ordinary C++ this refuses, and none of it is on the ladder (line 5586)
- What is in `tools/`, and what was inherited and is dead (line 5655)
- The names are asked of cl now, on the box where cl lives (line 5702)
- Rung 7.6: lambdas, and the ladder is walked (line 5751)
- Reference data members, refused since rung 3 (line 5948)
- Class temporaries: one gap, three symptoms (line 5979)
- Member initialisers, `int x = 5;` on the member itself (line 6009)
- Pointers to data members, and why no backend heard about them (line 6041)
- Decisions already taken (line 6119)
- Understandable means checkable - the principle the 09-02 review applied (line 6141)
- What the compiler says about itself, and where it said it wrongly (line 6219)
- Access control asked from the wrong class, in both directions (line 6239)
- Eight nested-parse doors, and one list between them (line 6285)
- A capture by value of a class or an array, and the name it exposed (line 6337)
- A written destructor destroyed nothing the class held (line 6394)
- Three scopes, and they were being asked in nearly the reverse order (line 6434)
- What the 2026-09-06 register found, and where each root stands (line 6478)
- V-09, the finding with no root, and the register that holds the tail now (line 6530)
- A capture-default takes `this` as well, and both of them do (line 6600)
- A class template's three symbols, and the name they could not spell (line 6646)
- Every way out of a handler ends the catch, and only one of them did (line 6745)
- A `throw` out of a handler, which is the one exit the parser cannot write (line 6817)
- The two runtime calls clang made and this did not, and the bug behind one (line 6904)
- `int z(5);`, and the question the parser was asking instead (line 6971)
- Six refusals in one round, and the boundary each of them has (line 7012)
- A two-dimensional array member, built twice over its first row (line 7071)
- The GNU spelling is x86_64-windows's default now, and COMDAT is why (line 7101)
- `-masm=masm` is x86_64-windows's default again, with the project's LINK, 2026-10-25 (line 7138)
- `LNK4078`, and a section name that folded into the wrong one (line 7200)
- `$cppxdata$`, and a question asked of the wrong witness (line 7226)
- The Microsoft virtual-base layout, measured (line 7255)
- A frame that skipped the guard page, and two wrong answers before it (line 7306)
- The four oracles, measured together at c66c8d0 (line 7386)
- Dynamic initialisation, and the four refusals that were waiting for it (line 7414)
- Class-typed exceptions: seven refusals, one of them stale (line 7550)
- A local beside a `try`, and the sort that broke unwinding (line 7637)
- Class-typed exceptions, three of the seven (line 7695)
- A refusal is not a substitution failure (line 7777)
- A C++11 feature refused as C++14, and the sweep that found it (line 7853)
- Catching by reference, which is a slot and not a copy (line 7951)
- The object ledger, because printing cannot show a leak (line 8016)
- The eleven alternative tokens, which are a lexer rewrite and nothing else (line 8106)
- An array parameter mangles as `Q` on the Microsoft ABI (line 8205)
- The volatile sweep, 2026-09-07 (line 8267)
- A function-local static is named `f.n`, where Itanium writes `_ZZ1fvE1n` (line 8349)
- How correctness is established (line 8372)
- `dynamic_cast<void *>`, and two ABIs answering differently (line 8463)
- Range-based `for` over a class (line 8581)
- One type, one selector index - and the miscompile beside it (line 8654)
- A destructible object inside a `try` body (line 8732)
- A thrown temporary, and the three faults behind one refusal (line 8818)
- Two implementations of one numbering, and the filler that actually breaks a handler (line 8893)
- The implicit destructor of an intermediate class, which nothing emitted (line 8954)
- `<stdexcept>`, which is three lines per class because of what is under it (line 8990)
- The `__has_*` predicates, and four walls that were all this compiler's (line 9053)
- Regions that do not overlap, and the sort that was premature rather than wrong (line 9120)
- A `try` inside a `try`, and the three mechanisms it wanted (line 9186)
- namespace, and the fact that a namespace is not a type (line 9241)
- nullptr, which the backends never heard about (line 9307)
- static_assert, a declaration that declares nothing (line 9375)
- explicit, which changes nothing about a function (line 9422)
- The named casts (line 9482)
- noexcept, both of them (line 9531)
- A virtual base is built once, and the three paths that had to learn it (line 9574)
- The Microsoft virtual-base layout, written (line 9608)
- The diamond, and cl's hidden most-derived flag (line 9669)
- The primary base, which need not be the one written first (line 9729)
- A class's own vptr goes in front of its bases - and then the type_info (line 9774)
- `<type_traits>`, and four gaps of one family (line 9831)
- Compiler++ links on Windows: clang is the assembler (line 9879)
- The GNU spelling catches, and the try map that was not there (line 9908)
- What is left, at the close of 2026-09-11 (line 9936)
- Virtual inheritance proper: the Itanium group, its thunks and the VTT (line 9947)
- Comments in src/ are at most three lines, and now there is an oracle (line 10044)
- Version 1.5, 2026-10-03: the optimizer rounds and the constructor template (line 10152)
- Version 1.4, 2026-09-18: the cl review's nine wrong answers, from Compiler-Cppi (line 10176)
- Version 1.3, 2026-09-17: what came from Compiler-Cppi and how (line 10202)
- The register's last five, 2026-09-17 (line 10233)
- The seam with cl: a constructor's RAX, overloaded slots, a prototype's parameter, empty bases, pragma pack, member pointers, the library, overrides (line 10350)
- A hot loop that straddles a 64-byte line, and the two S9 "regressions" that were placement (line 10500)
- The explicit destructor call, `<new>` over the runtime, and placement arrays, 2026-09-26 (line 10589)
- The C6000 runs on the boxes, never on the Mac, 2026-10-23 (line 10699)
- 1.29 came from GRIDE after M0, and was merged over M2, 2026-10-24 (line 10721)
- M2.10: Sema types the syntax tree, and `-check=typed` holds it against the parser, 2026-10-24 (line 10731)
- M2.11: the adapter, and `-front=typed`, 2026-10-24 (line 10741)
- M2.12a: the adapter takes pointers, arrays, members, enumerators and sizeof, 2026-10-24 (line 10753)
- M2.12b: references, member calls and overload sets in the adapter, 2026-10-24 (line 10764)
- M2.12c: an array of scalars with an initialiser, 2026-10-24 (line 10774)
- M2.12d: a plain struct returned and passed by value, 2026-10-24 (line 10782)
- M2.12e and M2.12f: what builds nothing, calls by qualified name and through a pointer, 2026-10-24 (line 10790)
- M2.12h: class locals, their destructors, and the regions that unwind them, 2026-10-24 (line 10798)
- M2.12i: copy-initialisation and class temporaries, 2026-10-24 (line 10807)
- M2.12j: `throw`, `try` and its handlers in the adapter, 2026-10-24 (line 10814)
- M2.12k: a `try` among objects, and objects in a `try`, 2026-10-24 (line 10822)
- M2.12l: function template calls, and a class with a destructor returned, 2026-10-24 (line 10828)
- M2.12m: static locals as data, constructor templates, and a reference's temporary, 2026-10-24 (line 10835)
- M2.12n: lambdas, switch and goto, using-declarations, `noexcept`, 2026-10-24 (line 10842)
- M2.12o: a range-based `for` over an array, and a cast to a reference, 2026-10-24 (line 10851)
- M2.12p: a struct's braced list, and temporaries with a destructor, 2026-10-24 (line 10857)
- Build (line 10866)
- The FH3 state tree on x86_64-windows, and dispFrame, which is not a frame size (line 10958)
- A runtime symbol reached from a PIE, 2026-09-27 (line 10997)
- S13 brought across, and not wider than a pointer, 2026-09-27 (line 11024)
- A loop's back edge takes its test with it, 2026-09-27 (line 11041)
- A double the loop only reads, held in a free xmm register, 2026-09-27 (line 11054)
- A counter that cannot go negative is its own 64-bit index, 2026-09-27 (line 11071)
- arm64 had no optimizer, and now has three rounds of one, 2026-09-27 (line 11083)
- The C6000 against cl6x, round two of its optimizer, 2026-09-27 (line 11109)
- The four boxes measured again, and where the optimizer holds back, 2026-09-27 (line 11124)
- TI's own C6747 toolchains as three boxes, 2026-09-28 (line 11186)
- Compiler++ on TI's simulator, and the first two things it found, 2026-09-29 (line 11304)
- Locals in registers on the C6000, and the emulator's unwinder, 2026-09-29 (line 11330)
- LNK6x links the kernels, and TI's simulator runs them, 2026-09-29 (line 11448)
- Delay slots filled and execute packets formed on the C6000, 2026-09-29 (line 11494)
- Fable's review mended, first round: the three tools against both CCS toolchains, 2026-09-30 (line 11614)
- CALLP and BNOP: the review's B5(1), 2026-09-30 (line 11676)
- C-A5: a function past 64 KB gets pr2 tables, and TI's runtime unwinds them, 2026-09-30 (line 11702)
- Leaner calls and frames on the C6000, 2026-09-30 (line 11718)
- Division by a constant on the C6000: the review's B3, third shape, 2026-09-30 (line 11738)
- The evening's round, measured together on TI's simulator, 2026-09-30 (line 11767)
- Software pipelining on the C6000, 2026-09-30 (line 11787)
- Loop invariants hoisted to a preheader on the C6000, 2026-09-30 (line 11834)
- Addresses folded into the access on the C6000, 2026-09-30 (line 11868)
- A block's late writes cross into the label it falls into, on the C6000, 2026-09-30 (line 11890)
- matmul and sieve against cl6x on the C6000: pairs made in place, pointers stepped, a stepped counter pipelined, a fill stored as doublewords, 2026-10-01 (line 11912)
- A constructor that is a template, and the body error a trial was swallowing, 2026-10-02 (line 11991)
- Two gaps a CCS 5.5 sample found, 2026-10-01 (line 12047)
- The rest of the C6000 list: size, frame slots, the unwind index, the corpus, 2026-10-01 (line 12073)
- The C6000 size level landed, and the gates at the merge with gcc-scheme 0407df9, 2026-10-05 (line 12121)
- A release check the emulator cannot fool, 2026-10-05 (line 12196)
- An 8-byte value saved in one word, and the release test that found it, 2026-10-06 (line 12234)
- The tms6747 suite runs on vm6747sim as well, 2026-10-06 (line 12253)
- A file of data alone, and `extern "C"` written without braces, 2026-10-06 (line 12270)
- A name is in scope in its own initialiser, 2026-10-06 (line 12304)
- An array of a class sized by its list, and the same list at static storage, 2026-10-06 (line 12342)
- Two inlined callees on one frame key, and the member access that never asked, 2026-10-06 (line 12371)
- A pair copy that stopped at a label took the label for its partner, 2026-10-06 (line 12395)
- A Microsoft builtin of two letters takes an argument slot, 2026-10-06 (line 12414)
- `T(x).f();`, an elaborated `struct tm` in a namespace, and a parameter named `tm`, 2026-10-06 (line 12425)
- `reinterpret_cast` and the levels two pointers share, 2026-10-06 (line 12463)
- A non-public base is accessible where [class.access.base]/4 says, 2026-10-06 (line 12483)
- A static local whose initialiser is not a constant, 2026-10-06 (line 12525)
- A static member's definition is read in its class's scope, 2026-10-06 (line 12554)
- `sizeof(S::m)`, and a parenthesis that holds a type only if one can be read there, 2026-10-06 (line 12594)
- `S::T(3)`, and a namespace in front of a member the class lacks, 2026-10-07 (line 12621)
- A static reference member, 2026-10-07 (line 12643)
- A class template's static data member is defined where it is used, 2026-10-07 (line 12661)
- A remake removed, and liveness that still said the register was free, 2026-10-07 (line 12680)
- A member holding a function pointer, and an inline function nobody uses, 2026-10-07 (line 12704)
- A reference to a function as a member, an unused vtable, and class scope before the namespace, 2026-10-07 (line 12753)
- Liveness that knows which text it was computed for, 2026-10-07 (line 12806)
- A loop that leaves on a byte it reads, eight turns at a time on the C6000, 2026-10-07 (line 12856)
- `char *` minus `const char *`, 2026-10-07 (line 12911)
- The simulator is SIM6747, 2026-10-07 (line 12930)
- `cmp $0, %r` is `test %r, %r`, brought across from Compiler-Cppi, 2026-10-07 (line 12938)
- Two wrong answers of the x86 optimizer, and the -O2 runs that now block a merge, 2026-10-07 (line 12969)
- throw-pointer-base-adjust on x86_64-windows was the case's argument order, 2026-10-07 (line 13004)
- M10 W1: CodeView on x86_64-windows, so cdb stops at a line and walks the stack, 2026-10-08 (line 13017)
- M10 W2: what cdb shows in Locals and Watch - types, parameters, locals, statics, 2026-10-08 (line 13044)
- M10 W4: classes, enums, member functions and loops as cl shows them in cdb, 2026-10-08 (line 13071)
- M10: no second run of a line in the CodeView line table, 2026-10-08 (line 13099)
- M10 W5: a function's entry is named by its `{` line, as cl names it, 2026-10-08 (line 13110)
- The review of 2026-10-08, and what landed, 2026-10-08 to 2026-10-13 (line 13120)
- Version 1.7, 2026-10-13: the review round's second half (line 13395)
- `-g` for tms6747: cl6x's `.dwpsn`, which vm6747 and sim6747 stop on, 2026-10-13 (line 13429)
- Version 1.8, 2026-10-13: a changed module's seal takes a new number (line 13458)
- Version 1.9, 2026-10-17: the mending plan's first round, and a review that found two of its faults (line 13471)
- Version 1.10, 2026-10-17: GRIDEx2's phase F, and the tools 1.9's -g needed (line 13539)
- Version 1.11, 2026-10-17: two defects the GRIDEx2 showcase met (line 13560)
- Version 1.12, 2026-10-17: what the 1.9 round found and left, and the nearest scope (line 13580)
- Day 3 of the 1.9 mending plan, 2026-10-17: inheriting constructors, `T v`, the headline (line 13622)
- Version 1.13, 2026-10-17: the mending plan's third day (line 13648)
- Version 1.14, 2026-10-17: a review of 1.12 and 1.13, and the three defects a design found (line 13661)
- Expression SFINAE, and the decltype categories it needed, 2026-10-17 (line 13702)
- Version 1.15, 2026-10-17: expression SFINAE (line 13745)
- Version 1.16, 2026-10-17: the review's two nits, and a variable read in its namespace (line 13760)
- Version 1.17, 2026-10-17: a const return type in a Microsoft name (line 13788)
- Ref-qualifiers on member functions, 2026-10-18 (line 13812)
- Inline namespaces, 2026-10-18 (line 13899)
- Anonymous unions, 2026-10-18 (line 14017)
- Version 1.18, 2026-10-18: ref-qualifiers, inline namespaces, anonymous unions (line 14136)
- A union's member initialiser, 2026-10-18 (line 14161)
- Template template parameters, 2026-10-18 (line 14180)
- Non-type template parameter packs, 2026-10-18 (line 14306)
- Two over-acceptances closed, 2026-10-18 (line 14422)
- Version 1.19, 2026-10-18: template template parameters, non-type packs, and three wrong answers (line 14438)
- Unnamed template parameters, 2026-10-18 (line 14460)
- A member template's signature over the enclosing class's parameter, 2026-10-18 (line 14480)
- A base copied by name in a mem-initialiser, 2026-10-18 (line 14495)
- The CCS samples whole: six gaps, one linkage letter, 2026-10-18 (line 14514)
- Version 1.20, 2026-10-18: the CCS samples whole (line 14544)
- Slicing: a derived object copied to its base, 2026-10-18 (line 14559)
- A non-type parameter has its declared type, 2026-10-18 (line 14572)
- offsetof is a constant expression, 2026-10-18 (line 14581)
- An elaborated name with a qualifier, and `class X x;` in a block, 2026-10-18 (line 14590)
- An overloaded function's name picked by its target type, 2026-10-18 (line 14600)
- Two namespaces' function templates of one name, 2026-10-18 (line 14612)
- Version 1.21, 2026-10-18: the open register emptied (line 14625)
- An overloaded function's name passed as an argument, 2026-10-20 (line 14637)
- A goto out of a handler, function try blocks, and constructors that unwind, 2026-10-21 (line 14676)
- A `for`'s init-statement unwinds, 2026-10-21 (line 14707)
- Dynamic exception specifications, 2026-10-21 (line 14715)
- `constexpr` objects of class type, 2026-10-21 (line 14736)
- A typedef names an unnamed class for linkage, 2026-10-21 (line 14774)
- A universal character name in an identifier, 2026-10-21 (line 14783)
- User-defined literals, 2026-10-21 (line 14811)
- `thread_local`, 2026-10-20 (line 14845)
- `alignas` on a local past the stack's alignment, 2026-10-20 (line 14905)
- The COM Core sources compile, 2026-10-21 (line 14937)
- Version 1.22, 2026-10-21: the language grid down to one row (line 14973)
- GRIDE's cppparse example compiles, and agrees with clang's build, 2026-10-21 (line 14998)
- Version 1.23, 2026-10-21: GRIDE's cppparse whole (line 15040)
- M2's end decided, and M2.13-M2.15: the facts, the dump, the denominator, 2026-10-25 (line 15053)
- M2.16: what a name resolved to, recorded where the parser resolved it, 2026-10-25 (line 15075)
- M2.17: the typed tree at 99.5% of expressions, and what the last 656 are, 2026-10-25 (line 15092)
- An object in an unnamed namespace has internal linkage, 2026-10-25 (line 15109)
- M4: the encoder library in the tree, `cxx3 -c` writes a Windows object on every host, masm re-based, 2026-10-25 (line 15123)
- The Windows object held to its own tables, with no Windows box, 2026-10-26 (line 15161)
- C7 and M3b.0: a type's handle, and the record on the typed tree, 2026-10-26 (line 15207)
- M7: C90 through the parser and `SemaC`, 2026-10-26 (line 15232)
- The design's basis numbered: cppparse 1.0 and compilerpp 1.0, 2026-11-12 (line 15271)
- M3b.8: the optimizer documents brought to what is true, 2026-11-13 (line 15278)
