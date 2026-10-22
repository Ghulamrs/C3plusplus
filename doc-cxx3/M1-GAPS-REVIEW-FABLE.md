# Review of the M1 gap list (`M1-GAPS-2026-10-23.md`) - Fable 5.1, 2026-10-23

**What was reviewed.** `doc-cxx3/M1-GAPS-2026-10-23.md` (identical to the C++Optimize copy), against
the raw Windows measurement (`shim.json`, 1,611 errors in 60 files, 1,180 distinct sites), the
source at `3b7ff04`, and the Mac's own `cxx3.exe` at the same commit with `-arch x86_64-windows`.
The oracle for "is this valid C++11" is `clang++ -std=c++11 -pedantic-errors` on the Mac; every
probe below was run through both, and the probe files are in the scratchpad
(`.../scratchpad/review/probes/`). Nothing under `src/`, `include/`, `lib/`, `tools/` or `tests/`
was changed; the experiments were run on a copy of `src/` in the scratchpad.

**Verdict in one paragraph.** The twelve language roots are real and each reproduces; the
file-count attributions L1 (43), L2, L3 (28), L5 (27) are right. Three descriptions are wrong in a
way that would send the fixer to the wrong place (L7, L8, and L5's classification), one root is
hidden by the way the data was read (a `-keep-going` defect inflates the knock-on counts), and the
list stops one layer too early: standing the first-layer roots in and recompiling - which this
review did, four layers deep, on the Mac - shows L7 and L9 are header-level roots that take down
the parser and the optimizer whole, and surfaces six language roots and twelve library gaps the
document does not have. The order of work changes accordingly: L7 moves to the top beside L1, and
two header roots the list does not know about (L13, L14) must be done before the optimizer or the
parser can compile at all.

---

## 1. The groupings, root by root

| # | the document says | what the data and the source say | verdict |
| --- | --- | --- | --- |
| L1 | a function type as a template argument; 43 files; first root | 43 files confirmed (25 via `OptIr.h:82`, 16 via `Parser.h:1811`, 5 via `Optimizer.h:26`, 2 via `CodeView.h:32`). The refusal is by name. But the *language half* is larger than "accept the syntax": `matchPattern` and `deduceOne` do not descend into a function type at all - see section 4 and the design. | **agree**, with the scope widened |
| L2 | `using Walker::visit;` refused by name; takes down the three code generators and `Masm.h:116` | Confirmed. **But the three lines are not load-bearing**: with all three deleted, every backend unit compiles under `clang++ -std=c++11 -fsyntax-only` (six units, zero errors). What they buy is silence under `-Woverloaded-virtual`, which the Makefile's `-Wall -Werror` turns into an error on clang (five errors at `X86_64Linux.h:54-58`). So L2 can be closed today by deleting the three lines plus `-Wno-overloaded-virtual`, or by the feature later. | **agree on the fact; the fix should be the source change** |
| L3 | a braced default argument; `Ast.h:238`, `Preprocessor.h:14`; 28 files | Confirmed, 28 files, and it is the *first* error in all 28. What the document does not say: it takes down `class Call` whole (`Inliner.cpp:29 'const class Call' has no member 'callee'`), not only the constructor. And "write the value" is not enough at `Preprocessor.h:15`: the rewritten default `std::vector<std::pair<std::string, std::string> >()` is refused `expected ','` - a comma inside a template-id inside a default argument is read as the parameter separator (probe `defarg.cpp`: clang `0`, cxx3 `expected ','` at col 201). A typedef dodges it; the bug stays. | **agree; one more defect beside it** |
| L4 | braced member initialiser with values on an array member; `C6xModel.h:63` | Confirmed. Two more sites of the same refusal are hidden behind it: `C6xModel.h:65` (`cross[2] = { false, false }`), and `X86_64Linux.h:178-179` (`GnuSpelling gnu_{out_};` - the *class* form, taking down `X86_64Linux`, `Masm`, `X86_64Windows`, `Backend` once L1/L2/L3 lift). Every array site in `C6xModel.h` initialises to all-false, so `= {}` is an exact rewrite there. | **agree; two hidden sites, one of them a header** |
| L5 | `unique_ptr != nullptr` refused as "'bool' and 'std::nullptr_t' cannot be combined"; 27 files; listed as a *language* root | 27 files confirmed. **It is a library gap, not a language root**: `include/memory` declares no `operator==`/`!=` between `unique_ptr<T>` and `nullptr_t` (it has them for `shared_ptr` against `shared_ptr` only). Two oracles say so: `clang++ -std=c++11 -I include` over cxx3's own `<memory>` refuses `Ast.h:359` with "invalid operands to binary expression ('const unique_ptr<Expr>' and 'std::nullptr_t')"; and the stand-in run with four operators appended to a copy of `<memory>` makes every L5 site go. The message cxx3 gives is misleading because it applied the `explicit operator bool` in a `!=` and then compared bool with nullptr_t - that conversion may not be used there ([class.conv.fct]/2), and clang refuses `q != nullptr` on a class with only an explicit bool conversion (probe `L5b.cpp`). cxx3 refuses it too, so it is a wording fault and not an over-acceptance. **Sibling found at layer 4:** `shared_ptr != nullptr` (`ParserInit.cpp:458`, `ConstObjectPtr` is a `shared_ptr`). | **reclassify to B; add shared_ptr** |
| L6 | `constexpr const char *kName = "cpp11"` not a constant | Confirmed (probe `L6.cpp`); one site. | **agree** |
| L7 | "a variable captured by reference reads as const inside the lambda, as though the call operator's const reached through the reference"; 8 sites, 5 files, "only the lambda; each site stands alone" | **The description is backwards and the reach is wrong.** Every site captures a *const* object or a *const reference* - `const std::string &s` (`Lexer.cpp:318`), `const Target &t` (`Backend.cpp:94`), `const bool isUnion` (`CodeViewTypes.cpp:123`), `const bool constant` (`C6xPipe.cpp:874`), `const std::vector<Line> &v` - and the closure's reference member is formed as `T &`, dropping the const, so it cannot bind to the const object. The error is at the lambda expression (the closure's construction), not inside the body. Probes: `L7a` (const ref to string), `L7b` (`const bool` by `[&]` and `[&c]`), `L7c` (`const T &` parameter by `[&t]`) all refused by cxx3 and accepted by clang; `L7d` (a non-const `int` by `[&]` and `[&n]`, written through) compiles and prints `7` on both. So the rule is: **a by-reference capture of a const object loses the const.** And it does not stand alone: `OptFlow.h:14` captures `const Convention &c` by `[&c]` - a header, included by every optimizer pass and all three x86 backends. In the layer-2 run it is the first error of 22 files; at layer 4 the message appears in **18 of 60 files**. | **correct the description; promote to a header root** |
| L8 | `Base::operator=(o)` "where the base's copy assignment is implicit": `'LineFields' was not declared` | Half right. The qualified call fails whether the base's `operator=` is written or implicit (probes `L8a` implicit, `L8b` written, `L8c` move: all three `'B' was not declared`; clang prints `7 1`, `8 1`, `7 1`). So it is "a qualified call of a base class's `operator=`", nothing about implicitness. The obvious rewrite `static_cast<B &>(*this) = o` is itself refused by name (`L8d`: "casting a reference to a different type is not supported yet"); `*static_cast<B *>(this) = o` works and is what the stand-in run used. | **correct the condition** |
| L9 | a pointer to a const member function; `Parser.cpp:204`, `:241`, `:391`; "qualifyForLookup and its two neighbours" | The first sites are in **`Parser.h`**, not `Parser.cpp`: `Parser.h:2336` (`refuseInlineAmbiguity`'s `bool (Parser::*same)(std::size_t, std::size_t) const`), `:2349`, `:2360`. They sit behind `Parser.h:1811` (L1), so the Windows run never reached them; the layer-2 run shows `Parser.h:2336` as the first error of all 16 parser units, and the class body stops there - 590 lines of declarations lost, `at_`, `src_`, `types_` "not declared" everywhere. **And a second refusal hides under it** (L13 below): with the `const` taken off, the same line is refused as "`'Parser::*' before 'Parser' is complete` - the Microsoft ABI sizes a pointer to a member by the class's inheritance model". Both must go, or the three declarations must change, before any parser file compiles. | **the site and the reach are wrong; a second root under it** |
| L10 | a local class of a member function calling a private member | Confirmed (`L10.cpp`: clang 42; cxx3 "'secret' is private"), and the data-member form through a pointer too (`L10b.cpp`). `ParserTemplate.cpp:80` is a third site (layer 4). | **agree** |
| L11 | `::reg(name)` refused by name | Confirmed (`L11.cpp`: clang 4). One function. | **agree** |
| L12 | `push_back(++x)` chooses `T &&` for an lvalue; "look at this one first" | Confirmed, and wider: `push_back(x = 5)` and `push_back(x += 2)` fail the same way (`L12b.cpp`; clang `5 4 6`). The result of a built-in assignment or prefix `++`/`--` is an lvalue ([expr.ass]/1, [expr.pre.incr]/1); `decltype` learned that in 1.15 through `Expr::Category`, overload ranking did not. The refusal is right and the choice is the fault, as the document says. | **agree; three operators, not one** |

**The "694 knock-on sites" and the 47 `expected ')'` / `expected ';'`.** Those two counts hide a
defect in the measuring instrument. After a refusal inside a template argument list, `-keep-going`
resumes at the next declaration with `inTemplateArgs_` still true - `DeclarationSnapshot`
(`Parser.h:1589-1600`) saves `angleSplit_` (`split`) but not that flag - so every `>` in an
expression for the rest of the unit is read as a closing angle. That is what `include/ostream:104:
expected ';'` in `Spelling.cpp` is (`base_ = f >> 8;`, the first `>>` after `OptIr.h:82`), and what
`Spelling.cpp:91/190/372`, `C6xPipe.cpp:1503/1521/1726/1964`, `ParserTemplate.cpp:1389` and
`ParserOverload.cpp:905` are: each line holds a `>` comparison (checked, all nine). Reproduced on
the Mac with `./cxx3.exe -S -arch x86_64-windows -keep-going=500 -I shim src/backend/Spelling.cpp`.
The consequence for the gap list is worse than nine phantom errors: every expression after the
first refused template-id in a unit is misparsed, so real errors in the rest of that unit are not
counted. **Add `inTemplateArgs_` to the snapshot before the next measurement**; it is one field.

## 2. What the list misses, and how it was found

The document says the next layer only shows once the one above compiles. That is true, and it is
also something that can be done now rather than later: the first-layer roots were stood in on a
copy of `src/` with measurement-only edits - the same device as the stand-in headers - and the 60
files recompiled on the Mac with the Windows target. Four layers were taken. The stand-ins are not
fixes; some of them are wrong code (member pointers replaced by ints), kept only until the next
root showed.

| layer | stood in | compiles | first errors |
| --- | --- | --- | --- |
| 0 (the document's run) | the 7 headers | 4 of 60 | L1, L3, L8 |
| 1 | + `std::function` -> `Fn0/Fn1/Fn2` stand-ins at the 14 sites; `= {}` defaults written out; `unique_ptr`/`nullptr` operators appended to `<memory>`; `LineFields::operator=` as `*static_cast<LineFields *>(this) =`; the three using-declarations deleted; `kName` as `const char *const`; `++nextIndex` split; `to_string(unsigned long long)` added | 10 | **`OptFlow.h:14` (L7, 22 files)**, **`Parser.h:2336` (L9, 16 files)**, `tellp` missing (3 files), `C6xModel.h:63` (L4), `Preprocessor.h:15` (the default-argument comma), `map::at`, `_MAX_PATH` |
| 2 | + `[&c]` -> `[c]` in `OptFlow.h`; the three member-pointer params' `const` removed; `gnu_{out_}` -> `= GnuSpelling(out_)`; `C6xModel.h:63-64` -> `= {}`; typedef for the Preprocessor default; `tellp`, `map::at`, `vector::data` added to copies of the headers | 11 | **`Parser.h:2336` again, now "`Parser::*` before `Parser` is complete" (L13, 16 files)**, **`OptFlow.h:14` again, now "`FlowOf<...>` has no static member function 'build'" (L14, 22 files)**, `vector<enum class>` (L17), `C6xModel.h:65` (L4), `ostringstream::swap`, `_fullpath` |
| 3 | + member pointers replaced by ints (wrong code, measurement only); the qualified base call made through a base reference; `C6xModel.h:65` -> `= {}`; `swap`, `_MAX_PATH`, `_fullpath` added | 23 | **L7 in 18 files**; `PassInfo{...}` as an expression (L15, 16 sites in `OptPipeline.cpp`); `n.kids = { e, ... }` (L18); eleven library gaps (section 3) |

### Language roots the list does not have

| # | root | evidence | reach |
| --- | --- | --- | --- |
| **L13** | **A pointer to a member of the class being defined, in its own body, on x86_64-windows** - refused "before 'Parser' is complete - the Microsoft ABI sizes a pointer to a member by the class's inheritance model". cl built stage 0 from this source, so cl takes it; `clang++ -target x86_64-pc-windows-msvc` takes it and sizes the pointer by the *completed* class (probe `mptr_incomplete.cpp`: `static_assert(sizeof(bool (P::*)(int,int) const) == 8)` passes for the MSVC target, fails for Linux at 16). The rule to measure on the box is cl's `/vmb` default: single-inheritance representation assumed for an incomplete class, checked at completion. | `Parser.h:2336/2349/2360`; the whole parser (16 files), under L9 |
| **L14** | **A qualified call to a base class *member template* through a template-id, `FlowOf<Entry>::build(s, lambda, regions)`** - "has no static member function 'build' - only a static member is reachable through a template-id with no object". `FlowOf::build` is `template <class EffectsFn> void build(...)` (`OptCore.h:147`). The 1.20 fix (`template-id-qualified-base-call`) covers a plain member: probe `L14a` (plain) passes, `L14b` (member template with a lambda argument) fails, clang prints `12`. | `OptFlow.h:14`; 22 optimizer files and the three x86 backends, under L7's site on the same line |
| **L15** | **An aggregate built by `T{...}` in an expression** - `Pass(PassInfo{"forward-values", kFlow, 0, 0, 0})` is "expected a type". F24 landed list-initialisation for a class with constructors; an aggregate's braces as an expression are not read (probe `aggr.cpp`: clang `forward 1`). | `OptPipeline.cpp:19-184`, 16 sites, one file |
| **L16** | **A class template's parameter pack read inside one of its member templates is empty - silently.** `template <class R, class... Args> struct Fn { template <class F> Fn(F) : v(Box<Args...>::n()) {} }` prints `0` where clang prints `2` (probe `pack2.cpp`); the same pack in a non-template member, or through a class-scope typedef, is right (`pack1`, `pack3`). Loud where the empty pack reaches a class with a required member (`pack6`: `Ops<int,{}>` has no `call`). `instantiateMemberTemplate` records the class's parameters and bindings for the second layer and not its packs. **Not in `src/` today - it is the shape `std::function`'s `template <class F> function(F)` needs (design, section 5), and a miscompile that wants a case whether or not std::function lands.** | 0 files of `src/`; blocks the std::function header |
| **L17** | **Placement-new value-initialisation of a scoped enumeration**, `new (p) T()` with `T` an `enum class`, is "'int' does not convert to 'enum class Kind'"; `Kind k = Kind();` is fine (probe `enumclass2.cpp`). It is `include/vector:192`'s `resize`, so **no `std::vector<enum class>` can be resized**. | `Arm64Darwin.cpp`, under L2/L3; any `vector<Kind>` |
| **L18** | **List-assignment to a vector**, `n.kids = { e, depAssignment(angle) };` - "expected an expression" (probe `listassign.cpp`: clang `4 5`). | `ParserDependent.cpp:50` |
| L3b | A comma inside a template-id inside a default argument is read as the parameter separator (`defarg.cpp`). | `Preprocessor.h:15` once L3's "write the value" is applied there |

Two layer-4 sites were not reproduced in isolation and are recorded as sites to look at, not roots:
`MirWebs.cpp:126` (`copy(r)` resolves to `std::copy`, where a local lambda of that name in a probe
does not - `copyname.cpp` passes) and `ParserConst.cpp:102` / `ParserType.cpp:432` (a scoped
enumeration nested in `Parser` in a `?:` and in `push_back`; the free-standing shape `scoped.cpp`
passes). `Optimizer.cpp:13`'s message `'unique_ptr<const class opt::Costs>::Costs>'` is B4 (a
`unique_ptr<Costs>` into a `unique_ptr<const Costs>`) with a garbled constructor key in the wording.

## 3. Library roots: the document's six, and twelve more

B1-B6 are confirmed (B1 in 5 files: `Mangle.cpp` alone has 6 sites). Found behind them:

| # | missing | first site | how found |
| --- | --- | --- | --- |
| B7 | `ostringstream::tellp` | `Arm64Darwin.h:75`, `Tms6747.h:184` | layer 1; also by clang over cxx3's own `<sstream>` |
| B8 | `ostringstream::swap` | `Tms6747.cpp:510` | layer 2 |
| B9 | `map::at` | `Inliner.cpp:173` | layer 0 (in the data, not in the document) |
| B10 | `map::rbegin`/`rend` | `OptAlign.cpp:136` | layer 3 |
| B11 | `set`'s `initializer_list` constructor | `OptJumps.cpp:173` (`static const std::set<std::string> tests = {...}`) | layer 3 |
| B12 | `std::fill`, `std::unique`, `std::next` | `MirAlloc.cpp:249`, `OptDataflow.cpp:28`, `OptHoist.cpp:165`, `OptValues.cpp:578` | layer 3 (B6 named `fill` only) |
| B13 | `shared_ptr` compared with `nullptr` | `ParserInit.cpp:458` | layer 3; L5's sibling |
| B14 | `unique_ptr<const T>` from `unique_ptr<T>` | `Optimizer.cpp:13` | layer 3; B4's const form |
| L5 | `unique_ptr` compared with `nullptr` (reclassified from the language list) | `Ast.h:359`, `:549` | - |
| S3 | `lib/stdlib.h` lacks `_MAX_PATH` and `_fullpath` under `_WIN32` | `main.cpp:17`, `Preprocessor.cpp:14` | layer 0 (in the data, not in the document) |

`<regex>`: agree that rewriting `A64Peep.cpp`'s patterns is the cheaper answer; the design (R5.3)
already says `<regex>` leaves the source. `<deque>` likewise.

## 4. Where I disagree about the order, and why

The document's order is: L1, the headers, {L2, L3, L5, L8}, {B, S1}, {L4, L6, L7, L9-L12}. Three
things change it.

**L7 is not local.** It is in `OptFlow.h`, one line from L14, and at layer 4 it is the single most
frequent error in the tree (18 files). Its fix is one rule in one place - the closure's reference
member takes the captured entity's declared type, cv included - and until it lands nothing in
`src/optimizer/` compiles whatever is done about std::function.

**L9 and L13 are one wall in front of the parser**, and there is a two-line source alternative:
`qualifyForLookup`, `enclosingNamespaceKey` and `refuseInlineAmbiguity` take a pointer to one of
five const predicates (`hasTypeNamed`, `hasEnumNamed`, `hasGlobalNamed`, `hasFunctionNamed`,
`sameTypedef`); an enumeration selecting the predicate, or a small functor, removes both refusals
from the parser's path at eleven call sites. Doing L9 and L13 as language features is right
eventually (both are valid C++11, both are "owed" under R3.3), but the parser - 16 files, the
largest cluster after the optimizer - should not wait on two features when the source can step
round them in an afternoon. The same argument closes L2 today (delete three lines, add
`-Wno-overloaded-virtual` to the clang flags) and L4's array sites (`= {}` is exact).

**L1 should not start before its dependencies are known**, and they were not: the function type
has to be *matched and deduced through*, not only parsed (`erase3.cpp`: `F<R (*)(A)>` with no pack
is refused "'R' is never used in this specialization's arguments"; `arity.cpp`: a pack in a
function pointer's parameter list does not deduce), and the header needs L16 fixed, since every
route round it is itself refused (design, section 5). L1 remains first by reach, but its estimate
is three parser/deducer changes plus the mangler, not one.

**My order:**

1. **The two measurement fixes**: `inTemplateArgs_` into `DeclarationSnapshot`; and the stand-in
   method of section 2 kept as a script beside `self-compile.cmd`, so each layer is one run rather
   than one round on the box.
2. **The source changes that cost nothing and free whole classes**: L2 (delete three lines), L4's
   two array sites (`= {}`), L8 (`*static_cast<LineFields *>(this) =`), L9/L13 (an enumerated
   predicate), L12's one statement, L6's one line, L11's one call, S1, S3. Each is a `src/` edit a
   later language fix does not have to undo, and together they unblock the parser and the C6000
   files. Record each as a *workaround for an owed feature* in `docs/EXCLUSIONS.md` so the feature
   is not forgotten.
3. **L7**, the capture rule - one place, 18 files.
4. **L1 with L16 and the deducer**, and the `<functional>` of the design. This is the long pole and
   the parser, optimizer and backends all wait on it; everything above it can land first so that
   when it does, the next run shows the real next layer rather than the four this review had to
   simulate.
5. **L14, L15, L3 (+L3b), L5/B13, L17, L18** - each a header or a 16-site file.
6. **The headers in section 1 of the document** (`<unordered_map>`, `<mutex>`, `<atomic>`,
   `<thread>`, `<chrono>`, `<cerrno>`) and **B1-B14**. These are independent of everything above
   and can be written in parallel by whoever is not on L1; they are listed after it only because
   none of them blocks a *class*.
7. **L10**, and measure again - with the two measurement fixes in, on the box.

## 5. Where I agree, said plainly

The method is right: errors grouped by normalised message, each traced to the first failing
declaration, headers counted once however many units include them, and "most of the errors are
knock-ons" is true. L1's reach and first-place ranking, L3's and L5's counts, L2's reach, L6,
L10, L11, L12's diagnosis ("the refusal is right, the choice is the fault"), B1-B6, S1, S2 and the
note on `<regex>` are all correct as written. The two hosts agreeing (1,611 against 1,609) is the
right check to have made, and it is what let this review run the next layers on the Mac. What the
document underestimates is only how much lies one layer down; the fix for that is the method of
section 2, run before each round rather than after it.
