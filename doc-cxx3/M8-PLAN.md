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

**M8.1, commit 3: an expression of dependent type, 2026-11-17** (lane m8f, branch `m8-d` from `976f133b`; one
lane-hour; code `05f8d060`). The first of the three things m8e's last paragraph names, and commit 3 is **still
not closed**: the read stays behind `-check=templates`, and what it leaves in the tables is still taken out, not
never written. **The mechanism is the ordinary productions', not a second reader**: in a pattern's body
(`patternBody_`, set by `Templates::readDeclaration` alone) an operand of dependent type is read by the production
that reads any operand, which notes its syntax as it always does, and what it hands on is a zero of a dependent
type - `zeroOf(dependentResult())`, a `Kind::DependentExpr` over one interned node - so the next production meets
a dependent operand again. Three functions in `ParserDependent.cpp` (`dependentOperand` over a type,
`dependentResult`, `dependentPostfix` for a subscript, a member's name and `++`), and one line at each door:
`overloadedBinary` and `overloadedUnary` (every binary and unary operator; with them `pointerAdd` and
`usualArithmetic` are never reached, the 268 size asks), `compound`, `incDec`, `assignValue` and
`requireAssignable`, `contextualScalar`, `checkAssignable`, `static_cast`, a class temporary and `X q(a, b)` of a
pattern class, a member of a pattern class through a template-id, a `return` to a reference, a scope's slot given
back, a callee of dependent type (`finishCall`, its arguments read). **A call with a dependent argument**
resolves nothing: `resolveOverload` answers a signature made of the call's own arguments (symbol `$pattern$...`,
dependent result), and a function template's call returns before deduction (`argsDependent_`, set by
`parseArguments`) - instantiation's to bind, M8.2. `ParserDependent`'s own reader (the signature's `DepExpr`
tree) is as it was: the brief's "through `ParserDependent`" is the file and the stand-in type, not that reader
noting syntax. No `ExprPtr` was added (ast-residue +0): operands are asked by type, results made by `zeroOf`.

| corpus, box | function templates defined | read | not read | read: text alone / shape / same | read, never replayed |
| --- | --- | --- | --- | --- | --- |
| `tests/cases`, Linux and Windows, before (`976f133b`) | 3,478 | 128 | 3,350 | 66 / 1 / 0 | 61 |
| `tests/cases`, Linux and Windows, after (`05f8d060`; the same count on both) | 3,478 | **3,186** | 292 | 413 / 9 / 0 | 2,764 |
| the self-host, Windows, before / after | 7,512 | 334 / **6,742** | 7,178 / 770 | 70 / 0 / 0, then 1,129 / 0 / 0 | - / 5,613 |

**Not read, by reason, after** (Linux): 138 a parameter pack, 41 a non-type parameter, 11 a template template
parameter (the three kinds the read does not bind - 190 of the 292); 59 a `static_assert` the pattern's stand-in
fails (`forward`'s "an rvalue cannot be forwarded as an lvalue": a condition over a dependent type folded, where
it should be kept); 14 + 6 undeclared names; 8 + 7 forms `ParserDependent` refuses in a signature; 2 the size of
a dependent type (`sizeof` of one in a body); 2 + 1 + 1 + 1 + 1 one each. **0 units die** (899 of 1,238 files
print the count, as at `976f133b`). The nine `shape` are named now (`-check=templates declaration shape:`): five
in `template-sfinae-misc`, one each in `trailing-return-template-decltype`, `template-keyword-member`,
`ssa-lvalue-forms`, `callable-members-and-braces` - a pattern's tree that differs from its first replay's in
shape, not looked at in this hour, and owed before the read is trusted without the flag. `same` stays 0 (a
pattern's `var` spells `T`).

**The read against no read** (`-S` with the flag against without, Linux, `boxkit/m8f-flag.sh`): 1,236 of 1,238
the same, **2 differ**, where m8e's tree had 0 - the price of reading 3,186 bodies with a state that still writes
tables: `block-scope-typedef` emits `f<int>` and `f<double>` in the other order, and `regex-ecmascript` emits
`vector<const char *>::size`, which a pattern's body used and no replay does. Both are what the function state
that writes no table ends, and why the flag stays.

**Gates** at `05f8d060`, one tree on each box, each built from clean; REF built fresh from `976f133b` in its own
kept tree (Linux `~/m8fr`, Windows `work\m8fr`; NEW `~/m8fn`, `work\m8fn`). Linux (g++ `-Werror` clean):
`tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and -O2, **5,298 compared,
0 differ**; `run.sh` 1232 passed, 0 failed; `typed.sh` 875 passed, 0 failed, 1 not for this host (other=38
name=8); `tests/subst.sh bodies` 0 failed (5,119 over 875 units, 3,381 / 682 / 1,004 / 52; `template-class`'s
`.templates` record re-recorded - its one body is read now); self-compile stage 0 103 of 103; exclusions 0
uncited, 0 stale (29 citations re-pointed); comment cap 0; memory gate held; ast-residue +0 on all seven;
`ide/generate.py --check` up to date. Windows (cl /W4 /WX clean): every case's `-S` text and diagnostics NEW
against REF at -O0 and -O2 for x86_64-windows `-masm=masm` and `-masm=gnu`, x86_64-linux, arm64-darwin and
tms6747, 1,238 cases, **12,380 the same, 0 differ**; the static gates pass; `-check=templates` over `tests/cases`
reads the same 3,186 of 3,478 (880 files print the count), and over the 103 sources, all compiling, **6,742 of
7,512** (334 before; 1,129 in text alone, 0 in shape; not read: 584 a pack, 101 `forward`'s assertion, 50 a
reference bound to a value, 32 + 1 + 1 undeclared names, 1 a size); `self-compile.cmd` 103 of 103;
`tests/names.sh` against that box's clang 883 passed, 0 failed; `tests/overload.sh` 46 agreed with clang, 0
differed. **Not run**: `tests/subst.sh`'s four
stand-in steps, `syntax.sh`, winpass, the -O1 rows and Compiler++'s units; no `tests/cases` program was added.
src/ lines: 94,737 before, 94,824 after.

**What the next lane takes** (commit 3 still open, then 4): the function state that writes no table (static
locals, default-argument records, a failed read's closures, what a non-dependent use in the body marks or
instantiates - the two cases above are its test), so the taking-out can go; the nine shapes; the three parameter
kinds (190 of the 292 left); `forward`'s `static_assert` over a dependent condition (59). Commit 2's remainder
is untouched: a member template's and a class member's signature, and the dump's line for a pattern.

**M8.1, commit 3: a read that marks nothing, the parameter kinds, the assertion, 2026-11-18** (lane m8g, branch
`m8-d` from `342ffb87`; one lane-hour; code `431cae33`). Four of the things m8f's last paragraph names, and commit
3 is **still not closed: the flag stays** (the last paragraph says why). **(1) The read's two differences are
gone, by not writing rather than by taking out.** A use met in a pattern's own tokens marks nothing
(`patternUses_`, set by `Templates::readDeclaration` alone): `noteUsed` and `markSymbolUsed` return, a call with
written template arguments and a static member's use leave their marks unset - so `regex-ecmascript` no longer
emits the `vector<const char *>::size` a pattern's body named. What the read *asks to be replayed* is no part of
the pattern and marks as any replay does (`Parser::ReplayUses`, three doors: `instantiateClass`, a member template
over no parameter of the pattern, a constexpr member replayed for a constant) - without it a class first
instantiated inside a read would lose the uses its instantiation makes, for good. A function specialization the
read asked for is recorded by key (`askedByPattern_`) and **seated among the replays where the program first asks
for it** (`Templates::madeWhereAsked`: the read's record stays in place, emptied, so no index moves) - so
`block-scope-typedef` emits `f<int>` before `f<double>` again. **`-S` with the flag against without: 2 of 1,238
differ before, 0 after, on Linux and on Windows.** **(3) The three parameter kinds are bound as a pattern binds
them** - `bindTemplateParameters` already had each (a pack of one `Kind::TemplateParam`, a non-type param-ref,
the stand-in `$TT<i>`), and `readDeclaration`'s three refusals were all that kept the read out. **(4) A
`static_assert` whose condition names the pattern's parameters is kept, not folded** (`staticAssertion`: read,
its syntax noted, instantiation's to assert) - `forward`'s 59.

| corpus, box | function templates defined | read | not read | read: text alone / shape / same | read, never replayed |
| --- | --- | --- | --- | --- | --- |
| `tests/cases`, Linux and Windows, before (`342ffb87`) | 3,478 | 3,186 | 292 | 413 / 9 / 0 | 2,764 |
| `tests/cases`, Linux and Windows, after (`431cae33`; the same count on both) | 3,478 | **3,379** | 99 | 452 / 11 / 14 | 2,902 |

**Not read, by reason, after** (Windows): 27 the size of a dependent type (3 before: a pack's or a non-type
parameter's body asking one), 21 a pack pattern `ParserDependent` refuses in a template argument list (`Ns...`
alone, and one beside other arguments), 14 + 6 undeclared names, 9 + 8 forms `ParserDependent` refuses in a
signature, 7 an incomplete type, 2 a literal operator's parameter list over a pack, 2 + 1 + 1 + 1 one each. **0
units die** (880 of 1,238 files print the count on Windows, as before).

**(2) The shapes, examined** - `-check=templates` now prints each one's two trees (`declaration shape: read`,
`replayed`; `Tree::spelled`), eleven with the two the non-type parameters brought. **Eight are the signature, not
the body**: `m25`, `m26`, `m28`, `m30`, `m31` and `m6` of `template-sfinae-misc`, `g7` of
`trailing-return-template-decltype`, `length` of `string-literal-array-reference` - an expression over the
parameters in a trailing `decltype` or an array bound is read by `ParserDependent`'s own reader, which notes its
literals or nothing (`(int "1") (int "2")` where the replay has `(binary "+" (cast "int" (int "1")) ...)`, no
`(name "N")` for a bound) - commit 2's remainder, the signature's expression kept as syntax. **Two are `T{}`**
(`zeroOf` of `callable-members-and-braces`, `braced` of `ssa-lvalue-forms`): a braced temporary of a dependent
type is noted as `(name "T")`, the replay's as `(cast "int" ...)` - the class-temporary door returns its zero
before the cast is noted. **One is a wrong read**: `p->template pick<5>(1)` in `template-keyword-member` is read
as `(p->pick < 5) > 1` - `template` after `->` on a dependent object is not honoured in a pattern. None mended
in this hour; each is owed before the read is trusted without the flag.

**Gates** at `431cae33`, one tree on each box, each built from clean; REF built fresh from `342ffb87` in its own
kept tree (Linux `~/m8gr`, Windows `work\m8gr`; NEW `~/m8gn`, `work\m8gn`). Linux (g++ `-Werror` clean):
`tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and -O2, **5,298 compared,
0 differ**; the flag against no flag, `-S` of every case, **1,238 the same, 0 differ**; `run.sh` 1232 passed, 0
failed; `typed.sh` 875 passed, 0 failed, 1 not for this host (other=38 name=8); `tests/subst.sh bodies` 0 failed
(5,119 over 875 units, 3,381 / 682 / 1,004 / 52; `template-variadic`'s `.templates` record re-recorded - one of
its four is read now, three ask a dependent size); self-compile stage 0 103 of 103; exclusions 0 uncited, 0
stale (17 citations re-pointed); comment cap 0; memory gate held; ast-residue +0 on all seven;
`ide/generate.py --check` up to date; `-check=templates` reads 3,379 of 3,478 (899 files print the count, as
before). Windows (cl /W4 /WX clean): every case's `-S` text and diagnostics NEW
against REF at -O0 and -O2 for x86_64-windows `-masm=masm` and `-masm=gnu`, x86_64-linux, arm64-darwin and
tms6747, 1,238 cases, **12,380 the same, 0 differ**; the flag against no flag **1,238 the same, 0 differ**; the
static gates pass; `-check=templates` reads the same 3,379 of 3,478 (880 files print the count). **Not finished
inside the hour on Windows** (the box was shared three ways; the chain was still running at the hard stop, its
results land in `boxkit/m8g-win-selfc.out` and `boxkit/m8g-win-names.out`): `self-compile.cmd`, `tests/names.sh`,
`tests/overload.sh`, and the self-host's count - the coordinator's to read before this is merged. **Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows and
Compiler++'s units; no `tests/cases` program was added. A Linux build made over build 1's objects segfaulted in
`Parser::parse` (objects newer than the shipped headers - CLAUDE.md's stale-object rule); every number here is
the clean build's. src/ lines: 94,824 before, 94,863 after.

**Whether commit 3 can drop the flag: no, not yet.** What this hour ended is the read's *marks* and the order of
what it asks for; the read still **writes and takes out** - the pattern's `Function` popped, its signature erased
from every overload set, its strings cut back, its syntax killed - and still **leaves**: a class specialization a
non-dependent use in a body instantiates (made earlier than with no read; its place among the replays was not
re-seated as a function's is), the `Signature` and `readTemplateDeclaration`'s effects of a function
specialization asked for, a constexpr member replayed for a constant during a read (emitted then), and whatever a
failed read left (static locals, default-argument records, closures) - none of which changes one byte of 1,238
cases' `-S` on either box, which is the test and not the proof. With that: 99 templates not read, eleven shapes
(one a wrong read), member templates and class members not read at all (commit 2's remainder), and the read's
cost with the flag always on not measured. **What the next lane takes**: the function state proper (the pattern
read into a `Program` and tables of its own, so the four take-outs go), a class specialization seated as a
function's is, the `->template` read and `T{}`'s cast, the signature's expression as syntax (eight shapes, with
commit 2's remainder), then the 99.

**M8.1, commit 3: a pattern read into a program of its own, 2026-11-18** (lane m8h, branch `m8-d` from
`4285d86e`; one lane-hour; code `eb7d4031`). **Lane m8g's Windows chain, read first, all passed** on its final
tree: `self-compile.cmd` 103 of 103, `tests/names.sh` 883 passed 0 failed, `tests/overload.sh` 46 agreed with
clang 0 differed, the self-host under `-check=templates` 103 of 103 with 7,323 of 7,512 function templates read
(189 not: 105 a dependent size, 50 a value with no address to bind, 32 + 1 undeclared, 1 no such call).
**(1) The function state, in part.** `Templates::readDeclaration` reads the pattern into a `Program` of its own
(`Parser::unit_` holds the unit's meanwhile): the pattern's `Function`, its static locals and its string literals
are never in the unit's program - nothing is popped or cut back, and a read that fails leaves none of them (m8g's
"whatever a failed read left"). A pattern's string labels come off a count of their own (`Parser::stringLabel`),
so the unit's `.L.str.N` are numbered as with no read whatever a replay inside the read emits. What the read
asks to be replayed writes the unit's program (`Parser::ReplayUses`, the same three doors, now switching
`current_` as well as the marks). **What (1) still does not hold**: the pattern's `Signature` is still entered
and taken out (its entry in `lookup_.functions_` stays, dead; its overload-set entries are erased by a walk of
the whole index, once a template - the registrations are in six places and no one door), its syntax is still
written and killed (the check needs it; M8.2 keeps it), and three things are still made *early*: a class
specialization a non-dependent use in a body instantiates, an asked function specialization's `Signature`
(seated where asked, by m8g), a constexpr member replayed for a constant. **(2) Three of the eleven shapes
mended**: `p->template pick<5>(1)` - `dependentPostfix` honours `template` after `.`/`->` on a dependent object
([temp.names]/4) and skips the arguments, where it read `(p->pick < 5) > 1`; `T{}` and `T{x}` of a dependent
type are noted as the cast a scalar's are (`bracedValueInit`), where the list door noted the bare name. All
three now differ from their replay in text alone. **The eight left are the signature's** (six of
`template-sfinae-misc`, `g7`, `length`): `ParserDependent`'s reader notes no syntax of its own - commit 2's
remainder, not started. **(3) The 99, one reason examined**: the dependent size (27; 8 of 8 sampled by
backtrace) is one asker - a *function parameter pack* in the pattern: `packParameter` gives the declared name
the type `packExpansion(T)`, and `Parser::declare` asks its alignment (`Parser.cpp:840`). Letting it through is
not the mend: the body's `rest...` and `sizeof...(rest)` then need a pack bound under the written name, and a
pack of one member would fold `sizeof...` to 1 - a wrong read where there is now none. Not mended; the other 72
not touched.

| corpus, box | function templates defined | read | not read | read: text alone / shape / same | read, never replayed |
| --- | --- | --- | --- | --- | --- |
| `tests/cases`, Linux and Windows, before (`4285d86e`) | 3,478 | 3,379 | 99 | 452 / 11 / 14 | 2,902 |
| `tests/cases`, Linux and Windows, after (`eb7d4031`; the same count on both) | 3,478 | 3,379 | 99 | **455 / 8** / 14 | 2,902 |

**Not read, by reason, after**: as before (27 a dependent size - 26 on Linux; 21 pack-pattern forms; 14 + 6
undeclared names; 9 + 8 signature forms; 7 an incomplete type; 2 a literal operator over a pack; 2 + 1 + 1 + 1 +
1). 0 units die (899 files print the count on Linux, 880 on Windows, as before).

**(4) The read's cost, always on** - the self-host's 103 sources compiled one at a time on Linux, user + sys
summed, without the flag and with `-check=templates` (which is the read at every function template's
declaration, the replays held against it, and the report): at REF `4285d86e` 177.53 s against 178.59 s; at `eb7d4031`, two rounds, 172.19 and 186.64 s
against 195.15 and 184.07 s - the box was shared, and its own spread (172 to 187 s with no flag) is wider than
the difference. The least of five, alternated, on one source each: `ParserExpr.cpp` 6.98 s against 6.93 s,
`Templates.cpp` 5.47 s against 5.61 s. **The read costs under 3% of the self-host's compile time, and this
method cannot say how far under.** **And the measurement found what the cases had not: with the flag, 8 of the
self-host's 103 sources' `-S` differ from no flag** (`MirAlloc`, `MirWebs`, `OptAlign`, `OptDead`, `OptHoist`,
`OptLoops`, `OptValues`, `X86_64Linux`; 1,634 to 3,712 lines each) - at REF and at `eb7d4031` alike, so it is
m8g's tree's and not this hour's. Nothing is lost or added: `std::vector<bool>`'s constructor, destructor,
`push_back`, `pop_back`, `back` and one `std::move` are emitted at another place - the class specialization a
pattern's body names is made at the read, and its members' replays stand where the read made it
(`boxkit/m8h-selfdiff.sh`; `Driver`'s two lines are `__TIME__`, different with no flag twice).

**Gates** at `eb7d4031`, one tree on each box, each built from clean; REF built fresh from `4285d86e` in its own
kept tree (Linux `~/m8hr`, Windows `work\m8hr` from `git archive`; NEW `~/m8hn`, `work\m8hn`). Linux (g++
`-Werror` clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and
-O2, **5,298 compared, 0 differ**; the flag against no flag, `-S` of every case, **1,238 the same, 0 differ**
(also with item 1 alone, before the two shape mends); `run.sh` 1232 passed, 0 failed; `typed.sh` 875 passed, 0
failed, 1 not for this host (other=38 name=8); `tests/subst.sh bodies` 0 failed (5,119 over 875 units, 3,381 /
682 / 1,004 / 52; no record re-recorded); self-compile stage 0 103 of 103; exclusions 0 uncited, 0 stale (12
citations re-pointed); comment cap 0; memory gate held; ast-residue +0 on all seven; `ide/generate.py --check`
up to date. Windows (cl /W4 /WX clean): every case's `-S` text and
diagnostics NEW against REF at -O0 and -O2 for x86_64-windows `-masm=masm` and `-masm=gnu`, x86_64-linux,
arm64-darwin and tms6747, 1,238 cases, **12,380 the same, 0 differ**; the flag against no flag **1,238 the
same, 0 differ**; the static gates pass; `tests/names.sh` 883 passed, 0 failed; `tests/overload.sh` 46 agreed
with clang, 0 differed; `-check=templates` reads the same 3,379 of 3,478 (880 files print the count).
`self-compile.cmd` 103 of 103. **Not finished inside the hour on Windows** (the box was shared): the self-host's
count, still running at the stop - it lands in `boxkit/m8h-win-names.out`. **Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows and
Compiler++'s units; no `tests/cases` program was added (the three mended shapes are their own cases' counts).
src/ lines: 94,863 before, 94,873 after.

**Whether commit 3 can drop the flag: no - the flag was not dropped.** The brief's condition is that (1) holds,
and it holds for the program's three tables only. And the output is **not** identical: 0 of 1,238
cases differ with the read on, on both boxes, but 8 of the self-host's 103 sources do (above) - m8g's "a class
specialization made earlier than with no read", now shown by a `-S`. Beside it: with the read always on every
unit would carry one dead `Signature` a function template, and a pattern the read does not understand costs a
caught failure, which must stay the *read's* and never the program's (99 here, 189 in the self-host; none kills
a unit today). The cost is not what stands in the way. **What the next lane takes**: a class
specialization seated where the program first asks, as `madeWhereAsked` seats a function's (the door is
`instantiateClass`'s `findTypedef(tag)` return; the test is `boxkit/m8h-selfdiff.sh` at 0 of 8, then the
self-host's `-S` with the flag against without as a standing gate beside the cases'); the `Signature` not
entered for a pattern (one door for the six registrations, then no erase); then the flag can go as its own commit. After that the
signature's expression as syntax (eight shapes, with commit 2's remainder), a function parameter pack in a
pattern (27, with the 21 pack-pattern forms beside it), then the rest of the 99.

**The class seating, tried and not committed** (`boxkit/m8h-classseat.patch` over `eb7d4031`, four lines:
`instantiateClass` records a class the read asked for in `askedByPattern_` and calls `madeWhereAsked` at its
`findTypedef(tag)` return). Linux only, a clean build in `~/m8hx`: **the eight self-host sources' `-S` with the
flag against without, 8 of 8 the same**; the cases' 1,238 the same, 0 differ; read 3,379 of 3,478, shape 8, as
before. It is not in the branch because it has no `identical.sh`, no Windows build and no gate behind it - the
next lane applies it, gates it, and has the self-host comparison (all 103, `boxkit/m8h-cost.sh`) to hold it to.

**M8.1, commit 3: a class specialization seated where the program asks, 2026-11-18** (lane m8i, branch `m8-d`
from `29c9b51c`; one lane-hour; code `789e7b4a`). **Lane m8h's Windows chain, read first, all passed**:
`self-compile.cmd` 103 of 103, `tests/names.sh` 883 passed 0 failed, `tests/overload.sh` 46 agreed 0 differed,
the self-host under `-check=templates` 103 of 103 with 7,323 of 7,512 read (189 not, by m8g's reasons).
**(1) The class seating, committed and gated** (`boxkit/m8h-classseat.patch`, four lines): a class
specialization a pattern's read instantiates is recorded in `askedByPattern_`, and `instantiateClass`'s
`findTypedef(tag)` return calls `madeWhereAsked` - its members' replays stand where the program first asks, as
with no read. **The self-host's `-S` with the flag against without: 8 of 103 differed, now 0** on Linux and on
Windows (`boxkit/m8i-selfsame.sh`, and `m8i-win.sh selfsame`: 102 the same; the 103rd is `Driver`, one line,
its `__TIME__` string - different with no flag twice); `tests/cases` 1,238 the same, 0 differ, both boxes; the
read's counts as m8h's (3,379 of 3,478; 455 / 8 / 14; 2,902 never replayed).
**(2) The `Signature` not entered, and an asked specialization's not made at the read: not done, and not one
door.** Two things found by reading and one by measuring. *An asked function specialization's `Signature`
cannot be held aside*: both of `instantiate`'s callers on the call path discard what it returns
(`Templates.cpp:1984`, `ParserTemplate.cpp:798`) and overload resolution then ranks the specialization where
it finds it - in `functionIndex_[name]`, beside the ordinary functions; a copy the read keeps to itself is a
candidate nobody sees. So it wants a set of the read's own that `Lookup` consults while `patternUses_` is set,
not a branch in `instantiate`. *The pattern's own `Signature`* is made by the ordinary declaration path, as any function's, and the erase walk
takes out entries of the pattern's own name only: whatever else a read enters (9,082 `functions_` entries over
7,399 reads in the self-host, below) it does not look for. *Measured*
(`boxkit/m8i-left.patch`, a count on stderr after each read, Linux `~/m8ix`, never committed): what the reads
leave in the unit's tables, summed -

| corpus (reads) | `functions_` entries | overload-set entries left | specializations | `typedefs_` entries | `enums_` | syntax nodes (killed) |
| --- | --- | --- | --- | --- | --- | --- |
| `tests/cases` (3,506 reads met) | 3,467 in 3,466 reads | 3 in 1 | 1 | 4,509 in 3,427 | 52 in 47 | 58,643 |
| the self-host (7,512) | 9,082 in 7,399 | 1,656 in 27 | 144 in 24 | 11,964 in 7,491 | 0 | 150,641 |

**So "the read writes nothing into the unit" is further off than the two tables the brief names**: nearly every
read leaves `typedefs_` entries (the bound parameters' and the body's own, unbound but not removed), 47 leave
enumerators, and 27 of the self-host's leave whole class specializations with their members' signatures in the
overload sets (1,656 entries - kept on purpose: the class is the program's, made early and now seated where
asked). None of it changes a `-S` on either box; all of it is the unit's memory with the read always on.

**Whether commit 3 can drop the flag: not yet - the flag was not dropped.** The output condition now holds
(the self-host 0 of 103 and the cases 0 of 1,238 with the flag against without, both boxes); the other half -
a read that writes nothing into the unit - does not, by the table. **What the next lane takes**, in order: the
read's `Lookup` as a layer (its signatures, typedefs and enumerators entered into tables of the pattern's own
that lookups consult first while the read runs, and dropped whole at its end - one door, which also holds an
asked specialization's `Signature` where resolution can see it); the table above at 0 but for the class
specializations, re-measured by `m8i-left.patch`; then the flag as its own commit; then the signature's
expression reader (eight shapes), not started here.

**Gates** at `789e7b4a`, one tree on each box, each built from clean; REF built fresh from `29c9b51c` in its own
kept tree (Linux `~/m8ir`, Windows `work\m8ir` from `git archive`; NEW `~/m8in`, `work\m8in`). Linux (g++
`-Werror` clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and
-O2, **5,298 compared, 0 differ**; `run.sh` 1232 passed, 0 failed; `typed.sh` 875 passed, 0 failed, 1 not for
this host (other=38 name=8); `tests/subst.sh bodies` 0 failed (5,119 over 875 units, 3,381 / 682 / 1,004 / 52;
no record re-recorded); self-compile stage 0 103 of 103; exclusions 0 uncited, 0 stale (no citation moved);
comment cap 0; memory gate held; ast-residue +0 on all seven; `ide/generate.py --check` up to date.
Windows (cl /W4 /WX clean; from lane m8i's handover, which this entry was committed without): the
self-host's `-S` with the flag against without 102 the same and `Driver`; cases with the flag 1,238 / 0; static
gates rc 0; names.sh 883 / 0; overload.sh 46 / 0; the five spellings against REF 5,780 the same, 0 differ (cases
1-578, `work\m8in`) and 6,588 the same with 12 caret-column rows (cases 579 on, `work\m8in2`, a tree name one
longer); self-compile.cmd not run (run by lane m8j at `c3cbb3c4`: 103 of 103). **Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows and
Compiler++'s units; no `tests/cases` program was added (the gate for this commit is the self-host comparison,
which no case showed). src/ lines: 94,873 before, 94,877 after.

**M8.1, commit 3 closed: the read's own `Lookup` layer, and the flag dropped, 2026-11-18** (lane m8j, branch `m8-d`
from `c3cbb3c4`; code `fcb6f5bc` the layer, `5d7f9c47` the temporaries' counter, `142889e4` the constexpr door,
`241cd65b` the flag). **The
layer**: `sema::Lookup::Layer` is the tables as they stood - the four sizes, the four indexes (the overload sets
as resolution reads them, objects, type names, enumerators) and the open `using namespace`s; `pushLayer` takes
it when `Templates::readDeclaration` starts and `popLayer` puts it back whole when the read ends, so whatever
the read entered - the pattern's own `Signature`, the bound parameters' and the body's typedefs, a local
enumeration, an overload-set key rewritten in passing - is found inside the read and is not there after.
`specializations_`, `memberTemplateMade_` and `owedBodies_` go back with it: **a function specialization the
read asks for is made in the layer (its `Signature` where resolution ranks it) and gone after**, and the program's
own ask makes it where it asks, as with no read - so `askedByPattern_` and `madeWhereAsked` (m8h's and m8i's
seating) are deleted. **What a layer cannot take back is refused in the read, by name**: a class specialization
the program has not made yet (its `Type` is completed in place and its members' bodies are the unit's) and a
member-template specialization over no pattern parameter and a class template's constexpr member a constant
asks for (`Constexpr::instantiateForConstant`) - the body of each is replayed into the unit at once. Each is a
`SubstitutionFailure` of the read's, "... the program has not asked for yet", M8.2's to make by substitution.
The count of reads did not move: `tests/cases` 3,379 of 3,478 and the self-host 7,323 of 7,512 (189 unread), as
at `c3cbb3c4`, and no unread reason over the cases names a refusal of these. **What the layer held when it was
dropped** (`boxkit/m8j-made.sh`, at `fcb6f5bc`): over the cases one read made a function specialization in it
(`block-scope-typedef`) and none a class; over the self-host no read made any.

**What a read leaves in the unit's tables**, by `boxkit/m8j-left-full.diff` (m8i's measure, after the pop;
Linux `~/m8jx`):

| corpus (reads) | `functions_` | overload-set entries | specializations | `globals_` | `typedefs_` | `enums_` |
| --- | --- | --- | --- | --- | --- | --- |
| `tests/cases` (3,506), before | 3,467 | 3 | 1 | - | 4,509 | 52 |
| `tests/cases` (3,506), after | 0 | 0 | 0 | 0 | 0 | 0 |
| the self-host (7,512), before | 9,082 | 1,656 | 144 | - | 11,964 | 0 |
| the self-host (7,512), after | 0 | 0 | 0 | 0 | 0 | 0 |

(The layer the read drops held 3,467 overload-set keys and 100 type-name keys of its own over the cases, 6,940
and 206 over the self-host.) **One thing the tables did not show, found by `identical.sh` on the flag-dropped
tree**: the read advanced `refTemps_`, the unit's count of named temporaries, so a `$chain` / `$endcatch` label
on tms6747 was one higher in `constructor-unwinds` and `function-try-block-constructor` (4 rows of 5,298; the
same at `c3cbb3c4` with the flag - m8i's flag-against-no-flag check ran the host's target alone). The read
now puts the count back (`5d7f9c47`).

**The flag is dropped** (`241cd65b`): `readDeclaration` runs in every compile; `-check=templates` is the
comparison alone (`holdBodies`, `holdDeclarations`, the report). With the flag against without at `fcb6f5bc`:
`tests/cases` 1,238 the same, 0 differ, and the self-host 102 of 103 the same with `Driver` differing in its
`__TIME__` string (2 lines), on Linux and on Windows. The read's cost, three self-host sources on Linux, REF
against the flag-dropped tree: ParserExpr 7.51 s -> 7.38 s, Templates 7.29 s -> 7.11 s, Driver 10.99 s ->
11.86 s (noise; 0.3 MB more at the peak). The layer is a copy of the four indexes a read - a journal of what
the read wrote is the cheaper form, owed if a unit ever shows the copy.

**Gates** on the final tree (`241cd65b`), each box built from clean; REF built fresh from `c3cbb3c4` in its own
kept tree (Linux `~/m8jr`, Windows `work\m8jr`; NEW `~/m8jk`, `work\m8jk`, names of one length). Linux (g++ `-Werror`
clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and -O2, **5,298
compared, 0 differ**; the self-host's `-S`, REF against NEW with no flag, 102 of 103 the same and `Driver` in
its `__TIME__` string (2 lines); `run.sh` 1232 passed, 0 failed; `typed.sh` 875 passed, 0 failed, 1 not for this
host (other=38 name=8); `tests/subst.sh bodies` 0 failed (5,119 over 875 units, 3,381 / 682 / 1,004 / 52; no
record re-recorded); self-compile stage 0 103 of 103; exclusions 0 uncited, 0 stale; comment cap 0; memory gate
held; ast-residue +0 on all seven; `ide/generate.py --check` up to date.
Windows (cl /W4 /WX clean): the self-host's `-S`, REF against NEW, 102 of 103 the same and `Driver` (2 lines);
static gates rc 0; `names.sh` 883 passed, 0 failed; `overload.sh` 46 agreed with clang, 0 differed; the five
spellings at -O0 and -O2 against REF, **9,919 the same, 0 differ** of 12,380 when the hour ended (the step
was stopped there by its PID - the rest is the wave gate's); `tools\windows\self-compile.cmd`, alone in a tree
of its own each time: `c3cbb3c4` 103 of 103 (owed by lane m8i), the final tree `241cd65b` **103 of 103**. With
the flag against without at `fcb6f5bc` on Windows: cases 1,238 / 0, the self-host 102 and `Driver`.
**Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows, Compiler++'s units;
no `tests/cases` program was added (the gates are the self-host and `identical.sh`, which found the label).
**Not done**: the signature's expression reader (eight shapes), not started; commit 4 not started.
src/ lines: 94,877 before, 94,886 after.

**M8.1, commit 4 begun: the class doors read under `-check=templates`, 2026-11-18** (lane m8k, branch `m8-e`
from `3a922049` - m8-d merged with main `156a6b46`; one lane-hour; code `0a9076aa`). **What is read**: a class
template's member bodies, an out-of-line member, a partial specialization's member bodies and an alias's
type-id, each where it is declared, by `Templates::readDeclaration` with a door (`ReadClass`, `ReadOutOfLine`,
`ReadPartial`, `ReadAlias`) - the function door's read, layer and all: the parameters bound to themselves, the
read's own `Lookup` layer, a `Program` of its own, specializations and temporaries' numbers put back. **The
class's read is `instantiateClass`'s own**, taken out whole as `readClassBody` (one implementation: the
instantiation calls it with its binding, the read with the parameters themselves and a tag of that read alone,
`C<$T0>$read<mark>`); while a class door's read lasts, the pattern's `C<T>` is that class (`readPattern_`), so
`Box<T>::get` and a member's `Box<T>` find its members. Each held member body is then replayed in a trial of
its own - one refused leaves the others read - and an out-of-line member is read from its `start` by
`topLevel`. **The replay is still what emits**: the class doors run under `-check=templates` alone (the
function door in every compile, as m8j left it), so a compile without the flag differs from `3a922049` by the
extraction only, which the gates below hold. What a refusal thrown through the class's read left set - the
class stack, the instantiating arguments, the held bodies, `inTemplateArgs_` (a `>` ended `r > 0` in the next
function), the parameters' bindings (kept by the read and unbound by it, refused or not) - is put back by the
read; found by the count over the cases, 70 of which failed under the flag before it was.

**Read / not read by door**, Linux `~/m8kn` (the committed code), `boxkit/m8k-count.sh` and `m8k-self.sh`.
Before this lane no class door was read at all (every body below was the replay's alone):

| door | `tests/cases` (900 units reach the count): bodies / read / unread | same / text / shape / unreplayed | the self-host (103 of 103): bodies / read / unread | same / text / shape / unreplayed |
| --- | --- | --- | --- | --- |
| function (m8j's, unchanged) | 3,478 / 3,379 / 99 | 14 / 455 / 8 / 2,902 | 7,512 / 7,323 / 189 | 0 / 1,127 / 0 / 6,196 |
| class members | 4,728 / 2,755 / 1,973 | 442 / 268 / 0 / 2,045 | 16,745 / 12,652 / 4,093 | 2,508 / 1,846 / 0 / 8,298 |
| out-of-line members | 22 / 0 / 22 | - | 0 | - |
| partial specializations' members | 1,737 / 251 / 1,486 | 11 / 25 / 0 / 215 | 3,186 / 1,054 / 2,132 | 6 / 103 / 0 / 945 |
| alias type-ids (no body: read or refused) | 7 / 7 / 0 | - | 0 | - |

A class whose own read is refused counts once, as "the class: ..." (its bodies are not known then), so `bodies`
is a floor. No read body differs from its replay in shape at a class door. **Not read, by reason** (cases /
self-host): the class: a base class has to be complete - a dependent base, `struct D : B<T>` (666 / 826, and 4
partials); the size of a dependent type - a data member `T v;` laid out, in the class (278 / 514; partials 62 /
101; out-of-line 6) and in a member body (326 / 885; partials 16 / 96); the pseudo-destructor does not name
the type of this object (168 / 396); the class: "expected" a token (163 / 204); has no member called (40 / 352);
no function called takes these arguments (85 / 198); was not declared (20 / 176); the class: is declared twice
(11 / 127); an in-class initialiser of a static member of dependent type (84 / 102); a template parameter in
this place of a signature (44 / 99, and 14 in the class); incomplete type (class 69 / 85, body 10 / 88,
out-of-line 11); the arms of `?:` (partials 16 / 96); a class specialization the program has not asked for
(3 / 26). **A fault of the read, not a wall**: a partial's class read ends "struct `X<$T0>$read` is defined
twice" for every partial of a class whose primary was read (1,388 / 1,835 of the partials' unread - all of
`<type_traits>`' `remove_reference`, `is_reference`, `iterator_traits`, `hash` ...): the partial's tokens are
read by `structOrUnionSpecifier` under the read's tag and meet a definition - not found in the hour. And the
out-of-line door reads 0 of 22: the class before it is refused (a data member of dependent type), so the
member never gets its turn; the door stands on the class's.

**What the class doors' reads leave** (the flag against no flag, `-S`, Linux): 1,235 cases the same and **4
differ** - `library-cppparse-pieces`, `map-set-comparator`, `map-set-node-stability`,
`member-template-body-unchosen` - each in one Itanium name, `St4pairIT_T0_E` written `St4pairIKT_T0_E`: the
pattern's shallow `pair<$T0,$T1>` is interned in the unit's type table by its tag and keeps the arguments of
whoever asks first, and `map`'s class read asks for `pair<const K, V>` before the unit does. The `Lookup` layer
does not hold the type table; commit 5 cannot turn the class read on until it does (a type-table mark, or the
shallow class keyed by its arguments' qualifiers). The self-host under the flag was not compared in the hour.

**Left of commit 4**, in the order the counts give: (1) the class's layout over a dependent member - `T v;`
takes no size in the read (the function door's `nameDependentSizes` answer, carried into `ParserClass`'s
layout), which opens the out-of-line door with it; (2) a dependent base, kept unlaid in the read; (3) the
partial's "defined twice"; (4) the type table in the layer (the 4 cases); (5) member templates - not started,
no door; (6) an alias's type-id is read (7 of 7) but held against nothing - an alias makes no body to compare.
The signature's expression reader (eight shapes): not started.

**The self-host under the flag** (Linux `~/m8kn`, `boxkit/m8k-selfsame.sh`, measured after the paragraph above
was written): `-S` with `-check=templates` against without, **34 of 103 the same, 69 differ** (3,948 lines),
the same one fault - `make_pair`'s `St4pairIT_T0_E` and its kin renamed by the pattern class the read interned
first. It is the flag's output alone; without the flag nothing differs (below).

**Gates** on the final tree (`0a9076aa`), each box built from clean; REF built fresh from `3a922049` in its own
kept tree (Linux `~/m8kr`, Windows `work\m8kr`; NEW `~/m8kf`, `work\m8kf`, names of one length). Linux (g++
`-Werror` clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and
-O2, **5,304 compared, 0 differ**; the self-host's `-S`, REF against NEW, 102 of 103 the same and `Driver` in
its `__TIME__` string (its diff opened: one `.byte` line, `23:03:48` against `23:04:03`); `run.sh` 1233 passed,
0 failed; `typed.sh` 876 passed, 0 failed, 1 not for this host (other=38 name=8); `tests/subst.sh bodies` 0
failed (5,119 over 876 units, 3,381 / 682 / 1,004 / 52; the two `.templates` records re-recorded - each gains
its class-members lines and nothing else); self-compile stage 0 103 of 103; exclusions 0 uncited, 0 stale
(**`3a922049` itself reads 11 uncited, 11 stale** - the merge with main moved seven `ParserTemplate.cpp`, three
`Templates.cpp` and one `Constexpr.cpp` citation; re-pointed here with this lane's own); comment cap 0; memory
gate held; ast-residue +0 on all seven; `ide/generate.py --check` up to date.
Windows (cl /W4 /WX clean, the box shared with three other gates): the self-host's `-S`, REF against NEW, 102 of
103 the same and `Driver` (2 lines); static gates rc 0; `names.sh` 884 passed, 0 failed; `overload.sh` 46
agreed with clang, 0 differed; the five spellings at -O0 and -O2 against REF, **4,796 the same, 0 differ** of
12,390 when the hour ended (the step was stopped there by its PID - the rest is the wave gate's);
`tools\windows\self-compile.cmd`, alone in a tree of its own (`work\m8ks`), **103 of 103**.
**Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows, Compiler++'s units;
`-check=templates` by door on Windows (the Linux counts above are the measure); no `tests/cases` program was
added. src/ lines: 95,217 before, 95,326 after.

**M8.1, commit 4, second step: the class doors' own faults, a dependent member and a dependent base,
2026-11-18** (lane m8l, branch `m8-e` from `edeb7535`; one lane-hour; code `381643e3`). Still under
`-check=templates` alone; the replay is what emits. **Four mends.** (1) *The type table*: a pattern class the
class door's read is first to ask for (`pair<$T0,$T1>`, asked as `pair<const K, V>` by `map`) is noted
(`readShallow_`) and gives its arguments and namespace back when the read ends, so the unit's first asker
names it as it always did - the class is still interned, it keeps nothing of the read's. (2) *A partial's
"defined twice"*: the read's tag was `C<$T0>$read<mark>` with `mark` the syntax tree's size, which the read
itself puts back - a partial with as many parameters as its primary took the primary's tag and met its
definition; the tag takes a serial of the read's own (`readSerial_`). (3) *A dependent data member*: in a
read's class (`Parser::readLayout_`, set by `readClassBody` for a read only) a member whose size no pattern
has is laid out as nothing (`Type::DependentLayout`: size 0, align 1 while the member is placed), and the
class is marked (`setDependentLayout`), so its own size asked in a body is refused by the same name as
before, "the size of a dependent type" - never a number. (4) *A dependent base*: `struct D : B<T>` in a
read's class leaves the base out (it is no class yet) and marks the class the same way; a member it would
bring is then refused by name in the body that names it.

**Read / not read by door**, Linux `~/m8ln` (the committed sources), `boxkit/m8l-count.sh`, `m8l-self.sh`;
before is m8k's table above:

| door | `tests/cases` before: bodies / read | after: bodies / read / unread | the self-host before | after: bodies / read / unread |
| --- | --- | --- | --- | --- |
| function (unchanged) | 3,478 / 3,379 | 3,478 / 3,379 / 99 | 7,512 / 7,323 | 7,512 / 7,323 / 189 |
| class members | 4,728 / 2,755 | 12,175 / 7,529 / 4,646 | 16,745 / 12,652 | 29,421 / 20,794 / 8,627 |
| out-of-line members | 22 / 0 | 22 / 8 / 14 | 0 | 0 |
| partial specializations' members | 1,737 / 251 | 607 / 442 / 165 | 3,186 / 1,054 | 1,796 / 1,389 / 407 |
| alias type-ids | 7 / 7 | 7 / 7 / 0 | 0 | 0 |

(`bodies` is a floor while a class is refused whole, so the class door's rose as its classes were read and the
partials' fell as 1,388 / 1,835 "defined twice" classes became their few bodies.) Read bodies against their
replays, cases: class members 1,083 the same, 553 in text, **2 in shape**, 5,891 unreplayed; the self-host
3,722 / 2,861 / 0 / 14,211. **Gone** (cases / self-host): the class: a base has to be complete (666 / 826), the
class: the size of a dependent type (278 / 514, partials 62 / 101), a partial's "defined twice" (1,388 / 1,835).
**Not read, by reason, after**: no function called takes these arguments (2: 1,768 / 2,652; 4: 408 / 612; 3:
136 / 204; 1: 91 / 259) - REF's own wall (85 / 198 there), now met in the classes it could not read: an
unqualified call of the class's own member with a member access as an argument, `void set(const S &o) {
assign(o.p, o.n); }`, where `this->assign(o.p, o.n)` and `assign(s, k)` both read, and a parameter `C *` fails as `const C *`
does - so it is the argument: `o.p` through a reference to the class being read looks dependent, and the
call is resolved over it instead of held (probes in Linux `~/m8l-probe`; not confirmed, not mended); the size of a dependent type in a body (641 / 1,398; partials 16 / 96);
a template parameter in this place of a signature (589 / 915; partials 68 / 102); the arms of `?:` (342 / 555;
partials 16 / 96); the pseudo-destructor (168 / 396); the class: "expected" a token (162 / 204); declares no
member with these parameters (59 / 101, and as many in partials); in-class initialiser of a static member
(84 / 102); has no member called (40 / 352); was not declared (27 / 311); incomplete type (the class 69 / 85,
a body 14 / 178, out-of-line's class 11); the class: declared twice (11 / 127); needs a complete type (4 / 90).

**The flag against no flag** (`-S`, Linux): the cases **0 differ** of 1,239 (4 before); the self-host **102 of
103 the same and `Driver`** in its `__TIME__` line alone (its diff opened: one `.byte` line; 69 differed
before).

**Left of commit 4**: (1) the unqualified own-member call above - the largest reason by far, and one root;
(2) a dependent size in a body, and the signature's "this place" (the expression reader's shapes); (3) the
pseudo-destructor over a dependent type (`explicitDestructorCall` compares `named` with `plain`; in a read
either may be dependent - one condition, not made because the function door shares the line and the gates
were already running); (4) "has no member" through a base left out; (5) a door for member templates - not
started; (6) the out-of-line door's 14 (11 an incomplete class, 3 "declares no member"). The signature's
expression reader: not started.

**Gates** on the final tree (`381643e3`), each box built from clean; REF built fresh from `edeb7535` in its own
kept tree (Linux `~/m8lr`, Windows `work\m8lr`; NEW `~/m8ln`, `work\m8ln`, names of one length). Linux (g++
`-Werror` clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and
-O2, **5,304 compared, 0 differ**; the self-host's `-S`, REF against NEW, 102 of 103 the same and `Driver` in
its `__TIME__` string (diff opened: one `.byte` line); `run.sh` 1233 passed, 0 failed; `typed.sh` 876 passed,
0 failed, 1 not for this host; `tests/subst.sh bodies` 0 failed (5,119 over 876 units, 3,381 / 682 / 1,004 / 52;
`template-class.templates` re-recorded - its class-members line reads 4 of 6 where it read 0 of 3, and the
dependent-size line is gone; `template-variadic`'s is unchanged); self-compile stage 0 103 of 103; exclusions
0 uncited, 0 stale (31 citations re-pointed); comment cap 0; memory gate held; ast-residue +0 on all seven;
`ide/generate.py --check` up to date. Windows (cl /W4 /WX clean - both builds passed the driver's nine-minute
cap on the shared box and ended on the box, the executables made): the self-host's `-S`, REF against NEW, 102
of 103 the same and `Driver` (its `__TIME__` line); the flag against no flag, cases 1,239 the same, 0 differ,
and the self-host 102 and `Driver` (diff opened: one `DB` line of the time); static gates rc 0; `names.sh` 884
passed, 0 failed; `overload.sh` 46 agreed with clang, 0 differed; the five spellings at -O0 and -O2 against
REF, **12,390 the same, 0 differ** (the whole compare, inside the hour); `tools\windows\self-compile.cmd` in a tree of its own (`work\m8ls`), **103 of 103**.
**Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows, Compiler++'s units;
`-check=templates` by door on Windows (the Linux counts are the measure); no `tests/cases` program was added.
src/ lines: 95,326 before, 95,368 after.

**A patch measured and not committed** (`boxkit/m8l-left.patch`, two conditions; Linux only, the Windows
gates were already running on the final tree): (a) `explicitDestructorCall` lets a pseudo-destructor through
in a read when the named type or the object's is dependent; (b) `memberCallWith` holds a member call whose
arguments are dependent (`patternBody_ && argsDependent_`), as the function-template paths do - the root of
"no function called takes these arguments": `o.n` through a reference to the class being read is dependent,
and the ordinary member call resolved over it. Both lines are the function door's too, so they change every
compile's read and want the whole gate. On Linux `~/m8lx` (built clean): cases class members **10,026 of
12,175 read** (7,529 committed), the function door 3,380 of 3,478, the flag against no flag 0 differ;
`identical.sh` against REF 5,304 compared, 0 differ; the self-host's `-S` against REF 102 and `Driver`; the
self-host's flag against no flag 102 and `Driver`;
`typed.sh` 876 / 0; `run.sh` 1233 / 0; self-compile 103 of 103. The next lane's first step.

**M8.1, commit 4, third step: the expression reader's reasons in a read's body, 2026-11-18** (lane m8m, branch
`m8-e` from `1e5937b7`; one lane-hour; code `54dcea46`, `516d1faa`, `d5ebe843`). Still under `-check=templates`
alone; the replay is what emits. **`54dcea46`** is m8l's measured patch as it was: a pseudo-destructor over a
dependent type, and a member call over a dependent argument, held in a read (`516d1faa` moves its comment into
the group above it - the comment cap said 1 over). **`d5ebe843`**, six holds, each `patternBody_` alone: a
dependent `sizeof` is a dependent value (the signature's "in this place" refusal is the signature's again); a
`?:` with a dependent arm is held as a dependent operator is; `new T` asks no size; a member returning its
dependent class by value asks the ABI nothing; a pack parameter (`Args... a`) asks no alignment; and a read's
class laid out over a dependent member (`Type::dependentLayout()`, a nested `Node` holding a `T`) is dependent
to `dependentType`. The three size sites were found by `catch throw SubstitutionFailure` under gdb in Linux
`~/m8mx` (`Abi::returnsIndirectly`, `newExpression`, `declare`).

**Read / not read by door**, Linux, `boxkit/m8m-count.sh`, `m8m-self.sh`; before is `54dcea46` (m8l's
`~/m8lx`, the same sources):

| door | `tests/cases` before: bodies / read / unread | after | the self-host before | after |
| --- | --- | --- | --- | --- |
| function | 3,478 / 3,380 / 98 | 3,478 / 3,389 / 89 | 7,512 / 7,324 / 188 | 7,512 / 7,324 / 188 |
| class members | 12,175 / 10,026 / 2,149 | 12,208 / 11,617 / 591 | 29,421 / 24,754 / 4,667 | 29,883 / 28,002 / 1,881 |
| out-of-line members | 22 / 8 / 14 | 22 / 8 / 14 | 0 | 0 |
| partial specializations' members | 607 / 442 / 165 | 607 / 526 / 81 | 1,796 / 1,389 / 407 | 1,796 / 1,587 / 209 |
| alias type-ids | 7 / 7 / 0 | 7 / 7 / 0 | 0 | 0 |

**Gone** (cases, class members): the size of a dependent type 641 -> 16 (partials 16 and the function door 25
are left), a template parameter in this place of a signature 589 -> 0, the arms of `?:` 342 -> 0; and, by
`54dcea46` against m8l's table (`1e5937b7`), the pseudo-destructor 168 -> 0 and no function called takes these
arguments 2,403 -> 72. **Not read, by reason,
after** (cases): the class: "expected" a token (162); the in-class initialiser of a static member (84); the
class: an incomplete type (69); no function called takes these 2 arguments (68); declares no member with these
parameters (59, and 59 in partials); has no member called (40); was not declared (27); the size of a dependent
type (16 class members, 16 partials, 25 the function door). The self-host after: has no member called (352),
was not declared (311), an incomplete type (223), the class: "expected" (204), declared twice (127), the size of a
dependent type (104 the function door, 96 partials).

**The flag against no flag** (`-S`): the cases **0 differ** of 1,239 on both boxes; the self-host **102 of 103
the same and `Driver`** in its `__TIME__` line alone on both (diffs opened: one `.byte` / `DB` line of the time).

**Gates** on the final tree (`d5ebe843`), each box built from clean; REF built fresh from `1e5937b7` in its own
kept tree (Linux `~/m8mr`, Windows `work\m8mr`; NEW `~/m8mf`, `work\m8mf`, names of one length). Linux (g++
`-Werror` clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and
-O2, **5,304 compared, 0 differ**; the self-host's `-S`, REF against NEW, 102 of 103 the same and `Driver` (its
`__TIME__` line); `tests/subst.sh bodies` 0 failed (5,119 over 876 units, 3,381 / 682 / 1,004 / 52; both
`.templates` records unchanged); `typed.sh` 876 passed, 0 failed, 1 not for this host; `run.sh` 1233 passed, 0
failed; self-compile stage 0 103 of 103; exclusions rc 0, comment cap 0, memory gate held, ast-residue +0,
`ide/generate.py --check` up to date. Windows (cl /W4 /WX clean, 103 sources, its log read whole): static gates
rc 0; the self-host's `-S`, REF against NEW, 102 of 103 and `Driver` (its `__TIME__` line); `names.sh` 884
passed, 0 failed; `overload.sh` 46 agreed with clang, 0 differed; the five spellings at -O0 and -O2 against REF, **12,390 the same, 0 differ** (6,200 in the first half run again alone, and the second half's 6,190 among 6,327 the same); `tools\windows\self-compile.cmd` in a tree of its own (`work\m8mt`), **103 of 103**.
**Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows, Compiler++'s units;
`-check=templates` by door on Windows (the Linux counts are the measure); no `tests/cases` program was added.
**What the lane did wrong**: a file copied into the gated Linux tree under a running gate (`54dcea46`'s
comment fix) made the self-host step relink `cxx3.exe` beneath `identical.sh` ("Permission denied", 200
false differences) - that run was stopped and the final tree gated from clean; and the Windows compare's two
halves were started side by side over one log, so the first half was run again under names of its own.

**Left of commit 4**: has no member / not declared through a base left out (40 / 27); the class: "expected" a
token (162, the class door's own parse); the in-class static initialiser (84); declares no member with these
parameters (59 + 59); a door for member templates - not started; the out-of-line door's 14; the last dependent
sizes (an array `new T[n]`'s element, partials').
src/ lines: 95,368 before, 95,381 after.

**M8.1, commit 4, fourth step: three of the class doors' reasons, 2026-11-18** (lane m8n, branch `m8-e` from
`9830a16e`; one lane-hour; code `d3767911`). Still under `-check=templates` for the class doors; the replay is what
emits. The reasons were read whole, not by their quote-stripped census, by a probe patch (`boxkit/m8n-why.patch`:
each refusal's text and place under `CXX3_WHY`), on `<type_traits>`, `<utility>`, `<memory>` and `<string>`
alone. **Three holds**, each in a read only (`patternBody_`): (1) *the class: expected '('* is `<type_traits>`'
`is_arithmetic : _arith_or<is_integral<T>::value, T>` and `is_fundamental`'s twin - a dependent class's member
named as a value in a template argument, which the expression reader took for a type and a functional cast; it
is held as an unresolved name (`depPrimary`, `X<args>::name` not followed by `(`, `<` or `::`); (2) *an in-class
initialiser of a static member* is `integral_constant`'s `static const T value = v;` - over a dependent `T` it is
read and folded to nothing; (3) *a name the class does not find when a dependent base was left out*
(`Type::dependentBase`, set beside `dependentLayout` by m8l's left-out base): `S::x`, an unqualified name, and
`o.x` / `p->x` are held as dependent in such a class (the replay finds them in the base; [temp.dep]/3 would look
for the unqualified ones nowhere, so this follows the replay, not the standard).
`e394cc44` moves (1)'s comment onto its line (the comment cap said 1 over at `d3767911`: the new line split a
three-line group from its one-line `if`; comments alone, and the file has no `__LINE__`).

**Read / not read by door**, Linux `~/m8nn` (`d3767911`), `boxkit/m8n-count.sh`, `m8n-self.sh`; before is m8m's
after (`d5ebe843`, the same sources as `9830a16e`):

| door | `tests/cases` before: bodies / read / unread | after | the self-host before | after |
| --- | --- | --- | --- | --- |
| function | 3,478 / 3,389 / 89 | 3,478 / 3,389 / 89 | 7,512 / 7,324 / 188 | 7,512 / 7,324 / 188 |
| class members | 12,208 / 11,617 / 591 | 12,044 / 11,699 / 345 | 29,883 / 28,002 / 1,881 | 29,679 / 28,104 / 1,575 |
| out-of-line members | 22 / 8 / 14 | 22 / 8 / 14 | 0 | 0 |
| partial specializations' members | 607 / 526 / 81 | 607 / 526 / 81 | 1,796 / 1,587 / 209 | 1,796 / 1,587 / 209 |
| alias type-ids | 7 / 7 / 0 | 7 / 7 / 0 | 0 | 0 |

(`bodies` is a floor while a class is refused whole: the 246 classes read now count their bodies, 82, where each
counted one.) Read class-member bodies against their replays: 1,158 the same, 1,190 in text, 2 in shape, 9,349
unreplayed. **Gone** (cases, class members): the class: "expected" 162 -> 1 (`expected a name`, one case), the
in-class initialiser 84 -> 0. **Not changed**: has no member called (40) and was not declared (27) - neither is
through a left-out base, so hold (3) met none of them in the cases. Their roots, read with the probe on
`deque-both-ends` (`<map>`/`<set>`'s `__tree`): `order::less(...)`, where `order` names `__tree_order<..., C>` - a
class over dependent arguments, shallow and without members - is "has no member called"; and `KeyOf::get(...)`,
where `KeyOf` is the class's type parameter, is "was not declared" (the ordinary reader knows `T::name` only in
the signature's expression reader). Both are one condition each at `primaryNode`'s two refusals; not made, the
gates being under way. **Not read, by reason, after**
(cases): the class: an incomplete type (69); no function called takes these 2 arguments (68); declares no member
with these parameters (59, and 59 in partials) - all `<utility>`'s `pair(const pair<const T1, T2> &)` and the
partial's `pair(const pair<T1, T2> &)` at their replay, the class read and only the member's definition not
matched against its declaration (not mended); has no member called (40); was not declared (27); the size of a
dependent type (16 class members, 16 partials, 25 the function door); an incomplete type in a body (16); declared
twice (11); a class specialization not asked for (9). The self-host after: has no member called (352) and was not
declared (311), an incomplete type (223), declared twice (127), no function takes these 2 arguments (102),
declares no member (101 + 101). **On Windows** (`work\m8nn`, x86_64-windows, the first
count by door there): the function door the same, 3,478 / 3,389 / 89; the class doors read 15 of 935 and 1 of 245
partials - 843 and 240 refused as "the class: cannot be given a name the linker can hold: this type has no
Microsoft linkage name yet", the read's class `C<$T0>$read<n>` meeting the Microsoft mangler. A wall of the
Windows names alone, which the Linux counts do not see; commit 5 has to give a read's class a Microsoft name (or
ask for none) before the class doors can be measured there.

**The flag against no flag** (`-S`): the cases 0 differ of 1,239 on Linux (`m8n-count.sh`; its "1" is the
script's own slot line, which carries the word), and 1,239 the same, 0 differ, on Windows; the self-host 102 of 103 the same and `Driver` (diff opened: one `.byte` line of the time) on Linux, 102 of 103 and `Driver` on Windows (diff opened: one `DB` line of the time).

**Gates** on the final code (`d3767911`), each box built from clean; REF built fresh from `9830a16e` in its own
kept tree (Linux `~/m8nr`, Windows `work\m8nr`; NEW `~/m8nn`, `work\m8nn`, names of one length). Linux (g++
`-Werror` clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and
-O2, **5,304 compared, 0 differ**; the self-host's `-S`, REF against NEW, 102 of 103 the same and `Driver` (2
lines, its `__TIME__` `.byte` line); `tests/subst.sh bodies` 0 failed (5,119 over 876 units, 3,381 / 682 / 1,004
/ 52); `typed.sh` 876 passed, 0 failed, 1 not for this host; `run.sh` 1233 passed, 0 failed; self-compile stage
0 103 of 103; static gates on `e394cc44`'s sources (Linux `~/m8nc`): exclusions rc 0, comment cap 0, memory gate
held, ast-residue +0, `ide/generate.py --check` up to date. Windows (cl /W4 /WX clean, 103 sources, 0 warnings,
its log read whole): static gates rc 0 on `e394cc44` (`work\m8nc`; at `d3767911` the comment cap said 1); the
self-host's `-S`, REF against NEW, 102 of 103 and `Driver` (its `__TIME__` `DB` line, diff opened); `names.sh`
884 passed, 0 failed; `overload.sh` 46 agreed with clang, 0 differed; the five spellings at -O0 and -O2 against
REF, **12,390 the same, 0 differ** (6,200 and 6,190, the halves one after the other); `tools\windows\self-compile.cmd` in a tree of its own (`work\m8ns`), **103 of 103**.
**Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows, Compiler++'s units; no
`tests/cases` program was added. **What the lane did wrong**: `tools/comment-lines` was run once on the Mac (a
text scan, to find the group) - the gate of record is the boxes' run above.

**Left of commit 4**: the pair constructors' "declares no member" (59 + 59, one root); `X::m` over a dependent class
and `T::f` over a type parameter (40 / 27, the roots above); the class's incomplete type (69); no function takes
these 2 arguments (68); a door for member templates - not started; the out-of-line door's 14; the last
dependent sizes; and the Windows class doors' Microsoft name for a read's class.
src/ lines: 95,381 before, 95,421 after.

**M8.1, commit 4, fifth step: a read's class named for Microsoft, two holds and the pair constructors, 2026-11-18**
(lane m8o, branch `m8-e` from `8120dc24`; one lane-hour; code `0a837e09`). Still under `-check=templates` for the
class doors; the replay is what emits. **Four mends**, each in a read only (`patternBody_`). (1) *A read's class
has no Microsoft name*: `C<$T0>$read<n>`'s members, static members and the special members the compiler writes
(`Linkage::memberSymbol`, `staticMemberSymbol`, five sites in `Classes.cpp` through `readName`) take a `$pattern$`
name where the Microsoft mangler has none, as `functionSymbol` already did - one name per signature (`fn->describe()`),
so overloads stay apart and a definition still finds its declaration. (2) *`order::less` over a dependent class*
(`__tree_order<..., C>`, shallow, no members) and (3) *`KeyOf::get` over a type parameter*: `primaryNode`'s `S::x`
over a scope `dependentType` answers for is held as a dependent value (`waits`). (4) *`<utility>`'s pair
constructors*: the class door registered its class for `C<$T0>` (`readPattern_`) only after the class's body was
read, so a member declared `pair(const pair<const T1, T2> &)` met the shallow pattern class and its replayed
definition the read's class - "declares no member with these parameters"; the class is now found from its head on
(`readingTag_` / `readingRead_`, the read's tag looked up while its body is read). And a partial's read registered
nothing that is its own: its key was spelled from its own parameters (`pair<$T0,$T1>` for `pair<const T1, T2>`) - the
primary's key - so the partial's `pair<T1, T2>` became the partial; a partial now registers no key.

**Read / not read by door**, before `8120dc24` (Linux: m8n's after, the same sources; Windows: `work\m8nn`'s count
summed by door, and the self-host measured in `work\m8or` here), after `0a837e09` (Linux `~/m8on`, `boxkit/m8o-count.sh`,
`m8o-self.sh`; Windows `work\m8on`, `m8o-win.sh bodies` / `self` and `m8o-wsum2.sh`):

| door | Linux cases before / after: bodies / read / unread | Linux self-host before / after | Windows cases before / after | Windows self-host before / after |
| --- | --- | --- | --- | --- |
| function | 3,478 / 3,389 / 89; 3,478 / 3,395 / 83 | 7,512 / 7,324 / 188; 7,512 / 7,325 / 187 | 3,478 / 3,389 / 89; 3,478 / 3,395 / 83 | 7,512 / 7,324 / 188; 7,512 / 7,325 / 187 |
| class members | 12,044 / 11,699 / 345; 12,041 / 11,805 / 236 | 29,679 / 28,104 / 1,575; 29,678 / 28,692 / 986 | 935 / 15 / 920; 934 / 434 / 500 | 3,481 / 0 / 3,481; 3,551 / 1,989 / 1,562 |
| out-of-line members | 22 / 8 / 14; 22 / 8 / 14 | 0 | 22 / 0 / 22; 22 / 1 / 21 | 0 |
| partial specializations' members | 607 / 526 / 81; 607 / 585 / 22 | 1,796 / 1,587 / 209; 1,796 / 1,688 / 108 | 245 / 1 / 244; 259 / 179 / 80 | 493 / 0 / 493; 493 / 292 / 201 |
| alias type-ids | 7 / 7 / 0 | 0 | 7 / 7 / 0 | 0 |

(Windows reaches the count in 881 of 1,239 cases, Linux in 900; a class refused whole counts once, so Windows'
`bodies` is a floor far below Linux's while most classes are refused.) **Gone** (Linux cases): has no member called
(40), was not declared (27), declares no member with these parameters (59 + 59 in partials); the self-host's has no
member (352) and was not declared (311) likewise. **Not read, by reason, after** (Linux cases): the class: an
incomplete type (69); no function called takes these 2 arguments (68); needs a complete type (24, 4 before - bodies
that now reach further); the size of a dependent type (16 + 16, and 25 the function door); an incomplete type in a
body (16); declared twice (11). The self-host: needs a complete type (266), an incomplete type (223), declared twice
(127), no function takes 2 arguments (102). **Windows, still a Microsoft name** (cases): 410 class members, 77
partials and 7 out-of-line refused as "this type has no Microsoft linkage name" - two sites (1)'s mend did not reach,
a constructor written in the class (`ParserClass.cpp`, `'C::C'`) and a member template (`ParserTopLevel.cpp`);
the self-host 1,440 class members and 201 partials the same way.
**A patch measured and not committed** (`boxkit/m8o-name.patch`, two lines: those two sites take the same
`$pattern$` name in a read; its Windows build, `work\m8ox` beside the gate chain on the shared box, passed the
driver's nine-minute cap and ended on the box, the executable made): the Windows cases then read **exactly as
Linux's** - class members 12,041 / 11,805 / 236, partials 607 / 585 / 22, out-of-line 22 / 8 / 14, the function
door 3,478 / 3,395 / 83, and the same reasons at the top (69, 68, 24, 16 + 16). Not gated (the gates were already
running on `0a837e09`); the next lane's first step, with the whole gate - the member-template line is the
function door's too.

**The flag against no flag** (`-S`): the cases 0 differ of 1,239 on Linux (`m8o-count.sh`; its "1" is the script's
own slot line), 1,239 the same, 0 differ, on Windows; the self-host 102 of 103 the same and `Driver` on Linux (diff opened: one `.byte` line of the time), 102 of 103 and `Driver` on Windows (diff opened: one `DB` line of the time, `11:06:17` against `11:07:00`).

**Gates** on the final code (`0a837e09`), each box built from clean; REF built fresh from `8120dc24` in its own
kept tree (Linux `~/m8or`, Windows `work\m8or`; NEW `~/m8og`, `work\m8on`, names of one length). Linux (g++
`-Werror` clean): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0 and -O2,
**5,304 compared, 0 differ**; the self-host's `-S`, REF against NEW, 102 of 103 the same and `Driver` (2 lines,
its `__TIME__` `.byte` line - the script removes the files, so this diff was not opened; the flag's was);
`tests/subst.sh bodies` 0 failed (5,119 over 876 units, 3,381 / 682 / 1,004 / 52); `typed.sh` 876 passed, 0 failed,
1 not for this host; `run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103; exclusions rc 0, comment cap
0, memory gate rc 0, ast-residue +0, `ide/generate.py --check` up to date.
Windows (cl /W4 /WX clean, 103 sources, 0 warnings, its log read whole): static gates rc 0 (`work\m8on`); the
self-host's `-S`, REF against NEW, 102 of 103 and `Driver` (its `__TIME__` line); `names.sh` 884 passed, 0 failed;
`overload.sh` 46 agreed with clang, 0 differed; the five spellings at -O0 and -O2 against REF, **12,390 the same,
0 differ** (6,200 and 6,190, the halves one after the other); `tools\windows\self-compile.cmd` in a tree of its own
(`work\m8os`), **103 of 103**.
**Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows, Compiler++'s units; no
`tests/cases` program was added. **What the lane did wrong**: two `grep`s on the Mac (text scans: one over
`src/sema/Classes.cpp` for its first lines, one over this entry's own placeholders) - every other search ran on the
Linux box.

**Left of commit 4**: the Windows class doors' last Microsoft names (the patch above); the class's incomplete type
(69); no function takes these 2 arguments (68); needs a complete type (24 / 266); a door for member templates - not
started; the out-of-line door's 14; the last dependent sizes; a partial's own key (`pair<const $T0,$T1>`) for its
`C<T>`, which is registered for no partial now.
src/ lines: 95,421 before, 95,446 after.

**M8.1, commit 4, sixth step: the Microsoft name at the last two sites, gated, 2026-11-18** (lane m8p, branch `m8-e`
from `0972fdf8`; one lane-hour; code `224b8993`). Still under `-check=templates` for the class doors; the replay is
what emits. `boxkit/m8o-name.patch` applied as its own commit: a constructor written in its class
(`ParserClass.cpp`, `declareConstructor`) and a member template at the top level (`ParserTopLevel.cpp`) take a
`$pattern$` name in a read where the Microsoft mangler has none, as m8o's `memberSymbol` and `functionSymbol` do.

**Read / not read by door**, before `0972fdf8` (m8o's after: Linux `~/m8on`, Windows `work\m8on`), after `224b8993`
(Linux `~/m8pn`, `boxkit/m8p-count.sh`, `m8p-self.sh`; Windows `work\m8pn`, `m8p-win.sh bodies` / `self`,
`m8p-wsum2.sh` / `m8p-wsum3.sh`):

| door | Linux cases before / after | Linux self-host before / after | Windows cases before / after | Windows self-host before / after |
| --- | --- | --- | --- | --- |
| function | 3,478 / 3,395 / 83; 3,478 / 3,395 / 83 | 7,512 / 7,325 / 187; 7,512 / 7,325 / 187 | 3,478 / 3,395 / 83; 3,478 / 3,395 / 83 | 7,512 / 7,325 / 187; 7,512 / 7,325 / 187 |
| class members | 12,041 / 11,805 / 236; 12,041 / 11,805 / 236 | 29,678 / 28,692 / 986; 29,678 / 28,692 / 986 | 934 / 434 / 500; 12,041 / 11,805 / 236 | 3,551 / 1,989 / 1,562; 29,678 / 28,692 / 986 |
| out-of-line members | 22 / 8 / 14; 22 / 8 / 14 | 0 | 22 / 1 / 21; 22 / 8 / 14 | 0 |
| partial specializations' members | 607 / 585 / 22; 607 / 585 / 22 | 1,796 / 1,688 / 108; 1,796 / 1,688 / 108 | 259 / 179 / 80; 607 / 585 / 22 | 493 / 292 / 201; 1,796 / 1,688 / 108 |
| alias type-ids | 7 / 7 / 0 | 0 | 7 / 7 / 0 | 0 |

**Windows reads as Linux does**, door by door, over the cases and the self-host: "this type has no Microsoft
linkage name" is gone from both (410 + 77 + 7 in the cases, 1,440 + 201 in the self-host before). One reason
differs by its count, not its door: the function door's dependent sizes, 26 on Windows against Linux's 25 and one
`type_info` refusal (the function door is 83 unread on both). **Not read, by reason, after** (both boxes, cases):
the class's incomplete type (69); no function called takes these 2 arguments (68); needs a complete type (24); the
size of a dependent type (26 / 25 + 16 + 16); an incomplete type in a body (16); declared twice (11). The self-host:
needs a complete type (266), an incomplete type (223), declared twice (127), dependent sizes (104 + 96 + 96), no
function takes 2 arguments (102), the class's incomplete type (85).
Linux's cases are as before, door by door and reason by reason (the patch changes only the Microsoft spelling).
**Two patches measured and not committed** (beside the gate, on Linux only; the gate is `224b8993`'s):
`boxkit/m8p-incomplete.patch` (`ParserType.cpp`, four lines: a read's data member of a dependent class no pattern
has made - `C<int> c` over a template template parameter, `set`'s `__tree<...> tree_` - is laid out as nothing, as
a dependent base is, and marks its class's layout dependent), `~/m8py`: class members **12,237 / 12,036 / 201**
(from 236), out-of-line 22 / 9 / 13, partials 607 / 586 / 21; the class's incomplete type 69 to 26; what the classes
now read whole meet next: an incomplete type in a body 16 to 24 (`__tree`'s `i`, a `__tree_node<$T0>`), the
out-of-line door's "declares no member with these parameters" 3 to 9; flag against no flag 0 differ. Its self-host
was not measured (stopped while it waited for a slot). With `boxkit/m8p-resize.patch` on top (`Overload::rankArgument`, two lines: in a read a parameter over the
pattern's own takes any argument, ranked a conversion - the replay ranks it), `~/m8pz`: class members
**12,237 / 12,108 / 129**, the 68 gone and none added; flag against no flag 0 differ. Both are the next lane's first
commit, with the whole gate (`boxkit/m8p-incomplete-resize.patch` is the two together).
**The reasons, read whole** (`boxkit/m8n-why.patch` over `224b8993`, `~/m8px`, `m8p-why.sh`): the 68 are
`basic_string<$T0>::resize(n)`'s `resize(n, 0)` against `resize(size_type, C)` - an `int` ranked against the
pattern's `C`; the 24 are `new __tree_node<$T0>` and `new __hash_node<$T0>` (`__tree:187`, `:194`,
`__hash_table:87`, `:89`), a specialization no pattern has made; the body's incomplete type is a local of the same.
**The flag against no flag** (`-S`): the cases 0 differ of 1,239 on Linux (`m8p-count.sh`; its "1" is the
script's own slot line) and 1,239 the same, 0 differ, on Windows; the self-host 102 of 103 the same and `Driver` on
both (Linux: diff opened, one `.byte` line of the time; Windows: 2 lines, the files replaced by the next step before
they were opened - REF against NEW's `Driver` diff, in the same place, was opened: one `DB` line of the time).

**Gates** on `224b8993`, each box built from clean; REF built fresh from `0972fdf8` in its own kept tree (Linux
`~/m8pr`, Windows `work\m8pr`; NEW `~/m8pn`, `work\m8pn`, names of one length). Linux (g++
`-Werror` clean, 0 warnings): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0
and -O2, **5,304 compared, 0 differ**; the self-host's `-S`, REF against NEW, 102 of 103 the same and `Driver` (its
`__TIME__` line); `tests/subst.sh bodies` 0 failed (5,119 over 876 units, 3,381 / 682 / 1,004 / 52); `typed.sh`
876 passed, 0 failed, 1 not for this host; `run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103; exclusions rc 0, comment cap 0, memory gate rc 0, ast-residue +0,
`ide/generate.py --check` up to date.
Windows (cl /W4 /WX clean, 103 sources, 0 warnings, its log read whole; the NEW build ran first and alone, under six
minutes): static gates rc 0 (`work\m8pn`); the self-host's `-S`, REF against NEW, 102 of 103 and `Driver` (its
`__TIME__` line); `names.sh` 884 passed, 0 failed; `overload.sh` 46 agreed with clang, 0 differed; the five
spellings at -O0 and -O2 against REF, **12,390 the same, 0 differ** (6,200 and 6,190, the halves one after the other); `tools\windows\self-compile.cmd` in a tree of its own
(`work\m8ps`), **103 of 103**.
**A defect, recorded and not retried**: the Windows REF build (`work\m8pr`, `0972fdf8`) passed the nine-minute cap -
it was started beside the counts, `names.sh` and `overload.sh` on the shared box rather than alone; the driver's
wait was ended at nine minutes and `build.cmd` ran on, its executable made at about twelve. Its log after the cap
was not read; the REF gates above ran against that executable.
**Not run**: `tests/subst.sh`'s four stand-in steps, `syntax.sh`, winpass, the -O1 rows, Compiler++'s units; the
patches' self-host; no `tests/cases` program was added. **What the lane did wrong**: one Linux tree (`~/m8pn`) was
shipped twice by two of the lane's own commands (an ssh left holding its session), and the first build failed in
the deleted tree; a gate run started on that failure was stopped by its PIDs before it ran anything, and the tree
was built again from clean. Linux's three slots were held by other lanes for most of the hour (the bodies step
waited 18 minutes); the serial chain was split into parallel queued steps.

**Left of commit 4**: the class's incomplete type (69 / 85; 26 with the patch); no function takes these 2 arguments (68 /
102; 0 in the cases with the patch); needs a complete type (24 / 266); a partial's own key for its `C<T>`; the member-template door - not started;
the out-of-line door's 14; the last dependent sizes. Unread is not 0 on either box, so commit 5 (the read on by
default) waits.
src/ lines: 95,446 before, 95,448 after.

**M8.1, commit 4, seventh step: m8p's two patches, gated, and the 24 `new` measured, 2026-11-18** (lane m8q, branch
`m8-e` from `06f4b8ae`; one lane-hour; code `f7bf0691`). Still under `-check=templates`; the replay is what emits.
`boxkit/m8p-incomplete-resize.patch` applied as its own commit (`ParserType.cpp`: a read's data member of a dependent
class no pattern has made is laid out as nothing and marks its class's layout dependent; `Overload::rankArgument`: in
a read a parameter over the pattern's own parameters matches any argument as a conversion), with
`tests/cases/template-class.templates` re-recorded - its class members read 8 of 8, from 4 of 6 (the bodies gate's
one failure on the first run, the record holding the old count; the new record is byte for byte the run's output).

**Read / not read by door** (bodies / read / unread), before `06f4b8ae` (m8p's `~/m8pn`, `work\m8pn`), after
`f7bf0691` (Linux `~/m8qn`, `boxkit/m8q-count.sh`, `m8q-self.sh`; Windows `work\m8qn`, `m8q-win.sh bodies` / `self`,
`m8q-wsum2.sh` / `m8q-wsum3.sh`). **Linux and Windows are the same door by door**, before and after, and reason by reason but the one m8p named:

| door | cases before | cases after | self-host before | self-host after |
| --- | --- | --- | --- | --- |
| function | 3,478 / 3,395 / 83 | 3,478 / 3,395 / 83 | 7,512 / 7,325 / 187 | 7,512 / 7,325 / 187 |
| class members | 12,041 / 11,805 / 236 | **12,237 / 12,108 / 129** | 29,678 / 28,692 / 986 | **31,158 / 30,315 / 843** |
| out-of-line members | 22 / 8 / 14 | 22 / 9 / 13 | 0 | 0 |
| partial specializations' members | 607 / 585 / 22 | 607 / 586 / 21 | 1,796 / 1,688 / 108 | 1,796 / 1,688 / 108 |
| alias type-ids | 7 / 7 / 0 | 7 / 7 / 0 | 0 | 0 |

Unread in all: the cases **355 to 246**, the self-host **1,281 to 1,138**. Gone: the class's incomplete type (69 to
26 in the cases, 85 to 0 in the self-host), no function takes these 2 arguments (68 and 102 to 0). **Not read, by
reason, after** - the cases: the class's incomplete type 26, needs a complete type 24, an incomplete type in a body
24, the size of a dependent type 25 + 16 + 16 (26 on Windows: one `type_info`), not declared 14 + 2, declared twice
11; the self-host: an incomplete type in a body 283, needs a complete type 266, declared twice 127, dependent sizes
104 + 96 + 96, a specialization not asked for 36 + 25 + 24 + 8.

**The flag against no flag** (`-S`): the cases 1,239 the same, 0 differ, on both boxes (Linux's `count.sh` "1" is
the script's own slot line); the self-host 102 of 103 and `Driver` on both, each diff opened: one line of
`__TIME__` (Linux `.byte`, Windows `DB` 11:59:14 against 12:00:38).

**Gates** on `413e4f9d` (the code; `f7bf0691` adds the record alone and ran the bodies step), each box built from
clean; REF built fresh from `06f4b8ae`, alone, in its own kept tree (Linux `~/m8qr`, about four minutes; Windows
`work\m8qr`, after the NEW build, under three minutes; NEW `~/m8qn`, `work\m8qn`, names of one length). Linux (g++
`-Werror` clean, 0 warnings): `tools/identical.sh` REF against NEW for tms6747, arm64-darwin and x86_64-linux at -O0
and -O2, **5,304 compared, 0 differ**; the self-host's `-S`, REF against NEW, 102 of 103 and `Driver` (2 lines, its
`__TIME__`; the script removes the files, not opened); `tests/subst.sh bodies` 1 failed on the first run (`template-class`'s record, above), then, re-recorded, **0 failed** (5,119 over 876 units, 3,381 / 682 / 1,004 / 52); `typed.sh` 876 passed, 0
failed, 1 not for this host; `run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103; exclusions rc 0,
comment cap 0, memory gate rc 0, ast-residue +0, `ide/generate.py --check` up to date. Windows (cl /W4 /WX clean,
103 sources, no warning line in either log; NEW built first and alone): static gates rc 0; the self-host's `-S`,
REF against NEW, 102 of 103 and `Driver` (opened: one `DB` line, 12:03:36 against 12:05:04); `names.sh` 884
passed, 0 failed; `overload.sh` 46 agreed with clang, 0 differed; the five spellings at -O0 and -O2 against REF,
**12,390 the same, 0 differ** (6,200 and 6,190); `tools\windows\self-compile.cmd` (`work\m8qs`, `413e4f9d`) **103 of 103**.

**Measured and not committed** (Linux only, beside the gate): `boxkit/m8q-new.patch` (`ParserExprNew.cpp`, seven
lines: a read's `new __tree_node<V>(v)` over a class no pattern has made reads its operands and stands as a zero
pointer, as a read's `T(x)` over a dependent class does), `~/m8qx`: class members **12,237 / 12,132 / 105** - the 24
"needs a complete type" gone and nothing else moved; flag 0; the self-host's class members **31,158 / 30,581 /
577** (the 266 gone). An argument of dependent type matched as a conversion beside it (`~/m8qy`) moved nothing, and
was dropped. With a read's local of such a class held as well (`ParserStmt.cpp`, `const_iterator i`; `~/m8qz`):
function door 3,478 / 3,402 / 76, class members 12,237 / 12,154 / 83, two new `'delete' needs a pointer` (`delete
at.n_` over the held local); `boxkit/m8q-held.patch` (the three: `new`, the local, `delete` of a dependent operand
read as nothing), `~/m8qw`: the cases' function door **3,478 / 3,402 / 76**, class members **12,237 / 12,156 / 81**, nothing added; flag 0; the self-host's class members **31,158 / 30,864 / 294** (the 283 and the 266 gone; declared twice 127 and the dependent sizes lead now). Unread with it: the cases 191, the self-host 589. It is the next lane's first commit, with the whole gate (Windows not measured).

**Left of commit 4**: in the cases 246 and the self-host 1,138 (191 and 589 with `m8q-held.patch`); the class's incomplete type 26;
the dependent sizes; a partial's own key for its `C<T>`; the member-template door - not started; the out-of-line
door's 13. Unread is not 0 on either box, so commit 5 (the read on by default) waits.
src/ lines: 95,448 before, 95,452 after.

**M8.1, commit 4, eighth step: m8q's held patch, a read's closure names, and the root of "declared twice", 2026-11-18**
(lane m8r, branch `m8-e` from `89911b67`; code `6a92fe79`, `62a9aa96`). Still under `-check=templates`; the replay is
what emits. `boxkit/m8q-held.patch` as its own commit (`6a92fe79`: a read's `new` of a dependent class no pattern has
made reads its operands and is a zero; a read's local of one is held; a read's `delete` of a dependent operand is its
operand typed void), written so it adds no raw `new` and no `ExprPtr` word - as first committed (`faf61e4a`) it raised
the memory gate 1,053 to 1,054 and the residue's `ExprPtr` 2,016 to 2,018, which m8q's static step reported as rc 0
(it printed `tail`'s status: a defect of the script, mended in `boxkit/m8rf-linux.sh`). **The coordinator's owed
item** (`62a9aa96`, `ParserExprLambda.cpp`, `ParserType.cpp`): a function template's read named its closures and
local classes first, so `lambda-returning-lambda`'s `tw::$_0` became `tw$3::$_0` in the -g text; a read's are now
`$read$tw::$_0` and take no name an instantiation will.

**Read / not read by door**, before `89911b67` (m8q's `~/m8qn`, `work\m8qn`), after `faf61e4a` (Windows `work\m8rn`,
`m8r-win.sh bodies` / `self`): the cases' function door 3,478 / 3,395 / 83 to **3,478 / 3,402 / 76**, class members
12,237 / 12,108 / 129 to **12,237 / 12,156 / 81**, out-of-line 22 / 9 / 13, partials 607 / 586 / 21, aliases 7 / 7 / 0
- unread **246 to 191**; the self-host's class members 31,158 / 30,315 / 843 to **31,158 / 30,864 / 294**, function
7,512 / 7,325 / 187, partials 1,796 / 1,688 / 108 - unread **1,138 to 589**; 103 of 103 compiled. These are m8q's
`~/m8qw` on Linux, reason by reason; Linux's counts on the gated tree were still running at the hour (below).

**"Declared twice" has its root** (`boxkit/m8n-why.patch` over `faf61e4a`, `~/m8rx`): all 11 in the cases are
`map:105` and `unordered_map:87`, `insert(const value_type &)` against `insert(const pair<K, V> &)` - value_type being
`pair<const K, V>`. `Templates::specializationKey` spelled a template parameter's argument without its cv, so
`pair<const $T0,$T1>` and `pair<$T0,$T1>` were one key and one class; and a partial's own key would have been its
primary's, which is why the class door registers none. `boxkit/m8r-cvkey.patch` (the cv in the key, and a partial
read keyed by its written arguments) is measured as `~/m8rw` - not finished at the hour; not committed, since the
key is the replay's too and wants identical.sh.

**Gates on `faf61e4a`** (REF built fresh from `89911b67`, alone, in Linux `~/m8rr` and Windows `work\m8rr`; NEW
`~/m8rn`, `work\m8rn`, built alone after it on Linux, before it on Windows): Linux g++ `-Werror` 0 warnings; `subst.sh
bodies` 0 failed (5,119 over 876 units); exclusions 0, comment cap 0. Windows cl /W4 /WX clean; the cases' `-S` with the
flag against without 1,239 the same, 0 differ; the self-host's flag against no flag and REF against NEW 102 of 103 and
`Driver` (both `fc` opened: one `DB` line of `__TIME__`); `names.sh` 884 passed, 0 failed; `overload.sh` 46 agreed, 0
differed. **Owed at the hour** (running, results in the files named in `boxkit/m8r-handover.md`): Linux identical.sh,
self-host REF against NEW and flag, typed, run, selfc, the counts; Windows the five spellings' halves and
self-compile; the final tree `62a9aa96` built and its static gates on both boxes (`~/m8rf`, `work\m8rf`), and its -g
text against REF (`~/m8ry`, `m8rf-win.sh same` with `GF=-g`). The member-template door, the out-of-line 13 and the
dependent sizes were not started. Unread is not 0, so commit 5 waits: 191 in the cases and 589 in the self-host, of
which "declared twice" (11 / 127) is the cv key above and the dependent sizes (25 + 16 + 16 / 104 + 96 + 96) lead.
src/ lines: 95,452 before, 95,467 after.

**M8.1, commit 4, ninth step: m8r's gates read, the cv key, the member-template door and the out-of-line 13, 2026-11-18**
(lane m8s, branch `m8-e` from `4c042137`; code `a71bb801`, `a1617ff1`, `80d93293`). Still under `-check=templates`; the
replay is what emits. **m8r's gates, read as they landed** (Linux on `faf61e4a` against REF `89911b67`): `identical.sh`
tms6747, arm64-darwin, x86_64-linux at -O0 and -O2 **5,304 compared, 0 differ**; `typed.sh` 876 passed, 0 failed;
`run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103; the self-host's `-S` REF against NEW 102 and `Driver`;
the -g text of `62a9aa96` against REF on the four targets at -O0 (`~/m8ry`), 841 of 4,420 differ, every one the tree's
name in a `.file` line (`m8rr` against `m8ry`) but `lambda-returning-lambda`'s five, which are the owed item itself
(`tw$2::$_0`, `tw$3::$_0` back to `tw::$_0`, `tw$2::$_0`). **Two failures, mended first**: on the final tree (`~/m8rf`,
62a9aa96 with the cv key, shipped from the worktree) `make comments` rc 2 - `62a9aa96`'s comment in
`ParserExprLambda.cpp` was four lines (`a1617ff1`, comment only) - and `tools/exclusions --check` **rc 1: 57 uncited,
57 stale**, the refusal sites' lines moved under the citations by the commit-4 steps, unseen because m8q's and m8r's
static step printed `tail`'s status (the Windows script's `exclusions rc` is `tail`'s too); re-pointed by
`boxkit/m8s-repoint.py` (each file's stale citations paired in order with its uncited sites) on the final tree.
**The cv key** (`a71bb801`, `boxkit/m8r-cvkey.patch` as its own commit): `Templates::specializationKey` keeps a
template parameter's cv, and a partial's read is keyed by its written arguments. The cases' "declared twice" (11) and
the self-host's (127) are gone, and the classes that were one key are read apart - more bodies, fewer unread.
**The member-template door** (`80d93293`): a class template's member templates are read inside its class's read, over
their own parameters numbered past the class's (`$T<n+i>`, so `pair<T, U>` in `template <class U>` is not
`pair<$T0,$T0>`), and a plain class's once the class is complete (`Templates::readMemberTemplates`,
`readMemberTemplatesOf`, the door `member-templates`); the body is replayed by `replayMemberTemplate`, the half of
`instantiateMemberTemplate` that sets the member-template state and replays, taken out whole and called by both.
**The out-of-line 13** had one root: the class door keyed its read `Box<$T0,0>` and `Counted<{}>` (the read's values
and packs are empty), where the out-of-line qualifier `Box<T, N>` spells `Box<$T0,$P1>` and `Counted<Ns...>`
`Counted<{$T0...}>`; the read is keyed as its members write it now. With it a read's `int v[N]` (an array of dependent
length, the "has an incomplete type" 26 + 4) is laid out as nothing, as a dependent class member is (`ParserType.cpp`).
`constructor-template.templates` (member templates 5 of 5) and `template-out-of-line.templates` (out-of-line 7 of 7)
recorded; the two old records hold.

**Read / not read by door** (bodies / read / unread). Before: m8r's after (`work\m8rn`, `~/m8qw`). The cv key: Linux
`~/m8rw` and `~/m8rf`, Windows `work\m8sn` - **the same door by door**. The door: Linux `~/m8sm` (Windows not measured,
below):

| door | cases before | cv key | door | self-host before | cv key | door |
| --- | --- | --- | --- | --- | --- | --- |
| function | 3,478 / 3,402 / 76 | the same | the same | 7,512 / 7,325 / 187 | the same | the same |
| class members | 12,237 / 12,156 / 81 | 12,565 / 12,495 / 70 | **12,574 / 12,525 / 49** | 31,158 / 30,864 / 294 | 34,924 / 34,757 / 167 | 34,925 / 34,757 / 168 |
| out-of-line members | 22 / 9 / 13 | 22 / 9 / 13 | **22 / 21 / 1** | 0 | 0 | 0 |
| partial specializations' members | 607 / 586 / 21 | the same | the same | 1,796 / 1,688 / 108 | the same | the same |
| alias type-ids | 7 / 7 / 0 | the same | the same | 0 | 0 | 0 |
| member templates | - | - | **497 / 438 / 59** | - | - | **1,510 / 1,364 / 146** |

Unread: the cases **191, 180, 206** (147 in the five doors that were there), the self-host **589, 462, 609** (463 in
the five); 103 of 103 compiled every time. **Not read, by reason, after**: the size of a dependent type leads - the
cases 25 + 16 + 16 + 47 (member templates), the self-host 104 + 96 + 96 + 114 (410 of 609); then not declared (14 + 2
+ 3; 32 + 32 + 1), a specialization not asked for (9 + 4 + 2; 36 + 26 + 24 + 8), no static member function through a
template-id (6 + 4 + 2 + 1 in the cases), a value with no address (26 in the self-host); the out-of-line's last one is a
pack (`Ns...` alone).

**The flag against no flag** (`-S`): the cases 1,239 the same, 0 differ (Linux `~/m8sm`; Windows `work\m8sn` on the
cv key); the self-host 102 of 103 and `Driver`, its `__TIME__` line (Linux `~/m8sm`; Windows `work\m8sn`).
On Windows the door's tree (`work\m8sm`) reads as Linux door by door, the cases' dependent sizes 26 to Linux's 25 (the
`type_info` one m8p named).

**Gates.** REF built fresh from `4c042137`, alone, in its own kept tree (Linux `~/m8sr`; Windows `work\m8sr`, after
the NEW build); NEW names of one length. **The cv key, `a71bb801`** - Linux `~/m8sn` (g++ `-Werror` clean):
`tools/identical.sh` REF against NEW for tms6747, arm64-darwin, x86_64-linux and x86_64-windows at -O0 and -O2, **8,840
compared, 0 differ**; `typed.sh` 876 passed, 0 failed; `run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103; `subst.sh bodies` 0 failed (5,119 over 876 units); the self-host's `-S` REF against NEW, and with the flag against
without, 102 and `Driver`; memory gate rc 0, residue +0, `ide/generate.py` up to date; exclusions rc 1 and comment cap rc 2, the two
failures above, mended by `a1617ff1` and `01c133e7`. Windows `work\m8sn` (cl /W4 /WX clean): the cases' flag 0 differ;
the self-host's flag and REF against NEW 102 and `Driver` (`fc` opened: one `DB` line of `__TIME__`); the five
spellings at -O0 and -O2 against REF, **12,390 the same, 0 differ** (6,200 and 6,190). **The door, `80d93293`** (Linux `~/m8sm`,
g++ `-Werror` clean; the comment-only `a1617ff1` not in it): `tools/identical.sh` REF against NEW for tms6747,
arm64-darwin, x86_64-linux and x86_64-windows at -O0 and -O2, **8,840 compared, 0 differ** (the replay path's
`replayMemberTemplate` is the same code moved); `subst.sh bodies` with the four records **0 failed** (5,119 over 876
units, unchanged); the self-host's `-S` REF against NEW, and with the flag against without, 102 and `Driver`; Windows
`work\m8sm` the cases' flag 1,239 the same, 0 differ, the self-host 103 of 103. **The final sources**
(`01c133e7`, Linux `~/m8sf`, each gate's own exit code): exclusions rc 0 (0 uncited, 0 stale), comment cap rc 0,
memory gate rc 0, ast-residue +0 rc 0, `ide/generate.py --check` rc 0. **A defect**: the Windows build of `work\m8sm`
passed the 9-minute cap on the shared box (the m8sn halves beside it) and was not retried; the exe it went on to make
(a fresh tree, /WX) gave the Windows counts.
**Owed at the hour**: on `80d93293` `typed.sh`, `run.sh`, self-compile and the Windows five spellings, for the wave
gate.

**Left of commit 4**, and so of commit 5's condition (`unread` 0 over the cases and the self-host, identical.sh 0 -
the second holds at every step): unread **206** in the cases and **609** in the self-host. The size of a dependent
type is half and more (104 and 410): `Type::size` already refuses it by name in a read (`nameDependentSizes`), so
what is owed is the sites in a read's body that ask - a local's slot, `sizeof(T)`, `p + n` over `T *` - answering
"dependent" without the size; then not declared (19 / 65), a specialization not asked for (15 / 94), the member
templates' own 59 / 146, the out-of-line door's last one (a pack, `Ns...` alone).
src/ lines: 95,467 before, 95,548 after.

**M8.1, commit 4, tenth step: m8s's owed gates, and the dependent sizes found at one site, 2026-11-18**
(lane m8t, branch `m8-e` from `a655d85a`; code `7e11157a`, `a8915e8d`, `6edec0f1`, `0e86e621`, `763f1661`). Still under
`-check=templates`; the replay is what emits. **m8s's owed gates on `80d93293`** (its sources are `a655d85a`'s; REF
trees Linux `~/m8tr`, Windows `work\m8tr`, each built alone): `typed.sh` 876 passed, 0 failed; `run.sh` 1233 passed,
0 failed; self-compile stage 0 103 of 103 (130 s); Windows built under the cap (cl /W4 /WX clean, about 3 minutes) and
its five spellings at -O0 and -O2 against `a71bb801` (`work\m8sn`, read only) **12,390 the same, 0 differ** (6,200 and
6,190).

**Where the size was asked.** `gdb` with `catch throw` over seven cases (`boxkit/m8t-bt.sh`) put every "the size of a
dependent type" at one site: `Parser::allocateFrameSlot` asking `objectAlign` of a pack parameter's type (`Ts...
rest`, `Kind::PackExpansion`), which `dependentType` does not answer, from `declare` in a function's parameter list -
in the function door's reads and in member-template and member bodies replayed by a class's read. m8s's stride probe
moved nothing because the pointer sites are not reached first. **Three steps.** `7e11157a`: `Parser::slotless` - a
read's type that is dependent or a pack's expansion - at the four frame-slot sites (`allocateFrameSlot`, `declare`'s
alignment and its debug record, `leaveScope`); the cases' dependent sizes 104 to 1, the self-host's 410 to 0.
`a8915e8d`: a read's object of dependent type called by its name (`F f; f();`, a member `comp_(a, b)`) is a call
through it, where it went to overload resolution as "'f' was not declared". `6edec0f1`: a read's `V<I>::f(x)` over a
dependent class is dependent ([temp.dep.type]), where the read's `V<$P0>` had no member under its tag ("has no static
member function"). `0e86e621`: three citations `a8915e8d`'s line moved, re-pointed.

**Read / not read, before (`a655d85a`) and after (`0e86e621`)**, Linux; Windows reads door by door as Linux on
`a8915e8d`. The cases: functions 3,478 / 3,402 / 76 to 3,478 / 3,414 / 64; class members 12,574 / 12,525 / 49 to
12,574 / 12,533 / 41; out-of-line 22 / 21 / 1 unchanged; partials 607 / 586 / 21 to 607 / 587 / 20; member templates
497 / 438 / 59 to 497 / 447 / 50; **unread 206 to 176**. The self-host: functions 7,512 / 7,325 / 187 unchanged; class
members 34,925 / 34,757 / 168 to 34,925 / 34,758 / 167; partials 1,796 / 1,688 / 108 unchanged; member templates
1,510 / 1,364 / 146 to 1,510 / 1,397 / 113; **unread 609 to 575**. The dependent sizes did not become reads: behind the
slot each met the next wall, **a pattern over a parameter pack in a read** (`std::forward<Args>(args)...`;
`ParserTemplate.cpp`'s `bindPatternMember`, which refuses a pack that stands for itself): about 100 in the cases, 409 in
the self-host. Then a specialization not asked for (15 / 94), a local name in a signature's expression (9 in the
cases), not declared (5 / 32), a value with no address (26 in the self-host), a dependent base in a mem-initialiser
("neither a member nor a direct base", 6 / 12).

**The flag against no flag** (`-S`): the cases 1,239 the same, 0 differ (Linux `~/m8tn` on `0e86e621`; Windows
`work\m8tn` on `a8915e8d`); the self-host 102 of 103 and `Driver`, its `__TIME__` line (Windows `a8915e8d`).

**Gates.** REF `a655d85a` built fresh and alone (Linux `~/m8tr`, Windows `work\m8tr`). Linux `~/m8tn` on `0e86e621`
(g++ `-Werror` clean): exclusions rc 0, comment cap rc 0, memory gate rc 0, ast-residue rc 0 (+0), `ide/generate.py`
rc 0 - each its own exit code; `tools/identical.sh` REF against NEW for tms6747, arm64-darwin, x86_64-linux and
x86_64-windows at -O0 and -O2, **8,840 compared, 0 differ**; `typed.sh` 876 passed, 0 failed; `run.sh` 1233 passed, 0
failed; self-compile stage 0 103 of 103 (126 s); `subst.sh bodies` 0 failed (5,120 over 876 units) once `763f1661` re-recorded `template-variadic.templates` (the old record failed: its three reads' reason moved from the size to the pack); the self-host's `-S` REF against NEW 102 and `Driver`. Windows `work\m8tn` on `a8915e8d` (cl /W4 /WX clean):
five spellings against REF **12,390 the same, 0 differ** (6,200 and 6,190). Windows `work\m8tf` on `0e86e621` (cl /W4
/WX clean, built alone): the static gates each rc 0 by its own status; the cases and the self-host read door by door
as Linux (176, 575); the cases' flag 1,239 the same, 0 differ; the self-host's flag 102 and `Driver`; the five spellings at -O0 and -O2 against REF **12,390 the same, 0 differ** (6,200 and 6,190). **Two defects.** Windows'
static step printed `tail`'s status for exclusions, so its rc 0 on `a8915e8d` hid three stale citations Linux found
(mended by `0e86e621`; `boxkit/m8t-win4.sh` reads `PIPESTATUS`). A Linux rebuild from shipped sources kept a stale
object (the tar kept the Mac's mtime, older than the object): its first gates were discarded and re-run from touched
sources.

**A probe, not committed** (`boxkit/m8t-pack.patch`, Linux `~/m8tp` over `0e86e621`): `bindPatternMember` reading a
read's pattern once over the pack as itself leaves the unread counts as they are (176) and the flag 0 differ, and moves
the wall a token on - "expected a type" (`std::forward<Args>(args)...`, the pack's name as a template argument inside
the copied pattern; 44 + 16 + 16 + 12 in the cases) and "'args' is a parameter pack, written without the '...'" (20).
**Left of commit 4**, and so of commit 5's condition (`unread` 0 over the cases and the self-host, identical.sh 0 - the
second holds at every step): unread **176** in the cases and **575** in the self-host; the pack pattern in a read is
three in five of both (about 100 and 409), then a specialization not asked for (15 / 94), the rest by reason above.
src/ lines: 95,548 before, 95,553 after.

**M8.1, commit 4, eleventh step: a pack pattern in a read, a left-out base, a read's own names, 2026-11-18**
(lane m8u, branch `m8-e` from `5702b99a`; code `2b12fc0b`, `208ade05`, `aee4dca3`, `295c2773`, `d98e3d1d`, `e7d3e017`,
`26eb822a`). Still under `-check=templates`; the replay is what emits. REF `5702b99a` built fresh and alone (Linux
`~/m8ur`, Windows `work\m8ur`). **Six steps.** `2b12fc0b`: m8t's probe finished - `bindPatternMember` reads a read's
pattern once, the pack's name in the copy bound as its own parameter (a type name, or a non-type parameter's
reference) with its `packs_` entry shadowed for the copy, and `dependentType` follows a read's `Kind::PackExpansion` to
its pattern, so `args` in `std::forward<Args>(args)...` is dependent; cases unread 176 to 71, self-host 575 to 167.
`208ade05`: a read's mem-initialiser naming the base its class left out (`: B<T>(x)`, `: B<T>{x}`, `leftOutBase`) reads
its arguments and drops the entry; 71 to 65, 167 to 155. `aee4dca3`: a read's local name in an expression over its
parameters is held as an unresolved name (a read spells nothing); 65 to 56. `295c2773`: a read's `rest...` over its
function parameter pack is one argument as dependent as its pattern, and its `sizeof...(args)` a dependent count; 56
to 45. `d98e3d1d`: 12 citations re-pointed, `template-variadic.templates` re-recorded (its three unread reads are read;
two differ in shape from the replay, the pack read once against its members). `e7d3e017`: the `rest...` line holds no
`ExprPtr` of its own (ast-residue on `295c2773` said 2016 to 2017, rc 1). `26eb822a`: a read's `std::remove` beside
`using ::remove;` - the read declared `std::remove`, and both of `topLevel`'s `lookupSignature` calls followed the
using-declaration from the unqualified key to `::remove` ("was not declared"; `gdb` on `Source::fail`,
`boxkit/m8u-why.sh`) - is asked for by the key `declareFunction` gave it; 45 to 40, 155 to 123.

**Read / not read, before (`5702b99a`) and after (`26eb822a`)**, Linux; Windows the same totals. The cases: functions
3,478 / 3,414 / 64 to 3,478 / 3,465 / 13; class members 12,574 / 12,533 / 41 to 12,574 / 12,554 / 20; out-of-line 22 /
21 / 1 to 22 / 22 / 0; partials 607 / 587 / 20 to 607 / 604 / 3; member templates 497 / 447 / 50 to 497 / 493 / 4;
**unread 176 to 40**. The self-host: functions 7,512 / 7,325 / 187 to 7,512 / 7,460 / 52; class members 34,925 /
34,758 / 167 to 34,925 / 34,862 / 63; partials 1,796 / 1,688 / 108 to 1,796 / 1,788 / 8; member templates 1,510 /
1,397 / 113 to 1,510 / 1,510 / 0; **unread 575 to 123**. Shape (a read whose tree differs from its replay's) rose with
the reads, the cases' functions 13 to 48 and member templates 1 to 6: a pack read once against its members. **What is
left, by reason**: a class specialization the program has not asked for (cases 16, self-host 94) - a design, M8.2's:
the class cannot be unmade when the read ends, and a stand-in under the real tag would be found by the later
instantiation; a value with no address (26 in the self-host), a read's `T &en = <dependent value>` in
`bindReference` (`boxkit/m8u-bindref.patch`, not built); the size of a dependent type, reached behind the pack (cases
4: `nontype-pack-deduction`, `nontype-pack-types`, `lambda-returning-lambda`); a pack beside other arguments (3), a
qualified name in a signature's expression (3), and singles.

**The flag against no flag** (`-S`, the cases): 1,239 the same, 0 differ, at every step on Linux; on Windows
`295c2773` and `26eb822a` 1,239 / 0, the self-host 102 and `Driver` (its `__TIME__`).

**Gates.** REF `5702b99a` fresh and alone (Linux `~/m8ur`, Windows `work\m8ur`, cl /W4 /WX clean). Linux `~/m8uk` =
`26eb822a` built fresh and alone (g++ `-Werror` clean): exclusions rc 0, memory gate rc 0, ast-residue rc 0 (+0),
`ide/generate.py` rc 0, **comment cap rc 2** (`26eb822a`'s comment made a group of four; `9511b1be` folds it into its
line); `tools/identical.sh` REF against NEW for tms6747, arm64-darwin, x86_64-linux and x86_64-windows at -O0 and -O2,
**8,840 compared, 0 differ**; `typed.sh` 876 passed, 0 failed; `run.sh` 1233 passed, 0 failed; self-compile stage 0 103
of 103 (94 s); `subst.sh bodies` 0 failed (5,120 over 876 units); the self-host's `-S` REF against NEW 102 and
`Driver`. The same on `~/m8uf` (`295c2773`'s sources): identical 8,840 / 0, typed 876 / 0, run 1233 / 0, selfc 103 /
103, bodies 0 failed, ast-residue rc 1 (mended by `e7d3e017`). Windows `work\m8uf` (`295c2773`, cl /W4 /WX clean):
five spellings at -O0 and -O2 against REF **12,390 the same, 0 differ**. Windows `work\m8uk` (`26eb822a`, cl /W4 /WX
clean, built alone): exclusions, memory, residue, generate rc 0 each by its own status, comments rc 1 (the same group);
the cases and the self-host read door by door as Linux (40, 123); the cases' flag 1,239 / 0; the self-host's flag 102
and `Driver`; five spellings at -O0 and -O2 against REF **12,390 the same, 0 differ** (6,200 and 6,190). **`9511b1be`**
(the comment folded, six citations back by one): Linux `~/m8uk` rebuilt from the touched source, every static gate rc 0
by its own status, cases unread 40 and flag 0 differ, self-host unread 123; Windows `work\m8um` built alone (cl /W4
/WX clean), every static gate rc 0, the cases' flag 1,239 / 0, the function door 13 unread as Linux. Windows' ast-residue counts ExprPtr 0 on every tree (-2016; Linux counts 2016):
its rc 0 there says nothing, the Linux figure is the gate's. **Two defects of the lane's own**: `295c2773` raised the
residue by one `ExprPtr` and `26eb822a` the comment cap by one group - each found by the static gate on its tree and
mended by the next commit; the long gates ran on `26eb822a`, and `9511b1be` after it moves a comment alone.
**Left of commit 4**, and so of commit 5's condition (unread 0 over the cases and the self-host, identical.sh 0):
unread **40** and **123**; the specialization not asked for is two in five of the first and three in four of the
second. src/ lines: 95,553 before, 95,602 after.

**M8.1, commit 4, twelfth step: the small reasons, and the design for a class a read may not make, 2026-11-18**
(lane m8v, branch `m8-e` from `a9d212f3`; code `2a53681d`, `31499903`, `f676397d`, `fecc58d7`, `44137b11`,
`fb5a767e`; `b4acc7ca` and `bb063b66` the citations). Still under `-check=templates`; the replay is what emits. REF `a9d212f3` built fresh and alone (Linux
`~/m8vr`, Windows `work\m8vr`). **Six steps, each its own commit.** `2a53681d`: m8u's patch, built - a read's
reference bound to a dependent value or over a dependent referent (`T &en = f(x);`) is held as its referent's
address, what it binds to being substitution's; the self-host's 26 "a value with no address to bind to" and the
cases' one "would take a converted copy" (`template-sfinae-misc`) are read: 123 to 97, 40 to 39. `31499903`: the
dependent sizes behind the pack, two sites found by `gdb` on the throw (`boxkit/m8v-throw.sh`) - `pointerAdd`'s
stride over a dependent element (`T a[] = { Vs..., T() }`, the initialiser's step to each element) is left to
substitution in a read, and a read's closure over a capture of dependent type is laid out under
`Type::DependentLayout` and marked, as a class door's member is; 39 to 35 (`nontype-pack-deduction`,
`nontype-pack-types` twice, `lambda-returning-lambda`). `f676397d`: "a qualified name other than `T::name`" was
`R2::den < R2::num` in `<ratio>`'s `ratio_less` - `depPrimary` took the `<` after `T::name` for a member template's
arguments; with no `template` before it the name is no template ([temp.names]/4) and `<` is less-than, in a
signature as in a read. **A refusal stricter than the standard, so it goes for every signature**: `f<T>` returning
`enable_if<T::num < 3, int>::type` and `g(V<T::num < 3> *)` are named `_Z1fI1AEN9enable_ifIXltsrT_3numLi3EEiE4typeES2_`
and `_Z1gI1AEvP1VIXltsrT_3numLi3EEE`, clang 19.1.5's names letter for letter (`boxkit/m8v-lt.cpp`, cxx3 on Linux,
clang `--target=x86_64-linux-gnu` on the Windows box; REF refuses it; not added as a case - REF against NEW would
differ on it); 35 to 32, 97 to 96. `fecc58d7`: a pack expanded beside other arguments (`List<Ns..., 4>`,
`Max<Ns..., ...>`, `Box<A..., ...>`) is taken in a read - as dependent as the expansion alone, and a read spells no
name - while a signature's still refuses it by name; 32 to 28, the self-host unchanged. `44137b11`: a read's
`T(a, b)` over its own parameter reads every argument (a constructor's call if `T` is a class, substitution's to
say) where `functionalCast` said "would need a constructor, and `T` is not a class"; 28 to 27
(`template-sfinae-leading-decltype`). `fb5a767e`: a read of a literal operator template (`template <char... Cs> int
operator"" _count()`) takes no parameters, as its specialization does - its own list is checked where it is declared
([over.literal]/5) - where `checkLiteralOperator` asked /3's list of it; 27 to 25 (`user-defined-literal`, two).

**Read / not read, before (`a9d212f3`) and after (`fb5a767e`)**, Linux and Windows the same door by door. The
cases: functions 3,478 / 3,465 / 13 to 3,478 / 3,474 / 4; class members 12,574 / 12,554 / 20 to 12,570 / 12,554 /
16 (four bodies fewer: a class whose head was refused counted one, and read it holds none); out-of-line 22 / 22 /
0, unchanged; partials 607 / 604 / 3 to 606 / 604 / 2; member templates 497 / 493 / 4 to 497 / 494 / 3; **unread 40
to 25**. The self-host: functions 7,512 / 7,460 / 52 to 7,512 / 7,486 / 26; class members 34,925 / 34,862 / 63 to
34,924 / 34,862 / 62; partials 1,796 / 1,788 / 8, unchanged; member templates 1,510 / 1,510 / 0; **unread 123 to
96**. **What is left, by reason**: a class specialization the program has not asked for - cases 16 of 25, the
self-host 94 of 96 - the design below; and singles, each named: the cases' 9 (`expected` a comma, the pattern `twice(xs)...` among a
call's arguments; an integer to an enumeration, a non-type pack of an enumeration's type; a cast between unrelated classes; a
call that would instantiate its callee; a member function template's argument not worked out; an array length 0; a
using-declaration in a class; a base not defined yet; and `function-try-block`, "cannot name the type of this" on
Linux and "the size of a dependent type" on Windows) and the self-host's 2 ("was not declared", "needs a pointer to
a struct or union": `x->m` over an `auto` local of dependent type, `OptPipeline`; `postfix`'s `->` has no dependent
hold).

**The design: a class specialization a read may not make** (no code; the count it would move is the cases' 16 and the
self-host's 94, of 25 and 96 unread). **What it is.** A read reaches `instantiateClass` for an argument list that names
no parameter (`V<3>`, a library class over the program's own types) whose tag the unit has not made. Making it is
`readClassBody`: the interned `Type` completed in place, the type name declared, its members' `Signature`s entered,
its held bodies queued as the unit's. The read's `Lookup` layer gives back the names and the owed bodies when the read
ends; it does not give back the `Type`'s completion or what `Classes` recorded of it - so m8j refused it by name.
**What the read needs from such a class, measured** (`boxkit/m8v-named.patch`, a probe on Linux `~/m8vs`, not
committed: the class held *incomplete* under its real tag - the shape `instantiateClass` already gives a template
declared and not defined - for the program's own ask to complete in place): the cases' 16 become 11 read and 5 not (4
a conversion to or from the class, 1 a static member's name); the self-host's 94 become 29 read and 65 not (49 "has
an incomplete type" - an object of the class declared, or a member reached - and 16 a conversion); the cases' `-S`
with the flag against without 1,239 the same, 0 differ (the self-host's `-S` was not compared). So a read needs **its
name alone in 11 of 16 and 29 of 94**, and in the rest **its members**: their names (which decide the parse -
`C::type x;` has no `typename` to say what `type` is, rightly, because `C` is not dependent), their types (the
conversions: constructors, conversion functions, bases) and that it is complete. **Its layout it does not need** for
the tree - a read keeps no slot - unless a constant asks its size, which none of the probe's leftover reasons names
(an "incomplete type" may hide one). Not measured: which tags they are
(a line breakpoint in the -O2 build did not hold), and whether each is made later in its unit.

**The options.**
(a) *A class read as a pattern of its own* - the class door run with the arguments bound, into the read's layer, gone
after. The door leaves nothing (flag 0 differ), but what it reads is a pattern under a `$` tag nothing else can name.
This class has its real tag, and the type table is not in the layer: either the `Type` is completed and then put back
(a journal over `Type`, `Classes`' specials and vtable record, static members, constants folded - every table
`readClassBody` writes, where one missed is a silent difference), or it is made under a tag of the read's own - and
is then a different `Type` from the one the program's earlier declarations already point at, so every conversion
between the two fails inside the read. Two or three lanes; made again by every read that asks (nothing outlives the
layer); and all of it is deleted at M8.5, when the class is made at the definition for good.
(b) *A deferred check, replayed when the class is asked for* - the read stops, the declaration is noted under the
tag, and the program's own `instantiateClass` of that tag reads it again. One lane. The second read stands later than
the declaration, so names declared in between are visible to it: the replay's own fault today, but the opposite of
what M8.5 binds, so M8.5 takes it out again. A body whose class is never asked for stays unread and is never
instantiated either (its replay is what asks). And under M8.2 the first instantiation of such a body can come before
the class exists, so it still falls back to replay.
(c) *A lookup that answers "dependent" for it.* Its members are then unknown and every expression over it is held.
It cannot serve the uses the probe left: with no `typename` written, a read cannot tell `C::type x;` from an
expression, so it either builds a wrong tree quietly or refuses - the same 5 and 65. And it calls dependent a name
M8.5 has to bind at the definition. Its sound half is the probe's: where only the name is needed the class is
**incomplete**, which is true, not dependent, which is not.
(d) *The class made at the definition, for good* - what the design expects: section 3.4's end state ("non-dependent
names are checked at the definition") and M8.5's row ("non-dependent names bound when the body is read at M8.1")
need the class complete at the read, as clang makes it ([temp.inst]/1; [temp.point]/3 puts it before the declaration
that holds the reference). It was built once: m8i's seating (`789e7b4a`, `askedByPattern_` and `madeWhereAsked`,
four lines; the self-host's `-S` with the flag against without went from 8 of 103 differing to 0 on both boxes) and
deleted by m8j for the layer. Its cost is not lines: a class cxx3 refuses to build, named in a template the program
never instantiates, becomes an error at the definition (clang's answer for a real error, a regression for a refusal
of ours), and the seating is a patch the replay's emission order needs and substitution will not.
**What M8.2's row expects** is the interim, not the end: "a shape the substitution does not cover falls back to
replay by name, counted by `-check=templates`" - an unread body is provided for there, and M8.6 (the deletion) is
where that provision ends. m8j's refusal says "M8.2's to make by substitution"; M8.2's row is function templates,
and a class by substitution is M8.3's.

**Recommended: the incomplete class now, the rest refused by name; (d) as M8.5's first step; not (a), not (b).**
(a) and (b) are each machinery M8.5 deletes, and (a)'s failure is silent. **The steps, as lanes.**
*Lane 1* (Opus, one hour): `m8v-named.patch` gated - a read's unasked class held incomplete under its tag, by the one
helper the `!decl.defined` branch already is; where the read then needs more it refuses by one name of its own ("the
members of a class specialization the program has not asked for yet") in place of the two general messages, so the
count keeps one reason. Gate: the flag against no flag 0 over the cases and the self-host on both boxes, identical.sh
0, names.sh, and m8j's measure of what a read leaves extended to the type table (the `Type`s a read interned, by
tag: each completed by the program's later ask, or never named again). Its one known hazard is m8k's: the interned
`Type` keeps the arguments of whoever asks first, here the read's. Expected: the cases' 16 to 5, the self-host's
94 to 65.
*Lane 2* (Opus, one hour): the measure (d) stands on - for each tag still refused, whether its unit makes it later
(expected: every one a replay reaches), and the unit's `-S` with the class made at the read under m8i's seating
against without; the tags listed in the plan.
*Lane 3* (Fable - it changes what compiles; M8.5's first step, and before M8.6): the class made at the definition
for good, m8i's seating back under the read, the class entered below the layer; each case `run.sh` or `identical.sh`
turns is read against clang. The reason's count goes to 0 and its refusal is deleted.

**Whether commit 5 can come once only these remain: yes**, on three conditions. Commit 5's "unread 0" was written
when unread meant a shape the read could not take yet; these are bodies the read declines because making the class
is not M8.1's to do. A read that declines leaves nothing - the function door has run with no flag since `241cd65b`
with unread never 0 (99 then, 4 now) and identical.sh 0 at m8j's gate and at each lane's since - so what on-by-default risks is what a
read *leaves*, and that is gated by the flag against no flag and identical.sh, not by the unread count. The
conditions: (1) every body still unread is a refusal by name under a **recorded ceiling** per corpus, which
`tests/subst.sh` fails on if it rises or a new reason appears (after lane 1: the class's members, and the singles
above, each named); (2) identical.sh 0 and the five Windows spellings 0 on the tree with the class doors read with
no flag - never yet run: the flag against no flag is its stand-in; (3) `-dump=syntax` names an unread body with its
reason where it shows the others once. What it costs until lane 3: M8.2 replays these bodies (its row's fallback,
counted), and M8.5 cannot check them at the definition.

**The flag against no flag** (`-S`, the cases): 1,239 the same, 0 differ at every step on Linux (the probes and both
gated trees) and on Windows at `fecc58d7` and `fb5a767e`; the self-host 102 the same and `Driver` (2 lines, its
`__TIME__`, as before) on both.

**Gates.** REF `a9d212f3` fresh and alone (Linux `~/m8vr`, Windows `work\m8vr`, cl /W4 /WX clean). Two rounds, each
tree built fresh and alone. **`fecc58d7`** (Linux `~/m8vk`, g++ `-Werror` clean; Windows `work\m8vk`, cl clean):
Linux exclusions (after `b4acc7ca`'s one citation), comment cap, memory gate, ast-residue (ExprPtr 2016, +0) and
`ide/generate.py` rc 0 each by its own status; `tools/identical.sh` REF against NEW for tms6747, arm64-darwin,
x86_64-linux and x86_64-windows at -O0 and -O2 **8,840 compared, 0 differ**; `typed.sh` 876 passed, 0 failed;
`run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103 (117 s); `subst.sh bodies` 0 failed (5,120 over 876
units; no `.templates` record changed); the self-host's `-S` REF against NEW 102 and `Driver`. Windows: the five
spellings at -O0 and -O2 against REF **12,390 the same, 0 differ** (6,200 and 6,190); every static gate rc 0 (its
ast-residue counts ExprPtr 0, which says nothing - Linux's is the gate); the counts door by door as Linux.
**`fb5a767e`** (Linux `~/m8vf`, Windows `work\m8vf`; `bb063b66` re-points 9 citations): Linux every static gate rc 0
by its own status (ExprPtr 2016, +0); identical.sh **8,840 compared, 0 differ**; `typed.sh` 876 passed, 0 failed;
`run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103 (66 s); `subst.sh bodies` 0 failed (no `.templates`
record changed); the self-host's `-S` REF against NEW 102 and `Driver`; the cases unread 25, flag 0 differ; the
self-host 96. Windows (cl /W4 /WX clean, built alone): the five spellings at -O0 and -O2 against REF **12,390 the
same, 0 differ** (6,200 and 6,190); the cases' flag 1,239 / 0; the self-host's flag 102 and `Driver`; the cases and
the self-host read door by door as Linux (25, 96); exclusions, comment cap, memory gate, residue and generate rc 0
each. **Not run**: `names.sh` and `overload.sh` (not in m8u's gate either); no case added for `T::num < x` in a
signature (the probe is `boxkit/m8v-lt.cpp`) - owed with commit 5, when REF and NEW both read it. **Probes kept in
`boxkit`**: `m8v-named.patch` (the design's measure), `m8v-throw.sh` / `m8v-why.sh` (gdb on a read's refusals).
src/ lines: 95,602 before, 95,608 after.
**Left of commit 4**: unread **25** in the cases and **96** in the self-host; the class a read may not make is 16
and 94 of them (lane 1 of the design: 5 and 65), the singles 9 and 2. **Commit 5 can come next** on the design's
three conditions - it does not wait for unread 0.

## M8.1, commit 5: the class doors read with no flag, under a recorded ceiling (lane m8w, 2026-11-18)

Branch `m8-e` from `a18490df`: `75a431ce` (the design's first lane), `35a868ef` (the ceiling in `tests/subst.sh`),
`c5448cb9` (`-dump=syntax` names an unread body), `bc20fad0` (**commit 5**), `0c9690a8` (the ceiling's records and 3
citations re-pointed), and one comment moved for the cap after it. The coordinator's decision, under the user's
standing instruction: commit 5 comes before unread reaches 0, on m8v's three conditions.

**Lane 1 - a read's unasked class held incomplete** (`75a431ce`, `boxkit/m8v-named.patch` as measured): where a read
reaches `instantiateClass` for an argument list that names no parameter and a tag the unit has not made, the class is
interned under its real tag and left incomplete - the shape a template declared and not defined already has - for the
program's own ask to complete in place; a read that needs its members is refused there by the general names ("has an
incomplete type", a conversion's "is A and this is B"). The one name of their own the design asked for is **not
done**: the two messages are counted as they are, and the ceiling holds each.

**Read / not read by door** (bodies / read / unread; `-check=templates`, Linux and Windows the same door by door).

| door | `tests/cases` before (`a18490df`) | after | the self-host before | after |
|---|---|---|---|---|
| functions | 3,478 / 3,474 / 4 | 3,478 / 3,474 / 4 | 7,512 / 7,486 / 26 | 7,512 / 7,486 / 26 |
| class members | 12,570 / 12,554 / 16 | 12,568 / 12,561 / 7 | 34,924 / 34,862 / 62 | 34,923 / 34,882 / 41 |
| out-of-line | 22 / 22 / 0 | 22 / 22 / 0 | - | - |
| partials' members | 606 / 604 / 2 | 606 / 606 / 0 | 1,796 / 1,788 / 8 | 1,796 / 1,796 / 0 |
| aliases | 7 / 7 / 0 | 7 / 7 / 0 | - | - |
| member templates | 497 / 494 / 3 | 497 / 494 / 3 | 1,510 / 1,510 / 0 | 1,510 / 1,510 / 0 |
| **unread** | **25** | **14** | **96** | **67** |

**Not read, by reason.** Before: "a class specialization the program has not asked for yet" 16 in the cases (9 a
class member, 4 the class, 2 a partial's member, 1 a function) and 94 in the self-host (36, 26, 8, 24); the singles 9
and 2. After, the cases' 14: 4 a conversion to or from the incomplete class (class members), 1 a static member's name
in it (a function), and the 9 singles - `expected`, an integer to an enumeration, `type_info` of a dependent type
(Linux's host; Windows' is "the size of a dependent type"), three member templates (a cast between unrelated
classes, a call with a dependent argument, a template argument not worked out), three classes (an array length 0, a
using-declaration of a base's member function, a base not defined yet). The self-host's 67: **49 "has an incomplete
type"** (25 the class, 24 a function), **16 a conversion** (class members), and the 2 singles (`was not declared`,
`needs a pointer to a struct or union`). As m8v's probe measured (16 to 5, 94 to 65).

**The ceiling** (`35a868ef`, records `0c9690a8`): `tests/subst.sh bodies` sums the cases' refusals by door and reason
and its new step `self` does the same over the compiler's 103 sources (in the default steps); each is held against
`tests/subst-cases.ceiling` (**14 in 11 reasons**) and `tests/subst-self.ceiling` (**67 in 5 reasons**) - a reason
the record does not hold or a count above its record fails, a count below prints "record again"
(`SUBST_RECORD=1`). Recorded on Linux `~/m8wf`; the cases' record is the Linux host's (the one `type_info` line), so
`subst.sh` on a Windows host would name that reason - owed if the gate ever runs there.
**`-dump=syntax`** (`c5448cb9`): after the tree, one line a declined body, at the body's place - `file:line:col:
(unread "door: reason")`, the reason as the count has it (`Tree::noteUnread`, `tallyRead` given the body's token).
Shown on `inheriting-constructors`: `...cpp:40:20: (unread "class-members: the class: a using-declaration inside a
class is not supported yet here ...")`, 21 lines where REF dumps 20; `tests/syntax.sh` 3 passed, 0 failed, no record
changed. A read body itself is still not in the dump (its nodes die with the read) - row 5's "shows each body once"
is not done by this.
**Commit 5** (`bc20fad0`): `readDeclaration` and `readMemberTemplatesOf` no longer ask `checkBodies_` - the class,
its out-of-line members, a partial, an alias and a member template are read once where they are declared in every
compile; never from inside a read, as before. `-check=templates` stays the comparison and the count. **Taken, and
identical**: see the gates. Not done of row 5: "a dependent name marked on its `name` node, `typename`/`template`
kept as payload".

**Gates.** REF `a18490df` built fresh and alone on both boxes (Linux `~/m8wr`, Windows `work\m8wr`); every NEW tree
built fresh, one build at a time, g++ `-Werror` and cl /W4 /WX clean. **Lane 1 alone** (`75a431ce`; Linux `~/m8wn`,
Windows `work\m8wn`): the cases 25 to 14 and the self-host 96 to 67 on both boxes, the flag against no flag 0
differ (Linux 1,239; Windows 1,239 / 0); `tools/identical.sh` REF against NEW on four targets at -O0 and -O2 **8,840
compared, 0 differ**; Windows five spellings **12,390 the same, 0 differ** (6,200 and 6,190); the self-host's `-S`
REF against NEW 102 and `Driver` on both. Its typed.sh, run.sh and static gates were not run apart - they ran on
the tree below, which holds it. **Commit 5** (`bc20fad0`, the class doors with no flag; Linux `~/m8wf`, Windows
`work\m8wf`): **identical** - identical.sh for tms6747, arm64-darwin, x86_64-linux and x86_64-windows at -O0 and
-O2 **8,840 compared, 0 differ**; Windows' five spellings (masm, gnu, x86_64-linux, arm64-darwin, tms6747) at -O0
and -O2, text and diagnostics, **12,390 the same, 0 differ** (6,200 and 6,190); the self-host's 103 sources' `-S`
REF against NEW with no flag **102 the same and `Driver`**, whose diff was opened on both boxes and is the one
`__TIME__` string (Linux `.byte ... 49, 57` against `51, 51`; Windows `$_L_str_141`), so **0 of 103**. Linux also:
`typed.sh` 876 passed, 0 failed; `run.sh` 1233 passed, 0 failed; self-compile stage 0 103 of 103 (145 s);
`subst.sh bodies self` 0 failed under the ceiling (14 and 67; 5,120 replayed over 876 units, no `.templates` record
changed); `syntax.sh` 3 passed, 0 failed; the flag against no flag 0 differ; exclusions, memory gate, ast-residue
(ExprPtr 2016, +0) and `ide/generate.py` rc 0 each by its own status - and **`make comments` rc 2** (Syntax.h:83,
this lane's comment between `roots` and `dump`). Windows also: the cases' flag 1,239 / 0, the self-host's flag 102
and `Driver`; the counts door by door as Linux; **`names.sh` 884 passed, 0 failed; `overload.sh` 46 agreed with
clang, 0 differed** (m8v had not run them). **`4057b4de`** moves that one comment (no token of code): Linux `~/m8wg`
built fresh, every static gate rc 0 by its own status (comments 0 over the cap), `syntax.sh` 3 / 0, `subst.sh bodies
self` 0 failed, identical.sh **8,840 compared, 0 differ**, the self-host's `-S` REF against NEW 102 and `Driver`
(its `__TIME__`); Windows' static gates with the header copied into
`work\m8wf` rc 0 each (ast-residue counts ExprPtr 0 there, which says nothing). **Not run on `4057b4de`'s own build**:
Windows' five spellings, names.sh and overload.sh (they ran on `bc20fad0`), and typed.sh, run.sh and the
self-compile on Linux. **Two defects of the lane's own**: `m8w-linux.sh` was replaced on the Linux box
while a chain was reading it, which ended that chain after `~/m8wn`'s build (its counts were run again by hand); and
the comment cap above, found by the gate and mended.
src/ lines: 95,608 before, 95,630 after.
**Left**: unread **14** in the cases and **67** in the self-host, every one a refusal by name under the ceiling; the
design's lane 2 (which tags, and the class made at the read under m8i's seating, measured) and lane 3 (the class
made at the definition, M8.5's first step) take the 5 and 65; the one name for the incomplete class's refusals; a
case for `T::num < x` in a signature (owed since m8v; `boxkit/m8v-lt.cpp`).

## M8.1: the items commit 5 left - one name for the held class, a read body in the dump, the dependent mark, `T::num < x` (lane m8y, 2026-11-18)

Branch `m8-e` from `904fe621`: `002f278c` (one name), `a302b3a6` (the dump and the marks), `40f8f34a` (the case), and
the records re-made on the final tree after them.

**First, the gates m8w did not re-run on `4057b4de`** (its tree is `904fe621`'s: the plan alone differs). REF
`904fe621` built fresh and alone (Linux `~/m8yr`, g++ `-Werror` clean; Windows `work\m8yr`, cl /W4 /WX clean), and
`a18490df` again on Windows (`work\m8ya`) as the five spellings' base. Linux: `typed.sh` **876 passed, 0 failed**;
`run.sh` **1233 passed, 0 failed**; self-compile stage 0 **103 of 103** (132 s). Windows: `names.sh` **884 passed, 0
failed**; `overload.sh` **46 agreed with clang, 0 differed**; the five spellings at -O0 and -O2, `904fe621` against
`a18490df`: **12,390 the same, 0 differ** (6,200 and 6,190), text and diagnostics.

**One name for the held class** (`002f278c`). `Templates::instantiateClass` notes each class a read holds incomplete
in `Parser::readHeld_`; `Parser::refuseHeldClass` refuses "the members of a class specialization the program has not
asked for yet" where a read needs such a class's members - a member or a local of it, a conversion or a reference
binding to or from it, a static member's name in it - in place of the general "has an incomplete type", "is A and
this is B" and "has no static member". It asks only inside a read, so no program's output moves. **Not read, by
reason**: the cases' 14 in 11 reasons to 14 in 11 - **5 under the one name** (4 class members, 1 a
function) and the 9 singles as before; the self-host's 67 in 5 to 67 in 5 - **65 under the one name** (25
the class, 24 a function, 16 class members) and the 2 singles. The ceilings re-recorded on the final tree (Linux
`~/m8yf`): `tests/subst-cases.ceiling` 14 in 11 lines and `tests/subst-self.ceiling` 67 in 5 - a line is a door and a
reason, so the one name stands on two lines in the cases (class members, functions) and three in the self-host (the
class, class members, functions). No count rose; the five general messages became one, as the design asked.

**A read body in `-dump=syntax`, and the dependent mark** (`a302b3a6`, row 5's two halves). Each body a read took is
shown once, after the live tree, as `file:line:col: (read (function ...))` (`Tree::noteRead` from `tallyRead`, the
node kept dead as before - no reader of the tree sees it); a body declined keeps its `(unread "...")` line. A name
node made in a read whose expression's type waits for the arguments is marked `dependent`, and a written `typename`
is kept on it as `typename` (`Node::marks`, `Parser::markReadName`, at the primary name and at a call's callee).
`tests/syntax/template-read.cpp` (recorded) shows both: `(read (function "twice" (var "template parameter x")
(block ... (call (name "g" dependent) (name "x" dependent)) (call (name "T::type" typename dependent) (name "k"))
...)))`. The three earlier syntax records do not change (a mark is set only in a read, whose nodes the live tree never
holds). **Not done of row 5**: `template` after `.`, `->` or `::` is not kept (the name's spelling stops at the
keyword in the primary's walk; changing that walk changes the live tree's names, so it waits for M8.2's reader); a
`typename` in a declaration's type is a `var` node's spelled type, not a name node; a member name over a dependent
object (`x.m`) is held by `dependentPostfix` and makes no name node to mark; a pack's name (`rest` in
`template-variadic`'s `total(rest...)`, whose `.templates` shape line now shows the marks) is left unmarked - its
type is the expansion's, which `dependentOperand` does not count.

**`T::num < x` in a signature** (`40f8f34a`): `tests/cases/template-dependent-less.cpp` - a return type's
`enable_if<T::num < 3, int>` and its negation chosen between, and a parameter's `V<T::num < 3> *` - prints `1 2 1
0`, clang's answer (`-x c++ -std=c++11 -pedantic-errors`, the Windows box), by cxx3 on Linux and on Windows. **Found on the
way**: the body `return V<T::num < 3>::value();` (a static member function of a class template-id whose argument is
dependent, called in a template's body) is refused "'g<A>' ... is outside the SSA lowering: the name 'V', of another
kind" - the case was written round it; the refusal is the SSA lowering's, by name, and is owed a case of its own.

**Gates on the round** (`40f8f34a`, whose records `9a891287` re-made; REF `904fe621` as above; each NEW tree built
fresh and alone after REF, g++ `-Werror` and cl /W4 /WX clean). The two iteration trees came first on Linux: item 2
alone (`~/m8yn`: unread 14 and 67, the reasons renamed, no `.templates` record moved) and items 2 and 3 (`~/m8yd`:
the three syntax records unchanged, `template-read` as shown). **The final** (Linux `~/m8yf`, Windows `work\m8yf`):
`tools/identical.sh` REF against NEW for tms6747, arm64-darwin, x86_64-linux and x86_64-windows at -O0 and -O2
**8,850 compared, 0 differ** (8,840 and the new case's 10); Windows' five spellings at -O0 and -O2 against REF, text
and diagnostics, **12,400 the same, 0 differ** (6,200 and 6,200); the self-host's 103 sources' `-S` REF against NEW
**102 the same and `Driver`**, whose two lines are its `__TIME__` bytes (`12:39:49` against `12:40:03`), so 0 of 103;
`run.sh` **1234 passed, 0 failed**; the cases' `-S` with `-check=templates` against without **1,240 the same, 0
differ** on both boxes; `names.sh` **885 passed, 0 failed** and `overload.sh` **46 agreed, 0 differed** (Windows);
`subst.sh bodies self` on the record step **1 failed** - `template-variadic.templates`, whose shape line spells the
read with its marks now - re-recorded in `9a891287`, the ceilings 14 and 67 under it; Windows' static gates with the
re-pointed `EXCLUSIONS.md` copied in, each rc 0 (exclusions, comment cap 0 over, memory gate, residue, generate).
Linux, on `~/m8yf` with the records in place: exclusions, comment cap (0 over), memory gate, ast-residue (ExprPtr
2016, +0) and `ide/generate.py` rc 0 each by its own status; `syntax.sh` **4 passed, 0 failed**; `subst.sh bodies
self` **0 failed** (5,124 replayed over 877 units; the cases 14 under 14 in 11, the self-host 103 of 103 and 67 under
67 in 5); self-compile stage 0 **103 of 103** (156 s); `typed.sh` **877 passed, 0 failed** (untyped other=38 name=8, as REF). **Two defects of the lane's own**: a Windows
chain written against the wrong tree name (`m8y-wplan2.sh`; its parent stopped by PID before any step ran, its
build left to finish, the chain written again as `m8y-wplan3.sh`), and `m8y-linux.sh` sent to the Linux box a second
time while chains read it - the same bytes (md5 `e71fca6e` both sides), so no chain read another script, but the
rule is broken in form.

src/ lines: 95,630 before, 95,677 after.

**What M8.1 still owes before it closes.** (1) **Fable's reading of the dependent-name marking** (row M8.1's stop,
before M8.2 builds on it), on `a302b3a6`'s `template-read` record. (2) **Unread 14 and 67 under the ceiling** - the
design's lane 2 (which tags the held classes are, and the unit's `-S` with the class made at the read under m8i's
seating, measured) and lane 3 (the class made at the definition, M8.5's first step, Fable) take the 5 and 65 under
the one name; the singles (9 and 2) each a step of its own. Row M8.1 can close with them outstanding, as commit 5
did, since a read declines and leaves nothing - but M8.2 then replays those bodies, counted. (3) The `template`
disambiguator as payload, and the member name's mark (above). (4) `subst.sh` on a Windows host names the Linux host's
`type_info` reason (m8w's note). (5) The lowering refusal above as a case.

**What M8.2 starts with** (the design's next row: substitution for function templates). Its input is what this round
makes visible: the function door's read body (the node `tallyRead` keeps in `declRead_` and the dump now shows once),
with its dependent names marked. Its first step: keep that body alive as the pattern (`pattern` beside `dead`, as the
signature's), and behind `-templates=subst` build a free function template's typed body from it under a `Binding`
with the Typer, the shapes it does not cover falling back to replay by name and counted by `-check=templates`; the
commit that does it deletes M8.1-early's stand-in (`substituteFunction`, `substituted`, `Typer::typeBody`,
`TypedTree::retype`, the census's sibling roots, `subst.sh`'s on-against-off steps) as the row says. Its gate is
row M8.2's: identical.sh replay against subst on the covered shapes, names.sh, overload.sh, the self-host under the
flag.

## M8.1: the design's lanes 2 and 3 met, the held class over a read's own local class, one name for a dependent throw, the ceiling host-free (lane m8z2, 2026-11-18)

Branch `m8-e` from `a51eab69`, with lane m8z's uncommitted work in the worktree: `e179edc2` (the local class named
apart), `3bcbfa65` (one throw name), `3cda1af1` (the open case), `43f308e5` (the new case), `8cf51148` (the record).

**Lane m8z (Fable, stopped at the user's word, 2026-11-18)**: its chains stopped by their own PIDs on both boxes, its
trees removed, its REF of `a51eab69` (Linux `~/m8zr`, Windows `work\m8zr`, built fresh and clean) reused here. Of its
work, **continued**: the one throw name and `tests/open/template-dependent-static-call` (redone, below). **Set aside as
`boxkit/m8z-lane3.patch`**, not landed: lane 3 as m8z wrote it - the wanted class made where the read's declaration
stands and the read made again over it (`Templates::makeWanted`) - took the cases' unread 14 to 9 (the held class's 5
to 0), the flag against no flag 0 differ on both boxes, but the self-host's 65 became **152 behind them** (50 constexpr
member bodies, 75 range-for, 25 a local class defined twice by the read made again) and on Windows 25 of 103 sources
died (rc 127). **Lane 2's measure** is m8z's (`boxkit/m8z-tags.sh` over that patch, Linux): the cases make 2 held
classes in 2 units - `lock_guard<std::mutex>` (asked later) and `Sum<{0}>` (never); the self-host 177 in 29 units, 8
tags, 125 asked later: `vector<opt::Block>`, `<opt::Edge>`, `<opt::Effects>`, `<const opt::Region *>` (25 made, 22
asked later each), `map<string, int>` (24, 22), `vector<bool>` (24, 12), `lock_guard<std::mutex>` (4, 4) and
**`vector<$read$build::Throw>` (25 made, 0 asked later)**.

**Where the design is wrong.** Lane 2 expected every held tag to be one a replay reaches, and (d) makes each at the
definition. `vector<$read$build::Throw>` is neither: `OptCore.h`'s member template `build<EffectsFn>` declares a local
`struct Throw` and a `std::vector<Throw>`, and a class over a template's own local class is **dependent** - the local
class belongs to the templated function and is made again with each specialization, so clang instantiates
`vector<Throw>` per `build<F>`, never at the definition. No program asks for a `$read$` tag; making it is what defined
the local class twice in m8z's patch (its 25). **Lane 3 must read a held class whose arguments name a read's local class
(or closure) as dependent**, and make only the rest at the definition. `e179edc2` names it apart so the count can see
it: "the members of a class specialization over the template's own local class" (`overReadLocal`, beside
`Parser::refuseHeldClass`). In the self-host it moves nothing - in `build`'s body the read stops first at an earlier
held class - so `tests/cases/template-local-class-held` (`Box<L> b;` over a local `L` in a function template; clang
`4 10`, Windows box) holds it: unread 1 under the new name with the flag, the program's own `Box<f<int>::L>` built.

**One name for a dependent throw** (`3bcbfa65`, item 2): a read's `throw` of a value whose type waits for the
arguments is refused "a throw of a value whose type waits for the arguments" in `Parser::throwStatement` before the
type_info and the size are asked - where the Itanium path said type_info and the Microsoft path its own, so the cases'
ceiling read differently by host. m8z wrote it as a second function with the refusal caught; that raised
`tools/ast-residue` (StmtPtr 639 to 641, ExprPtr 2016 to 2018 - design 12.2 lets it only fall), so it is two lines of
`throwStatement` here, which also keeps a read from asking `size()` of a dependent type (lane m8b's exit). The count
did not move (the cases' one throw, under the new name).

**`V<T::num < 3>::value()` in a template body** (`3cda1af1`, item 3): refused by the SSA lowering ("the name 'V', of
another kind", `src/ir/Lowering.cpp` - not this lane's file), so it is `tests/open/template-dependent-static-call`:
clang (`-x c++ -std=c++11 -pedantic-errors`, Windows box) prints `1 0`; cxx3 refuses on both boxes (`open.sh`: 1 still
differs).

**Not read, by reason**: before (`a51eab69`) the cases 14 in 11 and the self-host 67 in 5; after (`8cf51148`, recorded
on Linux `~/m8z2f` with `SUBST_RECORD=1`, the same bytes as written) **the cases 15 in 12** - the 14 as before, the
type_info line now the one throw name, and the new case's one under the local-class name - and **the self-host 67 in
5**, unchanged. The ceilings do not come down: lane 3 did not land.

**Gates** (REF `a51eab69`, m8z's, built fresh and clean on both boxes; NEW `8cf51148` built fresh and alone - Linux
`~/m8z2f` g++ `-Werror` clean, Windows `work\m8z2f` cl /W4 /WX clean - and `29fba9be`'s comment move rebuilt on
Linux). **Linux**: `tools/identical.sh` REF against NEW for tms6747, arm64-darwin, x86_64-linux and x86_64-windows at
-O0 and -O2 **8,860 compared, 0 differ** (on `8cf51148` and again on `29fba9be`); the self-host's 103 sources' `-S`
REF against NEW **102 the same and `Driver`**, its `__TIME__` bytes (`13:39:06` against `13:39:13`), so 0 of 103;
`run.sh` **1235 passed, 0 failed** (both trees); `typed.sh` **878 passed, 0 failed** (untyped other=38 name=8, as
REF); self-compile stage 0 **103 of 103** (75 s); `syntax.sh` **4 passed, 0 failed**; `subst.sh bodies self` **0
failed** (5,126 replayed over 878 units; the cases 15 under 15 in 12, the self-host 103 of 103 and 67 under 67 in 5);
the cases' `-S` with `-check=templates` against without **1,241 the same, 0 differ**; `open.sh` 1 still differs;
on `29fba9be` exclusions, comment cap (0 over), memory gate, ast-residue (ExprPtr 2016, StmtPtr 639, +0) and
`ide/generate.py` rc 0 each by its own status (`8cf51148`'s comment cap was 1 over - three lines over the one line
the throw's check left under them - which `29fba9be` mends). **Windows** (`8cf51148`'s binary; `29fba9be`'s file in
the tree for the static step): the five spellings at -O0 and -O2 against REF, text and diagnostics, **12,398 the same
and 12 that differ** - `posix-headers` and `windows-h-layout` refused on the targets not theirs, whose `#error`
caret stands one column further right under a tree path one letter longer (`m8z2f` against `m8zr`, shown by
`boxkit/m8z2-d1.sh`), so none of the compiler's; `names.sh` **886 passed, 0 failed**; `overload.sh` **46 agreed, 0
differed**; the flag against no flag **1,241 the same, 0 differ**; `typed.sh` **859 passed, 0 failed** (20 not for
this host); static rc 0 each (ast-residue counts nothing there - Linux's is the gate). **The ceiling is host-free**:
`subst.sh bodies self` with `SUBST_RECORD=1` on Windows writes both ceilings byte for byte as Linux's, and the cases'
passes there; at REF it fails on Windows ("the size of a dependent type", the Microsoft path's throw). Four
`.templates` records (`constructor-template`, `template-class`, `template-out-of-line`, `template-variadic`) and
`syntax.sh`'s four records fail on Windows at REF and NEW alike - a host difference of those records, owed. `run.sh` on Windows (`CXX1=./cxx3-msvc.exe`) does not run there: 359 passed, 876 failed - the 355
refusals as recorded and 2 programs printing, every other program failing to build or run, which is the suite's
link step on that host (REF was not measured; winpass is Windows' run gate, the coordinator's).

src/ lines: 95,677 before, 95,690 after.

**What M8.1 still owes.** (1) Lane 3 (the class made at the definition), with a held class over a read's local class
or closure read as dependent, and the 152 m8z's patch uncovered (constexpr member bodies, range-for) met, before the
5 and 65 go to 0; (2) the singles (9 in the cases, 2 in the self-host); (3) Fable's reading of the dependent-name
marking; (4) `template` as payload and the member name's mark; (5) the lowering's refusal of a class template-id's
static member call over a dependent argument (`tests/open/template-dependent-static-call`), M3's file.

## M8.1: lane 3 measured with the correction and set aside again, five singles read (lane m8z3, 2026-11-18)

Branch `m8-e` from `e11346bf`: `62cdff85`, `350f4244`, `a7a0a545` (the singles), `a5006585` (EXCLUSIONS re-pointed),
`6404350b` (the cases' ceiling), this entry.

**Lane 3 with m8z2's correction, measured** (`boxkit/m8z3-lane3.patch`: m8z's `boxkit/m8z-lane3.patch` on `e11346bf`,
and `Parser::refuseHeldClass` wanting a held class only when `overReadLocal` is false - a class over the read's own
local class stays dependent and is never made at the definition). Linux `~/m8z3a` (`SUBST_RECORD=1`) and Windows
`work\m8z3a`: **the 25 Windows sources that died are gone** - 103 of 103 compile; they were m8z's local class
defined twice, `vector<$read$build::Throw>` made below the read and the read made again over it. The cases' unread 15
to 10 (the held class's 5 to 0). **The self-host's 67 became 152**: the 65 held-class reads gone, and behind them
**50** `a constexpr member's body the program has not asked for yet`, **75** `a range-based 'for' needs an array or a
class with 'begin' and 'end'` (both class-members) and **25** under the local-class name (member-templates, the
correction's, read as dependent). Each is one header read in each of the 25 units that include `OptCore.h`: the 25
local-class reads are `build<EffectsFn>`'s `vector<Throw>`; the 75 and 50 are three and two member bodies of
`FlowOf<Entry>` - range-fors over a made `vector<Block>`'s elements (`for (int e : blocks[b].succs)`) and two calls of a
constexpr member. **Not landed**: the unread would rise (152 > 67), against the brief's first condition. What
lane 3 still needs, measured: a read's constexpr member body made where the class is (or the fold left to the
replay), and a range-for over a made class's element read as the replay reads it - then 152 is 25 + 2, under 67.

**The singles, by reason** (9 in the cases, 2 in the self-host before). Read now - five, each a read checking a rule
its parameters decide:
- `template-angle-split` (`the class: an array length must be positive, not 0`): `U m[sizeof(U) - 5]` - a class
  read's bound over its parameters is laid out as nothing, as `int v[N];` is (`Parser::arraySuffix`).
- `template-template-parameter` (`the class: '' is not defined yet - a base class has to be complete`): `struct
  Derived : C<int>` over a template template parameter - `$TT0<int>` left out as a dependent base is.
- `inheriting-constructors` (`the class: a using-declaration inside a class is not supported yet here`): `using
  Holder<T>::Holder;` over a left-out base waits for the arguments, as the base does.
- `nontype-pack-types` (`'' is '' and this is '' - C++ converts an integer to an enumeration only with a cast`):
  a read's non-type parameter had no type (an int); it has its declared type now, as a value's has
  (`Templates::bindTemplateParameters`, [temp.param]/5) - `template <Colour C>`'s `{ C, Red }`.
- `ssa-parameter-pack` (`expected ''`): `add3(twice(xs)...)` - a read's pattern over its function parameter pack is
  one argument as dependent as its pattern (`Parser::parseArguments`).
Left, with the reason each waits: `block-scope-typedef` (`a call with a dependent argument, which would instantiate
its callee`: `f<U>()` - the dependent-name marking, item 3), `constructor-template-in-member-template` (`a cast from ''
to '' - the two classes are unrelated`: `(Mx<K, K>)lhs` over a dependent `V<K>`) and `template-id-qualified-and-deduced`
(`cannot be worked out from this call`: `dot(v)` deducing `N` from a dependent `Vec<N>`) - both deduction against a
dependent argument, item 3's too; `function-try-block` (`a throw of a value whose type waits for the arguments`, m8z2's
one name, kept: a dependent throw read as nothing would add an `Ast.h` statement, which `tools/ast-residue` lets only
fall); `template-local-class-held` (the local-class name, dependent by design). The self-host's two, `'' was not
declared` (`OptJumps.cpp`) and `'' needs a pointer to a struct or union` (`OptPipeline.cpp`), not met this hour.

**Not read, by reason**: before (`e11346bf`) the cases 15 in 12 reasons and the self-host 67 in 5; after (`6404350b`,
recorded on Linux `~/m8z3f` with `SUBST_RECORD=1`, and on Windows the same bytes) **the cases 10 in 7** - the held
class's 5, the three member-template reads above, m8z2's dependent throw and the local-class one - and **the
self-host 67 in 5**, unchanged (the held class's 65, `OptJumps` and `OptPipeline`). With lane 3 (not landed) the
cases would be 5 and the self-host 152.

**Gates** (REF `e11346bf` built fresh from `git archive` on both boxes, Linux `~/m8z3r` and Windows `work\m8z3r`; NEW
`a5006585`'s sources - the ceiling commit after them changes no source - built fresh in `~/m8z3f` (g++ `-Werror`
clean) and `work\m8z3f` (cl /W4 /WX clean); both trees' paths of one length). **Linux**: `tools/identical.sh` REF
against NEW for tms6747, arm64-darwin, x86_64-linux and x86_64-windows at -O0 and -O2 **8,860 compared, 0 differ**;
the self-host's 103 sources' `-S` REF against NEW **102 the same and `Driver`**, its `__TIME__` bytes (`14:22:40`
against `14:22:48`), so 0 of 103; `run.sh` **1235 passed, 0 failed**; `typed.sh` **878 passed, 0 failed** (untyped
other=38 name=8, as REF); self-compile stage 0 **103 of 103** (77 s); `syntax.sh` **4 passed, 0 failed**; `subst.sh
bodies self` **0 failed** (5,126 replayed over 878 units; the cases 10 under 10 in 7, the self-host 103 of 103 and 67
under 67 in 5); the `.templates` records re-made, none changed; the cases' `-S` with `-check=templates` against
without **0 differ**; `open.sh` 1 still differs; exclusions (31 citations re-pointed, 0 uncited, 0 stale), comment
cap (0 over), memory gate, ast-residue (ExprPtr 2016, StmtPtr 639, +0) and `ide/generate.py` rc 0 each by its own
status. **Windows**: the five spellings at -O0 and -O2 against REF, text and diagnostics, **12,410 the same, 0 differ** (equal paths: m8z2's 12 caret columns gone); `names.sh` **886
passed, 0 failed**; `overload.sh` **46 agreed, 0 differed**; the flag against no flag **1,241 the same, 0 differ**;
`typed.sh` **859 passed, 0 failed** (20 not for this host); static rc 0 each; `subst.sh bodies self` with
`SUBST_RECORD=1` writes both ceilings byte for byte as committed; its four `.templates` records and `syntax.sh`'s four
fail there as m8z2 measured them at REF and NEW alike (a host difference of those records, owed; REF not re-run).

src/ lines: 95,690 before, 95,704 after.

**What M8.1 still owes.** (1) Lane 3 (`boxkit/m8z3-lane3.patch`, correction in): the five `FlowOf<Entry>` bodies it
uncovers - a constexpr member's body made where the class is, or its fold left to the replay; a range-for over a
made class's element read as the replay reads it - before the cases' 5 and the self-host's 65 go to 0 and 152 to 27;
(2) the singles left: three deductions and calls over a dependent argument (with item 3), the dependent throw, and
the self-host's two; (3) Fable's reading of the dependent-name marking; (4) `template` as payload and the member
name's mark; (5) `tests/open/template-dependent-static-call`, M3's file.

## M8.1: FlowOf<Entry>'s five bodies read, and lane 3 set aside a third time - the self-host's code changed (lane m8z4, 2026-11-18)

Branch `m8-e` from `52e04f1e`: this entry alone. Lane 3 with the three reads below, its 19 EXCLUSIONS citations
re-pointed and both ceilings re-recorded, was committed as `6e5ee35c`, `10b069e8`, `f0d21552`, gated, and taken off the
branch again when the last gate failed; it is kept whole as `boxkit/m8z4-lane3.patch` (against `52e04f1e`).

**Why not landed: the self-host's `-S`, REF against NEW, 89 the same and 14 differ** (Driver's `__TIME__` and 13
backend and optimizer units - SsaSelect, X86_64Windows, X86_64Linux, MirAlloc, MirWebs, the Opt* - 98 or 346 lines each: NEW emits `std::pair<const std::string, int>::~pair()`
(`_ZNSt4pairIKSbIcEiED1Ev`) and what follows it, which REF does not - a class made at the read owes a member the
program never asked for). Every other gate held (below). What lane 3 still needs: a class made below a read owes no
member body until the program asks (the `seat_` log seats specializations, not the bodies they owe).

**Lane 3, measured** (`boxkit/m8z3-lane3.patch`, m8z2's correction in): a class a read holds and then needs the members
of is made where the read's declaration stands and the read made again; a class over the read's own local class
stays dependent. **The five `FlowOf<Entry>` bodies** (`src/optimizer/OptCore.h`, read, not edited) it uncovered:
- the 75, `a range-based 'for' needs an array or a class ...` (`succBlocks`, `predBlocks`, `joinPads`): the range
  was `decltype(a dependent expression)`, not an element of a made class - `blocks[b]` is a subscript, and
  `Parser::memberCallWith` asked `argsDependent_`, the arguments of whichever call was read last (a dependent
  call in the read before), not its own. The subscript now asks of its own index (`ParserExpr.cpp`), and the range is the
  made `vector<Block>`'s element, `const vector<int>`, read as the replay reads it.
- the 50, `a constexpr member's body the program has not asked for yet` (`connect`'s and `dominators`' `const int
  ... = static_cast<int>(v.size())`): no constexpr member at all - `Constexpr::instantiateForConstant` is asked of
  every call a constant fold meets, and `size()` was a body still waiting. **Folded only in the replay**: a read's
  tree is dead to every reader and the body cannot be replayed into the unit from inside the read's layer, so in a
  read the fold answers "not a constant" and the replay, which asks again, folds it (`Constexpr.cpp`).
- behind them `dominators`' `std::vector<bool> seen(n, false)`: `vector<bool>` held, met by `ParserStmt`'s `X q(a,
  b)` path for a class with no constructor; that path now wants the held class (`refuseHeldClass`), as the
  declaration's own check below it does.

**Not read, by door and reason** (with lane 3; not landed, so the committed ceilings are `52e04f1e`'s). Before (`52e04f1e`): the cases 10 in 7 reasons, the self-host 67 in 5 (25 + 16
class-members and 24 functions under the held class, OptJumps' and OptPipeline's one each). After (lane 3's tree,
recorded on Linux `~/m8z4f` with `SUBST_RECORD=1`): **the cases 5 in 5** (the three member-template reads, the
dependent throw, the local-class one) and **the self-host 52 in 4** - 25 member-templates (`build<EffectsFn>`'s
`vector<Throw>`, dependent by design), OptJumps' `'' was not declared` (`include/memory:98`, `make_shared`'s read),
OptPipeline's `'' needs a pointer to a struct or union` (`rounds<G>`'s `g->add`), and **25 class-members `'' was not
declared - a prototype must come first` at `OptCore.h:301` (`dominators`) on Linux only**: Windows `work\m8z4b` reads
the same sources at 27 in 3 and `OptCore.h` alone reads `dominators` on both hosts - a host difference owed (the
ceiling holds Linux's, the higher). 103 of 103 sources compile on both boxes.

**The singles, set aside** (`boxkit/m8z4-singles.patch`): `memberCallWith` asking of its own arguments everywhere,
not only for a subscript - `rounds<G>`'s `g->add` is `operator->` after `g(new G())` set the flag. Its Windows build
was capped at 9 minutes under the gates' load (a defect of the box's load, not retried), so it is not measured.

**Gates of lane 3's tree** (REF `52e04f1e` built fresh from `git archive`, Linux `~/m8z4r`, Windows `work\m8z4r`; NEW `6e5ee35c`'s
sources built fresh, Linux `~/m8z4f` g++ `-Werror` clean, Windows `work\m8z4b` cl /W4 /WX clean; paths of one
length). **Linux**: run.sh **1235 passed, 0 failed**; typed.sh **878 passed, 0 failed**; syntax.sh **4 passed, 0
failed**; subst.sh bodies self **0 failed** (cases 5 under 5, self-host 103 of 103 and 52 under 52); the `.templates`
records re-made, none changed; open.sh 1 still differs; exclusions (19 re-pointed, 0 uncited, 0 stale), comment cap
(0 over), memory gate, ast-residue (+0 each) and ide/generate.py rc 0 each. **Windows**: names.sh **886 passed, 0
failed**; overload.sh **46 agreed, 0 differed**; typed.sh **859 passed, 0 failed** (20 not this host); the
five spellings at -O0 and -O2 against REF, text and diagnostics, **12,410 the same, 0 differ**; the flag against
no flag **1,241 the same, 0 differ**; static rc 0 each; `subst.sh bodies self` with `SUBST_RECORD=1` writes the
cases' ceiling byte for byte as committed and the self-host's as 27 in 3 (the Linux-only 25 above); its four
`.templates` records fail there as m8z2 and m8z3 measured them (the host difference already owed).
**Linux, the rest**: `tools/identical.sh` REF against NEW for tms6747, arm64-darwin, x86_64-linux and x86_64-windows
at -O0 and -O2 **8,860 compared, 0 differ**; the cases' `-S` with `-check=templates` against without **0 differ**
(902 of 1241 reached the count); self-compile stage 0 **103 of 103** (137 s); **the self-host's `-S` REF against NEW
14 of 103 differ** (above) - the one gate failed.

src/ lines: 95,704 before and after (lane 3's tree 95,827).

**What M8.1 still owes.** (0) Lane 3 (`boxkit/m8z4-lane3.patch`): no member body owed for a class made below a
read until the program asks, then the self-host's `-S` 0 of 103; (1) the Linux-only 25 at `OptCore.h:301`; (2) `boxkit/m8z4-singles.patch` measured and
landed (OptPipeline's), and OptJumps' `make_shared` read; (3) the cases' five: three deductions and calls over a
dependent argument (with item 3), the dependent throw, the local class (dependent by design); (4) Fable's reading of
the dependent-name marking; (5) `template` as payload and the member name's mark; (6)
`tests/open/template-dependent-static-call`, M3's file.

## M8.1: lane 3 landed - a class made below a read seats its implicit members where the program asks (lane m8z5, 2026-11-18)

Branch `m8-e` from `4263ed20`: `81ea4832` (lane 3, m8z4's `boxkit/m8z4-lane3.patch` with the fault mended) and this
entry. src/ 95,704 -> 95,849 lines (+156 -11 over nine files; the mend itself 25 lines in `Templates.h`,
`Templates.cpp`, `Classes.cpp`).

**What m8z4's 14 were: order, not bodies.** Measured on Linux (`~/m8z5a`, the patch as m8z4 left it, against REF
`~/m8z5r` = `4263ed20`): Optimizer, OptJumps and OptValues emit the same symbol set as REF (566, 374, 455 symbols, the
sorted lists identical); each diff is one block moved - `pair<const string,int>`'s `D1 D2 C1(&&) C2(&&)` and
`map<string,int>`'s with it (173 or 224 lines, so 346 or 448 changed), or `opt::MOperand::MOperand()` (132), or
`Str::Str()` (49). `Classes::defineImplicitFunctions` defines the used implicit members in the order of
`lookup_.functions_`, and a class `makeWanted` makes at the read declares its specials there, ahead of everything the
program declares between the read and its own first ask; the seat log moved the specialization, never the functions.
**The mend**: `makeClass` notes the range of functions its making declared beside its seat; `asked` gives each of
them the seat `functions_.size()` at the program's ask; `Templates::seatedOrder` puts a seated function just ahead of
the first function declared after its ask, and `defineImplicitFunctions` walks that order (then what the pass itself
declares, in index order, as before). Nothing is owed that REF did not owe: the four units read 0 lines against REF.

**The Linux-only 25** (`'' was not declared - a prototype must come first`, `OptCore.h:301`, `FlowOf<Entry>::
dominators`): a `[&]` closure called inside a class template's member whose nested class holds the parameter. A probe
(`boxkit/m8z5-p1.cpp`, variants p2-p4) is unread on Linux with the call and with `Block` holding `E`; read with either
taken away (`return 0;` for the call, `struct B { int idom; };`). Not mended this lane, nor why Windows reads it.

**H2, set aside** as `boxkit/m8z5-h2.patch`: `dependentType` takes a struct or enumeration under a read's tag
(`$read$f::L`, `C<$T0>$read3::D`) as dependent, a read function's own enumeration gets a tag of the read's
(`$read$f::E`: REF refuses `read-dependent-local` with "'E' is defined twice" - the read and the replay shared one
interned type), and a function read's array bound over a parameter is refused by name ("an array whose bound depends
on a template parameter"), never folded with 0. Three cases with clang's answer (`read-dependent-member` 223,
`-local` 222, `-bound` "307 507"). Its Windows build passed 9 minutes under the gate's load and was not retried: no
gate has seen it.

**Unread by door and reason** (the ceilings, re-recorded on Linux `~/m8z5f` with `SUBST_RECORD=1`, the same as
m8z4's measure of the patch): before (`4263ed20`) cases 10 in 7, self 67 in 5; after cases 5 in 5, self 52 in 4 -
class-members 25 "'' was not declared - a prototype must come first" (the 25 above), member-templates 25 "over the
template's own local class", 1 "'' needs a pointer to a struct or union", 1 "'' was not declared".

**Gates** (REF `4263ed20` built fresh and alone on each box, `~/m8z5r` / `work\m8z5r`, NEW `~/m8z5f` / `work\m8z5f`):
**the self-host's `-S`, REF against NEW, 102 of 103 the same on Linux and on Windows, the one Driver's `__TIME__`**;
Linux - `identical.sh` 8,860 compared / 0 differ (four targets, -O0 and -O2), run 1235/0, typed 878/0, selfc
103/103, syntax 4/0, subst 0 failed (bodies: 5126 replayed over 878 units, 52 differ in shape), the flag 0 differ,
open 1 (as before), static gates rc 0 each (exclusions 0 uncited 0 stale and 0 re-pointed, comments, memory,
ast-residue, ide); Windows - five spellings 12,410 / 0 differ (6,200 + 6,210), the flag 1241/0, names 886/0, overload 46/0,
typed 859/0, cl /W4 /WX clean, static rc 0 each, subst: ceilings cases 5 in 5, self 27 in 3 (Linux 52 in 4: the 25,
a host difference), 4 `.templates` records failing as at m8z4, m8z3 and m8z2 (host).

**Next**: (1) the 25 (the probe above); (2) H2 from `boxkit/m8z5-h2.patch`, built and gated; (3) the review's H1 and M3
(a floor on `read`, every case unit counted); (4) `boxkit/m8z4-singles.patch`; (5) the cases' five and the review's M1-M7.

## M8.1: H2 landed, the ceiling made sound, and the Linux-only 25 read (lane m8z6, 2026-11-18)

Branch `m8-e` from `d6f4b538`: `24799adb` (H2 from `boxkit/m8z5-h2.patch` unchanged, an Itanium name mended, `tests/subst.sh`),
`e2c89714` (the ceilings re-recorded, EXCLUSIONS re-pointed: 14) and this entry. src/ 95,849 -> 95,873 lines (+25 -1 in
`ParserTemplate.cpp`, `ParserType.cpp`, `Mangle.cpp`).

**H2.** `dependentType` takes a class or enumeration under a read's tag (`$read$f::L`, `C<$T0>$read3::D`) as dependent, a
read function's enumeration is tagged `$read$f::E`, and a function read's array bound over a parameter is refused by name
("an array whose bound depends on a template parameter"), never folded with 0. Its three cases, clang's answers:
`read-dependent-member` 223, `read-dependent-local` 222, `read-dependent-bound` "307 507" - right on both boxes. The
Windows build, alone, took about 2 minutes (m8z5's capped one ran under the gate's load). **What the Itanium targets
found**: `read-dependent-member` wrote `g(C<int,1>::E)` as `_Z1gN8C<int,1>1EE` on x86_64-linux, arm64-darwin and the
C6000 (REF too: no case had an enumeration of a class specialization in a signature), and the assembler refused it.
`Mangle.cpp`'s `enumeration` now writes a specialization scope through `prefix`, `_Z1gN1CIiLi1EE1EE` (c++filt:
`g(C<int, 1>::E)`, beside its sibling `_Z1gN1CIiLi1EE1DE`); an enumeration in a plain class or a namespace is spelled
as before.

**M3: what `tests/subst.sh` enforces now.** The ceiling files hold, beside each reason's count, each door's sums
(`door <name> bodies= read= unread= shape=`) and each body whose read differs in shape from its replay, by place
(`shape <file:line:col>`, every unit, not only the four with a `.templates` record). It fails on: a reason not
recorded or above its count (as before); **a door's `read` below its record** (a body moving from read to unread, or a
door that stops reading); a door's `shape` above its record; **a shape at a place not recorded**; and, for the cases as
for the self-host, **a unit that reaches no count** (one whose `.notarget` names this host's target is excused:
`windows-h-layout` on Linux). Crossed on purpose on Linux (a door's read one above what it reads, one recorded shape
taken away): both fail by name, rc 1. And a run whose self step lost its units to a second run sharing `$OUT` failed
four floors ("read 1949, below the floor of 35148") where the old ceiling would have passed it as "below the record".
Not done: (c), a reason being one message with its names taken out, so a body can still trade places with another
under one reason at one count - the door's read floor bounds it, the reason does not. **`make test`**: not added. The
wave gate runs it whole (`tools/wave-gate`, leg `subst`); its run and typed steps would repeat make test's run.sh and
typed.sh four more times; and its four `.templates` records fail on Windows (the host difference owed since m8z2).
When that is mended, `tests/subst.sh bodies self` belongs in make test, and the rest stays in the wave gate.

**The Linux-only 25** (`'' was not declared - a prototype must come first`, class-members, `OptCore.h:301`): not the
host but the ABI. One Linux build reads the probe at `-arch x86_64-windows` and refuses it at x86_64-linux,
arm64-darwin and tms6747. The shape is smaller than m8z5 had it: a `[&]` or `[=]` closure calling any member of a
captured `std::vector<int>` (`post[x]`, `post.size()`, `post.at(x)`), called, in a member of a class template - no
nested class needed; a closure over an `int[3]`, or over a class template of the file's own, is read. Not rooted
further. **H2 reads it**: the closure is the read's own class now, dependent, so `in(0, 1)` is left to the
instantiation and nothing is resolved over it at the read - every probe read on every target, and the self-host's 25 with them.

**Unread by door and reason** (Linux, the ceilings): before (`d6f4b538`) cases 5 in 5 - member-templates 3 (a member
function template not worked out from the call; a call with a dependent argument; a cast between unrelated classes),
functions 1 (a throw of a dependent value), 1 (a class specialization over the template's own local class); self 52
in 4 - class-members 25 (the 25), member-templates 25 (the local class), functions 1 ("needs a pointer to a struct or
union"), 1 ("was not declared"). After: cases 6 in 5 - member-templates the same 3, functions 1 (the throw), **2 "an
array whose bound depends on a template parameter"** (`read-dependent-bound`, and `nontype-pack-expansion`, which the
read had folded with 0); the local class's 1 is read. Self 27 in 3 - **member-templates 25 "has no begin and end ...
so a range-based for"** (the 25 the local class held: a range-based `for` over the now dependent local class, the
review's M4 refusal), functions 1 and 1 as before; the class-members 25 are read. Windows, the same ceilings measured there: cases 6 in 5, self 27 in 3, every door's sums and the 51 and 3 shapes as on Linux - **the host difference of the self-host is gone**.

**Gates** (REF `d6f4b538` from `git archive`, Linux `~/m8z6r`, Windows `work\m8z6r`; NEW `~/m8z6n`, `work\m8z6n`; each
built alone, paths of one length; g++ `-Werror` and cl /W4 /WX clean): **Linux** - run 1238/0, typed 881/0, syntax
4/0, subst bodies self 0 failed (879 of 879 case units counted, 5136 replayed over 881 units, 54 differ in shape; self
103 of 103), the four `.templates` records re-made the same, open 1 (as before), static rc 0 each (exclusions 0
uncited 0 stale after 14 re-pointed, comments 0 over, memory, ast-residue +0, ide); `identical.sh` REF against NEW on tms6747, arm64-darwin, x86_64-linux and x86_64-windows at -O0 and -O2 8,890 compared, **16 differ, all the two new cases** (`read-dependent-local` 10, REF refusing it with "'E' is defined twice"; `read-dependent-member` 6, the Itanium name on three targets) and 0 among the rest; the cases' `-S` with `-check=templates` against without 0 differ (905 of 1244 reached the count); **the self-host's `-S` REF against NEW 102 of 103 the same, Driver's `__TIME__` the one**; self-compile stage 0 103 of 103 (127 s). **Windows** - the five spellings at -O0 and -O2 against REF, text and diagnostics, 12,424 the same, **16 differ, the same 16**; **the self-host's `-S` REF against NEW 102 of 103, Driver's `__TIME__` the one**; names 888 passed, 1 failed - `read-dependent-local`, three name families none of them H2's (a local enumeration written as at namespace scope, the closure's `$_0`, `T_` where clang substitutes `S1_`), recorded in its `.nonames`, after which the three cases pass; overload 46/0; typed 862/0 (20 not this host); static rc 0 each; subst bodies self: 850 of 850 case units counted, the ceilings held, the four `.templates` records failing as at m8z2-m8z5 (host).

**What M8.1 still owes**: the review's H1 (`staticMemberRef`'s `usedStaticMembers_` without `!patternUses_`, a
case printing `main`); M3 (c); M1, M2, M4-M7 (M4's range-based `for` over a dependent range now holds 25 of the 27);
`boxkit/m8z4-singles.patch`; the four `.templates` records on Windows; why the Itanium targets refused the 25 and the
Microsoft one did not (H2 hides it now); `tests/open/template-dependent-static-call`.

### M8.1 commits 1-3 landed, 2026-11-18 (the coordinator)

- Commits 1-3 (lanes m8b-m8j): the count (`-check=templates`), the signature kept at the declaration, and a function
  template's body read at its declaration in a Lookup layer of its own - the read always on (the flag dropped in
  241cd65b), writing nothing into the unit's tables; the replay still emits. Read 3,379 of 3,478 function templates over
  tests/cases; the self-host's -S the same with the read on and off but Driver's `__TIME__`.
- With them (lane m8x): docs/EXCLUSIONS.md's 11 citations re-pointed after the merge with main; **x86_64-windows: a
  function with a landing pad and no call keeps 8 bytes below its unwind-help word** (000dbd71) - with no outgoing area
  the word sat at rsp+0 and dispUnwindHelp read 0, on main too (ssa-ms-try, ssa-try-catch, nested-parse-state); and two
  gate mends: win-structure's unwind faults fail the step (6cede9be - m6g had passed with 6), and a worse cycle case
  passes only through tests/tms6747-cycles.txt (ca4eaa3d).
- **Gate: tools/wave-gate m81j -w dbdce549 at ac71cf9d: 43 passed, 0 failed, 4 not run** (the fixed points, not owed).
  The first gate of this tree (m81i) failed only on the unwind mend's 31 unnamed x86_64-windows differences (the frame 8
  bytes larger, its unwind bytes and frame offsets moved); m81g and m81h were void (a Linux build stopped under load).
- tests/wave-expected.txt emptied (31 rows were this wave's). Commit 4 (the class doors) continues on m8-e.

### The Fable review's H1 landed: a read marks nothing used, 2026-11-18 (the coordinator)

- The one Fable run the user allowed (review m8rev, boxkit/m8rev-review.md: 2 high, 7 medium, 3 low) found H1: a pattern's
  read marked a static data member used, so a template never instantiated that names `Box<int>::s` emitted its dynamic
  initialiser - on main 95e06424 the case printed `init ran` twice (clang: once). Lane h1 (63ed234d) marks only outside a
  read at four sites: staticMemberRef, an external thread_local's wrapper, a copied string's label, a mem-initialiser's
  constructor; case tests/cases/template-unused-static-init (clang's .expected; its class half is read once m8-e lands).
- **Gate: tools/wave-gate h1g -w 95e06424 at 099f3fb0: 40 passed, 0 failed, 4 not run** (the fixed points, not owed); the
  only output difference is the new case (the base build emits the initialiser). tests/wave-expected.txt emptied.
- H2, M1-M7 and L1-L3 are M8's lanes' (m8z6 on m8-e: H2 and M3 first).

### m8-e merged with main 15254936, checked before its gate, 2026-11-18 (lane mw11)

- **The merge** (887ec84a on `m8-f`, from c7208333 and main's 116 commits since 3a922049): two conflicts, both in documents -
  this file took both sides (m8-e's commit 4 and H2 entries, then main's commits 1-3 and H1); docs/EXCLUSIONS.md's 18
  hunks differed only in line numbers, m8-e's side taken and 13 citations re-pointed by tools/exclusions on Linux
  (b4b2c60e, 0 uncited, 0 stale). The eleven source files both sides changed merged without a conflict; H1's four
  `!patternUses_` guards and M8's read doors stand side by side. src/ lines: 95,873 (m8-e) and main's walker deletion -> 94,228.
- **Built**: g++ -Werror (Linux) and cl /W4 /WX (Windows, first and alone) clean.
- **identical.sh** against REF main 15254936, four targets at -O0 and -O2: 9,100 compared, 26 differ on Linux; Windows's five
  spellings 12,520, 26 differ - on both all M8's own: read-dependent-local 10 (main refuses it), template-dependent-less 10
  (main refuses `T::num < 3` in a signature), read-dependent-member 6 (the Itanium name of a class specialization's
  enumeration; Itanium targets only). The self-host's -S against REF: 101 of 102 the same on each box, Driver by its
  `__TIME__` (102 sources: main deleted Walker.cpp).
- **Suites**: Linux run 1246/0 (template-unused-static-init passes, its class half read), typed 902/0, syntax 4/0, open 1
  (as before), the four `.templates` records the same; Windows names 910/0, overload 46/0, typed 883/0. Static gates rc 0
  each: exclusions 0/0, comments 0 over, memory, ide, ast-residue 2012/635/158/357/0/9/717 on both REF and NEW (+0 from
  M8; main's baseline says ast-lines 722, main measures 717).
- **Not held: tests/subst.sh self** (cases hold: 6 unread under 6, 51 shapes of 51). Every self door's bodies fell with main's
  sources (functions 7512 -> 7404, class-members 35148 -> 34628, member-templates 1535 -> 1515, partial-members 1796 ->
  1766), and so its reads under their floors; two counts rose, so the ceilings were not re-recorded: unread 27 -> 28
  (`make_owned`, moved by main from Ast.h to Owned.h, is unread "'' was not declared" in X86_64Linux.cpp, which includes
  Owned.h after optimizer/Optimizer.h - a pack parameter named `args` is unread there, one named `a` is read) and
  class-members shape 57 -> 58 (not yet placed); the function door's shape place moved Ast.h:131:20 -> Owned.h:8:20.
  Owed by M8's next lane before the gate.

### subst.sh self made to hold after the merge with main, 2026-11-18 (lane m8g1)

- **The unread that rose, 27 -> 28, and the one before it** (7b84327e): `packParameter` (ParserClass.cpp) records an
  instantiation's function parameter pack under its written name in `packs_`, and nothing undoes it. A later read of a
  template whose own pack has that name found the stale binding in `packPatternEnd`/`bindPatternMember`, renamed it
  `args$0` and refused "'args$0' was not declared" - Owned.h's `make_owned` after `std::function`'s `operator()(Args...
  args)` in X86_64Linux.cpp, and (m8-e's one) `<memory>`'s `make_shared` in OptJumps.cpp. A parameter named `a` met no
  stale name. Mend in the read: `Templates::readDeclaration` saves `packs_`, clears it for the read and restores it, so
  nothing outside a read changes. Case template-read-pack-name (clang -std=c++11 on Windows: `3 2`; cxx3 the same on both
  boxes) with its `.templates` record: 3 functions read of 3, where the unmended tree read 2.
- **The class-members shape 57 -> 58, named**: the body is `include/map:92:8`, `map::operator[]` - its read holds `V()`
  as a cast over a dependent V, its replay for a class V as a class temporary (`(call (name "V"))`). It is the same body
  that differs in 22 other units; the 58th unit is Tms6747.cpp, which main's sources (Tms6747.h without Walker.h, with
  Regions.h) now make replay `map<..., class>::operator[]`. A read cannot know V's kind, so it is not mended here; the
  record stays at m8-e's 57 and subst.sh self fails by that line alone, owed to the coordinator's ruling.
- **Re-recorded** (0b995713, then 31a53fc2 re-pointing 3 Templates.cpp citations): self floors fell door by door by the
  bodies main's sources removed - functions 108 removed, floor 7510 -> 7403 (107: the mend reads one more), class-members
  520 and 520, member-templates 20 and 20, partial-members 30 and 30; unread 27 -> 26; function-door shape place
  Ast.h:131:20 -> Owned.h:8:20; class-members shape kept at 57. Cases floors up, the new case's two shape places added.
- **Gates at 31a53fc2**: identical.sh against REF 66f06d01 - Linux four targets 9,110 compared, 0 differ; Windows five
  spellings 12,530, 0 differ (the new case the same on both). Linux run 1247/0, typed 903/0; Windows names 911/0, overload
  46/0. subst.sh bodies self: Linux 1 failed, the class-members shape 58 alone; Windows 6 failed - that line and the five
  `.templates` records, which differ from cxx3-msvc's output by CR alone (as at m8z5). Static gates rc 0 on both boxes.
- **The class-members shape recorded at 58** (coordinator's ruling, 2026-11-18): no new shape - the same known body,
  `include/map:92:8` (`map::operator[]`, `V()` read as a cast where the replay makes a temporary), counted in one more
  unit, Tms6747.cpp, which main's changes made include it. The read cannot know V's kind (owed to M8.2's binding). The
  record lists no class-door places, so the count alone moves. The coordinator judges this rule; it is not a user's
  ratchet. Windows' five `.templates` records that differ by CR alone stay as they are, owed.

### M8.1 commits 4 and 5 landed, with H2 and the merge with main, 2026-11-19 (the coordinator)

- Commit 4 (the class doors read: class members, out-of-line members, partials, member templates, aliases) and commit 5
  (the doors read with no flag, under recorded ceilings tests/subst.sh enforces), lanes m8k-m8z6 and m8g1; the Fable
  review's H2 (dependent nested/local classes, closures, enums; a value-dependent bound) and its M3 (the ceiling a sound
  gate: floors on read, shape compared over every unit, every case unit counted); an Itanium name for a class
  specialization's enumeration; a stale pack name across reads mended. Unread: cases 6, self-host 26 (Linux = Windows).
- Merged with main 15254936 by lane mw11 (two document conflicts; no merge fault); against main only M8's own cases differ.
  The self ceilings' floors fell by exactly the bodies main's sources removed; class-members shape 57 -> 58 is the
  coordinator's ruling (no new shape: map::operator[]'s known body in one more unit, Tms6747.cpp).
- **Gate: tools/wave-gate m81k -w 15254936 at 776fc5c8: 39 passed, 1 failed, 4 not run** (the fixed points, not owed).
  The one FAIL was identwin "named and the same": read-dependent-member's three x86_64-windows rows named a difference
  that is not there (the Itanium fix does not touch a Microsoft name). Those rows taken out; the gate's own got list
  against the rows left, checked by the coordinator: 6 and 6, the same. Landed on that.
- Owed: the Windows CRLF compare (branch m8-crlf, the user's ask - lands with the next M8 wave); M1, M2, M4-M7, L1-L3 of
  the review; M3 (c); boxkit/m8z4-singles.patch; tests/open/template-dependent-static-call.

## M8.1: the review's M5, M6, M1, M2 and M4, and what the mark means (M7) (lane m8h1, 2026-11-19)

Branch `m8-g` from `0177b5cc`: `6184e0a2` (M5), `1c721fd0` (M6), `91ede77c` (M1), `139213de` (M2), `7700d6b4` and `01dd0dc7` (M4's
case), `fbe8007b` (the cases ceiling, the syntax record), `9c769fc3` (EXCLUSIONS: 11 re-pointed) and this entry. src/ 94,232 -> 94,287 lines (+63 -8 in `ParserDependent.cpp`, `ParserExpr.cpp`,
`ParserOperator.cpp`, `ParserType.cpp`, `Parser.h`, `syntax/Syntax.h`, `syntax/Syntax.cpp`). Every clang answer below is
clang 19.1.5 `-x c++ -std=c++11 -pedantic-errors -nostdinc++ -isystem include` on the Windows box.

- **M5** (`Parser::expr`): a comma whose left operand has a dependent type has a dependent type ([temp.dep.expr]/1), so
  `g((x += 1, k))` reads `(call (name "g" dependent) (comma "," ...))` where `g` was unmarked and resolved to `g(int)`.
  Case `template-read-comma`, clang `3 5`. (An overloaded `operator,` is refused at its declaration, so no case can show
  the type it would give.)
- **M6** (`Parser::refuseUntypedMember`, from `unqualifiedSpecifiers`' two template-id paths): a class template-id whose
  argument tokens name a bound type or non-type parameter, followed by `::name` where a type is read and no `typename`
  was written, is refused by name, as `T::type` is: "'Sel<(N>3)>::type' names a member of a class whose arguments depend
  on 'N', and whether that is a type depends on them - write 'typename ...'" - at the read and at the replay alike, so the
  program is refused as clang refuses it ("missing 'typename' prior to dependent type name"). A template-id naming a
  class being defined (the current instantiation, `Own<N>::type` inside `Own`) is exempt. Cases
  `template-typename-dependent-id` (`.error`) and `template-typename-not-needed` - what clang takes with no `typename`:
  the current instantiation's member, `k = Sel<(N > 3)>::s(k)`, `Sel<sizeof(T) == 4>::c`, `Sel<true>::type`,
  `sizeof(Sel<(N > 1)>::c)` - clang `8 3`. **Found**: `Sel<(N > 3)>::s(k);` as a statement is refused, by REF too
  ("'Sel<1>' has no member type called 's'": `atTypeName` says a class template-id starts a declaration) - a refusal
  stricter than the standard, now `tests/open/template-static-call-statement` (clang `s 7`, `7`); its mend is in the
  statement builder's question, lane m6d-s4h's file, not touched.
- **M1** (`Parser::unfoundResult`, `markReadName`, `syntax::Unfound`): which names change - exactly an *unqualified* name
  that no scope declares, read in a class whose base was left out (`primaryNode`'s last `leftOutBase(nullptr)`): it
  stays read (no body moves to unread) but its node is marked `unfound`, never `dependent`; clang refuses it
  ([temp.dep]/3: unqualified lookup looks in no dependent base), `tests/open/template-left-out-base-unqualified`
  (REFUSE; cxx1's replay finds `nb` and prints 1, as before - the over-acceptance M8.5 is to refuse, now visible in the
  tree). Unchanged, and right to be: `this->nb`, `C::nb` and a mem-initialiser naming the base - members of an unknown
  specialization ([temp.dep.expr]/5), still `dependent` (case `template-left-out-base-qualified`, clang `4`); a call
  `g(nb)` over the unfound name still waits (`g` dependent), since nothing in it may be bound at the definition.
  Not made a hard refusal: a read declines by name and the replay decides, so a refusal from a read would be an
  unread body, not a refused program; and the self-host and `include/` are not measured against it yet.
- **M2** (`Parser::dependentPostfix`): after `.template` / `->template` over a dependent object, the member node keeps
  `template` and the written arguments: `(member ".template cv<int>" (name "b" dependent))` against `(member ".cv" ...)`
  for `b.cv(k)` - the two are two trees now. Case `template-read-member-template`, clang `4.5`. Not done: the review's
  second half, `conv<int>(k)` (a function template-id with non-dependent arguments) reading `(name "conv")` - the
  primary's name builder, owed.
- **M4**: `tests/cases/template-read-shapes.cpp`, ten valid shapes, each in a template of its own, instantiated
  (clang `3 3 3 4` / `1 8 4 1 11 1`), each unread by its reason: range-for over `T` and over `const T &` (2, "a
  range-based 'for' needs an array or a class with 'begin' and 'end'"), `T::s;` and `T::f(k);` (2, "names a member of
  the template parameter ... depends on the argument"), `int (T::*pm) = 0;` ("'T' is not a class"), a local class
  holding a `T` and `alignof(T)` (2, "the size of a dependent type"), `dynamic_cast<T *>(&x)` ("casts to a pointer to a
  class ... and 'T' is neither"), `int a[N];` ("has an incomplete type" - not H2's bound message) and `char
  b[sizeof(T)];` ("an array whose bound depends on a template parameter"). Two are out: `T::template X<int>::s;` (a
  member class template is refused whole, "not supported yet") and **`T{};`, which under `-check=templates` refuses the
  whole unit** ("expected a name", REF too; without the flag it compiles) - owed, as it would fail subst.sh's count.
  None is mended: range-for and the expression statements are decided in the statement builders (lane m6d-s4h's).
- **M7, what the mark means, and what M8.2 needs it to mean.** `dependent` on a name node is *the expression's type waits
  for the arguments* (type-dependent, [temp.dep.expr]) - `markReadName` sets it on a name, and on a call's callee from
  the call's type - and nothing more; it is not [temp.dep]'s *dependent name*. It is on every local and parameter of
  dependent type, on members of the current instantiation, and it is absent where an argument is only value-dependent
  (`g(N)`). `typename` records a written `typename`; `unfound` (M1) a name no scope has. **M8.2 reads `dependent` as
  type-dependent only.** What M8.2 needs before it binds a call at the definition, and does not have: (1) a
  `dependent-name` mark set only on an unqualified callee with a type-dependent argument ([temp.dep]/1) and on a name
  qualified by a dependent scope; (2) on such a callee, the functions unqualified lookup found at the definition
  ([temp.dep.candidate]: those, and ADL from both contexts); (3) the explicit arguments of a function template-id
  (M2's second half). Until (1)-(3) land, M8.2 binds at the definition only a name with no mark, and replays every body
  whose tree holds an `unfound` name, a `dependent` callee, or a member template-id.

**Unread by door and reason** (Linux, `tests/subst.sh bodies self`). Before (`0177b5cc`): cases 6 in 5 - functions 3
(a throw of a dependent value 1, an array bound 2), member-templates 3 (1 each: a member function template not worked
out, a call with a dependent argument, a cast between unrelated classes); self 26 in 2 - member-templates 25 (range-based
`for` over a dependent range), functions 1 ("needs a pointer to a struct or union"). After: **self 26 in 2, the same
bodies, every door's read and shape as recorded** (M5, M6, M1 and M2 moved no body of the compiler's own); cases 17 in
12 - the old 6 unchanged, and the new cases' 11: `template-read-shapes` 10 in 7 reasons (above; the bound reason 2 -> 3)
and `template-typename-not-needed` 1 ("has no member type called", the same in REF). The cases ceiling was re-recorded
for those alone: functions bodies 3490 -> 3503, read 3487 -> 3489, unread 3 -> 14; class-members 12570 -> 12575;
member-templates 497 -> 498 and 494 -> 495; no shape, no old count up.

**Gates** (REF `0177b5cc` from `git archive`, Linux `~/m8h1r`, Windows `work\m8h1r`; NEW `~/m8h1n`, `work\m8h1n` at
`7700d6b4`'s source - the later commits change cases and records only; each built alone, paths of one length; g++ `-Werror`
and cl /W4 /WX clean). **Linux**: `identical.sh` REF against NEW on tms6747, arm64-darwin, x86_64-linux and x86_64-windows
at -O0 and -O2 **9,160 compared, 0 differ** (it leaves out a `.error` case; the new cases compile the same under both);
**the self-host's `-S` REF against NEW 101 of 102 the same, Driver's `__TIME__` the one**; self-compile stage 0 102 of 102
(159 s); run 1253/0; typed 908/0; syntax 5/0 (the new `template-read-marks` read by hand); subst bodies self 0 failed
with the committed ceilings (906 of 906 case units counted, self 102 of 102); open 34 differ, 2 agree - the same two
REF closes, and the two new entries OPEN as recorded; static gates rc 0 each (exclusions 0 uncited 0 stale after 11
re-pointed, comments 0 over, memory, ast-residue, ide). **Windows**: the five spellings at -O0 and -O2 against REF, text and
diagnostics, **12,580 the same, 10 differ - all `template-typename-dependent-id`**, which REF accepts and NEW refuses (M6,
its `.error`); **the self-host's `-S` REF against NEW 101 of 102, Driver's `__TIME__` the one**; names 915 passed, 1
failed - `template-left-out-base-qualified`'s inline constructor of `B` emitted (CLAUDE.md's rule; `1cf65309` takes the
constructor out, and it passes; the cases ceiling's class-members 12575 -> 12574 with it, `6660c1ef`, and subst.sh holds
on Linux again); overload 46/0; typed 889/0 (20 not this host); syntax 5/0 and **subst bodies self 0 failed** (874 of 874
case units, 17 under 17, self 26 under 26, shapes 53 and 3) - the CRLF fix holds; static rc 0 each. **run.sh on Windows:
906 of the printing cases "fail" by CR alone** - `tests/run.sh` diffs `.expected` against output a Windows build writes
with CRLF (the m8-crlf fix covered subst.sh and syntax.sh); the 343 refusals pass; a host difference of the suite, owed
(`tr -d '\r'` at its `diff`), not of this round. Self-compile (`tools/windows/self-host.cmd`, stage 0, after the chains: its stage 0 re-runs
`msvc\build.cmd`, which cannot replace a `cxx3-msvc.exe` the chains hold open): 102 of 102 sources, stage 1 built.

**Owed**: M2's `conv<int>(k)`; the statement builders' three refusals (`Sel<...>::s(k);`, `T::f(k);`/`T::s;`, range-for
over a dependent range - 25 of the self-host's 26); M1's refusal (M8.5); M7's (1)-(3) (M8.2); the review's M3 (c), L1-L3;
`boxkit/m8z4-singles.patch`; run.sh's CR compare on Windows; `T{};` under `-check=templates` (a unit refused).

### The Fable review's M items and the Windows CRLF compare landed, 2026-11-19 (the coordinator)

- Lane m8h1: the review's M5 (a comma over a dependent operand is dependent), M6 (`typename` required for
  `X<dependent>::type`, refused as clang refuses it; not stricter than clang where none is needed), M1 (an unqualified name
  not found under a left-out base marked `unfound`; `this->nb`, `C::nb` stay dependent; the unqualified case open), M2
  (`.template cv<int>` kept in the tree), M4 (ten common shapes, each unread by its reason), M7 stated for M8.2 - each with
  a case from clang 19.1.5. Unread: self-host 26 unchanged; cases 6 -> 17, all eleven new from the new cases.
- The user's ask, branch m8-crlf: tests/subst.sh's .templates compare and tests/syntax.sh strip CR on both sides - on
  Windows syntax.sh 4/0 and subst.sh bodies 0 failed (they failed there alone before).
- **Gate: tools/wave-gate m8gg -w 487fd9cb at 839a1c5e: 40 passed, 0 failed, 4 not run** (the fixed points, not owed).
- Owed: run.sh compares output without stripping CR on Windows (all printing cases fail there; the Windows legs of the
  gate are winpass, not run.sh); `T{};` under -check=templates refuses the unit, a static call as a statement refused
  (both in REF too); `T::f(k);` and range-for in the statement builders (M6d's); M2's `conv<int>(k)`; M1's name refused
  outright (M8.5); M7's mark and the functions found at the definition (M8.2).

## M8.2 split into slices, and slice 1's scaffolding (lane m82a, 2026-11-19)

Branch `m8-2a` from `6b97ad9a` (m6d-w: M8.1 whole and the review's M items in). **What `-templates=subst` does today**: it is not
refused - it switches on M8.1-early's stand-in (`substituteFunction`: a specialization of the stand-in's subset made from a
replayed sibling's body, retyped), falling back to replay under `-g` and on the C6000; `CXX1_TEMPLATE_SUBST=1` the same.
**Each slice is one commit of identical output**: `identical.sh` REF `6b97ad9a` against NEW without the flag 0 differ on four
targets; with `-templates=subst` on NEW, 0 differ on the shapes the slice covers; everything else replays by name and is
counted by `-check=templates` (with the flag: `-check=templates subst (N): covered|replayed: <reason>`).

| slice | what | covers | still replays |
| --- | --- | --- | --- |
| 2a (this) | `Templates::Binding` (a type parameter to its `TypeId`), the read body kept by `afterParams` (`readBody_`), `Templates::instantiate` asked first under the flag, `substShape` - the census of what slice 1 covers; `instantiate` builds nothing yet and always declines | nothing built; counted as `covered` | everything (the stand-in still runs after it, unchanged) |
| 2b | the Typer under a binding: `Typer::typeFunction` over the read root with `Kind::TemplateParam` i bound to `types[i]` *while* typing (arithmetic conversions, implicit conversions made under the bound types - not a retype after); the specialization's `Function` shell (params, locals, frame) still from its own replay, its lowered body the substituted one | a free function template, type parameters only, whose read body holds names, literals, unary/binary/assign/conditional/comma/subscript operators and the plain statements (`substShape`'s list) - no call, member, `this`, cast, `sizeof`, `new`/`delete`, `throw`, lambda, `try`, `switch`, `goto` | the frame (the replay runs, its body is dropped); every other shape |
| 2c | the frame from the binding: params and locals laid from the read's `var` nodes by the parser's own layout (one function, shared, not copied); the replay no longer runs for a covered shape; the Itanium pattern signature from the syntax signature | 2b's shapes, built with no replay | the rest |
| 2d | calls: M8rev M7 (1)-(3) first - a `dependent-name` mark, the functions unqualified lookup found at the definition, a function template-id's explicit arguments - then `call` nodes; `sizeof`, casts; members over a bound class | 2c's and calls | non-type parameters, packs, member templates, class doors |
| 2e | the stand-in deleted whole, as the M8.2 row says (`substituteFunction`, `substituted_`, `Typer::typeBody`'s retype, `TypedTree::retype`, the sibling root in `Typer::census`, subst.sh's on-against-off steps) - once 2c covers the stand-in's subset | the same | the same |
| 2f | a non-type parameter bound to a value (M8rev L2: a dependent value refuses to fold, by name, before any is substituted) | 2d's with non-type parameters | packs (M8.4) |

**Slice 2a, src/** `Templates.h` +7, `Templates.cpp` +47 (`instantiate`, `substShape`, the census line, the read kept in
`tallyRead`, the binding made in `instantiatePending`): src/ 94,181 -> 94,235 lines. Nothing without the flag reads what it adds;
with the flag it adds lines only to `-check=templates`'s report.

**The census of slice 1** (Linux, `-check=templates -templates=subst -S` over the 1,269 cases, each function specialization the
fixed point makes, summed): **covered 258**; replayed by reason - a `cast` in the body 198, a `call` 181, a non-type, pack or
template parameter 177, a `member` 20, a `literal` (`true`, `false`, `nullptr`) 14, a `sizeof` 10, no body read at the declaration
7. So slice 1's shapes are 30% of the function door over the cases (258 of 865), and `call` and `cast` are 2d's first weight.
`instantiate` builds nothing yet: the 258 replay (17 of them still taken by the stand-in after it, as before).

**Gates** (REF `6b97ad9a` and NEW built in one Linux tree, `~/m82a-ref.exe` and `~/m82a-new.exe`; Compiler++ not in the tree,
so the cases alone): `identical.sh` REF against NEW at -O0 and -O2 - x86_64-linux 1,848, tms6747 1,848, x86_64-windows 3,696
(both spellings), arm64-darwin 1,848 compared, **0 differ**; with `-templates=subst` on NEW against REF without it - x86_64-linux
1,848, x86_64-windows 3,696, arm64-darwin 1,848, **0 differ** (the C6000 is refused the flag whole, so not run); run.sh 1263/0, and
with `-templates=subst` 1263/0 (every case passes, none fails; the slice's 258 covered bodies fall back); subst.sh 0 failed (on =
off 952 of 952, ceilings cases 17 under 17, self 26 under 26); self-compile of `Templates.cpp` by NEW rc 0; static: exclusions
0/0, comments 0 over, memory gate held, ast-residue not raised (a `const syntax::Node &` read as a node-ref, 325 -> 326, mended
to a copy before the commit); g++ `-Werror` clean; **Windows** cl /W4 /WX clean (83 s alone, 76 s for the mend), names.sh 924
passed 0 failed, overload.sh 46/0.

**Where it stopped**: 2b is the next commit - the Typer typing the read root under the binding. Not done in the hour because the
read's records type each operand as `Kind::TemplateParam`, so a retype after typing (the stand-in's way) misses the conversions
a bound type asks for (`T a = x * 2` with `T = double`): the binding must be in the Typer while it types.

## M8.2 slice 2b: the read body typed under the binding, lowered where it types as the replay does (lane m82b, 2026-11-19)

Branch `m8-2b` from `2e824d8b` (slice 2a). **What it does, under `-templates=subst` only**: `Templates::instantiate`, for a
specialization 2a counted `covered` whose type parameters are bound to scalars (a class's locals are built by records keyed on the
replay's own nodes, `Typer::localBuilt`), keeps `Bound` - the read root and a `Substituted` from `$T<i>` to the argument - by
symbol, and lets the replay run as before (it still lays the frame: 2c's). `Typer::typeBody` types the replay's root, then the read
root with `bind_` set, so `Typer::bound` maps each type the Typer takes from the parser - a `var`'s declared type, a name's, a
record's, a local's - *while* it types, and the arithmetic conversions are the bound types' own. `Typer::typedApart` holds the two
typed bodies against each other field by field, every fact the lowering reads but the node ids (kind, text, place, type, cast,
folded value, literal, forms, the record whole); the lowering takes the read where nothing is apart, else the replay's, by name.
`-check=templates built (N): substituted | replayed: <where>` is printed after the lowering. The stand-in, where it takes a
specialization first, is left as it was.
Two facts the read keeps apart from the replay and the hold had to know: a `var`'s text is its type's spelling and its name
(`$T0 a` against `int a`; an unnamed parameter is its type alone), so what follows the type is held; and the read numbers its
locals as a function of its own, so a local's serial - a frame fact, the replay's in 2b - is taken from the replay's `var` in its
place, mapped one to one and refused where the map does not hold.

**src/** `Templates.h` +5, `Templates.cpp` +18, `Typing.h` +5, `Typing.cpp` +66, `Parser.h` +1, `Driver.cpp` +4: src/ 94,235 ->
94,334 lines. Without the flag `bound_` is empty and `bound` returns its argument: nothing reads what 2b adds.

**The census** (Linux, `-check=templates -templates=subst -S` over the 1,269 cases): at the fixed point - covered 238; replayed
by reason, a type parameter bound to a class or an array 288, a non-type, pack or template parameter 177, a `call` 77, a `cast` 49,
a `member` 13, a `literal` 9, a `sizeof` 8, no body read 6. At the lowering, of the 238 (each a specialization's body typed once per
unit): **substituted 180**; replayed by name - a `var` typed apart from its replay 34 (a type `substituted` does not carry: an
array of `T`, `volatile`), a `function` read apart in its parts 3, a `conditional` resolved apart in its type 3, a `unary` in its
kind 1. **The shapes that substitute** are slice 1's (names, literals, unary/binary/assign/conditional/comma/subscript, the plain
statements) over scalar arguments, in 46 cases: adl-block-declaration-outlives-nested, algorithm-upper-bound, alias-template,
char16-char32, const-return-name, default-arg-base-init, default-arg-declares, direct-init-scalar, enum-template-argument,
extern-template(.part), function-template-default-argument, function-template-explicit-specialization,
function-template-partial-ordering, inline-namespace-template, lambda-capture-hides-template, library-self-compile-pieces(-2),
member-template-bare-name, namespace-function-hides-template, namespace-function-template-name,
namespace-template-replay-from-outside, read-dependent-local, static-function-template-linkage, template-deduction,
template-function, template-in-namespace, template-name-shadowed, template-nondependent-argument, template-overload,
template-param-hides-enclosing, template-read-pack-name, template-sfinae-expression, template-sfinae-fallback,
template-sfinae-leading-decltype, template-sfinae-misc, template-sfinae-substitution-table, template-subst-refused, template-subst,
template-two-of-a-name, template-unnamed-parameter, trailing-return-template-decltype, trailing-return-type, ucn-identifier,
wchar-targets, wstring.

**Gates** (REF `2e824d8b` and NEW built in one Linux tree, `~/m82b-ref.exe` and `~/m82b-new.exe`; the cases alone, as 2a): `identical.sh`
REF against NEW without the flag at -O0 and -O2 - x86_64-linux 1,848, tms6747 1,848, x86_64-windows 3,696, arm64-darwin 1,848, **0
differ**; with `-templates=subst` on NEW (the 180 bodies built from the read) - x86_64-linux 1,848, x86_64-windows 3,696, arm64-darwin
1,848, **0 differ**; `-g` at -O0, without and with the flag, x86_64-linux 924 and x86_64-windows 1,848, 0 differ each; run.sh
1263/0, and with `-templates=subst` 1263/0 (none falls back as a failure: 41 of the 238 replay by name, counted above); subst.sh 0
failed (952 the same, ceilings 17 under 17 and 26 under 26); self-compile of `Templates.cpp`, `Typing.cpp`, `Driver.cpp` by NEW rc 0;
static: exclusions 0/0, comments 0 over, memory gate held, ast-residue node-refs 325 (not raised); g++ `-Werror` clean; **Windows** cl
/W4 /WX clean (first and alone, about 2 minutes, 07:06-07:08), names.sh 924 passed 0 failed, overload.sh 46/0.

**The `syntax::Node` copy in `substShape`** (2a's): the count was a false positive of `tools/ast-residue`'s pattern. Its node-refs
regex takes the identifier just before a `&` or `*`, so `const syntax::Node &n` reads as `Node`, which `node_kinds` seeds as `Ast.h`'s
base class; the `syntax::` qualifier is never seen (`Typing.cpp`'s own `const syntax::Node &n` in `Typer::node` is in the baseline
the same way). `syntax::Node` (`Syntax.h:47`) is a 32-byte trivially copyable record, so the copy is one small copy per node walked,
no allocation. The tool and its count are left as they are.

**Where it stopped, and what is next**: 2b is done for its shapes. Left: the 34 `var`s typed apart (an array of `T`, `volatile` - 
`Templates::substituted` carries pointers, references, `const` and functions only), the three `conditional`s, a `unary`, three
functions in their parts; then 2c (the frame from the binding, no replay for a covered shape), 2d (calls), as 2a's table says.

## M8.2 slice 2c: where the frame from the binding stops, and the array of `T` carried (lane m82c, 2026-11-19)

Branch `m8-2c` from `2ae0bb1f` (m6d-w3: main `b7a27aec`, m6d-w2's wave, M8.2 2a-2b, M6d S6). **2c is not reached in this commit**;
what it needs is measured below. What the commit does, under `-templates=subst` only: `Templates::substituted` carries an array
whose bound is a number (`T a[3]` with `T = int`), as it carried pointers, references, `const` and functions; and `Typer::typedApart`
names the read's type when a `var` is typed apart, so the census says what the binding did not carry. Without the flag `bound_` is
empty and neither line is reached.

**Why 2c did not fit the hour.** The replay's frame is not a product of one function the read could call. The parser lays it while it
parses a definition: the parameters and every declared local through `Parser::allocateFrameSlot` (`Parser.cpp:813`), a slot freed at
a scope's close reused by stored type and alignment (`freeSlots_`, `Parser.cpp:786`, `:853`), the sret slot (`ParserTopLevel.cpp:1573`),
the variadic save area (`:1581`), `frameAligns_` by symbol - and 66 calls of `allocateFrameSlot` in `src/parser/` hand out slots for
temporaries no `var` node names. A read at the declaration declares its locals slotless (`slotless`, `Parser.cpp:818`) and makes no
`Function`. So the specialization's `Function` shell (params and their offsets, locals with offset, scope, align, serial and `declared`
for `-g`, `frameSize`, `sretSlot`, `regSaveSlot`, `frameAlign`, symbol, display name, `inline`, `noexcept`, `lookup_.functions_[].defined`,
and what `topLevel` records beside it) has one maker today, the replay. 2c is then two steps: (1) the parser's slot sequence made a
function of what it reads - the declared and hidden slots recorded on the read's syntax as it is parsed at the declaration (each
`allocateFrameSlot` call keyed by the node it serves, with the scope closes that free) - so a frame pass over the read under the
binding hands out the same offsets in the same order; held against the replay's `Function` field by field under `-check=templates`
before anything uses it; (2) the replay skipped for a covered body whose frame holds, its shell made from the pass (the stand-in's
`substituteFunction` already builds a `Function` with no replay from a sibling's: the same constructor, its facts from the pass).

**The census** (Linux, `-check=templates -templates=subst -S` over the 1,276 cases): at the fixed point unchanged from 2b - covered
238; replayed by reason, a type parameter bound to a class or an array 288, a non-type, pack or template parameter 177, a `call` 77,
a `cast` 49, a `member` 13, a `literal` 9, a `sizeof` 8, no body read 6. At the lowering: **substituted 180** in 46 cases, as 2b;
replayed by name - a `var` typed apart 34, now by the read's type: a class template-id over a parameter 29 (`const basic_string<$T0> &` 19,
`const unique_ptr<$T0> &` 3, `const pair<$T0,$T1> &` 2, `Box<...>` 2, `const vector<$T0> &`, `const shared_ptr<$T0> &`, `X<Y<$T0>> *`), a dependent
member type 4 (one under a pointer), a pointer to member of a parameter 1; a `function` read apart in its parts 3, a
`conditional` resolved apart in its type 3, a `unary` in its kind 1. **The array of `T` moved none of 2b's 34**: none is an array.
The 29 are the class door's (M8.3: `substituted` cannot name `basic_string<int>` without instantiating it), so the cheapest of the
58 left is not among the `var`s; the next to read are the three `conditional`s and the `unary` (single shapes, causes not yet named).

**src/** `Templates.cpp` +1 (the array in `substituted`), `Typing.cpp` +2 (the read's type in `typedApart`'s `var` line): src/ 94,482 ->
94,485 lines.

**Gates** (REF `2ae0bb1f` and NEW built in one Linux tree, `~/m82c-ref.exe` and `~/m82c-n2.exe`; the cases alone): `identical.sh` REF against NEW
without the flag at -O0 and -O2 - x86_64-linux 1,856, tms6747 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ**; with
`-templates=subst` on NEW - x86_64-linux 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ** (the 180 bodies built from the
read); `-g` at -O0, without and with the flag, x86_64-linux 927 the same (349 refused alike) and x86_64-windows 902 (374), 0 differ
each; run.sh 1270/0, and with `-templates=subst` 1270/0; subst.sh 0 failed; `-check=fold` over tests/cases compared 16,475, agreed
12,243, differ 0; tms6747 SIM=1 -O2 shards 1-3 146/148/147 passed, 0 failed; self-compile of `Templates.cpp` by NEW rc 0; static:
exclusions 0/0, comments 0 over, memory gate held, ast-residue node-refs 301 (not raised); g++ `-Werror` clean; **Windows** cl /W4
/WX clean (first and alone: the whole tree 08:15-08:16, 86 s; the final delta 08:35-08:36, 36 s), names.sh 928 passed 0 failed,
overload.sh 46/0.

**Where it stopped, and what is next**: 2c's step (1), the parser's slot sequence recorded on the read so a frame pass hands out
the replay's offsets; then step (2), the replay skipped where the pass's frame holds. Beside it the three `conditional`s
(`template-function`'s `a > b ? a : b`, `template-deduction`'s two) and the `unary` (`template-sfinae-unresolved`'s `+t`): the
read resolves them apart from the replay in type and kind; naming both types in `typedApart`'s message is the first probe.

## M8.2 slice 2c, step 1: the parser's frame steps recorded on the read, and the frame they lay held against the replay's (lane m82d, 2026-11-19)

Branch `m8-2d` from `a559360a` (m6d-w3: main `0155813c`, S6, 2a-2c's `de4f21d0`, m6d-s7b). **What it does, under `-templates=subst`
only**: the parser logs each step of a frame's layout while `Parser::frameLog_` is set (`logFrame`: `z` the frame's start at
`topLevel`, `d` a declared local - its type, `alignas`, offset and serial, the reuse of a freed slot inside it - `a` a slot no
local names, `f` a slot a closing scope gives back, `S` the sret slot, `V` the variadic save area, `e` the frame's size, `p`/`q` a
nested function's state cleared and put back, `o`/`c` a synthesised frame opened and closed). `readDeclaration` sets it for a
function template's read (`readFrames_`, by `afterParams`), and `instantiatePending` for the replay of every specialization
2b bound (`bound_`). `Templates::layFrame` walks the read's steps with each type passed through the binding (`substituted`) and
lays the frame as the parser does - `freeSlots_`' reuse by stored type and alignment, `objectAlign`, a reference stored as a
pointer, a freed slot found by its local's serial in its own frame - and holds every step against the replay's: kind, type,
offset (the frame's alignment, `frameAligns_`, is not held yet: step (2) owes it). `-check=templates frame (N): equal | differ: <step>: <what>`. Nothing uses the frame for code yet; without the flag the
log is never set and `logFrame` returns at once.

**The census** (Linux, `-check=templates -templates=subst -S` over the 1,276 cases): of 2b's 238 covered, 221 are replayed and
checked (17 are taken by the stand-in first, as before). **Frame equal 187, differ 34**, and each of the 34 is `a local: typed
apart` - the read's local whose type the binding does not carry, unit for unit the 34 `var`s 2b's lowering keeps apart (a class
template-id over a parameter 29, a dependent member type 4, a pointer to member 1: `substituted` cannot name `basic_string<int>`
without the class door, M8.3). No frame differs in an offset, a reuse, a hidden slot, the sret slot or the frame's size. **All 180
substituted bodies (46 cases) have their frame equal**, so step (2) - the replay skipped where this check holds - has its
denominator: 187, of which the 7 not substituted are 2b's `function`/`conditional`/`unary` read apart.

**src/** `Parser.h` +4, `Parser.cpp` +16 -3, `ParserTopLevel.cpp` +4, `Entities.h` +3 (`FrameEvent`, so `Templates.h` names it),
`Templates.h` +4, `Templates.cpp` +81 (`layFrame`, the two logs, the census line): src/ 94,493 -> 94,602 lines.

**Gates** (REF `a559360a` and NEW built in one Linux tree, `~/m82d-ref.exe` and `~/m82d-n1.exe`; the cases alone): `identical.sh` REF against NEW
without the flag at -O0 and -O2 - x86_64-linux 1,856, tms6747 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ**; with
`-templates=subst` on NEW - x86_64-linux 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ** (the 180 bodies built from the
read); `-g` at -O0, without and with the flag, x86_64-linux 927 the same (349 refused alike) and x86_64-windows 902 (374), 0 differ
each; run.sh 1270/0, and with `-templates=subst` 1270/0; subst.sh 0 failed (ceilings 17 under 17, self 102 of 102 reached, 26 under
26); `-check=fold` over tests/cases compared 16,475, agreed 12,243, differ 0; tms6747 SIM=1 -O2 shards 1-3 146/148/147 passed, 0
failed; self-compile of `Templates.cpp` by NEW rc 0; static: exclusions 0/0, comments 0 over, memory gate held, ast-residue node-refs
298 (rc 0, not raised); g++ `-Werror` clean; **Windows** cl /W4 /WX clean (first and alone, the whole tree 08:52-08:54, about 2
minutes), names.sh 928 passed 0 failed, overload.sh 46/0.

**Where it stopped, and what is next**: step (1) is done. Step (2): for a body whose frame is `equal`, make its `Function` shell from
the laid frame (params' and locals' offsets from the `d` steps, `frameSize` from `e`, `sretSlot`, `regSaveSlot`; `frameAlign`,
`declared`, scope and serial for `-g` from the same steps) and skip the replay - `substituteFunction`'s constructor is the model.
The 34 wait on the class door (M8.3).

## M8.2 slice 2c, step 2's first part: the frame's alignment held, and where skipping the replay stops (lane m82e, 2026-11-19)

Branch `m8-2e` from `977f40f8` (m82d). **Two commits.** (1) The wave gate m6dw3g's memory-gate finding on `a559360a`
(raw-pointers 92 against 89): `Typer::bound` takes and returns a `TypeId` (`Typing.h`, `Typing.cpp`: the declaration's two
declarators and the definition's parameter), its seven callers through `TypeTable::id` and `TypeTable::type` - back to 89.
(2) **The frame's alignment in the frame check**, which step 1 owed: the parser logs the alignment a frame starts at on its `z`
step (`frameAlign_`, reset only by a nested function, so a frame starts where the last one left it); `Templates::layFrame` lays
from the parser's `frameAlign_` before the replay - not from the replay's step, which it only checks - tracks each frame's
alignment as `allocateFrameSlot` does (a nested function's reset at `p`, put back at `q`; a synthesised frame's `o`/`c` keeping
it) and returns the widest slot past it outside a nested function; `instantiatePending` holds that against what the replay left
in `frameAligns_[symbol]` and in `frameAlign_` for the next frame. With it, the frame a covered body lays reads nothing of the
replay. The same commit re-points `docs/EXCLUSIONS.md`'s 15 stale citations (the wave gate's 4 and 11 more in `Parser.cpp` and
`ParserTopLevel.cpp`, moved by m82d's lines): 0 uncited, 0 stale.

**The census** (Linux, `-check=templates -templates=subst -S` over the 1,276 cases): covered 238, replayed by reason as m82d
(a class or array argument 288, a non-type/pack/template parameter 177, `call` 77, `cast` 49, `member` 13, `literal` 9, `sizeof`
8, no body read 6). **Frame equal 187 with the alignment held, differ 34** (all `a local: typed apart`, the class door's); no
frame differs in its alignment. **Substituted with no replay 0; substituted beside a replay 180** (46 cases); replayed at the
lowering by name 41 (a `var` typed apart 34, a `function` in its parts 3, a `conditional` 3, a `unary` 1); the rest of the
fixed point replays by the reasons above.

**Where skipping the replay stops.** The frame is now the read's own, but three things the replay makes are not: (a) the shell's
records - a `d` step carries type, alignment, offset and serial, and a `::Local` wants its name, `isParam`, `scope` (the replay's
block number, `currentBlock()`), `staticName`, `declared`; `Param`s are the `d` steps under `inParams_`, unmarked in the log;
`-g` wants `fnVars_` and the block table as the replay numbers them; (b) the body's proof - the 180 substitute because
`Typer::typedApart` holds the read's typed body against the replay's, node for node, and takes each local's serial from the
replay's; with no replay there is nothing to hold it against at the lowering, and the brief's rule (a body that differs falls
back) has no fallback once the parse is over: a no-replay body needs its proof at parse time or an accepted trust in the read,
which is a decision, not a step; (c) the replay's side effects on the unit - `lookup_.functions_[].defined`, the `holdBodies` root
the driver lowers, string labels and anything `topLevel` numbers in order. Step (2) is then: the log's `d` steps carrying name,
parameter mark and block; the shell made from them as `substituteFunction` makes one; the body root the driver walks for the
symbol; and the decision about (b).

**src/** `ParserTopLevel.cpp` +1 -1, `Templates.h` +1 -1, `Templates.cpp` +25 -5, `Typing.h` +1 -1, `Typing.cpp` +9 -9: src/ 94,602 ->
94,622 lines.

**Gates** (REF `977f40f8` and NEW built in one Linux tree, `~/m82e-ref.exe` and `~/m82e-n1.exe`; the cases alone; measured on commit (2)'s tree, commit (1) its subset): `identical.sh` REF against NEW without the flag at -O0 and -O2 -
x86_64-linux 1,856, tms6747 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ**; with `-templates=subst` on NEW - x86_64-linux
1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ** (the 180 bodies built from the read); `-g` at -O0, without and with the flag,
x86_64-linux 927 the same (349 refused alike) and x86_64-windows 902 (374), 0 differ each; run.sh 1270/0, and with `-templates=subst`
1270/0; subst.sh 0 failed (self 102 of 102 reached, the self shape record 34 bodies in 21 units, ceilings 17 under 17 and 26 under 26);
`-check=fold` over tests/cases compared 16,475, agreed 12,243, differ 0; tms6747 SIM=1 -O2 shards 1-3 146/148/147 passed, 0 failed;
self-compile of `Templates.cpp`, `Typing.cpp`, `ParserTopLevel.cpp` by NEW rc 0; static, each tool's own exit status (m82d's
script printed `tail`'s, which is how 92 and the stale citations passed it): exclusions 0/0, comments 0 over, memory gate raw-pointers
89 (rc 0), ast-residue node-refs 298 (rc 0, not raised), ide/generate.py up to date; g++ `-Werror` clean; **Windows** cl /W4 /WX clean
(first and alone: the whole tree 09:24-09:25, about 1 minute; the final delta 09:27:54-09:28:55), names.sh 928 passed 0 failed,
overload.sh 46/0.

## M8.2 slice 2c, step 2's second part: the shell's records from the read, held against the replay's function (lane m82f, 2026-11-19)

Branch `m8-2f` from `8fdc673b` (m82e). **What it does, under `-templates=subst` only**: a `d` step of the parser's frame log
carries the local's name, the parameter mark (`inParams_`) and the block it is declared in (`currentBlock()`, the number the
replay's `fnVars_` and block table use) - `FrameEvent` +3 fields, `logFrame` +3 arguments, one call. `Templates::layFrame` hands
back the offsets it laid, and `Templates::shellApart` makes from them the records `substituteFunction` gives a `Function` - each
`Param` (stored type, offset) and `::Local` (name, stored type, offset, `isParam`, scope, alignment, `declared`, serial) from the
outermost frame's `d` steps, `sretSlot` from `S`, `regSaveSlot` from `V`, `frameSize` from `e` (the frame's alignment is set on every function at the
unit's end from `frameAligns_`, which m82e's check holds) - and, where the frame is `equal`, holds them against the function the replay made, record for record.
`-check=templates shell (N): equal | differ: <record>`. Nothing uses the shell for code yet.

**The census** (Linux, `-check=templates -templates=subst -S` over the 1,276 cases): covered 238, frame equal 187, differ 34 (as
m82e). **Shell: 187 equal of the 187 whose frame is equal, 0 differ.** Every record is the replay's - parameters, locals' names, types,
offsets, blocks, alignments, `declared`, the sret slot, the save area, the frame's size - but the serials: the read numbers its locals as it
read them, so a no-replay shell takes the read's serials, which its own typed body resolves to, and the check holds them one for
one against the replay's (a first build compared them equal and found all 187 renumbered). Substituted with no replay 0; beside a
replay 180 (46 cases); replayed by reason as m82e (a class or array argument 288, a non-type/pack/template parameter 177, `call`
77, `cast` 49, `member` 13, `literal` 9, `sizeof` 8, no body read 6; at the lowering by name 41).

**Two defects found on the way** (not mended here: no side tasks), self-compiling `Templates.cpp`: cxx3 reads `::Local` - the
global `Local`, written inside `namespace sema` - as `sema::Local` in a functional cast `::Local(...)` ("no function called
'sema::Local::Local' takes these 6 argument(s)") and in `typedef ::Local X`, though `std::vector< ::Local>` is read right; and
`std::vector< ::Local>::value_type` is refused ("has no member type called 'value_type'"). g++ and cl take all three. `shellApart` fills each `::Local` field by field after `resize`.

**Where the skip stops, exactly.** (a) The shell's records are the read's now, checked; the block table (`Function::blocks()`,
`blockPositions()`, which `-g` reads) is not logged yet - a `b` step at `Parser.cpp`'s `blocks_.push_back` would carry it.
(b) Decided by the coordinator: trusted under `-templates=subst`, the replay beside it only under `-check=templates`. What the
parse-time fallback must name is the 7 bodies whose frame and shell are equal but whose typed body is not (`function` in its parts
3, `conditional` resolved apart 3, `unary` 1): at parse time the read's shape has to refuse them by name, which nothing does yet.
(c) The replay's side effects on the unit, read off `ParserTopLevel.cpp`'s definition path for a free function: the `Function`
pushed where the replay pushes it (its index orders the module); `syntaxFunctions_[read]` and `definedBodies_` (the census's
root - a read root shared by several specializations, so by a map of its own as `Typer::census` places `substituted_`);
`setBlocks`; `setDisplayName`, `setInline` (replayingInline_), `setNoexcept`, `setFrameAlign`; `discardableInline_`;
`lookup_.functions_[].defined`; `frameAligns_[symbol]` and `frameAlign_` for the next frame (m82e's check computes both);
`constexprFns_` for a `constexpr` one. String labels and `topLevel`'s numbering are not reached: the covered shape has no string
literal, call or lambda. `Typer::typeBody` then types the read under the binding with no replay to hold it against.

**src/** `Parser.h` +3 -1, `Parser.cpp` +1 -1, `Entities.h` +1 -1, `Templates.h` +4 -1, `Templates.cpp` +57 -2: src/ 94,622 -> 94,682 lines.

**Gates** (REF `8fdc673b` and NEW built in one Linux tree, `~/m82f-ref.exe` and `~/m82f-n4.exe`; the cases alone): `identical.sh` REF against
NEW without the flag at -O0 and -O2 - x86_64-linux 1,856, tms6747 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ**; with
`-templates=subst` on NEW - x86_64-linux 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ** (the 180 bodies built from
the read); `-g` at -O0, without and with the flag, x86_64-linux 927 the same (349 refused alike) and x86_64-windows 902 (374), 0
differ each; run.sh 1270/0, and with `-templates=subst` 1270/0; subst.sh 0 failed (self 102 of 102 reached, the self shape record
34 bodies in 21 units, ceilings 17 under 17 and 26 under 26); `-templates=subst -check=templates` over the 1,276 cases no check
failure; `-check=fold` compared 16,475, agreed 12,243, differ 0; tms6747 SIM=1 -O2 shards 1-3 146/148/147 passed, 0 failed;
self-compile of `Templates.cpp` and `Parser.cpp` by NEW rc 0 (after the two defects above were written round); static, each tool's
own exit status: exclusions 0/0 (three citations in `Templates.cpp` re-pointed), comments 0 over, memory gate raw-pointers 89 (rc 0),
ast-residue node-refs 298 (rc 0, not raised: a `const Var &` alias of `::Local` read 299, and went), ide/generate.py up to date; g++
`-Werror` clean; **Windows** cl /W4 /WX clean (first and alone: the whole tree 09:44:46-09:47:06, about 2 minutes 20; the deltas
09:58:16-10:00:39 and 10:15:46-10:17:23), names.sh 928 passed 0 failed, overload.sh 46/0 (on the final tree; the same on the first).

## M8.2 slice 2c, step 2 continued: what the read keeps apart refused at parse time, and the block table from the read (lane m82g, 2026-11-19)

Branch `m8-2g` from `7774bb42` (m82f). **Two commits**, both under `-templates=subst` only. (1) **The 7 bodies whose frame and shell held
but whose typed body did not now fall back at parse time, by name**: `Templates::substShape` takes the binding and walks the read
twice - pass 1 the shapes the slice covers, as before (so every earlier reason reads as it did); pass 2 what the read records apart
from any replay: a node the parser recorded with a type the binding does not carry (`p_.dependentType(substituted(s, t))` - the
three `conditional`s, whose read the parser holds as `zeroOf(dependentResult())` and notes as a `Value`), a `var` declared with
such a type (a member pointer too, which `substituted` keeps as written), and a unary `+` over a dependent name (the replay's
`unaryNode` hands its operand on, record and all: `template-sfinae-unresolved`'s `+t`). `instantiate` refuses a trailing `decltype`
(`->` at depth 0 of the declaration's tokens) over a `sizeof`, a cast or a qualified name (`DepExpr` ops the read keeps no syntax
for, so the function node has other parts than the replay's: `template-sfinae-misc`'s `m28`, `m30`, `m31`); a leading `decltype`
has no parts in either and still substitutes. The same pass refuses the 34 `var`s m82b-m82f found typed apart at the lowering, now
before any replay - the class door's (M8.3). So no covered body needs the replay's typed body to be found apart.

**The census after (1)** (Linux, `-check=templates -templates=subst -S` over the 1,276 cases): covered 197 (17 taken by the stand-in
first, as before); replayed by reason - a class or array argument 288, a non-type/pack/template parameter 177, `call` 77, `cast` 49, a
`name` resolved to a type the binding does not carry 26, `member` 13, `literal` 9, a `var` declared so 8, `sizeof` 8, no body read 6,
a trailing `decltype` 3, a `conditional` 3, a unary `+` 1. **Frame equal 180, differ 0; shell equal 180, differ 0.** Substituted with no
replay 0; **beside a replay 180** (46 cases, as m82f); **replayed at the lowering by name 0** (m82f: 41).

**src/ (1)** `Templates.h` +1 -1, `Templates.cpp` +48 -14 (`substShape`'s second pass, `unnotedOperand`, the trailing `decltype` in
`instantiate`): src/ 94,682 -> 94,716 lines. **Gates (1)** (REF `7774bb42` and NEW built in one Linux tree, `~/m82g-ref.exe` and
`~/m82g-n3.exe`; the cases alone): `identical.sh` REF against NEW without the flag at -O0 and -O2 - x86_64-linux 1,856, tms6747 1,856,
x86_64-windows 3,712, arm64-darwin 1,856, **0 differ**; with `-templates=subst` on NEW - x86_64-linux 1,856, x86_64-windows 3,712,
arm64-darwin 1,856, **0 differ**; `-g` at -O0, without and with the flag, x86_64-linux 927 the same (349 refused alike) and x86_64-windows
902 (374), 0 differ each; run.sh 1270/0, and with `-templates=subst` 1270/0; subst.sh 0 failed (self 102 of 102 reached, the self shape
record 34 bodies in 21 units, ceilings 17 under 17 and 26 under 26); `-templates=subst -check=templates` over the 1,276 cases no frame,
shell or body apart; `-check=fold` compared 16,475, agreed 12,243, differ 0; tms6747 SIM=1 -O2 shards 1-3 146/148/147 passed, 0 failed;
self-compile of `Templates.cpp` by NEW rc 0; static, each tool's own exit status: exclusions 0/0 (three `Templates.cpp` citations
re-pointed), comments 0 over, memory gate raw-pointers 89 (rc 0), ast-residue node-refs 298 (rc 0, not raised), ide/generate.py up to
date; g++ `-Werror` clean; **Windows** cl /W4 /WX clean (first and alone: the whole tree 10:41:16-10:43:21, about 2 minutes; the delta
10:46:59-10:48:39), names.sh 928 passed 0 failed, overload.sh 46/0.

(2) **The -g block table logged on the read**: `Parser::enterBlock` logs a `b` step - the block it opens in (`currentBlock()`) and
where it opens (`pos`) - `Templates::layFrame` holds each against the replay's, and `Templates::shellApart` builds the table
`Function::blocks()` and `blockPositions()` carry (topLevel's outermost block 0, then one entry per `b` of the outermost frame, a
nested or synthesised frame's passed over) and holds it whole against the replay's function. With it every record `-g` reads of a
covered body - `fnVars_`'s locals with their blocks (m82f's `d` steps) and the block numbering - is the read's own, checked.
**The census after (2)**: as after (1) - covered 197, frame equal 180, **shell equal 180 with the block table**, differ 0;
substituted beside a replay 180, with no replay 0.

**src/ (2)** `Parser.cpp` +1, `Templates.cpp` +8 -1: src/ 94,716 -> 94,724 lines (the lane: 94,682 -> 94,724, +42).
**Gates (2)** (NEW `~/m82g-c2.exe`, built in a second Linux tree `~/m82g2` so (1)'s chain ran undisturbed): `identical.sh` REF against NEW
without the flag at -O0 and -O2 - x86_64-linux 1,856, tms6747 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ**; with
`-templates=subst` - x86_64-linux 1,856, x86_64-windows 3,712, arm64-darwin 1,856, **0 differ**; `-g` at -O0, without and with the flag,
x86_64-linux 927 the same (349 refused alike) and x86_64-windows 902 (374), 0 differ each - once the tree's own path is read alike: the
second tree bakes `m82g2` into its include directory and the cases' paths, which 217 and 210 outputs spell, and nothing else differs;
run.sh 1270/0, and with `-templates=subst` 1270/0; subst.sh 0 failed (self 102 of 102, the self shape record 34 bodies in 21 units,
ceilings 17 under 17 and 26 under 26); `-templates=subst -check=templates` no frame, shell or body apart; `-check=fold` compared 16,475,
agreed 12,243, differ 0; tms6747 SIM=1 -O2 shards 1-3 146/148/147 passed, 0 failed; self-compile of `Templates.cpp` and `Parser.cpp` by
NEW rc 0; static: exclusions 0/0 (the three `Templates.cpp` citations and `Parser.cpp:846` re-pointed), comments 0 over, memory gate
raw-pointers 89 (rc 0), ast-residue node-refs 298 (rc 0, not raised), ide/generate.py up to date; g++ `-Werror` clean; **Windows** cl /W4
/WX clean (first and alone, the delta 10:58:05-10:59:06), names.sh 928 passed 0 failed, overload.sh 46/0.

**Where it stopped, exactly**: (1) and (2) are done. Not started: (3) the replay's side effects made without it - the `Function`
pushed where the replay pushes it, `syntaxFunctions_`/`definedBodies_` for the read root, `setBlocks` (now from the `b` steps),
display name, `inline`, `noexcept`, `discardableInline_`, `lookup_.functions_[].defined`, `frameAligns_[symbol]` and `frameAlign_` (m82e's
check computes both) - each to be shown equal to the replay's under `-check=templates`; then (4) `Typer::typeBody` with no replay and the
replay skipped under `-templates=subst` for the 180 whose frame, shell and body the check holds.
