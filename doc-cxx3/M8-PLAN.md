# M8: template substitution in place of token replay, VM `--run`, `--health` on the typed tree

Written 2026-10-26 by Fable 5.1, from `12dc8dc` (`m3-line`, M3b.1 and M7 merged), branch `m8-plan`. Design
section 12's M8 row - *Template substitution replaces token replay where the suite allows, and replay is
deleted; VM `--run` and VM reference test; `--health` on the typed tree* (R4.5, R4.6) - and sections 3.4, 7 and 9
say what M8 is. This note is in the shape of `M7-PLAN.md`: what a commit may change, how each step is judged,
and a "Where it stands" section the implementer appends to as steps land. It is a plan and not a measurement:
no number below was taken for it beyond the line counts and the sites named; every "today" is what the source
says at `12dc8dc`. **M8 begins after M3b.7**: the design's row keeps replay "inside the new parser until M8" and
M3's deletion commit removes the parser-to-`Ast.h` path first, so M8's substitution writes the typed tree and
M3's lowering reads it - there is no second consumer to keep level. Where M5 and M6 have not landed when M8
starts, arm64-darwin and tms6747 are judged by their own emission only (`emit.sh`'s rows), as M3 judged them.

## What replay is today, site by site

A template's definition is **not parsed when it is read**: `Parser::templateDeclaration` (`ParserTemplate.cpp:402`)
reads the parameter list, learns what the declaration declares (`templatedName`, `:175`, binding the parameters to
themselves), records token indices - `TemplateDecl::start`, `afterParams`, `aliasAt`, a `Partial::bodyAt`, an
`OutOfLine::start` (`Entities.h:217-300`) - and steps over the rest (`skipTemplatedDefinition`, `:367`). Every
instantiation is a **re-entry of the parser at one of those indices with the parameters bound**
(`Templates::bindTemplateParameters`, `Templates.cpp:37`: a type parameter as a type name, a non-type one as an
enumerator carrying its declared type, a pack as a `PackBinding`), through five doors:

| door | where | what it re-reads |
| --- | --- | --- |
| a class specialization | `Templates::instantiateClass`, `Templates.cpp:1336` - `p_.at_ = partial ? bodyAt : afterParams`, then the class production | the class head and body; an alias template's type-id from `aliasAt` |
| a function specialization, a class's used members | `Templates::instantiatePending`, `:305` - a fixed point over `specializations_`; `p_.at_ = sp.start; topLevel()` for a function, `replayInlineBodies(now)` for held member bodies, `p_.at_ = outOfLine[k].start; topLevel()` for out-of-line members, each gated by `Signature::used` or `usedStaticMembers_` ([temp.inst]/2-3) | whole definitions, qualifier included |
| a member template | `Templates::instantiateMemberTemplate`, `:1506` - two binding layers (the class's, then the member's), a pattern read for the Itanium name, then `replayInlineBodies(one)` synchronously | the member's declaration and body |
| a candidate for deduction | `Templates::instantiateViableTemplates`, `:1583`/`:1635`, and `Parser::instantiate` - under a `Parser::Trial` (`Parser.h:221`), so a failure is a dropped candidate (SFINAE) unless it names a refusal, which re-raises | the declaration, through `readTemplateDeclaration` (`ParserTemplate.cpp:252`), twice: as a pattern (Itanium spells `T_`) and substituted (Microsoft spells the result) |
| a constexpr member wanted by a constant | `Constexpr.cpp:464` (`instantiateForConstant`) - `replayInlineBodies` of one held body ahead of the fixed point | the body, so `fold` has something to run |

Under each door the body is lexed once and **parsed once per instantiation**, into fresh syntax nodes (the
typed tree's census counts a specialization's body as a source body with `fromTemplate`, `Typing.h:125`), with
every name looked up at the instantiation. Two things already stand outside replay and are what substitution
grows from: `ParserDependent.cpp` reads an expression over dependent operands as a `DepExpr` tree, untyped,
for Itanium's `X...E` spelling (expression SFINAE, 1.15); and the pattern read of a signature binds a
parameter to itself (`patternOnly_`, `nonTypePatternParams_`), which is a dependent read of a declaration.
`Parser::replayInlineBodies` (`Parser.cpp:694`) is also the road for three things that are not templates - a
member body held until its class closes (`ParserType.cpp:1585`), a lambda's call operator
(`ParserExprLambda.cpp:477`), a local class's members. Those read their tokens **once**, later; they are a
parse order, not a replay, and M8 leaves them (decided below).

## What the design asks, and where it is unclear

**3.4's end state**: a template body is parsed once into syntax; Sema instantiates it by substitution on the
syntax tree and keeps an instantiation stack, so a diagnostic in an instantiation shows the chain; non-dependent
names are checked at the definition and dependent names at instantiation; SFINAE is a failed substitution. Two
properties of replay end with it - *SFINAE is a trial re-parse* and *bodies are not checked at the definition*.

**7, the VM**: compilerpp's stack machine (`RIDE/projects/compilerpp`: `Bytecode.h` 255 lines, `VM.cpp` 933,
`VM.h` 125), fed from the SSA IR; runs whole programs for `--run` with natives for C library calls; its word
width and layout from the selected target - a change to the VM, whose stack and memory are an 8-byte word today;
one `std::vector<std::uint8_t>` of memory, the 50-million-step cap. **The VM reference test** is one sentence:
"`--run` output must equal the target program's", and section 11's gate table repeats it: *identical*. What is
not said, and is taken here as the plan's reading rather than invented as the design's: the programs are
`tests/cases`' runnable ones, the comparison is stdout and exit status against the `.expected` and the
host-built program, and the layout is the host target's. **Open question 1**: which features the VM is required
to run - exceptions through SSA's `invoke` and pads, RTTI, `thread_local`, varargs, dynamic initialisation,
`atexit` - and which natives; the design names only "C library calls". **Open question 2**: whether `--run` takes
a program of several translation units (`<case>.part.cpp`) and links in the VM, or one unit. Both are decided
at M8.7 by measuring what the cases need and recorded in "Where it stands", not here.

**9, `--health`**: cppparse's analyser rules (`cpp/cppparse/analyzer/report/Health.hpp`: codes E100-W165, "strict
C++98", over a linked `CodeBase` of a whole project) run on the typed tree, which has its `#if` evaluated;
reports in text, HTML, PDF and SVG as cppparse writes them (`Report.cpp`, `Html.cpp`, `Pdf.cpp`, `Diagram.cpp`).
**Open question 3**: about half of cppparse's codes (E110, E120-E124, E130-E136, E142-E145, E156, E160-E163) are
things a compiler refuses, and cxx3 refuses them with its own diagnostics before any health pass could run;
which rows become health findings and which are the compiler's errors re-labelled is not said. **Open question
4**: R1.2 names "`--health` findings on the examples" as a measure "at or above its target value", and no target
value is stated anywhere in the design. **Open question 5**: cppparse's reports read a project directory
(`ProjectReader`); cxx3 compiles the units named on its command line, and whether `--health` over several inputs
is one report or one per unit is not said. Each is decided at M8.8 and recorded.

## Where replay and substitution differ in what a program sees

These are the shapes "where the suite allows" is about, and each is decided now so the implementer measures
rather than argues:

| shape | under replay | under substitution | M8 |
| --- | --- | --- | --- |
| a non-dependent name declared after the template (`tests/open/two-phase-lookup-late-name.cpp`, `docs/CONFORMANCE.md` "A name declared after a template is found inside it") | found at the instantiation; prints 7 | [temp.res]/9: refused at the definition, as clang refuses it | **substitution's answer**: the open entry closes to `tests/cases` with its `.error`, the CONFORMANCE entry is retired, `tools/feature-sweep`'s `two-phase-lookup` row records the change, and CLAUDE.md's "Decisions already taken" gains a dated line saying the rung-5 decision is reversed (the revision rule: recorded, not rewritten) |
| a dependent call that ADL alone must find ([temp.dep.candidate]) | unqualified lookup at the instantiation finds any function in scope | only ADL at instantiation | substitution's; a case in `tests/cases` that depended on the wider lookup is judged against clang and rewritten if clang refuses it |
| an ill-formed body of a template never instantiated | compiles: the body is never read | checked at the definition for its non-dependent part; refused where clang refuses | substitution's, from M8.5 on; the cases that break are found by `run.sh`, each read against clang |
| a diagnostic inside an instantiation | one place, with "in the instantiation of ..., asked for at ..." for a member template (1.5) | the chain, every level | substitution's; the text of the existing refusals is kept where `tools/exclusions` cites them |
| a refusal naming a feature inside a trial | re-raised, never a substitution failure (1.5's rule) | the same rule, on the substitution | **kept**: a refusal is an answer, not a no |
| `typename` and `template` disambiguators | read and dropped | needed, [temp.res]/3-5 - cxx3 already requires `typename` for `X::type` as a type (A27) | the syntax read of M8.1 keeps both as node payload; a body a conforming compiler needs them in and cxx3 took without is a case to measure |
| the instantiation depth bound (`kMaxNesting`, `Specialization::depth`) | bodies replayed after their asker, the depth carried | the same bound on the substitution stack | kept, the same message |

Nothing else may differ: the names (Itanium from the pattern, Microsoft from the substituted signature), the
on-use gates, vague linkage, partial ordering, the pack rules, the `>>` split - all are held by the gates below.

## What a commit may change, and how it is judged

M8.1-M8.5 change nothing emitted unless `-templates=subst` is given; M8.6 changes nothing at all; M8.7 and
M8.8 add a mode and a report and change no emission. Every commit is judged by the rows that apply:

| gate | where | what must hold |
| --- | --- | --- |
| `tools/identical.sh REF NEW` | Mac | REF a build of the base M8.0 names; every case and the 16 Compiler++ units at -O0/-O1/-O2, both Windows spellings and x86_64-linux: **0 differ** bar the known path diagnostics; from M8.2 the same run with `-templates=subst` on NEW, the shapes the step covers |
| `tests/emit.sh` against the golden recorded at M8.0 | Mac, Linux | 0 changed; arm64-darwin and tms6747 0 at every step |
| `make test` | Mac, Linux (g++ 11.5, `-Werror`) | run, emit, names (against clang: the manglings are the pattern's and must not move), overload (SFINAE and partial ordering), syntax, typed (the census: `functions = bodies + synthesised`, `untyped=` not growing), ssa (every lowered body verified), coff, c90, win-structure, exclusions 0/0, comment cap 0, memory gate held; from M8.7 `tests/vm.sh`, from M8.8 `tests/health.sh` |
| `tools/self-host`, `tools/self-host out -O2` | Mac, Linux | stage 1 = stage 2 at -O0 and -O2 at every step, under the flag from M8.2 (cxx3's own source is the largest template corpus the suite has: `<vector>`, `<map>`, `<string>`, `std::function`) |
| `tools/feature-sweep --clang clang++` | Mac | the three counts recorded at M8.0 and unchanged bar the rows this plan names |
| `tests/open.sh` | Mac | 11 open at M8.0; `two-phase-lookup-late-name` closed at M8.5 and moved |
| Windows rows | the Wine stand-in on the Linux box (`tools/wine-cases.sh`), -O0 and -O2, both spellings; **the box's rows are owed** - no Windows box answers this round, so every Windows number is written "stand-in" | the stand-in's baseline less Wine's two unwinder cases |
| compile time | Mac, Linux | the self-host corpus's wall time at -O0 and -O2, best of three, at M8.0 and at M8.6 - recorded (R1.2's measure); substitution over syntax is expected faster than re-parsing tokens, and a slower number is a finding |

A commit stays local; the coordinator merges. M8's edits stay in `src/sema/Templates.*`, `src/sema/Typing.*`,
`src/syntax/`, `src/parser/ParserTemplate.cpp` and `ParserDependent.cpp`, `src/ir/Lowering.*` where the
substituted body is lowered, the driver's flags, `src/vm/` (new), `src/sema/Checks.*` (new) and `tests/`.

## Steps

| step | what | gate |
| --- | --- | --- |
| M8.0 | **The plan, the REF, the golden, the census.** This note; REF built at the base; the golden recorded; `-check=templates` counting, per unit, the instantiations each door makes (class, function, member template, out-of-line member, alias, constexpr-ahead) so every later step can say what fraction left replay; `tests/open.sh`, `feature-sweep` and the self-host compile time recorded as the baseline | REF and golden exist; the census over the host cases and the self-host corpus recorded |
| M8.1 | **The template body in the syntax tree, read once.** `templateDeclaration` parses the definition it steps over today into syntax at the declaration, the parameters bound to themselves - the pattern read extended from signatures to bodies - with a dependent name marked on its `Name` node and `typename`/`template` kept as payload; `-dump=syntax` shows each template body once; replay is untouched and still reads the tokens. Partial specializations, out-of-line members, alias type-ids and member templates each get their syntax the same way | `identical.sh` 0; golden 0; `make test` green; `syntax.sh` over the templates; `-check=typed`'s census unchanged. **A stop**: Fable reads the dependent-name marking before M8.2 builds on it |
| M8.2 | **Substitution for function templates.** Its commit deletes M8.1-early's stand-in whole - `Templates::substituteFunction` and `substituted`, `Typer::typeBody`, `TypedTree::retype`, the sibling-root insertion in `Typer::census`, and the `tests/subst.sh` steps that compare on against off (Q6, the user's). `Templates::instantiate` for a free function template builds the typed body from the syntax body under a `Binding` (type → `TypeId`, non-type → value, pack → list), Sema's Typer run with that environment, then M3's lowering; the instantiation stack is the binding stack; behind `-templates=subst`, a shape the substitution does not cover falls back to replay by name, counted by `-check=templates`. The Itanium pattern signature is derived from the syntax signature, not re-read | `identical.sh` replay against subst: 0 differ on the covered shapes; names.sh 0 differ; overload.sh green; self-host under the flag |
| M8.3 | **Class templates.** `instantiateClass` by substitution of the class body's syntax through `Classes` (layout, specials, vtables as before); held member bodies and out-of-line members on use ([temp.inst]/2), static data members on use (/3), partial specializations (the pattern match is unchanged, the body substituted), alias templates, nested classes of a specialization, the injected class name | the same rows; `template-*`, `tuple`, `include-*` cases under the flag on Mac, Linux and the stand-in |
| M8.4 | **The rest of the doors.** Member and constructor templates (two binding layers), pack expansion as a node's expansion rather than a token copy (`bindPatternMember` and the three `expandPack*` retire), SFINAE as a failed substitution with the refusal re-raise kept, `ParserDependent`'s `DepExpr` unified with the dependent syntax, default template arguments, `instantiateForConstant` folding over the substituted body, `deduceAuto` reading its initialiser once | overload.sh 44/0 and the `sfinae-*` rows of feature-sweep; `-check=templates` reads 0 falling back over the host cases and the self-host corpus |
| M8.5 | **Two-phase lookup, and bodies checked at the definition.** Non-dependent names bound when the body is read at M8.1, dependent names at substitution; the table above applied, each case `run.sh` turns red read against clang and rewritten or re-recorded with its reason; the open entry closed; CONFORMANCE and CLAUDE.md amended | run.sh green on every box; `tests/open.sh` 10 open; feature-sweep's `two-phase-lookup` row refused as clang refuses it, the other counts unchanged |
| M8.6 | **The deletion**, one commit: `-templates=subst` the only path; `skipTemplatedDefinition`, the token indices on `TemplateDecl`, `Partial::bodyAt`, `OutOfLine::start`, `Specialization::start` and `bodies`, the `p_.at_ =` re-entries and `replayInlineBodies` calls in `Templates.cpp` and `Constexpr.cpp`, `Parser::Trial`'s template uses (the cast disambiguation's stays), `bindTemplateParameters`'s shadowing of the type-name and enumerator tables. `readTemplateDeclaration` goes with them. **A stop** before it: Fable reads M8.5's numbers | `identical.sh` against the M8.5 binary **0 differ at every level**; `make test` green; self-host on every box; memory gate down; exclusions 0/0 after the refusal sites moved; `wc -l` of `ParserTemplate.cpp` recorded |
| M8.7 | **VM `--run` and the reference test.** compilerpp's VM as `src/vm/`, fed from `ir::Module` after lowering (the -O0 SSA, no MIR), its word and layout from the target (`Target`), natives for the C library the cases call; `cxx3 -run file.cpp`; `tests/vm.sh`: every runnable host case, `-run`'s stdout and exit status equal the `.expected` and the host-built program's, the cases the VM cannot run named by reason (open questions 1 and 2 answered there). The 50-million-step cap kept | `tests/vm.sh` counts recorded and in `make test`; the VM's own source under the memory gate and the comment cap; `-dump=ssa` unchanged |
| M8.8 | **`--health` on the typed tree.** `sema::Checks` runs cppparse's rules over the typed tree after Sema, under `-keep-going`; open question 3 settled by measuring each code against what cxx3 already refuses and recording the split; text first, then HTML, PDF and SVG in cppparse's shapes; `tests/health.sh` over GRIDE's cppparse example and `examples/`, the findings recorded as R1.2's measure (open question 4: the first number *is* the baseline, no target being stated) | the report agrees with cppparse's on the shared rows for its own example; `make test` green with `health.sh` |
| M8.9 | **The close.** Design 3.4's "until then" clause marked ended by date; CLAUDE.md's rung-5 decision amended and "The parser is twelve files" brought to what is true; README; the handover with every open question's answer | every row green; the measures table filled |

What M8 does **not** do, said now: the held inline member bodies, lambdas and local classes keep
`replayInlineBodies` under a truer name - they read their tokens once and the design's row names template
replay alone; moving them is a parse-order change with no suite to allow it and belongs to no milestone yet.
The VM is not used for constexpr (3.5 and the Fable review's blocker); `Constexpr` stays the tree evaluator.
`--health`'s PDF and SVG writers are ported, not called out to cppparse, so the compiler stays one executable.

## What must not change, and what looks wrong but is load-bearing

- **Two spellings of one signature.** Itanium is spelled from the pattern (`T_`, `XT_E`, `Dt...E`) and Microsoft
  from the substituted signature; `names.sh` holds both. Substitution must keep the pattern signature as a
  product of the syntax, not re-derive it from the substituted one, which has forgotten where each type came from.
- **A candidate never chosen gets no body**, a class's member is defined on use, a static member on an evaluated
  mention: `Signature::used`, `memberIsUsed`, `usedStaticMembers_`, `pruneUnchosenCandidates`. The fixed point of
  `instantiatePending` is what delivers them; its order goes, its gates stay.
- **A refusal inside a trial re-raises** (1.5): the three `Trial` uses in `Templates.cpp` become failed
  substitutions, and the rule travels with them. The C-style cast's trial (`ParserOperator.cpp`) is grammar and stays.
- **Instantiated definitions have vague linkage** (`replayingInline_`), and a specialization is not a member of
  whatever class asked for it (`classStack_` swapped out at `instantiateClass`); a body reads names in the
  namespace it was declared in (`decl.ns`, `namespacePathStack`).
- **The emit golden and `identical.sh` are the whole evidence that substitution equals replay.** Neither reads
  the syntax or typed tree; `typed.sh`'s census and `ssa.sh`'s verifier are what see the tree, and both must be
  run under the flag from M8.2 on.
- **A pack is bound to a list**, type or value, and `sizeof...`, `rest...` and a partial's tail `R...` read it;
  `Packs` indexed like the binding is the shape every door shares.
- **The depth bound and the `>>` split** (`angleSplit_`, one slot) are state the syntax read of M8.1 must still
  save and restore where a trial rewinds (`Tree::kill`, `revive`); M2.15's revive rule - a body defined inside
  a discarded reading is the tree's if it was emitted - applies to a template body read once as well.

## Where it stands

*(Appended as each step lands: the commit, the gate rows with their numbers on the boxes they were measured on,
the `-check=templates` census, and what was found and not mended, registered in `tests/open/` with the oracle's
answer. Nothing above this line is edited; a correction is recorded below with its date.)*

**M8.0** - not started. The five open questions above are answered here when M8.7 and M8.8 land.

**M8.0, the measure, 2026-11-15** (lane m8a, branch `m8-a` from main `3b8a80fb`; M8 started early on one small piece,
ahead of this plan's order - the REF, the golden and `-check=templates` of the M8.0 row above are still owed, and the
line "not started" stands corrected by this entry). The counter is `Templates::count` at each door, printed once per
unit by `CXX1_TEMPLATE_CENSUS=1` as `template-census: ...` on stderr (nothing otherwise). Measured on the Windows box
with the cl-built compiler, `-S` at -O0, one line a unit, summed (`boxkit/m8a-win4.sh census`, `self`):

| corpus | function bodies | class bodies | members of a class template | member templates | dependent bases | SFINAE trials (failed) | alias ids | constexpr ahead |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tests/cases`, 1224 cases, x86_64-windows (868 reach the census; the rest are refused or hold no parse to its end) | 787 | 2026 | 3958 | 184 | 56 | 1505 (21) | 24 | 32 |
| `tests/cases`, x86_64-linux (887 reach it) | 787 | 2026 | 3958 | 184 | 56 | 1505 (21) | 24 | 32 |
| the self-host, cxx3's 103 sources as `tools/self-host` compiles them, x86_64-windows (100 compile; `C6xPack`, `C6xPipe`, `C6xSelect` are refused by main's own build too - "no function called 'c6x::crossings' takes these 2 argument(s)") | 4833 | 63680 | 51984 | 1399 | 188 | 35105 (329) | 0 | 564 |

A function body is one `topLevel` replay of a function specialization; a class body one replay of a class head and
body; a member one held or out-of-line member body replayed on use; a dependent base a class body whose head names a
parameter after its `:`; a trial one `Parser::Trial` around a candidate's instantiation (the two in `Templates.cpp`).
What the numbers say: **M8 is the class doors** - in the compiler's own source 95% of replays are class bodies and
their members (115,664 of 121,896 bodies), 35,105 trials stand behind 6,232 function and member-template bodies, and
the function door this lane opened is 4% of it. `tests/run.sh`'s other suites read the same `tests/cases`; `tests/open`
and the C90 corpus were not measured.

**M8.1-early, the first substitution, 2026-11-15** (the same lane; not this plan's M8.1 - the syntax read at the
declaration is still owed, and this step stands in for a piece of M8.2). Under `CXX1_TEMPLATE_SUBST=1` a function
specialization is not replayed where `Templates::substituteFunction` takes it: its `Function` is made from a
**replayed sibling's** - the same template, already read - with every type passed through `Templates::substituted`
(the sibling's arguments to its own, under any pointer, reference, const or function declarator), and the Typer types
the sibling's syntax root for it and retypes the nodes and records (`Typer::typeBody`, `TypedTree::retype`); the
lowering sees a typed body like any other. The pattern is a sibling and not a body read once at the declaration, so the
first specialization of each layout still replays - that is what M8.1's syntax read replaces. **The subset**, read off
the template's own tokens so that every type in the body is a parameter's: type parameters only (no pack, non-type or
template parameter), every argument an unqualified arithmetic type, the sibling's arguments pairwise distinct and of
the same size, alignment and floating-ness, and a declaration and body made of the parameters, `const`, `void`,
declarators, names the template itself declares, `=`, unary `*` and `&` and `return` - no literal, call, cast, binary
operator or other name, so no lookup, dependent or not. Everything else **falls back to replay by name**, counted
(`template-replayed (N): <reason>`); the C6000 is refused whole, its walker still reading the Ast body a substituted
function does not have. The census row (`by-substitution` against `function`, the start of the ratchet):

| corpus | by substitution | by replay | the largest reasons for replay |
| --- | --- | --- | --- |
| `tests/cases` | 5 (all in the probe, `template-subst.cpp`) | 782 | 330 an argument that is not arithmetic, 155 a parameter that is not a type, 51 `typename`, 44 `inline`, 40 `int`, 36 `auto`, 22 no sibling of the layout |
| the self-host | 0 | 4833 | 4112 an argument that is not arithmetic, 642 `inline`, 20 a lookup, 19 `if`, 19 `bool`, 19 `&&` |

Gates, on the boxes named (Mac: none): **switch off**, the tree at `f2dcccdf` against main `3b8a80fb`'s cl build
(`work\w16g`) on the Windows box, every case's `-S` text and diagnostics at -O0 and -O2 for x86_64-windows `-masm=masm`,
`-masm=gnu` and x86_64-linux, 1224 cases, 7,344 comparisons - **7,338 the same, 6 differ**, all six the `#error` caret of two refused cases (`posix-headers` on the Windows spellings, `windows-h-layout` on Linux) standing one column apart under a tree path one letter shorter, the known path diagnostic; **switch on against off**, the same grid - **7,344 the same, 0 differ** (the first pass flagged 378 cases through CRLF in their diagnostics alone; `boxkit/m8a-win3.sh again` re-ran them normalised); the self-host's 100 sources on
against off, 99 the same and `Driver.cpp` differing by its unpinned `__DATE__`/`__TIME__` alone (not re-run pinned). The
probe `tests/cases/template-subst.cpp` (its `.expected` clang 19.1.5's on the Windows box): on = off in all six, and
the linked program prints clang's output on and off at -O0 and -O2, on Windows at `5c880a04` and on Linux (g++ build,
`-Werror` clean). cl /W4 /WX clean at `5c880a04`; exclusions 129 sites 0 uncited 0 stale (18 citations re-pointed),
comment cap 0, memory gate held (Typing's raw pointers 89), ast-residue not above its baseline, `ide/generate.py
--check` up to date - on the Windows box at `5c880a04`. On the Linux box at `5c880a04`: g++ `-Werror` build clean, the
probe's program prints the same five lines on and off, its `-S` text is identical on against off at -O0 and -O2
natively, and the same five static gates pass (clang is not on that box's path: the `.expected` is the Windows clang's).
**Not done, by name**: `run.sh` under the switch was not run (the Linux lock was the wave gate's; on = off in every
emitted file is what stands for it) while `typed.sh` under it on the Linux box at `5c880a04` reads 861 passed, 0 failed,
1 did not compile for this host; `-check=typed` and `-dump=typed` type a substituted body with its sibling's types (they call
`typeFunction`, not `typeBody`); a substituted function carries no block table for `-g` and an empty Ast body; the
identity grid was run at `f2dcccdf`, two commits before the last (a functor moved between headers, the empty body made
by `oneStatement`, the C6000 refused) - only the probe, cl and the static gates were run again at `5c880a04`.
src/ lines: 94,414 before, 94,615 after.

**M8.1-early held by a gate, and Q6, 2026-11-15** (lane r6f, the M6c review's R109; on main `458f8974`). **Q6 is the
user's decision, not a lane's: the early sibling substitution is KEPT as a stand-in, off by default, and deleted when
M8.2 lands** - M8.2's row above names what its commit removes. Until then it is held, not trusted:
- **The switch is a driver option**, `-templates=subst` (the plan's spelling; `CXX1_TEMPLATE_SUBST=1` the same), and
  **`-g` refuses it** by name - a substituted function has no block table.
- **The subset in the code is the subset in the probe.** `tests/cases/template-subst.cpp` (with `-templates=subst` in
  its `.flags`, so `run.sh` runs it on every gate) now holds every integer size signed and unsigned (`char`, `signed`/
  `unsigned char`, `short`, `int`, `long long`), `float` and `double`, through declarations, `*` and `&` (two levels),
  `=` through a pointer, `const`, and `return` - 16 by substitution. `template-subst-refused.cpp` holds the shapes beside
  it, each replayed by name and counted: **a conversion between two parameters' types** whose sign or floating kind is
  not the sibling's (`U conv(T)` at `<long long, int>` then `<long long, unsigned>`, sign against zero extension; on
  Windows also `<double, float>` then `<long double, float>`) - a new refusal, the review's item 4, since the sibling's
  typed tree keeps the conversion it typed; **a `bool` argument** (a conversion to `bool` is a compare) - new; **a call**
  ("a name the template does not declare: a lookup"); **a literal** ("a token outside the subset: a literal", which
  printed an empty token before). Both `.expected` are clang 19.1.5's on the Windows box; cxx3 prints them on and off at
  -O0 and -O2 in both Windows spellings.
- **`tests/subst.sh`, for `tools/wave-gate` to call** (lane r6w holds the gate; this lane does not edit it): `run.sh`
  and `typed.sh` with the switch at -O0 and -O2; the census over every case with each replay's reason; the two probes'
  counts and reasons and `-g`'s zero; and **on against off** - the `-S` text and diagnostics of every case reaching the
  function door, for x86_64-linux, x86_64-windows and arm64-darwin at -O0 and -O2. It fails on one failed case, one
  count off its record or one differing file.
- **What stays open, by name**: `-check=typed`/`-dump=typed` still type a substituted body with its sibling's types;
  the conversion refusal is by argument, not by reading the body (it refuses `second`, which converts nothing); the
  C6000 is refused by its target's name.

The census row at this tree (`tests/subst.sh census`, switch on, -O0):

| corpus | by substitution | by replay | the largest reasons for replay |
| --- | --- | --- | --- |
| `tests/cases`, x86_64-linux, 865 units reach the census | 17 (16 in `template-subst`, `ident<unsigned>` in `template-subst-refused`) | 812 | 340 an argument that is not arithmetic, 153 a parameter that is not a type, 51 `typename`, 45 `inline`, 40 `int`, 39 no sibling of the layout, 36 `auto`, 22 `decltype`, 16 `bool`, 16 `+`, 13 a lookup; the new refusals: 3 a `bool` argument, 2 a conversion between parameters, 2 a literal |

Gates: on the Linux box (g++ `-Werror` build clean) at this src: **`tests/subst.sh` 0 failed** - `run.sh` with the switch 1222 passed, 0 failed at -O0 and at -O2; `typed.sh` with it 865 passed, 0 failed, 1 not for this host, at both; the probes counted as recorded and `-g` at 0; on against off over the 147 cases reaching the function door, 880 files compared (3 targets, -O0 and -O2), 880 the same, 0 differ; `run.sh` with the switch off 1222 passed, 0 failed; self-compile 103 of 103 (stage 0, -O2); exclusions 0 uncited 0 stale (three Templates.cpp citations re-pointed), comment cap 0, memory gate, ast-residue and `ide/generate.py --check` pass. On the Windows box (cl /W4 /WX clean): both probes print clang's output on and off at -O0 and -O2 in both spellings; winpass -O0 (WINLANES=4) gnu 833 same, 0 differ, 355 refused as recorded, 33 skipped; masm 832 same, 0 differ, 355, 34 - wave 19's 832/831 with the one new case.
src/ lines: 94,210 before, 94,230 after.
**The early substitution, decided by the user, 2026-11-15** (the M6c review's Q6, written by lane r6g). The user:
**keep M8's early sibling substitution as a stand-in, off by default** (`CXX1_TEMPLATE_SUBST` unset; no gate turns it
on except the probe's own on-against-off rows), and **delete it when M8.2 lands** - M8.2's substitution from the body
read once at the declaration replaces `Templates::substituteFunction`'s sibling, `Typer::typeBody`'s retyping of a
sibling's root and `TypedTree::retype` in the same commit, and the census's `by-substitution` row is then M8.2's.
Until then its gaps (run.sh under the switch, `-check=typed` and `-g` of a substituted body) are rows of M6-PLAN's
"Owed at M6c's close".

**M8.1, its commits planned and the first landed, 2026-11-15** (lane m8b, branch `m8-b` from g1 `027251b9` with p7
`219a667c` merged, `ff3e7e1f`; one lane-hour). The row asks for one read at the declaration; what a lane-hour holds is
the measure that read is judged by. **The plan, each commit of identical output** (the first four run no new path
without `-check=templates`):

| commit | what | state |
| --- | --- | --- |
| 1 | **The count.** `syntax::Tree::differs` holds two subtrees against each other - 0 the same, 1 a text differs under the same kinds, 2 a kind or a child count; `Templates::holdBodies` at the function, class-member and member-template doors keeps the first `function` node read for a body (keyed by its declarator's place) and holds every later replay of that body against it; `-check=templates` prints `bodies= held= same= text= shape=` and each body whose replays differ in shape; `tests/subst.sh bodies` sums it over `tests/cases` and compares a case's `.templates` record | **landed, `c1504cb9`, `d3d90a13`** |
| 2 | **The signature at the declaration.** `templatedName`'s pattern read (the parameters bound to themselves, `patternOnly_`) notes the `function` node and its parameters' `var` nodes and keeps them on `TemplateDecl`; a `pattern` mark on `syntax::Node` beside `dead`, so `-dump=syntax` shows the node once and `roots()`, the Typer's census and `adopt` pass it by; the count gains `signature=` (replays whose parameter list agrees) | planned |
| 3 | **A function template's body at the declaration**, under the flag: `skipTemplatedDefinition` replaced there by the function production in pattern mode - a declaration of dependent type makes a `var` and no slot, an expression over a dependent operand goes to `ParserDependent`'s reader (which notes syntax as it builds its `DepExpr`), a non-dependent one to the ordinary productions - into a function state of its own that reaches none of the tables a definition writes (`lookup_.functions_` and `functionIndex_`, `program.functions`, the defined-symbol and discardable-inline sets, static locals, the string pool, `specializations_`); a body the pattern read cannot take is **unread by name**, counted as replay's reasons are; the count holds each replay against the declaration's tree instead of the first replay's | planned |
| 4 | **The class doors**, where 95% of the self-host's replays are: a class template's held member bodies, out-of-line members (`OutOfLine::start`), partial specializations (`Partial::bodyAt`), member templates and an alias's type-id, each read as commit 3 reads a function | planned |
| 5 | **On by default**: the read runs with no flag once `unread` is 0 over `tests/cases` and the self-host and `identical.sh` reads 0; a dependent name marked on its `name` node, `typename`/`template` kept as payload; `-dump=syntax` shows each body once. The stop for Fable's reading | planned |

**What commit 1 measures, and what it does not.** The tree kept is the **first replay's**, not one read at the
declaration - no dependent read of a body exists yet, and the pattern read of a signature writes the function tables
(commit 3 names them). So the count answers the question a single tree stands on: *do the replays of one body build
the same tree?* `same` and `text` are bodies one tree can stand for (`text` is the payload substitution rewrites: a
`var` node spells its type); `shape` is a body whose parse depends on the binding, which a tree read once cannot hold
as it is. Measured on the Windows box, cl build of `c1504cb9`, `-S` at -O0 (`boxkit/m8b-win.sh bodies self`):

| corpus | bodies replayed | first reads | same as the first | text alone | shape | bodies behind the shapes |
| --- | --- | --- | --- | --- | --- | --- |
| `tests/cases`, 876 units reach the count | 5,123 | 3,385 | 682 | 1,004 | 52 (3.0% of the 1,738 compared) | 32 places in 19 cases |
| the self-host, 103 of 103 sources compile under the flag | 60,612 | 11,912 | 16,410 | 31,933 | 357 (0.7% of the 48,700 compared) | 8 bodies |

**The shapes, read** - four families, and M8.2-M8.4 owe each a node that holds it: **a pack** (`f(args...)`,
`sizeof...`, `A &&...a`: the expansion is copied per member - M8.4's row already turns it into one node) - in
`tests/cases` every place in a case's own source, and in the library `__fn_model::call`, `function::operator()`,
`make_shared`, `vector::emplace_back`, with `make_owned` in the compiler's `Ast.h`; **`T()` in an expression**,
`value_type(k, V())` - value-initialisation is a constructor call for a class and a zero for a scalar
(`map::operator[]`, `unordered_map::operator[]`: 41 of the self-host's 73 shape lines); **a direct-initialised
local**, `vector<T> keep(first, last)` (`vector::insert`, 19 lines); **a mem-initialiser**, `first(p.first)`
(`pair`'s converting constructor, 1 line). Not counted: the constexpr-ahead door (`Constexpr.cpp`, 32 and 564 bodies in M8.0's census), a
class's head and member declarations (no `function` node with a block), and an alias's type-id.

**Commit 3 tried and not landed** (`boxkit/m8b-read.patch`, 65 lines, for the lane that takes it): under the flag,
at the unit's end, each defined function template was read from `afterParams` by `topLevel` under a `Trial`, its
parameters bound to themselves and `patternOnly_` set, the function it made popped again. Over `tests/cases` on the
Linux box, 110 templates in the units that survive: **5 read** (4 differing from their first replay in text alone, 1 in
shape), 105 unread - 44 a pack, 23 a non-type parameter, 11 a template template parameter (the three the read does
not bind yet), 20 "has no member" (a member access through a dependent object), 2 a call with a dependent argument, 5
others one each. **And 97 units die in it**: a local or a by-value parameter of dependent type is sized, and the
target's size function prints "no size for this type yet" and exits, outside any trial (`src/backend`, which this lane
may not touch). So the walls commit 3 meets, in order: the frame (a `var` of dependent type must take no slot and ask
no size), dependent member access, dependent calls - then the three parameter kinds. A read that can end the process
is not kept behind a count other gates sum, so it is a patch and not a commit.

**Gates** at `d3d90a13` (the code; this entry and three citations follow it): on the Linux box
(g++ `-Werror` build clean; REF `~/m8bref` built from `git archive ff3e7e1f`): `tools/identical.sh` REF against NEW for
tms6747, arm64-darwin and x86_64-linux at -O0 and -O2, **5,274 compared, 0 differ**; `run.sh` 1228 passed, 0 failed;
`typed.sh` 871 passed, 0 failed, 1 not for this host; `tests/subst.sh bodies` 0 failed (5,119 bodies over 871 units -
the `.error` cases left out - 3,381 / 682 / 1,004 / 52, the two `.templates` records as written); self-compile stage 0
103 of 103; exclusions 129 sites 0 uncited 0 stale (three `Templates.cpp` citations re-pointed), comment cap 0, memory
gate held, ast-residue +0 (`c1504cb9` read node-refs +1 - two `syntax::` references in `holdBodies`; `d3d90a13` names
none), `ide/generate.py --check` up to date. On the Windows box (cl /W4 /WX clean, `work\m8bf`; REF `work\m8bref`):
every case's `-S` text and diagnostics, NEW against REF, at -O0 and -O2 for x86_64-windows `-masm=masm` and `-masm=gnu`,
x86_64-linux, arm64-darwin and tms6747 (`boxkit/m8bf-win.sh same`), 1,234 cases, 12,340 comparisons - **12,328 the
same, 12 differ**, all twelve the `#error` caret of the two refused cases (`posix-headers`, `windows-h-layout`) standing
two columns apart under a tree path two letters shorter, the known path diagnostic (read: `fc` of the two `.err`);
`-check=templates` as tabled above over `tests/cases` and the 103 sources; `self-compile.cmd` 103 of 103. The five static gates pass there too (its ast-residue reads 0 for the four pointer kinds on
that box and is not the measure). **Not run**: `tests/subst.sh`'s other four steps (the stand-in's, untouched),
`names.sh`, `overload.sh`, the -O1 rows and Compiler++'s units of `identical.sh`; no `tests/cases` program was added,
so winpass and the C6000 baselines have nothing new to hold. The Windows box dropped every ssh session once in this
hour (about 22:48 PKT, back in two minutes with no job of any lane left); the Windows rows are from the rerun after it.
src/ lines: 94,348 before, 94,424 after.

**Asked of the user (Q8)**: M8.1's row is five commits, and commits 3 and 4 are each more than a lane-hour (the
dependent read of a body is the pattern read grown from signatures to statements). In order, one lane after another
on `ParserTemplate.cpp` - the recommended option, taken unless told otherwise - or 3 and 4 as two lanes at once?

**M8.1, commits 2 and 3, 2026-11-17** (lane m8d, branch `m8-d` from `7ac21eef` - m8-b `0c6336ce` merged with main
`f20e46ca`; one lane-hour; code `4c45208e`, both steps in the one commit the hour held, gated once). **Commit 2, the
signature at the declaration**: `readTemplateDeclaration`, when `templatedName` asks (`noteSignature_`), keeps what
it read as a `function` node over one `var` a parameter, each spelling its type, on `TemplateDecl::signature`;
`syntax::Node` has a `pattern` mark and `Tree::markPattern` sets it **with `dead`**, so every reader that passes a
dead node by - `roots()`, `adopt`, the dump, the Typer's census - passes a pattern by with no edit of its own, and
only the count reads it. Not done of the row's text: `-dump=syntax` does not show the node (it would re-record
`tests/syntax` by hand; it goes with commit 5, which shows each body once), and a member template's and a class
member's signature are not kept (commit 4's doors). The count gains `declared=` (replays of a function template
whose signature was kept) and `signature=` (those whose leading `var`s are as many as the kept ones). **Commit 3, the
body at the declaration**, from `boxkit/m8b-read.patch`: under `-check=templates` alone, at the unit's end,
`Templates::readDeclarationsOnce` reads each defined function template from `afterParams` by `topLevel` under a
`Trial`, parameters bound to themselves, and holds the tree against the first replay's. What m8b's patch died on is
mended three ways, each reached only under that read: `Type::size` and `align` refuse a dependent type by name (a
`SubstitutionFailure`, `Type::nameDependentSizes`, thread-local) where the target printed and ended the unit;
`Parser::patternBody_` gives a dependent local or parameter no slot and asks no alignment; and a pattern the mangler
refuses takes a placeholder symbol (`Linkage::functionSymbol`). **0 units die** on either box (m8b: 97). It is still
the patch's shape and not the row's: the read is at the unit's end and not in `templateDeclaration`, and it writes
the tables a definition writes (the function is popped from `program.functions`; `lookup_.functions_`, the string
pool and the static locals keep what it left) - so it stays behind the flag, and commit 3 is **not closed**.

| corpus, box | function templates defined | read | not read | read: text alone / shape / same | read, never replayed | `declared=` | `signature=` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `tests/cases`, Linux (875 units, Itanium names) | 3,477 | **750** | 2,727 | 33 / 1 / 0 | 716 | 825 | 796 |
| `tests/cases`, Windows (880 units, Microsoft names) | 3,478 | **77** | 3,401 | 17 / 1 / 0 | 59 | 826 | 797 |
| the self-host, Windows, 103 of 103 compile under the flag | 7,512 | **330** | 7,182 | 68 / 0 / 0 | 262 | 5,029 | 5,007 |

**Not read, by reason** (`tests/cases`, Linux; a quoted name is taken out so one reason is one line): 1,270 the size
of a dependent type (asked somewhere other than a local's slot - the callers were not located in this hour, and the
refusal names none: the next lane's first job); 747 "has no member" (a member access through a
dependent object); 177 a cast of a reference to another type; 138 a parameter pack, 41 a non-type parameter, 11 a
template template parameter (the three kinds the read does not bind); 136 a compound assignment and 68 + 36 + 21 + 2
+ 2 + 1 an operator over a dependent operand; 62 a static member or enumerator of a dependent class; 9 a call no
function takes; 4 a signature's own refusal; 2 undeclared names. On Windows 738 more stop at "no Microsoft linkage
name yet": a call in the body with a dependent argument instantiates the callee, and `Parser::instantiate` asks the
Microsoft mangler for a name over `T` (Itanium spells one, which is why Linux reads ten times as many). The walls in
order, then: dependent calls (no instantiation in a pattern - the 738), the size askers, dependent member access, the operators - `ParserDependent`'s
reader for each - and the three parameter kinds. `same` is 0 because a pattern's `var` spells `T` where a replay
spells `int`: text alone is the agreement a single tree can have, and the one shape is a pack's.

**Gates** at `4c45208e`, one tree, each box built from clean (REF from `git archive 7ac21eef` in its own tree,
Linux `~/m8dref`, Windows `work\m8dref`). Linux (g++ `-Werror` clean): `tools/identical.sh` REF against NEW for
tms6747, arm64-darwin and x86_64-linux at -O0 and -O2, **5,298 compared, 0 differ**; `run.sh` 1232 passed, 0 failed;
`typed.sh` 875 passed, 0 failed, 1 not for this host (untyped by kind as before: other=38 name=8); `tests/subst.sh
bodies` 0 failed (5,119 over 875 units, 3,381 / 682 / 1,004 / 52 - m8b's numbers; the two `.templates` records
re-recorded with the new line and keys); self-compile stage 0 103 of 103; exclusions 129 sites, 0 uncited, 0 stale
(24 citations re-pointed by `boxkit/m8d-repoint.py`); comment cap 0; memory gate held; ast-residue +0 on all seven;
`ide/generate.py --check` up to date. Windows (cl /W4 /WX clean, `work\m8d`): every case's `-S` text and
diagnostics NEW against REF at -O0 and -O2 for x86_64-windows `-masm=masm` and `-masm=gnu`, x86_64-linux,
arm64-darwin and tms6747 (`boxkit/m8d-win.sh same`), 1,238 cases, 12,380 comparisons - **12,368 the same, 12
differ**, the same twelve as m8b's (the `#error` caret of `posix-headers` and `windows-h-layout` under a tree path
three letters shorter); the static gates pass; `-check=templates` as tabled; `self-compile.cmd` 103 of 103. **Not
run**: `tests/subst.sh`'s four stand-in steps, `names.sh`, `overload.sh`, `syntax.sh`, winpass, the -O1 rows and
Compiler++'s units; no `tests/cases` program was added. src/ lines: 94,585 before, 94,712 after.

**What the next lane takes** (commit 3 still open, then 4): move the read from the unit's end into
`templateDeclaration` with a function state that writes no table; no instantiation from a pattern body (the
Microsoft 738); find the size askers behind 1,270; then `ParserDependent`'s reader for member access and the
operators. Commit 2's remainder: the signature of a member template and of a class template's member, and the
dump's line for a pattern.

**M8.1, commit 3 a step further, 2026-11-17** (lane m8e, branch `m8-d` from `c6548881`; one lane-hour; code
`c0af8ca7`). Three of the four things m8d's last paragraph names, and commit 3 is **still not closed**: the read
stays behind `-check=templates`. **(a) The read is in `templateDeclaration`**: `Templates::readDeclaration` reads a
defined function template's body where it is declared, under the same `Trial` and pattern state, and keeps the
tree; the unit's end only holds each kept tree against its first replay (`holdDeclarations`). What it leaves behind
is taken out again, not yet never written: the pattern's `Function` is popped from `program.functions`, its
`Signature` taken out of every overload set of `lookup_.functionIndex_` (the entry itself stays in
`lookup_.functions_`, reached by no name), and its string literals leave the pool as a lambda's trial reading's do
(unless the read emitted a function or a global that may name one). **The measure**: every case's `-S` text with
the flag against without it - the first build read 2 of 1,238 differing (`constructor-unwinds`,
`function-try-block-constructor`: the pool's numbering, a pattern's literal now taken at the declaration and not
last), the committed one **0 of 1,238** (Windows, `boxkit/m8e-win.sh flag`). Not taken out: a static local's
global, default-argument records, a closure's functions when the read fails after making them, and whatever a
non-dependent instantiation the body starts writes (which replay would write later) - the function state that
writes no table is still owed. **(b) No instantiation over a pattern's own parameters**: `Parser::instantiate`
refuses by name a binding that is dependent while a pattern body is read. The Microsoft 738 are gone, and so are
most of Linux's 750 reads - they had been read by instantiating the callee over `T`, which Itanium can name and
Microsoft cannot; the two boxes now read the same bodies. **(c) The size askers found** (gdb at the throw, Linux):
the 1,270 were one line, `Parser::declare`'s debug record asking a by-value dependent parameter's alignment -
mended; the 268 left are `Parser::pointerAdd` (the element size under `T *` arithmetic) and
`Parser::usualArithmetic` (a comparison over a dependent operand), which are (d)'s operators and not slots.
**(d) not started**: `ParserDependent`'s reader builds a `DepExpr` for a signature's mangling and notes no syntax,
and refuses a local name; a body's expression needs it to note the nodes the ordinary productions note and to give
the ordinary reader an expression of dependent type back - more than what the hour had left.

| corpus, box | function templates defined | read | not read | read: text alone / shape / same | read, never replayed |
| --- | --- | --- | --- | --- | --- |
| `tests/cases`, Linux, before (`c6548881`) | 3,477 | 750 | 2,727 | 33 / 1 / 0 | 716 |
| `tests/cases`, Windows, before | 3,478 | 77 | 3,401 | 17 / 1 / 0 | 59 |
| `tests/cases`, Linux and Windows, after (`c0af8ca7`; the same on both) | 3,478 | **128** | 3,350 | 66 / 1 / 0 | 61 |

**Not read, by reason, after** (the same lines on both boxes): 748 "has no member" (a member access through an
object whose class is a pattern); 484 + 295 + 112 + 94 + 37 + 2 + 2 + 1 an operator over a dependent operand; 378
+ 9 a call with a dependent argument (the refusal of (b), most of them met inside overload resolution's trial and
reported as "no specialization of it could be made"); 268 the size of a dependent type (`pointerAdd`,
`usualArithmetic`); 204 a compound assignment; 177 a cast of a reference to another type; 138 a pack, 41 a
non-type, 11 a template template parameter; 68 + 59 a construction from two arguments; 62 a static member or
enumerator of a dependent class; 40 a member of a non-class; 75 a signature's own refusals; 14 + 3 undeclared
names; 8 + 7 forms `ParserDependent` refuses in a signature; 6 + 2 and five more, one or two each. The walls in order for the next lane: an expression of dependent type the ordinary
productions can carry (member access, the operators, calls, casts - one mechanism, `ParserDependent`'s), then the
three parameter kinds; and the function state that writes no table before any of it runs without the flag.

**Gates** at `c0af8ca7`, one tree on each box; REF is lane m8d's kept tree (`4c45208e`, whose `src/` is
`c6548881`'s - Linux `~/m8d`, Windows `work\m8d`), not a new build. Linux (g++ `-Werror` clean, `~/m8e`):
`tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and -O2, **5,298 compared,
0 differ**; `run.sh` 1232 passed, 0 failed; `typed.sh` 875 passed, 0 failed, 1 not for this host (other=38
name=8); `tests/subst.sh bodies` 0 failed (5,119 over 875 units, 3,381 / 682 / 1,004 / 52, the two `.templates`
records as written); self-compile stage 0 103 of 103; exclusions 0 uncited, 0 stale (18 citations re-pointed);
comment cap 0; memory gate held; ast-residue +0 on all seven; `ide/generate.py --check` up to date. Windows (cl
/W4 /WX clean, `work\m8e`): every case's `-S` text and diagnostics NEW against REF at -O0 and -O2 for
x86_64-windows `-masm=masm` and `-masm=gnu`, x86_64-linux, arm64-darwin and tms6747, 1,238 cases, **12,380 the
same, 0 differ** (m8d's twelve `#error` caret lines do not show: the two tree paths are the same length); the
static gates pass; `-check=templates` over the 103 sources, all compiling: 7,512 function templates, **334 read**
(330 before), 70 in text alone, 0 in shape; `self-compile.cmd` 103 of 103.
`tests/names.sh` on that box against its clang: 883 passed, 0 failed; `tests/overload.sh`: 46 agreed with clang,
0 differed (m8d ran neither). **Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1
rows and Compiler++'s units; the flag-against-no-flag measure on Linux; no `tests/cases` program was added. src/
lines: 94,712 before, 94,737 after.

**What the next lane takes** (commit 3 still open, then 4): the expression of dependent type - `ParserDependent`
noting syntax and handing the ordinary reader a node back - for member access, the operators (with `pointerAdd`
and `usualArithmetic`, the last size askers), dependent calls and casts; a function state that writes no table in
place of the taking-out above; then the three parameter kinds. Commit 2's remainder is untouched: a member
template's and a class member's signature, and the dump's line for a pattern.
