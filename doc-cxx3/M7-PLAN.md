# M7: C90 through the C++ parser and `SemaC`, so c90 can be retired

Written 2026-10-26 by Fable 5.1, from `c56064a` (the M3 line with M4 merged), branch `m7`. Design
section 12's M7 row - *C90 through the parser and `SemaC`; c90 retired* (R3.2) - and sections 3.2-3.3
say what M7 is; open item 8 of the design says what it starts from: cpp11 refuses `.c` today
(`Driver.cpp:1264`), so C90 is new work. This note is in the shape of `M3-PLAN.md`: what a commit may
change, how each step is judged, and a "Where it stands" section appended to as steps land - the
numbers written there are what "M7 closed" is measured by. The user is not available for this round:
every decision below is taken here and its reason written beside it.

## The compiler M7 replaces, and what it decides

**c90 1.4** (`Compilers/Compiler-Ci`, its own repository; `c90-1.4.dat`) is Compiler-C grown up: one
pass per stage, `Parser.cpp` doing the type work, four targets - x86_64-linux, x86_64-windows,
arm64-darwin, tms6747 - and a differential suite: **431 single-file cases and 8 multi-file
directories under `tests/cases` and `tests/multi`**, each with `// expect: N` on its first line,
compiled twice (c90 and the host's `gcc -w`), both run, and the two must agree on stdout and exit
status, which must be N. Beside them `tests/c90-probe.sh` (34 C90 features c90 once lacked; it
accepts 32, refusing K&R definitions and trigraphs by decision) and `tests/not-c90-probe.sh` (9
things C90 forbids; it accepts 8 as extensions - `//` comments, `long long`, variadic macros, a
declaration after a statement, a `for`-init declaration, a trailing enum comma, an empty macro
argument, GNU's `, ## __VA_ARGS__`). Its `tests/tms6747.sh` runs every case on vm6747 and on
sim6747 against the host's gcc, skipping the 26 cases of `tms6747-lp64.txt` whose reference needs
a 64-bit long. **It records no cycle counts** - the end gate's "cycle measurements, if its suite has
them" has nothing to compare, and is recorded so.

c90 is **not edited and not deleted** by M7: retiring it from GRIDE/RIDE is the user's call. M7
delivers the replacement and the evidence.

### The oracle for C90, written down first

Section 11.1 decides who says what C++11 is; for C90 the same shape, with these two witnesses:

| question | who decides |
| --- | --- |
| What does a valid program print and return? | **c90 1.4's own suite**: every case of it, as c90 and gcc agree on it. Where cxx3 and c90 disagree, the case is read against **clang `-std=c89 -pedantic-errors`** (what it accepts) and **clang `-std=gnu89`** / gcc (what it prints), and the verdict is recorded case by case in "Where it stands" |
| Is a program valid C90? | clang `-std=c89 -pedantic-errors`, with the eight extensions c90 accepts kept (they are in c90's own cases: `//` comments head every one). Where c90 refuses what clang accepts, cxx3 may refuse too, and the refusal is **recorded, not hidden** - `tests/open/c90-*.c` holds the program with clang's answer |
| Is a construct C++ and not C? | clang `-std=c89 -pedantic-errors` refuses it. cxx3 in C90 mode refuses it **by name**, saying it is C++ and that the file is being read as C90 - the dialect's one refusal table |
| ABI | the C ABI is the C++ ABI's subset on every target (a C struct is a trivially copyable class; a C function is `extern "C"`), so c90's cases hold it, and the host compiler on the Linux box and the Mac is the second witness, as in c90's suite |

Four measured disagreements between c90 1.4 and clang, taken before a line was written (the probes in
`tests/open/c90-*.c` carry them), and how each is decided:

| shape | c90 1.4 | clang `-std=c89 -pedantic-errors` | M7 |
| --- | --- | --- | --- |
| `int f();` then `f(1, 2)` | refused: "'f' takes 0 argument(s), given 2" | valid: `()` is an unspecified parameter list | **refused, as c90 refuses it** - an unprototyped function type is a type-system feature of its own, and no case of c90's suite has one. `tests/open/c90-unspecified-parameters.c` holds the program and clang's answer |
| `sizeof(0xFFFFFFFF)` on an LP64 target | 8 (`long`) | 4 (`unsigned int`: C90 6.1.3.2 gives a hex or octal literal an `unsigned int` rung) | **clang's**: the C90 ladder is int, long, unsigned long for decimal and int, unsigned int, long, unsigned long for hex and octal - with `long long` and `unsigned long long` above as the extension c90 already takes |
| K&R definitions, trigraphs | refused by decision (C23 deleted both) | valid | **both stay refused**, as c90 refuses them (cxx3 replaces no trigraph in either dialect - measured on c90's `trigraphs.c` probe: "expected '{'"; this plan first said otherwise and was corrected 2026-10-26). Each is in `tests/open/` with clang's answer |
| implicit `int`, an implicit function declaration | refused ("a prototype must come first") | valid C90 (a warning, not a pedantic error) | **refused, as c90 refuses them**; `tests/open/` holds each |
| `typedef int T; typedef int T;` in one scope | refused | not valid C90 (valid C11, valid C++) | **accepted**: the C++ rule stands, an over-acceptance recorded in `docs/CONFORMANCE.md`'s C90 section rather than built against |
| `struct S {...}; int S;` | accepted | accepted | accepted: in C the tag and the ordinary name are two namespaces, which is the one place the C dialect changes *lookup* rather than a rule |

## What a commit may change, and how it is judged

**Nothing M7 does may change what cxx3 emits for C++.** Every step is gated by:

| gate | where | what must hold |
| --- | --- | --- |
| `tools/identical.sh REF NEW` | Mac | REF is a build of `c56064a` kept at `~/cxxopt-build/ref-m7-src/cxx3.exe` (its own tree); every case and the 16 Compiler++ units at -O0, -O1, -O2 for x86_64-windows in both spellings and x86_64-linux at -O0/-O1/-O2: **0 differ** bar the known nine (a diagnostic naming `lib/` by its path, REF's tree being another folder) |
| `tests/emit.sh` against the golden recorded at `c56064a` | Mac | 0 of 1,924 changed |
| `make test` | Mac | run, emit, names, overload, syntax, typed, adapted, coff, exclusions 0/0, comment cap 0, memory gate held - and, from M7.2 on, `tests/c90.sh` |
| `tests/c90.sh` | Mac (arm64-darwin), Linux box (x86_64-linux) | the new suite below: every case of c90's suite through `cxx3 -std=c90`, differential against the host's C compiler, at -O0 and -O2 |
| `tests/c90.sh -arch tms6747` | Linux box | the same cases on vm6747 and sim6747, the lp64 and sim skip lists honoured, at -O0 and -O2 |
| `tests/c90.sh -arch x86_64-windows` | Mac | compile and assemble only: the MASM spelling assembled in process (`-c`), the GNU spelling by the Mac's clang; **runs are owed** - no Windows box this round |
| `python3 tools/memory-gate --check`, `python3 tools/comment-lines` | Mac | the count only goes down; no comment group over the cap |

A commit stays local and is never pushed; the coordinator reviews `m7` and merges. Other agents hold
`src/optimizer/`, the x86 backend (`m3-fable`), `src/sema/` typing and `TypeId` (`m3b0`), and the
Windows checks (`winchk`): M7's edits stay in the parser's productions, the new `SemaC`, the lexer's
keyword table, the preprocessor's predefined macros, the driver's `.c` path, `lib/`'s C headers and
`tests/`. Where `src/sema/Typing.cpp` must learn the dialect (the truth type of a comparison), the
change is one accessor and is named in the handover for `m3b0`.

## The design, as built

- **One parser, one `Dialect`.** `sema::Dialect { C90, Cxx11 }` is decided by the driver per input -
  `.c` is C90, `-std=c90|c89|iso9899:1990|gnu89` says C90 for every input, `-std=c++11|gnu++11`
  says C++ - and handed to the lexer, the preprocessor's macro set and the parser. The productions
  take the flag where C90's grammar departs from C++11's (design 3.2: `T(x)`, declarators, the
  keywords); they are not layered as classes.
- **`SemaC` and `SemaCxx : SemaC`** (`src/sema/SemaC.h`, `SemaC.cpp`): the rules C++ derives from C,
  as virtual methods the parser, `Lookup`, `Overload` and `Linkage` ask - the truth type (`int` / `bool`),
  the character literal's type (`int` / `char`), whether a string literal is const, the integer
  literal's ladder, whether `void *` converts to an object pointer and an integer to an enumeration
  without a cast, whether a tag is a type name, whether a jump may pass an initialisation, whether a
  const object at file scope has internal linkage, whether a name is mangled, whether a second
  declaration may overload, whether `auto` deduces. `SemaC` answers C90; `SemaCxx` overrides each
  to C++11's answer - tightening (`void *`, string literals, the const object) as well as extending
  (overloading, `auto`, mangling). Nothing emitted for C++ moves: every override returns what the
  parser's inline rule returned before the rule was lifted out, which `identical.sh` holds.
- **The refusal table is one table** (`SemaC::refuseCxxOnly`): a C++-only construct met in C90 mode
  is refused with "`<construct>` is C++, and this file is being compiled as C90 (-std=c90) - `<hint>`",
  the constructs a C90 token stream can reach: a reference declarator, a function declared inside a
  struct, a member initialiser, a default argument, a second declaration of one name with another
  parameter list (C has no overloading; two `f`s would be one symbol), a functional cast `T(x)` in an
  expression. The C++ keywords themselves are identifiers in C90 (`int class = 2;` is C), so
  `class X {}` is a parse error there as clang gives one, and no table row is needed.
  **These are not exclusions**: `tools/exclusions` marks a refusal by "not supported", "is C++1x" or
  "yet", and a dialect refusal says none of them on purpose - a C++ construct in a C file is
  ill-formed C, an error, not a missing feature (design 3.2's third bullet).
- **The driver takes `.c`.** The by-name refusal of `.c` goes; `-std=` is read; each `Job` carries
  its dialect; the predefined macros are two sets (C90's has no `__cplusplus` and no
  `__STDC_VERSION__`, as c90 1.4 and clang `-std=c89` have none).
- **The C headers in `lib/`** wrap their `extern "C" {` in `#ifdef __cplusplus` and gain the
  typedefs C has and C++ spells as keywords (`wchar_t` in `<stddef.h>`, per target, as c90's does).
  The line counts of the opening wrappers are kept, so no diagnostic naming a header line moves.

## Steps

| step | what | gate |
| --- | --- | --- |
| M7.0 | **The plan, the REF, the golden, the suite.** This note; REF built at `c56064a`; the emit golden recorded; c90 1.4's 431 cases, `pp_helper.h` and 8 multi directories copied verbatim into `tests/c90/cases` and `tests/c90/multi`; `tests/c90.sh` written in c90's `run.sh`'s shape (differential against the host's C compiler, `ulimit -t` rather than `timeout` so it runs on the Mac, `-arch tms6747` for the emulator and simulator legs, `-arch x86_64-windows` for the compile-and-assemble leg; `C90CC=<compiler>` so the same harness measures c90 1.4 itself). **Baseline**: c90 1.4 built from a copy of its sources, run through `tests/c90.sh` on the Mac, every case passing - the equivalence the oracle rests on, measured | REF and golden exist; the harness passes on c90 1.4 |
| M7.1 | **The dialect reaches the lexer, the preprocessor and the driver.** `sema::Dialect`; `.c` and `-std=`; the lexer's keyword table is C90's 32 words in C90 mode and the alternative tokens are identifiers there; a `Token` an identifier whose spelling is a C++ keyword is marked so `Token::is("new")` answers no for it; the C90 macro set; `SemaC`/`SemaCxx` with every rule answering as the inline code did. A `.c` compiles as far as the parser lets it | `identical.sh` 0 differ; golden 0; `make test` green; `cxx3 -std=c90` on a hello-world prints what gcc prints |
| M7.2 | **The C rules, one per commit where it changes emitted C.** The character literal; the string literal; the truth type; the literal ladder; `void *` both ways; integer to enumeration; the tag namespace (`Lookup` keeps a C tag set, a bare name finds a tag only under `struct`/`union`/`enum`); tentative definitions and the const object's external linkage; a jump past an initialisation; `auto` as a storage class only; `cLinkage_` from the first token so every name is its own symbol - a `static` function's too (C has no `_ZL`); the lib headers. After each: `tests/c90.sh` on the Mac, the count of cases passing recorded | `identical.sh` 0 differ and golden 0 at every commit; `tests/c90.sh` climbs to 431 + 8 on the Mac |
| M7.3 | **The refusals.** The table above, and a probe set `tests/c90/refused/*.c` each with its `.error`, run by `tests/c90.sh`; c90's own `tests/c90` and `tests/not-c90` probes run through `cxx3 -std=c90` and the accept/refuse column recorded against c90 1.4's | every C++-only probe refused by name, every C90 feature c90 accepts accepted |
| M7.4 | **The other targets.** `tests/c90.sh` on the Linux box (x86_64-linux at -O0 and -O2); `-arch tms6747` on vm6747 and sim6747 at -O0 and -O2, the lp64 and sim lists honoured; `-arch x86_64-windows` compiled in both spellings and assembled on the Mac (MASM in process, GNU by clang) at -O0 and -O2; `-g` compiles for every target (DWARF's language byte and CodeView's are C's in C90 mode) | the counts recorded per box and level; Windows runs owed |
| M7.5 | **The close.** `make test` with `tests/c90.sh` in it; `docs/EXCLUSIONS.md` unchanged (0/0); README and CLAUDE.md say the compiler reads C90 and how; the handover | every gate row green; the measures table filled |

What M7 does **not** do, said now: K&R definitions, an unprototyped `()` parameter list, implicit `int`,
implicit function declarations - c90 refuses each, so does this, and each is in `tests/open/` with
clang's answer; `-g` parity with cl under cdb for C (c90's M10 W3-W5 work: the closing-brace line,
`LF_ENUM`) is not measured this round and is owed; the C library over the UCRT and RTS6x is design
section 8's and not M7's - `lib/` is what c90 shipped, which is what its suite links against.

## What must not change, and what looks wrong but is load-bearing

- **`Token::is()` keeps matching an identifier's text** - `is("final")`, `is("override")` are
  contextual keywords read that way. The C90 mark on a token (`keywordAsName`) is what stops
  `is("new")` matching the C identifier `new`; it is set by the lexer only in C90 mode, so no C++ token
  changes.
- **Every C rule is asked through `SemaC`**, never through `dialect() == C90` at the site, so that the
  C++ answer is one place and `identical.sh` holds it. The exception is grammar (the keyword table,
  `auto`, `T(x)`), where the production itself is the rule.
- **A C file's `cLinkage_` is 1 from the first token.** `extern "C"` inside a C file is not C and the
  headers no longer write it; the parser's linkage depth is what the manglers read, and a C name is
  its own symbol by that depth and by `SemaC::mangles()` for the static-function case the depth
  alone gets wrong (`[dcl.link]/4` gives an internal C++ function a C++ name; C has no such name).
- **The emit golden and `identical.sh` compile `.cpp` files only**, so a C-mode change cannot show
  there - which is why every step also runs `tests/c90.sh`, and why `make test` gains it.

## For the implementer: where each remaining piece goes

Fable 5.1's budget ran out after M7.2 (below); Claude Opus 5.5 takes M7.3-M7.5 from here. What exists
and where the next edits land, file by file, as of the last commit on `m7`:

**What exists.** `src/Dialect.h` (the enum); `src/sema/SemaC.{h,cpp}` (`SemaC`, `SemaCxx`, `rulesFor`,
the refusal table `kCxxOnly[]` and `refuseCxxOnly`); the lexer's `isKeywordC90` and `Token::keywordAsName`
(`Lexer.h`, `Lexer.cpp` identifier branch); `predefinedMacros(b, dialect)` (`Backend.cpp`); the driver's
`-std=`, `dialectFor`, `Job::dialect`, `macrosC_` (`Driver.cpp`, `Driver.h`); `Parser::rules()`,
`dialect()`, `tagKey()` (`Parser.h`) and `cLinkage_ = 1` for C90 in the constructor; the rule sites
listed under M7.2 in "Where it stands"; the guarded `lib/` headers; `tests/c90.sh`, `tests/c90/`.

**M7.3, the refusals** - each a one-line `rules().refuseCxxOnly(src_, pos, "<row>")` guarded by
`dialect() == Dialect::C90`, the row being a `kCxxOnly` construct string; a probe `tests/c90/refused/<name>.c`
beside its `.error` holding the message's head (`tests/c90.sh` runs them in the host leg already):
- *a reference*: `ParserType.cpp` where the declarator reads `&` - the `consume("&")` and `consume("&&")`
  branches that call `types_.referenceTo` / `rvalueReferenceTo` (about lines 2676 and 2685), and the
  parameter-list `&` at about line 2464;
- *a function declared in a struct*: `ParserType.cpp` member loop, where `memberIsFunction` is computed
  (about line 1017) - refuse when true in C90 (a function pointer member, `int (*f)(int)`, is not a function
  and must pass: test `d.type->isFunction()`, not `peek().is("(")` alone);
- *a member initialiser*: the member loop's `=` after a data member's declarator (search `memberInit_[` in
  `ParserType.cpp`); a bit-field's `:` is not this;
- *a default argument*: `ParserTopLevel.cpp` about line 883 (`defaults.back() = at_; skipDefaultArgument()`)
  and the prototype path in `parameterTypes` (`skipDefaultArgument` call sites, `grep -n skipDefaultArgument src/parser`);
- *a functional cast*: the head of `Parser::functionalCast` (`ParserExpr.cpp:642`) - in C90 `T(x)` in an
  expression is not a cast; `(T)x` is unaffected;
- *a function try block*: where `block(true)` reads `try` in a C90 unit (`ParserStmt.cpp`); `try` is an
  identifier in C90 so this may already be a plain parse error - probe before writing it.
- `tests/c90/refused/` wants one probe per row, each checked first with `clang -std=c89 -pedantic-errors`
  (it must refuse) and `clang++ -std=c++11` (it must accept, or the row is not C++'s).

**M7.3 also**: `tests/open.sh` reads `*.cpp` only; give it a `.c` leg compiling with `-std=c90`, the oracle
recorded with `clang -std=c89 -pedantic-errors -w` (ACCEPT + stdout, as the C++ entries are), and move
the five programs under `tests/open/`: `c90-knr-definition.c`, `c90-unspecified-parameters.c`
(`int f(); ... f(1, 2)`), `c90-implicit-int.c` (`main() { return g(2); }`), `c90-implicit-declaration.c`,
`c90-trigraphs.c`. The programs, each one line and each accepted by `clang -std=c89 -pedantic-errors -w`
(measured 2026-10-26; the K&R and trigraph ones are c90 1.4's `tests/c90/knr_definition.c` and `trigraphs.c`):
`int f(); int main(void) { return f(1, 2); } int f(int a, int b) { return a + b; }` (exit 3);
`main() { return g(2); } int g(int x) { return x + 1; }` (exit 3, implicit int and implicit declaration at once -
split them into two files); `int f(a, b) int a; int b; { return a + b; } int main(void) { return f(1, 2); }` (exit 3).

**M7.4, the other targets**:
- Linux box: `scp` the tree (no `tests/out-*`) to `~/m7-<tag>/`, `make -j2` (one build at a time:
  `pgrep -fa 'make -j'` first), `sh tests/c90.sh` and `CXX1_FLAGS=-O2 sh tests/c90.sh`; then
  `sh tests/c90.sh -arch tms6747` and with `-O2` (the tools under `~/ti6x/{VM6747,ASM6x/build,LNK6x/build,SIM6747}`,
  `rts6x.lib` in `~/ti6x/RTS6x/build` - the harness looks there; `SIM=0` if the sim leg's tools are missing,
  said in the report). Delete `~/m7-<tag>/` after. The lp64 list is honoured; `pp_predefined` passes there.
- Windows: `sh tests/c90.sh -arch x86_64-windows` on the Mac (the MASM spelling `-c` in process, the GNU
  spelling through the Mac's clang); the runs are owed to the box.
- `-g`: compile every case with `-g` for each target as a smoke test; `Dwarf.cpp`'s `kLangCpp` (0x04) should
  be `DW_LANG_C89` (0x01) for a C90 unit and `CodeView.cpp`'s `CodeViewLanguage` C (0) rather than C++ (1) -
  the dialect must reach the backend: a `Program::dialect` field set by the parser is the smallest road. Not
  done, and owed; nothing in c90's suite measures it.
- The Typer (`src/sema/Typing.cpp`, lines 206-268) types a comparison, `!`, `&&`, `||` as `Kind::Bool`;
  for a C90 unit `-check=typed` would report them differing. One accessor - `p_.rules().truthType()` - closes
  it; it is `m3b0`'s file, so name it in the handover rather than editing it unasked.

**M7.5, the close**: `make test` gains `@CXX1=$(TARGET) ./tests/c90.sh` after `coff.sh` (it needs the host's
`cc`, which `run.sh` already needs); README and CLAUDE.md say the compiler reads C90 (`-std=c90`, `.c`) and
what it refuses; `docs/CONFORMANCE.md` gains a C90 section (the typedef redefinition, `S x;` after
`struct S {}` is *refused* here as C refuses it, a label before a declaration accepted as C++ does,
digraphs accepted though they are C94's); `docs/EXCLUSIONS.md` is unchanged by design (`tools/exclusions
--check` 0/0 - a dialect refusal is not an exclusion); the handover.

## Where it stands

*(Appended as each step lands: the commit, the gate rows with their numbers on the boxes they were
measured on, and what was found and not mended, registered in `tests/open/` with the oracle's answer.
Nothing above this line is edited; a correction is recorded below with its date.)*

**M7.0, the plan, the REF, the golden, the suite** (2026-10-26, Fable 5.1, Mac). REF built from `git
archive c56064a` at `~/cxxopt-build/ref-m7-src/cxx3.exe`; the emit golden recorded at `c56064a`, 1,930
files. c90 1.4 built from a copy of its sources in the scratchpad (its own suite does not run on the Mac:
`timeout` is absent and its `xargs` line is too long for macOS, which is why the harness here uses
`ulimit -t` and `-L 1`). **Baseline, c90 1.4 through `tests/c90.sh` on the Mac: 437 passed, 1 failed**,
the one `pp_predefined`, which asks for `__linux__` and `__x86_64__` and exits 3 under both c90 and the
host's clang on a Mac - a Linux-only case, listed in `tests/c90/linux-only.txt` and skipped on another
host with its reason printed. c90's two probe sets through c90 1.4: 32 of 34 C90 features accepted (K&R,
trigraphs refused), 8 of 9 extensions accepted (a typedef redefinition refused). Four probes against
clang before any code was written are the disagreement table above.

**M7.1, the dialect plumbing** (2026-10-26, Fable 5.1, Mac). `identical.sh` against REF: **6,039 compared,
9 differ, the known nine** (`posix-headers` for x86_64-windows at three levels in both spellings,
`windows-h-layout` for x86_64-linux at three - each a diagnostic naming `lib/` by its path, REF's tree
being another folder; no assembly differs). Emit golden 0 of 1,930 changed. `run.sh` 988 passed, 0 failed
(646 printed their recorded output, 340 refused as recorded). A hello-world `.c` compiles, links and
prints what clang's build prints.

**M7.2, the C rules** (2026-10-26, Fable 5.1, Mac), the sites: the integer and character literal
(`ParserExpr.cpp`, `rules().integerLiteralType(t)` in place of the inline ladder); the string literal's
element (`rules().stringLiteralElement`, and `rules().wideElement()` for `L"..."` - the C typedef's
integer, which is what `wchar_t w[] = L"xy"` measures against); the truth type at the comparison, `!`,
`&&` and `||` (`ParserExpr.cpp`, `ParserOperator.cpp`); `void *` to an object pointer (`Overload.cpp`
`rankArgument`, `ParserOverload.cpp` `requireConvertible`); an integer to an enumeration (both files);
tentative definitions (`ParserTopLevel.cpp`, the `defines && prev->emitted` refusal); the const object's
linkage (three sites in `ParserTopLevel.cpp`); a jump past an initialisation (`checkJump` returns at once);
`auto` as a storage class only (two sites in `ParserType.cpp`); every symbol its own name
(`Linkage::functionSymbol`, `dataSymbol`); the tag namespace (`Parser::tagKey`: `struct S`, `union S`,
`enum S` are the type-name table's keys in C90, at the four sites in `ParserType.cpp`); a second
declaration with another parameter list refused through the table (`ParserClass.cpp`); the fifteen C
headers' `extern "C"` under `#ifdef __cplusplus` with the opening wrapper's line count kept, `wchar_t`
a typedef per target in `<stddef.h>`, `_CXX1_INLINE` in `<time.h>`. **`identical.sh` 6,039 compared, 9
differ, the known nine**; emit golden 0 of 1,930. **`tests/c90.sh` on the Mac (arm64-darwin, -O0): 436
passed, 2 failed** before the wide-literal fix - `pp_predefined` (Linux-only, now skipped) and
`st_wide_literals` (`L"xy"` typed `wchar_t[N]` where the C typedef is `int`; mended by `wideElement()`,
the re-run's number is in the handover). The four probes: `p2` and `p3` print what clang prints (`4 5 9
a 10 3`; `5 7 5 1` / `8 4`, the `8 4` being clang's and not c90's `8 8`), `p4` refused by name through
the table, `p1` refused as c90 refuses it. c90's probe sets through `cxx3 -std=c90`: the same 32 of 34
and 8 of 9 as c90 1.4, plus the typedef redefinition accepted (9 of 9) - the one over-acceptance.
**The gate on the final M7.2 build, Mac, 2026-10-26**: `tests/c90.sh` **437 passed, 0 failed at -O0 and
at -O2** (arm64-darwin; the one Linux-only case skipped by name) - every case and every directory of
c90 1.4's suite, which is c90's own baseline on this host; `identical.sh` against REF 6,039 compared, 9
differ, the known nine; `make test` green - run.sh 988/0, emit golden 0 of 1,930, names.sh 655/0,
overload.sh 44/0, syntax 3/0, typed.sh 648/0 (0 differ), adapted.sh 648/0, coff.sh 1,270 compared 0
differ, `tools/exclusions --check` 126 sites 0 uncited 0 stale (46 citations re-paired by line order
within each file after the inline ladder left `ParserExpr.cpp`, then checked by the tool), comment cap 0,
memory gate held. Not run: the Linux box, tms6747, Windows - all owed to M7.4.
**Not done in M7.2**: the refusal probes of M7.3; the `.c` leg of `tests/open.sh`; the Linux, tms6747 and
Windows legs; `-g`'s language bytes.
