# WS-D - top level, statements, lexer, preprocessor: handover

Branch `review/d-toplevel`, from main `2955c0b`, 2026-10-08/09, executor Fable 5.1; **rebased on
main `869bae7`, finished and gated 2026-10-13** by the main session's side session (Opus 5.5), under
`docs/REVIEW-PLAN-2026-10-08.md` section 4 "WS-D" and section 5.2 step 5. Nothing was pushed or
merged. The first sitting's sections below are kept; where the second sitting changed what they
say, "2026-10-13" says so. The CLAUDE.md drafts are at the end.

## Second sitting, 2026-10-13: rebased, F27 built, the case label, the gate

**Rebase on main `869bae7`** (49 commits: WS-A, C1, E1, F merged, the 1.6 reseal, the workspace
reorg). One real conflict, `ParserType.cpp`'s in-class member declarator: F18's trailing return
type and C1's `override`/`final` both read after the exception specification. Resolved in the
grammar's order - `noexcept`, then `-> T`, then the virt-specifiers - which is
[dcl.decl]/4's `parameters-and-qualifiers trailing-return-type` followed by
[class.mem]'s `virt-specifier-seq`. `docs/EXCLUSIONS.md` conflicted in four commits; main's
version was taken each time and the result re-cited once at the end (section "EXCLUSIONS.md").

**F27a/b/c built for the first time, and compiled first time** - cl `/W4 /WX` on the Windows box
and g++ `-Werror` on the Linux box, no edit. The three cases' hand-written `.expected` were then
produced by VS clang (`clang++ -x c++ -std=c++11 -pedantic-errors`) and are byte-identical to what
was written by hand; `range-for-no-begin-refused.cpp`'s comment said clang refuses at 16:16 and
clang says 14:16 - corrected. **Two of main's `.error` cases were F27's own refusals**,
`range-for-class-iterator-refused` and `range-for-temporary-refused` (rung 7.3's); both now
compile, as clang does, and are deleted with the features, rule 8. `range-for-no-begin-refused`
stays, with F27b's wording. The amended commit drops "WIP" (`06efd44`).

**The scoped-enum `case` label** (`fae5bd6`). [stmt.switch]/2 makes a case label a converted
constant expression of the condition's promoted type, and a scoped enumeration converts to and
from nothing - so `Parser::caseLabel` now reads the label as an expression, refuses it where the
condition or the label is a scoped enumeration and the two types differ, and only then folds it.
clang's message, measured: "value of type 'int' is not implicitly convertible to 'Colour'", and
the other way round "value of type 'Colour' is not implicitly convertible to 'int'", both at 7:10
in the two cases `scoped-enum-case-int-refused` and `scoped-enum-case-in-int-switch-refused`.
`enum-class.cpp`'s `case Colour::Red:` keeps compiling. It is not an exclusion - it refuses
ill-formed C++11 - so `tools/exclusions` does not count it.

**Two path fixes the 2026-10-13 workspace reorg left**, both tools-only: `run-cases.cmd` built
the project's MASM and LINK from `<root>\..\MASM` while its own message (and the reorg) said
`<root>\..\..\Windows\MASM`, so the MASM spelling silently did not run from a clean layout
(`1c3625b`); and `tools/verify-three` tarred them from `$ROOT/..` after testing for them at
`$ROOT/../../Windows` (`3cc9787`).

## Commits

| commit | item | state |
| --- | --- | --- |
| `112d55c` | F20 const or reference member in a mem-initialiser list | landed |
| `4ce07f4` | F09 namespace alias | landed at namespace scope; inside a block refused by name |
| `bca501a` | F44 `__COUNTER__`; L19 a missing standard header refused by name | landed |
| `984c123` | cases: F20's and F09's constructors out of line, so names.sh agrees | |
| `22e950b` | F01 raw string literals | landed |
| `3150d80` | F18 trailing return types | landed; two shapes refused by name |
| `efb20c6`, `79abca4` | F14 delegating constructors | landed; a class with a virtual base refused by name |
| `06efd44` | F27a class iterator, F27b free `begin`/`end`, F27c a temporary range | landed, built and gated 2026-10-13 |
| `a1eb3a7` | the first sitting's note | |
| `fae5bd6` | the scoped-enum `case` label; EXCLUSIONS re-cited after the rebase | landed |
| `fa9083c`, `433948c`, `637520f` | clang's column; F27's two old refusal cases deleted; citations after the case-label edit | |
| `1c3625b`, `3cc9787` | run-cases.cmd and verify-three: MASM and LINK where the reorg put them | tools |

(The SHAs above are the rebased ones; the first sitting's `02e55d8` ... `5f5b5a6` are what
`origin/review/d-toplevel` still holds - the rebased branch replaces it, so a push needs `--force-with-lease`.)

**One deviation from the plan's ownership**, said first because the main session decides it:
F18 carries one small hunk each in `ParserType.cpp` (WS-C1's) and `ParserTemplate.cpp`
(WS-E1's) - the in-class member and the template pattern read the arrow too. Each is a call into
a function defined in WS-D's files, named in `3150d80`'s message. After the rebase the
`ParserType.cpp` hunk sits beside C1's virt-specifiers, in the order above.

## F20 - a const or reference member named in a mem-initialiser list (`02e55d8`)

[class.base.init]/8: the store from a constructor's own list is the member's initialisation,
so the refusal of a const member there goes; one the list leaves out with nothing to give it a
value is refused, as clang refuses it. Cases `const-member-init` (clang's output),
`const-member-uninitialised-refused`, `reference-member-uninitialised-refused`. Emit golden 0 of
1582 changed.

## F09 - namespace aliases (`77ddb81`)

[namespace.alias]: `namespace A = N;` and `A = N::M;` - every later `A::` to the end of the
enclosing namespace is rewritten to N's name in place, and the readers that meet the name
without `::` (a using-directive, a second alias) resolve it. Inside a block it is refused by
name, the rewrite reaching a namespace's close and not a block's. `namespace-alias-refused`
goes; `namespace-alias` (clang's output), `namespace-alias-in-block-refused`,
`namespace-alias-not-namespace-refused` arrive. Emit golden 0 of 1582 changed.

## F44 `__COUNTER__` and L19 the missing-header refusal (`f42cb9a`)

`__COUNTER__` expands to 0, 1, 2 ... once per expansion - an extension clang, g++ and cl share.
A macro argument is now macro-replaced once however often it is used ([cpp.subst]/1), which is
what makes `TWICE(__COUNTER__)` one value; `defined()` and `#ifdef` answer yes for `__LINE__`,
`__FILE__` and `__COUNTER__`. `#include <tuple>` and the 47 other C++11 header names `include/`
lacks are refused as "the standard header <tuple> is not provided by this compiler yet", the
table cited in EXCLUSIONS. Cases `preprocessor-counter` (clang's output),
`missing-standard-header-refused`. Emit golden 0 of 1582 changed.

## F01 - raw string literals (`71a049c`)

[lex.string]/4 and [lex.pptoken]/3: `R"delim(...)delim"` with every prefix (R, u8R, LR, uR, UR)
is its characters as written - no escape, no splice, no comment, no directive, a newline kept -
closed only by its own `)delim"`. The lexer reads it whole; the preprocessor's line splitter
leaves a splice inside one alone, and a raw string a line leaves open is copied verbatim to its
close, so a line inside it that looks like `%:define` or `#if` is not a directive and a name
inside it is not a macro. `raw-string-refused` goes; `raw-string-literal` (clang's output)
arrives.

## F18 - trailing return types (`722a9cd`)

[dcl.fct]/2: `auto f(params) -> T` reads T after the parameter list, with the parameters in
scope - `decltype(a)` names one - and on a member in the class's scope, so a nested type needs no
qualifier. Free functions, prototypes, out-of-line members (`auto S::f() const -> T`), members
declared and defined inside a class, block-scope declarations and function templates with
`-> T`. Refused by name: a function template whose trailing type is `decltype(...)` (Itanium
spells it as the expression, `Dt...E`, which the mangler cannot write) and `-> auto` (C++14); a
base other than plain `auto` is ill-formed and refused as clang refuses it. Cases
`trailing-return-type` (clang's output; names agree on all three targets),
`trailing-return-deduced-refused`, `trailing-return-template-decltype-refused`.

## F14 - delegating constructors (`b7fcd50`, `3f417bd`)

[class.base.init]/6: a mem-initialiser naming the class itself - by its name, its qualified tag
or a template-id that makes this class - is the list's only entry; the target is chosen by
overload resolution with its defaults applied and called on `this` before the body, and the
bases, members and vptr are left to it. A list with another entry beside it is refused as clang
refuses it; a class with a virtual base is refused by name (the target would have to be told
whether the object is the most derived). Cases `delegating-constructor` (clang's output, ledger
balanced; names agree but for the `.nonames` recording clang's C2-only inline template
constructors on x86_64-linux and a pre-existing Windows RTTI name),
`delegating-not-alone-refused`.

## F27a, F27b, F27c - the range-based `for` over a class (one rewrite, see "Commits")

**State (2026-10-13): built and gated - see "Second sitting". What follows was the first sitting's account, and the measurements now agree with it.** It was, on 2026-10-09: `5f5b5a6` a WIP commit - written, not built, not gated. The Windows box went down
at about 06:55 on 2026-10-09 and the user wound the workstream up at 07:20 with the weekly budget
at 92%, before a single build of the F27 code had run. Everything below about F27 describes what
the code is written to do, not what was measured; the first build may find a compile error or a
wrong answer, and the three cases' `.expected` were written from clang's rules by hand and have
NOT been produced by VS clang on the box yet - the recipe below does that first.

The three were written as one rewrite of `Parser::classRangeEnds` and `rangeForStatement`
(`ParserStmt.cpp`), and the code is committed once: the member/free choice, the iterator's kind
and the range's value category are three questions asked of one statement, and separating the
code would have meant three intermediate states of a function none of which is a feature. The
cases, the refusal that went and the EXCLUSIONS rows are the per-item record.

**F27a.** [stmt.ranged]/1's loop `for (auto __b = begin, __e = end; __b != __e; ++__b) { T x =
*__b; }` is built as before for a pointer, and for a class iterator `!=`, `++` and `*` are the
iterator's own operators through `comparison` (which asks `overloadedBinary` first) and
`overloadedUnary` - the ordinary resolution, so a member or a free `operator!=`, a const
`operator*`, and a `T &` the loop variable binds to all come for free. `begin`'s type and `end`'s
must be one type (C++11; the message says C++17 relaxed it). The iterators are *objects* now,
`initRangeLocal`: a class with a destructor is built where `T v = init;` would build it - a call
returning through a slot claims the slot (`claimCallResult`), a class with constructors goes
through `constructLocal` as copy-initialisation, a pointer is stored - and goes on `alive_`, so
the loop's end destroys it, a `return` or `break` out of the body does, and an exception does
through a cleanup region (`rangeBuilt_` records the statement index each became alive at, in
the shape `block()` keeps). Case `range-for-class-iterator` (clang's output): a class iterator
with a copy constructor and destructor, live count back to zero after each loop, a free
`operator!=`, a member one, a reference loop variable written through, `auto`, a const range, a
`break`.

**F27b.** Where the class has neither member, `begin(r)` and `end(r)` are looked up by the
argument alone - the class's own namespace, else the global one (`lookupKeys`'s rule for an
operator, written out because the two names are not operators). Case `range-for-free-begin`:
a namespace's `Row` with const and non-const overloads, a global `Pair`. The refusal
`range-for-no-begin-refused` stays and now says what [stmt.ranged]/1 looked for and found
neither of; clang refuses the same program at the same column.

**F27c.** A temporary range is what `auto &&__range` binds and keeps alive to the loop's end
([class.temporary]/5). A class temporary written as `P(1)` arrives as the glvalue `*(ctor(&tmp),
&tmp)`; `extendTemporary` takes its slot off the pending list and onto `alive_`, as `const T &r =
P(1)` does. A prvalue - a call returning the class - is built into a slot of its own by
`initRangeLocal`. Both are destroyed after the loop and on an exception out of it. Case
`range-for-temporary` (clang's output): `make(10)`, `Bag(20)`, a plain struct returned by
value, and a throw out of the body caught one function up with the range destroyed on the way.

**What is left of F27, and the refusal that stays.** On x86_64-windows a range or iterator
with a destructor inside a `catch` handler is refused by name (`ParserStmt.cpp:813`, the same
funclet-inside-a-funclet reason `block()` gives at `:1239`; cited in EXCLUSIONS beside it). An
rvalue reference loop variable stays refused. F27d, a braced list, is E2's after F25. **A
pre-existing defect found reading the code and not mended**: the loop variable itself, `T x :
r` with T a class with a copy constructor, is still copy-initialised by a bytewise `Assign` (the
array path has done so since 7.3) and never destroyed - `for (std::string s : v)` shallow-copies
each element. It wants the same `initRangeLocal` road for `d.name`, with the body block's own
cleanup; left because the body's `alive_` bookkeeping (loopMarks_, breakMarks_) is taken
before the variable is built and the change is a round of its own.

## Golden

**2026-10-13, at the tip, against main `869bae7`.** Recorded on the Windows box from main's own
build (`tests/emit.sh --record`, `--o2 --record`) and read from the branch's:

- -O0, all four targets: **0 of 1638 files changed, 36 added** - nine WS-D cases with recorded
  output or a refusal that emits on every target it allows. Nothing that already compiled moved,
  the pointer range-for loop included, which the F27 rewrite rebuilds from the same nodes.
- -O2, tms6747: **0 of 411 changed, 9 added**.

The first sitting's per-item goldens (F20, F09, F44: 0 of 1582 each) are superseded by this one.

## Gates (2026-10-13, at the tip; builds on the boxes, Mac only for the text checks)

Windows box: `C:\Users\GRA\work` (it did not exist; made today - the box was cleaned on
2026-10-13), laid out as the Mac is: `Compilers\ws-d` (the branch), `Compilers\main` (for the
golden), `TI\{ASM6x,LNK6x,SIM6747,VM6747,RTS6x}` and `Windows\{MASM,LINK}`, each a `git archive`
of the Mac's HEAD, every tool built there by cl. RTS6x's `rts6x.lib` was built by main's cpp11.
The box ran the tree at `fae5bd6` + `fa9083c`; the commits after it change no source.

| gate | result |
| --- | --- |
| Linux box (g++ 11 build, `~/work/Compilers/ws-d`) `run.sh` -O0 | 667 passed, 2 failed - the two old F27 refusal cases, deleted since |
| Linux box `run.sh` -O2 (`CXX1="./cpp11.exe -O2"`) | 666 passed, 3 failed - the same two, and "a quoted pattern with -S", which quotes `"$CXX1"` and so cannot pass with a flag inside it: pre-existing on main, a harness quirk, not WS-D's |
| Windows `run-cases.cmd`, GNU spelling, -O0 and -O2 | **402 printed / 0, 243 refusals / 0**, both levels (compared on the Mac, CR stripped, as verify-three does) |
| Windows `run-cases.cmd`, MASM spelling (project's masm + LINK), -O0 and -O2 | **402 / 0, 243 / 0**, both levels |
| `winlink-check.cmd` | member-pointer-models, overload-slots, this-return: ab and ba all match |
| Windows `tests/run.sh` -O0 and -O2 under Git bash | 403 of 404 recorded outputs agree once CR is stripped; every failure is one of the 19 cases whose `.notarget` names x86_64-windows (run.sh takes the box for x86_64-linux) or the two deleted cases - run-cases.cmd is that box's real host suite |
| `names.sh` (bash + VS clang 19) | **421 passed, 6 failed** - the six main has: compare-with-zero, pipelined-data-exit, pipelined-reduction, ternary-index-after-remake, and ifdef-comment and using-declaration-chain (no Darwin headers on this box). Every WS-D case agrees |
| `overload.sh` | **33 agreed, 0 differed** |
| `tests/tms6747.sh` -O0 and -O2, both legs (vm6747; asm6x + lnk6x + rts6x.lib on sim6747) | **411 passed, 0 failed** at each level, every pass also on sim6747 |
| `tools/exclusions --check docs/EXCLUSIONS.md` | **0 uncited, 0 stale**; 126 sites, 117 messages |
| `make comments` (`tools/comment-lines src`) | **0** groups over the cap |
| `tools/seal check` | 9 sealed files differ, as expected: `Lexer.cpp`, `Lexer.h`, `Preprocessor.cpp`, `Preprocessor.h`, `Parser.h`, `ParserStmt.cpp`, `ParserTemplate.cpp`, `ParserTopLevel.cpp`, `ParserType.cpp` |

Not run: the Linux box's tms6747 legs (one leg at a time there, and the Windows box ran both);
TI simulator jobs (rule 4: WS-H and the final gate only).

## EXCLUSIONS.md

Main's document taken whole at the rebase, then re-cited by message against the rebased source
(57 citations moved), and WS-D's rows applied: the raw-string, delegating-constructor, const-member,
three range-for, trailing-return and namespace-alias rows gone; the missing standard header, the
namespace alias inside a block, the trailing return type that is itself deduced, the `decltype`
trailing return of a function template, the delegating constructor of a class with a virtual base,
and the range-based `for` with a destructor inside a Windows handler added. The count paragraph
says 126 sites, 117 messages, at this branch. `--check` reads 0/0.

## Sealed files changed

The nine in the gate table. The source seal stays 1.6 until the release step reseals 1.7.

## Draft CLAUDE.md paragraphs

### The range-based `for` finishes its rewrite: a class iterator, a free `begin`, a temporary

**[stmt.ranged]/1 is a rewrite, and three thirds of it were refused by name until 2026-10-09.**
`for (T x : r)` is `for (auto __b = begin-expr, __e = end-expr; __b != __e; ++__b) { T x = *__b;
body }`, and the loop built here had begin and end as members, the iterator a pointer, and the
range an lvalue - which is every container in `include/` and nothing else. The class iterator is
the standard's own case: `!=`, `++` and `*` are whatever the iterator declares, found by the
operator resolution every other expression goes through, so a member `operator!=` or a free one,
a const `operator*` and a `T &` the loop variable binds to come for nothing. The two iterators
are objects of the loop's own scope now - built as `T v = init;` builds one, on `alive_`, so the
loop's end, a jump out and an exception each destroy them, through the cleanup regions `block()`
already shapes; the pointer loop emits what it emitted, which the golden said. Where the class
has neither `begin` nor `end` the free `begin(r)`/`end(r)` are looked up by the argument's own
namespace and then the global one, the operators' rule. And a temporary range is what `auto
&&__range` binds and keeps to the loop's end: the `*(ctor(&tmp), &tmp)` a class temporary already
is has its slot moved onto `alive_` as a reference binding would, and a call returning the class
is built into a slot of its own. What stays refused: the loop variable of class type is still
copied bytewise, as the array loop has copied it since rung 7.3 - a defect, recorded; an rvalue
reference loop variable; on x86_64-windows a range or iterator with a destructor inside a `catch`
handler, the funclet rule.

### Delegating constructors, trailing return types, raw strings, a const member initialised

Each of F14, F18, F01 and F20 is written up in its commit message (`b7fcd50`, `722a9cd`,
`71a049c`, `02e55d8`) and in this note's sections; the main session lifts the paragraph it wants.
Two sentences worth the CLAUDE.md entry: **a delegating constructor leaves the bases, members
and vptr to its target** - the list's one entry is the whole initialisation, [class.base.init]/6
- and a class with a virtual base is refused because cl's most-derived flag and Itanium's C1/C2
split would both have to be passed through, which is a round of its own; and **a trailing
return type is read in the scope of the parameters**, so `decltype(a + b)` names them, which is
the whole reason the syntax exists.

### `__COUNTER__`, a namespace alias, and a header named by its absence

`__COUNTER__` is an extension the three oracles share and is one per expansion - which made a
macro argument's replacement happen once however often the parameter is used ([cpp.subst]/1),
a rule that had been wrong without a way to see it. `namespace A = N;` is a rewrite of every
later `A::` to the end of the enclosing namespace, which is why it works at namespace scope and
is refused by name inside a block. And `#include <tuple>` says "the standard header <tuple> is
not provided by this compiler yet" rather than "cannot find", for the 48 C++11 header names
`include/` lacks - a refusal `tools/exclusions` can count.

### A scoped enumeration's `case` label

**[stmt.switch]/2 converts a case label to the condition's promoted type, and a scoped
enumeration promotes and converts to nothing**, so `case 1:` under `switch (Colour c)` and
`case Colour::Red:` under `switch (int n)` are both ill-formed. `caseLabel` folded the label
first and narrowed it to the governing type without asking what type it was, which accepted
both; it reads the label as an expression now and refuses a scoped enumeration on either side
unless the two are one type, with clang's words. Found in WS-C1's code by WS-D's review, mended
after the rebase gave this branch `enum class`.

## Left open, said plainly

- **The range-for loop variable of class type is copied bytewise** and never destroyed -
  pre-existing since rung 7.3, found reading F27's code, not mended (its section above). Wants
  `initRangeLocal`'s road for `d.name` inside the body's own cleanup bookkeeping; a round of its own.
- **F27d**, a braced list as the range, is E2's (plan step 6).
- The Linux `run.sh -O2` "quoted pattern" line is a harness quirk on main, not this branch's.
