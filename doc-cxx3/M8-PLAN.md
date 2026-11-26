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

## M8.2 slice 2c, step 2's end: the replay's side effects made from the read, and where skipping it stopped (lane m82h, 2026-11-19)

Branch `m8-2h` from `bac0c670` (m6d-w4: main `8d5464a1`, m8-2f, m6d-glob `b4fc3fa8`, m8-2g `1802a01b`). **Two commits.** (0) `shellApart`'s
`::Local` built by its constructor again, not field by field: m6d-glob reads `::Local(...)` inside `namespace sema` as the global one,
and the self-compile of `Templates.cpp` takes it (rc 0). (1) **The replay's side effects made from the read and held against the
replay's**: the read of a function template keeps the `Function` it made at the declaration (`readFns_`, by `afterParams`: name, return
type, linkage, variadic, position, `this`, pads, `noexcept`, throw list); `Templates::madeFromRead` makes the specialization's function
from it and the shell (m82f's records and m82g's block table) - symbol, display name as the definition path sets it, `inline` unless
internal - and `madeApart` holds it against the replay's function, with the unit's records the replay leaves: one function pushed
where it pushes it, no declaration or specialization made, every declaration of the symbol defined, `discardableInline_`, no
`constexprFns_` entry, one body root (`syntaxFunctions_`/`definedBodies_`). What the made function cannot carry is named: a return
type the binding does not carry, a `decltype` return (the replay works it out), a `noexcept`, a throw list or pads.
`-check=templates made (N): equal | differ: <record> | differ: replayed: <reason>`. The frame's alignment (`frameAligns_`,
`frameAlign_`) is m82e's check, unchanged. Nothing uses the made function for code yet.

**The census** (Linux, `-check=templates -templates=subst` over the 1,279 cases): substituted 180, frame 180 equal, shell 180 equal,
**made 111 equal, 0 differ in a record**; replayed by name 69 (a return type the binding does not carry 40, a `decltype` return 29); the
fixed point's reasons as m82g; units with a nonzero exit 350 on (0) and on (1). Substituted with no replay 0, beside a replay 180.

**src/** (0) `Templates.cpp` +1 -3: 94,737 -> 94,735; (1) `Templates.h` +7 -1, `Templates.cpp` +68 -6: -> 94,803 (the lane +66).

**Gates** (REF `bac0c670` and NEW built in one Linux tree `~/m82h`, `~/m82h-ref.exe`, `~/m82h-n0.exe`, `~/m82h-n1b.exe`): `identical.sh` REF
against NEW at -O0 and -O2, x86_64-linux 1,860 and x86_64-windows 3,720, without the flag and with `-templates=subst`, **0 differ** on
each commit; self-compile of `Templates.cpp` rc 0 on each (a first form of (1) put a `Function` in a `std::pair`, which cxx3's
`<utility>` copies and a `Function` cannot be: the self-compile refused it, and a vector holds them now); static on each commit's tree,
each tool's own exit status: exclusions 0/0 (the three `Templates.cpp` citations re-pointed: 1162/1214/1911, then 1224/1276/1973),
comments 0 over, memory gate raw-pointers 89 (rc 0), ast-residue node-refs 298 (rc 0, not raised), ide/generate.py up to date; g++
`-Werror` clean; **Windows** cl /W4 /WX clean, first and alone (the whole tree with (1) in its first form 11:13:45-11:16:33, under 3
minutes; the delta with (1) as committed and the work below, 11:28:26-11:31:10). Four targets, -g, run.sh, fold and subst.sh were run on
the tree below, which holds (1).

**Where it stopped, exactly: (2), the replay skipped, written and not committed** (`boxkit/m82h-c2-wip.patch`, src/ 94,849):
under `-templates=subst` without `-check=templates`, `Templates::madeWithoutReplay` lays the frame from the read alone (`layFrame`
with no replay), pushes the made function, defines the declarations, marks it discardable, sets `frameAligns_[symbol]` and `frameAlign_`,
and records the symbol in `unreplayed_`; `Typer::census` roots it at its read, `typeBody` types the read under the binding alone,
`declaredReturnOf` substitutes the read's return; tms6747 replays (its generator may walk the Ast body). Measured on it (`~/m82i`, a
path of the same length): **without the check 128 bodies built with no replay, 69 beside a replay**; `identical.sh` 0 differ on four
targets without the flag and on three with `-templates=subst`; `-g` 0 differ (x86_64-linux 929 the same, 350 refused alike;
x86_64-windows 904, 375) without and with the flag; run.sh 1273/0 both; `-check=fold` compared 16,594, agreed 12,314, differ 0;
self-compile of `Templates.cpp`, `Typing.cpp`, `Driver.cpp` rc 0. **subst.sh: 5 failed**, which is why it is not committed: (a) typed.sh
under the switch, untyped 158 against the 46 recorded - `Typer::check`'s walk types every census root with `typeFunction` and no
binding, so a read root typed there is its pattern's `$T0`; it wants the binding the lowering types it under, and then
`recordDiffers` holds the bound records against the read's, which name `$T0`, so the check needs a rule for a bound root as well;
(b) the M8.1 probes, `template-subst` by-substitution 0 (not 16) and `template-subst-refused` 0 (not 1) with its reason missing: a
body made with no replay has no root of its own, so `substituteFunction` must not take it as a sibling's pattern (the patch
excludes it), and the sibling then takes the read path instead. Either M8.1 takes an unreplayed pattern (its root the read, typed
under the pattern's binding and retyped to its own) or the probe's numbers move with the coordinator's decision; that is a
decision. Also owed: the 128 include the bodies the check's stand-in takes first (m82g's 17), which no check holds.

## M8.2 slice 2c's end: the replay skipped, the stand-in retired where M8.2 makes the body, and the typed check bound (lane m82i, 2026-11-19)

Branch `m8-2i` from `7f420fe0`. **One commit**: m82h's step (2) (`boxkit/m82h-c2-wip.patch`, applied as it stood) and what it
needed to pass. (i) The body proof, the coordinator's decision: under `-templates=subst` a covered body is made from the read and
not replayed (`madeWithoutReplay`); under `-check=templates` the replay runs beside it and the frame, shell and made checks hold it.
(ii) **M8.2 asked before the stand-in**: `instantiatePending` runs `substituteFunction` only for a specialization `instantiate` did
not cover (`bound_`), so M8.1's sibling substitution retires wherever M8.2 makes the body - the 17 it took first (m82g) are M8.2's
now. (b) **`Typer::check` types an unreplayed read root under its binding**, as `typeBody` does for the lowering, and
`recordDiffers` takes the binding: a bound root's record type is held against the parser's substituted by it. (c) **The census**:
`madeWithoutReplay` names why it replays, and `-check=templates replay (N): substituted with no replay | beside a replay, under
-check=templates | replayed: <reason>` prints with the built report (under the check, or `CXX1_TEMPLATE_CENSUS`).

**tests/subst.sh's probes re-pointed** at M8.2's census, the probe files' records only. Old: `template-subst` by-substitution 16 (the
stand-in, from a same-layout replayed sibling), `-g` 0; `template-subst-refused` by-substitution 1 and four stand-in reasons (a
conversion between two parameters' types, a bool argument, a name the template does not declare, a literal). New: `template-subst`
42 substituted with no replay, plain and under `-g`, 42 beside a replay and made 42 equal under the check; `template-subst-refused` 10
with no replay (the conversions, the bool argument and the literal are M8.2's shapes), plain and `-g`, and the call refused by reason
twice (`subst (2): replayed: a 'call' in the body`; the stand-in then refuses it as `a name the template does not declare: a lookup`);
the stand-in substitutes nothing in either (by-substitution=0). The census step also prints M8.2's replay lines; its door sum now
reads "through the function door", which counts M8.2's bodies with no replay too. `template-subst-refused.cpp`'s two comment lines
say what it now shows.

**The census** (Linux, over the 1,279 cases): without the check, **128 substituted with no replay, 69 replayed by reason** (a return type
the binding does not carry 40, a `decltype` return 29); under `-check=templates`, 197 beside a replay: covered 197 (180 + the
stand-in's 17), frame 197 equal, shell 197 equal, made 128 equal, 69 replayed by name; the fixed point's reasons as m82g. typed.sh
under the switch: **untyped 46** (recorded 46; m82h's step (2) alone: 158), 922 passed both levels.

**src/** `Templates.cpp` +74 -40, `Templates.h` +3, `Typing.cpp` +30 -3, `Typing.h` +2 -2, `Driver.cpp` +1 -1: 94,803 -> 94,867 (+64).

**Gates** (REF `7f420fe0` built in `~/m82ia`, NEW in `~/m82ib`, paths of one length): on n2 (the commit but one line, below) `identical.sh`
REF against NEW at -O0 and -O2, **0 differ** on four targets without the flag (x86_64-linux 1,860, x86_64-windows 3,720, arm64-darwin
1,860, tms6747 1,860) and on three with `-templates=subst`; **-g 0 differ** with and without the flag (x86_64-linux 929 the same, 350
refused alike; x86_64-windows 904, 375 - the 218 and 211 the script first called different were the include directory's path, REF's
`m82ia` read as `m82ib`); run.sh 1273/0 without and with `-templates=subst`; `-check=templates -templates=subst` over the cases: 350
units with a nonzero exit, as at m82h, none failing on a difference; `-check=fold` compared 16,594, agreed 12,314, differ 0;
self-compile of `Templates.cpp`, `Typing.cpp`, `Driver.cpp` rc 0; tms6747 SIM=1 -O2 shards 1-3 rc 0. n2's memory gate said raw-pointers
90 (rc 1): a `const Type *` in `recordDiffers`, made a `TypeId` - **n3, the commit**: static, each tool's own exit status, exclusions
rc 0, 0/0 (the three `Templates.cpp` citations re-pointed 1258/1310/2007), comments rc 0 (0 over), memory gate rc 0 (raw-pointers 89),
ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date; self-compile of `Typing.cpp` rc 0; `identical.sh` x86_64-linux
and x86_64-windows 0 differ, without and with the flag; **subst.sh 0 failed** (its census: 127 with no replay, 40 and 29 replayed by
reason, over the 922 units it counts; the "4 counts below the record" note as at m82h, nothing re-recorded); g++ `-Werror` clean.
**Windows**: cl /W4 /WX clean, first and alone - the whole tree 12:00:10-12:03:28 (3 min 18 s), n3's delta 12:08:10-12:11:37;
overload.sh 46/0 and names.sh 930/0 on n3 alone (a first run met the delta build relinking its exe at 12:11 and lost four consecutive
cases to it).

**Left**: the 69 a made function cannot carry (a return type the binding does not carry, a `decltype` return) replay by name; the
census step's door count still holds M8.2's no-replay bodies as the function door's; tms6747 replays every covered body.

## M8.2 slice 2d, first part: M8rev M7 (1)-(3) - the dependent-name mark, the definition's candidates, a template-id's explicit arguments (lane m82j, 2026-11-19)

Branch `m8-2j` from `58a4b2c2` (m6d-w4: main `8d5464a1`, M8.2 2a-2c's end `756c1454`, m6d-glob, m6d-runcr, m6d-winterm). Three
commits, each M8rev M7's fix as the review states it; nothing is substituted yet, and nothing outside a read changes.

**(1) The mark split in two** (`ffdf5741`). `syntax::Dependent` was "the expression's type waits for the arguments" and was read as a
lookup switch; it is now `TypeDependent` (`type-dependent` in the dump, set where `Dependent` was) beside `DependentName`
(`dependent-name`), [temp.dep]/1's: an unqualified callee with a type-dependent argument (`argsDependent_` of its own argument list),
or a name in a dependent scope - `T::f`/`typename T::type` (the first component a bound type parameter) or a member that
`templateIdMember` found in a dependent `B<T>` (`dependentScopeEnd_`, the token after it). Measured in the new
`tests/syntax/template-dependent-names`: `g(x)`, `conv<int>(x)`, `undeclared(x)`, `g(st)` in a member, `B<T>::bs`, `T::type(k)`,
`C<T>::get2(st)` dependent names; `g(k)`, `ns::g(x)`, `::g(x)`, `conv<int>(k)` not; `x`, `st` type-dependent only. `g(nb)` over an
unfound `nb` is still a dependent name (its body is refused by `unfound`). **Records re-recorded by hand**, for exactly that: in
`template-read` and `template-read-marks` every `dependent` became `type-dependent`, and `g` (3 calls) and `T::type` gained
`dependent-name`; `template-read.cpp`'s comment says which mark. ParserExpr's citations +7.

**(2) The definition context's candidates** (`9208301d`). [temp.dep.candidate]: an unqualified dependent-name callee keeps what
unqualified lookup found at the definition - a member of the class (`findMemberOwner`), else the functions of the name
(`qualifyForLookup`, no specialization made earlier) and its template - on the syntax tree (`Tree::noteFound`), each with a spelling
for the dump (`found "g(int)" found "g(double)"`, `found "template conv"`) and its symbol for the instantiation (`foundSymbols`); ADL
from both contexts stays the instantiation's. Records: the three template records gained `found` and nothing else (checked line by
line, the found lists stripped). ParserExpr's citations +27 from `58a4b2c2`.

**(3) A function template-id's explicit arguments kept** (this commit). The explicit-call door (`ParserTemplate.cpp`, M2.12l's node)
named its callee `conv` and dropped `<int>`; the callee is now spelled with its list (`spelledTokens`, `conv<int>`), as the read's
dependent form already was. Record: `template-dependent-names`' `(call (name "conv") (name "k"))` became `"conv<int>"`, the only
change. `primary`'s own template-id spelling joins tokens with no space (`conv<unsignedint>`), where `spelledTokens` writes
`unsigned int` - left as it was. ParserTemplate's citations +1 (1806, 1891, 1895, 1965).

**src/**: `ParserExpr.cpp` +32 -5, `Parser.h` +4 -1, `ParserTemplate.cpp` +6 -5, `Syntax.cpp` +13 -1, `Syntax.h` +12 -2,
`Templates.cpp` +1 -1: 94,867 -> 94,920 (+53; 94,880 after (1), 94,919 after (2)).

**Two records (1) and (2) left stale**, found by the full gate on (3): `tests/cases/template-read-pack-name.templates` and
`template-variadic.templates` print a read's declaration shape, marks included; (1)'s and (2)'s own gates ran subst.sh's typed and
census steps, not its record compare, so at `ffdf5741` and `9208301d` `subst.sh` fails these two. Re-recorded here by hand, for
exactly the marks: each record's one shape line differs from the new output only by `dependent` -> `type-dependent` and the added
`dependent-name` and `found` lists (checked with those stripped). The found list of `total` in `template-variadic` holds the
template's own pattern signature (`total(template parameter, parameter pack expansion)`) beside `template total` - left as found.

**Gates** (REF `58a4b2c2` in `~/m82ja`; NEW in `~/m82jb` (1), `~/m82jc` (2), `~/m82jd` (3), one path length). Each commit:
`identical.sh` REF against NEW at -O0 and -O2, **0 differ on four targets without the flag** (x86_64-linux 1,862, x86_64-windows
3,724, arm64-darwin 1,862, tms6747 1,862) and **on three with `-templates=subst`**; syntax.sh 6/0 (with that commit's records);
typed.sh 923/0; subst.sh's typed and census steps rc 0; static (each tool's own exit status): memory gate rc 0 (raw-pointers 89),
ast-residue rc 0 (node-refs 298, not raised), comments rc 0 (one group over at first on (1), the constant line, folded before the
commit), exclusions re-pointed to 0/0. On (3), the full gate: run.sh 1273/0 without and with `-templates=subst`; **-g 0 differ**
with and without the flag (x86_64-linux 712 the same, 350 refused alike, 218 differing by the tree's path alone; x86_64-windows
694, 375, 211 - each pair re-read with both paths as `m82jd`, `boxkit/m82j-gre.sh`); `-check=templates -templates=subst` over the
cases: 1,280 units, 350 nonzero exits as at m82i, none failing on a difference, the census unchanged (197 covered, frame and shell
197 equal, made 128 equal; without the check 128 with no replay, 69 replayed by reason; 46 cases substituting); `-check=fold`
compared 16,596, agreed 12,316, differ 0; subst.sh 0 failed but for the two records above (self 102 of 102, ceilings held, the "4
below the record" note as at m82h); self-compile of `ParserExpr.cpp`, `ParserTemplate.cpp`, `Syntax.cpp`, `Templates.cpp`,
`Typing.cpp`, `Driver.cpp` rc 0; tms6747 SIM=1 -O2 shards 1-3 rc 0; g++ `-Werror` clean. **Windows** (all three commits' tree):
cl /W4 /WX clean, first and alone, the whole tree 13:44:50-13:50 (about 5 minutes); names.sh 931/0, overload.sh 46/0.

**Left**: no `call` node is substituted yet (the brief's "if the hour allows" - not reached); the read's `primary` spells a
template-id with no space between words; the found list's pattern signature above; M7's other over-marks (`g(sizeof(T))`) stand as
type-dependent, which is what that mark means.

## M8.2 slice 2d, second part: a call bound at the definition made from the read (lane m82k, 2026-11-19)

Branch `m8-2k` from `8a9e4b6c` (m82j). **First, the base**: `tests/subst.sh` on `8a9e4b6c` itself (REF built in `~/m82ka`) -
**0 failed**, rc 0 (self 102 of 102, ceilings held, the "4 below the record" note as at m82h): m82j's two hand re-recorded
`.templates` records hold, nothing to mend.

**One commit, the brief's (1)**: a `call` node in a covered read body under `-templates=subst`, where the call has no
type-dependent part ([temp.nondep]: bound at the definition, so the read's Call record is the one the replay makes) -
`Templates::nondependentCall`: neither the call nor any of its parts marked `type-dependent` or `dependent-name`, the callee a
`name`, the read's record a direct Call (no value, vtable slot or object), a function of scalars (not variadic, no class or
reference), and no specialization. `Templates::callsUsed` makes the use the replay's resolution makes (`noteUsed` on the callee,
[basic.def.odr]/3), so an inline callee is still emitted where the replay would have. Everything else in a call replays by name,
four reasons in place of "a 'call' in the body". **Overload resolution over the bound argument types is not reached here**: a
call over a type-dependent argument is (2)'s - and every one of the suite's is a dependent-name call; `Overload::rankArgument`
ranks an Ast `Expr`, so (2) wants a rank over the typed tree's `TypeId`s first (not begun; the residue gate would see the Ast).

**The probe** `tests/cases/template-subst-call` (`.flags` `-templates=subst`; `.expected` clang 19's, `clang++ -x c++ -std=c++11
-pedantic-errors` on the Windows box): a call of a plain function, of an inline one (its use made, or it is not emitted), of
`ns::half`, a call alone as a statement - 8 bodies with no replay; `viaIdent`'s `ident(a)` (a dependent name) replays by reason (2).
**tests/subst.sh**, for exactly that: the probe added to the probes step (8 with no replay plain and `-g`, made 8 equal and the
dependent call's reason under the check, the stand-in taking none), and template-subst-refused's call line re-pointed from "a
'call' in the body" to the new reason - no record count changed.

**The census** (Linux, `-check=templates -templates=subst` over the 1,281 cases): **covered 205** (197 + the probe's 8), frame
205 and shell 205 equal, made 136 equal; without the check **136 substituted with no replay** (128 + 8), 69 replayed by reason
(as before). The 77 call bodies before: **71 a call with a type-dependent argument or a dependent-name callee** (69 + the probe's
2), 4 through a value, a member or a class or unresolved, 2 of a specialization, 2 over a class, a reference or variadic
arguments - so outside the probe no case's body is newly made by (1); the suite's calls in function templates wait on (2).

**(2) and (3) not reached in the hour.** (3) was tried and set aside, uncommitted (`boxkit/m82k-c3.sh`, the specialization
refusal dropped in a scratch tree): `conv<long>(k)` and `sq(3)` in a covered body made from the read link to undefined
`conv<long, int>` and `sq<int>` - the read declares the specialization under `patternUses_` and `noteUsed` alone does not put
its body in the fixed point, which the replay's call does; under the check the made check names it ("a declaration or
specialization it made", 2). So (3) wants the instantiation to ask for the specialization as the replay's call does.

**src/** `Templates.cpp` +38 (`nondependentCall`, `callsUsed`, the `call` case in `substShape`, the use made in
`madeWithoutReplay`), `Templates.h` +2: 94,920 -> 94,960 (+40). EXCLUSIONS re-pointed (Templates.cpp 1296, 1348, 2045).

**Gates** (REF `8a9e4b6c` in `~/m82ka`, NEW in `~/m82kb`, one path length): `identical.sh` REF against NEW at -O0 and -O2, **0
differ on four targets without the flag** (x86_64-linux 1,864, x86_64-windows 3,728, arm64-darwin 1,864, tms6747 1,864) and **0
on three with `-templates=subst`**; **-g 0 differ** with and without the flag (x86_64-linux 713 the same, 350 refused alike, 218
differing by the tree's path alone; x86_64-windows 695, 375, 211 - re-read by `m82k-gre.sh`); run.sh 1274/0 without and with
`-templates=subst`; `-check=templates -templates=subst` over the cases: 1,281 units, 350 nonzero exits as at m82j, none failing
on a difference; `-check=fold` compared 16,598, agreed 12,318, differ 0; **subst.sh 0 failed** (run and typed at -O0/-O2 1274/0
and 924/0, self 102 of 102, ceilings held); syntax.sh 6/0; typed.sh 924/0; self-compile of `Templates.cpp`, `Typing.cpp`,
`Driver.cpp` rc 0; tms6747 SIM=1 -O2 shards 1-3 rc 0; static, each tool's own exit status: exclusions rc 0 0/0 (after the
re-point; rc 1 3/3 before it), comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised),
ide/generate.py up to date; g++ `-Werror` clean. **Windows**: cl /W4 /WX clean, first and alone, the whole tree 14:17:36-14:18:41
(65 s); names.sh 932/0, overload.sh 46/0.

**Left**: (2) - a dependent-name callee resolved at the instantiation from `found` plus ADL, which needs overload ranking over
`TypeId`s; (3) - the specialization asked for at the instantiation; the 4 calls through a value, a member or a class, and the 2
over a class, a reference or variadic arguments.

## M8.2 slice 2d, third part: one overload ranking over typed arguments, and a dependent-name call resolved at the instantiation (lane m82l, 2026-11-19)

Branch `m8-2l` from `bb00b349` (m6d-w5: main `09b2e441`, m6d-winterm2, m8-2j and m8-2k).

**(a) The ranking over a typed argument** (this commit). `Overload::rankArgument` ranked an Ast `Expr`; it now ranks a
`sema::RankedArgument` - the argument's `TypeId`, its value category, whether it is a string literal or a null pointer constant
(the literal 0 or `nullptr`), the function it designates, and `constructs`, the question a class target asks of a converting
constructor ([over.ics.user]), which only a caller holding the node can answer. The `Expr` form reads the node into one and
calls the typed form, so one ranking serves both: every fact it read off the node is read in one place, and the converting
constructor is still asked lazily, only where the ranking reaches a class (it instantiates constructor templates, so asking it
eagerly would change what is emitted). `resolveOverload` still returns a `Signature` by value. No output changes.

**src/** (a): `Overload.cpp` +24 -11, `Overload.h` +12 -1: 94,987 -> 95,011 (+24). EXCLUSIONS re-pointed (Overload.cpp 630 -> 643).
No `Ast.h` residue added: node-refs stays 298 (the lambda that asks the converting constructor names no node type).

**Gates of (a)** (REF `bb00b349` in `~/m82la`, NEW in `~/m82lb`, one path length): `identical.sh` REF against NEW at -O0 and -O2,
**0 differ on four targets without the flag** (x86_64-linux 1,866, x86_64-windows 3,732, arm64-darwin 1,866, tms6747 1,866) and
**0 on three with `-templates=subst`**; **-g 0 differ** with and without the flag (x86_64-linux 714 the same, 350 refused alike,
218 differing by the tree's path alone; x86_64-windows 696, 375, 211 - re-read by `boxkit/m82l-gre.sh`); run.sh 1275/0 without
and with `-templates=subst`; `-check=templates -templates=subst` over the cases: 1,282 units, 350 nonzero exits as before, none
failing on a difference, the census as m82k's (205 covered, frame and shell 205 equal, made 136 equal; without the check 136
with no replay, 69 replayed by reason); `-check=fold` 0 failing; **subst.sh 0 failed** (self 102 of 102, ceilings held);
syntax.sh 6/0; typed.sh 925/0; self-compile of `Overload.cpp`, `Templates.cpp`, `Typing.cpp`, `Driver.cpp` rc 0; tms6747 SIM=1
-O2 shards 1-3 rc 0; static, each tool's own exit status: exclusions rc 0 0/0 (after the re-point; rc 1 1/1 before it),
comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date;
g++ `-Werror` clean. **Windows**: cl /W4 /WX clean, first and alone (the delta 14:39:57-14:42:13, after a first try that cl
refused for a slip in the lambda, mended before any gate); names.sh 933/0, overload.sh 46/0.

**(b) A dependent-name call resolved at the instantiation** (the next commit). [temp.dep.candidate]: an unqualified callee
marked `dependent-name` takes the functions the definition's lookup found (`found`, m82j's (2)) and ADL's, which an arithmetic
argument has none of ([basic.lookup.argdep]/2), ranked as any call is. `Templates::dependentCall`, asked by `substShape` for a
call `nondependentCall` sends on: the candidates are the found list's (a function template among them replays by name: the
replay deduces), free functions of scalars with no default or ellipsis; the arguments are locals, typed under the binding
(`substituted`), arithmetic and no enumeration; the replay's own lookup must see the same set - a function of the name declared
since the definition, in another scope or specialized replays by name; each candidate is ranked through (a)'s typed form, and
the best is `Overload::bestViable`, the selection `resolveOverload` now calls too (one [over.match.best] loop). The choice is
kept on the bound body by node (`Bound::calls`, the call and its callee), the Typer records it as the parser records a direct
call (`Typer::boundCall`: `Call` on the call, `Callee` on the name), and `callsUsed` marks the function used, as the replay's
resolution does. Under `-check=templates` the replay runs beside it and `typedApart` holds the two node for node.
`-check=typed` holds the two records against that choice (`recordDiffers`: "the record the instantiation chose").

**The probe** `tests/cases/template-subst-depcall` (`.flags` `-templates=subst`; `.expected` clang 19's, `clang++ -x c++ -std=c++11
-pedantic-errors` on the Windows box): `pick(a)` over `pick(int)`/`pick(double)` for an int, a double and a char (a promotion),
`wide(a)` over an int and a short, `two(a, k)` over two two-parameter candidates, a call alone as a statement for an int and a
float (a promotion to double), and `ident(a)`, a function template, replayed by reason - 9 bodies with no replay (plain and
`-g`), made 9 equal under the check; its output is clang's with and without the flag. **tests/subst.sh**, for exactly that: the
probe added to the probes step, and the two existing probes' dependent call line re-pointed from "a call with a type-dependent
argument or a dependent-name callee" to "a dependent-name call a function template may answer" (their callee is `ident`, a
template) - no record count changed.

**The census** (Linux, `-check=templates -templates=subst` over the 1,283 cases): 350 nonzero exits as before, none failing on a
difference; **covered 215** (205 + the probe's 9 + 1 whose return replays it anyway), frame and shell 215 equal, made 145 equal;
without the check **145 substituted with no replay** (136 + the probe's 9), 70 replayed by reason. Of m82k's 71 bodies over a
dependent call: 47 still a call with a type-dependent part whose callee is no unqualified dependent name (`nondependentCall`'s
reason), 17 a function template among the candidates, 3 a member or a function over a class, reference, default or ellipsis, 2
an argument other than a local, 1 a name another scope declares, 1 an argument ADL would search - and outside the probe one
body resolved (its return type replays it).

**src/** (b): `Overload.cpp` +26 -19, `Overload.h` +3, `Templates.cpp` +78 -6, `Templates.h` +5 -3, `Typing.cpp` +32 -5,
`Typing.h` +3: 95,011 -> 95,125 (+114). EXCLUSIONS re-pointed (Templates.cpp 1368, 1420, 2117).

**Gates of (b)** (REF `bb00b349` in `~/m82la`, NEW in `~/m82lc`, one path length; n3 is (b) but the typed check's hold, n4 the
commit): on n3, `identical.sh` REF against NEW at -O0 and -O2, **0 differ on four targets without the flag** (x86_64-linux 1,868,
x86_64-windows 3,736, arm64-darwin 1,868, tms6747 1,868) and **0 on three with `-templates=subst`**; **-g 0 differ** with and
without the flag (x86_64-linux 715 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 697, 375, 211 -
re-read by `boxkit/m82l-gre.sh`); syntax.sh 6/0; typed.sh 926/0; the census above. n3's subst.sh failed twice, its typed step
under the stand-in's switch on the probe: `recordDiffers` held the chosen call's record against the read's placeholder - mended
in this commit (the hold above). On n4: **subst.sh 0 failed** (run 1276/0 at -O0 and -O2, typed 926/0, same 970/0, self 102 of
102, ceilings held), typed.sh 926/0, the probe's `-check=typed` clean; static, each tool's own exit status: exclusions rc 0 0/0
(after the re-point; rc 1 3/3 before it), comments rc 0, memory gate rc 0 (raw-pointers 89; 90 on a first n2, a `const Type *`
in `resolvedType`, mended before the commit), ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date; g++
`-Werror` clean. Not run on n4 itself: identical and -g (n4 changes only the typed check's comparison, which emits nothing),
run.sh with `-templates=subst` as its own run (identical with the flag stands for it; subst.sh's run step ran), `-check=fold`,
tms6747 SIM=1. **Windows**: cl /W4 /WX clean, first and alone, the final tree's delta 15:21:36-15:23:10 (94 s); names.sh 934/0,
overload.sh 46/0 on n3's tree.

**Left**: the 47 calls with a type-dependent part and no unqualified dependent-name callee, the 17 whose candidates include a
function template (deduction at the instantiation), arguments that are not locals (a typed argument is read only from a
local's record here), class, enumeration and pointer arguments (ADL), and (3), the explicit-argument call that needs the
specialization requested - not started, as the brief said.

## M8.2 slice 2d, fourth part: the 47 calls with a type-dependent part, measured apart before any is made (lane m82m, 2026-11-19)

Branch `m8-2m` from `973d6d36` (m82l). **First, the base** - the checks m82l did not run on its own final commit, each on
`973d6d36`'s tree built from clean (`~/m82mb`) against `bb00b349` (`~/m82ma`, one path length): `identical.sh` at -O0 and -O2,
**0 differ on four targets without the flag** (x86_64-linux 1,868, x86_64-windows 3,736, arm64-darwin 1,868, tms6747 1,868) and
**0 on three with `-templates=subst`**; **-g 0 differ** with and without the flag (x86_64-linux 715 the same, 350 refused alike,
218 by the tree's path alone; x86_64-windows 697, 375, 211 - every DIFFER pair re-read with both paths as one,
`boxkit/m82m-gre.sh`); `-check=fold` compared 16,600, agreed 12,320, differ 0, no unit failing; run.sh with `-templates=subst`
1276/0; the census as m82l recorded it (215 covered, 145 with no replay, 70 replayed). **Windows**: cl /W4 /WX of the whole tree
clean, first and alone (15:28:43-15:31:22, about 2m40s); names.sh 934/0, overload.sh 46/0. Nothing to mend.

**One commit** (this one): `nondependentCall`'s reason for a call with a type-dependent part is split three ways, so the census
says which of the brief's two items each body waits on - a callee that is a function template (its last component, before any
`<`, names one: `std::move(a)`, `twice<T>(x)`), a callee that is not a name (through a value or a member), and the rest. Nothing
is substituted that was not: the reason text is printed only under `-check=templates` or `CXX1_TEMPLATE_CENSUS`.

**The census** (Linux, `-check=templates -templates=subst` over the 1,283 cases): unchanged but for the split - 350 nonzero exits
as before, none failing on a difference; covered 215, frame and shell 215 equal, made 145 equal; without the check 145 with no
replay, 70 replayed. **m82l's 47 are 32 + 10 + 5**: **32 a qualified or explicit-argument call a function template may answer**
(the library's `std::move`, `std::forward`, `std::swap` and the like over a bound pointer or scalar: algorithm-sort-large 6,
library-cppparse-pieces 10, range-for-vector 3, std-function 3, std-function-ledger 3, and one each in array-header,
functional-less, std-move-and-forward, std-swap-two-ints, template-function (`twice<T>(twice<T>(x))`), thread-pool,
thread-sleep-yield); **10 through a value or a member** (a comparator or a member call: algorithm-sort-large, include-map-set,
include-string, library-self-compile-pieces-2, map-move-only-mapped, map-set-comparator, return-self-referential,
static-local-braced-list 1 each, wstring 2); **5 the rest** (lambda-returning-lambda 2, map-set-node-stability 1,
member-template-body-unchosen 2). So the brief's (a) - a non-dependent callee typed under the binding through the one ranking -
has at most these 5 in the suite, and 32 + 17 = 49 bodies wait on (b): deduction at the instantiation and the specialization
requested as the replay's call requests it (`Templates::instantiateViableTemplates`, reached from `ParserTemplate.cpp`'s call
door at 1751 and the explicit-argument door) - which m82k's scratch showed is what makes `conv<long, int>` and `sq<int>` defined.

**src/** `Templates.cpp` +9 -3: 95,125 -> 95,131 (+6). EXCLUSIONS re-pointed (Templates.cpp 1374, 1426, 2123).

**Gates** (REF `973d6d36` in `~/m82mb`, NEW in `~/m82mc`, one path length; every check on this commit's own tree): `identical.sh`
REF against NEW at -O0 and -O2, **0 differ on four targets without the flag** (x86_64-linux 1,868, x86_64-windows 3,736,
arm64-darwin 1,868, tms6747 1,868) and **0 on three with `-templates=subst`**; **-g 0 differ** with and without the flag
(x86_64-linux 715 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 697, 375, 211 - re-read by
`boxkit/m82m-gre.sh`); `-check=templates -templates=subst` over the cases as above, none failing on a difference; `-check=fold`
compared 16,600, agreed 12,320, differ 0; run.sh 1276/0 without and with `-templates=subst`; **subst.sh 0 failed** (self 102 of
102, the ceiling 26 of 26, 3 shapes of 3); syntax.sh 6/0; typed.sh 926/0; self-compile of `Templates.cpp` rc 0; static, each
tool's own exit status: exclusions rc 0 0/0 (after the re-point), comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue
rc 0 (node-refs 298, not raised), ide/generate.py up to date; g++ `-Werror` clean. **Windows**: cl /W4 /WX clean, first and
alone, the delta 15:44:46-15:47:27 (2m41s); names.sh 934/0, overload.sh 46/0.

**Left**: (a)'s 5 (a non-template callee over a bound argument, typed through the one ranking) and the 10 through a value or a
member; (b) whole - the 32 + 17 calls a function template may answer, deduced at the instantiation from the bound argument
types, the specialization requested where the replay's call requests it, partial ordering through the existing code. Not begun:
the hour went to the base's checks and this measure.

## M8.2 slice 2d, fifth part: a function template among a call's candidates, deduced and requested at the instantiation (lane m82n, 2026-11-19)

Branch `m8-2n` from `cf45c483` (m82m; its own final commit checked clean, so no base re-check here).

**(1) Deduction at the instantiation** (this commit). The 49 bodies whose call a function template may answer (m82m's 32 qualified
or explicit-argument calls, m82l's 17 dependent-name calls with a template among `found`) are deduced where the specialization
is made: `Templates::templateCandidates` reads each argument as `deductionArgType` gives it (a local, typed under the binding, a
named local an lvalue so a reference to its type; an unqualified call's argument arithmetic and no enumeration, which ADL adds
nothing for), reaches the templates the replay's door reaches (`templatesNamed`, the written scope), and deduces each through
`deduceTemplateArguments` - Templates' one deduction, the replay's own. Nothing more is substituted: the body still replays, and
its reason says how far the instantiation got. **The census** (Linux, `-check=templates -templates=subst` over 1,283 cases): the
49 are **23 deduced** ("a call a function template answers, deduced at the instantiation, whose specialization is not requested
yet"), 13 over an argument whose type ADL would search, 12 over an argument other than a local, 1 an explicit-argument call
(`twice<T>`); everything else unchanged (215 covered, 145 with no replay, 70 replayed). tests/subst.sh's three probe reason lines
re-pointed, for exactly that (`ident(a)`, now deduced), no count changed.

**src/** (1): `Templates.cpp` +45 -5, `Templates.h` +3 -2: 95,131 -> 95,172 (+41). EXCLUSIONS re-pointed (Templates.cpp 1414,
1466, 2163).

**Gates of (1)** (REF `cf45c483` in `~/m82nb`, NEW in `~/m82nc`, one path length; every check on this commit's own tree):
`identical.sh` REF against NEW at -O0 and -O2, **0 differ on four targets without the flag** (x86_64-linux 1,868,
x86_64-windows 3,736, arm64-darwin 1,868, tms6747 1,868) and **0 on three with `-templates=subst`**; **-g 0 differ** with and
without the flag (x86_64-linux 715 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 697, 375, 211 -
re-read by `boxkit/m82n-gre.sh`); `-check=templates -templates=subst` over the cases: 1,283 units, 350 nonzero exits as before,
none failing on a difference; `-check=fold` 0 units failing; run.sh 1276/0 without and with `-templates=subst`; **subst.sh 0
failed** (self 102 of 102, the ceiling 26 of 26, 3 shapes of 3); syntax.sh 6/0; typed.sh 926/0; self-compile of `Templates.cpp`
rc 0; static, each tool's own exit status: exclusions rc 0 0/0 (after the re-point; rc 1 3/3 before it), comments rc 0, memory
gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date; g++ `-Werror` clean.
**Windows**: cl /W4 /WX clean, first and alone, the whole tree 16:11:05-16:12:09 (about a minute); names.sh 934/0, overload.sh 46/0.

**(2) The specialization requested, ranked and recorded** (this commit, `73f1ff02`). A body whose calls (1) deduced is made from the read
when it holds one such call: `substShape` collects it (`requests`) instead of refusing, and once the body's whole shape is known
- so a body that replays for another reason has asked for nothing the replay would not ask for first -
`Templates::requestCall` makes the request where and as the replay's call door makes it, without the replay's tokens: in the
instantiation's own context, as the fixed point sets it for a replay (the template's parameters bound, its namespace,
`instantiationKey_`/`Of_`/`Display_`, its depth, `replayingInline_`), `templatesNamed` reaches the templates and
`instantiateViableTemplates` - the door's own call - deduces and declares each viable specialization at the callee's source
position (the node's `loc`, the token's `pos`). The candidates are the door's index, `Parser::templateCallKey` - the door's key
code moved into one function both call (`$global:` set included) - ranked through m82l's typed `rankArgument` and chosen by
`Overload::bestViable`, whose `betterCandidate` is the one [over.match.best] order (a non-template before a template, then
partial ordering, [temp.func.order]). The choice is kept on `Bound::calls` like a dependent-name call's (m82l), so
`Typer::boundCall` records it, `-check=typed`'s `recordDiffers` holds it, and `callsUsed` marks the specialization used, which
puts its body in the fixed point as the replay's resolution does - the specialization is defined in the object (run.sh links
and runs every case under the flag). Refused by name, after the request: a chosen function over a class, a reference or a
default (the read's call is lowered for scalars only), a candidate over a class (its converting constructors are not asked),
no best candidate, an ordinary function hiding the templates; before it, more than one such call in a body (the replay orders
their requests, the read's walk does not - not yet ordered here).

**The probe** `tests/cases/template-subst-deduce` (`.flags` `-templates=subst`; `.expected` clang 19's, `clang++ -x c++
-std=c++11 -pedantic-errors` on the Windows box): `twice(a)` for an int and a double, `over(a)` where an exact non-template
`over(int)` beats the template and the template `over<double>` beats a conversion, `ns::neg(a)` qualified, `mix(a, k)` over two
deduced parameters, a call alone as a statement, and `over(p)` over a pointer, replayed by reason (ADL) - 14 bodies with no
replay (plain and `-g`), made 14 equal under the check; its output is clang's with and without the flag. **tests/subst.sh**, for
exactly that: the probe added to the probes step, and the three probes whose `ident(a)` is now made re-counted (refused 10 -> 12,
call 8 -> 10, depcall 9 -> 10; their deduced-reason lines gone).

**The census** (Linux, `-check=templates -templates=subst` over 1,284 cases): 350 nonzero exits as before, none failing on a
difference; **covered 236** (215 + the probes' 19 + 2 in library-cppparse-pieces), frame and shell 236 equal, made 166 equal; without the
check **166 substituted with no replay** (145 + 21), 70 replayed by reason (as before). Of the
49: 21 made (the probes' 19 and the suite's 2); **12 bodies with more than one call a function template answers** (array-header
1, library-cppparse-pieces 2, std-function-ledger 3, std-function 3, std-swap-two-ints 1, thread-pool 1, thread-sleep-yield 1:
`std::move` twice, `std::swap`'s three moves); the rest over arguments that are no local (15) or whose type ADL would search
(14) - counted after the first deduced call, so these reasons now name what a body holds beyond it.

**src/** (2): `Templates.cpp` +103 -21, `Templates.h` +4 -1, `ParserTemplate.cpp` +19 -17, `Parser.h` +1: 95,172 -> 95,260 (+88).
EXCLUSIONS re-pointed (Templates.cpp 1496, 1548, 2245; ParserTemplate.cpp 1755, 1808, 1893, 1897, 1967).

**Gates of (2)** (REF `cf45c483` in `~/m82nb`, NEW in `~/m82nc`, one path length; every check on this commit's own tree):
`identical.sh` REF against NEW at -O0 and -O2, **0 differ on four targets without the flag** (x86_64-linux 1,870,
x86_64-windows 3,740, arm64-darwin 1,870, tms6747 1,870) and **0 on three with `-templates=subst`** - every case, so every body
now made from the read emits what the replay emitted, its requested specialization included; **-g 0 differ** with and without
the flag (x86_64-linux 716 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 698, 375, 211 - re-read by
`boxkit/m82n-gre.sh`); `-check=templates -templates=subst` over the cases: 1,284 units, 350 nonzero exits as before, none failing
on a difference; `-check=fold` compared 16,604, agreed 12,324, differ 0; run.sh 1277/0 without and with `-templates=subst` (each case linked and run:
the requested specializations are defined); **subst.sh 0 failed** on the committed tree (run 1277/0 and typed 927/0 at -O0 and
-O2, self 102 of 102, the ceiling 26 of 26, 3 shapes of 3) - a first run failed one probe line, `template-subst-refused`'s
"template-replayed (2): a name the template does not declare: a lookup", the stand-in's refusal of `viacall`, which M8.2 now
makes, so the stand-in is not run there (decision ii): the line removed, for exactly that; syntax.sh 6/0; typed.sh 927/0;
self-compile of `Templates.cpp` and `ParserTemplate.cpp` rc 0; static, each tool's own exit status: exclusions rc 0 0/0 (after
the re-point), comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised),
ide/generate.py up to date; g++ `-Werror` clean. **Windows**: cl /W4 /WX clean, first and alone, the delta 16:24:45-16:25:46
(about a minute); names.sh 935/0, overload.sh 46/0.

**Left**: a body with more than one such call (the requests in the replay's order: the read's calls in source order, arguments
first); arguments that are not locals (a typed argument is read only from a local's record); class, enumeration and pointer
arguments of an unqualified call (ADL); a chosen specialization over a reference (`std::move`, `std::forward`, `std::swap` -
the read's call lowered for reference parameters); explicit-argument calls (`twice<T>`); the 10 through a value or a member.

## M8.2 slice 2d, sixth part: more than one template call in a body, and specializations over a reference (lane m82o, 2026-11-19)

Branch `m8-2o` from `73f1ff02` (m82n; its own final commit checked clean, so no base re-check here). m82n's nit mended: its
(2) paragraph said "(the next commit)" where it meant that commit, `73f1ff02`.

**(a) More than one call a function template answers, in the replay's order** (this commit). A body m82n refused by name
because it held two or more deduced calls is made from the read: `Templates::instantiate` makes the requests in the order the
replay's doors make them. **What fixes that order is the read's node order**: a call's `syntax` node is noted when its parse
completes, after its arguments (`note` in ParserExpr/ParserTemplate), which is where the replay's call door fires
(`instantiateViableTemplates` deduces from the arguments' types, so it runs after them), and no request's argument holds
another request (each argument is a local, `callArgTypes`), so ordering the requests by `NodeId` is ordering them as the
replay's tokens are read - not the walk's order (`substShape` pops a stack, children last first), and not the fixed point's
worklist, which only sees the specializations once they are declared. A body that a later request refuses replays; the
requests made before the refusal are a prefix of the replay's, in its order. **Shown equal**: the specializations' order in
the object and each symbol's definition are `identical.sh`'s compare (REF replaying every body, NEW making them from the
read): 0 differ with `-templates=subst`; under `-check=templates` each body made 14 equal in the probe.

**The probe** `tests/cases/template-subst-order` (`.flags` `-templates=subst`; `.expected` clang 19's, `clang++ -x c++
-std=c++11 -pedantic-errors` on the Windows box): `chain` (four calls of three templates, one twice), `pair2` (two calls in
one expression), `calls` (two calls as statements, for `int` and `float`) - 14 bodies with no replay, plain and `-g`, made 14
equal under the check; its output is clang's with and without the flag. tests/subst.sh: the probe added to the probes step,
for exactly that; no other probe line changed.

**The census** (Linux, `-check=templates -templates=subst` over 1,285 cases): 350 nonzero exits as before, none failing on a
difference; **covered 252** (236 + the probe's 14 + 2 in library-cppparse-pieces); without the check **182 substituted with no
replay** (166 + 16), 70 replayed by reason (as before). Of m82n's 12 bodies with more than one such call: 2 made
(library-cppparse-pieces), 4 now refused after the request for a chosen function over a reference (`std::move`, `std::swap`:
array-header, std-swap-two-ints, thread-pool, thread-sleep-yield), 6 for a candidate over a class beside the deduced one
(std-function 3, std-function-ledger 3: `std::swap`'s overloads over `std::function`).

**src/** (a): `Templates.cpp` +6 -2: 95,260 -> 95,264 (+4). EXCLUSIONS re-pointed (Templates.cpp 1500, 1552, 2249).

**Gates of (a)** (REF `73f1ff02` in `~/m82ob`, NEW in `~/m82oc`, one path length; every check on this commit's own tree):
`identical.sh` REF against NEW at -O0 and -O2, **0 differ on four targets without the flag** (x86_64-linux 1,872,
x86_64-windows 3,744, arm64-darwin 1,872, tms6747 1,872) and **0 on three with `-templates=subst`** (the same counts) - the
specializations in the replay's order in the object, each defined as the replay defines it; **-g 0 differ** with and without
the flag (x86_64-linux 717 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 699, 375, 211 - re-read
by `boxkit/m82o-gre.sh`); `-check=templates -templates=subst` over the cases: 1,285 units, 350 nonzero exits as before, none
failing on a difference (frame 252 equal, shell 252 equal, made 182 equal, the 70 replayed apart by reason); `-check=fold`
compared 16,604, agreed 12,324, differ 0; run.sh 1278/0 without and with `-templates=subst`; **subst.sh 0 failed** (self 102
of 102, the ceiling 26 of 26, 3 shapes of 3); syntax.sh 6/0; typed.sh 928/0; self-compile of `Templates.cpp` and
`ParserTemplate.cpp` rc 0; static, each tool's own exit status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0
(raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date; g++ `-Werror` clean.
**Windows**: cl /W4 /WX clean, first and alone, the whole tree 16:51:06-16:53:26 (about two minutes); names.sh 936/0,
overload.sh 46/0.

**(b) A deduced call whose specialization takes or returns a reference** (this commit, on `f0264874`). The chosen
function may now take a parameter by reference and return a reference: `Templates::requestCall` accepts a reference return
whose referent is a scalar (the call an lvalue for `T &`, an xvalue for `T &&`; the typed tree reads it through the reference
as every call of a function returning one is lowered, `Typer::resolvedType`), and a reference parameter that binds the named
local directly ([dcl.init.ref]/5: the referent's unqualified type the argument's, so no temporary; the local's address is
passed as the replay's call passes it). **Deduction through a forwarding reference and reference collapsing** are the
replay's own: the argument is given to `deduceTemplateArguments` as `deductionArgType` gives it (a named local an lvalue, so
`T &&` deduces `T = int &` and the parameter collapses to `int &`), and the candidates are ranked by `rankArgument` with the
argument an lvalue, as before. **The argument's value category** is the typed tree's for a name: an lvalue, whatever the
local's declared type. The specialization the call reaches over a reference binding (`mv<int &>`) is itself replayed, by
reason ("a type parameter bound to a class or an array"); the body that calls it is made from the read.

**The probe** `tests/cases/template-subst-ref` (`.flags` `-templates=subst`; `.expected` clang 19's on the Windows box):
`mv` (a `std::move` of its own: `T &&` with `unref<T>::type &&` returned), `sw` (a `std::swap`: `T &` twice, three `mv`s),
`pick` (`T &` returned and assigned through), `cref` (`const T &`), each for an `int` and a `double` - 14 bodies with no
replay, plain and `-g`, made 14 equal under the check; its output is clang's with and without the flag. tests/subst.sh: the
probe added to the probes step, for exactly that; no other probe line changed.

**The census** (Linux, `-check=templates -templates=subst` over 1,286 cases): 350 nonzero exits as before, none failing on a
difference; **covered 270** (252 + the probe's 14 + 4); without the check **200 substituted with no replay** (182 + 18), 70
replayed by reason (as before). The 4 are (a)'s bodies refused for a reference, now made: `std::swap`'s and `std::move`'s
callers in array-header, std-swap-two-ints, thread-pool and thread-sleep-yield; the reason "a deduced call of a function over
a class, a reference or a default" is gone from the census. Left of m82n's 12: the 6 beside a candidate over a class
(`std::swap`'s overloads over `std::function`, whose converting constructors the read does not ask).

**src/** (b): `Templates.cpp` +8 -3: 95,264 -> 95,269 (+5). EXCLUSIONS re-pointed (Templates.cpp 1505, 1557, 2254).

**Gates of (b)** (REF `73f1ff02` in `~/m82ob`, NEW in `~/m82oe` - `~/m82oc`'s copy with (b) shipped, one path length; every
check on this commit's own tree): `identical.sh` **0 differ on four targets without the flag** (x86_64-linux 1,874,
x86_64-windows 3,748, arm64-darwin 1,874, tms6747 1,874) and **0 on three with `-templates=subst`**; **-g 0 differ** with and
without the flag (x86_64-linux 718 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 700, 375, 211);
`-check=templates -templates=subst`: 1,286 units, 350 nonzero exits as before, none failing on a difference (frame and shell
270 equal, made 200 equal, the 70 replayed apart by reason); `-check=fold` compared 16,604, agreed 12,324, differ 0; run.sh
1279/0 without and with `-templates=subst`; **subst.sh 0 failed** (self 102 of 102, the ceiling 26 of 26, 3 shapes of 3);
syntax.sh 6/0; typed.sh 929/0; self-compile of `Templates.cpp` and `ParserTemplate.cpp` rc 0; static, each tool's own exit
status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not
raised), ide/generate.py up to date; g++ `-Werror` clean. **Windows**: cl /W4 /WX clean, first and alone, the delta
17:08:24-17:13:42 (about five minutes); names.sh 937/0, overload.sh 46/0.

**Left**: (c) an argument that is not a local (15 bodies: a parameter is already a local; a member, a literal or a temporary
needs its type under the binding from the typed tree, which `callArgTypes` does not read yet) - not started, the hour spent on
(a) and (b) and their gates; a candidate over a class beside a deduced call (6: the read does not ask converting
constructors); class, enumeration and pointer arguments of an unqualified call (ADL, 14); explicit-argument calls
(`std::forward<T>`, `twice<T>`); the 10 through a value or a member.

## M8.2 slice 2d, seventh part: arguments that are not named locals, and a class candidate beside a deduced call (lane m82p, 2026-11-19)

Branch `m8-2p` from `50f39adb` (m82o; its own final commit checked clean, so no base re-check here).

**(a) A deduced call over an argument that is not a named local** (this commit). `Templates::callArgTypes` gives deduction and
the ranking each argument as `deductionArgType` gives it - an lvalue as a reference to its type, a prvalue as its type - for
more forms than a local: a dereference (`*p`, an lvalue, [expr.unary.op]/1), an address (`&a`), a unary or binary arithmetic or
comparison expression and a literal (prvalues), a member through `->` or of a named object (an lvalue), each through parentheses.
**The type is the typed tree's**: `Typer::typeUnder` types the argument's syntax under the specialization's binding as the body
itself is typed (`bound`, the binding put in `Templates::bound_` for the call), no second rule for a type. The value category
is the form's (`argumentCategory`); any other form replays by reason ("whose value category the read does not decide here"),
and an argument that holds a call replays ("that holds a call") - so no request's argument holds another, which is what orders
the requests by node (m82o (a)). `requestCall` ranks a prvalue as one (`RankedArgument::lvalue` false) and binds a reference
parameter only to an lvalue: a reference over a prvalue - a temporary materialised for it - is refused by name ("a reference
that binds no local as it is"), not made.

**The probe** `tests/cases/template-subst-arg` (`.flags` `-templates=subst`; `.expected` clang 19's, `clang++ -x c++
-std=c++11 -pedantic-errors` on the Windows box): `pick(*p) = *p + 1` and `ns::twice(*p)`, `ns::first(&a)`, `ns::twice(a - b)`
and `ns::twice(-a)`, `ns::mix(a, 2)` over a literal, each for an `int` and a `double` - 18 bodies with no replay, plain and `-g`,
made 18 equal under the check; `viaMember` (`ns::mix(a, b->v)`) replays by reason ("a 'member' in the body": the body walk does
not take a member yet). Its output is clang's with and without the flag. tests/subst.sh: the probe added to the probes step,
for exactly that; no other probe line changed.

**The census** (Linux, `-check=templates -templates=subst` over 1,287 cases): 350 nonzero exits as before, none failing on a
difference; **covered 292** (270 + the probe's 18 + 4); without the check **222 substituted with no replay** (200 + 22),
70 replayed by reason (as before). **Without the lanes' probes** (`template-subst*`): **88 with no replay** (84 + 4), covered
158 (154 + 4) - the suite's own 4 made: algorithm-sort-large 2, range-for-vector 1, library-cppparse-pieces 1
(`std::move(*b)`, `std::__sort_by_less(.., &*x)`, `std::__sort_swap(.., j - 1)`). Of m82o's 15: 4 made, 8 now refused for an
argument that holds a call (`a.data()` in `__cxx1_compare_n`'s 4 callers, `std::__sort_less<T>()`, `std::forward<T>(x)`), 2
beside a candidate over a class, 1 a call with a type-dependent argument resolved at the instantiation.

**src/** (a): `Templates.cpp` +48 -11, `Templates.h` +1 -1, `Typing.cpp` +7, `Typing.h` +2: 95,269 -> 95,315 (+46).
EXCLUSIONS re-pointed (Templates.cpp 1542, 1594, 2291).

**Gates of (a)** (REF `50f39adb` in `~/m82pb`, NEW in `~/m82pe`, one path length; every check on this commit's own tree):
`identical.sh` REF against NEW at -O0 and -O2, **0 differ on four targets without the flag** (x86_64-linux 1,876,
x86_64-windows 3,752, arm64-darwin 1,876, tms6747 1,876) and **0 on three with `-templates=subst`**; **-g 0 differ** with and
without the flag (x86_64-linux 719 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 701, 375, 211 -
re-read by `boxkit/m82p-gre.sh`); `-check=templates -templates=subst` over the cases: 1,287 units, 350 nonzero exits as before,
none failing on a difference; `-check=fold` 0 units failing on a difference; run.sh 1280/0 without and with `-templates=subst`;
**subst.sh 0 failed** (run 1280/0 and typed 930/0 at -O0 and -O2, same 994 of 994, self 102 of 102, the ceiling 26 of 26, 3
shapes of 3; the cases ceiling 17 of 17, 53 shapes of 53, 4 counts below the record - not re-recorded); syntax.sh 6/0; typed.sh
930/0; self-compile of `Templates.cpp` and `ParserTemplate.cpp` rc 0; static, each tool's own exit status: exclusions rc 0 0/0
(after the re-point), comments rc 0, memory gate rc 0 (raw-pointers 89: a first build's `typeUnder` returned a `const Type *`,
90, so it returns a `TypeId`), ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date; g++ `-Werror` clean.
**Windows**: cl /W4 /WX clean, first and alone, the whole tree 17:37:07-17:38:11 and the delta 17:48:22-17:49:26 (about a
minute each); names.sh 938/0, overload.sh 46/0.

**(b) A deduced call beside a candidate over a class** (this commit). m82n refused a deduced call whose door's index held a
candidate over a class, because the read asks no converting constructor. A candidate whose class parameter is a non-const
lvalue reference (`C &`) is now ranked with the rest through the one ranking, `rankArgument`: such a reference binds no
temporary ([dcl.init.ref]/5), so a scalar argument reaches it by no converting constructor and `rankArgument` answers None
without consulting `RankedArgument::constructs` - the same path, and so the same answer, as the replay's door takes with its
own `constructs` set. A class parameter by value, by `const C &` or by `C &&` would ask a converting constructor, and still
replays by name ("a deduced call beside a candidate over a class that takes a temporary").

**The probe** `tests/cases/template-subst-class` (`.flags` `-templates=subst`; `.expected` clang 19's on the Windows box):
`sw(T &, T &)` beside `sw(Box<T> &, Box<T> &)`, whose `Box<int>` specialization stands in the set when `useSw` calls `sw(a, b)`
for an `int` and a `double` - 4 bodies with no replay, plain and `-g`, made 4 equal under the check; its output is clang's with
and without the flag. tests/subst.sh: the probe added to the probes step, for exactly that.

**The census** (Linux, `-check=templates -templates=subst` over 1,288 cases): 350 nonzero exits as before, none failing on a
difference; **covered 304** (292 + the probe's 4 + 8), made 304 equal under the check; without the check **234 substituted with
no replay** (222 + 12), 70 replayed by reason (as before). **Without the lanes' probes: 96 with no replay** (88 + 8): the suite's
own 8 are m82o's 6 (`std::swap`'s callers beside `swap(function<R(Args...)> &, function<R(Args...)> &)` in std-function 3 and
std-function-ledger 3) and the 2 (a) reached the same refusal with (algorithm-sort-large 1, std-move-and-forward 1). The
reason "a deduced call beside a candidate over a class" is gone from the census.

**src/** (b): `Templates.cpp` +5 -2: 95,315 -> 95,318 (+3). EXCLUSIONS re-pointed (Templates.cpp 1545, 1597, 2294).

**Gates of (b)** (REF `50f39adb` in `~/m82pb`, NEW in `~/m82pf` - REF with (a) and (b) shipped, one path length; every check on
this commit's own tree): `identical.sh` **0 differ on four targets without the flag** (x86_64-linux 1,878, x86_64-windows
3,756, arm64-darwin 1,878, tms6747 1,878) and **0 on three with `-templates=subst`**; **-g 0 differ** with and without the flag
(x86_64-linux 720 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 702, 375, 211);
`-check=templates -templates=subst`: 1,288 units, 350 nonzero exits as before, none failing on a difference (made 304 equal);
`-check=fold` 0 units failing; run.sh 1281/0 without and with `-templates=subst`; **subst.sh 0 failed** (typed 931/0 at -O0
and -O2, same 1,000 of 1,000, self 102 of 102, the ceiling 26 of 26; the cases ceiling 17 of 17, 53 shapes of 53, 4 counts
below the record - not re-recorded); syntax.sh 6/0; typed.sh 931/0; self-compile of `Templates.cpp` and `ParserTemplate.cpp`
rc 0; static, each tool's own exit status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (raw-pointers 89),
ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date; g++ `-Werror` clean. **Windows**: cl /W4 /WX clean,
first and alone, the delta 17:56:56-17:58:05 (about a minute); names.sh 939/0, overload.sh 46/0.

**Left**: (c) ADL arguments (14: class, enumeration and pointer arguments of an unqualified call - the associated namespaces of
the bound argument types searched at the instantiation), not started; an argument that holds a call (8: `a.data()`,
`std::__sort_less<T>()`, `std::forward<T>(x)` - its request ordered inside the outer one's); a member in the body walk
(`substShape` takes no `member` node yet, so `viaMember` replays); a reference parameter over a prvalue (the temporary it
materialises); a class candidate that takes a temporary (its converting constructors asked through `constructs`);
explicit-argument calls; the 10 through a value or a member.

## M8.2 slice 2d, eighth part: argument-dependent lookup at the instantiation, and an argument that is a call (lane m82q, 2026-11-19)

Branch `m8-2q` from `71680841` (m82p; its own final commit checked clean, so no base re-check here). REF for identity is
`50f39adb`, as m82p's.

**(a) A deduced call over a pointer or an enumeration, its set looked up as the replay's** (this commit). The candidate set the
replay's call door ranks is `resolveOverload`'s: the key (`templateCallKey`) qualified for lookup, else the arguments'
namespaces' ([basic.lookup.argdep]/2), then one set from the key's, its unnamed namespace's and the arguments' namespaces'.
That lookup is now two functions of the arguments' types, `Overload::lookupName` and `Overload::candidateSet`, which
`resolveOverload` calls as it did inline and `Templates::requestCall` calls in the instantiation's context (the parameters bound,
the template's namespace) - one lookup, not a copy; `requestCall` before took the key's index alone. `callArgTypes` therefore
no longer refuses an unqualified call over a pointer or an enumeration ("whose type ADL would search"): only an argument of a
class or an array replays by name ("which ADL and a constructor would answer" - its converting constructors and associated
classes are not asked). A key that names a constructor template still replays.

**A wrong answer the probe found, and why it is not mended here**: the replay asks no argument-dependent lookup at a function
template's call door at all - the key it ranks under is `$global:weigh` or `lib::size`, not the bare name, so `candidateSet` finds no
function under an argument namespace and an ordinary `geo::weigh(geo::Pt *)` is never a candidate (cxx3 answers `1 1 1 1 / 3 3` with and without
the flag; clang 19 `70 7 1 1 / 9 3`). Mending it changes output without the flag, so it is registered in
`tests/open/template-adl-ordinary` with clang's answer; the body made from the read inherits the replay's answer by
construction, which `-check=templates` holds.

**The probe** `tests/cases/template-subst-adl` (`.expected` clang 19's on the Windows box): `useWeigh` over an enumeration (an
ordinary `weigh(geo::Shade)` beats the template), over `geo::Pt *` and `int *` (partial ordering picks `weigh(T *)`) and a
`double`; `lib::count` over a pointer and an `int` (`lib::size(T *)` and `lib::size(T)`) - 9 bodies with no replay, plain and
`-g`, made 9 equal under the check; `weigh<geo::Pt>`'s 2 replay ("a type parameter bound to a class"). The deduce probe's
`usePtr` (`over(p)` with `int *`) is made too: 15. tests/subst.sh: the probe added, the deduce count 15, its ADL reason line
re-pointed to the new probe's refusal.

**The census** (Linux, `-check=templates -templates=subst` over 1,289 cases): 350 nonzero exits as before, none failing on a
difference; **covered 316** (304 + 12: the probe's 9, the deduce probe's 1, the suite's 2); 316 made equal. **Without the lanes'
probes: 98 with no replay** (96 + 2: algorithm-sort-large 1, std-move-and-forward 1). Of the 14 "ADL would search": 3 made, 11
over a class argument (algorithm-sort-large 2, functional-less 1, include-string 2, library-cppparse-pieces 2, range-for-vector 1,
ssa-conversion-string-local 1, wstring 2); the dependent-name call over one (read-dependent-local) keeps its reason.

**src/** (a): `Overload.cpp` +53 -42, `Overload.h` +5, `Templates.cpp` +9 -7: 95,318 -> 95,336 (+18). EXCLUSIONS re-pointed
(Overload.cpp 654; Templates.cpp 1547, 1599, 2296).

**Gates of (a)** (REF `50f39adb` in `~/m82qb`, NEW in `~/m82qc` - REF with (a) shipped, one path length; every check on this
commit's own tree): `identical.sh` **0 differ on four targets without the flag** (x86_64-linux 1,880, x86_64-windows 3,760,
arm64-darwin 1,880, tms6747 1,880) and **0 on three with `-templates=subst`**; **-g 0 differ** with and without the flag
(x86_64-linux 721 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 703, 375, 211);
`-check=templates -templates=subst`: 1,289 units, 350 nonzero exits as before, none failing on a difference (made 316 equal);
`-check=fold` 0 units failing; run.sh 1282/0 without and with `-templates=subst`; **subst.sh 0 failed** (same 1,006 of 1,006,
self 102 of 102, the ceiling 26 of 26; 4 counts below the record - not re-recorded); syntax.sh 6/0; typed.sh 932/0;
self-compile of `Templates.cpp` and `ParserTemplate.cpp` rc 0; static, each tool's own exit status: exclusions rc 0 0/0,
comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date;
g++ `-Werror` clean. **Windows**: cl /W4 /WX clean, first and alone, the full build 18:16:44-18:19 (about 2.5 minutes);
names.sh 940/0, overload.sh 46/0.

**(b) A deduced call whose argument is a call** (this commit). The replay's door fires after its arguments are parsed, so an
inner call's specialization is asked, and its node noted, before the outer's: the requests sorted by node (m82o (a)) are already
in that order, and `Templates::instantiate` hands `requestCall` the body's records so far (`Bound::calls`). `callArgTypes` takes
an argument that is itself a call as a prvalue of what its record returns - the request's or the dependent-name call's chosen
`Signature`, or for a call bound at the definition the read's own resolution - so deduction and the one ranking see the type the
replay's `deductionArgType` gives the call's `Expr`. Before any record exists (the shape walk, `templateCandidates`) such an
argument defers the deduction to the request, which deduces through `instantiateViableTemplates` and replays by reason where
nothing is viable. A call returning a reference, a qualified type or nothing replays by name ("a call that returns a reference,
a qualified type or nothing"); an argument that holds a call deeper than itself (`a.data() + 1`, a member call) still replays.

**The probe** `tests/cases/template-subst-nest` (`.expected` clang 19's on the Windows box): `twice(inc(a))` (a request inside
a request) for an `int` and a `double`, `twice(sq(a)) + 1` (an ordinary dependent-name call inside), `inc(half(a))` for an `int`
and a `double` - 9 bodies with no replay, plain and `-g`, made 9 equal under the check (5 of them replayed "that holds a call"
before). tests/subst.sh: the probe added. **Residue**: the first build raised node-refs 298 -> 299 - `Resolved::Call &&
nondependentCall(` reads as a node reference to the tool (the known false-positive family); the condition was reordered, the
tool not edited, 298 kept.

**The census** (Linux, `-check=templates -templates=subst` over 1,290 cases): 350 nonzero exits as before, none failing on a
difference; **covered 325** (316 + the probe's 9), 325 made equal. **Without the lanes' probes: 98 with no replay** (+0): the
suite's 8 that replayed "that holds a call" now replay by their inner call's own reason - 4 "a call through a value, a member
or a class" (`a.data()` in `__cxx1_compare_n`'s callers), 3 "a type-dependent argument or a dependent-name callee", 1
"explicit-argument" (`std::__sort_less<T>()`, `std::forward<T>(x)`); the reason "that holds a call" is gone from the census.

**src/** (b): `Templates.cpp` +20 -9, `Templates.h` +5 -2: 95,336 -> 95,350 (+14). EXCLUSIONS re-pointed (Templates.cpp 1558,
1610, 2307).

**Gates of (b)** (REF `50f39adb` in `~/m82qb`, NEW in `~/m82qe` - REF with (a) and (b) shipped, one path length; every check on
this commit's own tree): `identical.sh` **0 differ on four targets without the flag** (x86_64-linux 1,882, x86_64-windows
3,764, arm64-darwin 1,882, tms6747 1,882) and **0 on three with `-templates=subst`**; **-g 0 differ** with and without the flag
(x86_64-linux 722 the same, 350 refused alike, 218 by the tree's path alone; x86_64-windows 704, 375, 211);
`-check=templates -templates=subst`: 1,290 units, 350 nonzero exits as before, none failing on a difference (made 325 equal);
`-check=fold` 0 units failing; run.sh 1283/0 without and with `-templates=subst`; **subst.sh 0 failed** (same 1,012 of 1,012,
self 102 of 102, the ceiling 26 of 26; 4 counts below the record - not re-recorded); syntax.sh 6/0; typed.sh 933/0;
self-compile of `Templates.cpp` and `ParserTemplate.cpp` rc 0; static, each tool's own exit status: exclusions rc 0 0/0,
comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised), ide/generate.py up to date;
g++ `-Werror` clean. **Windows**: cl /W4 /WX clean, first and alone, 18:32:52-18:39:58 (102 files: `Templates.h` is wide);
names.sh 941/0, overload.sh 46/0.

**Left**: (c) a reference parameter over a prvalue (the temporary the replay materialises), not started; the 11 over a class
argument (its converting constructors and associated classes); an argument holding a call deeper than itself, and a member
call as one; the replay's own missing ADL at a function template's call door (tests/open/template-adl-ordinary - output
changes without the flag, so a decision of its own); a member in the body walk; a class candidate that takes a temporary;
explicit-argument calls; the 10 through a value or a member.

## M8.2 slice 2d, ninth part: a class lvalue as a deduced call's argument, bound by reference (lane m82r, 2026-11-19)

Branch `m8-2r` from `17cf24ea` (m6d-w8: main `5d98c251` + m8-2q + m8-adl). REF for identity is `50f39adb`, as m82p's and
m82q's. **The Windows box was down** (the user, 2026-11-19): Linux only; cl /W4 /WX, names.sh, overload.sh, winpass, cdb and
MASM are owed, and the probe's `.expected` is g++'s (`g++ -std=c++11 -pedantic-errors` on the Linux box), clang 19's owed.

**(a) A class lvalue argument, ranked as the replay ranks it and bound by `C &` or `const C &`** (this commit). m82q refused
every deduced call over an argument of a class ("which ADL and a constructor would answer"). Its namespaces are searched by
the one lookup already (`Overload::candidateSet`, m82q (a) and m8-adl). What the read cannot ask is a converting
constructor: `RankedArgument::constructs` is the replay's `convertingConstructor` over an `Expr`, which also instantiates
constructor templates. So `callArgTypes` takes a class **lvalue** (a local or a parameter by its record, a dereference or a
member by the typed tree), and an array or a class prvalue - the temporary it materialises - replays by name ("over an array
or a class prvalue"). `requestCall` lets a candidate whose class parameter is the argument's own class stand in the set
(`C`, `const C &`, `C &&` of the same class ask no constructor: [over.best.ics]/6, [dcl.init.ref]/5). Any other class
parameter still refuses before ranking. Ranking passes a `constructs` that records being asked: a ranking that asks it
replays by name ("whose ranking asks a converting constructor"), so the read never answers where the replay would have
consulted one - the invariant held, not remembered. The chosen function may take the class by `C &` or `const C &` of
its own class: the reference binds the lvalue directly, no temporary, its address passed as the replay's call passes it. A
by-value class parameter (its copy constructor, the temporary's destructor) replays by name, as before.

**The probe** `tests/cases/template-subst-classarg` (`.flags` `-templates=subst`; `.expected` g++'s, clang owed): `pick(b, t)`
over a `const Box &` parameter, `bump(b, t)` over `Box &`, `scale(p, t)` found in `geo` by ADL and `geo::scale(p, t)`, and
`poke(n, t)` over a `Noisy &` with a user copy constructor (its copy count printed, 0), each for an `int` and a `double` - 18
bodies with no replay, plain and `-g`, made 18 equal under the check. `useByVal`'s 2 (`byVal(Box, T)`) replay ("a deduced call
of a function over a class ..."). tests/subst.sh: the probe added to the probes step, its made count and its reason line.

**The census** (Linux, `-check=templates -templates=subst` over 1,295 cases): 350 nonzero exits as before, none failing on a
difference; **covered 356** (338 + the probe's 18); without the check **286 substituted with no replay** (268 + 18), 70
replayed by reason (as before). **Without the lanes' probes: 111 with no replay** (+0): the suite's 11 over a class argument
still replay, by a nearer reason. 6 pass a class prvalue (algorithm-sort-large 2, functional-less 1,
library-cppparse-pieces 2, range-for-vector 1: "over an array or a class prvalue"). 5 reach deduction now and find no template
that deduces (include-string 2, ssa-conversion-string-local 1, wstring 2: "a call no function template it reaches deduces
for"). The reason "which ADL and a constructor would answer" is gone from the census.

**src/** (a): `Templates.cpp` +17 -9: 95,363 -> 95,371 (+8). EXCLUSIONS re-pointed (Templates.cpp 1568, 1620, 2317).

**Gates of (a)** (REF `50f39adb` in `~/m82rb`, NEW in `~/m82rc` - REF's tree with this commit shipped, one path length; every
check on this commit's own tree): `identical.sh` without the flag on four targets differs **only in m8-adl's four cases**
(template-adl-class, -hidden-friend, -ordinary, -template-argument, which REF answers wrongly: x86_64-linux 8 of 1,892,
x86_64-windows 16 of 3,784, arm64-darwin 8, tms6747 8). It is the same with `-templates=subst` on three targets; no other
case differs. **-g** the same four with and without the flag (x86_64-linux 723 the same, 350 refused alike, 218 by the
tree's path alone; x86_64-windows 705, 375, 211). `-check=templates -templates=subst`: 1,295 units, none failing on a
difference (286 made equal, 70 replayed by reason). `-check=fold` 0 units failing. run.sh 1288/0 without and with
`-templates=subst`. **subst.sh 0 failed** (same 1,042 of 1,042, self 102 of 102, the ceiling 26 of 26; the cases ceiling 17
of 17, 53 shapes of 53). syntax.sh 6/0; typed.sh 938/0. Self-compile of `Templates.cpp` and `ParserTemplate.cpp` rc 0.
Static, each tool's own exit status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue
rc 0 (node-refs 298, not raised), ide/generate.py up to date. g++ `-Werror` clean. **The Wine stand-in** (Linux,
`c3wine`, the project's LINK; not the box): the 12 template-subst probes built with `-c` for x86_64-windows, linked and run,
12 passed 0 failed with `-templates=subst` and at -O0. **Owed on Windows**: cl /W4 /WX, names.sh, overload.sh, winpass,
cdb, MASM, and clang 19's answer for the probe's `.expected`.

**Left**: (a)'s constructor half - a class parameter by value (the copy constructor, the copy's destructor) and a class
prvalue (the temporary materialised, its destructor scheduled) - with `constructs` asked from the read. Each needs the records
only the replay's parse writes today (`temporaryBuilt_`, `convertedBuilt_`, keyed by the replay's own nodes), made for the
read's nodes. Also left: a candidate over another class (a base, or one reached by a converting constructor); deducing a
class template-id parameter from a class argument (the 5 above); (b) function templates found only in an associated
namespace (m8-adl's left); the rest of m82q's left list.

## M8.2 slice 2e: M8.1-early's stand-in deleted whole (lane m82s, 2026-11-19)

Branch `m8-2s` from `9989a6f1` (m82r). **Step 1, the proof first** (the coordinator's decision ii): a measuring build of
`9989a6f1`, never committed, printed each body `substituteFunction` made. Under `-templates=subst` over the 1,295 cases on
x86_64-linux, x86_64-windows and arm64-darwin, and over the self-host's 102 sources on x86_64-linux and x86_64-windows (102 of
102 rc 0 each), **the stand-in substituted 0 bodies**. It was asked 669 times over the cases (plain and under
`-check=templates` alike) and refused each: 348 an argument that is not arithmetic, 159 a parameter that is not a type, 43
`inline`, 31 `int`, the rest a token outside its subset or a lookup. M8.2 asks first, and the stand-in is asked only for a body
M8.2 does not cover; it refused every one, so it made nothing M8.2 does not. tms6747 and `-g` it refused whole.

**One commit, the deletion as the 2e row lists it.** `Templates::substituteFunction` and its token subset
(`substitutableTokens`), `substituted_`, `refusedSubst_` and the census's `template-replayed (N)` lines it fed,
`substituteDebug_` (substituteTemplates takes the switch alone now), `Substituted::pattern`, `SubstitutedTypes`,
`TypedTree::retype` and `resolutionCount`, which only it read. `Typer::typeBody`'s retype and `declaredReturnOf`'s stand-in
branch go, as does the sibling-root insertion in `Typer::census`. `Templates::substituted` stays: it is M8.2's binding as a
function of a type. The `by-substitution` door is M8.2's alone now. `-templates=subst`'s help text names M8.2.
`template-subst.cpp`'s first two comment lines say the read makes its bodies.

**tests/subst.sh**: the `same` step is removed. It asserted that `-S` text and diagnostics with `CXX1_TEMPLATE_SUBST=1` equal those
without, over the cases reaching the function door (x86_64-linux, x86_64-windows, arm64-darwin, -O0 and -O2). That is asserted
now by `identical.sh` with `NEW_FLAGS=-templates=subst` against REF without it, on the same three targets, which every M8.2 lane
and wave gate runs. The census step's `template-replayed` lines (the stand-in's reasons) are gone. So is the per-probe check
`by-substitution=0` ("the stand-in substituted where M8.2 makes the body"), which has nothing left to hold. Steps run, typed,
census (M8.2's lines and the probes, unchanged), bodies and self stay. EXCLUSIONS re-pointed (Templates.cpp 1465, 1517, 2214).

**src/**: 95,371 -> 95,226 (-145: Templates.cpp +3 -106, Templates.h +5 -15, Typing.cpp +5 -25, Typing.h +1 -13, Driver.cpp and
Parser.h +6 -6). 100 or more lines go, **so the wave gate owes the fixed point** (self-host).

**The census** (Linux, unchanged against REF): `-check=templates -templates=subst` over 1,295 cases, 350 nonzero exits as REF,
none failing on a difference; covered 356, built 356 equal. Without the check, 286 substituted with no replay and 70 replayed by
reason (40 a return type the binding does not carry, 30 a `decltype` return). **Without the lanes' probes: 111 of the suite's own
bodies made with no replay** (+0).

**tests/subst-self.ceiling**: the member-templates door's floor lowered, bodies 1,515 -> 1,485 and read 1,490 -> 1,460 (unread
25 as before). The self corpus is the compiler's own source, and the deletion took 30 member-template bodies out of it (the
`substituted_` map's and its iterators' members among them). The reader is unchanged: NEW's exe over REF's sources reads 1,515
and 1,490, the record, as REF's own exe does. class-members' `shape=57` against the record's 58 is REF's too and is left alone.

**Gates** (REF `9989a6f1` in `~/m82sb`, NEW in `~/m82sc` - REF's tree with this commit shipped, one path length; every check on
this commit's own tree): `identical.sh` without the flag 0 differ on four targets (x86_64-linux 1,892, x86_64-windows 3,784,
arm64-darwin 1,892, tms6747 1,892), and with `-templates=subst` 0 differ on three (1,892, 3,784, 1,892). **-g** 0 differ with
and without the flag (x86_64-linux 726 the same, 350 refused alike, 219 by the tree's path alone; x86_64-windows 708, 375,
212). `-check=fold` compared 16,725, 0 differ, 0 units failing. run.sh 1288/0 without and with `-templates=subst`. syntax.sh
6/0; typed.sh 938/0. Self-compile of `Templates.cpp`, `Typing.cpp` and `Driver.cpp` rc 0. Static, each tool's own exit
status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (node-refs 298, not raised),
ide/generate.py up to date. g++ `-Werror` clean. **The Wine stand-in** (Linux, `c3wine`, the project's LINK; not the box): the
12 template-subst probes built for x86_64-windows, linked and run, 12 passed 0 failed with `-templates=subst` and at -O0.
**Owed on Windows** (the box is down): cl /W4 /WX, names.sh, overload.sh, winpass, cdb, MASM. **Owed by the wave gate**:
the fixed point (the self-host), since 145 lines went.

## M8.2 slice 2f, first part: M8rev L2 - a value-dependent name in a read is marked, and the read takes no constant from it (lane m82t, 2026-11-19)

Branch `m8-2t` from `8c75b11a` (main: M8.2 through 2e, the stand-in deleted, the replay's ADL mended). **The finding** (M8rev
L2): in a function template's read a non-type parameter is bound as an enumerator worth 0, so `int w = N` read `N` as the
constant 0, `const int c = N + 1` and `static const int K = N * 2` kept constants (1, 0), `enum { E = N }` made E = 0 and `case
N:` a label 0. Nothing read those constants yet (M8.2 refuses a non-type parameter before it builds), but slice 2f's second
part would have.

**What it does** (with and without the flag; a read's tree only): a new syntax mark, `value-dependent` (`syntax::ValueDependent`,
[temp.dep.constexpr]/2), on a name in a read that is a non-type parameter standing for itself (`EnumConst::valueDependent`, set
where `bindTemplateParameters` binds a pattern's parameter), a const or static const local whose initialiser holds one
(`Local::valueDependent`), or an enumerator made from one, and those after it (`E = N`). `Constexpr::fold` refuses a node so
marked in a read: no fold asked of the read takes a constant from it. Where the read must have a constant to go on - a const
local's record, an enumerator's value, a case label, a template argument, a static member's value (`constantExpression`,
`caseLabel`, `Parser::standInConstant`) - it takes the binding's stand-in as before, under `Constexpr::StandIn`, and the entity
keeps the mark: so the read's tree is what REF read, node for node, and every constant from a parameter is marked where it is
read. A refusal (the body unread) was tried first and measured: it made 16 bodies of the cases and 2 of the self-host unread
(class doors over `ratio`, `chrono` and the like: "the class: a constant from a value-dependent name"), which the ceiling may
not take; so the read keeps its stand-in and M8.2 is the one to refuse a marked constant (slice 2f's second part: a body whose
constant positions hold the mark is folded under the binding or replays by name). `-check=fold` counts these apart: "a
value-dependent name in a template's read". A non-const local (`int w = N`) is not marked, as the standard has it.

**Shown**: `tests/syntax/template-read-value` (new; its dump read by hand: `(name "N" value-dependent)` in `w`, `c` and `K`'s
initialisers, in `case N:` and in `enum { E = N }`; `(name "c" value-dependent)`, `(name "K" value-dependent)` and `(name "E"
value-dependent)` where they are read; `w` and `x` unmarked; f, g and h all read, as REF reads them). No existing syntax
record changed (none holds a non-type parameter). The probe `tests/cases/template-subst-value` (`.flags` `-templates=subst`; `.expected` g++'s on Linux, clang 19's
on Windows below): f, g, h over `N`, 5 specializations, all replayed by name ("a non-type, pack or template parameter, bound
by name"), its line in tests/subst.sh's census step.

**Found, not mended** (no side tasks): a local `enum { E = N };` in a function template instantiated twice is refused "'E' is
declared twice" by REF and NEW alike (`g<4>(); g<7>();`): the replay's enumerator outlives the specialization's body. The probe
instantiates `g` once; the defect is owed to the register.

**src/**: Syntax.h +5 -2, Syntax.cpp +8, Entities.h +4, Templates.cpp +1, Constexpr.h +8, Constexpr.cpp +3, Parser.h +4 -2,
ParserExpr.cpp +12 -3, ParserStmt.cpp +8, ParserConst.cpp +10 -3, ParserType.cpp +4 -1: 95,226 -> 95,282 (+56). EXCLUSIONS re-pointed
(ParserExpr.cpp six, ParserStmt.cpp six, ParserType.cpp twelve, Constexpr.cpp one, Templates.cpp 1466, 1518, 2215).

**The census** (Linux, `-check=templates -templates=subst` over 1,296 cases): 350 nonzero exits as REF's 1,295, none failing on
a difference; covered 356, built 356, as REF. Without the check **286 substituted with no replay and 70 replayed**, as REF;
**without the lanes' probes 111** (+0): this part makes no body; it marks what slice 2f's second part must fold under the
binding. The probe's 5 replay by name.

**Gates** (REF `8c75b11a` in `~/m82tb`, NEW in `~/m82tc` - REF's tree with this commit shipped, one path length; every check on
this commit's own tree): `identical.sh` without the flag 0 differ on four targets (x86_64-linux 1,894, x86_64-windows 3,788,
arm64-darwin 1,894, tms6747 1,894) and with `-templates=subst` 0 differ on three (1,894, 3,788, 1,894); **-g** 0 differ with and
without the flag (x86_64-linux 727 the same, 350 refused alike, 219 by the tree's path alone; x86_64-windows 709, 375, 212).
`-check=fold` compared 16,745, 0 differ, 0 units failing (43 "a value-dependent name in a template's read"). run.sh 1289/0
without and with `-templates=subst`. **subst.sh 0 failed** (cases ceiling 17 of 17, 53 shapes; self 102 of 102, ceiling 26 of
26). syntax.sh 7/0. Self-compile of the six changed `.cpp` files rc 0. Static, each tool's own exit status: comments rc 0,
memory gate rc 0 (raw-pointers 89), ast-residue rc 0 (ExprPtr 2012, node-refs 298: not raised), ide/generate.py up to date;
exclusions rc 0 0/0 after the last re-point. g++ `-Werror` clean. The Wine stand-in: the 13 template-subst probes 13 passed with
`-templates=subst` and at -O0. **Windows**: cl /W4 /WX clean (85 s, alone); clang 19 (`-nostdinc++ -isystem include`) prints
the probe's `.expected`; names.sh 947 passed 0 failed and overload.sh 46 agreed 0 differed, on this commit's tree.

**Left**: slice 2f's second part - the Binding carries a non-type parameter's value (integral and enumeration) and the Typer
folds a `value-dependent` node under it (`N`, `sizeof(T) * N`, `T a[N]`, `static const int K = N`), pointer and reference
parameters replaying by name; then the census and the bodies newly made under -check=templates (third part). The local enum
defect above.

## M8.2 slice 2f, second part: a non-type parameter's value in the Binding, and the reads folded under it (lane m82u, 2026-11-19)

Branch `m8-2u` from `8afc757c` (m82t). **What it does, under `-templates=subst` only**: `Templates::Binding` carries the
specialization's non-type arguments (`values`, by parameter index), and `instantiate` no longer refuses a non-type parameter
outright: a pack or template parameter still replays by name, and a non-type parameter of a type other than `int` (an
enumeration, `unsigned`, a pointer, a reference, `T v`) replays by name ("a non-type parameter of a type other than int").
`Templates::valueShape` then holds every `value-dependent` node of the read (m82t's mark) to the value the replay folds:
a non-type parameter is its argument, and a const `int` local made from one is its initialiser folded under the binding
(`foldUnder`: names, int literals, parentheses, unary `+ - ~ !`, the binary arithmetic, bitwise, comparison and logical
operators, refused past int, on a zero divisor and on `INT_MIN / -1`). Each fold is kept by node (`Bound::values`), and the
Typer puts it on the read's record (`Typer::record`, `recordDiffers` holding it alike), so the lowering reads the replay's
constant and never the read's stand-in. A marked node the binding cannot fold makes its body replay by name ("a
value-dependent name the binding does not fold"); a static local over a parameter and any array local beside one replay
by name too (their value and bound are the read's stand-in, which no record carries). `switch`, `sizeof` and an enumerator
over a parameter are still refused before this (substShape), so `case N:`, `sizeof(T) * N` and `enum { E = N }` replay.

**The body proof** (decision i): under `-check=templates` the first measurement made the 6 bodies of the probe and held 2 of
them apart ("a 'name' resolved apart from its replay": `c` read as 0 against the replay's 3) - a read of a const local the
read left unmarked. Mended: an unmarked read of a const local from a parameter is folded too. Then a second const local
from a parameter (`d` after `c`) was found to read as a Local in the read, where the replay reads an enumerator-like
constant: that is m82t's marking, not this fold, and such a body replays by name. **Found, not mended** (no side tasks):
the second const int local made from a non-type parameter in a read resolves as a Local, not as a constant (the probe's
`add`); owed to slice 2f's third part.

**The census** (Linux, `-check=templates -templates=subst` over 1,297 cases): 350 nonzero exits as REF; covered 383
(REF 356, +27); without the check **309 substituted with no replay** (REF 286, +23) and 74 replayed at the lowering
(REF 70); **without the lanes' probes 130 of the suite's own bodies made with no replay (REF 111, +19)**. Replayed by
reason, new: a pack or template parameter 116, a non-type parameter of a type other than int 15, a value-dependent name the
binding does not fold 3 (the probes'), a static local over a non-type parameter 2. The new probe
`tests/cases/template-subst-nontype` (`.flags` `-templates=subst`; `.expected` g++'s): `scale` and `pick` 4 made with no
replay and equal under the check, `add` 2 replayed by name; template-subst-value's 5 replay with their reasons (2 a static
local, 1 a value-dependent name, 2 a `switch`); tests/subst.sh's lines say so.

**src/**: Templates.cpp +98 -3, Templates.h +7 -3, Typing.cpp +4 -1: 95,282 -> 95,384 (+102; 100 or more lines, so the
wave gate owes the fixed point). EXCLUSIONS re-pointed (Templates.cpp 1560, 1612, 2309).

**The self corpus held it too**: the first gate's subst.sh failed on the self-host's class-members door, shape 59 against
the record's 58 - the fold's maps (`std::map<std::string, long long>` and kin) were specializations the compiler's own source
had not made before, and one of them the read does not take. The record is not raised: the fold keeps only map types the
corpus already makes (`std::map<std::string, int>`; `Bound::values` a `std::map<std::uint32_t, std::size_t>` like `calls`,
each value kept plus 2^31).

**Gates** (REF `8afc757c` in `~/m82ub`, NEW in `~/m82uc` - REF's tree with this commit shipped, one path length; every check on
this commit's own tree, the second gate): `identical.sh` without the flag 0 differ on four targets (x86_64-linux 1,896,
x86_64-windows 3,792, arm64-darwin 1,896, tms6747 1,896) and with `-templates=subst` 0 differ on three (1,896, 3,792, 1,896);
**-g** 0 differ with and without the flag (x86_64-linux 728 the same, 350 refused alike, 219 by the tree's path alone;
x86_64-windows 710, 375, 212). `-check=fold` 0 units failing. run.sh 1290/0 without and with `-templates=subst`.
`-check=templates -templates=subst` over the cases: 350 nonzero exits as REF, none on a difference. **subst.sh 0 failed**
(self 102 of 102, ceiling 26 of 26, 3 shapes of 3). syntax.sh 7/0; typed.sh 940/0. Self-compile of `Templates.cpp` and
`Typing.cpp` rc 0. Static, each tool's own exit status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (raw-pointers
89), ast-residue rc 0 (ExprPtr 2012, node-refs 298: not raised), ide/generate.py up to date. g++ `-Werror` clean. The Wine
stand-in: the 14 template-subst probes 14 passed with `-templates=subst` and at -O0. **Windows** (this commit's tree):
cl /W4 /WX clean (76 s, alone); clang 19 (`-nostdinc++ -isystem include`) prints the probe's `.expected`, as cxx3-msvc's
build does; names.sh 948 passed 0 failed, overload.sh 46 agreed 0 differed.

**Left** (slice 2f's third part): the second const local from a parameter read as a Local (above); `case N:`, `sizeof(T) * N`
and `enum { E = N }` once `switch`, `sizeof` and a local enumeration are in substShape's list; a static local and an array
local over a parameter (their value and bound from the binding, not the read's stand-in); non-type parameters of an
enumeration or another integral type (the fold in that type); the local enum defect m82t found.

**Code** `e7189622` (its message says 95,383 and +101: the line count measured on its own tree is 95,384, +102, as above).

## M8.2 slice 2f, third part (1): a local enumeration is its own function's, declared twice no more (lanes m82v and m82w, 2026-11-19 and 20)

Branch `m8-2v` from `e5b19923` (m82u). **The defect** (m82t found it, in a function template): a local `enum` declared in
two functions - two specializations of one template, and REF refuses two plain functions alike - was "declared twice" (the
unnamed enumeration's enumerators) or "defined twice" (a named one's tag): an enumerator and an enumeration's type were kept
by name for the whole unit, where [basic.scope.block] gives each block its own. A refusal stricter than the standard.
**The mend**, on the replay path and the plain one alike: an enumerator of a local enumeration carries its function's symbol
(`EnumConst::owner`), and another function's is not the same declaration; a local enumeration's type takes its function's
symbol in `Parser::localEnumOwner_`, and a second function's `E` is the type `E$2` (shown as `E` to -g, found first through
`localTypes_`, its `E::a` keyed under it). Every program REF compiles is unchanged: the new names arise only where REF
refused. **Not mended** (no side tasks): one function's two blocks each with `enum { A }` are still refused; a local
enumeration that shadows a namespace's of the same tag is still "defined twice"; a local enumeration as a template argument
mangles through its tag (`E$2`), as REF's first one mangled through `E` - local-entity mangling is not written for enums.
The case `tests/cases/template-local-enum-twice` (clang 19 on Windows, `-nostdinc++ -isystem include`, prints its
`.expected`; g++ agrees).

**The case's two earlier forms**, each failing tests/subst.sh under m82v (the ceiling is not raised for either): a local
class over `T` in `pick` ("the size of a dependent type" 3 above its ceiling of 2 - the read leaves that body unread; REF
compiled it, and it was not the defect), then `Colour::Red` over the read's own local enumeration ("'' is not an enumerator
of ''": a read's `$read$f::E` keys its enumerators under the written tag - a read defect, **found, not mended**). Both left
the case; what is left is the defect alone.

**src/**: ParserType.cpp +21 -5, Parser.h +1, Entities.h +2: 95,384 -> 95,403 (+19). EXCLUSIONS re-pointed (ParserType.cpp, 11
lines, +16). **Every gate below ran on the final tree** (lane m82w, 2026-11-20: REF `e5b19923` built in `~/m82wb`, NEW in
`~/m82wc`, g++ `-Werror` clean). **tests/subst.sh 0 failed** (cases 17 unread under a ceiling of 17 in 12 reasons, 53 shapes;
self 102 of 102, 26 under 26; its "4 counts below the record" note stands as on REF - not re-recorded). **The census**
(Linux, `-check=templates -templates=subst`, 1,298 cases): 350 nonzero exits as REF; covered 385 (REF 383); without the
check 311 substituted with no replay (REF 309) and 74 replayed (REF 74); **without the lanes' probes 132 of the suite's own
bodies made with no replay (REF 130, +2: the new case's)**. identical.sh without the flag - only the new case differs, at -O0
and -O2 (REF refuses it): x86_64-linux 1,898/2, x86_64-windows 3,796/4, arm64-darwin 1,898/2, tms6747 1,898/2; with `-templates=subst`: the same on three targets (1,898/2, 3,796/4, 1,898/2). **-g** (every case, -O0, both ways): x86_64-linux
728 same, 350 refused alike, 220 differ - 219 by the tree path alone (`m82wb` read as `m82wc`, the include directory each
build carries) and the new case; x86_64-windows 710, 375, 213 - 212 by the path alone and the new case; the same counts with
`-templates=subst`. -check=fold 16,776 compared 0 differ. run.sh 1291/0 without and with `-templates=subst`. syntax 7/0,
typed 941/0. Self-compile of ParserType.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0, memory
gate rc 0 (89), ast-residue rc 0 (2012/298: not raised), ide up to date. Wine stand-in 14/14 both. **Windows**: cl /W4 /WX
clean (1 m 32 s with the ship, alone); clang 19 prints the case's `.expected` (46 976 / 26 -7 / 6 15 / 7 17), as cxx3-msvc's
build does; names.sh 949/0, overload.sh 46/0 (the code is m82v's; run again for the case's final form).

**Left**: from m82u's list, (2) the second const int local made from a parameter read as a Local, (3) `case N`, `sizeof(T) *
N`, `enum { E = N }`, static and array locals over a parameter, non-int non-type parameters; the read's `E::a` over its own
local enumeration (above); one function's two blocks each with `enum { A }`, a local enumeration shadowing a namespace's tag.

## M8.2 slice 2f, third part (2): a read's `E::a` over its own local enumeration (lane m82x, 2026-11-20)

Branch `m8-2x` from `82ad1782` (m82w). **The defect** (m82v and m82w found it): in a function template's read, `Colour::Red` or
`Size::Large` over the function's own local enumeration gave "'' is not an enumerator of ''" and left the body unread. The
read's enumeration is its own type, `$read$f::Colour`, but its `E::a` key (and a scoped one's every key) was made under the
written tag, `Colour::Red`, where `enumeratorThroughEnum` asks the type's tag. **The mend**: a read's local enumeration keys its
enumerators under the tag the type has (`renamed`, as a second function's `E$2` already did), so the read finds them as the
replay does. The case `tests/cases/template-local-enum-read` (`pick` over an unscoped `Colour`, `level` over a scoped `Size`;
clang 19 on Windows, `-nostdinc++ -isystem include`, prints its `.expected`, as g++ and cxx3-msvc's build do): REF reads none
of its 3 bodies (2 unread by the defect); NEW reads all 3, and each replays by name ("a 'conditional' the read resolved to a
type the binding does not carry" - the read's local enumeration is not a type the binding carries), so this part makes no
body. **Found, not mended** (no side tasks): a scoped local enumeration's `?:` (`v > 1 ? Size::Large : Size::Small`) leaves a
read unread, "'' does not convert to '' implicitly - a scoped enumeration converts only with a cast"; it left the case.

**Item (2) diagnosed, not mended** (the hour): m82u's "second const local read as a Local" is not about the second. A read of
any const int local made from a parameter (`const int c = N + 1; return x + c;`, one local) replays "a value-dependent name the
binding does not fold": the read resolves `c` as a Local, as the replay does (`-dump=typed`: `name 'c' rec=local`), and
`Templates::foldUnder` folds a Name only where it resolves as an Enumerator. The mend owed: a marked Name that resolves as a
Local is the slot's value, not a fold - left as it is where only its initialiser's names need the binding, and a constant
position over it (a template argument, a bound) still refused.

**src/**: ParserType.cpp +2 -1: 95,403 -> 95,404 (+1). EXCLUSIONS re-pointed (ParserType.cpp, 12 lines, +1).

**The census** (Linux, `-check=templates -templates=subst`, 1,299 cases): 350 nonzero exits as REF; covered 385 (REF 385);
without the check 311 substituted with no replay (REF 311) and 74 replayed (REF 74); **without the lanes' probes 132 of the
suite's own bodies made with no replay (REF 132, +0)**. The new case's 3 bodies are read and replay by name (above).

**Gates** (REF `82ad1782` in `~/m82xb`, NEW in `~/m82xc`, one path length; every check on this commit's tree): **tests/subst.sh
0 failed** (cases 17 unread under a ceiling of 17 in 12 reasons, 53 shapes; self 102 of 102, 26 under 26; the "4 counts below
the record" note as on REF, not re-recorded). identical.sh without the flag 0 differ on four targets (x86_64-linux 1,900,
x86_64-windows 3,800, arm64-darwin 1,900, tms6747 1,900; the new case compiles alike on REF, whose replay reads it) and with
`-templates=subst` 0 differ on three (1,900, 3,800, 1,900). **-g** both ways: x86_64-linux 730 same, 350 refused alike, 219 differ
by the tree path alone; x86_64-windows 712, 375, 212 by the path alone. `-check=fold` 16,789 compared, 0 differ, 0 units failing.
run.sh 1292/0 without and with `-templates=subst`. syntax 7/0, typed 942/0. Self-compile of ParserType.cpp rc 0. Static, each
tool's own status: exclusions rc 0 0/0 (after the re-point), comments rc 0, memory gate rc 0 (89), ast-residue rc 0
(2012/298: not raised), ide up to date. g++ `-Werror` clean. Wine stand-in 14/14 with `-templates=subst` and at -O0.
**Windows**: cl /W4 /WX clean (3 m 35 s with the ship, alone); clang 19 prints the case's `.expected`, as cxx3-msvc's build
does; names.sh 950 passed 0 failed, overload.sh 46 agreed 0 differed, on the final tree.

**Left**: item (2) as diagnosed above; item (3) (`case N`, `sizeof(T) * N`, `enum { E = N }`, static and array locals over a
parameter); a scoped local enumeration's `?:` in a read; the read's local enumeration as a type the binding carries.

## M8.2 slice 2f, fourth part: a const local over a parameter, read from its slot (lane m82y, 2026-11-20)

(lane m82y, branch `m8-2y` from `270e9b6e`; one lane-hour.) m82x's diagnosis mended: in a read, `const int c = N + 1;`
then `return x + c` replayed by name ("a value-dependent name the binding does not fold"), because the read resolves `c`
as a Local, as the replay does, and `Templates::foldUnder` folded a Name only where it resolves as an Enumerator. Now a
marked Name that resolves as a Local is a const int local of the body (`consts`, declared once, not a parameter's name)
whose initialiser folds under the binding; `valueShape` records no value for it - the typed tree reads it from its slot,
as the replay's record does - and `foldUnder` follows a Local through its initialiser, so `const int d = c * 2` folds too.
The probe `template-subst-nontype`'s `add` (two const locals, two specializations) is made with no replay: the probe
counts 6 made, 6 equal under `-check=templates` (REF 4, and 2 replayed by name); its `.expected` unchanged.
`template-subst-value` is unchanged (its `f` replays for the static local, `g` for `enum { E = N }`, `h` for the switch).

**src/**: Templates.cpp +4 -2: 95,404 -> 95,406 (+2). EXCLUSIONS re-pointed (Templates.cpp 1563, 1615, 2312).

**The census** (Linux, `-check=templates -templates=subst`, 1,299 cases): 350 nonzero exits as REF; covered 387 (REF 385);
without the check 313 substituted with no replay (REF 311) and 74 replayed (REF 74); **without the lanes' probes 132 of the
suite's own bodies made with no replay (REF 132, +0)**.

**Gates** (REF `270e9b6e` in `~/m82yb`, NEW in `~/m82yc`, one path length; every check on this commit's tree): **tests/subst.sh
0 failed** (self 102 of 102, 26 under 26; the probe lines for `template-subst-nontype` 4 -> 6 made and equal, its refused line
gone). identical.sh without the flag 0 differ on four targets (x86_64-linux 1,900, x86_64-windows 3,800, arm64-darwin 1,900,
tms6747 1,900) and with `-templates=subst` 0 differ on three (1,900, 3,800, 1,900): `add` made from the read emits what the
replay did. **-g** both ways: x86_64-linux 730 same, 350 refused alike, 219 differ by the tree path alone; x86_64-windows 712,
375, 212 by the path alone. `-check=fold` 16,789 compared, 0 differ, 0 units failing. run.sh 1292/0 without and with
`-templates=subst`. syntax 7/0, typed 942/0. Self-compile of Templates.cpp and ParserType.cpp rc 0. Static, each tool's own
status: exclusions rc 0 0/0 (after the re-point), comments rc 0, memory gate rc 0 (89), ast-residue rc 0 (2012/298: not
raised), ide up to date. g++ `-Werror` clean. Wine stand-in 14/14 with `-templates=subst` and at -O0. **Windows**: cl /W4 /WX
clean (1 m 44 s with the ship, alone); names.sh 950 passed 0 failed, overload.sh 46 agreed 0 differed, on the final tree.

**Left**: item (2) - `case N` (a 'switch' in the body), `sizeof(T) * N`, `enum { E = N }` (its enumerator resolves as an
Enumerator that is neither a parameter nor a const local: its initialiser is not in the read's walk), a static local and an
array local `T a[N]` over a parameter; item (3), a scoped local enumeration's `?:` that leaves a read unread. Not started.

## M8.2 slice 2f's bucket: a type parameter bound to a plain struct, reached by reference or pointer (lane m82z, 2026-11-20)

(lane m82z, branch `m8-2z` from `659c6646`; one lane-hour.) **Step (0), the uncovered census by reason, measured first** (REF
`659c6646`, Linux, `-check=templates -templates=subst`): over tests/cases (1,299 units) the largest replay reason is "a type
parameter bound to a class or an array", 316 (without the lanes' probes 312; then a pack or template parameter 116, a `cast`
54); over the self-host's 102 sources it is 3,742 of 4,948 replayed (then a `name` typed apart 376, 356 covered). The
coordinator's bucket stands.

**What lands**: a type parameter bound to a plain struct of the unit - `Templates::boundClass`: no template's specialization,
base or virtual function, every member a public scalar of its own - where the body builds, copies and destroys no object of
it: no local or parameter of a class by value, no return of the bound class by value, no operator, assignment or return over
a class's object (`Templates::boundObject`: a name of it, `*p`, `p[i]`). A member access the read has no record of (`t.x`,
`p->x`, `(*p).y`, `p[1].x`) is covered where its object is a name of a bound class (or `*p`, `p[i]` of a pointer to one) and
the member a data member of it; a member the read did record (a non-dependent object) is the replay's record and covered too.
The Typer makes the record (`Typer::boundMember`) with `Parser::memberRecord` - one writer: both of the parser's member paths
(`.` and `->`) now make their record through it, the const of [expr.ref]/4 written once. `recordDiffers` lets a bound
member's record stand without a parser record; `typedApart` holds it against the replay's under `-check=templates`. The step
the brief calls (1)'s constructor half - a local `T v;`, a copy or a return of T by value - is not made: each replays by name
("a local or a parameter of a class by value, which its constructor builds", "a bound class returned by value, which its copy
would build"), and lifetime.h's ledger is not reached by anything this commit makes. (2), an array bound to T, not started.

**The probe** `tests/cases/template-subst-bound` (`.flags` `-templates=subst`): `sumOf`, `bump`, `second`, `add` made with no
replay (4 made, 4 equal under `-check=templates`; REF 0 - all 7 replayed "bound to a class or an array"); `mean` replays for
its `?:` ("a 'conditional' the read resolved to a type the binding does not carry"), `copyOf` and `viaLocal` by name as above.
Its `.expected` is g++'s and clang 19's on Windows (below). tests/subst.sh: the probe's lines added; template-subst-adl 9 -> 11
made (its `weigh(T *)` and `size(T *)` over `geo::Pt`; its refused line gone) and template-subst-arg 18 -> 20 (two bodies over
a non-dependent member).

**The census** (Linux, 1,300 units, 350 nonzero exits as REF): covered 398 (REF 387); without the check 324 with no replay (REF
313), 74 replayed; **without the lanes' probes 135 of the suite's own bodies with no replay (REF 132, +3**: template-adl-class
3 -> 4, template-adl-hidden-friend 1 -> 3). The old reason splits (all cases): 159 "a union or a type that is no class" -
a probe shows a reference bound by a forwarding reference lands here (`T` = `int &`, `P &`, `const char (&)[2]`) - not split further;
64 a class template's specialization; 25 a class with a member other than a public scalar of its own; 19 a member that is no
data member of a bound class reached by name; 14 a local or a parameter of a class by value; 3 a bound class returned by value;
2 a class with a base or a virtual function; 1 an operator, assignment or return over a class's object. So the bucket was not
one: the next sizeable pieces are the 159 (a reference binding needs no constructor) and class template
specializations (64), before the constructor half.

The self-host's 102 sources under NEW: covered 356 as REF (+0); the old 3,742 split as 3,425 "a union or a type that is no
class", 207 a class template's specialization, 50 a class with a member other than a public scalar, 23 a class with a base or
a virtual function, 13 a member that is no data member of a bound class reached by name, 2 an operator over a class's object,
1 an array. The self-host's class-bound bodies are its library's (`std::vector<Entry>` and the like) and replay as before.

**src/**: Templates.cpp +75, Templates.h +2, Typing.cpp +18, Typing.h +2, Parser.h +6, ParserExpr.cpp -10: 95,406 ->
95,499 (+93). EXCLUSIONS re-pointed (Templates.cpp 1638, 1690, 2387; ParserExpr.cpp 3770).

**Gates** (REF `659c6646` in `~/m82zb`, NEW in `~/m82zc`, one path length; every check on this commit's tree): identical.sh
without the flag 0 differ on four targets (x86_64-linux 1,902, x86_64-windows 3,804, arm64-darwin 1,902, tms6747 1,902) and
with `-templates=subst` 0 differ on three (1,902, 3,804, 1,902) - the new case included, its bodies made emitting what REF's
replay does; **-g** both ways: x86_64-linux 731 same, 350 refused alike, 219 differ by the tree path alone; x86_64-windows 713,
375, 212 by the path alone. `-check=templates -templates=subst` over tests/cases: the 350 nonzero exits are REF's own set,
none failing on a difference. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26; cases 17 under 17, 53 shapes of 53).
run.sh 1293/0 without and with `-templates=subst`. typed 943/0, syntax 7/0. `-check=fold` 16,797 compared, 0 differ, 0 units
failing. Self-compile of Templates.cpp, Typing.cpp and ParserExpr.cpp rc 0. Static, each tool's own status: exclusions rc 0
0/0 (after the re-point), comments rc 0, memory gate rc 0 (89: the Typer's member made with TypeId handles, not pointers),
ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows** (work\m82zv, the final tree): cl
/W4 /WX clean (about 5 min, beside this lane's superseded names run; the first build, of the edit before the static fixes,
about 3.5 min alone); clang 19 and cxx3-msvc print the probe's `.expected`; names.sh 951 passed 0 failed, overload.sh 46
agreed 0 differed. The Wine stand-in was not run (the box answered). lifetime.h is not used: no object is built by what lands.

**Left**: the constructor half of (1) - a local `T v;`, a copy or a return of T by value, with the construction records the
read needs written by the replay's parse writer; a reference bound to T (the 159 / 3,425 above, to be named apart first);
class template specializations (64 / 207); classes with bases, private or class-typed members; (2) an array bound to T;
slice 2f's items owed by m82y.

## M8.2 slice 2f's bucket, split: a type parameter bound to an lvalue reference to a scalar (lane m82aa, 2026-11-20)

(lane m82aa, branch `m8-2aa` from `71c540e8`; one lane-hour.) **Step (0), m82z's "a union or a type that is no class" split
by what T is bound to** (`Templates::boundClass` names it; measured on a build that changes the reason's words alone, Linux,
`-check=templates -templates=subst`): over tests/cases, 159 = 83 an lvalue reference to a class, 66 an lvalue reference to a
scalar, 7 an lvalue reference to an array, 2 an rvalue reference to a scalar, 1 void (none a union, a function type or any
other); without the lanes' probes 157 = 83 / 64 / 7 / 2 / 1. Over the self-host's 102 sources, 3,425 = 2,808 an lvalue
reference to a class, 616 an lvalue reference to a scalar, 1 an lvalue reference to an array. So every one is a reference -
the forwarding reference `T &&` deduced from an lvalue ([temp.deduct.call]/3: T = U &) - and the largest part is a reference
to a class; this lane took the reference to a scalar, which needs neither boundClass's rules nor a member record.

**What lands**: a type parameter may bind an lvalue reference to a scalar (`int &`, `const int &`, `double &`). The Binding
already carries the reference's TypeId; `Templates::substituted` - the one substitution the Typer, the shell and the requests
use - now collapses as [dcl.ref]/6 does (`T &&` and `T &` with T = U & are U &; `T &` with T = U && is U &) and drops a const
over a reference ([dcl.ref]/1: `const T` with T = U & is U &), as the replay's re-read declaration does. A bound class's
"returned by value" refusal now asks for a class (a reference returned is no copy). **Refused by name**: a function whose
read return type is dependent while a type parameter is bound to a reference replays ("a dependent return type over a type
parameter bound to a reference, collapsed apart from the replay's"): under `-check=templates` the first measurement made
`same(T &&a) { return a; }` with T = `int &` and `madeApart` found its return type apart from the replay's - not diagnosed
within the hour, so refused rather than trusted. A reference to a class, to an array, an rvalue reference stay as split above.

**The probe** `tests/cases/template-subst-fwd` (`.flags` `-templates=subst`; clang 19 on Windows with cxx3's own `<utility>`,
g++ on Linux and cxx3-msvc print its `.expected`): `inc`, `twice` (from an lvalue, a const lvalue and a prvalue), `setTo`
(`T b = a;` a reference local), `through`, `viaStd`, `moved` made with no replay - 6 made, 6 equal under `-check=templates`
(REF 1 made, 12 replayed "a union or a type that is no class"); `same` and the three `fwd`/`std::forward`/`std::move`
specializations over a reference replay by the new reason, `static_cast` bodies by "a 'cast' in the body". tests/subst.sh:
the probe's lines added (its 6 made in each spelling, 6 equal, the new reason's 4); no other probe's count moves.

**The census** (Linux, `-check=templates -templates=subst`, 1,301 units, 350 nonzero exits - the same 350 as the build that
changes the words alone): covered 405 (REF 398); made 331 equal, 0 differ beyond the 74 replayed by reason as REF; without the
check 331 with no replay (REF 324), 74 replayed; **without the probes 136 of the suite's own bodies with no replay (REF 135,
+1**: forwarding-reference 0 -> 1). Of the old 66 / 64 references to a scalar, 56 now replay by the new reason, the rest by
reasons of their bodies (a dependent-name call, a sizeof, an explicit-argument call). The self-host (102 sources, 0 nonzero): covered 356 as REF (+0); its 616 references to a scalar
all replay by the new reason (its library's `std::forward` and `std::move`), its 2,808 references to a class as split.

**src/**: Templates.cpp +17: 95,499 -> 95,516. EXCLUSIONS re-pointed (Templates.cpp 1655, 1707, 2404).

**Gates** (REF `71c540e8` in `~/m82aab`, NEW in `~/m82aac`, one path length; every check on this commit's tree): identical.sh
without the flag 0 differ on four targets (x86_64-linux 1,904, x86_64-windows 3,808, arm64-darwin 1,904, tms6747 1,904) and
with `-templates=subst` 0 differ on three (1,904, 3,808, 1,904), the new case among them; **-g** both ways:
x86_64-linux 731 same, 350 refused alike, 220 differ by the tree path alone; x86_64-windows 713, 375, 213 by the path alone.
`-check=templates -templates=subst` over tests/cases: 350 nonzero exits, the set of the build that changes the words alone,
none failing on a difference. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3). run.sh 1294/0
without and with `-templates=subst`. typed 944/0, syntax 7/0. `-check=fold` 16,866 compared, 0 differ, 0 units failing.
Self-compile of Templates.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0
(89), ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows** (work\m82aaw, this source):
cl /W4 /WX clean (95 s alone, the first build of the step-1 edit 164 s); clang 19 and cxx3-msvc print the probe's
`.expected`; overload.sh 46 agreed 0 differed; names.sh 952 passed 0 failed, after the first run's one failure was recorded - the probe's std::forward and std::move, which clang 19
folds as builtins, recorded in `template-subst-fwd.nonames` as a header difference.

**Left**: the dependent return type over a reference binding (56 in the suite) - why `same`'s made return type is apart from
the replay's; a reference to a class (83 / 2,808, with boundClass's rules and the member record), to an array (7), an rvalue
reference (2); class template specializations (64 / 207); the constructor half of m82z's (1); an array bound to T.

## M8.2 slice 2f: the dependent return over a reference binding, diagnosed and mended (lane m82ab, 2026-11-20)

(lane m82ab, branch `m8-2ab` from `1ca3569f`; step (1) of the lane's brief.) **The diagnosis**: m82aa's `same(T &&a) {
return a; }` with T = `int &` was apart from the replay in its return type alone. A function returning a reference is
emitted with a pointer return (ParserTopLevel's `emittedReturn`: `int &` returns as `int *`), so the read's own function holds
`T &&` as `T *`; `madeFromRead` substituted that pointer - `int & *` - where the replay re-reads `T &&` with T = `int &`, collapses
it to `int &` ([dcl.ref]/6) and emits `int *`. **The mend**: `madeFromRead` substitutes the return type as written (the
parser's `declaredReturns_`, the one record the typed tree reads it from) and emits a reference so substituted as a pointer
to its referent - `T &&` or `T` with T = `U &` is `U *`, as the replay's; m82aa's refusal ("a dependent return type over a
type parameter bound to a reference, collapsed apart from the replay's") is deleted, not kept as a stand-in.

**The probe** `template-subst-fwd` (unchanged; its `.expected` stands): 7 made, 7 equal under `-check=templates` (REF 6) -
`same` is made; `fwd` and the specializations of `std::forward` and `std::move` over a reference now replay by "a 'cast' in the
body", their `static_cast<T &&>`. tests/subst.sh: the probe's lines 6 -> 7 and the reason re-pointed.

**The census** (Linux, `-check=templates -templates=subst`, 1,301 units, 350 nonzero as REF): covered 406 (REF 405), made 332
equal, none differ beyond the 74 replayed as REF; without the check 332 with no replay (REF 331), 74 replayed; **without the
probes 136 of the suite's own bodies with no replay (REF 136, +0)**: the 56 the refusal held now stop at their bodies' own
reasons ("a 'cast' in the body" 112 without probes, the largest; a dependent-name call, an explicit-argument call). The
self-host (102 sources, 0 nonzero): covered 356 as REF (+0); its 616 references to a scalar stop at their bodies (a 'cast'
651, the library's `std::forward` and `std::move`); 2,808 references to a class as split.

**src/**: Templates.cpp +1: 95,516 -> 95,517. EXCLUSIONS re-pointed (Templates.cpp 1656, 1708, 2405).

**Gates** (REF `1ca3569f` in `~/m82abb`, NEW in `~/m82abc`, one path length; every check on this commit's tree):
identical.sh without the flag 0 differ on four targets (x86_64-linux 1,904, x86_64-windows 3,808, arm64-darwin 1,904, tms6747 1,904) and with `-templates=subst` 0 differ on three (1,904, 3,808, 1,904); **-g** with and without the flag: x86_64-linux 731 same, 350 refused alike, 220 differ by the tree path alone; x86_64-windows 713, 375, 213 by the path alone. `-check=templates -templates=subst` over tests/cases: 350 nonzero exits as REF, none failing on a difference. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3). run.sh 1294/0 without and with `-templates=subst`. typed 944/0, syntax 7/0. `-check=fold` 16,866 compared, 0 differ, 0 units failing. Self-compile of Templates.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (89), ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows** (work\\m82abw, this source): cl /W4 /WX clean (3 m 54 s with the ship, alone); names.sh 952 passed 0 failed; overload.sh 46 agreed 0 differed.

## M8.2 slice 2f: a type parameter bound to an lvalue reference to a plain struct (lane m82ab, 2026-11-20)

(lane m82ab, step (2), on `160563c5`.) **What lands**: T may bind an lvalue reference to a class (`T &&` deduced from an
lvalue of P is T = `P &`, from a const lvalue `const P &`) where m82z takes the class itself - a plain struct of the unit
(`boundClass` asked of the referent; a class it refuses replays by that class's reason, by name). One helper,
`boundTo`, names what a binding reaches (the class itself, or what a reference to one refers to), and `substShape` asks it
for the two questions m82z asked of the class: whether a class is bound at all (so no object of it is built, copied or
operated on) and whether a member access's object is a bound class (the Typer's `boundMember` already types `p.x` through
a reference). Member access through the reference, `T q = p;` (a reference local), passing it on (`sum(p)`), are made
from the read and held under `-check=templates`; `return p;` over the class's object, a deduced call over the class (one
of `std::forward<T>` / `std::move`'s specializations) and their `static_cast` bodies replay by name.

**The probe** `tests/cases/template-subst-fwdclass` (`.flags` `-templates=subst`; g++ on Linux, cxx3 on Linux and
clang 19 and cxx3-msvc on Windows print its `.expected`; `.nonames` as template-subst-fwd's: clang folds std::forward and std::move): 6 made, 6 equal
under `-check=templates` (REF 0); replayed by name: 2 by "a 'cast' in the body", 1 a deduced call of a function over a
class, 1 an explicit-argument call, 1 a return over a class's object (`same`). tests/subst.sh: the probe's lines.

**The census** (Linux, 1,302 units, 350 nonzero as REF): covered 412 (`160563c5` 406), 338 with no replay (332), 74
replayed; **without the probes 136 of the suite's own bodies (+0)**: the suite's 83 references to a class all pass the
binding now and stop at their bodies' own reasons - "a 'cast' in the body" 148 over all (119 before), std::forward and
std::move's own bodies. The self-host (102 sources, 0 nonzero): covered 356 (+0); its 2,808 references to a class now name the class's own reason - 1,402 a class template's specialization (1,609 in all), 972 a member other than a public scalar (1,022), 63 a base or a virtual function (86) - and 371 pass the binding and stop at "a 'cast' in the body" (1,022 in all), the library's std::forward and std::move.

**src/**: Templates.cpp +3: 95,517 -> 95,520. EXCLUSIONS re-pointed (Templates.cpp 1659, 1711, 2408).

**Gates** (REF `1ca3569f` in `~/m82abb`, NEW in `~/m82abd`, one path length; every check on this commit's tree):
identical.sh without the flag 0 differ on four targets (x86_64-linux 1,906, x86_64-windows 3,812, arm64-darwin 1,906, tms6747 1,906) and with `-templates=subst` 0 differ on three (1,906, 3,812, 1,906), the new case among them; **-g** with and without the flag: x86_64-linux 221 differ by the tree path alone, x86_64-windows 214 by the path alone (the new case one of each). `-check=templates -templates=subst` over tests/cases: 350 nonzero exits as REF, none failing on a difference. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3). run.sh 1295/0 without and with `-templates=subst`. typed 945/0, syntax 7/0. `-check=fold` 0 differ, 0 units failing. Self-compile of Templates.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (89), ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows** (work\\m82abv, this source): cl /W4 /WX clean (8 m 55 s with the ship, beside the coordinator's gate); names.sh 953 passed 0 failed; overload.sh 46 agreed 0 differed.

**Left**: the bodies that hold the forwarding reference now - `std::forward` and `std::move`'s `static_cast<T &&>` ("a 'cast'
in the body", 148 in the suite), the explicit-argument call `std::forward<T>(p)`, a return of a class's object by reference;
a reference to an array (7), an rvalue reference (2); class template specializations (64 / 207); the constructor half of m82z's.

## M8.2 slice 2f: a static_cast to a reference under the binding - std::forward's and std::move's own bodies (lane m82ac, 2026-11-20)

**What the read held apart.** `static_cast<T &&>(t)` and `static_cast<typename remove_reference<T>::type &&>(t)` replayed by
"a 'cast' in the body" (148 over tests/cases, 1,022 over the self-host), and behind the cast three facts were not the replay's:
(a) `X<T>::m` - std::forward's parameter `typename remove_reference<T>::type &`, std::move's return and cast type - stayed a
dependent member under the binding: `Templates::substituted` (the one substitution) now resolves it as memberTypeWalk does,
the specialization X<U> the replay made found by the key it is declared under (`specializationKey`, `findTypedef`) and m by
`lookupInClass` (`memberUnder`); a class not made, or an argument that is no type, leaves it dependent and replays by name.
(b) A read's cast to a dependent reference is a zero of its referent (`Parser::staticCast`), its category lost: the read's
`refCastTypes_` now keeps the written `&&` (ParserExpr's noteKeywordForm, a read alone), and `Templates::castUnder` collapses
it under the binding ([dcl.ref]/6: `T &&` with T = int & is int &, an lvalue) - one `Templates::castRecord` gives a node's cast
to `Typer::record`, the Typer's cast rule and `recordDiffers` alike. (c) A form's value record (`Resolved::Value`) substituted
to a reference reads as its referent ([expr]/5, `valueUnder`, `Typer::boundValue`). substShape takes a static_cast to a
reference over a name of its referent's type; any other cast replays by name ("a cast other than a static_cast to a
reference, which the read records no type for"; "... over another type, which the replay converts"; "a static_cast to a
member type of a class the binding has not made").

**Probes** (no new case; tests/subst.sh's lines): template-subst-fwd 11 made, 11 equal under -check=templates (was 7: fwd,
std::forward and std::move's bodies over a reference to a scalar), its 3 left replay by "an explicit-argument call";
template-subst-fwdclass 8 (was 6); template-subst-ref 16 (was 14: `mv`'s `static_cast<typename unref<T>::type &&>`).

**The census** (Linux, 1,302 units, 350 nonzero as REF): covered 511 (REF 412), 437 with no replay (REF 338), 74 replayed;
**without the probes 227 of the suite's own bodies (REF 136, +91)**; "a 'cast' in the body" is gone: 49 casts other than a
static_cast to a reference are left by that name (48 of the suite's own). The self-host (102 sources, 0 nonzero): covered 1,374 (REF 356, +1,018), all 1,374 made equal under -check=templates; its 1,022 at a 'cast' are 4 now, by the new name; 1,609 class template specializations remain its largest reason, then 376 "a 'name' the read resolved to a type the binding does not carry".

**src/**: Templates.cpp +55, Templates.h +4, Typing.cpp +4, Typing.h +1, ParserExpr.cpp +1: 95,520 -> 95,585 (+65).
EXCLUSIONS re-pointed (Templates.cpp 1714, 1766, 2463; ParserExpr.cpp 3771).

**Gates** (REF `b0b65370` in `~/m82acb`, NEW in `~/m82acc`, one path length; every check on this commit's tree): identical.sh without the flag 0 differ on four targets (x86_64-linux 1,906, x86_64-windows 3,812, arm64-darwin 1,906, tms6747 1,906) and with `-templates=subst` 0 differ on three (1,906, 3,812, 1,906); **-g** with and without the flag: x86_64-linux 221 differ by the tree path alone, x86_64-windows 214 by the path alone, 0 otherwise. g++ `-Werror` clean.
`-check=templates -templates=subst` over tests/cases: 350 nonzero exits as REF, none failing on a difference.
**tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3). run.sh 1295/0 without and with
`-templates=subst`. typed 945/0, syntax 7/0. `-check=fold` 16,938 compared, 0 differ, 0 units failing. Self-compile of
Templates.cpp, Typing.cpp and ParserExpr.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0, memory
gate rc 0 (89), ast-residue rc 0 (2012/298, not raised), ide up to date. **Windows** (work\\m82acw, this source): cl /W4 /WX
clean (95 s with the ship, alone); names.sh 953 passed 0 failed; overload.sh 46 agreed 0 differed.

**Left**: (2) the explicit-argument call `std::forward<T>(x)` / `fwd<T>(a)` (8 over the suite; templateCandidates refuses
any written `<`): the replay's door (ParserTemplate's explicit-list path) reads the list once per overload from the tokens
under the binding (`templateArguments` at the list, in a Trial), deduces what the list leaves, and ranks the made
specializations under a `$explicit:` key - requestCall would take the list's token from the callee node's position and do
the same in the instantiation's context, then rank as it ranks a deduced call; (3) functional and C-style casts over a
scalar T (a C-style cast's `castTypes_` holds T as written, so the Typer's substitution already covers its record; substShape
refuses it by name today); a static_cast to a non-reference dependent type (the read records no type for it).

## M8.2 slice 2f: a C-style, a functional and a static_cast to a non-reference type under the binding (lane m82ad, 2026-11-20)

**Step (0), the census at REF `9e0da666`** (Linux, 1,302 units, 350 nonzero): covered 511, 437 with no replay; without the
probes 301 covered, 227 with no replay; the largest reasons there 116 a pack or template parameter, 102 a class template's
specialization, 48 "a cast other than a static_cast to a reference", 38 a class with a member other than a public scalar,
26 a 'name' the binding does not carry, 4 an explicit-argument call. Self-host (102 sources, 0 nonzero): covered 1,374, all
equal; 1,609 a class template's specialization, 1,022 a class with a member other than a public scalar, 376 a 'name', 4 a cast.

**What landed (m82ac's (3)).** `(T)e`, `T(e)` and `static_cast<T>(e)` to an arithmetic type or void under the binding, over an
operand the typed tree types arithmetic, are made from the read: the C-style and functional casts' `castTypes_` already held T
as written, and a read's `static_cast<T>` to a dependent non-reference type is now recorded so too (ParserExpr's
noteKeywordForm, a read alone); `Templates::castRecord` substitutes it (the one cast typer, as m82ac's reference casts), and
`Templates::valueCast` says why any other replays: "a cast to a type other than an arithmetic type or void under the
binding", "a cast over an operand the typed tree does not type arithmetic", and "a cast to its operand's own type, which the
replay records as its operand" (the replay's no-op conversion hands the cast node its operand's record, which the read does
not have - seen as "resolved apart: its kind" under -check=templates before it was refused). typedApart holds a cast spelled
as its recorded type (`$T0` against `int`) by the cast's type, as it holds a var's.

**Not landed (m82ac's (2), the explicit-argument call).** Built and probed, not committed (boxkit/m82ad-wip/s2-explicit-wip.patch):
`Templates::explicitMade` reads the list again from its token in a Trial per overload, deduces what it leaves and instantiates
as Parser::templateCall's door does, requestCall ranking the made set; with a call returning an lvalue reference to a scalar
taken as an argument, template-subst-fwd made 12 equal (was 11) - but (a) one body (`through`, `twice(fwd<T>(a))`) was read
apart from its replay in a call node's place under -check=templates, not diagnosed, and (b) `std::forward<T>` found no one
name token at the callee's place (a qualified callee's node is placed elsewhere than its last name: to be measured).

**Probe** tests/cases/template-subst-cast (clang 19 and cxx3-msvc on Windows, g++ and cxx3 on Linux print its .expected): 5 made, 5 equal under
-check=templates, 5 by "its operand's own type", 1 a cast to a pointer, 1 `T()`; tests/subst.sh: the probe's lines, template-subst-deduce 15 -> 16 made equal (`mix`'s `static_cast<T>(b)`).

**The census** (Linux, 1,303 units, 350 nonzero as REF): covered 528 (REF 511), 452 with no replay (REF 437), 76 replayed;
**without the probes 236 of the suite's own bodies (REF 227, +9)**, covered 312 (REF 301); the 48 casts now 13 "other than a
static_cast to a reference" (named casts other than static_cast, `T()`, `T(a, b)`), 9 "its operand's own type", 6 "an operand
not arithmetic", 1 "a type other than arithmetic or void". The self-host (102 sources, 0 nonzero): covered 1,374 (+0), all
equal; its 4 casts are 2 by "its operand's own type" and 2 that now stop at a 'literal' (96 -> 98).

**src/**: Templates.cpp +28, Templates.h +1, Typing.cpp +4, ParserExpr.cpp +1: 95,585 -> 95,619 (+34). EXCLUSIONS re-pointed
(Templates.cpp 1742, 1794, 2491; ParserExpr.cpp 3772).

**Gates** (REF `9e0da666` in `~/m82adb`, NEW in `~/m82adc`, one path length; every check on this commit's tree): identical.sh without the flag 0 differ on four targets (x86_64-linux 1,908, x86_64-windows 3,816, arm64-darwin 1,908, tms6747 1,908) and with `-templates=subst` 0 differ on three (1,908, 3,816, 1,908); **-g** with and without the flag: x86_64-linux 221 differ by the tree path alone, x86_64-windows 214 by the path alone, 0 otherwise. g++ `-Werror` clean. `-check=templates -templates=subst` over tests/cases: 350 nonzero exits as REF, none failing on a difference. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3; it notes 4 ceiling counts below the record, not re-recorded). run.sh 1296/0 without and with `-templates=subst`.
typed 946/0, syntax 7/0. `-check=fold` 16,938 compared, 0 differ, 0 units failing. Self-compile of Templates.cpp, Typing.cpp
and ParserExpr.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (89), ast-residue
rc 0 (2012/298, not raised), ide up to date. **Windows** (work\\m82adw, this source): cl /W4 /WX clean (442 s with the ship, a fresh tree, beside the coordinator's gate); names.sh 954 passed 0 failed; overload.sh 46 agreed 0 differed.

**Left**: the explicit-argument call (above, its WIP patch and the two faults it showed); the 13 casts other than a static_cast to a reference that remain (const_cast and reinterpret_cast to no reference, `T()`, `T(a, b)`), and a no-op cast, which wants the replay's record of its operand made for the read; then the census's largest: 116 a pack or template parameter, 102 a class template's specialization.

## M8.2 slice 2f: a type parameter bound to a class template's specialization, used as a plain struct (lane m82ae, 2026-11-20)

**Step (0), the split.** At REF `3ac9d01f` (m82ad's NEW census: covered 528, 452 with no replay; without the probes 236) "a
class template's specialization" was 102 of the suite's own bodies and 1,609 of the self-host's. Split by building the census
twice - this commit, whose `boundClass` names a specialization's reasons apart, and a diagnostic build (not committed) that
takes every completed specialization - and reading where the bodies go. Of the suite's 102: 47 pass the plain-struct test
(every member a public scalar, no base) and then go by what the body does - +14 made with no replay (member access on a
public scalar member), the rest to their bodies' next reasons (a member function call, "a call through a value, a member or a
class" and "a member that is no data member of a bound class"; a local or copy by value; a call passing it on); 54 have a
member other than a public scalar and 1 a base. Taking those 55 too (the diagnostic build) makes 29 more with no replay
(member access on a public scalar member of a class whose other members are not), 13 replay as a bound class returned by
value, 4 as an operator over the class's object, 3 as a call through a member, the rest by their bodies' other reasons; all
499 made under the check equal. Over the self-host the diagnostic build covers 2,787 (REF 1,374, +1,413), all 2,787 equal
under -check=templates - the self-host's specializations are its library's (`std::vector<Entry>` and the like), whose
members are private and class-typed: this commit takes 149 of the self-host's 1,609 (covered 1,523), and 1,407 replay as a
specialization with a member other than a public scalar.

**What lands.** `Templates::boundClass` takes a class template's specialization the instantiation has completed as m82z
takes a plain struct of the unit: no base or virtual function, every member a public scalar of its own; member access and
its records through `Typer::boundMember` and `Parser::memberRecord` as for a plain struct; held under -check=templates. Its
refusals now say they are a specialization's ("a class template's specialization with a member other than a public scalar
of its own", "... with a base or a virtual function", "... the instantiation has not completed"); a plain class's reasons are
spelled as before. Not landed: the 55 above (the diagnostic build is the change; its gates are owed), (2) a member function
call on the bound specialization, and m82ad's explicit-argument call.

**The probe** `tests/cases/template-subst-spec` (`.flags` `-templates=subst`): `sumOf` over `Box<int>` and `Box<double>`,
`bump`, `second` made with no replay (4 made, 4 equal; REF 0); `Row<int, 2>` (an array member), `Hid<int>` (a private member,
a member function call) and `Wrap<int>` (a class member) replay as a specialization with a member other than a public scalar,
`viaLocal` as a local by value. Its `.expected`: g++ and cxx3 on Linux, clang 19 and cxx3-msvc on Windows print it.
tests/subst.sh: the probe's lines.

**The census** (Linux, 1,304 units, 350 nonzero as REF): covered 546 (REF 528), 470 with no replay (REF 452), 76 replayed;
**without the probes 250 of the suite's own bodies (REF 236, +14)**, covered 326 (REF 301). Self-host (102 sources, 0
nonzero): covered 1,523 (REF 1,374, +149), all 1,523 made equal; 1,407 a specialization with a member other than a public
scalar, 1,022 a class with one, 376 a 'name', 161 a dependent-name call, 98 a 'literal'.

**src/**: Templates.cpp +2: 95,619 -> 95,621. EXCLUSIONS re-pointed (Templates.cpp 1744, 1796, 2493).

**Gates** (REF `3ac9d01f` in `~/m82aeb`, NEW in `~/m82aec`, one path length; every check on this commit's tree): identical.sh
without the flag 0 differ on four targets (x86_64-linux 1,910, x86_64-windows 3,820, arm64-darwin 1,910, tms6747 1,910) and
with `-templates=subst` 0 differ on three (1,910, 3,820, 1,910); **-g** with and without the flag: x86_64-linux 221 differ by
the tree path alone, x86_64-windows 214 by the path alone, 0 otherwise. **tests/subst.sh 0 failed** (self 102 of 102, 26
under 26, 3 shapes of 3). run.sh 1297/0 without and with `-templates=subst`.
`-check=templates -templates=subst` over tests/cases: 350 nonzero exits, REF's count, none failing on a difference.
typed 947/0, syntax 7/0. `-check=fold` 16,952 compared, 0 differ, 0 units failing. Self-compile of Templates.cpp, Typing.cpp
and ParserExpr.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0, memory gate rc 0 (89),
ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows** (work\\m82aew, this source): cl /W4
/WX clean (3 min 38 s with the ship, alone of this lane's, beside the coordinator's gate running cases); names.sh 955 passed
0 failed; overload.sh 46 agreed 0 differed.

**Left**: the 55 specializations with a member other than a public scalar or a base - the diagnostic build's one line, owed
its gates (identical with and without the flag, -g, run.sh) and an answer to why m82z's plain-struct test asks every member
to be a public scalar when the body reaches only the data members it names; a member function call on a bound class through
the one ranking; m82ad's explicit-argument call (boxkit/m82ad-wip/s2-explicit-wip.patch).

## M8.2 slice 2f: every completed class template specialization bound to T, its members asked where the body reaches one (lane m82af, 2026-11-20)

**Why m82z's plain-struct test asked what it asked** - each condition taken away alone in a diagnostic build (m82ae's s2,
every completed specialization accepted) and held by probes (tests/cases/template-subst-member, -private-refused) against
g++ and the replay:
- **A public member - needed.** A body made from the read is not replayed under `-templates=subst`, and the replay is the
  only thing that checks access (`checkAccessible` in the parser's member path; `Parser::memberRecord` and
  `Typer::boundMember` carry no access). With the test gone, `peek(h)` reading `Hid<int>`'s private `x` compiled and
  printed 2 where clang, g++ and the replay refuse it.
- **A scalar member - needed.** `a.n = b.n` over a member of class type with its own `operator=` was made from the read and
  the SSA lowering then refused the unit ("an assignment to what is not a scalar object"); the copy is the replay's (an
  operator call), not a store. A member read by subscript (`r.v[0]`, an array of scalars) is made equal.
- **No base - not needed.** A member declared in a base has its whole offset in `Member::offset` (`adjust` is only for
  `o.Base::m`): `own` and `inBase` over `D1<int> : B0, B1` made equal and right; the conversion to a base (`toBase`) is made
  right (6), its frame apart from the replay's under the check ("a slot given back"), so it replays there.
- **No virtual function - not needed.** The vptr is already in the members' offsets: `poly` over `Poly<int>` made equal and
  right (10). **A member in a virtual base - kept refused**: not at a constant offset (the parser reaches it through
  `virtualBaseMember`); the diagnostic build typed `viaVirtual` apart from its replay under the check. Both were measured
  with the probe's first form (boxkit/m82af-wip/template-subst-member-full.cpp, one template alone at a time by
  boxkit/m82af-each.sh) and left out of the committed probe: names.sh on Windows found `VD<int>`'s vbtable name
  `??_8VD<int>@@7B@` refused by the MASM spelling and the implicit C1/C2 constructors of both apart from clang's - neither
  this slice's, both owed to the names register.
- **Bit-fields - not needed.** `bits` writes and reads `int a : 3; int b : 5` made equal and right (10).
- **A static member** is no data member: `t.s` replays by that name, as before.

**What changed**: `Templates::boundClass` takes a completed specialization whatever its bases and members; `substShape`'s
member case asks the member the body reaches: public, a scalar or an array of one, not in a virtual base, an anonymous union
or two bases - each refused by name. A plain class of the unit keeps m82z's test whole (the next lane may move it to the
access as well). The probes: template-subst-member (4 made with no replay, 3 equal under the check, `toBase` apart by its
frame; `copyIn`, `byValue`, `stat` replay by name), template-subst-private-refused (an `.error` case: refused
with and without the flag, the replay's message); template-subst-spec's three specializations now replay by their bodies'
own shapes. tests/subst.sh: the probes' lines.

**src/**: Templates.cpp +8: 95,621 -> 95,629. EXCLUSIONS re-pointed (Templates.cpp 1752, 1804, 2501).

**The census** (Linux, 1,306 units, 351 nonzero - REF's 350 and the new `.error` case, none failing on a difference): covered
579 (REF 546), 503 with no replay (REF 470), 76 replayed; **without the probes 279 of the suite's own bodies (REF 250,
+29)**, all 279 made equal, covered 355 (REF 326). **Self-host** (102 sources, 0 nonzero): covered 2,787 (REF 1,523,
+1,264), all 2,787 made equal; 1,022 a plain class with a member other than a public scalar, 376 a 'name', 278 a dependent
call, 86 a plain class with a base or a virtual function.

**Gates** (REF `844fae37` in `~/m82afb`, NEW in `~/m82afc`, one path length; every check on this commit's tree): identical.sh
without the flag 0 differ on four targets (x86_64-linux 1,912, x86_64-windows 3,824, arm64-darwin 1,912, tms6747 1,912) and
with `-templates=subst` 0 differ on three (1,912, 3,824, 1,912); **-g** with and without the flag: x86_64-linux 221 differ by the tree path alone, x86_64-windows 214 by the path alone, 0 otherwise. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3).
run.sh 1299/0 without and with `-templates=subst` (no probe builds an object in a made body: the ledger is not asked). `-check=templates -templates=subst` over tests/cases: 351
nonzero exits as above. typed 948/0, syntax 7/0. `-check=fold` 16,958 compared, 0 differ, 0 units failing. Self-compile of
Templates.cpp, Typing.cpp and ParserExpr.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0,
memory gate rc 0 (89), ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows**
(work\\m82afw, this source): cl /W4 /WX clean (173 s with the ship, alone); names.sh 955 passed with the probe's first form
failing (above), then the committed probes 2 passed 0 failed; overload.sh 46 agreed 0 differed; clang 19 and cxx3-msvc print
template-subst-member's `.expected` and both refuse the private read.

**Left**: a plain class of the unit asked at the access as a specialization now is (its 1,022 self-host and 38 suite
bodies); a member function call on a bound class through the one ranking; `VD<int>`'s vbtable name under the MASM spelling;
m82ad's explicit-argument call (boxkit/m82ad-wip/s2-explicit-wip.patch).

## M8.2 slice 2f: a plain class bound to T asked at the member a body reaches, as a specialization is (lane m82ag, 2026-11-20)

**Why**: m82af showed which of m82z's whole-class conditions a made body needs - a public member (only the replay checks
access), a scalar member (a class member's operator= the lowering refuses), no virtual base (no constant offset) - and which
it does not (a base, a virtual function, a bit-field); it moved a specialization to the member reached and left a plain class
of the unit on the whole-class test, its 38 suite and 1,022 self-host bodies replayed by "a class with a member other than a
public scalar of its own" and 86 self-host by "a class with a base or a virtual function".

**What changed**: `Templates::boundClass` takes any struct of the unit as it takes a completed specialization: one check for
both, in `substShape`'s member case at the member the body reaches - public, a scalar or an array of one, not in a virtual
base, an anonymous union or two bases, each refused by name as before. The two whole-class reasons are gone. The probes:
tests/cases/template-subst-plain (g++ and cxx3 on Linux, clang 19 and cxx3-msvc on Windows print its `.expected`): REF made 0 (its seven
templates replayed by the two whole-class reasons and one by a static member's name); now 5 made with no replay, 5 equal
under the check (`scalarOf`, `own`, `inBase` over two bases, `bits` over bit-fields, `poly` through a pointer to a class with
a virtual function), `copyIn` replayed by its class-typed member, `stat` by a static member's name;
template-subst-plain-private-refused (an `.error` case: a plain class's private member read through T is refused with and
without the flag, the replay's message, as clang and g++ refuse it). tests/subst.sh: the probe's lines.

**src/**: Templates.cpp -9, Templates.h 0: 95,629 -> 95,620 (-9). EXCLUSIONS re-pointed (Templates.cpp 1743, 1795, 2492).

**The census** (Linux, 1,308 units, 352 nonzero - REF's 351 and the new `.error` case; none fails on a difference, the
`made ... differ: replayed` lines of four sfinae and decltype cases are REF's as they were): covered 601 (REF 579), 525 with
no replay (REF 503); **without the probes 296 of the suite's own bodies (REF 279, +17)**, all made equal, covered 372; the
whole-class reasons are gone from both corpora, their other bodies at their next reasons. **Self-host** (102 sources, 0
nonzero): covered 3,824 (REF 2,787, +1,037), all 3,824 made equal under `-check=templates`.

**Gates** (REF `3f7ca494` in `~/m82agb`, NEW in `~/m82agc`, one path length; every check on this commit's tree): identical.sh
without the flag 0 differ on four targets (x86_64-linux 1,914, x86_64-windows 3,828, arm64-darwin 1,914, tms6747 1,914 -
the two new cases agree with REF, which replays them) and with `-templates=subst` 0 differ on three (1,914, 3,828, 1,914);
**-g** with and without the flag: x86_64-linux 221 differ by the tree path alone, x86_64-windows 214 by the path alone, 0
otherwise. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3). run.sh 1301/0 without and with
`-templates=subst`. typed 949/0, syntax 7/0. `-check=fold` 16,962 compared, 0 differ, 0 units failing. Self-compile of
Templates.cpp, Typing.cpp and ParserExpr.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0,
memory gate rc 0 (89), ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows**
(work\\m82agw, this source): cl /W4 /WX clean (215 s with the ship, alone); names.sh 957 passed 0 failed, the probes 2/0;
overload.sh 46 agreed 0 differed; clang 19 and cxx3-msvc print template-subst-plain's `.expected`, both refuse the private
read (clang: "'x' is a private member of 'Hid'").

**Item (3), diagnosed from the source, not built**: a class-typed member reached through T is an assignment the read
records as built in - the replay resolves `a.n = b.n` to `Noisy::operator=` (a call record on the assign node), the read has
no record (the operands are dependent), so the made body reaches `Lowering.cpp:5544` "an assignment to what is not a scalar
object"; the fix is the member-call record of item (2) applied to an operator, after it.

**Left**: a member function call on a bound class (`t.f()`, `p->g(x)`) through the one ranking - scoped, not started
(boxkit/m82ag-handover.md: the set `overloadsOf(tag::name)` ranked as `dependentCall` ranks, a non-public member refused by
name, the record `completeCall` makes put on the call node by the Typer as `boundMembers_` puts a member's, and its own branch
in `recordDiffers` since a member's `Signature::params` lack the `this` pointer); a class-typed member reached through T;
`VD<int>`'s vbtable name under the MASM spelling; m82ad's explicit-argument call.

## M8.2 slice 2f: a member function called on a class bound to T, ranked over the class's members (lane m82ah, 2026-11-20)

**Why**: m82ag left a member function call on a bound class (`t.f()`, `p->g(x)`) replayed by "a call through a value or a
member with a type-dependent part": the read records no call for it (its object is dependent), so a body holding one was
never made from the read.

**What changed**: substShape's call case asks a call whose callee is a member of a bound class's object
(`Templates::boundObjectClass`, factored out of the member case, which now asks it too) of `Templates::boundMemberCall`: the
owner as `memberCallWith` finds it (`Lookup::findMemberOwner`), the set `overloadsOf(owner::name)` ranked over the arguments
`callArgTypes` types (`Overload::rankArgument`, `bestViable`), the record's function type built as `memberCallWith` builds it,
`this` first - a const member's `const C *`, a base's function on the base, which the lowering converts as it does any member
call's (`toBase`). The chosen function is the call's and its callee's in `Bound::calls` (marked used by `callsUsed`), its type
in `Bound::members`; `Templates::chosenType` is the one answer `Typer::boundCall` records (`hasThis` set) and `recordDiffers`
holds. Refused by name, each replayed: a member function other than public (the replay checks access, and refuses it as clang
does), a static one called through the object, a virtual one (its slot), a non-const one on a const object, a base's other
than through one public non-virtual direct base, a qualified, operator, destructor or template name, a set with a candidate
over a class, a reference, a default, variadic arguments or another object qualifier, a class argument.

**Probes**: tests/cases/template-subst-mcall (g++ and cxx3 on Linux, clang 19 and cxx3-msvc on Windows print its
`.expected`): 5 made with no replay; under `-check=templates` 4 equal and `onBase` apart by its frame - the replay's `this`
conversion to a base at offset 4 lays a slot no local names - so it replays there; `virt` and `stat` replay by name.
tests/cases/template-subst-mcall-private-refused (`.error`): refused with and without the flag, as clang refuses it.
template-subst-spec's `asked(h)`, `h.get()` on `Hid<int>`, is made now (5 made, 5 equal; REF 4). tests/subst.sh: the
probes' lines.

**src/**: Templates.cpp, Templates.h, Typing.cpp: 95,620 -> 95,717 (+97). EXCLUSIONS re-pointed (Templates.cpp 1833, 1885, 2582).

**The census** (Linux, 1,310 units, 353 nonzero - REF's 352 and the new `.error` case; none fails on a difference): covered
609 (REF 601), 531 with no replay (REF 525) - the probes' 6; **without the probes 296 of the suite's own bodies (REF 296,
+0)**, all made equal. **Self-host** (102 sources, 0 nonzero): covered 3,824 (REF 3,824, +0), all made equal. No body of the
suite's own or the self-host's is made by this commit alone: their member calls stand behind other reasons first (without the
probes: 47 a dependent-name call, 21 a member that is no data member, 13 a call through a value the read did not resolve,
10 a call through a member with a type-dependent part, 2 a member call by a qualified, operator or template name).

**Gates** (REF `93668f05` in `~/m82ahb`, NEW in `~/m82ahc`, one path length; every check on this commit's tree but the
probe's out-of-line `D2::D2`, re-probed alone): identical.sh without the flag 0 differ on four targets (x86_64-linux 1,916,
x86_64-windows 3,832, arm64-darwin 1,916, tms6747 1,916) and with `-templates=subst` 0 differ on three (1,916, 3,832,
1,916); **-g** with and without the flag: x86_64-linux 221 differ by the tree path alone, x86_64-windows 214 by the path
alone, 0 otherwise. **tests/subst.sh 0 failed** (self 102 of 102, 26 under 26, 3 shapes of 3). run.sh 1303/0 without and
with `-templates=subst`. typed 950/0, syntax 7/0. `-check=fold` 16,962 compared, 0 differ, 0 units failing. Self-compile of
Templates.cpp, Typing.cpp and ParserExpr.cpp rc 0. Static, each tool's own status: exclusions rc 0 0/0, comments rc 0,
memory gate rc 0 (89), ast-residue rc 0 (2012/298, not raised), ide up to date. g++ `-Werror` clean. **Windows**
(work\\m82ahw, this source): cl /W4 /WX clean (about 3 minutes with the ship, alone); clang 19 and cxx3-msvc print
template-subst-mcall's `.expected` and refuse the private call (clang: "'hidden' is a private member of 'Sec'").

## M8.2 slice 2f: a class-typed member of a bound class assigned, `a.m = b.m`, by the operator= the replay calls (lane m82ah, 2026-11-20)

**Why**: m82ag's item (3): the replay resolves `a.n = b.n` over a member class with its own operator= to a call of it -
`Parser::assignValue` records the call on the value's node (`assignCalls_`) - while the read records nothing (its operands are
dependent), so a made body reached `Lowering.cpp:5544` and substShape refused every class-typed member by "a bound class's
member other than a scalar or an array of one, which an operator over it would copy".

**What changed**: one writer of the choice: `Parser::copyAssignFor` (the copy or move assignment by the value's category) and
`Parser::assignCallType` (the call's function type, `C *` returned and taken first), factored out of `assignValue`, which
calls them. substShape's assignment case takes `a.m = b.m` where both are members of a bound class's object of one class type
(the left not const): the operator= `copyAssignFor` answers for an lvalue is put in `Bound::assigns` by the value's node, or
none for a class assigned by its bytes; the two members are then allowed past the scalar rule. The Typer gives a made body's
value node that fact by its typed node (`Typer::boundAssigns_`, a read node being shared by every binding of one body), and
the lowering's `assignCall` callback is handed the typed node beside the syntax one; `madeWithoutReplay` marks the operator=
used as `assignValue` does. Refused by name: any other assignment over a class-typed member, an operator= other than public.

**Probe**: tests/cases/template-subst-assign (g++ and cxx3 on Linux print its `.expected`): 3 made, 3 equal - an operator=
through `.` and `->`, a member assigned by its bytes. template-subst-member (`copyIn`) and template-subst-plain (`copyIn`)
are made now: 5 and 6 substituted with no replay (REF 4, 5), 4 and 6 equal under the check; tests/subst.sh: their lines.

**Gates** (lane m82ai finished m82ah's item 2 and gated it on the commit's own tree, REF 80fe8c62): census 1311 units 353
nonzero (REF 1310 / 353, the new case 0), covered 614 (REF 609), 536 made with no replay (REF 531), without the probes 296
(REF 296, +0) all equal; identical against REF 0 differ on four targets plain (1918/3836/1918/1918) and three with
-templates=subst (1918/3836/1918); -g 0 differ beyond the tree path both ways, with and without the flag (linux 221, windows
214); subst.sh 0 failed (self 102/102, 26/26, 3/3), run.sh 1304/0 both, typed 951/0, syntax 7/0, fold 16962 compared 0
differ; static rc 0 all (exclusions 0/0, comments 0, memory 89, residue 2012/298 not raised, ide up to date), g++ -Werror
clean; Windows: cl /W4 /WX clean (213 s with ship), clang 19 and cxx3-msvc print template-subst-assign's `.expected`, names
959/0 (probes 3/0), overload 46/0; self-host census 102 sources 0 nonzero, covered 3,843 (REF 3,824, +19) all equal; self Templates.cpp Typing.cpp ParserExpr.cpp rc 0; src 95,717 -> 95,762 (+45).

## M8.2: the Itanium pattern signature kept from the declaration's read, and the self-host under -templates=subst (lane m82ai, 2026-11-20)

**The pattern signature, before**: re-read from tokens at every new specialization. `Parser::instantiate`
(ParserTemplate.cpp, after the key's dedupe) called `readTemplateDeclaration` a second time with every parameter bound
to `Kind::TemplateParam`, and `itaniumTemplateFunctionName` spelled that; the declaration's own read - `templatedName`'s,
the one that writes `TemplateDecl::signature` (the syntax signature, M8.1) - made the same pattern type and dropped it
(`fnPattern`, used only by `requireDeducibleAfterPack` and the literal operator's check). **Now**: `TemplateDecl::pattern`
keeps that read's function type beside `signature` (and an out-of-line member template's merge keeps its definition's),
and `Parser::instantiate` spells the Itanium name from it; a decl without one fails by name. The syntax node itself carries
spellings, not types, so "derived from the syntax signature" is met as "made by the one read that made it". Left: the
member-template door (`Templates::instantiateMemberTemplate`) still re-reads its pattern under the class's binding layer.

**Gates** (on the commit's own tree, REF 5f5f2698): identical against REF 5f5f2698 0 differ on four targets plain (1918/3836/1918/1918) and three with -templates=subst (1918/3836/1918); -g 0 differ beyond the tree path both ways, with and without the flag (linux 221, windows 214); names 959/0 and overload 46/0 on Windows; subst.sh 0 failed (self 102/102, 26/26, 3/3), run.sh 1304/0 both, typed 951/0, syntax 7/0, fold 16954 compared 0 differ (8 fewer than REF's 16962: the folds the second read made); census 1311 units 353 nonzero, covered 614, 536 no replay, without probes 296 - unchanged (no body changes; the self census not re-run); static rc 0 all (exclusions 0/0, comments 0, memory 89, residue 2012/298 not raised, ide up to date), g++ -Werror clean; Windows cl /W4 /WX clean (101 s with ship); src 95,762 -> 95,760 (-2).

**The self-host under -templates=subst** (tools/self-host on Linux, -O0, the flag on every stage, ~/m82ai-selfh): stage 0
102 of 102 sources (196 s), stage 1 built; stage 1 102 of 102 (1213 s, slots shared with the wave gate), stage 2 built;
stage 2 102 of 102 (980 s), stage 3 built: **102 of 102 sources the same object from stages 0, 1 and 2** (at 5f5f2698's
code). Stage 2 = stage 3 not measured: stage 3's compile (about 16 minutes) was stopped at the lane's hour by its own PIDs.

## M8.2: the member-template door's pattern kept from the declaration's read (lane m82aj, 2026-11-23)

**Before**: `Templates::instantiateMemberTemplate` bound the class's parameters, then the member's own to the
specialization's arguments, and re-read the signature from tokens (`readTemplateDeclaration`, the member's parameters
bound to `Kind::TemplateParam`) at every new specialization, for the Itanium name alone. **Now**: the in-class record
(ParserType.cpp, where a member template is met in its class's body - under the class's binding, which is the class
layer the door re-bound) reads that pattern once and keeps it in `TemplateDecl::pattern`, as 1c3d48e4 keeps a free
function template's; a member template declared in its class and defined at namespace scope takes the definition's read
at the merge (1c3d48e4's line). The door spells from `mt.pattern` and fails by name where none was kept. So both doors
spell the Itanium pattern from the one read at the declaration; what still reads a signature again is deduction's
trial (`deduceTemplateArguments`) and the explicit specialization's match, which ask what a signature *is* under an
argument, not its pattern. EXCLUSIONS re-pointed (22 ParserType.cpp lines, +5). src 95,813 -> 95,815 (+2).

**Gates** (on the commit's own tree, REF 52cd1c1e): identical 0 differ on four targets plain (1926/3852/1926/1926) and three
with -templates=subst (1926/3852/1926); -g 0 differ beyond the tree path both ways, with and without the flag (linux 221,
windows 214); names 963/0 and overload 46/0 on Windows; subst.sh 0 failed (run 1308/0 and typed 955/0 at -O0 and -O2 with
the switch, self 102/102, ceiling cases 17 of 17, self 26 of 26, 3 shapes of 3), run.sh 1308/0, typed 955/0, syntax 7/0,
fold 16985 compared 0 differ; **census** the same as REF's beyond the tree path, over all cases (1315 units, 353 nonzero
exits as REF, none failing on a difference; covered 614, 536 made with no replay, 78 replayed by reason) and without the
probes (296 with no replay, 78 by reason): no body changes, the suite's own bodies made 296 before and after; self census
102 sources 0 nonzero, 3847 substituted, made 3847 equal, as REF; self-compile of Templates.cpp, Typing.cpp, ParserExpr.cpp
rc 0; static rc 0 all (exclusions 0/0 after the re-point, comments 0, memory 89, residue 2012/298 not raised, ide up to
date); g++ -Werror clean; Windows cl /W4 /WX clean, first and alone (215 s with the ship).

**tests/subst.sh's two decision-ii probes, checked** (the same lane, its own commit): `template-subst` was re-pointed whole
(42 with no replay plain and `-g`, 42 beside a replay and made 42 equal under the check); `template-subst-refused` asserted
its 12 with no replay and beside a replay but not made 12 equal, and nothing held its refused shapes - all twelve bodies
are M8.2's now (the conversions, the bool argument, the literal, and `viacall`'s call), so "each refused shape
with its reason" is that none replays. **Finished**: made (12): equal under the check; for both probes, plain, `-g` and
under the check, the census line's `by-substitution=0` (the stand-in takes none) and no `replay (N): replayed:` line (no
body by reason); the case's two comment lines say all twelve. On that tree: subst.sh's census step 0 failed (535 with no
replay, 44 and 34 by reason, as before), the case's own output the same as REF's plain and `-g` on x86_64-linux and
x86_64-windows; src unchanged.

**The self-host under -templates=subst, stage 2 = stage 3** (the same lane; M8.2's close criterion): tools/self-host on
Linux at -O0 with the flag on every stage, at 52cd1c1e's code (REF, before 0f8c4df3), into ~/m82aj-selfh, one box job a
stage: stage 0 102 of 102 (153 s), stage 1 102 of 102 (1084 s), stage 2 102 of 102 (859 s), stage 3 102 of 102 (507 s,
three compiles at once, SELFHOST_JOBS=3, resumed after stage 2's job): **102 of 102 sources the same object from stages
0, 1 and 2, and 102 of 102 the same object from stages 2 and 3** - differ.txt empty. M8.2's self-host gate is met at
52cd1c1e; 0f8c4df3 (the member door's pattern) and 4ceae8dd (tests only) were not self-hosted again (their identical,
names and census rows above say no output changed). Left of M8.2's row: nothing this lane found; the deduction trial and
the explicit specialization's match still read a signature under an argument (not the Itanium pattern).

## M8.3 split into slices, the class door's census, and slice 3a: a class's layout by substitution, held against the replay's (lane m83a, 2026-11-23)

Branch `m8-3a` from `f0a710a9` (m6d-w16: main `270035bb` and m8-2aj, M8.2 closed). **What the class door is today**: every
class template specialization the program asks for is made by `Templates::makeClass` replaying the class head and body
(`readClassBody`) under the arguments, its member bodies held and replayed on use; the class door's read at the declaration
(M8.1 commit 5) lays the class out once over its own parameters, under a tag of the read alone (`C<$T0>$read3`, `readLayout_`),
and reads its member bodies into syntax. That read class is what M8.3 substitutes. **Each slice is one commit of identical
output without the flag**: identical.sh against REF `f0a710a9` 0 differ on four targets; with `-templates=subst` 0 differ;
under `-check=templates -templates=subst` each class specialization prints `-check=templates class (N): equal`, `differ: <what>`
or `replayed: <reason>`, and a `differ` fails the slice. **The oracle is the replay's class** - its layout and members are
what identical.sh, names.sh and the cases' clang `.expected` already hold - compared field by field where the slice builds.

| slice | what | covers | still replays |
| --- | --- | --- | --- |
| 3a (this) | the read class kept by the token its body is read from (`readClass_`, primary and partial), `classShape` (the census by reason), and the layout by substitution: the read class's data members, each type through `substituted` under the binding, laid as the parser lays a class of no base, vptr or bit-field, held against the replay's class - each member's name, type, offset, access, `mutable`, `volatile`; the size; the alignment | a primary or partial over type parameters bound to scalars, whose read class has data members alone of types that are no class (scalars, pointers, references, arrays of them), with no base, dependent base, vptr, static data member, bit-field or anonymous union; member functions, typedefs and nested types beside them are not looked at | the class itself: the replay still makes the program's class, its members and specials (3a builds and compares, nothing it builds is used) |
| 3b | the class made from the read in place of the replay for 3a's shape: the `Type` under its real tag from the substituted members, the member functions' declarations entered under that tag with their signatures substituted, the specials as `Classes` decides them, the held bodies owed from the read's member bodies (typed under the `Binding` by M8.2's function path, on use, [temp.inst]/2); held against a replay run beside it under the check: the members, the functions' symbols and signatures, the specials' triviality | 3a's shape with no replay | the rest |
| 3c | a type parameter bound to a class, and data members of class type: Itanium's empty subobjects and tail padding (`Conflict`, `dataSize`, `podForLayout`) from the substituted types; static data members on use ([temp.inst]/3) | the largest bucket (635 in the cases, 51,815 in the self-host) | bases, vptrs, non-type parameters |
| 3d | bases - named and named through a parameter - and virtual functions: the vtable, vptr sites and thunks through `Classes` as today, from the substituted bases; the check compares the slots and the base offsets | 81 + 40 + 5 in the cases | non-type parameters, packs |
| 3e | non-type parameters by value (the `Binding`'s values: an array bound, a static member's initialiser), alias templates (the type-id substituted), the injected class name, nested classes of a specialization | 458 in the cases | packs and member templates (M8.4) |

**The census of the class door at 3a** (`-check=templates -templates=subst -S`, Linux, NEW at this commit; every class
specialization `makeClass` makes, summed): **`tests/cases`** (1,315 units, 353 nonzero exits as REF's): 2,184 class
specializations - **equal 613** (603 without the `template-subst-*` probes), **differ 0**; replayed by reason - a type
parameter bound to a class 635, a non-type parameter 458, a static data member 183, a pack or template parameter 109, a base
81, a base named through a parameter (the read lays it out as nothing) 40, a data member of class type 27, a type parameter
bound to a type that is no scalar, class, reference or array (an enumeration, a member pointer) 24, a virtual function 5, bound
to a reference 4, to an array 2, a data member of an incomplete type 1, of a type the binding does not carry 1, a bit-field or
an anonymous union 1. **The self-host** (102 sources, 0 nonzero): 71,314 class specializations - **equal 15,992, differ 0**;
bound to a class 51,815, a base 2,228, a pack or template parameter 401, a non-type parameter 259, a static data member 216, a
base through a parameter 215, a data member of class type 63, bound to a reference 51, to another type 49, a data member of a
type the binding does not carry 24, bound to an array 1. So 3a's shape is 28% of the class door over the cases and 22% over
the compiler's own source; a type parameter bound to a class is 3c's, and the self-host's largest (73%). The first build of
this slice (n1) found 40 cases and 211 self-host classes laid apart where the replay's class had a base the read had laid out
as nothing (a base named through a parameter, `dependentBase`): those are refused by name now, before the compare.

**src/**: `Templates.h` +5 (`readClass_`, `classCensus_`, `classShape`, `classLaid`), `Templates.cpp` +71 (the two functions,
the read kept in `readDeclaration` under the flag alone, the census in `makeClass`, the report line): src/ 95,815 -> 95,891.
Without `-templates=subst` nothing it adds is reached; with it and without `-check=templates` it adds a count only.

**Gates** (on the commit's own tree, Linux, REF `f0a710a9` built in its own tree): identical 0 differ on four targets plain
(x86_64-linux 1926, x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) -
3a changes no output with the flag either, the replay's class being the program's; subst.sh 0 failed (self 102 of 102,
ceiling 26 of 26, 3 shapes of 3), then run.sh 1308/0, then run.sh with CXX1 carrying `-templates=subst` 1308/0 (952 printed
their recorded output, 352 refused as recorded; no class falls back, since none leaves replay); `-check=templates
-templates=subst` over the cases: 1315 units, 353 nonzero exits, the same set as REF's, every unit's report the same as REF's
beyond the tree path and the new `class` lines, class `differ` 0; over the self-host 102 sources, 0 nonzero, class `differ` 0
(built 3849 substituted, made 3849 equal, as before); self-compile of Templates.cpp by NEW rc 0; static: exclusions 0/0 after
three Templates.cpp citations re-pointed (+3), comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to
date; g++ `-Werror` clean; Windows cl /W4 /WX clean, first and alone (about 8 minutes with the ship, the box shared and loaded),
then names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed with that build (clang 19 on the box).

**Where it stopped**: 3a is the census and the compare; 3b - the class made from the read for 3a's shape, the replay not run -
is the next commit, and its first question is the member functions: the read's declarations live in the read's lookup layer
and go with it (`Lookup::Layer` in `readDeclaration`), so 3b keeps each member's read signature beside `readClass_` before
the layer pops, as 1c3d48e4 kept a function template's pattern.

## M8.3 slice 3b, first part: the read class's member functions kept past the read, and held against the replay's (lane m83b, 2026-11-23)

Branch `m8-3b` from `f88b3dd3` (m83a). **What it does**: the class door's read (`readDeclaration`, under `-templates=subst` alone)
keeps, beside `readClass_` and before the read's `Lookup::Layer` pops, every function the read declared under the read class's tag -
the written member functions and the special members `Classes` declared at the class's end (a trivial one is no function and so
is not among them) - with that tag (`readMembers_`). `classShape` asks one thing more: each kept function's return and parameter
types, through `substituted` under the binding, must name no type the binding does not carry (a dependent member type, or a class
the read named over its own parameters - `initializer_list<$T0>`, `pair<const $T0,$T1>`, which is no specialization once the read
ends and its arguments are taken back). Under `-check=templates`, a class whose layout `classLaid` holds equal is held member by
member against the replay's (`classMembers`): the functions the replay declared under the real tag, in order - the count; each
name with the read's tag spelled as the real one; the return and parameter types under the binding, the read class standing for
the replay's; variadic, const `this`, access, virtual, `explicit`, static, `noexcept`, defined in the class; implicit or written.
The replay still makes the program's class; nothing here is used beyond the census line.

**Census** (`-check=templates -templates=subst -S`, Linux, at this commit): **`tests/cases`** (1,315 units, 353 nonzero exits, the
set REF's): 2,184 class specializations - **equal 406** (396 without the `template-subst-*` probes; layout and member functions),
**differ 0**; the 207 that 3a held equal and 3b does not are `replayed: a member function's signature over a type the binding does
not carry` (an `initializer_list<T>` constructor, a `const pair<const K,V> &` parameter, an iterator's `typename X<T>::reference`);
the other reasons as 3a's. **The self-host** (102 sources, 0 nonzero): 71,314 - **equal 3,868, differ 0**; over a type the binding
does not carry 12,131; the rest as 3a's (bound to a class 52,201, a base 2,252, ...). The first build (n1) found 207 cases and the
self-host's 1,378 apart on a parameter's or return type; each was such a type, refused by name now before the compare.

**src/**: `Templates.h` +3 (`readMembers_`, `classMembers`), `Templates.cpp` +53 (the kept functions in `readDeclaration`, the
signature test in `classShape`, `classMembers`, its call in `makeClass`): src/ 95,891 -> 95,947 (+56). EXCLUSIONS re-pointed (3
Templates.cpp lines, +9).

**Where it stopped** (the hour): step (2), the class made from the read with no replay for this shape - the `Type` under its real
tag, the kept functions entered under it with their signatures substituted and their symbols spelled for it, the specials as
`Classes` declares them, the held bodies owed through M8.2's function path - is not begun; the census above is the set it would
cover (406 in the cases, 3,868 in the self-host), and `classMembers` is the check it would be held by.

**Gates** (on the commit's own tree, Linux, REF `f88b3dd3` built in its own tree): identical 0 differ on four targets plain
(x86_64-linux 1926, x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) - no
output changes with the flag either, the replay's class being the program's; subst.sh 0 failed (self 102 of 102, ceiling 26 of 26,
3 shapes of 3), then run.sh 1308/0, then run.sh with CXX1 carrying `-templates=subst` 1308/0 (952 printed, 352 refused as
recorded; no class falls back, none leaving replay); `-check=templates -templates=subst` over the cases: 1315 units, 353 nonzero
exits, every unit's report REF's beyond the tree path and the `class` lines; class `differ` 0 in the cases and the self-host (built
3852 substituted, made 3852 equal); self-compile of Templates.cpp by NEW rc 0; static: exclusions 0/0 after the three citations
re-pointed, comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date; g++ `-Werror` clean; Windows cl
/W4 /WX clean, first and alone (95 s with the ship).

## M8.3 slice 3b, second part: each member function's Itanium name spelled for the real tag from the read's signature (lane m83b, 2026-11-23)

The second thing the class made without a replay needs is its member functions' symbols, and the read's are spelled for the read's
tag (`Box<$T0>$read3`, or `$pattern$...`). Under `-check=templates`, `classMembers` now spells each kept function's name for the
real tag from its signature under the binding - a constructor by `itaniumConstructorName`, a destructor by `itaniumDestructorName`,
every other member (operators, static members, the implicit assignments) by `Linkage::memberSymbol` as the parser's member
declaration calls it - and holds it against the replay's symbol; `refThis` joins the qualifiers compared. The Microsoft spelling is
not compared: `memberSymbol` reads the declarator's written parameters there (`manglingType`), which a class made from the read
does not have - that is the first thing step (2) owes on x86_64-windows. **Census**: cases equal 406, differ 0; self-host equal
3,868, differ 0 - every covered class's member names agree with the replay's, specials included. **src/**: `Templates.cpp` +10:
95,947 -> 95,957.

**Gates** (this commit's tree, Linux, REF `f88b3dd3`): identical 0 differ on four targets plain (1926/3852/1926/1926) and three
with `-templates=subst` (1926/3852/1926); subst.sh 0 failed, then run.sh 1308/0, then run.sh under the flag 1308/0; self-compile
of Templates.cpp rc 0; static rc 0 (exclusions 0/0, comments 0, memory 89, residue 2012/298 not raised, ide up to date); g++
`-Werror` clean; Windows cl /W4 /WX clean, first and alone (91 s with the ship). names.sh 963/0 and overload.sh 46/0 (Windows,
clang 19) were run on the first part's build; this part changes only what `-check=templates` prints.

**Where it stopped**: step (2) proper - the class made from the read with no replay (the `Type` under its real tag from
`classLaid`'s layout, the kept functions entered under it with the names above, `Classes::declareImplicitSpecials` over the made
type, the held member bodies owed through M8.2's function path on use) - is not begun; the 406 / 3,868 are its cover and
`classLaid` + `classMembers` its check.

**The second part's Windows rows**, added after its commit: names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed
with the cl build of `902fdc62`'s src (clang 19 on the box).

## M8.3 slice 3b, third part: each member function's Microsoft name spelled for the real tag from the read's signature (lane m83c, 2026-11-23)

Branch `m8-3c` from `76ca4f46` (m83b). **What it does**: the Microsoft spelling of a member reads the parameters as written
(`Linkage::manglingType` - a `const T` or an array parameter keeps what [dcl.fct]/5 drops), which a read's signature did not keep
once its declarator was gone. A member function's `Signature` now keeps them where they differ (`written`, set where the parser
declares a constructor, a static member function and a member function), and under `-check=templates` `classMembers` spells each
kept function's Microsoft name for the real tag as the parser and `Classes` do: the written parameters under the binding standing
where `manglingType` reads them; a constructor by `microsoftConstructorName`, a destructor by `microsoftDestructorName`, Classes'
implicit `operator=` by `microsoftCopyAssignName` and its implicit specials as public; every other member by
`Linkage::memberSymbol`. So a covered class's members are held on both ABIs' symbols. Nothing the program sees changes:
`written` is read only by the check.

**Census** (`-check=templates -templates=subst -S`, Linux, NEW at this commit): **`tests/cases` under `-arch x86_64-windows`**
(1,315 units, 377 nonzero exits, the set REF's under the same flags, every unit's report REF's beyond the `class` lines): 2,184 class specializations - **equal 406** (396 without the `template-subst-*`
probes; layout, member functions and their Microsoft symbols), **differ 0**; the reasons as m83b's. **The self-host under `-arch x86_64-windows`** (102 sources, 0 nonzero): **equal 3,864, differ 0** (3,868 on x86_64-linux; the target's headers differ); built 3,848 substituted, made 3,848 equal. Over the cases on x86_64-linux (353 nonzero, as REF's): equal 406, differ 0, as m83b's.

**src/**: `Entities.h` +2 (`Signature::written`), `ParserClass.cpp` +3 (it set at the three member declarations), `Templates.cpp`
+14 (`classMembers`' Microsoft spelling): src/ 95,957 -> 95,976 (+19). EXCLUSIONS re-pointed (3 ParserClass.cpp lines).

**Gates** (this commit's tree, Linux, REF `76ca4f46` built in its own tree): identical 0 differ on four targets plain (x86_64-linux 1926, x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) - no output changes with the flag either, the replay's class being the program's; subst.sh 0 failed (self 102 of 102, ceiling 26 of 26, 3 shapes of 3), then run.sh 1308/0, then run.sh with CXX1 carrying `-templates=subst` 1308/0 (952 printed, 352 refused as recorded; no class falls back, none leaving replay); `-check=templates -templates=subst` class `differ` 0 over the cases on both x86_64 targets and the self-host under x86_64-windows; self-compile of Templates.cpp and ParserClass.cpp by NEW rc 0, plain and under the flag; static rc 0 (exclusions 0/0 after the three citations re-pointed, comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date); g++ `-Werror` clean; Windows cl /W4 /WX clean, first and alone (218 s with the ship), then names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed with that build (clang 19 on the box).

**Where it stopped**: step (2) - the class made from the read with no replay for the classes the checks hold (the `Type` under
its real tag from `classLaid`'s layout, the kept functions entered under it with the names `classMembers` now spells on both
ABIs, `Classes`' specials over the made type, the held member bodies owed through M8.2's function path on use, and the replay run
beside it only under `-check=templates`) - is not begun: the hour went to this part and its gates. The 406 classes in the cases
and 3,868 in the self-host (x86_64-linux) are its cover. What it meets first: M8.2's function path (`instantiate` in
`instantiatePending`) is taken for a specialization that is no class alone; a class specialization's held member bodies are replayed
by `replayInlineBodies(now)` beside it, so a class made with no replay owes its member bodies that path extended to a member (the
made class's `this`, the read tag's names respelled for the real one) before the class itself can skip its replay. The self-host
under `-templates=subst` was not run: this part changes nothing the program sees, and its three stages pass the hour.

## M8.3 slice 3b, step 2's first part: a covered class's held member bodies typed under its binding beside the replay (lane m83d, 2026-11-23)

Branch `m8-3d` from `8fda8eb6` (m83c). **What it does**: under `-templates=subst`, the class door's read keeps each held member body's
read by the token it is read from (`readBody_`, as the function door's); `makeClass` keeps, for a class the checks cover (`classShape`
empty, and `classLaid` + `classMembers` equal under `-check=templates`), its binding with the read class standing for the replay's
(`coveredClass_`). In `instantiatePending`, each held member body a class specialization owes now is asked `memberShape`: its read;
no friend; an overload of its own (`PendingBody::which`); no `this`, no data member, static member or member function of its own
class (the made class owes those: `this` and the members through it); then M8.2's `substShape` under the class's binding, and no
deduced call (the class's binding as the outer layer of a request is not built). A body that passes is registered in `bound_` under
its member's symbol, and `Typer::typeBody` - M8.2's function path - types the read under the binding beside the replay's body and
lowers the read where `typedApart` holds them equal, the replay's where not, by name. The replay still runs for every member body
(`replayInlineBodies(now)`), lays the frame and makes the function; nothing is made from the read with no replay yet.

**Census** (`-check=templates -templates=subst -S`, Linux, NEW built from this commit's src but for one comment's place in `Templates.h`). **`tests/cases`** (1,315 units, 353 nonzero exits as
m83c's): class equal 406 (396 without the `template-subst-*` probes), differ 0, the reasons as m83c's; **member bodies owed** 4,211 -
**covered 52, typed beside the replay and lowered from the read** (`built`: 666 substituted = 614 function specializations + 52 member
bodies, 0 replayed apart), made with no replay 0; replayed by reason - its class replayed 3,911, a data member of its own class 105,
defined out of line 72, a `meminit` in the body 44, no body read at the declaration 16, a call through a value, a member or a class 3,
a bound class's member other than public 2, a `sizeof` 2, `this` written 2, a dependent-name call another scope declares 1, a
`literal` 1. **The self-host** (102 sources, 0 nonzero): class equal 3,868, differ 0, the reasons as m83b's; member bodies owed 57,920 - **covered 164**, of which 72 reach the typed tree and type equal beside the replay (`built`: 3,925 substituted = 3,853 function specializations + 72; the other 92 are bodies the unit does not emit, which the Typer never asks for), made with no replay 0; replayed by reason - its class replayed 57,570, a data member of its own class 173, a `meminit` 13. Functions as before: frame 3,853 equal, made 3,853 equal.

**src/**: `Templates.h` +3 (`coveredClass_`, `memberCensus_`, `memberShape`), `Templates.cpp` +47 (the read kept in `tallyRead`, the
binding kept in `makeClass`, the census and registration in `instantiatePending`, `memberShape`, the report line): src/ 95,976 ->
96,026 (+50). EXCLUSIONS re-pointed (3 Templates.cpp lines).

**Gates** (on the commit's own tree, Linux, REF `8fda8eb6` built in its own tree): identical 0 differ on four targets plain (x86_64-linux 1926, x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) - the covered shape: a held member body of a class the checks hold, reaching no `this` and no member of its own class, lowered from its read; subst.sh 0 failed (self 102 of 102, ceiling 26 of 26, 3 shapes of 3), then run.sh 1308/0, then run.sh with CXX1 carrying `-templates=subst` 1308/0 (952 printed, 352 refused as recorded; no class falls back, none leaving replay); `-check=templates -templates=subst` class `differ` 0 and member bodies typed apart 0 over the cases and the self-host, the cases' nonzero set m83c's; self-compile of Templates.cpp by NEW rc 0, plain and under the flag; static rc 0 (exclusions 0/0 after the three citations re-pointed, comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date); g++ `-Werror` clean; Windows cl /W4 /WX clean, first and alone (141 s with the ship; 94 s for the final delta), then names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed with that build (clang 19 on the box).

**Where it stopped**: step (1) proper - a member body made from the read with no replay (`madeWithoutReplay` for a member: the
frame from the read's steps, which the class door's read does not log yet; the shell with `this`; `madeApart` against the replay's
function under `-check=templates`) - and the bodies that reach `this` or their own class's members (105 + 2 + the member calls),
which need the Typer to take the read class's records (`owner`, a member's offset in `serial`) for the made class's; step (2), the
class made from the read with no replay, waits on both.

## M8.3 slice 3b, step 2's second part: a covered class's member bodies reaching `this` and their own class's members (lane m83e, 2026-11-23)

Branch `m8-3e` from `b73242c9` (m83d). **What it does**: `memberShape` no longer replays a held member body for writing `this`, a
data member of its own class or a member function of its own class (it still replays one naming a static member of its own class);
such a body goes through M8.2's `substShape`, which now takes a `this` node (its `*this` still an object of a class it
refuses operators over), and is typed beside the replay as m83d's are. **One map from the read class to the class
the program uses**: the covered class's binding already has the read class standing for the replay's (`coveredClass_`); under a
binding `Typer::record` now takes a record's `owner` through it (`bound`), its `symbol` through `readSymbol_` - each read member
function's symbol to the replay's, which `classMembers` fills as it matches them (it now runs for every covered class, its verdict
still held only under `-check=templates`) - and a data member's offset (`serial`) as the program's class lays it (`offsetUnder`, by the
member's name). `this` is the read's record under the binding: a pointer to the class, `const` in a const member function.
`recordDiffers` asks the same of the typed tree. Nothing the program sees changes: the replay still runs for every member body,
and the read is lowered only where `typedApart` holds it equal to the replay's.

**Census** (`-check=templates -templates=subst -S`, Linux, NEW at this commit's src). **`tests/cases`** (1,315 units, 353 nonzero
exits, REF's set): class equal 406, differ 0; **member bodies owed 4,211 - covered 149 (147 without the `template-subst-*` probes;
m83d 52)**, made with no replay 0, replayed beside: `built` 752 substituted = 614 function specializations + 138 member bodies
(m83d 52), 5 replayed apart - 4 in `template-nested` (`outer`'s and `deeper`'s bodies reaching `held_.n_`, a member of
`inner<$T0>`, a class the read named over its own parameter, which the map does not carry: its offset is the read's) and 1 in
`regex-ecmascript` (a `conditional` typed apart); replayed by reason - its class replayed 3,911, defined out of line 72, a `meminit`
44, no body read at the declaration 16, a `delete` 6, a call through a value, a member or a class 4, a bound class's member other
than public 2, a `sizeof` 2, and five of one each (an operator over a class's object, a `literal`, a `conditional` over a type the binding
does not carry, two calls the replay's lookup answers); `this` written replays none now (`substShape` takes a `this` node). **The self-host** (102 sources at this commit's src, 0 nonzero): class equal 3,868, differ 0; member bodies owed 57,941 - **covered 337
(m83d 164)**, of which 123 reach the typed tree and type equal beside the replay (`built`: 3,977 substituted = 3,854 function
specializations + 123; m83d 72), 0 apart, made with no replay 0; replayed by reason - its class replayed 57,591, a `meminit` 13.
Functions: frame 3,854 equal, made 3,854 equal (one more function specialization than m83d's: the sources grew by this commit).

**src/**: `Templates.h` +4 (`classMembers`' out-parameter, `readSymbol_`, `symbolUnder`, `offsetUnder`), `Templates.cpp` +11 (the map
filled in `makeClass` and `classMembers`, `memberShape`'s three refusals gone, `symbolUnder`, `offsetUnder`, `substShape`'s `this`),
`Typing.cpp` +7 (`record` and `recordDiffers` under a binding): src/ 96,026 -> 96,048 (+22). EXCLUSIONS unchanged (0 stale).

**Gates** (on the commit's own tree, Linux, REF `b73242c9` built in its own tree): identical 0 differ on four targets plain
(x86_64-linux 1926, x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) - the
covered shapes: m83d's, and a held member body of a covered class reaching `this`, `this->m`, its own data members and its own member
functions, lowered from its read where it types equal; subst.sh 0 failed (self 102 of 102, ceiling 26 of 26, 3 shapes of 3), then run.sh
1308/0, then run.sh with CXX1 carrying `-templates=subst` 1308/0 (952 printed, 352 refused as recorded; none falls back, none fails);
`-check=templates -templates=subst` class `differ` 0 over the cases and the self-host, member bodies typed apart 5 over the cases
(each replayed by name, above) and 0 over the self-host, no unit failing on a difference, the cases' nonzero set 353 as REF's;
self-compile of Templates.cpp and Typing.cpp by NEW rc 0, plain and under the flag; static rc 0 (exclusions 0/0, comments 0 over,
memory gate 89 held, ast-residue 2012/298 not raised, ide up to date); g++ `-Werror` clean; Windows cl /W4 /WX clean, first and
alone (212 s with the full ship; 492 s for the final delta, the box shared), then names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed with that
build (clang 19 on the box).

**Where it stopped**: step (2) - the class door's read logs no frame steps for member bodies (`p_.frameLog_` is set only at the
function door, Templates.cpp's `readFrames_[decl.afterParams]`), so neither `layFrame`/`shellApart` nor `madeFromRead` can make a
member body with no replay; `madeFromRead` takes a `Specialization` and wants a member's shell with `this` as the hidden first
parameter. A member of a class the read named over its own parameter (`inner<$T0>` inside `outer<K>`) wants that class's
specialization in the map as well.

## M8.3 slice 3b, step 3's first part: a covered member body's frame steps logged at the class door, and its frame, shell and made function held against the replay's (lane m83f, 2026-11-23)

**What landed**: the class door's read logs each held member body's frame steps, by the body's start, as the function door logs a
function template's; the replay logs a covered member body's steps beside it; `layFrame`, `shellApart` and `madeApart` hold the
member's frame, its shell (`this` first, then the parameters, the locals, -g's block table, the sret slot, the save area, the frame's
size) and its function's records against the replay's. Nothing the program sees changes: the replay still makes every member body.

- **One place that logs a member body's steps**: `Parser::bodyFrames_`, a map by body start, read by `replayInlineBodies` for each
  body it holds a key for - inside its own `clearFunctionState`/`restoreFunctionState`, so neither the read nor the replay logs the
  `p`/`q` of the batch's frame (m83f's first build set `frameLog_` around the read's call and every member frame was apart on that).
  The class door points it at `readFrames_` for the one body it reads; `instantiatePending` at a map of the covered bodies of the
  batch it replays. A replay nested in a body logs into the body's steps as before, and opens no log of its own.
- **`keepRead`**: the function a read made, kept by its start (`readFns_`, `readFnAt_`) - the function door's copy moved into it,
  and the class door's held bodies now kept beside their steps.
- **`memberFrame`**: the frame laid from the read from the replay's own `z` alignment; the shell from `madeFromRead` under the covered
  class's binding (a Specialization of the body's start and its member's symbol); `this` goes first among the parameters, as the
  definition path inserts it after reading them; a member's display name is under its class's whole name, as the parser sets it.

**Census** (`-check=templates -templates=subst`, the cases, x86_64-linux): class equal 406 differ 0, nonzero 353 (REF's set); member
bodies covered 149 (147 without the probes), built 752 = 614 + 138, replayed by reason as m83e; member frames 148 equal, 1 apart
("a local: typed apart", nested-class-of-class-template - a local of a nested class's type); shells 148 equal of 148; made 144 equal,
4 apart ("its name, symbol or alias", template-class 2 and template-nested 2); member bodies made with no replay 0, beside a replay
149, replayed 4,062 by reason. Self-host: nonzero 0, class equal 3,868 differ 0, member covered 338, frames 338 equal, shells 338
equal, made 338 equal; built 3,979.

**src/**: `Parser.h` +1 (`bodyFrames_`), `Parser.cpp` +9 (`replayInlineBodies` logs a body it holds a key for), `Templates.h` +2,
`Templates.cpp` +42 net (`keepRead`, `memberFrame`, the class door's log, `instantiatePending`'s map, `this` first in
`madeFromRead`): src/ 96,048 -> 96,102 (+54). EXCLUSIONS re-pointed (Parser.cpp 1 line, Templates.cpp 3 lines).

**Gates** (on the commit's own tree, Linux, REF `48579d6c` built in its own tree): identical 0 differ on four targets plain
(x86_64-linux 1926, x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) - the
covered shapes: m83e's (a covered class's held member bodies, `this`, its own members), now with their frames and shells logged and
held; subst.sh 0 failed (self 102 of 102, ceiling 26 of 26, 3 shapes of 3), then run.sh 1308/0, then run.sh with CXX1 carrying
`-templates=subst` 1308/0 (952 printed, 352 refused as recorded; none falls back, none fails); `-check=templates -templates=subst`
class `differ` 0 over the cases and the self-host, no unit failing on a difference, the cases' nonzero set 353 as REF's, the
self-host's 0; self-compile of Templates.cpp and Parser.cpp by NEW rc 0, plain and under the flag; static rc 0 (exclusions 0/0,
comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date); g++ `-Werror` clean; Windows cl /W4 /WX
clean, first and alone (92 s full, 94 s for the final delta), then names.sh 963 passed 0 failed and overload.sh 46 agreed 0
differed with that build (clang 19 on the box).


**Where it stopped**: step (2), a covered member body made with no replay. What it needs, found here: (a) made inside
`replayInlineBodies`' batch, at the body's place (a hook in place of its `topLevel`), since each body's frame starts where the last
one left `frameAlign_`; (b) `madeWithoutReplay`'s side effects without `discardableInline_` (a specialization's member is not
discardable) and with the display name above; (c) constructors and destructors left to the replay (the 4 "name, symbol or alias"
apart are there: the C1/C2 alias and the read's class name); (d) a refusal by name, before the body is trusted, of the 5 bodies
typed apart beside the replay (4 reaching a member of `inner<$T0>` in template-nested, 1 conditional) and of the frame apart above -
the class the read named over its own parameter in `Typer::record`'s map is what (d)'s four want, step (3).

## M8.3 slice 3b, step 2's named refusals: every covered member body the checks held apart now replays by name (lane m83g, 2026-11-23)

**What landed**: `memberShape` refuses by name, at parse time, each member body m83e and m83f found apart beside the replay, so
none of them is covered and the census shows 0 apart: a member or a local of a class the binding's map does not carry
(`overRead`: an unmapped specialization or a class nested in one - `Tree<$T0>::It`, nested-class-of-class-template's frame
apart), a data member named alone at a read offset other than its class's (template-nested's 4: `n_++` after `T last_` in
`inner<T>`, whose 'name' record keeps the read's offset where the record has no owner - `this->n_` is mapped already), and a
`conditional` (regex-ecmascript's `sub_match::length`, typed apart). Nothing the program sees changes.

**Census** (`-check=templates -templates=subst`, the cases, x86_64-linux): class equal 406 differ 0, nonzero 353 (REF's set);
member bodies covered 143 (m83f 149), the six apart now replayed by name - a local of a class the read named over its parameter
5, a `conditional` 5, a data member named alone at another offset 4, a member of a class the read named over its parameter 1
(the reasons overlap the earlier ones they now come before: `meminit` 44 -> 38); member frames 143 equal of 143, shells 143
equal, made 141 equal and 2 "its name, symbol or alias" (constructors and destructors: step 2 replays them); built 751
substituted, **0 apart** (m83f 5); made with no replay 0. Self-host: nonzero 0, class equal 3,868 differ 0, member covered 338 (all
338 m83f's), frames, shells and made 338 equal, built 3,980 substituted, 0 apart (by the step 2 build: one memberShape).

**src/**: `Templates.h` +1 (`overRead`), `Templates.cpp` +15 (`overRead`, four refusals in `memberShape`): src/ 96,102 -> 96,118
(+16). EXCLUSIONS unchanged (0/0).

**Gates** (the commit's own tree, Linux, REF `99ead8d2` built in its own tree): subst.sh 0 failed (self 102 of 102, ceiling 26 of 26, 3
shapes of 3), then run.sh 1308/0, then run.sh with CXX1 carrying `-templates=subst` 1308/0 (952 printed, 352 refused as
recorded; none falls back, none fails); identical 0 differ plain on x86_64-linux (1926) and x86_64-windows (3852) at the commit,
the rest of its runs still going (the step 2 entry has them; that tree, a superset, is 0 differ on four targets plain and three
with the flag); `-check=templates` class `differ` 0 and no unit failing on a difference over the cases (nonzero 353, REF's) and
the self-host (nonzero 0, by the step 2 build, whose memberShape is this one); static rc 0 (exclusions 0/0, comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide
up to date); self-compile of Templates.cpp rc 0 plain and under the flag; g++ `-Werror` clean; Windows cl /W4 /WX clean, first and
alone (219 s full, 209 s delta), then names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed (clang 19 on the box) -
on the tree before the last refusal (a data member named alone), which the step 2 build's cl /W4 /WX holds clean (113 s delta).

## M8.3 slice 3b, step 2: a covered member body made from its read with no replay under -templates=subst (lane m83g, 2026-11-23)

**What landed**: under `-templates=subst` without `-check=templates`, each covered member body (`memberShape`'s "covered") is
made from its read in place of its replay: `instantiatePending` keeps it in `memberMade_` (by its start, with its member's
symbol), and `replayInlineBodies` asks `Templates::madeMember` in place of the body's `topLevel`, so each frame starts where the
last one left `frameAlign_`. `madeMember` replays a constructor or destructor by name (their C1/C2 aliases are the replay's) and
otherwise calls M8.2's `madeWithoutReplay` with the member: the frame laid from the read, `madeFromRead`'s shell with the display
name under the class's whole name, no `discardableInline_` (a specialization's member is not discardable), `defined`, the frame's
alignment, `unreplayed_` (the read is the body's root the driver lowers) and the callees used - `callsUsed` now reads a read
member's symbol through `symbolUnder`, so a call to the class's own member function marks the program's. Under
`-check=templates` the replay runs beside it as before (decision i), with memberFrame's frame, shell and made checks.

**Census** - the cases with no check (`CXX1_TEMPLATE_CENSUS=1 -templates=subst`, x86_64-linux): member bodies **made with no
replay 141** (139 without the probes), beside a replay 0, replayed 2 by name (a constructor or destructor) and the rest as
`memberShape` replays them (its class replayed 3,911, out of line 72, ...); functions 536 with no replay as M8.2. With `-check=templates`:
member bodies covered 143, beside a replay 143, frames 143 equal, shells 143 equal, made 141 equal and 2 apart (the constructor
and destructor, replayed); built 751 substituted, 0 apart. Self-host (`-check=templates -templates=subst`): nonzero 0, class equal 3,868, member covered 338, beside a replay 338, frames, shells
and made 338 equal, built 3,980 substituted, 0 apart.

**src/**: `Parser.cpp` 0 net (the hook in place of `topLevel`), `Templates.h` +2, `Templates.cpp` +25 (`madeMember`,
`madeWithoutReplay`'s member, `instantiatePending`'s `memberMade_`, `callsUsed` through `symbolUnder`): src/ 96,118 -> 96,145
(+27). EXCLUSIONS re-pointed (3 Templates.cpp lines).

**Gates** (the commit's own tree, Linux, REF `99ead8d2`): identical 0 differ on four targets plain (x86_64-linux 1926,
x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) - the covered shapes:
a covered class's held member bodies other than its constructors and destructors (m83d's, m83e's `this` and own members), now
made from the read with no replay against REF's replay, byte for byte; subst.sh 0 failed (self 102 of 102, ceiling 26 of 26, 3
shapes of 3), then run.sh 1308/0, then run.sh with CXX1 carrying `-templates=subst` 1308/0 (952 printed, 352 refused as
recorded; none falls back, none fails); `-check=templates -templates=subst` class `differ` 0 and no unit failing on a difference
over the cases (nonzero 353, REF's) and the self-host (nonzero 0); self-compile of Templates.cpp and Parser.cpp rc 0 plain and
under the flag; static rc 0 (exclusions 0/0 after re-pointing, comments 0 over, memory gate 89 held, ast-residue 2012/298 not
raised, ide up to date); g++ `-Werror` clean; Windows cl /W4 /WX clean (113 s delta over the step 1 build); names.sh 963/0 and
overload.sh 46/0 were run on the step 1 build, not again on this one. Step 1's own identical runs, finished after its commit:
0 differ on four targets plain (1926/3852/1926/1926) and three with `-templates=subst` (1926/3852/1926).

**What is left**: step (3), `inner<$T0>` - a class the read made apart - in `Typer::record`'s map, and a 'name' record with no
owner taken through the covered class's binding, so the 6 refused here are covered again; constructors and destructors made
from the read (their C1/C2 aliases); out-of-line members and `meminit` bodies (72 and 38 over the cases).

## M8.3 slice 3b, step 3: a data member named alone through the covered class's binding (lane m83h, 2026-11-23)

**What landed**: `Templates::offsetUnder` takes a data member's record with no owner - a member named without `this->` - as its
own class's: the read class, last in a covered class's map (`s.from.back()`, only where that is a class: a free function's map ends
in a parameter, and its member records keep their offsets - `template-subst-member` held `t.c` apart the first time this read a
parameter as the class). m83g's named refusal ("a data member named alone, which the read lays at another offset") is lifted, and
template-nested's 4 (`n_++` after `T last_` in `inner<T>`) are covered again, held equal. `readStands` builds the read class's place
in the covered class's map (`coveredClass_` and `classMembers`'s copy) and, before it, the class the program declared under the
read's own tag where that is another Type (`inner<$T0>`, made inside `outer`'s read): over the cases it covers no body more - the
local of `Tree<$T0>::It` (nested-class-of-class-template) and regex-ecmascript's member over its read still replay by name, owed.

**Census** (x86_64-linux): cases with `-check=templates -templates=subst`: class equal 406 differ 0, nonzero 353 (REF's); member
bodies covered **147** (m83g 143; 145 without the probes), frames 147 equal, shells 147 equal, made 143 equal and 4 "its name,
symbol or alias" (the constructors and destructors, which this step still replays); built 755 substituted, 0 apart. With no check:
member bodies made with no replay 143 (141 without the probes), beside 0, replayed 4 by name (a constructor or destructor) and the
rest as `memberShape` replays them (its class replayed 3,911, out of line 72, `meminit` 38, ...).

**src/**: `Templates.h` +1, `Templates.cpp` +11 (`readStands`, `offsetUnder`'s owner, one refusal out): src/ 96,145 -> 96,157
(+12). EXCLUSIONS unchanged (0/0).

**Gates** (the commit's own tree, Linux, REF `0fbd2fd3` built in its own tree): identical 0 differ on four targets plain
(x86_64-linux 1926, x86_64-windows 3852, arm64-darwin 1926, tms6747 1926) and three with `-templates=subst` (1926/3852/1926) - the
covered shapes: m83g's covered member bodies, now with template-nested's 4 (a data member named alone in a class read after a
`T` member) against REF's replay; subst.sh 0 failed (its run.sh under the flag at -O0 and -O2 1308/0, typed.sh 955/0; self 102 of
102, ceiling 26 of 26, 3 shapes of 3); the plain run.sh and a separate run.sh under the flag were cut for the hour (identical holds
the plain output to REF's); `-check=templates` class `differ` 0, 0 apart, no unit failing on a difference over the cases (nonzero
353, REF's) and the self-host (nonzero 0, by the step 2 build); self-compile of Templates.cpp and Typing.cpp rc 0 plain and under
the flag; static rc 0 (exclusions 0/0, comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date);
g++ `-Werror` clean; Windows cl /W4 /WX clean (212 s full), names.sh 963/0 and overload.sh 46/0 on the tree before
`offsetUnder`'s class test (one line, which the step 2 tree's cl /W4 /WX build, names.sh and overload.sh hold clean).

## M8.3 slice 3b, step 3: a covered class's constructors and destructors made from the read with no replay (lane m83h, 2026-11-23)

**What landed**: `madeMember` no longer replays a covered class's constructor or destructor by name: it is made from its read
like any held member body. A covered class has no base, no vptr and no data member of class type, and a body with a `meminit`
replays (38), so its member construction and destruction are none, and what is left is the replay's naming: `memberNamed`
(called by both `madeWithoutReplay` and `memberFrame`, so the check holds what the program gets) gives the display name under the
class's whole name and, on Itanium, the C2 or D2 alias as the replay's definition path names it (`constructorC2_`, else
`itaniumConstructorName` over the member's signature; `itaniumDestructorName`) - no virtual base splits a covered class; the
Microsoft forms have one name and no alias. `madeFromRead` names a covered class's constructor or destructor by the program's
class (the read's `Holder<$T0>$read1` is `Holder<int>`), and `madeApart` tells "its name" from "its symbol or alias".

**Census** (x86_64-linux): cases with no check: member bodies **made with no replay 147** (145 without the probes), beside 0,
replayed the rest by `memberShape`'s reasons (its class replayed 3,911, out of line 72, `meminit` 38, no body read 16, ...);
functions 536 with no replay as M8.2. With `-check=templates`: covered 147, frames 147 equal, shells 147 equal, **made 147
equal**; built 755 substituted, 0 apart. Self-host (`-check=templates -templates=subst`): nonzero 0, class equal 3,868, member covered 338, frames, shells and made 338
equal, built 3,980 substituted, 0 apart (no constructor or destructor of a covered class there).

**src/**: `Templates.h` +1, `Templates.cpp` +18 (`memberNamed`, `madeFromRead`'s name, `madeApart`'s split, `madeMember`):
src/ 96,157 -> 96,176 (+19). EXCLUSIONS re-pointed (3 Templates.cpp lines).

**Gates** (the commit's own tree, Linux, REF `0fbd2fd3`): identical 0 differ on four targets plain (1926/3852/1926/1926) and
three with `-templates=subst` (1926/3852/1926) - the covered shapes: every covered member body, its constructors and destructors
now among them (template-class's `Holder<int>` and `Pair<int>`, template-nested's `inner<int>` and `inner<char>`, C1 with its C2
alias, D1 with D2), made from the read with no replay against REF's replay, byte for byte; subst.sh 0 failed (its run.sh under
the flag -O0/-O2 1308/0, typed.sh 955/0; self 102 of 102, ceiling 26 of 26, 3 shapes of 3), then run.sh 1308/0, then run.sh with
CXX1 carrying `-templates=subst` 1308/0 (952 printed, 352 refused as recorded; none falls back, none fails - the ledger's cases
among them); `-check=templates -templates=subst` class `differ` 0, 0 apart, no unit failing on a difference over the cases
(nonzero 353, REF's) and the self-host (nonzero 0); self-compile of Templates.cpp and Typing.cpp rc 0 plain and under the flag;
static rc 0 (exclusions 0/0 after re-pointing, comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to
date); g++ `-Werror` clean; Windows cl /W4 /WX clean, first and alone (336 s delta over the step 1 build), then names.sh 963
passed 0 failed and overload.sh 46 agreed 0 differed (clang 19 on the box).

**What is left**: out-of-line member bodies (72) and `meminit` bodies (38) - a constructor with member initialisers is where
member construction from the read begins; the 5 locals and 1 member of a class the read named over its parameter
(`Tree<$T0>::It`; regex-ecmascript), which `readStands`' class declared apart does not reach - a nested class read inside its
enclosing class's read is the next map; the 5 conditionals.

## M8.3 slice 3b, step 4's shape: which covered class could be made from the read with no replay, and why the rest replay (lane m83i, 2026-11-23)

**What landed** (census only; nothing the program sees changes, with or without the flag): the read now keeps its held member
bodies' records beside its member functions (`readBodies_`, by the class's start, each with its function's symbol at the read),
and `classSkip` asks of every covered class (one the checks hold equal: `classLaid`, `classMembers`) what a class made from the
read with no replay would lack - anything its body declares at its own level beyond data members and member functions (a
typedef, alias or using-declaration, an enumeration, a nested or elaborated class, a friend, a member template, a
`static_assert`), refused by name - and whether the replay's held member bodies are the read's records under the real tag (each
one's place, key, class and written name, and its function the replay's through `readSymbol_`). `classWhy` gives a member body
whose class was not covered its class's own reason ("its class replayed: <the class door's reason>"), so the census says which
class shape holds which bodies.

**Census** (x86_64-linux, `-check=templates -templates=subst` over tests/cases): class equal 406 (396 without the probes), of
which **the class skip's shape takes 123** (118 without the probes) with 104 held member bodies (102), their records held equal
to the replay's in every one (no `differ`); the rest replay by name - a member typedef, alias or using-declaration 266 (390
bodies), a member template 10 (1), a nested or elaborated class 5 (13), a member enumeration 2 (0). Member bodies: covered 147
(unchanged), the rest by reason as m83h; **the 3,911 "its class replayed" bodies, by their class's reason**: a static data member
2,076, a type parameter bound to a class 1,164, a member function's signature over a type the binding does not carry 348, a
non-type parameter 109, a pack or template parameter 73, a data member of class type 63, a base named through a parameter 32,
a base 26, a virtual function 15, a parameter bound to a reference 4, a data member of a type the binding does not carry 1. **The
class skip frees none of the 3,911**: it takes only classes the checks hold equal, whose bodies are already the 147 covered; what
frees them is widening the class door's shape, static data members first (2,076, [temp.inst]/3) and class-bound parameters
second (1,164). Self-host: nonzero 0, class equal 3,868, of which the shape takes 168 (168 held bodies, all records equal) and a member typedef, alias or using-declaration holds 3,700 (4,290 bodies); member covered 338, frames, shells and made 338 equal; built 3,982 substituted (3,980 at m83h: the self-host compiles this change's two maps too), 0 apart; its "its class replayed" bodies are 57,980, a class-bound parameter 46,573 of them.

**src/**: `Templates.h` +5, `Templates.cpp` +51 (`classSkip`, `classWhy`, `readBodies_` at the read, the census in `makeClass`):
src/ 96,176 -> 96,232 (+56). EXCLUSIONS re-pointed (3 Templates.cpp lines: 1972->1976, 2024->2028, 2721->2725).

**Gates** (the commit's own tree, Linux, REF `d3e919f7`): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst` on x86_64-linux (1926; the covered shapes as m83h: every covered member body, constructors and destructors among them, made from the read against REF's replay); subst.sh 0 failed (self 102 of 102, ceiling 26 of 26, 3 shapes of 3); run.sh plain and under the flag running at the commit (the lane's handover gives their counts); `-check=templates` class `differ` 0 and no unit failing on a difference over the cases (nonzero 353, REF's) and the self-host (nonzero 0); self-compile of Templates.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0 after re-pointing, comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date); g++ `-Werror` clean; Windows cl /W4 /WX clean, first and alone (212 s full), then names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed (clang 19 on the box).

**Where it stopped, and what is left**: step (1) itself - the program's class made from the read for the 123 classes the shape
takes (the Type and its members laid from the read's records, its member functions declared under the real tag with the
replay's symbols that `classMembers` already computes, the injected class name, its held bodies from `readBodies_` through
m83g's path) - is not begun: it replaces `readClassBody`'s replay in `makeClass`, whose side effects (the function table's
entries the bodies' `which` index, implicit specials, the class scope's names) are what step 4's records must stand for; the
replay beside it only under `-check=templates`. Then the shape widened: member typedefs (266 classes), static data members
(2,076 bodies), out-of-line member bodies (72).

## M8.3 slice 3b, step (1)'s door: the class skip asked before the replay, and the read's layout split out (lane m83j, 2026-11-23)

**What landed** (census only; nothing the program sees changes, with or without the flag): step (1) replaces `readClassBody`'s
replay in `makeClass`, so its decision cannot wait for the replay as `classSkip` does. `classBefore` asks it first - `classShape`,
then `classBeyond` (classSkip's scan of the body's own level, split out: a typedef, alias or using-declaration, an enumeration, a
nested or elaborated class, a friend, a member template, a `static_assert`, refused by name), then the read's records kept - and
the census holds what it takes against what `classSkip` takes after the replay ("differ: the class skip before the replay and
after it" when they part). `laidFromRead` is `classLaid`'s layout split out: the read's data members under the binding, offset,
size and alignment, which the class made from the read completes; `classLaid` now holds that against the replay's.

**Census** (x86_64-linux, `-check=templates -templates=subst` over tests/cases): before the replay the door takes **123** (118
without the probes) - 72 (70) with member functions or held bodies and **51** (48) of data members and implicit special members
alone, the first shape a class made with no replay needs no function table for - exactly the 123 the skip takes after it; no
`differ`; every other class's reason before the replay is its reason after (a type parameter bound to a class 635, a non-type
parameter 458, a member typedef, alias or using-declaration 266, a signature over an uncarried type 207, a static data member
183, ...). nonzero 353 (REF's). Self-host: nonzero 0, before the replay 168 = the skip's 168, no `differ`; member typedefs 3,700.

**src/**: `Templates.h` +3, `Templates.cpp` +39 (`classBeyond`, `classBefore`, `laidFromRead`, the census in `makeClass`):
src/ 96,232 -> 96,274 (+42). No EXCLUSIONS line moved (its Templates.cpp citations are all before the change).

**Gates** (the commit's own tree, Linux, REF `235c0e6e`): identical 0 differ plain on x86_64-linux 1926, x86_64-windows 3852,
arm64-darwin 1926 (tms6747 and the `-templates=subst` legs in the lane's handover); subst.sh 0 failed (self 102 of 102, ceiling
26 of 26, 3 shapes of 3); `-check=templates` class `differ` 0 over the cases and the self-host; static rc 0 (exclusions 0/0,
comments 0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date); g++ `-Werror` clean; Windows cl /W4 /WX
clean, first and alone (101 s full), then names.sh 963 passed 0 failed and overload.sh 46 agreed 0 differed.

**Where it stopped**: the class itself made from the read is not begun. What the replay sets that a class made from the read must
set too (structOrUnionSpecifier, ParserType.cpp): the Type under the tag with its local name, specialization, template namespace,
injected class name (`tag::name`) and class-key name; its members and size from `laidFromRead`, data size, zero-sized and empty
subobjects, `complete`; the implicit specials (`Classes::declareImplicitSpecials`), non-trivial copy and destructor flags; any
member initialiser's place (`memberInit_`). One implementation of those is the parser's tail, so the next step is that tail
split into a function both call, then the 51 classes of data members alone made through it with the replay beside them only
under `-check=templates`.

## M8.3 slice 3b, step (2)'s record: a covered class's member typedefs and aliases read under the binding, held against the replay's (lane m83j, 2026-11-23)

**What landed** (census only; nothing the program sees changes, with or without the flag): the read keeps each covered class's
member typedefs and aliases in order (`readTypedefs_`, by the class's start, from the read's layer: every `readTag::name` but the
injected name and a nested class), and the class skip takes a class that declares them: `classBeyond` refuses by name only a
using-declaration now; `typedefsCarried` (before and after the replay alike) refuses by name an alias whose type the binding does
not carry (a class named over the read's parameter, as `classShape` does for a signature); after the replay `classSkip` holds
each one's name and its type under the binding against the replay's own `tag::name` (`memberTypedefs` over the replay's typedefs).

**Census** (x86_64-linux, `-check=templates -templates=subst` over tests/cases): the skip takes **368** classes (358 without the
probes), 123 as before and **245** (240) with member typedefs or aliases (348 held bodies), their aliases equal to the replay's in
every one; 17 replay by name (an alias over a type the binding does not carry); no `differ`; before the replay the door takes the
same 368 (209 of data members and implicit specials alone, 159 with member functions or held bodies). nonzero 353 (REF's).
**Self-host**: nonzero 0; the skip takes 168 + **3,393** with member typedefs (3,658 held bodies), 149 replay by name; but
**158 classes (632 held bodies) differ in a held member body's place, key, class or name** - the typedef classes reach the held
bodies' comparison for the first time, and in the library's headers some of them are apart. The door before the replay takes
those 158 too ("differ: the class skip before the replay and after it", 158), so step (1) may not take a class with member
typedefs until that difference is found and mended or refused by name; the cases hold none of it.

**src/**: `Templates.h` +5, `Templates.cpp` +40 (`readTypedefs_` at the read, `memberTypedefs`, `typedefsCarried`, `classSkip`'s
comparison): src/ 96,274 -> 96,319 (+45). EXCLUSIONS re-pointed (3 Templates.cpp lines: 1976->1977, 2028->2029, 2725->2726).

**Gates** (the commit's own tree, Linux, REF `235c0e6e`): in the lane's handover (identical plain on four targets and with
`-templates=subst` on three, subst.sh, run.sh plain and under the flag); static rc 0 (exclusions 0/0 after re-pointing, comments
0 over, memory gate 89 held, ast-residue 2012/298 not raised, ide up to date); self-compile of Templates.cpp rc 0 plain and under
the flag with the check; g++ `-Werror` clean; Windows cl /W4 /WX clean (91 s delta), names.sh and overload.sh in the handover.

**Where it stopped**: the 158 self-host classes apart in their held bodies are not diagnosed (one or two per source, so a
library header's class); the class itself made from the read (step (1)) is not begun - the previous entry says what it must set.

## M8.3 slice 3b, step (1): the class made from the read - the completion tail shared, the data-member classes made with no replay, the 158 diagnosed (lane m83k, 2026-11-23)

**Commit 1 (refactor)**: `structOrUnionSpecifier`'s tail after the layout is laid - the data size, zero-sized flags and empty
subobjects, `complete()`, the held bodies taken, the using-members, implicit specials and inheriting constructors, cl's flag, the
non-trivial copy and destructor, abstract, the vtable and vbtable, the bodies replayed or held - is `Parser::completeClass`, over
one record of the layout (`sema::ClassLaidOut`, in Entities.h); the parser calls it as before. Identical on four targets.

**Commit 2 (decision i)**: under `-templates=subst`, a class the door takes before the replay as "data members and implicit special
members alone" is made from the read with no replay (`classFromRead`: the parser's head for a specialization - the tag, its
template and arguments, the injected name, the class key - then `laidFromRead` into the record and `completeClass`, in the scope a
replay reads it in); under `-check=templates` the replay is the program's and the class door holds it as before ("beside").
`fromReadWhyNot` refuses by name what the made class could not carry yet: `#pragma pack`, a member initialiser (`memberInit_` is
keyed by the read's tag), `alignas`, `final`, and a member typedef or alias. The last one is load-bearing: a first build took the
typedef-only classes (`iterator_traits<int *>`, `remove_reference<int>`), which `classBefore` also calls "data members and implicit
specials alone", and made them without their member types - 48 cases stopped compiling under the flag; refused, none does.

**Commit 3 (the 158, census only)**: every one of the 158 self-host classes apart in a held body's key is `initializer_list<X>`
with X written with `::` (`const struct Type::BaseSpec *` 60, `enum opt::mn::Mn` 26, `const struct opt::Region *` 25, `enum
sema::Rank` 24, `opt::MOperand *` 14, `c6x::Node *` 4, `ir::Inst *` 3, two of one): its constructor's held body. `Parser::localOf`
takes what follows the tag's last `::`, inside the argument list too, so `constructorKey` gives the replay
`initializer_list<const struct Type::BaseSpec *>::BaseSpec *>` where the read, over `$T0`, gives `tag::tag`. The replay is
self-consistent (every lookup goes through the same `constructorKey`), so the program is right; the read's key is the true one.
Mending `localOf` changes the replay's names (`~` + localOf for destructors, the constructor test in the declarators) and is
owed to its own round; until then the door and the skip refuse by name a tag with `::` inside its arguments ("replayed: a template
argument written with '::', where localOf cuts the constructor's key"), so nothing apart is ever made from the read.
The refusal is wider than the fault: it takes every covered class with such an argument (810 over the self-host, 632 held bodies
- exactly the 158's), 579 of them with no body apart; narrowing it to a class whose constructor has a held body, or mending
`localOf` to skip a template-id's arguments, is the next step before step (2)'s typedef classes are made from the read.

**Census** (x86_64-linux, tests/cases, `-templates=subst` with no check, `CXX1_TEMPLATE_CENSUS=1`): nonzero 353 (REF's); classes
made from the read with no replay **50** (47 without the probes), 1 replayed by name for a member initialiser, 158 (153) typedef
classes replayed by name; member bodies with no replay 147 as before. Under `-check=templates` (commit 3's tree): 49 beside, 1
member initialiser, 155 typedef, the 3 others now under the `::` refusal; nonzero 353; no class `differ`. **Self-host** under the
check: nonzero 0; skip 2,814 + 168 made from the read, 810 refused by the `::` name, 76 by an uncarried alias, no `differ` - the
158 are gone; no data-member class of the self-host reaches the made path (each has a member typedef or a `::` argument).

**src/**: commit 1 `Parser.h` +2, `ParserType.cpp` +19, `Entities.h` +9 (96,319 -> 96,349); commit 2 `Templates.h` +3,
`Templates.cpp` +61 (96,413); commit 3 `Templates.cpp` +4 (96,417). EXCLUSIONS re-pointed in commit 1 (12 ParserType.cpp lines).

**Gates** (each commit's own tree, Linux, REF `f3cee43e`): commits 1 and 2 - identical 0 differ on four targets plain
(1926/3852/1926/1926), commit 2 also with `-templates=subst` on three (1926/3852/1926: the 50 classes made with no replay emit what
the replay did); subst.sh 0 failed, then run.sh 1308/0, then run.sh under the flag 1308/0. Commit 3 (census only) - subst.sh 0
failed, identical x86_64-linux plain and under the flag 0 differ, the check census over the cases and the self-host above. Static
rc 0 on every tree (exclusions 0/0, comments 0, memory gate 89, ast-residue 2012/298 not raised, ide up to date); self-compile
of Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ `-Werror` clean; Windows cl /W4 /WX clean (229 s full,
then 175 s delta), names.sh 963/0 and overload.sh 46/0 on commit 3's tree.

**Where it stopped**: the typedef classes (158 over the cases, 153 without the probes) still replay - made from the read they need
their member typedefs and aliases declared under the real tag (`readTypedefs_` has them, under the binding), and the `::` refusal
narrowed or `localOf` mended first; then the classes with member functions and held bodies (step 3's bodies already come from the read).

## M8.3 slice 3b, step (1): Parser::localOf mended, and the typedef classes made from the read (lane m83l, 2026-11-23)

**Commit 1**: `Parser::localOf` takes the tag's last `::` outside any template-id's arguments (a depth count over `<>` and
`()`), so `initializer_list<const struct Type::BaseSpec *>` is its own local name, as the read's key has it, and `constructorKey`
gives the replay the key the read gives. m83k's refusal of a tag with `::` inside its arguments, at the door and the skip, is
removed: such a class goes to the same checks as any other, which take it where they hold it equal. No emitted name moved: the
key is internal, and every lookup goes through the same `constructorKey`.
Census of commit 1 (x86_64-linux, `-check=templates -templates=subst`): **self-host** nonzero 0; the skip takes 3,551 classes
(4,290 member bodies) with their member typedefs and 168 with member functions and held bodies alone (m83k: 2,814 + 168), 149
replay for an alias over a type the binding does not carry (m83k 76), none by the `::` name (m83k 810), no class `differ`; frames,
shells and made functions 3,861 equal, member bodies 338 equal, built 3,985 substituted. **tests/cases** as at m83k: nonzero 353
(REF's), with no check 50 classes made from the read with no replay, 158 typedef classes replayed by name, member bodies 147 with
no replay; under the check no class `differ`. src/: `Parser.h` +5, `Templates.cpp` -4 (96,417 -> 96,418).
Gates (commit 1's tree, REF 37fd67f9): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst`
on three (1926/3852/1926); subst.sh 0 failed, then run.sh 1308/0, then run.sh under the flag 1308/0; static rc 0 (exclusions 0/0,
comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (full, about
5 minutes, alone), names.sh 963/0, overload.sh 46/0.

**Commit 2 (step 2)**: the typedef classes made from the read with no replay under `-templates=subst`. `classFromRead` declares
each `readTypedefs_` entry under the real tag (`tag::name`, its type under the binding), after the injected name and the tag,
before the completion tail - where a replay's class body declares it; `fromReadWhyNot` no longer refuses a member typedef or
alias, only one naming the class itself ("replayed: a member typedef or alias naming its own class", which the read would give as
the read class). `iterator_traits<int *>` and `remove_reference<int>` keep their member types: every case that runs links and runs
under the flag. Under `-check=templates` the replay is the program's and the skip holds the typedefs against it, as before.

Census of commit 2: **tests/cases** with no check: classes made from the read with no replay **208** (200 without the probes;
commit 1: 50), 1 replayed for a member initialiser, none for a member typedef; member bodies with no replay 147; nonzero 353
(REF's). Under the check 208 beside, no class `differ` (the free functions' frame and made lines as on commit 1's tree).
**Self-host** under the check: nonzero 0; 2,423 classes beside (the replay the program's), the skip as on commit 1 (3,551 with
member typedefs, 168 alone, 149 uncarried alias), no `differ`; frames/shells/made 3,861 equal, member bodies 338, built 3,985.
src/: `Templates.cpp` +6 (96,418 -> 96,424).
Gates (commit 2's tree, REF 37fd67f9): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst`
on three (1926/3852/1926: the 208 classes made with no replay emit what the replay did); subst.sh 0 failed, then run.sh 1308/0,
then run.sh under the flag 1308/0 (1308 pass, 0 fail; what falls back to the replay is the census above); static rc 0 (exclusions 0/0, comments 0, memory
89, ast-residue 2012/298 not raised, ide up to date); self-compile of Templates.cpp and ParserType.cpp rc 0 on both commits'
trees; g++ -Werror clean; Windows cl /W4 /WX clean (delta, about 4 minutes, alone), names.sh 963/0, overload.sh 46/0.

**Where it stopped**: the classes with member functions and held bodies still replay with no check (the skip takes 168 over the
self-host, their bodies already from the read); a typedef naming its own class replays by name; the 149 self-host classes with an
alias over a type the binding does not carry replay by name.

## M8.3 slice 3b: a type parameter bound to a class, and data members of class type, made from the read (lane m83m, 2026-11-23)

**Commit 1**: `classShape` takes a type parameter bound to a class - a struct `boundClass` takes (M8.2's: not a union, not a
specialization the instantiation has not completed) whose tag the read did not name over its parameters - where it refused every
binding that was no scalar; such a parameter goes to the same checks as a scalar one (`classLaid`, `classMembers`, `classSkip`),
and a class the door takes as "data members and implicit special members alone" is made from the read with no replay under
`-templates=subst` (`classFromRead`, m83k's path), its covered member bodies through m83g's path. A data member of
class type stays refused by name here (commit 2 takes it), and a binding to a union, a reference or an array.
Census of commit 1 (x86_64-linux): **tests/cases** with no check: classes made from the read with no replay **393** (384 without
the probes; 2acec9d6: 208/200), 1 replayed for a member initialiser; member bodies with no replay 218 (147); nonzero 353 (REF's).
Under `-check=templates`: 393 beside, no class `differ`; member bodies covered 219, frames, shells and made 218 equal and one
thread-pool body apart in its frame (a slot no local names), which replays by name for its landing pads, so nothing apart is
made; 25 classes replay bound to a class the read named over its parameters. **Self-host** under the check: nonzero 0; **17,254
classes beside** (2,423), member bodies covered 1,026 (338), frames, shells and made 3,861 equal, no `differ`; the largest reasons
left: a member function's signature over a type the binding does not carry 20,567 classes, a data member of class type 18,515.
src/: `Templates.cpp` +4 (96,424 -> 96,428). No EXCLUSIONS line moved.
Gates (commit 1's tree, REF 2acec9d6): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst`
on three (1926/3852/1926: the 393 classes made with no replay emit what the replay did); subst.sh 0 failed, then run.sh 1308/0, then run.sh under the flag 1308/0 (1308 pass, 0 fail; what falls back to
the replay is the census above); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to
date); self-compile of Templates.cpp and ParserType.cpp rc 0; g++ -Werror clean; Windows cl /W4 /WX clean (full, about 4
minutes, alone), names.sh 963/0, overload.sh 46/0.

**Tried and not committed: a data member of class type.** `classShape` taking a data member of any class the read did not name
over its parameters (the next reason, 210 classes over the cases and 18,515 over the self-host) makes only 9 more cases' classes
with no replay (402) and parts the checks: under `-check=templates` 16 classes over the cases differ (forwarding-constructor,
thread-pool: 15 in a member function's name, 1 in the number of member functions) and over the self-host 63 in a member
function's name and 25 in a data member's type, 182 member bodies behind them. A class-typed member gives the class implicit
special members that call the member's (non-trivial), whose names and count the read's records do not carry; that, and the
member's type under the binding, is what the next round finds and mends or refuses by name before the shape is widened.

## M8.3 slice 3b: a member function's signature over a class template-id, carried (lane m83n, 2026-11-23)

**The measure**: classes made with no replay - suite **393 of 2,184** (unchanged, 152dc675's), self-host **17,254 beside** under the
check (unchanged). The commit does not raise it, and is needed for the next: a class with written member functions is made only
by the replay (`classFromRead` takes "data members and implicit special members alone"), so a signature the read now carries
moves its class to the door's "data members, member functions and held bodies" shape, which the next step makes from the read.

**Commit 1**: the reason, counted by kind. `classShape`'s refusal "a member function's signature over a type the binding does not
carry" names the type's kind (`uncarriedKind`). Before the commit (n0, the same build mapping nothing), over the cases 276: a
member type of another class over its parameters 138 (`reverse_iterator<It>`'s `typename iterator_traits<It>::reference`), a class
template-id over the parameters 124 (`initializer_list<T>`, `pair<const K, V>`), its own template under other arguments 14; over
the self-host 20,567 (10,424 / 9,762 / 381). The second and third are taken: a class template-id the read named over its parameters
stands, under the binding, for the specialization the program declared by its key before the replay (`specializationUnder`,
split from `memberUnder`, which uses it; `carriedId`); the pair goes into the class's `Substituted`, so `classMembers`,
`classSkip` and the member bodies' typing see the real class. The ids found before the replay (`early`, kept from
`classBefore`) are the only ones the census after it takes, so the door before and after the replay agree. A template-id the
program had not declared before the replay stays refused by name; nothing the program sees changes.
Census (x86_64-linux): **tests/cases** under the check - equal 758 (750), the signature reason 268 (276: 138 / 123 / 7), the door's
held-bodies shape 279 (271), no class `differ`, nonzero 353 (REF's); with no check 393 made with no replay, member bodies 218, as
before. **Self-host** under the check: nonzero 0, 17,254 beside, no `differ`; the signature reason 20,332 (20,567: a member type of
another class 10,424, a template-id not declared before the replay 9,718, its own template under other arguments 190), the
held-bodies shape 10,423 (10,188).
src/: `Templates.h` +3, `Templates.cpp` +34 (96,428 -> 96,465, +37). EXCLUSIONS re-pointed (3 Templates.cpp lines, +1).
Gates (commit 1's tree, REF 152dc675): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst`
on three (1926/3852/1926); subst.sh 0 failed, then run.sh 1308/0, then run.sh under the flag 1308/0 (1308 pass, 0 fail; the
fallbacks are the census above); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to
date); self-compile of Templates.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full, about 4
minutes, alone), names.sh 963/0, overload.sh 46/0.

**Where it stopped** (the hour): the largest kind left, a member type of another class (`iterator_traits<It>::reference` through a
member typedef), and most template-ids, name a specialization the replay itself makes when it reads the body: the read carries
them only once the door makes those specializations in the body's order, as the replay would. Then work (2): the classes of the
held-bodies shape (279 / 10,423) made from the read with their member bodies through m83g's path, which is what raises the measure.

## M8.3 slice 3b: the held-bodies shape made from the read - written member functions and their held bodies with no replay (lane m83o, 2026-11-23)

**The measure** at commit 1: classes made with no replay - suite **544 of 2,184** (393 at 13fc1432; 533 without the probes, 384), self-host
**24,819 beside** under the check (17,254).

**Commit 1**: `classFromRead` takes the door's second shape, "data members, member functions and held bodies", as well as the
first. `membersFromRead` declares each written member function of the read (`readMembers_`, not the implicit ones) under the
real tag where the replay's class body declares it, before `completeClass` makes the implicit specials: its name and owner
retagged (`retagged`, now the one spelling `classMembers` and `classSkip` use too), its types under the binding with the read
class standing for the made one (`readStands`), its symbol from the read's signature (`memberSymbol`, split from
`classMembers`, which holds the replay's against it under the check), keyed by `constructorKey`, `destructorKey` or
`tag::name` as `declareMember` keys it. The read's held bodies (`readBodies_`) become the class's, retagged and keyed to those
functions, so `classSkip`, the member census and `replayInlineBodies` see the records a replay would have held; a covered body
is made from its read (m83g's path), the rest replay as bodies of a class the replay made do. `fromReadWhyNot` refuses by name
what is kept by the read's symbol or tag: a member function's default argument (`defaultArgs_`), a defaulted special member
(`defaulted_`), and a held body of no written member. Under `-check=templates` the replay is the program's, beside, as before.
Census (x86_64-linux, tests/cases): with no check classes made from the read with no replay **544** (393), 126 replayed for a
member function's default argument, 3 for a member initialiser; member bodies with no replay 218; nonzero 353 (REF's). Under the
check 544 beside, no class `differ`, before the replay 394 + 279 by shape, member covered 219 (the thread-pool body apart in its
frame replays by name, as at m83m). **Self-host** under the check: nonzero 0, **24,819 classes beside** (17,254), 2,864 replayed for a member function's default
argument, before the replay 17,258 + 10,425 by shape, member covered 1,028, frames, shells and made 3,863 + 1,028 equal, no `differ`.
src/: `Templates.h` +3, `Templates.cpp` +46 (96,465 -> 96,514, +49). No EXCLUSIONS line moved.
Gates (commit 1's tree, REF 13fc1432): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst` on three
(1926/3852/1926: the 544 classes made with no replay emit what the replay did); subst.sh 0 failed, then run.sh 1308/0, then run.sh
under the flag 1308/0 (1308 pass, 0 fail; the fallbacks are the census above); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not
raised, ide up to date); self-compile of Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full, about 2.5 minutes, alone), names.sh 963/0, overload.sh 46/0.

**Commit 2**: a member function's default arguments carried. `declareMember` keeps a default's tokens and the scope they are
read in by the function's symbol (`defaultArgs_`, `defaultArgNamespace_`); `membersFromRead` copies the read symbol's entries to
the real symbol, and `classMembers` holds them against the replay's under the check ("differ: a member function's default
arguments or the scope they are read in": none over the cases or the self-host). Most of the 126 were no default at all - an
entry `defaultArgs_` holds for a symbol with an empty list counted as one - so the refusal went whole; in its place a class
body with `constexpr` replays by name ("a constexpr member, whose body a constant compiles where it is needed": W<int> and
W<double> of constexpr-object-template, which stopped compiling when made from the read - their constructor's body is
compiled at the fold, through the replay's path).
Census: with no check classes made from the read with no replay **668** (544; 657 without the probes), 2 replayed for a constexpr
member, 3 for a member initialiser, nonzero 353 (REF's); under the check 668 beside, no class `differ`. **Self-host** under the
check: nonzero 0, **27,684 classes beside** (24,819; the whole of the door's two shapes), member covered 1,030, frames,
shells and made 3,864 + 1,030 equal, no `differ`.
src/: `Templates.cpp` +6 (96,514 -> 96,520). No EXCLUSIONS line moved.
Gates (commit 2's tree, REF 13fc1432): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst` on three
(1926/3852/1926: the 668 classes made with no replay emit what the replay did); subst.sh 0 failed, then run.sh 1308/0, then run.sh
under the flag 1308/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not
raised, ide up to date); self-compile of Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean
(delta, about 1 minute, alone), names.sh 963/0, overload.sh 46/0.

**Where it stopped**: a constexpr member (its body compiled where a constant needs it, which the class made from the read does not
reach yet), a member initialiser, and the data members of class type (m83m's try: the implicit specials' names and count) are
next; then a member type of another class (10,424 self-host) and the template-ids not declared before the replay.

## M8.3 slice 3b: data members of class type made from the read with no replay (lane m83p, 2026-11-23)

**The measure**: classes made with no replay - suite **735 of 2,184** (668 at 6d85ca31; 723 without the probes, 657), self-host
**30,707 beside** under the check (27,684).

**Commit 1**: `classShape` takes a data member of class type (or an array of one) - a plain class, a specialization the program
declared, a type parameter bound to a class - and refuses by name only one of a class the read named over its parameters
("a data member of a class the read named over its parameters"). m83m's try parted the checks in two ways, both mended here.
First, a class-typed member gives the class implicit special members that call the member's (`declareImplicitSpecials` asks the
member's class), which the read, laying `T t;` over a template parameter, cannot know: with such a member `classMembers` holds
the written member functions alone against the replay's, and the implicit specials are `completeClass`'s under the binding in
the class made from the read, as in the replay's (same members, same written functions; identical.sh under the flag is their
oracle). Second, the 15 differing classes over the cases (thread-pool's `atomic<T>`, 15 integer types) were not about members at
all: a conversion function `operator T()` is filed under its target's spelling (`operatorName`), and the read's was "operator
template parameter" where the replay's is "operator int" - `memberName` names it under the binding (`Parser::conversionSpelling`,
now a static member, the one spelling), for the declared function, its symbol and its held body's key (`bodyKey`, which
`classSkip` holds the same way). The differ reasons name the function or the type, so the next difference says what it is.
Census (x86_64-linux, tests/cases): with no check classes made from the read with no replay **735** (668), 3 replayed for a member
initialiser, 2 for a constexpr member, nonzero 353 (REF's). Under the check no class `differ`, class equal 826, skip "data
members, member functions and held bodies alone" 167. **Self-host** under the check: nonzero 0, **30,707 classes beside** (27,684), class equal 31,307, frames, shells and made 3,864 + 1,030 equal, member covered 1,030; 25 classes `differ` in "a data member's type: int for int" - `EntryOf<I, Payload>` of OptCore.h, whose `Kind kind` is the read class's own member enumeration, set apart from the replay's as a nested class would be; the class is refused before the replay ("a member enumeration", and its member initialiser), so the census shows it and nothing the program sees changes - owed: a data member of a member enumeration refused by name in `classShape`, so the census holds no differ.
src/: `Templates.cpp` +23, `Templates.h` +2, `Parser.h` +1, `ParserType.cpp` 0 (96,520 -> 96,546, +26; the static gate's count). No EXCLUSIONS line moved.
Gates (the commit's tree, REF 6d85ca31): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst` on three (1926/3852/1926: the 735 classes made with no replay emit what the replay did - the covered shapes: data members of scalar or class type with implicit specials alone, and with written member functions, held bodies, member typedefs and default arguments); subst.sh 0 failed, then run.sh 1308/0, then run.sh under the flag 1308/0 (1308 pass, 0 fail; the fallbacks are the census above); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile of Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full, about 4 minutes, alone, at the first try; delta of the commit about 2 minutes, alone), names.sh 963/0, overload.sh 46/0.
**Left**: a member type of another class (10,424 self-host), the template-ids not declared before the replay, a constexpr member,
a member initialiser, a data member of a class the read named over its parameters (35 over the cases, 2,582 self-host), a data member of a member enumeration refused by name (the 25 census differs).

## M8.3 slice 3b: a data member of a member enumeration refused by name, and why the template-ids not declared before the replay are not carried (lane m83q, 2026-11-24)

**The measure**: classes made with no replay - suite **735 of 2,184** (unchanged, 7aad979c's), self-host **30,707 beside** under the
check (unchanged). The commit does not raise it, and is needed for the next: it is m83p's owed refusal, so the self-host census
holds no class `differ` and the next step's differences are its own.

**Commit 1**: `classShape` refuses by name "a data member of a member enumeration the read named over its parameters" - an
enumeration whose tag holds a `$`, as `EntryOf<I, Payload>`'s `Kind kind` of OptCore.h does (the read class's own `Kind`, set apart
from the replay's as a nested class is). The class was refused before the replay anyway ("a member enumeration"), so nothing the
program sees changes; the census now names it where m83p's showed 25 classes `differ` in "a data member's type: int for int".
Census (x86_64-linux): **tests/cases** with no check 735 made from the read with no replay, nonzero 353 (REF's); under the check no
class `differ`, class equal 826. **Self-host** under the check: nonzero 0, 30,707 beside, class equal 31,307, **no class `differ`**
(25), 25 refused by the new name; frames, shells and made 3,864 + 1,030 equal.
src/: `Templates.cpp` +2 (96,546 -> 96,548). No EXCLUSIONS line moved.
Gates (the commit's tree, REF 7aad979c): identical 0 differ on four targets plain (1926/3852/1926/1926) and with `-templates=subst` on three
(1926/3852/1926: the covered shapes as at m83p - data members of scalar or class type with implicit specials alone, and with
written member functions, held bodies, member typedefs and default arguments); subst.sh 0 failed, then run.sh 1308/0, then run.sh
under the flag 1308/0 (1308 pass, 0 fail; the fallbacks are the census above); static rc 0 (exclusions 0/0, comments 0, memory 89,
ast-residue 2012/298 not raised, ide up to date); self-compile of Templates.cpp rc 0 plain and under the flag; g++ -Werror clean;
Windows cl /W4 /WX clean (full, about 8 minutes, the header put back rebuilt every file; n1's full build, alone, 3.5 minutes),
names.sh 963/0, overload.sh 46/0.

**Tried and not committed** (work (2), the template-ids not declared before the replay: 17,304 self-host classes): `idsBefore`
made, before a class the door takes, each class template-id its member functions' signatures name over its parameters that the
program had not declared, through `makeClass` in the body's order (return type first, as the replay reads a declaration), then
`classBefore` carried them as m83n's `carriedId` does. It made none: such an id's type is the read's shallow class
(`Two<$T0,$T0>`), and when the read ends `readDeclaration` clears the specialization of every shallow class the read was first
to ask for ("keeps no arguments of the read's": `readShallow_`, `setSpecialization("", {})`), so `isSpecialization()` is false
and its template name and arguments are gone by the time the door asks - neither `carriedId` nor the door sees them. The next
step keeps them (the name, arguments and namespace each cleared shallow had, by type, beside `readShallow_`'s clearing) for the
door's `earlyArgs`; the try and its probe (`template-subst-early`, clang's 4 5 7 / 2.5) are in boxkit (`m83q-ids-wip.patch`,
`m83q-probe/`).

## M8.3 slice 3b: the template-ids not declared before the replay, made before the class from the ids the read's end cleared (lane m83r, 2026-11-24)

**The measure**: classes made with no replay - suite **779 of 2,184** (735; 761 without the probes, 723), self-host
**35,003 beside** under the check (30,707).

**Commit 1** (m83q's root, fixed): when a read ends, `readDeclaration` clears the template name and arguments of every shallow
class the read was first to ask for (`readShallow_`); now each one's id is kept beside that loop (`readIds_`, by type), and
`readId` answers a class's own id or the kept one - for `specializationUnder`, `classShape`'s signature ids and `earlyArgs`.
`idsBefore` (m83q's try, its prints dropped) makes, before a class the door takes, each class template-id its member functions'
signatures name over its parameters that the program had not declared, through `makeClass` in the body's order (return type
first, as the replay reads a declaration); `classBefore` then carries them as `carriedId` does. A cleared shallow is never a
carried class and counts as uncarried in the data members, the signatures, `typedefsCarried` and `earlyArgs` (a shallow such as
`__tree_order<typename KeyOf::..., Cmp>` holds no `$` in its tag: carried, it let `__tree`'s typedef `order` through and 10 cases
failed). `earlyArgs` takes no template with partial specializations and none whose body names the read's own template
(`__tree_iterator` inside `__tree_const_iterator`: made there by the replay inside the class it has begun). Probe
`template-subst-early` (clang's 4 5 7 / 2.5; 6 classes made with no replay under the flag).
Census (x86_64-linux): **tests/cases** with no check 779 made from the read with no replay, nonzero 353 (REF's); under the check no class `differ`, class equal 907 (826), 779 beside.
**Self-host** under the check: nonzero 0, 35,003 beside (30,707), class equal 39,561, no class `differ`; "a class template-id over its parameters the program had not declared" 9,289 (17,304 at m83q's count); frames, shells and made 3,865 + 2,073 equal.
src/: `Templates.cpp` +67, `Templates.h` +6 (96,548 -> 96,621, +73). EXCLUSIONS: three citations re-pointed (Templates.cpp 1978,
2030, 2727 moved 3 lines), no refusal moved.
Gates (the commit's tree, REF 332e3cab): identical 0 differ on four targets plain (1928/3856/1928/1928, the probe one more) and with `-templates=subst` on three ( the covered shapes as at m83q, and a member function over a class template-id the program had not declared, of another template's type parameters, with no partial and not naming the class); subst.sh 0 failed, then run.sh 1309/0, then run.sh under the flag 1309/0 (1309 pass, 0 fail; the fallbacks are the census above); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not
raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (n1 full, alone, 00:46:24-00:48:03, ~1.6 min; the final
tree's delta 01:00:15-01:01:46, ~1.5 min), probe on Windows clang 4 5 7 / 2.5 and cxx3-msvc the same, names.sh 964/0, overload.sh 46/0.
Left: a member type of another class (13,691 self-host), a data member of a class the read named over its parameters (2,582).

## M8.3 slice 3b: a member type of another class, its class made before the class the door takes (lane m83s, 2026-11-24)

**The measure**: classes made with no replay - suite **783 of 2,184** (779; the probe's 4, two of them REF's too), self-host
**35,003 beside** under the check (35,003, unchanged).

**Commit 1** (m83r's n5 and the door's want): `memberUnder` asks `readId`, so `X<$T0>::m` over a shallow the read's end cleared
finds the program's `X<U>` by its kept id; `classShape` wants, for `idsBefore`, the class `X<$T0>` of a member type a member
function's signature names when the program had not declared it and `earlyArgs` takes it, and the second `classBefore` then finds
the member in the class `idsBefore` made (in the body's order, through `makeClass`). Probe `template-subst-memtype` (clang's
5 7 / 2.5 on Windows; `Traits<int>` before `Box<int>`, which is made from the read).
**Why the self-host did not move, measured** (a debug build over Templates.cpp, Parser.cpp, Lowering.cpp): every member type
the door met there is `iterator_traits<$T0>::...`, a template with two partial specializations, which `earlyArgs` refuses. The
next commit takes them; this one is the path they need.
**What it changed in the census**: under the check no class is made from the read that the replay refuses, but "differ: the
class skip before the replay and after it: replayed: a member typedef or alias over a type the binding does not carry" is new
(tests/cases 18, self-host 151): before the replay a typedef `typename X<T>::m` is refused because `X<U>` is not made yet, after
it `memberUnder` now finds the replay's `X<U>` and takes it. The class is replayed either way (no-check census 783 made, as
under the check), so nothing the program sees changes; owed: the typedef's `X<U>` made before the class too (tried, below).
Census (x86_64-linux): **tests/cases** with no check 783 made, nonzero 353 (REF's); under the check class equal 1,083 (907).
**Self-host** under the check: nonzero 0, 35,003 beside, class equal 53,300 (39,561); "a member type of another class over its
parameters" 13,739 before the replay (unchanged); frames, shells and made 3,865 + 2,073 equal.
src/: `Templates.cpp` +7 (96,621 -> 96,628). EXCLUSIONS: no line moved.
Gates (the commit's tree, REF b6c8b37a): identical 0 differ on four targets plain (1930/3860/1930/1930) and with `-templates=subst` on three (1930/3860/1930; the covered shapes as at m83r, and a member function over a member type of a class template-id the program had not declared, of a template with no partial); subst.sh 0 failed, then run.sh 1310/0, then run.sh under the flag 1310/0 (1310 pass, 0 fail; the fallbacks are the census above); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (full, alone, 11:09:23-11:13:08, ~3.8 min), probe on Windows clang 5 7 / 2.5 and cxx3-msvc the same, names.sh 965/0, overload.sh 46/0.

**Commit 2** (work (2)): a data member of a class template-id the read named over its parameters (`Cell<$T0> c`) is the
program's `Cell<U>` where it was declared before the class (`carriedId`, carried as the signatures' ids are), and otherwise wanted
by `idsBefore` when `earlyArgs` takes it, so the door makes it first and the class from the read. The replay makes a data
member's class and a signature's in the body's order, which the read's members and signatures do not keep apart: a class
wanting both is refused by name ("class template-ids made first for a data member and a member function's signature").
Probe `template-subst-datamember` (clang's 9 / 3.0 8 on Windows). The measure: suite **792 of 2,184** (783: the probe 4,
template-nested 3, template-nested-angle 1, template-subst-spec 1), self-host 35,003 beside (unchanged: its 2,606 are refused by
`earlyArgs`, the same templates as below). Census: no check 792, nonzero 353; under the check the same 18 skip differs as
commit 1 (self-host 151), no other; self-host nonzero 0. src/: `Templates.cpp` +14 (96,628 -> 96,642). EXCLUSIONS: no line moved.
Gates (the commit's tree, REF b6c8b37a): identical 0 differ on four targets plain (1932/3864/1932/1932) and with `-templates=subst` on three (1932/3864/1932; the covered shapes and a data member of a class template-id the program had not declared, of a template with no partial); subst.sh 0 failed, then run.sh 1311/0, then run.sh under the flag 1311/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (delta, alone, 11:39:41-11:42:10, ~2.5 min), probe on Windows clang 9 / 3.0 8 and cxx3-msvc the same, names.sh 966/0, overload.sh 46/0.
**Tried, not committed (the hour)**: `boxkit/m83s-n4-wip.patch` (against commit 2) - `earlyArgs` takes a template with partial
specializations when none of its bodies names the read's template (`makeClass` chooses the partial), and `typedefsCarried` wants a
typedef's `X<$T0>`, ahead of the members' (the replay reads typedefs first): suite **810**, self-host **35,154 beside**, class
differ 0 in both (the 18 and 151 gone), nonzero 353 / 0; no gate run. The 13,739 `iterator_traits<$T0>` stay refused there:
next, why `earlyArgs` still refuses them (a debug line in `classShape`'s member-type branch, as m83s-dbg.sh did).

## M8.3 slice 3b: a member type of a template with partial specializations, its class made before the class the door takes (lane m83t, 2026-11-24)

**Commit 1** (m83s's n4, gated): `earlyArgs` takes a template with partial specializations when none of its bodies (the
primary's or a partial's) names the read's template - `makeClass` picks the partial, the same pattern match the replay uses, so
the class made first is the replay's - and `typedefsCarried` wants a typedef's `X<$T0>` whose `X<U>` the program had not
declared, ahead of the members' (the replay reads the typedefs first). Probe `template-subst-partial` (`Traits` with `T *` and
`const T *` partials, `Rev<int *>` and `Rev<const double *>`; clang's 9 9 18 / 2.5 5.0 on Windows, cxx3-msvc the same). The
measure: suite **814 of 2,184** (792; the probe's 4), self-host **35,154 beside** under the check (35,003). Census: no check 814
made, nonzero 353 (REF's); under the check no class differ (m83s's 18 skip differs gone), self-host nonzero 0, no class differ.
src/: `Templates.cpp` +10, `Templates.h` +1 (96,642 -> 96,653). EXCLUSIONS: no line moved.
Gates (the commit's tree, REF d0a6d92b): identical 0 differ on four targets plain (1934/3868/1934/1934) and with
`-templates=subst` on three (1934/3868/1934; the covered shapes, a data member's and a signature's class template-id the program
had not declared, and now one of a template with partials); subst.sh 0 failed, then run.sh 1312/0, then run.sh under the flag
1312/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0,
memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (full, alone,
11:55:32-11:57:07, ~1.6 min), names.sh 967/0, overload.sh 46/0.
**Found, not yet mended**: the self-host's 13,739 "a member type of another class over its parameters" are not `earlyArgs`'s - a
debug line shows it takes `iterator_traits<$T0>` under the binding (a pointer argument). They are all `reverse_iterator<$T0>`,
whose body holds `template <class U> reverse_iterator(const reverse_iterator<U> &)`: the first `classBefore` (with `wanted`)
is refused by `classBeyond`'s "a member template", so `idsBefore` makes nothing, and the second (no `wanted`) reports the
signature's member type because `classShape` is asked before `classBeyond`. The census names the wrong reason (owed: the
member template's reason first); the next step for these is a member template in a class made from the read.

**Commit 2** (the census names the blocker): `classBefore` reports `classBeyond`'s refusal before the shape's. Under a member
template the first `classBefore` (with `wanted`) is refused, `idsBefore` makes nothing, and the second then failed on a
signature's id that only that refusal left unmade - so the census named an id, not the member template. Only the reason
changes: every refusal of either starts "replayed:", so the door decides as before (identical 0 differ). Why it is needed
for the next step: the measure's next classes are these. The self-host's "before the replay: replayed: a member template" is
now **29,588** (`reverse_iterator<$T0>` 13,739 by its converting constructor template, `vector<$T0>`'s `initializer_list<$T0>`,
`pair<$T0,$T1>`'s `template <class U1, class U2> pair(const pair<U1, U2> &)`), the cases' 540 (57); the signature lines of 13,739 and 9,289
fall to 0 and 906 (its own template under other arguments, 2,234, is `pair<const K, V>` naming `pair<K, V>`). The measure unchanged: suite 814, self-host 35,154 beside. src/: `Templates.cpp` +1 (96,653 -> 96,654).
EXCLUSIONS: no line moved.
Gates (the commit's tree, REF d0a6d92b): identical 0 differ on four targets plain (1934/3868/1934/1934) and with
`-templates=subst` on three (1934/3868/1934; commit 1's shapes); subst.sh 0 failed, then run.sh 1312/0, then run.sh under the
flag 1312/0; census no class differ in the cases or the self-host, nonzero 353 (REF's) / 0; self-compile Templates.cpp and
ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not
raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (delta, alone, ~1 min, done 12:14:09), names.sh 967/0,
overload.sh 46/0, the probe on clang and cxx3-msvc the same.

## M8.3 slice 3b: a class with member function templates made from the read under -templates=subst (lane m83u, 2026-11-24)

**Commit 1**: the class door takes a class whose body declares member function templates. The replay's class body declared
each one in place (`template <class U> reverse_iterator(const reverse_iterator<U> &)`, `pair`'s converting constructor,
`vector`'s range constructor); that block is now `Parser::memberTemplate`, the one path, and the body loop calls it. A class
made from the read calls it too (`Templates::templatesFromRead`, from `classFromRead` before `completeClass`): at each of the
read's member templates' `template` token, in the body's order (`templatesOf`, by `start`), with the access the read saw,
the class's parameters bound by name, `instantiatingParams_`/`Binding_`/`Values_`/`Args_` and the class on the class stack as
the replay's body has them - so its `TemplateDecl` (class binding, packs, pattern, `memberTemplates_` key) is the replay's,
and a constructor template counts as a user-declared constructor before the implicit specials are decided, as in the body.
`classBeyond` no longer refuses "a member template": it passes the template's parameter list (whose `class U` read as a
nested class) and still refuses a `template <...> friend`. Under the check `classMembers` holds the read's member templates
(kept under the read's tag) against the replay's (`templatesHeld`: count, place, name, kind, access, class parameters and
binding under `s`); a pattern is not compared, since a member template's own parameters and the class's are both `$Tn` in
the read's. Probe `template-subst-membertemplate` (`Rev<I>` with a converting constructor template, `Box<T>` with two member
function templates; clang's 6 6 3 / 2 5 3 / 4.00 5.00 on Windows, cxx3-msvc the same); its .nonames records the C1 of the
inline constructors on x86_64-linux (implicit-move.nonames's difference; a constructor template of a class template cannot
be defined out of line here). The measure: suite **1,047 of 2,184** (814; 1,012 without the probes, 810; the probe's 4),
self-host **51,435 beside** under the check (35,154). Census: no check 1,047 made, nonzero 353 (the same units as REF's);
under the check no class differ in the cases or the self-host, self-host nonzero 0; the self-host's "before the replay"
refusals now lead with a signature's class template-id the program had not declared 9,316, a data member's 2,606, a base
2,373, its own template under other arguments 2,234 (`pair<const K, V>` naming `pair<K, V>`, work 2, not reached).
src/: 96,654 -> 96,744 (+90: `Templates.cpp` +77, `Templates.h` +5, `ParserType.cpp` +7, `Parser.h` +1). EXCLUSIONS: 28
`ParserType.cpp` citations re-pointed (the extracted block moved its three refusals and everything after it), no refusal moved.
Gates (the commit's tree, REF cf66b74e): identical 0 differ on four targets plain (1936/3872/1936/1936) and with
`-templates=subst` on three (1936/3872/1936: every case, the member-template classes among them); subst.sh 0 failed, then
run.sh 1313/0, then run.sh under the flag 1313/0; census no class differ in the cases or the self-host (the frame and made
differ lines are REF's own); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0
(exclusions 0/0 after the re-pointing, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror
clean; Windows cl /W4 /WX clean (full, 12:30:49-12:34:50, ~4 min, the coordinator's gate jobs on the box), names.sh 967/1
then the probe 0 failed with its .nonames, overload.sh 46/0, the probe on clang and cxx3-msvc the same.
**Next**: the 9,316 signatures' class template-ids the program had not declared that the member-template classes now reach
(which ids, and whether earlyArgs takes them); `pair<const K, V>` naming `pair<K, V>` (2,234).

## M8.3 slice 3b: a member typedef's class template-id made before the class under -templates=subst (lane m83v, 2026-11-24)

**Commit 1**: the 9,316 self-host classes refused as "a member function's signature ... a class template-id over its
parameters the program had not declared" were, by a debug build over Templates.cpp, Parser.cpp and Lowering.cpp, three ids:
`vector<$T0>`'s `initializer_list<$T0>` (316), `pair<$T0,$T1>`'s `pair<const $T0,$T1>` (259, its own template: work 2) and
`__hash_table`'s `__hash_iterator<$T0, ...>` (78). The first was wanted by `classShape` and `earlyArgs` takes it; nothing made
it because `typedefsCarried` then refused `typedef std::reverse_iterator<T *> reverse_iterator` - a class template-id over the
parameters the program had not declared, which it did not want (only an `X<$T0>::m`) - so `classBefore` failed, `idsBefore`
made nothing, and the second `classBefore` named the signature. A typedef's ids are now `classShape`'s, read in the
typedefs' order ahead of the members' as the replay reads the body: `X<$T0>::m` (m83t's, moved from `typedefsCarried`) and
`X<$T0>` itself, wanted when `earlyArgs` takes it and mapped under `s` once made (`carriedId`), so `classFromRead` declares
the typedef as the made class; `typedefsCarried` passes what `classShape` wanted and refuses the rest as before. Probe
`template-subst-typedefid` (`Vec<T>` with `typedef Rev<T *> reverse_iterator` and `assign(List<T>)`, neither declared
before `Vec<int>`; clang's 3 15 6 / 3.75 2 on Windows, cxx3-msvc the same; 6 classes made from the read, 4 made before);
its .nonames records x86_64-linux's C2-only constructors (implicit-move.nonames's difference). The measure: suite
**1,147 of 2,184** (1,047; 1,106 without the probes, 1,012), self-host **59,302 beside** under the check (51,435); the
self-host's "had not declared" 9,316 -> 3,175. src 96,744 -> 96,757 (+13, `Templates.cpp`); no EXCLUSIONS line moved.
Census: no check 1,147 made, nonzero 353 (REF's units); under the check no class differ in the cases or the self-host (the
frame and made differ lines are REF's own), self-host nonzero 0, "made before the class" 32,800. Gates (the commit's tree,
REF aae2267c): identical 0 differ on four targets plain (1938/3876/1938/1938) and with `-templates=subst` on three
(1938/3876/1938: every case, the probe's typedef ids among them); subst.sh 0 failed, then run.sh 1314/0, then run.sh under
the flag 1314/0 (0 failed); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0
(exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4
/WX clean (delta, alone, 13:10:46-13:12:47, ~2 min); names.sh whole on aae2267c 968/0 (owed since m83u), on this tree
969/0 with the probe's .nonames; overload.sh 46/0; the probe on clang and cxx3-msvc the same.
**Next**: `pair<const K, V>` named by `pair<K, V>`'s converting constructor (259 of the three sources' 653; the self-host's
3,175 "had not declared" left are it and `__hash_iterator<$T0, ...>` under `__hash_table`), then "its own template under
other arguments" (2,234).

## M8.3 slice 3b: its own template under other arguments, made inside the class under -templates=subst (lane m83w, 2026-11-24)

**Commit 1**: `pair<K, V>`'s `pair(const pair<const K, V> &)` and its partial's `pair(const pair<K, V> &)` each name their
own template under other arguments; the replay makes that class where it reads the member, inside the class it has begun
(its tag declared, being defined), so the other one's body finds the first incomplete and does not make it again. Making it
first (idsBefore) would reverse that order, so it is made where the replay makes it: `classShape` takes such an id before
the replay when `earlyArgs` takes it (`itself`: the read's own template, no scan of its bodies) and maps the program's after
it (`specializationUnder`); `membersFromRead` calls `idsInside` before declaring each member, which makes the id its
signature names through `makeClass` (the one path) and maps it under `s`. Found on the way: `readStands` maps a partial's
`pair<$T0,$T1>` to the partial itself, so `idsInside` asks under `classShape`'s map (`ids`) and puts its mapping ahead of
readStands' in `s` (the first version declared `pair<const int,int>(const pair<const int,int> &)` and ten map cases failed
under the flag). Probe `template-subst-ownid` (`Two<A, B>` and its partial `Two<const A, B>`, each made first; clang's
7 34 7 / 56 11 on Windows, cxx3-msvc the same; 4 classes made from the read with no replay); its .nonames records
x86_64-linux's C2-only constructors. Found and owed: a partial's member constructor defined out of line
(`template <class A, class B> Two<const A, B>::Two(A x, B y)`) is refused "defined twice" with no flag, so the probe's
partial defines its members inline. The measure: suite **1,204 of 2,184** (1,147; without the probes 1,159, 1,106),
self-host **64,733 beside** under the check (59,302): its "had not declared" 3,175 and "its own template under other arguments" gone, the next largest reason "a data member of a class the read named over its parameters" (2,606). src 96,757 -> 96,788 (+31, `Templates.cpp`, `Templates.h`); no
EXCLUSIONS line moved. Census: no check nonzero 353 (REF's set); under the check no class differ, nonzero 353.
Gates (the commit's tree, REF 8746760f): identical 0 differ on four targets plain (1940/3880/1940/1940) and with
`-templates=subst` on three (1940/3880/1940: every case, the probe's own-template ids and the map cases' `pair<const K, V>`
among them); subst.sh 0 failed, then run.sh 1315/0, then run.sh under the flag 1315 passed, 0 failed (the classes not made from the read replayed, as the census counts them);
self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0,
memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (full on the first
version 13:32:31-13:36:10, ~3.7 min alone; delta 13:55:04-13:57:39, ~2.6 min); names.sh 970/0 with the probe's .nonames (a first run beside the probe's own, in one tree, said 820/0 and does not count); overload.sh 46/0; the probe on
clang and cxx3-msvc the same.
**Next**: the self-host's "a data member of a class the read named over its parameters" (2,606), then "a base" (2,373)
and "a type parameter bound to a reference" (1,841); the partial's out-of-line member constructor refused "defined twice".

## M8.3 slice 3b: a data member's class template-id over ids of the parameters, made before the class under -templates=subst (lane m83x, 2026-11-24)

A data member or a member typedef whose class template-id takes, as arguments, other ids over the class's parameters
(`Tree<Pair<K, V>, KeyOf<K, V> > tree_`, map's `__tree<value_type, __map_key<K, V, C> >`) was refused "a data member of a
class the read named over its parameters": `earlyArgs` refuses an argument the read named over the parameters. Now
`argsWanted` walks such an id's arguments innermost first, as the replay makes them reading the id: an argument the program
carried is mapped under `s`, one `earlyArgs` takes is wanted ahead of the id (moved there when an earlier member wanted it
behind), and `earlyArgs` takes an argument already wanted (`pending`); `idsBefore` maps each id it makes (or finds) under its
`s`, so the next id reads its arguments as the made classes; every id is made through `makeClass`, the one path. Found on the
way: a member typedef's id is made by the replay ahead of the data members' (m83v), so its arguments must be wanted ahead of
it too - the first version made `Tree<Pair<int,char> >` before the typedef's `Tree<Pair<char,int> >` and identical under the
flag differed on the probe in the order of the two `key` bodies. Probe `template-subst-nestedid` (`Map<K, V>` with
`Tree<Pair<K, V>, KeyOf<K, V> > tree_` and `typedef Tree<Pair<V, K>, KeyOf<V, K> > back; back back_;`; clang's 35045 57067
on Windows, cxx3-msvc the same; 14 classes made from the read, REF 12); its .nonames records x86_64-linux's C1 of `Map`'s
constructor (template-subst-ownid's difference). Recorded, not fixed: `tests/open/partial-ctor-out-of-line` (clang ACCEPT
1 2 30 4): a partial's constructor defined out of line beside the primary's is refused "defined twice" with no flag; alone,
it is never emitted and the link fails. The measure: suite **1,220 of 2,184** (1,204; REF on this tree 1,216, the gain
the probe's 2 Maps and nested-parse-state's 2), self-host **64,984 beside** under the check (64,733); its "a data member of a
class the read named over its parameters" only 2,606 -> 2,603 - the self-host's are another shape (below). src 96,788 ->
96,823 (+35, `Templates.cpp`, `Templates.h`); no EXCLUSIONS line moved. Census: no check nonzero 353 (REF's set); under the
check no class differ, nonzero 353; self-host nonzero 0.
Why the self-host's 2,603 stay: a debug build (boxkit/m83x-Tdbg.cpp: M83XDBG at the refusal, with `wanted`) over the
self-host's sources printed nothing - the first `classBefore` (with `wanted`, in `idsBefore`) never refuses them for the data
member; the reason is the second one's (no `wanted`), after `idsBefore` made nothing: the first pass refused for another
reason, or passed and `fromReadWhyNot` stopped it. The census names the data member where that other reason holds.
Gates (the commit's tree, REF 235c0262): identical 0 differ on four targets plain (1942/3884/1942/1942) and with
`-templates=subst` on three (1942/3884/1942: every case, the probe's nested ids and the map cases among them); subst.sh 0
failed, then run.sh 1316/0, then run.sh under the flag 1316/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and
under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date);
g++ -Werror clean; Windows cl /W4 /WX clean (full 14:25:16-14:29:01, ~3.75 min alone; delta 14:45:35-14:47:36, ~2 min);
names.sh 971/0 with the probe's .nonames (the restored template-subst-datamember among them); overload.sh 46/0; the probe and template-subst-datamember on clang and cxx3-msvc the same. A slip on the way: the probe was first written over lane m83s's template-subst-datamember; restored from HEAD and the gates rerun whole on the fixed tree.
**Next**: name what `idsBefore` met when it made nothing (the first pass's reason, or `fromReadWhyNot`'s) in place of the
second pass's "a data member", then the largest of those; then "a base" (2,373) and "a type parameter bound to a reference"
(1,841); the partial's out-of-line constructor (tests/open/partial-ctor-out-of-line).

## M8.3 slice 3b: a base the read laid as the program's own class, and the census naming what idsBefore met (lane m83y, 2026-11-24)

**The measure**: class specializations made with no replay - suite **1,262 of 2,184** (1,220; the probe's 4), self-host
**65,332 beside** under the check (64,984).

**Commit 1**: (a) a class whose bases the read laid are the program's own classes - none virtual or reached through another,
none a class template-id over the parameters, none polymorphic - and whose own data members name no parameter (`struct One :
Tag`, `Two : Tag, Data`, `Pad : Tail` over a non-POD `Tail`) is laid by the read exactly as the replay lays it, so
`laidFromRead` takes the read's layout whole (members with the bases' copied in, size, alignment, data size, the zero-sized
flags, the empty subobjects but the read's own) and `classFromRead` adds the read's bases in their order before the members, as
the replay's head does; `classLaid` holds the bases (class, offset, access, virtual, direct, place) and the data size against
the replay's. A base beside a data member over the parameters is refused by name ("a base, beside a data member over the
parameters", 1,631 self-host), and "a base" is named by kind (a class template-id over the parameters, a polymorphic class, a
virtual base). A base `X<$T0>` is not here: the read leaves it out of its layout ("a base named through a parameter", 215).
(b) `idsBefore` answers what stopped it - the first `classBefore`'s refusal (ids wanted) or `fromReadWhyNot`'s - and the census
names that where the second `classBefore` (no ids wanted) refuses; the door decides as before. The self-host's 2,603 "a data
member of a class the read named over its parameters" are gone from "before the replay", each under its own reason.
Probe `template-subst-plainbase` (clang's 23 4 8 / 7.5 113 42 on Windows, cxx3-msvc the same; 4 classes made from the read,
under the check class equal 4); its .nonames records x86_64-linux's C1 of `Pad<long long>`'s implicit constructor
(template-subst-ownid's difference). src 96,823 -> 96,848 (+25, `Templates.cpp`, `Templates.h`); no EXCLUSIONS line moved.
Census: no check 1,262 made, nonzero 353 (REF's count); under the check no class differ, self-host nonzero 0. The self-host's
largest "before the replay" refusals now: a signature's class template-id the program had not declared 2,210, a type
parameter bound to a reference 1,841, a base beside a data member over the parameters 1,631, a data member of a type the
binding does not carry 906.
Gates (the commit's tree, REF aec022e8): identical 0 differ on four targets plain (1944/3888/1944/1944) and with
`-templates=subst` on three (1944/3888/1944: every case, the probe's plain bases among them); subst.sh 0 failed, then run.sh
1317/0, then run.sh under the flag 1317/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static
rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl
/W4 /WX clean (full on the census half 15:14:25-15:18:13, ~3.8 min alone; delta 15:21:03-15:28:49, ~7.7 min, the box busy);
names.sh 972/0 with the probe's .nonames; overload.sh 46/0; the probe on clang and cxx3-msvc the same.
**Next**: a base beside a data member over the parameters (1,631: lay the members after the read's bases under the binding, the
parser's base layout one path with the replay's); a base `X<$T0>` the read leaves out (215: made first, as idsBefore makes ids);
the signature ids not declared (2,210) and a type parameter bound to a reference (1,841).

## M8.3 slice 3b: a member function's signature over a class template-id whose arguments are ids of the parameters, made before the class under -templates=subst (lane m83z, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,272 of 2,184 (1,262; the probe's 10 among them), self-host
65,354 beside under the check (65,332); the self-host's 2,210 "a signature's class template-id the program had not declared" are gone - 22 made, the rest now "class template-ids made first for a data member and a member function's signature" (2,478), the largest reason before the replay.

**The finding** (a debug build, `boxkit/m83z-Tdbg.cpp`, over Templates.cpp, Parser.cpp and Lowering.cpp): every "a member function's
signature over a type the binding does not carry: a class template-id over its parameters the program had not declared" there
is `map`'s and `unordered_map`'s `initializer_list<value_type>` (123 and 65), whose argument `pair<const K, V>` a typedef wanted
first: `earlyArgs` takes it once asked with the ids already wanted (pending), as a data member's id is asked.
**Commit 1**: a signature's id has its arguments' ids wanted first (`argsWanted`, the data member's walk, innermost first) and
`earlyArgs` asks it with the ids wanted before it; nothing else changes. Probe `template-subst-sigid` (`Holder<T>`'s
`Wrap<Box<T> > wrapped()` and `Pair<Box<T>, Wrap<T> > paired(const Wrap<Box<T> > &)`, the program declaring none of them first;
clang's 5 510 3 / 40 80 on Windows, cxx3-msvc the same; under the check 10 classes made from the read, class equal 10, 8 ids
made first). `map` itself still replays: its data member `tree_` wants ids too, so "class template-ids made first for a data
member and a member function's signature" refuses it as before. src 96,848 -> 96,850 (+2, `Templates.cpp`); no EXCLUSIONS line moved.
**Tried and set aside** (`boxkit/m83z-n2.patch`): placing a signature's ids ahead of or behind the data members' by where the body
declares each (the signature's `pos` against the member's name in the class's braces), so `map` is made from the read. The suite's
count rose to 1,293, but with no check the census's nonzero rose 353 -> 361: `map() {}`'s `tree_` (a `__tree` made first, with a
written default constructor) is "outside the SSA lowering: a member built by no constructor the parser recorded" once `map` is
made from the read (deque-both-ends, include-map-set, map-move-only-mapped, map-set-comparator, map-set-node-stability,
member-template-body-unchosen, range-for-vector, return-self-referential). The check did not see it: the replay is the program's
there. **Owed first**: a constructor made from the read records its members' constructors (`memberBuilt_`) for a data member whose
class was made before it; then n2's placement lands.
Census: no check nonzero 353 (REF's count); under the check no class differ, self-host nonzero 0. Gates (the commit's tree, REF 7d47fbaf): identical 0 differ on four targets plain (1946/3892/1946/1946) and with `-templates=subst` on three (1946/3892/1946: every case, the probe's signature ids among them); subst.sh 0 failed, then run.sh 1318/0, then run.sh under the flag 1318/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean (delta 16:11:01-16:17:43, ~6.7 min, the box shared; the base full 3.6 min alone); names.sh 973/0 (no .nonames); overload.sh 46/0; the probe on clang and cxx3-msvc the same.
**Next**: a constructor made from the read records its members' constructors for a data member whose class was made before it (then `boxkit/m83z-n2.patch` makes `map` and the 2,478 from the read); a type parameter bound to a reference (1,841); a base beside a data member over the parameters (1,631); a data member of a type the binding does not carry (906).

## M8.3 slice 3b: a constructor made from the read records its members' constructors, and `map` is made from the read under -templates=subst (lane m83aa, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,301 of 2,184 (1,272; the probe's 8, 1,293 otherwise),
self-host 67,222 beside under the check (65,354); its 2,478 "class template-ids made first for a data member and a member function's signature" gone.

**The check first.** Under `-check=templates` a covered member body is replayed beside its read (`memberFrame`); the replay's
`constructMember` records what builds each class-typed member (`memberBuilt_`, `fn|m|0`), and nothing held the read's record
against it - the read made none, so with no check `map() {}`'s `tree_` (a `__tree` made first, with a written default
constructor) was "outside the SSA lowering: a member built by no constructor the parser recorded". Now `memberFrame` asks what
the read records (`Templates::membersBuilt`) and holds it against the replay's records key for key (`builtApart`; a base's `^`,
`~` and `$` keys are not compared); a difference prints "-check=templates failed: <where>: member 'm' built by a constructor the
read records none of" and fails the unit (the driver's `templatesFailed`). With the read recording none (a stub build over n2,
the tree as m83z left it) the check fails all nine units by name - the eight n2 broke (deque-both-ends, include-map-set,
map-move-only-mapped, map-set-comparator, map-set-node-stability, member-template-body-unchosen, range-for-vector,
return-self-referential) and the probe.
**The cause mended, one path with the replay's.** `constructMember`'s three decisions are functions both paths call:
`constructedClass` (the class whose constructor builds a member; none for a reference or a class with no constructor),
`noteChosen` (the chosen constructor noted used, through `noteUsed`) and `keepMemberBuilt` (the record; two choices under one key
none). A constructor made from the read (`madeMember`) records, in declaration order, each class-typed member the list leaves
to its class's default constructor and notes it used, as the replay does reading the body. What the read cannot carry replays
the body by name: a member in a union or an array or with an initialiser of its own; a default constructor taking defaults or
not public; and a member whose class has a destructor - the replay's constructor guards it while the body runs (a slot and an
LSDA), which the read's frame does not lay: with that member made from the read too, `identical.sh -templates=subst` differed on
the nine (18 files: `map() {}` with no landing pad), so `map`'s and `set`'s constructors replay and the classes are made from the
read. Then m83z's n2 (`boxkit/m83z-n2.patch`: a signature's ids placed ahead of or behind the data members' by where the body
declares each) is applied.
Probe `template-subst-memberctor` (`Map<K, V>`'s `Map() {}` leaving `Box<Pair<K, V> > box_` to `Box() : n(7)` - made from the
read; `Guarded<K, V>`'s `Guarded() {}` beside `Held held_` with a destructor and the ledger - replayed by name; 15 111 3 and LEDGER
0s plain, under the flag and the check; g++, clang on Windows and cxx3-msvc the same; 8 classes made from the read; REF e84c027f under the flag
refuses it, `Map`'s constructor "outside the SSA lowering" - the fault was there before n2, with no signature id; `.nonames` for
x86_64-linux's C1/D1 of the inline constructors and destructors). Found and owed: `Held<E>`'s `lfBuilt(this, "Held")` left the
class-members door unread with "argument 2 of '' is '' and this is ''", a diagnostic with its names empty. src 96,850 -> 96,947
(+97, n2's among them); four EXCLUSIONS citations re-pointed (ParserClass.cpp 2989 -> 3002, Templates.cpp 1981 -> 2031,
2033 -> 2083, 2730 -> 2780).
Census: no check nonzero 353 (n2 alone 361; the set n3's); under the check 0 failed, no class differ, nonzero 353, self-host
nonzero 0, 0 failed. Gates (the commit's tree, REF e84c027f): identical 0 differ on four targets plain (1948/3896/1948/1948) and with `-templates=subst` on three (1948/3896/1948: every case, `map`, `set` and the probe among them); subst.sh 0 failed, then run.sh 1319/0, then run.sh under the flag 1319/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and
under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++
-Werror clean; Windows cl /W4 /WX clean (full 16:42:23-16:46:01, ~3.6 min alone; the final delta 16:58:55-17:06:31, ~7.6 min,
the box shared); names.sh 974/0 and the final probe again (its .nonames), overload.sh 46/0.
**Next**: the largest reasons now, self-host under the check: a type parameter bound to a reference (1,841); a base beside a data member over the parameters (1,631); a data member of a type the binding does not carry (906). Owed: a member with a destructor made from the read - its constructor's guard (the slot and the LSDA the replay lays), so `map()`'s and `set()`'s bodies need no replay; the class-members door's empty diagnostic above.

## M8.3 slice 3b: a constructor made from the read guards a member whose class has a destructor, as the replay does, under -templates=subst (lane m83ab, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,308 of 2,184 (1,301; the probe's 7, 1,301 otherwise),
self-host 67,222 beside under the check (67,222). This commit raises member bodies, not classes: a covered member body made
with no replay, suite 475 (REF 447), its 19 "a class-typed member with a destructor, which the replay's constructor guards"
gone - `map()`'s and `set()`'s constructors among them - and with them m83aa's 18 differing files under the flag.
**What the replay lays and the read did not.** The replay's `assemble` puts each member whose class has a destructor under
`guardSubobject`: on Itanium `wrapCleanups` allocates the exception pointer's slot (an `a` step, after the body's steps and
before the frame's size) and both ABIs set the function's landing pads; the SSA lowering builds the guard itself from the
member's type. A read over the parameters sees no destructor on `_Rb_tree<$K, ...>`, so its frame had neither. Now
`membersBuilt` names each such member's destructor (public, not deleted, no virtual base, or the body replays by name);
`guardedFrame` adds one slot per guard to the read's steps before its `e` (none on Microsoft), `madeWithoutReplay` lays that
frame, marks the pads and notes the destructors used, last member first, as `assemble` does. A member whose class the read
saw as no parameter's (a plain `Held`) is guarded by the read itself: its slot is in the read's frame already, and its
pads replay the body (`madeFromRead`'s "an exception specification or landing pads"). Under `-check=templates` the guarded
frame is held against the replay's step for step and the pads against its pads; either apart fails the unit by name ("a
guarded member's frame apart", "... landing pads apart") - the first build counted `Held`'s slot twice and the check failed
`template-subst-memberctor` by name. The class-members door's diagnostic: the census's key drops quoted names (one reason
for every name); the place's own note (`-dump=syntax`'s unread) now keeps them.
Probe `template-subst-guardedmember` (`Map<K, V>` with `Cell<K>` and `Tag<V>`, each with a destructor, left to their default
constructors by `Map() {}` and `explicit Map(int t) { check(t); }`; a throw out of the body destroys both; `Guarded<K>` beside
a plain `Held` with the ledger replays by name; clang 51 154 8 / caught 7 / 20 3 and LEDGER 0s, cxx3-msvc the same, cxx3 the
same plain, under the flag and the check; `.nonames` for x86_64-linux's C1/D1). src 96,947 -> 96,981 (+34); three EXCLUSIONS
citations re-pointed (Templates.cpp 2780 -> 2811, 2083 -> 2114, 2031 -> 2062).
Census: no check nonzero 353 (the set before); under the check 0 failed, no class differ, nonzero 353; self-host nonzero 0,
0 failed. Gates (REF 131f5c7f): identical 0 differ on four targets plain (1950/3900/1950/1950) and with `-templates=subst`
on three (1950/3900/1950: every case, `map`, `set` and the probe among them); subst.sh 0 failed, then run.sh 1320/0, then
run.sh under the flag 1320/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up
to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag (the first build's
`vector::insert(pos, n, v)` is not in the library's subset: a loop); g++ -Werror clean; Windows cl /W4 /WX clean (full
~2.5 min alone; the final delta 17:50:43-17:54:42, ~4 min); names.sh 974/0, then the probe again under its .nonames 0 failed; overload.sh 46/0;
the final build (n3: the loop alone) runs the probe the same and self-compiles rc 0; every other gate ran on n2.
**Next**: the largest reasons, self-host under the check: a type parameter bound to a reference (1,841 - T bound to U & as
M8.2's boundClass binds it, with substituted's collapse; refuse a data member the binding makes a reference); a base beside
a data member over the parameters (1,631); a data member of a type the binding does not carry (906) and one of a class the read named over its parameters (906). Member bodies:
"a 'meminit' in the body" (7,734) and "a call with a type-dependent argument" (7,576) lead.

## M8.3 slice 3b: a type parameter bound to a reference, in a class made from the read under -templates=subst (lane m83ac, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,320 of 2,184 (1,308; the probe's 4 and eight of the suite's),
self-host 69,034 beside under the check (67,222). "A type parameter bound to a reference", the largest reason at 1,841, is
down to 1 (a reference to a class the read named over its parameters).
`classShape`'s binding loop took a scalar or a class (`boundClass`'s) and refused every reference. It now asks the same of
the referent: `T` bound to `U &` or `U &&` with U a scalar or a bound class is substituted as it is, and `substituted`
already collapses `T &`, `T &&` and `const T` over it as [dcl.ref]/1 and /6 say - the library's iterators
(`Iter<T, T &, T *>`, `R operator*()`) are this shape. Two shapes the replay builds otherwise are refused by name: a data
member the binding makes a reference ("a data member the binding makes a reference": `Box<int &>`'s `T v_`), and a `const`
or `volatile` over a parameter bound to a reference in a signature or a member typedef (`cvOverReference`: "a signature's
const over a parameter bound to a reference") - the replay keeps the qualifier on the reference (`const R` with R =
`const int &` reads as "const const int &"), so the out-of-line definition of a member declared by the read failed under
the flag on the first build ("was declared to return 'const int &' and this says 'const const int &'"). Neither refusal
occurs in the self-host. Not changed: the replay's own handling of `const` over a reference.
Probe `template-subst-refbound` (`Iter<int, int &, int *>`, `Iter<int, const int &, const int *>`, `Iter<Pt, Pt &, Pt *>` and
`Box<int>` made from the read; `Peek<int &>`'s `const R` and `Box<int &>` replayed by name; clang 28 4 40 10 / 10 7 12 12,
cxx3-msvc the same, cxx3 the same plain, under the flag and under the check; `.nonames` for x86_64-linux's C1). src 96,981
-> 96,999 (+18); no EXCLUSIONS line moved.
Census: no check nonzero 353 (the set before); under the check no class differ, nonzero 353 (the three frame lines
`template-subst-mcall`, `template-subst-member` and `thread-pool` print are REF's too); self-host nonzero 0. Gates (REF
5ab642ea): identical 0 differ on four targets plain (1952/3904/1952/1952) and with `-templates=subst` on three
(1952/3904/1952: every case and the probe among them); subst.sh 0 failed, then run.sh 1321/0, then run.sh under the flag 1321/0; static rc 0 (exclusions 0/0, comments 0,
memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++
-Werror clean; Windows cl /W4 /WX clean (full 18:00:52-18:08:02 on the first build, ~7 min, the box shared; the final delta
18:10:04-18:14:09, ~4 min); names.sh 976/0 and the probe under its .nonames 0 failed; overload.sh 46/0.
**Next**: the largest reasons, self-host under the check: a base beside a data member over the parameters (1,631 - the
parser's base layout made one shared path, m83y); a data member of a type the binding does not carry (906) and one of a
class the read named over its parameters (906).

**M8.3 slice 3b, a base beside a data member over the parameters, 2026-11-24 (lane m83ad, from f21e8b73).** The measure:
class specializations made with no replay - suite 1,346 of 2,184 (1,320; the probe's 5), self-host 71,081 beside under the
check (69,034). The parser's layout of a class's non-virtual bases is one function now, `Parser::layBases` (ParserType.cpp):
the body `structOrUnionSpecifier` reads calls it over its lay order, and `Templates::laidFromRead` calls it over the read's
bases (non-virtual, direct, none polymorphic: classShape's), then lays the read's own data members under the binding from
the cursor it hands back, stepping an empty subobject of their type past one already there on Itanium (`emptyClash`,
`addEmpties`: the parser's conflict list, which was a local struct, is the one both paths ask) and carrying cl's
zero-sized flags as the parser's member loop does. So classShape no longer refuses "a base, beside a data member over the
parameters", and laidFromRead no longer takes the read's own layout whole for a class with bases. `Packed` moved to file
scope with it. classLaid holds the data size and cl's two zero-sized flags against the replay's from the laid record
(completeClass's formula), where it held the read's data size. Not changed: the parser's virtual bases, vptr and vbptr
placement and its member loop; a class with a virtual or polymorphic base or a base over the parameters still replays by name.
Probe `template-subst-basebeside` (`One<int>`, `One<E>` with E both base and member, `Two<double>`'s array, `Pad<char>` in
Tail's tail padding on Itanium, `Mix<E, char>` over two bases, each held against the same class written out; clang 10 1 1 /
10 1 1 / 10 7 1 1 / 33 1 1 1, cxx3-msvc the same, cxx3 the same plain, under the flag and under the check, REF replays all
five by name; `.nonames` for x86_64-linux's C1 of Pad<char>'s and PadC's implicit constructors, template-subst-plainbase's
difference). src 96,999 -> 97,053 (+54); 25 EXCLUSIONS citations re-pointed (ParserType.cpp's lines moved; one into layBases).
Census: no check nonzero 353 (the set before); under the check no class differ, nonzero 353; self-host nonzero 0; "a base, beside a data member over the parameters" 1,631 -> 0.
Gates (REF f21e8b73): identical 0 differ on four targets plain (1954/3908/1954/1954) and with `-templates=subst` on three
(1954/3908/1954: every case and the probe among them); subst.sh 0 failed, then run.sh 1322/0, then run.sh under the flag 1322/0;
static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile
Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (the delta
18:42:45-18:46:26, ~3.7 min, and again 18:49-18:52 on the final tree; the first full build failed on `Packed`, fixed);
names.sh 977/0 and the probe under its .nonames 0 failed; overload.sh 46/0.
**Next**: the largest reasons, self-host under the check: a data member of a type the binding does not carry (906), a data member of a class the read named over its parameters (906), and a signature over a member type of another class over its parameters (906, before the replay) - one class template, by their equal counts; read which before choosing.

**M8.3 slice 3b, a pointer to the class itself, and a table its signatures name, 2026-11-24 (lane m83ae, from ed7f2e8b).** The measure:
class specializations made with no replay, suite 1,360 of 2,184 (1,346; the probe's 10), self-host 71,987 beside under the check
(71,081). A debug build (boxkit/m83ae-Tdbg.cpp: each class's tag, reason and place under CXX1_TDBG) named the three 906s:
`__hash_node<pair<const K,V>>` ("a data member of a type the binding does not carry": `__hash_node *next`, the read's pointer
to its own class) and `unordered_map<K,V,...>` ("a data member of a class the read named over its parameters", `table t_`,
after idsBefore stopped on "a member function's signature over ... a member type of another class over its parameters":
`iterator erase(const_iterator)` over `typename table::const_iterator`, whose `table` the data member had already wanted but
whose arguments earlyArgs asks carried). Now classShape passes a data member that points (through pointers and arrays) at the
read class or the class readStands takes for it, and classFromRead lays the data members, and the door's classLaid holds
them, under readStands's map, so the pointer is to the class being made, as the replay's is; a data member's or a signature's
`X<$T0>` (or `X<$T0>::m`) already wanted is not wanted twice nor refused - idsBefore makes it where the data member placed it,
and the second pass finds it carried. Not changed: the order of wanted ids, earlyArgs, a by-value member of the class itself
(incomplete, refused by name). Probe `template-subst-selfnode` (Node's `next`, Table's `node *head_`, Map's `table t_`
with `typename table::iterator` signatures; clang 2 4 60 / 1 97 1, cxx3-msvc the same, cxx3 the same plain, under the flag
and under the check, all ten classes made from the read; a braced list in a held body first met the class-members door's owed
"'' is full, and there are 2 more initialiser(s)", so the probe assigns). src 97,053 -> 97,068 (+15); no EXCLUSIONS line moved.
Census: no check nonzero 353; check no class differ, nonzero 353; self-host nonzero 0; `__hash_node`'s 906 now equal. The self-host's classes still replayed, by held reason: a data member of a class the read named over its parameters 906 (unordered_map, now stopped before the replay on "a member function's signature over a class template-id over dependent arguments" - likely `pair<iterator, bool> insert`, over `typename table::iterator`: the next), a pack or template parameter 401, a non-type parameter 259, a type parameter bound to a class the read named over its parameters 252, a static data member 216, a base named through a parameter 215, bound to a specialization not completed 76, bound to a type no scalar, class, reference or array 49, a member enumeration's data member 25, the rest 4 (2,403 in all). The suite's 824 classes still replayed,
by reason (the class door's held reason under the check): a non-type parameter 458, a static data member 183, a pack or template
parameter 109, a type parameter bound to a class the read named over its parameters 46, a base named through a parameter 40,
a type parameter bound to a type no scalar, class, reference or array 24; the rest 1 to 6 each.
Gates (REF ed7f2e8b): identical 0 differ on four targets plain (1956/3912/1956/1956) and with `-templates=subst` on three
(1956/3912/1956: every case and the probe among them); subst.sh 0 failed (its first run raced the probe's ship and met the
braced list above; re-run on the final probe: run 1323/0 at -O0 and -O2, typed 970/0, ceiling 17 of 17), run.sh 1323/0, then
run.sh under the flag 1323/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to
date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX
clean (full 19:21-19:25, ~3.8 min alone, then two deltas); names.sh 977/0 beside the probe, and the probe under its
`.nonames` (x86_64-linux's C1/D1 of inline special members) 0 failed; overload.sh 46/0. A box defect: the first builds of
REF and the debug tree passed build.sh's 540 s cap at 81 objects on a box at load 6 (a gate beside), and make resumed.
**Next**: unordered_map's 906, stopped before the replay on a signature over a class template-id over dependent arguments
(likely `pair<iterator, bool> insert`); then the suite's non-type parameters (458) and static data members (183).

## M8.3 slice 3b: a signature's class template-id over a member type of a table made first, under -templates=subst (lane m83af, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,368 of 2,184 (1,360; the probe's 6, `map-move-only-mapped`'s
1, `unordered-map`'s 1), self-host 72,046 beside under the check (71,987). A debug build (boxkit/m83ae-Tdbg.cpp's two lines)
confirmed m83ae's guess: `unordered_map<string,unsigned,...>` stopped before the replay on "a member function's signature over a
type the binding does not carry: a class template-id over dependent arguments" - `pair<iterator, bool> insert(const value_type &)`,
whose `iterator` is `typename table::iterator` and whose `table` (`__hash_table<...>`) the typedef and the data member `t_` had
already wanted. earlyArgs asked each argument carried; now an argument that is a member type of a class already wanted
(`table::iterator`) is made after it, so idsBefore makes `pair<iterator, bool>` through makeClass behind the table, in the body's
order, with the member found in the table it just made. Not changed: the order of wanted ids, a signature's id over an id not yet
wanted (refused by name as before), makeClass. Probe `template-subst-pairiter` (`Set<K>` with `typedef Table<K> table`,
`typename table::iterator` and `Pair<iterator, bool> insert`; clang 7 9 1 2 1 / x 1 on Windows, cxx3-msvc the same, cxx3 the same
plain, under the flag and under the check; all six classes equal and made from the read, where REF made 4 and stopped Set<int> and
Set<char> on that reason). src 97,068 -> 97,070 (+2); no EXCLUSIONS line moved.
Census: no check nonzero 353; check no class differ, nonzero 353 (the frame and member `differ` lines are REF's, unchanged);
self-host nonzero 0. unordered_map's 906 stops are 577 now: `unordered_map<unsigned,...>` in Parser.h (575, 594, 635 and
beside) still stop on the same reason - its table not wanted ahead of the signature, or carried by the program before it (the
debug build dbg2 over src/sema/Classes.cpp names them: the next). Self-host still replayed by held reason: a data member of a
class the read named over its parameters 577, a pack or template parameter 401, a non-type parameter 259, a type parameter bound
to a class the read named over its parameters 252, a static data member 216, a base named through a parameter 215, bound to a
specialization not completed 76, bound to a type no scalar, class, reference or array 49, a member enumeration's data member 25.
Gates (REF 0e429405): identical 0 differ on four targets plain (1958/3916/1958/1958) and with `-templates=subst` on three
(1958/3916/1958: every case and the probe among them); subst.sh 0 failed, run.sh 1324/0, run.sh under the flag 1324/0; static
rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and
ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 19:54-19:57, ~3.5 min alone);
names.sh 979/0 with the probe; overload.sh 46/0.
**Next**: the 577 `unordered_map<unsigned,...>` (dbg2 over Classes.cpp); then non-type parameters (suite 458, self-host 259):
`Substituted` carrying the binding's values so `T a[N]` (an array whose boundParam is N) lays as `arrayOf(T, values[N])`,
classShape's refusal at its first loop lifted for an int parameter, and the read's own `array<T, N>` ids keyed by value.

## M8.3 slice 3e, first part: a class over a non-type parameter named in member bodies or as a bound `[N]`, under -templates=subst (lane m83ag, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,436 of 2,184 (1,368; the probe's 8), self-host 72,046
beside under the check (72,046, unchanged: 245 of the self-host's 259 name the value outside member bodies, the other 14 stop on a later reason). classShape refused every non-type parameter at its first loop. Now it scans the class's own
tokens (its head and body, a member function's body skipped to its `}`): a non-type parameter named nowhere else, or only as a
bound `[N]`, is no refusal. The read keeps such a bound as the parameter (`Type::boundParam`, arraySuffix's pattern branch), so
`Substituted` carries the class's values by index (makeClass's `useValues`, through idsBefore, classBefore and the two classShape
calls) and `substituted` lays `T a[N]` (and `T m[R][C]`) as `arrayOf(T, values[N])`; the layout is laidFromRead's, held by
classLaid. A member body holding a value-dependent name (`syntax::ValueDependent`) replays (memberShape: "a non-type parameter's
value, which the read held as its stand-in"), under the real values as every replayed body does: without it a body typed from
the read took N's stand-in 0 (the first build printed `0 1 0 1` for `15 4 8 3`, and -check=templates did not see it: it holds
layouts, symbols and shapes, not a body's constants - owed). A class with a member template beside a non-type parameter replays
(templatesHeld holds the read's stand-in class values against the real ones: 4 `differ`s in the first build). Not changed: a
non-type parameter named anywhere else (a signature, a typedef, a base, `[N + 1]`, `X<N>`) replays by name; packs and template
parameters; function templates' valueShape. Probe `template-subst-valueparam` (an int, a bool and an enumeration parameter named
in bodies; `T items[N]`; `T m[R][C]`; clang 19 on Windows 15 4 8 3 / 1 2 4 / 33 79 4 / 4 9 16 / 6 5 12, cxx3-msvc the same,
cxx3 the same plain, under the flag, under the check and REF; its 8 classes made from the read and `equal`).
src 97,070 -> 97,097 (+27); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2062/2114/2811 -> 2064/2116/2813).
Census: no check nonzero 353; check no class differ, nonzero 353 (the frame and function-door `differ` lines are REF's); self-host
nonzero 0. The suite's non-type classes still replayed: named outside its member bodies 343 (of 458), a member template
beside one 4; member bodies replayed for a value 50.
Gates (REF cead7672): identical 0 differ on four targets plain (1960/3920/1960/1960) and with `-templates=subst` on three (1960/3920/1960: every case and the probe among them); subst.sh 0 failed, run.sh 1325/0, then run.sh under the flag 1325/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 20:28-20:35, ~7 min with the box shared, then two deltas ~2 min each); names.sh 980/0 with the probe; overload.sh 46/0; the probe by clang 19 and cxx3-msvc on Windows the same.
**Next**: the 343 - a non-type parameter in a signature, a typedef or a base (`X<N>` ids keyed by value, `[N + 1]` folded
under the values; a bare `[N]` there is taken now through `substituted`, which no probe holds yet), and member bodies folded under the values (valueShape) rather than
replayed; then the 577 `unordered_map<unsigned,...>` in Parser.h (not reached this hour).

## M8.3: -check=templates holds a made body's constants against its replay's (lane m83ah, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,436 of 2,184 (1,436, unchanged), self-host 72,046 beside
under the check (72,046, unchanged). Not a raise: the oracle the next one needs. m83ag's first build typed member bodies from
the read with N's stand-in 0 and printed `0 1 0 1` for `15 4 8 3`, and the check passed it: it held frames, shells, symbols and
layouts, not what a body folds. Now `constantsHeld`, after every covered body's replay under -check=templates (the member path in
memberFrame, the function path beside layFrame), walks the read's `function` node and the replay's root together, node for node
where kind and arity agree (a shape apart is the body check's), and holds each node's constant as the Typer reads it under the
binding (`constantOf`): 2f's `Bound::values` first, then a sizeof's or alignof's (`keywordValues_`), a case label's, an
enumerator's or a folded static member's value, and an array local's bound under the binding; the first difference fails the
unit by name (`-check=templates failed: <place>: a constant: 'N' 0 for the replay's 3`) and is counted `made (n): constants:
differ: ...`, the rest `constants: equal`. Shown on m83ag's mistake re-created (memberShape's value-dependent replay taken out,
a throwaway build): under the flag the probe printed `0 1 0 1 / 2 2 4 / 33 79 4 / 0 9 16 / 0 5 12`, and the check failed the
unit, rc 1, on seven bodies of `template-subst-valueparam` (`N` 0 for 3, 2 and 4; `B` 0 for 1; `C` 0 for 3); the real build
holds the probe right four ways, rc 0 under the check. Not changed: what is made from the read, the census of classes, members
replayed for a value (memberShape).
src 97,097 -> 97,156 (+59); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2064/2116/2813 -> 2119/2171/2868).
Census: no check 1332 units, nonzero 353, classes made with no replay 1,436 (unchanged); check nonzero 353, no class differ,
`constants: equal` 1,130 bodies, 0 differ, 0 units failed; self-host nonzero 0, `constants: equal` 11,621, 0 failed.
Gates (REF e6719501): identical 0 differ on four targets plain (1960/3920/1960/1960) and with `-templates=subst` on three (1960/3920/1960); subst.sh 0 failed, run.sh 1325/0, then run.sh under the flag 1325/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full, 21:12-21:14, ~2 min); names.sh 980/0; overload.sh 46/0; the probe by clang 19 and cxx3-msvc on Windows the same.
**Next**: with the constants held, the suite's 343 non-type classes named outside member bodies (a signature, a typedef or a
base: `X<N>` ids keyed by value, `[N + 1]` folded under the values), and member bodies with a value-dependent name folded under
the values (valueShape, its `Bound::values` now held by this check) rather than replayed; then the 577 `unordered_map<unsigned,...>`.

## M8.3 slice 3f: a class over a non-type parameter named in its own id or as another template's id argument, under -templates=subst (lane m83aj, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,445 of 2,184 (1,436; the probes' 6, 3 more), self-host 72,055 beside under the check
(72,055). classShape's scan, which refused a non-type parameter named anywhere outside member bodies but a bound `[N]`, now takes
it in two more places. (a) The primary's own id over its parameters in order (`V<N> &fill`, `V<N> twice()`, `const V<N> &`;
`Box<T, N> &set`), matched against the read's own key (`V<$P0>`): the read class itself, which readStands maps to the class
made. (b) A whole argument of another class template-id (`V<N> r[M]`, `const V<N> &row`): `argValued` gives the class's `$P<i>`
its value under `s` in `specializationUnder` and `earlyArgs` (which refused every non-type parameter), and idsBefore and
idsInside make such an id under those values (both passed zeros). A written value is still refused: a read class's own
arguments are its stand-ins (`array<$T0,$P1>$read` carries `0` for N) - the build that took them made `V<0>` and
`array<int,0>` inside the class (4 units failed under the flag, not under the check, which replays beside) - and the build that
took them off read classes alone left one class differ in thread-pool (its census before the replay and after it apart: a
member typedef over a type the binding does not carry); refused, both are gone. An expression argument (`V<N + 1>`) replays.
Probes template-subst-ownvalueid (clang `6 12 0 12 / 5 16`; its first form, an out-of-line constructor `V<N>::V(int)`, met on
x86_64-linux the C1-beside-C2 difference names.sh records elsewhere, so it is `fill`) and template-subst-valueid
(`12 6 24 / 9 x 20`), right plain, under the flag, under the check and by REF.
src 97,156 -> 97,200 (+44); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2119/2171/2868 -> 2130/2182/2879).
Census (n3, and n4 for the refused written values, the commit's semantics; n5's own census was still running at the hour): no check 1,334 units, nonzero 353 (the same set), classes 1,445; check nonzero 353, `constants: equal` 1,135, 0 units failed, no class differ (n4); self-host (n3) nonzero 0, no class differ, `constants: equal` 11,621, its class lines equal to m83ah's (72,055 unchanged: the self-host's 259 name the value in bases and expressions).
Gates (REF f1ca11db), on n5 (the commit's build): subst.sh 0 failed, run.sh 1327/0, then run.sh under the flag 1327/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); Windows cl /W4 /WX clean (full, ~3 min); on n3 (n5 less the written values): identical 0 differ on four targets plain (1964/3928/1964/1964), with `-templates=subst` x86_64-linux 1964/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; Windows names.sh 981/1 (the probe's first form, C1 beside C2 on x86_64-linux), overload.sh 46/0, both probes by clang 19 and cxx3-msvc the same. Owed to the wave gate: n5's identicals, its census and self-host census, names.sh and overload.sh on n5 (running at the hour).
**Next**: a non-type parameter in a base (`Q : V<I>`, refused as a base over its parameters) and in a typedef's or a bound's
expression (`[N + 1]`, `X<N + 1>`, folded under the values), member bodies folded under the values (valueShape) rather than
replayed; then the 577 `unordered_map<unsigned,...>`.

## M8.3 slice 3g: a class over a non-type parameter named in a data member's bound expression, under -templates=subst (lane m83al, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,451 of 2,184 (1,445; the probe's 6, no other unit has the shape), self-host 72,368 of 74,390 beside under the
check (72,055). classShape's scan takes a non-type parameter in one more place: a data member's first bound in the class's own
braces (`T slot[N + 1];`, `char cells[R * C + (R - 1)];`, `int w[(N + 1) / 2], h;`), which the read laid with no bound (the
member as nothing, ParserType's arraySuffix). `foldBound` folds the bound's tokens as int under the class's values (its integral
non-type parameters, int literals, `+ - * / %`, parentheses; anything else, a fold past int or a bound not positive replays),
`Substituted::bounds` keeps the value by the member's name, and `dataUnder` lays that member as the array of it wherever the
read's data members are taken under `s` (classShape's member loop, laidFromRead, so classLaid holds it under the check). A bound
inside parentheses (a parameter's), a typedef's or a static member's, one in a nested class, and one another dimension follows replay as
before; an expression argument of an id (`V<N + 1>`) and a base (`Q : V<I>`, laid by the read as nothing) still replay.
Probe template-subst-valuebound (clang 19 `3 16 13 20 / 1 16 1.5 24 / 7 10 1 4 11 / 3 16 1 8 15`), right plain, under the flag, under the check and by REF; its 6 classes made from the read, 6 `equal` under the check.
The suite moves by its probe alone (no other unit declares such a bound); the self-host's 72,368 is over n1's own sources, whose Templates.h names a new `std::map<std::string, long long>` in every includer, so it is not like for like with 72,055 (n0 over e2701905's sources). The fold is the one the next step (`X<N + 1>`, a typedef's bound) takes.
src 97,200 -> 97,262 (+62); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2130/2182/2879 -> 2164/2216/2913).
Owed by m83aj, on its final build e2701905 against REF f1ca11db: identical 0 differ plain x86_64-windows 3928, arm64-darwin 1964, tms6747 1964, and with `-templates=subst` x86_64-linux 1964, x86_64-windows 3928, arm64-darwin 1964; no-check census nonzero 353, classes 1,445; check census nonzero 353, `constants: equal` 1,135, no class differ; self-host census nonzero 0, 72,055 beside, `constants: equal` 11,621.
Census (n1, this commit's build): no check 1,335 units, nonzero 353 (the same set), classes 1,451; check nonzero 353, `constants: equal` 1,135, no class differ; self-host nonzero 0, no class differ, `constants: equal` 11,625, 72,368 beside.
Gates (REF f1ca11db), on n1: subst.sh 0 failed, then run.sh 1328/0; identical 0 differ plain x86_64-linux 1966, x86_64-windows 3932, arm64-darwin 1966, tms6747 1966; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; Windows cl /W4 /WX clean (full, alone, ~3.8 min), names.sh 983/0, overload.sh 46/0, the probe by clang 19 and cxx3-msvc the same. Owed to the wave gate (running at the hour): run.sh under the flag, identical with `-templates=subst` on three targets.
**Next**: an expression argument of a class template-id (`X<N + 1>`: argValued folding the read's `DepExpr` - Literal, TemplateParam, the arithmetic Operators - keyed by the value) and a typedef's bound; a base over the parameters (`Q : V<I>`, which the read lays as nothing: the head's id made first under the values and laid by layBases); member bodies folded by valueShape rather than replayed; then the 577 `unordered_map<unsigned,...>`.

## M8.3 slice 3h: a class over a non-type parameter named in an expression argument of another template's id, under -templates=subst (lane m83an, 2026-11-24)

classShape's scan takes a non-type parameter inside a whole argument of a class template-id that foldBound folds under the class's values (`V<N + 1> more;`, `V<N * 2 - 1> odd;`, `const V<N + 1> &get() const`, `V<(M * N) % 5 + 1> cells;`, `V<M / 2> half;`): idArg walks out from the parameter, through enclosing parentheses, to the argument's `<` or `,` and its `>` or `,`, and the folded tokens must be the whole argument.
The read keeps such an argument as its `DepExpr` (`dependentValue`, `expr`); argValued folds it under `s.values` (foldArg: an `int` literal, a non-type parameter by index, `pl mi ml dv rm`, `ng ps`; int range, no negative result, no division by zero or of a negative), so specializationUnder, earlyArgs and idsBefore key and make the id by its folded value (`V<4>`), as the replay's constantExpression does.
A typedef's bound, a base over the parameters (`Q : V<I>`, laid by the read as nothing) and member bodies folded by valueShape still replay.
Probe template-subst-valueexpr (by hand and clang 19 `7 9 36 20 / q 11 12 20 / 1 9 12`), right plain, under the flag, under the check and by REF; its 8 classes made from the read, 8 `equal` under the check (n1, which stopped the walk at an enclosing parenthesis, made 7: `Pair<char, 3, 4>` replayed).
src 97,262 -> 97,287 (+25); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2164/2216/2913 -> 2180/2232/2929).
The measure: class specializations made with no replay - suite 1,459 of 2,184 (1,451; the probe's 8, no other unit has the shape), self-host 72,368 of 74,390 beside under the check (72,368, unchanged: no self-host source names an expression argument; like for like, no header changed).
Census (n2, this commit's build): no check 1,336 units, nonzero 353 (the same set), made with no replay 1,459; check nonzero 353, `constants: equal` 1,135, no class differ; self-host nonzero 0, no class differ, `constants: equal` 11,626, 72,368 beside. The suite's units still refused for "a non-type parameter named outside its member bodies" are the library's headers (integral_constant's `T v` in a static member and its kin).
Gates (REF 1f1cc4be), on n2: subst.sh 0 failed, then run.sh 1329/0, run.sh under the flag 1329/0 (973 printed their recorded output, 352 refused as recorded, 0 failed); identical 0 differ plain x86_64-linux 1968, x86_64-windows 3936, arm64-darwin 1968, tms6747 1968, and with `-templates=subst` x86_64-linux 1968, x86_64-windows 3936, arm64-darwin 1968; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full, ~3.3 min, beside n1's names run), names.sh 984/0, overload.sh 46/0, the probe by clang 19 and cxx3-msvc the same.
**Next**: a base over the parameters (`Q : V<I>`): ParserType.cpp's read leaves it out with no record, so the read must keep each left-out base's type and access by the class's start, for classShape (made first under the values), laidFromRead's layBases and classFromRead's addBase; a typedef's bound (`typedef T R[N + 1]`; an array type is interned, so it is keyed by the typedef's name, not its type); member bodies folded by valueShape rather than replayed; then integral_constant's value-named static member.

## M8.3 slice 3c, static data members: a class template's static data members declared from the read under the binding, under -templates=subst (lane m83ai, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,484 of 2,184 (1,436; the probe's 5), self-host 72,046
beside under the check (72,046, unchanged: its 216 classes with a static data member now stop on another of the shape's reasons,
asked after the binding now; the suite's pack or template parameter count rose 109 -> 136 the same way). classShape refused every read class with a static data member.
Now it asks staticShape, after the binding is mapped: a static data member is declared from the read by staticsFromRead, which
puts the parser at the member's declarator in the class's own braces (staticAt) under the bound parameters, in the class's scope
and namespace, and calls `Parser::declareStaticMember` - the replay's own path, so its type is the read's under `substituted`,
its symbol `staticMemberSymbol` on both ABIs, and a value written in the class (`static const int size = sizeof(T);`,
`twice = size * 2`) is evaluated there as the replay evaluates it. Its out-of-line definition is instantiated on an evaluated
use by `instantiatePending`'s `outOfLine` loop ([temp.inst]/3, `usedStaticMembers_`), unchanged and the same for both doors
(the definition is the replay's, never a copy: its symbol and section as before). Still replayed, by name: a `constexpr` static data member,
one whose type the binding does not carry or is a class the read named over its parameters (or the class itself), one whose
initialiser names a template (a class made out of the body's order), one its braces do not declare by name; a member body
naming a static member of its own class replays as before (memberShape). Under -check=templates, staticsHeld declares the
read's members on a class of their own in the replay's class's scope and holds each one's name, type, access, symbol, value,
`thread_local` and `volatile` against the replay's (classLaid no longer says "the replay's class has a static data member").
Probe `template-subst-staticdata` (a counter, folded values over `sizeof(T)` and over another static, an array and a class-typed
static defined out of line, a pointer; clang 19 on Windows 2 1 4 2 / 3 0 8 1 7 / 121 / 11 9 1, cxx3-msvc the same, cxx3 the same plain, under the flag, under the check and REF; its 5 classes made from the read and `equal`).
src 97,097 -> 97,211 (+114); no EXCLUSIONS line moved.
Census: no check nonzero 353; check `equal` 1,663, no class differ, nonzero 353 (the frame and function-door `differ` lines are REF's); self-host nonzero 0. Still replayed in the suite: a non-type parameter named outside its member bodies 347, a pack or template parameter 136, a type parameter bound to a class the read named over its parameters 46, a base through a parameter 40, bound to a type that is no scalar, class, reference or array 33, a constexpr static data member 1.
Gates (REF e6719501): identical 0 differ on four targets plain (1962/3924/1962/1962) and with `-templates=subst` on three (1962/3924/1962: every case and the probe among them); subst.sh 0 failed, run.sh 1326/0, then run.sh under the flag 1326/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 21:17-21:26, ~9 min with the box shared - at the wait's cap, the remote build linked; Templates.cpp alone again 9 min); names.sh 981/0 with the probe; overload.sh 46/0.
**Next**: (2), not reached this hour: the next largest reason one fix covers is the self-host's 577 `unordered_map<unsigned,...>` (Parser.h), "a member function's signature over a class template-id over dependent arguments" (one shape, 577 classes); in the suite the non-type 347 are lane m83ah's, then a pack or template parameter bound by name 136.

## M8.3 slice 3f: a class over a non-type parameter named in its own id or as another template's id argument, under -templates=subst (lane m83aj, 2026-11-24)

**The measure**: class specializations made with no replay - suite 1,445 of 2,184 (1,436; the probes' 6, 3 more), self-host 72,055 beside under the check
(72,055). classShape's scan, which refused a non-type parameter named anywhere outside member bodies but a bound `[N]`, now takes
it in two more places. (a) The primary's own id over its parameters in order (`V<N> &fill`, `V<N> twice()`, `const V<N> &`;
`Box<T, N> &set`), matched against the read's own key (`V<$P0>`): the read class itself, which readStands maps to the class
made. (b) A whole argument of another class template-id (`V<N> r[M]`, `const V<N> &row`): `argValued` gives the class's `$P<i>`
its value under `s` in `specializationUnder` and `earlyArgs` (which refused every non-type parameter), and idsBefore and
idsInside make such an id under those values (both passed zeros). A written value is still refused: a read class's own
arguments are its stand-ins (`array<$T0,$P1>$read` carries `0` for N) - the build that took them made `V<0>` and
`array<int,0>` inside the class (4 units failed under the flag, not under the check, which replays beside) - and the build that
took them off read classes alone left one class differ in thread-pool (its census before the replay and after it apart: a
member typedef over a type the binding does not carry); refused, both are gone. An expression argument (`V<N + 1>`) replays.
Probes template-subst-ownvalueid (clang `6 12 0 12 / 5 16`; its first form, an out-of-line constructor `V<N>::V(int)`, met on
x86_64-linux the C1-beside-C2 difference names.sh records elsewhere, so it is `fill`) and template-subst-valueid
(`12 6 24 / 9 x 20`), right plain, under the flag, under the check and by REF.
src 97,156 -> 97,200 (+44); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2119/2171/2868 -> 2130/2182/2879).
Census (n3, and n4 for the refused written values, the commit's semantics; n5's own census was still running at the hour): no check 1,334 units, nonzero 353 (the same set), classes 1,445; check nonzero 353, `constants: equal` 1,135, 0 units failed, no class differ (n4); self-host (n3) nonzero 0, no class differ, `constants: equal` 11,621, its class lines equal to m83ah's (72,055 unchanged: the self-host's 259 name the value in bases and expressions).
Gates (REF f1ca11db), on n5 (the commit's build): subst.sh 0 failed, run.sh 1327/0, then run.sh under the flag 1327/0; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); Windows cl /W4 /WX clean (full, ~3 min); on n3 (n5 less the written values): identical 0 differ on four targets plain (1964/3928/1964/1964), with `-templates=subst` x86_64-linux 1964/0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; Windows names.sh 981/1 (the probe's first form, C1 beside C2 on x86_64-linux), overload.sh 46/0, both probes by clang 19 and cxx3-msvc the same. Owed to the wave gate: n5's identicals, its census and self-host census, names.sh and overload.sh on n5 (running at the hour).
**Next**: a non-type parameter in a base (`Q : V<I>`, refused as a base over its parameters) and in a typedef's or a bound's
expression (`[N + 1]`, `X<N + 1>`, folded under the values), member bodies folded under the values (valueShape) rather than
replayed; then the 577 `unordered_map<unsigned,...>`.


## M8.3 slice 3d: a dependent member keyed by its owner, so two reads' `pair<table::iterator, bool>` are two pattern ids, under -templates=subst (lane m83ak, 2026-11-24)

Found by three debug builds (boxkit/m83ak-dbgline.txt, m83ak-dbg2.sh, m83ak-dbg3.sh over Templates.cpp): the self-host's
`unordered_map<unsigned,...>` of Parser.h stopped on `pair<table::iterator, bool> insert` because the `table` that
`iterator`'s owner named was `unordered_set`'s (`__hash_table<$T0,$T0,__unordered_set_key<$T0>,...>`), not its own -
keySpelling spelled every dependent member as `dependent member type`, so `pair<table::iterator, bool>` read in
`<unordered_set>` and in `<unordered_map>` was one shallow class, and readIds_ kept the last read's arguments. Not the
program's `pair<const unsigned,V>` (a map over the same arguments first, as members, at namespace scope: all `equal`).
keySpelling now spells a dependent member as its owner's key and `::name`; a real specialization's key has none.
Probe `template-subst-twotables` (two class templates each naming `Pair<iterator, bool>` over their own table's
`table::iterator`; 7 9 1 2 0 by hand, cxx3 the same plain, under the flag and under the check; its 6 classes `equal`, REF 5
and one stopped on "a class template-id over dependent arguments").
src 97,211 -> 97,212 (+1); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2814, 2117, 2065).
The measure: class specializations made with no replay - suite 1,502 of 2,184 (1,484; the probe's 6), self-host 72,623 of
74,390 beside under the check (72,046: the 577 `unordered_map<unsigned,...>` of Parser.h).
Census: no check nonzero 353; check `equal` 1,671, no class differ, nonzero 353; self-host nonzero 0, `equal` 73,095.
Gates (REF a8dd1cd3): identical 0 differ on four targets plain (1966/3932/1966/1966) and with `-templates=subst` on three
(1966/3932/1964); run.sh 1328/0, then run.sh under the flag 1328/0 (the two counted beyond the probe's: a set-aside probe rsync
left in the box tree); subst.sh: its one failure is that set-aside probe's stale output in tests/out-subst (an unread reason of
`G::get(...)`), removed after the run - owed: subst.sh once more on a clean tree (the wave gate); static rc 0 (exclusions 0/0
after the re-pointing, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and
ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full ~3.5 min), names.sh 982/0
with the probe, overload.sh 46/0, clang 19 and cxx3-msvc on the probe 7 9 1 2 0.
**Next**: the suite's "a pack or template parameter, bound by name" (136), not reached this hour; the self-host's next
largest after this: a pack or template parameter 401, non-type 259, bound to a class over its parameters 252.

## M8.3 slice 3i: a class whose static data member's initialiser names a non-type parameter, and a member typedef of its own class, under -templates=subst (lane m83ap, 2026-11-25)

The branch first merges main (b4781f0c, 5a550bb2): this work needs slice 3c's staticShape, staticsFromRead and staticsHeld (lane m83ai), which 96d07af4 lacked; only this file conflicted. The merge is REF for every gate below; its census is the base: 1,507 of 2,184.
classShape's scan takes a non-type parameter in a static data member's initialiser in the class's own braces (`static const T value = v;`, `static const int size = N * 2 + 1;`, `static const bool big = (N > 3);`, `static_cast<T>(N + 1)`): staticInit walks back from the parameter to `static` past an `=` at its own parenthesis level, never through `;`, braces, `:`, `typedef`, `using` or `enum`, and never out of a parenthesis the `=` is inside (a parameter's default argument `f(int a = N)` replays). staticsFromRead declares the member under the bound values, so its value is the replay's, and staticsHeld holds it under the check.
That alone moved integral_constant to fromReadWhyNot's "a member typedef or alias naming its own class" (176 classes, n1: 1,529). The refusal is gone: classFromRead declares the read's member typedefs under readStands's map (the read class is the class being made), so `typedef integral_constant<T, v> type;` and `typedef const Sized *pointer;` name the made class, as the replay's do; classSkip held them already (typedefsCarried passes the read class).
A base over the parameters (`Q : V<I>`), a typedef's bound and member bodies folded by valueShape still replay.
Probe template-subst-staticvalue (by hand and clang 19 `7 7 q 1 / 5 11 0 1 15 / 20 20 4 / 1 0`), right plain, under the flag, under the check and by REF; its 8 classes made from the read, 8 `equal` under the check.
src 97,401 -> 97,410 (+9); no EXCLUSIONS line moved.
The measure: class specializations made with no replay - suite 1,705 of 2,184 (1,507 on the merge; the probe's 8 and 190 more over 89 units: integral-constant 10, template-arg-unsigned-name 7, nontype-pack-recursive 3, the containers' headers 2 each), self-host 73,042 of 74,390 beside under the check (72,840 on the merge, +202).
Census (n2, this commit's build): no check 1,338 units, nonzero 353 (the merge's set); check nonzero 353 (the same units as n1's), `constants: equal` 1,353, no class differ (the three frame `differ` lines, template-subst-mcall, template-subst-member and thread-pool, are n1's as well, the REF's); self-host 102 sources, nonzero 0, no class differ, `constants: equal` 12,002 (12,002 on the merge).
Gates (REF 5a550bb2, the merge), on n2: subst.sh 0 failed, then run.sh 1331/0, run.sh under the flag 1331/0 (975 printed their recorded output, 352 refused as recorded, 0 failed); identical 0 differ plain x86_64-linux 1972, x86_64-windows 3944, arm64-darwin 1972, tms6747 1972, and with `-templates=subst` x86_64-linux 1972, x86_64-windows 3944, arm64-darwin 1972 (every case, the probe among them); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 00:20:35-00:25:07, ~4.5 min, beside n1's names run), names.sh 986/0, overload.sh 46/0, the probe by clang 19 and cxx3-msvc the same.
**Next**: a base over the parameters (`Q : V<I>`): ParserType.cpp's read leaves it out with no record (the `readLayout_` branch at "A read's `struct D : B<T>`"), so the read must keep each left-out base's type and access by the class's start, for classShape (made first under the values), laidFromRead's layBases and classFromRead's addBase; then a typedef's bound and valueShape's member bodies.

## M8.3 slice 3f: a class over a pack or a template template parameter it names in member bodies alone, under -templates=subst (lane m83am, 2026-11-24)

classShape refused every class with a pack or a template template parameter ("a pack or template parameter, bound by
name"). It now scans the class - a primary from its head, a partial from past its written arguments (which name the
parameter: `is_function<R(A...)>`) - through its bases and body, a member function's body skipped as slice 3e skips it,
and refuses by that name only a pack or template parameter named there; one named nowhere else is in the class's tag
alone, so the class is made from the read like any other, and its member bodies, which instantiate's door refuses for any
pack or template parameter, replay under the specialization's packs as before. The binding loop passes such a parameter.
A parameter named in a signature, a data member, a base or a mem-initialiser still replays by name.
Probe `template-subst-packparam` (Tagged<T, Ts...>, Traits<R(A...)>, Holder<template<class> class C, T>, Seq<int... Ns>,
Maker<C> and Count<Ts...> naming theirs in a body, Call<Ts...> naming its pack in a data member `Tagged<int, Ts...>`; clang
19 7 b 7 9 42 5 8 3 / 4 8 4 / 2 4, cxx3 the same plain, under the flag, under the check and cxx3-msvc on Windows; its 11
classes `equal`, Call replayed). A first probe named the pack as `f(Ts... a)`: that member's read differs in shape from its
replay under REF too (the function parameter pack family M8.1 records), and lifted subst.sh's class-members shape over 9.
Work (0): tests/subst.sh on a fresh tree at 3a6be07b (its own exe): 0 failed.
src 97,212 -> 97,233 (+21); no EXCLUSIONS line moved.
The measure: class specializations made with no replay - suite 1,533 of 2,184 (1,502; the probe's 11, so 20 of the
suite's own), self-host 72,623 of 74,390 beside under the check (72,623, unchanged: its 401 -
`function<R(Args...)>`, `__fn_model<F, R, Args...>` - name the pack in a signature or a data member; `equal` 73,095, nonzero 0).
Census: no check nonzero 353; check `equal` 1,711 (1,671; the first probe's 10), no class differ, nonzero 353; still in the
suite "a pack or template parameter, bound by name" 102 (136): named in a signature, a data member, a base or an id.
Gates (REF 3a6be07b, on the commit's build): identical 0 differ on four targets plain (1966/3932/1966/1966) and with
`-templates=subst` on three (1966/3932/1966: every case and the probe among them); run.sh 1328/0, then run.sh under the flag
1328/0; subst.sh on the first probe 1 failed (class-members shape 10 over 9, the `f(Ts... a)` above); the probe's bodies step
now reads shape 0, so 9 again - the re-run of subst.sh was queued behind the box's other jobs at the hour (owed: the wave
gate); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile
Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (~5.5 min,
box shared), names.sh 983/0, overload.sh 46/0.
**Next**: the suite's other 102 of the reason name the pack in a signature (`f(Ts... a)`), a data member or a base: a pack
expanded under the binding in the read's member signatures (substituted over `packExpansion`), which the self-host's 401
(`function<R(Args...)>`, `__fn_model`) need as well; after it, the suite's non-type 350 (lane m83al's).
Owed above, run after the commit on its tree (b3957edc, the probe naming its pack in a data member): subst.sh 0 failed.

## M8.3 slice 3g: a pack expanded under the binding in a class's member signatures and data members, under -templates=subst (lane m83ao, 2026-11-25)

classShape refused a class naming its pack anywhere but member bodies. A pack expanded in a parameter list - `R(Args...)`,
`R (*call_)(void *, Args...)`, `R operator()(Args... a)`, `const Ts &...` - is now made from the read: `Substituted` carries
the class's packs (makeClass's, a partial's chosen ones), and `paramsUnder` lays a read's `packExpansion` parameter as one
parameter per member, as Parser::packParameter makes them for the replay (cv dropped); substituted's function types,
membersFromRead's signatures and written parameters, classMembers and memberSymbol (both ABIs) go through it. The scan
takes a pack name whose innermost bracket is `(` and which `...` follows (through `&`, `&&`, `*`), and skips a
mem-initializer list as it skips the body; a pack in a template-id (`Tagged<int, Ts...>`), a base or a typedef still
replays by name. templatesFromRead binds the class's packs for its member templates, and templatesHeld holds them
(a pack's class binding is the class's own).
Probe `template-subst-packsig` (Fn<R(Args...)> with a function-pointer member over the pack and a member taking one,
Sink<Ts...> over `const Ts &...`, the empty pack; 7 7 100 / 2 0 3 / 2 2 / 0 0 / 1, clang 19 the same; 6 classes `equal`,
REF replays all 6). A first probe gave its members function parameter packs (`f(Args... a)`): their declarations differ in
shape from the replay under REF too (the declaration door's pack family), and lifted subst.sh's class-members shape to 11
and partial-members to 2, so the probe carries the pack in function types alone.
src 97,233 -> 97,273 (+40); 3 EXCLUSIONS citations re-pointed.
The measure: class specializations made with no replay - suite 1,548 of 2,184 (1,533; measured 1,551 with the first
probe's 9 classes, the final probe makes 6; 9 of the suite's own), self-host 72,963 of 74,390 beside under the check
(72,623; the self-host's 401 "a pack or template parameter, bound by name" are all gone: 340 made from the read, the rest
replayed for the next reason, "a type parameter bound to a class the read named over its parameters" 252 -> 310).
Census (n2): no check nonzero 353; check `equal` 1,730 (1,711), no class differ, nonzero 353; the suite's reason 102 -> 80
(left: a pack in a template-id, a base or a typedef); one new member `differ` in std-function, "the read's frame's size
where the replay has a slot no local names" (the frame family thread-pool shows under REF), owed. Self-host census on n2
nonzero 1: Templates.cpp itself, whose new range-for over a PackMembers is outside the SSA lowering ("a range-based for's
begin or end taking another type"); n3 indexes it.
Gates (REF 684b39a6, on n2; n3 differs by that loop and three citations): identical 0 differ on four targets plain
(1968/3936/1968/1968) and three with `-templates=subst` (1968/3936/1968); run.sh 1329/0, then under the flag 1329/0;
subst.sh 1 failed on the first probe (above), re-run on the final probe owed or below; static rc 0 (exclusions 0/0 after
re-pointing, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile ParserType.cpp rc 0
plain and under the flag, Templates.cpp rc 1 on n2 (the loop above), re-run on n3 below; g++ -Werror clean; Windows cl
/W4 /WX clean on n2 (~3.5 min) and n3 (~6 min, box shared); names.sh and overload.sh below.
**Next**: the suite's 80 left name the pack in a template-id or a base; the largest reason in the suite census is "a
non-type parameter named outside its member bodies" 350, and in the self-host "a type parameter bound to a class the read
named over its parameters" 310.
Owed above, run after the commit: on n3 (the commit's tree) the probe right plain, under the flag and the check (6 equal),
static rc 0 (src 97,273), self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; subst.sh on the
final probe 0 failed (cases 17 unread under 17, 53 shapes of 53; self 26 under 26); Windows names.sh 984/0 and overload.sh
46/0 (the full runs on n2; the probe's final form 1/0 on n3); cl /W4 /WX clean on n3.

## M8.3 slice 3h: a type parameter bound to a lambda's closure type, and a noexcept body the replay wraps for the bodies before it, under -templates=subst (lane m83aq, 2026-11-25)

Found by a debug build (boxkit/m83aq-Tdbg.cpp, cxx3-msvc `-arch x86_64-linux` on Windows): every class the census called "a type
parameter bound to a class: one the read named over its parameters" was bound to a closure type - `main::$_0`, whose tag holds a
`$` - in `is_same`, `__fn_callable`, `__fn_model`, `remove_reference`, `decay` over a lambda. The door's thirteen tests of a read
id by a `$` in the tag now ask `overParameters`: a `$` that is no program closure's `$_N` or `$deduced_N` (a read's own closure,
`$read$f::$_0`, is still over its parameters). No class is copied: the closure's class is the program's, bound as any class is.
Work (0): std-function's one member frame `differ` ("the read's frame's size where the replay has a slot no local names") and
thread-pool's under REF are one cause - `explicit operator bool() const noexcept` in `function` and `unique_lock`: the replay's
`replayInlineBodies` clears `mayThrow_` once per call, not per body, so a call in a body replayed before it wraps a noexcept body
for [except.spec]/9 (wrapUp's `.ex.ptr` slot) where the read, one body a call, does not. The read cannot know the replay's order:
`wrapInherited` (asked at madeMember before every replayed body, `throwsBefore_`) replays such a body by name, and memberFrame
names it ("member: replayed: a noexcept body wrapped by the calls of the bodies before it"). Not changed: the replay's wrap
itself (clang wraps neither body; resetting `mayThrow_` per body changes emitted code against REF - owed, the user's call). The
two function-door frame `differ`s m83ap names (template-subst-mcall `onBase`, template-subst-member `toBase`) are another cause:
a derived-to-base conversion at a nonzero offset, whose `.bp` slot (ParserOverload.cpp, `off > 0`) the read over `const T &`
cannot lay - owed.
Probe `template-subst-closurearg` (Holder<F>, Model<F>, Same<A, B>, Unref<T &> over three closures; 107 1 / 17 1 / 42 4 13 / 1 0 /
25, clang 19 on Windows and cxx3-msvc the same; its 8 classes `equal`; `.nonames`: the closure numbering lambda.nonames records).
src 97,273 -> 97,294 (+21); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2085/2137/2834 -> 2096/2148/2845).
The measure: class specializations made with no replay - suite 1,595 of 2,184 (1,548 on 7d70abef's build, counted now; the
probe's 8, so 39 of the suite's own), self-host 73,446 of 74,390 beside under the check (72,963; the 310 "bound to a class the
read named over its parameters" all gone, the reason no longer in either census).
Work (0), counted on 7d70abef's build: suite no check 1,548, check `equal` 1,727, no class differ, nonzero 353, member frame
`differ` 2 (std-function, thread-pool). On n1: check `equal` 1,786, no class differ, nonzero 353, member frame `differ` 0 (2
named); self-host nonzero 0, `equal` 73,979, member frame `differ` 0 (39 named, the n2 census's 39 `differ`s); the function
door's frame `differ` 2 (mcall, member: the `.bp` cause above, REF's).
Gates (REF 684b39a6, n1): identical 0 differ on four targets plain (1970/3940/1970/1970); run.sh 1330/0, then under the flag
1330/0; subst.sh on the first probe 1 failed (its `hold` template's brace list into a shallow `Holder<F>`, "'' is full": a
function-door read's reason the ceiling does not record), the probe rewritten without it, re-run below; static rc 0
(exclusions 0/0 after re-pointing, memory 89, ast-residue 2012/298 not raised, ide up to date; comments 0 after one group
the helper split was mended); g++ -Werror clean; Windows cl /W4 /WX clean (full, 00:42-00:45, ~3 min); names.sh 985/0,
overload.sh 46/0; clang 19 and cxx3-msvc on the probe the same.
**Next**: the suite's largest reason, "a non-type parameter named outside its member bodies" (lane m83ah's family), and the
self-host's unordered_map left; the function door's `.bp` frame family; the replay's per-call `mayThrow_` (the user's call).
Owed above, run after 1b153bea on its tree: identical with `-templates=subst` 0 differ on three targets (1970/3940/1970: every
case and the probe among them); subst.sh on the final probe 0 failed (self 26 unread under 26, 3 shapes of 3); the probe right
plain, under the flag, under the check (8 classes `equal`) and REF; static rc 0 on the final tree (comments 0); self-compile
Templates.cpp and ParserType.cpp rc 0 plain and under the flag.

## M8.3 slice 3i: a pack named in a template-id, `X<Ts...>`, expanded under the binding, and the function door's `.bp` family replayed by name, under -templates=subst (lane m83as, 2026-11-25)

Work (1): classShape refused any class naming a pack outside a parameter list's `(Args...)` ("a pack or template parameter,
bound by name"). A pack written as a template-id's argument, `Count<Ts...>` or `Count<Rest...>`, now passes: `packUnder`
expands the read id's pack argument (`expandsParam`, the parameter standing for itself) into the members `s.packs` binds, each
other member substituted, as the parser lists a bound pack in a template-id; `specializationUnder` asks it, so the program's
class is found by its key, and `earlyArgs` asks it for another template's pack parameter (its own template under a pack, as in
`Tuple<Rest...>` inside Tuple, stays the replay's), so idsBefore makes an undeclared one first through makeClass, the pack bound
as the parser binds one (no type in the binding, the members in `Packs`) - in the body's order, never a copy. A pack in a base,
`: Base<Ts>...`, is unchanged (a base over the parameters is m83ar's). Work (2): the `.bp` family is replayed by name, not laid
by the read: `boundMemberCall` refuses a member function a base at a nonzero offset declares ("... whose conversion the replay
lays a slot for", template-subst-mcall's `onBase`), and substShape a reference or pointer local to such a base
(template-subst-member's `toBase`, `const B1 &r = t`). Not changed: the replay's conversion; a bound class passed to a base's
reference parameter (dependentCall's) is not asked - owed if a census shows it.
Probe `template-subst-packid` (Holder<Ts...> with a Count<Ts...> member and a Count<Ts...> * signature, Pair<T, Rest...>, an
empty Holder<>; 3 4 8 / 1 7 16 / 9 8, clang 19 on Windows and cxx3-msvc the same; under the check its 6 classes `equal`, 3 made
before the class).
src 97,294 -> 97,337 (+43); 3 EXCLUSIONS citations re-pointed (Templates.cpp 2096/2148/2845 -> 2127/2179/2876).
The measure: class specializations made with no replay - suite 1,604 of 2,184 (1,595 on cda107fb's build, counted now; the
probe's 6, so 3 of the suite's own; counted on n1, which n2 differs from only in two range-for loops written as index loops),
self-host 73,449 of 74,390 beside under the check (73,446; on n2). The function door: suite "substituted with no replay" 536 ->
534 (onBase, toBase by name). Work (0): the REF census (no check) 1,595, nonzero 353; the brief's suite "pack or template
parameter" family is mostly static data members over a pack (`Sum<Rest...>::value`) and member bodies, which this slice leaves.
On n2's self-host: nonzero 0, `equal` 73,982 (73,979), frame `differ` 0 (39 named), function door frame all `equal`.
n1's self-compile of Templates.cpp failed: a range-based `for` over `PackMembers` (begin/end of its base's type) is outside the
SSA lowering - packUnder and the base scan were written as index loops (n2); self-compile of Templates.cpp and ParserType.cpp
on n2 rc 0 plain and under the flag.
tests/subst.sh: template-subst-mcall and template-subst-member's "replay (5): substituted with no replay" are 4 (the body now
replayed by name), mcall's frame `differ` line is "frame (4): equal", and the two new reasons are recorded.
Gates (REF = cda107fb's build): identical on n1 0 differ on four targets plain (1972/3944/1972/1972) and with -templates=subst
on three (1972/3944/1972); on n2 x86_64-linux plain and flag 1972/0; run.sh on n1 1331/0 plain and with CXX1 carrying
-templates=subst (1331 pass, none fall back to failing); subst.sh on n2 0 failed (run -O0/-O2 1331/0, typed 978/0); static
rc 0 (exclusions 0/0 after re-pointing, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror
clean (n1, n2); Windows cl /W4 /WX clean (n1 full 01:21:31-01:27:03, ~5.5 min; n2 delta, built 01:54); names.sh 986/0 and
overload.sh 46/0 on n1; clang 19 and cxx3-msvc on the probe the same.
**Next**: a pack in a base, `: Base<Ts>...`, and a static data member over a pack (`Sum<Rest...>::value`, staticShape);
a bound class passed to a base's reference parameter (dependentCall), not asked by the `.bp` refusal.
Owed above, run after 506d9701 on its tree (n2): the suite census with no check 1,604 of 2,184 made with no replay (n1's),
nonzero 353; under the check `equal` 1,795 (1,786), no class differ, nonzero 353, every frame `equal` (the function door's two
`.bp` differs gone); "a pack or template parameter, bound by name" 50 left (80 before).


## M8.3 slice 3j: a static data member's initialiser naming a template, its ids made where the door evaluates it, under -templates=subst (lane m83av, 2026-11-25)

**The measure**: class specializations made with no replay - suite 1,620 of 2,184 (1,604 at 9b3111da; the two probes' 16), self-host 73,449 of
74,390 beside under the check (73,449, unchanged: no self-host class stopped on this reason at 9b3111da). staticShape refused every read static data member whose initialiser names a template
(`1 + Count<Rest...>::value`, `2 * Size<T>::value`, `Same<T, int>::value`): the class that template-id names is made where
the initialiser is evaluated, and the replay evaluates it at the declarator while it reads the body. The door evaluates it there
too - staticsFromRead puts the parser at the declarator and calls `Parser::declareStaticMember` (slice 3c), so the id is made
by the parser's own path (makeClass, never a copy) - after the ids idsBefore made before the class. The two orders are one
while no template is named after the initialiser in the class's braces outside its member bodies (those are replayed later by
both doors), the class's own name as its constructor aside; where one is, the class is still replayed, by name ("a static data
member's initialiser naming a template, before another template named in its class"). staticsFromRead now binds the packs
(`Sum<Rest...>` needs Rest as the binding holds it), as readClassBody binds them; it bound none before.
Work (0), the REF census (9b3111da's build, under the check): `equal` 1,795, nonzero 353; the suite has no class that stopped on
"a static data member's initialiser naming a template" at 9b3111da (the brief's 65 was counted before slices 3g-3i, whose
classes now stop earlier, on a non-type parameter named outside its member bodies, m83au's) - the suite gains the probe's
classes. On m83au's tree (ratio's static_asserts let through) the reason is 128: <ratio>'s `num` is followed by `den` and by `typedef ratio<num, den> type`, so ratio stays replayed by name; `__gcd`'s own initialiser has nothing after it and would pass once merged (owed: gated there). "A pack or template parameter, bound by name" is 50
in 24 files: template template parameters (template-template-*), std::function (<functional>), the nontype-pack cases, the
variadic and SSA pack cases, user-defined-literal; not reached this hour.
Checked, as m83as left owed: a bound class passed to a base's reference or pointer parameter (`g(t)`, `h(&t)` with T bound
to D whose B2 is at a nonzero offset, in a member body and a function template's): probe template-subst-baseref 2 20 102
plain, under the flag and under the check, clang 19 and cxx3-msvc the same - the derived-to-base adjustment is right.
Probe template-subst-staticid (Count over a type pack, Size, Twice, Same/IsInt, and Late, which names Size<T> after its static
and stays replayed): 3 0 1 / 8 2 / 1 0 / 3 4 8 1, clang 19 on Windows and cxx3-msvc the same, cxx3 plain, under the flag,
under the check and REF the same; under the check 15 classes `equal`, Late refused by the new reason.
src 97,337 -> 97,354 (+17); no EXCLUSIONS line moved (0 stale).
Gates (REF = 9b3111da's build): identical on n1 0 differ on four targets plain (1976/3952/1976/1976) and with -templates=subst on three (1976/3952/1976: every case, both probes among them); run.sh 1333/0 plain, then with CXX1 carrying -templates=subst 1333 pass, none fall back, 0 fail; subst.sh 0 failed; -check=templates over tests/cases `equal` 1,811 (1,795), no class differ, nonzero 353 (REF's), and the self-host nonzero 0, `equal` 73,982, frame differ 0; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean; Windows cl /W4 /WX clean, alone (full build 02:13:09-02:14:56, ~2 min); names.sh 988/0 (the two probes among them), overload.sh 46/0.
**Next**: the pack family's 50 (template template parameters first: template-template-* are most of the files); a static
data member's initialiser naming a template after another static's (staticsFromRead declares them in order, so the scan may
step over another static's initialiser); sizeof...(Ts) in a static initialiser.

## M8.3 slice 3i: a class whose static data member's initialiser names a non-type parameter, and a member typedef of its own class, under -templates=subst (lane m83ap, 2026-11-25)

The branch first merges main (b4781f0c, 5a550bb2): this work needs slice 3c's staticShape, staticsFromRead and staticsHeld (lane m83ai), which 96d07af4 lacked; only this file conflicted. The merge is REF for every gate below; its census is the base: 1,507 of 2,184.
classShape's scan takes a non-type parameter in a static data member's initialiser in the class's own braces (`static const T value = v;`, `static const int size = N * 2 + 1;`, `static const bool big = (N > 3);`, `static_cast<T>(N + 1)`): staticInit walks back from the parameter to `static` past an `=` at its own parenthesis level, never through `;`, braces, `:`, `typedef`, `using` or `enum`, and never out of a parenthesis the `=` is inside (a parameter's default argument `f(int a = N)` replays). staticsFromRead declares the member under the bound values, so its value is the replay's, and staticsHeld holds it under the check.
That alone moved integral_constant to fromReadWhyNot's "a member typedef or alias naming its own class" (176 classes, n1: 1,529). The refusal is gone: classFromRead declares the read's member typedefs under readStands's map (the read class is the class being made), so `typedef integral_constant<T, v> type;` and `typedef const Sized *pointer;` name the made class, as the replay's do; classSkip held them already (typedefsCarried passes the read class).
A base over the parameters (`Q : V<I>`), a typedef's bound and member bodies folded by valueShape still replay.
Probe template-subst-staticvalue (by hand and clang 19 `7 7 q 1 / 5 11 0 1 15 / 20 20 4 / 1 0`), right plain, under the flag, under the check and by REF; its 8 classes made from the read, 8 `equal` under the check.
src 97,401 -> 97,410 (+9); no EXCLUSIONS line moved.
The measure: class specializations made with no replay - suite 1,705 of 2,184 (1,507 on the merge; the probe's 8 and 190 more over 89 units: integral-constant 10, template-arg-unsigned-name 7, nontype-pack-recursive 3, the containers' headers 2 each), self-host 73,042 of 74,390 beside under the check (72,840 on the merge, +202).
Census (n2, this commit's build): no check 1,338 units, nonzero 353 (the merge's set); check nonzero 353 (the same units as n1's), `constants: equal` 1,353, no class differ (the three frame `differ` lines, template-subst-mcall, template-subst-member and thread-pool, are n1's as well, the REF's); self-host 102 sources, nonzero 0, no class differ, `constants: equal` 12,002 (12,002 on the merge).
Gates (REF 5a550bb2, the merge), on n2: subst.sh 0 failed, then run.sh 1331/0, run.sh under the flag 1331/0 (975 printed their recorded output, 352 refused as recorded, 0 failed); identical 0 differ plain x86_64-linux 1972, x86_64-windows 3944, arm64-darwin 1972, tms6747 1972, and with `-templates=subst` x86_64-linux 1972, x86_64-windows 3944, arm64-darwin 1972 (every case, the probe among them); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 00:20:35-00:25:07, ~4.5 min, beside n1's names run), names.sh 986/0, overload.sh 46/0, the probe by clang 19 and cxx3-msvc the same.
**Next**: a base over the parameters (`Q : V<I>`): ParserType.cpp's read leaves it out with no record (the `readLayout_` branch at "A read's `struct D : B<T>`"), so the read must keep each left-out base's type and access by the class's start, for classShape (made first under the values), laidFromRead's layBases and classFromRead's addBase; then a typedef's bound and valueShape's member bodies.

## M8.3 slice 3j: a class whose base is a class template-id over its parameters (`Q : V<I>`), under -templates=subst (lane m83ar, 2026-11-25)
Base d751a1a5 (m83ap). The read left each base over the parameters out of its layout with no record (ParserType.cpp's `readLayout_` branch, "A read's `struct D : B<T>`"), so classShape refused every such class ("a base named through a parameter, which the read laid out as nothing"). The read now keeps each one it leaves out, in Parser::readBases_ by the read class: its type (the id over the parameters), its access and its place in the base list; a virtual one or one of a template template parameter is kept as null, which still replays.
classShape takes a read whose bases are all kept ids (one beside a base the read laid replays, by its old name; so does any `using` in the body, which the read skips beside such a base): each base's id is mapped to the class the program declared under the binding, or wanted by idsBefore ahead of every other id, so makeClass makes it first, as the replay does reading the head; one that is polymorphic, final, incomplete, a union or of a virtual base replays by name, and so does one with a constructor, a destructor, a non-trivial copy or an `operator=` of its own: the read declared its implicit members knowing no base, and n1 (which took those) met 15 classes in thread-pool, `atomic<T> : __atomic_base<T>` whose deleted copy and own constructors give the replay other implicit members (`differ: the number of member functions` under the check). basesUnder is the one place the made class's bases come from: the read's, or the kept ones under the binding laid by Parser::layBases (the parser's own path, offsets and empty subobjects included); laidFromRead lays the members after them, classFromRead adds them by addBase in the order written, and classLaid holds them against the replay's under the check.
A typedef's bound and member bodies folded by valueShape still replay.
Probe template-subst-baseparam (by hand and clang 19 `7 16 1 / 3.5 1.5 16 / a 9 z 2 16 / 11 4 / 3 4`), right plain, under the flag, under the check and by REF on Linux, and by cxx3-msvc under the flag and the check; its 11 classes made from the read, 11 `equal`, 6 made before the class (the bases): a base over a non-type parameter, over a type parameter, two such bases, a private one, an empty one.
src 97,410 -> 97,458 (+48); 25 EXCLUSIONS citations into ParserType.cpp re-pointed (+3), no line moved otherwise.
The measure: class specializations made with no replay - suite 1,724 of 2,184 (1,705 at d751a1a5; the probe's 11, template-dependent-base 3, chrono-clocks 2, template-keyword-member, template-left-out-base-qualified and thread-sleep-yield 1 each), self-host 73,405 of 74,390 beside under the check (73,042, +363); self-host census 102 sources, nonzero 0, no class differ, `constants: equal` 12,035.
Census (n2, this commit's build): no check 1,339 units, nonzero 353 (REF's set); check nonzero 353, class `equal` 1,903, no class differ (the three frame `differ` lines, template-subst-mcall, template-subst-member and thread-pool, are the REF's, lane m83aq's); still replayed by a base: 36 "a class template-id over its parameters" (a base the read laid that names its parameters, or a kept one earlyArgs cannot make), 17 "with a constructor, a destructor or an assignment of its own".
Gates (REF d751a1a5), on n2: subst.sh 0 failed, then run.sh 1332/0, run.sh under the flag 1332/0 (976 printed their recorded output, 352 refused as recorded, 0 failed); identical 0 differ plain x86_64-linux 1974, x86_64-windows 3948, arm64-darwin 1974, tms6747 1974, and with `-templates=subst` x86_64-linux 1974, x86_64-windows 3948, arm64-darwin 1974 (every case, the probe among them); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 01:16:02-01:19:49, ~3.8 min, box shared; n1's failed first on `Parser::BasesLaid` in Templates.h, where Parser is incomplete), names.sh 987/0, overload.sh 46/0, the probe by clang 19 and cxx3-msvc the same.
**Next**: a base over the parameters beside a base the read laid (the two orders merged by their places), a `using` beside one (the read skips it), a typedef's bound, valueShape's member bodies.


## M8.3: [except.spec]/9 asked of each definition alone - `mayThrow_` cleared where a definition starts (lane m83au, 2026-11-25)
`mayThrow_` counts what a body calls that may throw, and a `noexcept` function whose count is nonzero gets the catch-all terminate wrapper (an LSDA on the Itanium targets).
It was cleared only by clearFunctionState, which replayInlineBodies runs once for all the bodies of a class and topLevel never runs, so a `noexcept` function was wrapped after an earlier function - in the same class (replayed) or at namespace scope - had called something that can throw (lane m83aq found it, m83at the second door; boxkit/m83at-leak.cpp shows both on REF).
Now replayInlineBodies clears it per body and the definition's start in ParserTopLevel.cpp (beside `lambdaCount_ = 0`, before the mem-initialisers) clears it for every definition, replayed or not.
The changed outputs, each a `noexcept` function that calls nothing that throws, unwrapped as clang leaves it (clang 19 `-target x86_64-unknown-linux-gnu -S`: no LSDA for `leaf`, `pure`, `function(function &&)`): noexcept-terminates-callee (`leaf`), noexcept-callee-search-terminates (`pure`, `leaf`), std-function and std-function-ledger (`function(function &&)` C1/C2, `operator bool`, `==`/`!=` against nullptr), thread-pool (`thread::joinable`, `unique_lock::owns_lock`) at -O0 and -O2 on x86_64-linux, arm64-darwin and tms6747 (std-function-ledger at -O2 only there), and thread-sleep-yield on tms6747 (`this_thread::yield`, empty on the C6000, lost the wrapper's frame slot); x86_64-windows none (the wrapper is Itanium's).
src 97,458 -> 97,460 (+2); 7 EXCLUSIONS citations into Parser.cpp and ParserTopLevel.cpp re-pointed.
The measure (unchanged: a code generation fix, the brief's own defect): class specializations made with no replay - suite 1,724 of 2,184, self-host 73,405 of 74,390 beside under the check; self census 102 sources, nonzero 0.
Gates (REF 5a97c8c3), on n2: identical plain x86_64-linux 1974/10, x86_64-windows 3948/0, arm64-darwin 1974/10, tms6747 1974/11, with `-templates=subst` x86_64-linux 1974/10, x86_64-windows 3948/0, arm64-darwin 1974/10 - every difference one listed above; subst.sh 0 failed, then run.sh 1332/0, run.sh under the flag 1332/0 (976 printed their recorded output, 352 refused as recorded; std-function-ledger's ledger among them); static rc 0 (exclusions 0/0 after the re-point, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 02:01:20-02:04:22, ~3 min), names.sh 987/0, overload.sh 46/0.

## M8.3 slice 3k: a class over a non-type parameter named in a `static_assert` of its braces or in a static data member's conditional, under -templates=subst (lane m83au, 2026-11-25)
The suite's 80 "a non-type parameter named outside its member bodies" (95 with members) split by token context (lane m83at's debug patch, boxkit/m83at-Tdbg.cpp): 69 in <ratio> (thread-sleep-yield 26, chrono-clocks 23, thread-pool 20) - `ratio`'s `static_assert(D != 0, ...)`, most of them, and `__gcd<A, 0>`'s `A < 0 ? -A : A`, where staticInit stopped at the conditional's `:` - then member-template-enclosing-parameter 8 (`V<N + K>`, `W<A - B>` in a member template's return type), and one each in constructor-own-template-id (a mem-initialiser), static-assert (`Buffer`'s static_assert) and template-enumerator (`enum { Twice = K * 2 }`).
classShape now keeps a `static_assert` in the class's own braces whose condition folds true under the values (assertAt: foldBound's operands, one comparison), the replay's answer; any other static_assert replays by name there ("replayed: a static_assert", classBeyond's refusal moved into classShape, which knows the values). staticInit walks back through a `:` that a `?` before it matches, stopping at an access specifier's.
`ratio` itself still replays: its `num` and `den` name `__gcd` (staticShape's "a static data member's initialiser naming a template", 65 -> 128 in the check census, now the largest reason after the packs).
Probe template-subst-assert (by hand `3 2 4 4 10 5 / 7 9 4 5`), right plain, under the flag, under the check (7 classes `equal`, 7 made from the read) and by REF on Linux.
src 97,460 -> 97,480 (+20); no EXCLUSIONS line moved.
The measure: class specializations made with no replay - suite 1,738 of 2,184 (1,724; the probe's 7, thread-sleep-yield 3, chrono-clocks 2, thread-pool and static-assert 1 each: `__gcd<A, 0>`, `__gcd<0, B>`, `Buffer`), self-host 73,407 of 74,390 beside under the check (73,405); self census 102 sources, nonzero 0, no class differ.
Census (n4): no check 1,340 units, nonzero 353 (REF's set); check nonzero 353, class `equal` 1,917 (1,903), no class differ; non-type outside member bodies 80 -> 10, static_assert 64 -> 0.
Gates (REF 5a97c8c3), on n4: identical plain x86_64-linux 1976/10, x86_64-windows 3952/0, arm64-darwin 1976/10, tms6747 1976/11, with `-templates=subst` x86_64-linux 1976/10, x86_64-windows 3952/0, arm64-darwin 1976/10 - each set exactly the mayThrow_ commit's, the probe the same under the flag; subst.sh 0 failed, then run.sh 1333/0, run.sh under the flag 1333/0 (977 printed their recorded output, 352 refused as recorded); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 02:20:14-02:22:15, ~2 min), names.sh 988/0, overload.sh 46/0, the probe by clang 19 and cxx3-msvc the same.
**Next**: a static data member's initialiser naming a template (128, `ratio`'s `__gcd<N, D>::value`: the class it names made ahead, as the replay makes it reading the initialiser), then the member template's return type over a non-type parameter (8).

## M8.3 slice 3l: a class over a non-type parameter named in a member template's declaration, under -templates=subst (lane m83aw, 2026-11-25)
The self-host census by reason under -check=templates on ac0e5d48 (REF), 102 sources, nonzero 0, `equal` 73,407: the classes still replayed stop on a data member of a class the read named over its parameters 577 (24 in each of 24 sources; before the replay they stop on "a member function's signature over a type the binding does not carry: a class template-id over dependent arguments", 577), a pack or template parameter bound by name 401, a type parameter bound to a class the read named over its parameters 252, a base that is a class template-id over its parameters 213, a type parameter bound to a specialization the instantiation has not completed 76, bound to a type that is no scalar, class, reference or array 63, a static data member's initialiser naming a template 41, a data member of a member enumeration over the parameters 25, a signature over a class template-id the program had not declared 14.
The 577 was not reached this hour (its debug build, boxkit/m83aw-Tdbg.cpp, prints the class and data member where classShape stops, and is left for the next lane). Done instead: the suite's member-template-enclosing-parameter (`V<N + K>`, `W<A - B>`, `V<N * K>` in a member template's return type).
classShape no longer scans a member template's declaration, from its `template` to its `;` or the end of its body, for the class's non-type parameters: templatesFromRead reads that declaration under the values (Parser::memberTemplate, instantiatingValues_), so the made class's member templates carry them; the refusal "a member template beside a non-type parameter" (lane m83ag's, for templatesHeld's stand-ins) is gone. templatesHeld now holds the replay's class values against `s`'s (the read's are the primary's stand-ins) and takes the read's pattern binding for a non-type parameter (a Kind::TemplateParam) as standing for that value, the replay's binding being no pattern.
Probe template-subst-memtvalue (clang 19 `63 10 6 12 / 4 12 20`), right plain, under the flag, under the check (7 classes `equal`) and by REF; member-template-enclosing-parameter the same (8 `equal`, REF 8 replayed).
src 97,480 -> 97,488 (+8); no EXCLUSIONS line moved.
The measure: class specializations made with no replay - suite 1,759 of 2,184 (1,738; the probe's 7, member-template-enclosing-parameter 8, constructor-template-in-member-template 2, template-member 2, constructor-template and template-keyword-member 1 each), self-host 73,407 of 74,390 beside under the check (unchanged: no self-host class has a member template beside a non-type parameter); self census 102 sources, nonzero 0.
Census (n2): no check 1,341 units, nonzero 353; check nonzero 353, class `equal` 1,938 (1,917), no class differ; non-type outside member bodies 10 -> 2.
Gates (REF ac0e5d48), on n2: identical plain 0 differ on four targets (1978/3956/1978/1978), with `-templates=subst` 0 differ on three (1978/3956/1978); subst.sh 0 failed, then run.sh 1334/0, run.sh under the flag 1334/0 (978 printed their recorded output, 352 refused as recorded); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (02:50:54-02:54:55, ~4 min, alone), the probe by clang 19 and cxx3-msvc the same; names.sh and overload.sh not finished at the hour (owed to the wave gate).
**Next**: the self-host's 577: the debug build names them as unordered_map<K, V, H, E>'s data member `t_` of `__hash_table<pair<const K, V>, K, __unordered_map_key<K, V>, H, E>` (48 per source in Lookup.cpp and Parser.cpp), where classShape is asked with no wanted list (readId true, earlyArgs not reached): the class is stopped first by "a member function's signature over a class template-id over dependent arguments", so the signature's id made ahead, as the replay makes it, comes before the data member's.

## M8.3 slice 3m: a class over a template template parameter named as a template-id in its braces, and `sizeof...` of a pack in its static initialiser, under -templates=subst (lane m83az, 2026-11-25)
Base: m8-3az at c617b946 (slice 3l) lacked slices 3f-3j's pack work, so every class with a pack or template parameter replayed there; the merge 89600951 brings m8-3ax2 (500d6651: main's 3f/3g, the 3h-3j line, m8-3at) beside 3l, one conflict (templatesHeld: 3l's held class values and the class-pack check both kept).
Work (0): the suite census by reason on the merge (n0, no check): class specializations made with no replay 1,983 of 2,184 (c617b946 1,759). Work (1), the pack family by a debug build (n0 + TDBGPK at classShape's scan, boxkit/m83az-go0.sh): 11 units still stopped on "a pack or template parameter, bound by name" - template-template-* (a template template parameter C at `C <` in the class's braces: Holder, H1/H2/HN/HP, Def, Two, Nest, Typed; Derived's base; Pass's `Holder<C, int>`; Kind) and nontype-pack-* (`static const int size = sizeof...(Ns);`: List, Seq; Max's `Rest...` stays). The largest group is C.
The read holds `C<T>` as an id over `$TT<i>`; the replay binds C to the argument's TemplateDecl (bindTemplateParameters). `Templates::templateUnder` gives that template's name for `$TT<i>` from `s` (classShape's binding loop maps templateParam(i) to the bound template name, as templateOf stands for it), so specializationUnder and earlyArgs find or make `Box<int>` by the parser's own path (makeClass through idsBefore, never a copy), before the class as its replay makes it. A C bound to an alias template, `typename C<T>::type` (memberUnder finds Cell<double> only after the replay: n1's one "differ: the class skip before the replay and after it", Typed), C as an argument (`Holder<C, int>`) and a base `: C<int>` still replay by name. The scan also lets `sizeof...(Ns)` through: staticsFromRead evaluates it at the declarator with the packs bound, staticsHeld holds it.
Probe template-subst-ttparam (Holder over Box, ns::Cell and Box<double>, Two over Pair, Link with `C<T> *` and an out-of-line member, Ints<Box>, Holder<Alias, int> replayed): 7 2 a b 3 1.5 4 / 40 z 9 -1 120 11 / 8 8 8 8, clang 19 on Windows and cxx3-msvc the same; cxx3 plain, under the flag, under the check and REF the same; under the check 13 classes `equal` (n0 6).
src 97,649 with the merge; this commit +17/-4 in Templates.cpp/.h; 3 EXCLUSIONS citations re-pointed (2243/2295/2992 -> 2255/2307/3004).
The measure: class specializations made with no replay - suite 2,023 of 2,184 (n0 1,983; the probe's 13, nontype-pack-deduction 7, nontype-pack-types 4, nontype-pack-expansion 3, nontype-pack 2, template-template-members 1 ...), self-host 74,359 of 74,381 beside under the check (22 replayed: a member initialiser; c617b946 73,407 of 74,390 - the merge and 3m together, the merge's own self census not run); self-host nonzero 0, class `equal` 74,892, no class differ.
Census (n2): no check 1,349 units, nonzero 353; check nonzero 353, class `equal` 2,214 (n1 2,199), no class differ (n1 had Typed's one).
Gates on n2: static rc 0 after the re-pointing (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0; g++ -Werror clean; identical against REF c617b946 0 differ plain on four targets (1994/3988/1994/1994) and with `-templates=subst` on three (1994/3988/1994: every case, the probe among them); subst.sh 0 failed, then run.sh 1342/0, run.sh with CXX1 carrying -templates=subst 1342 pass, none fall back, 0 fail; Windows cl /W4 /WX clean (delta 04:54:08-04:59:39, ~5.5 min, box shared; n1's delta passed the 9-minute cap, a defect, its exe written at 04:52), names.sh 997/0, overload.sh 46/0; on c617b946 (owed by m83aw) names.sh 989/0, overload.sh 46/0, cl ~2 min.
**Next**: C bound to an alias template (template-template-alias, -parameter: resolve the alias's target under the binding); `typename C<T>::type` once memberUnder's id is made before the class; C passed as an argument (`Holder<C, int>`); nontype-pack-recursive's `Max<..., Rest...>`.

## M8.3 slice 3l: `ratio` made from the read - a static data member's initialiser naming a template beside later statics and a typedef of its own template over them, under -templates=subst (lanes m83ax, m83ay, 2026-11-25)
`ratio`'s `num` and `den` name `__gcd<N, D>::value`, and after them come `den`'s initialiser and `typedef ratio<num, den> type;`: staticShape's after-scan took both for "another template named in its class" (128 lines in the check census at 500d6651, 64 of them before the replay), so every `ratio` replayed.
staticShape's after-scan now steps over another static data member's initialiser (staticsFromRead declares them in order, each read where the replay reads it) and over ownTypedefAt's typedef - `typedef X<...> n;` in the class's own braces, X its own template, an argument naming one of its static data members - which staticsFromRead reads after the statics by the parser's own path (Parser::specifiers and declarator at its token, the replay's place); under the check it is declared as `tag::$held::n` and staticsHeld compares it with the replay's. classFromRead, typedefsCarried and classSkip leave that typedef to staticsFromRead.
Probe template-subst-ratio (`1 2 / 1 1 / 1000`): clang 19 and cxx3-msvc the same on Windows; right plain, under the flag and under the check on Linux (14 classes `equal`, 14 made from the read; REF replays 5).
src 97,624 -> 97,676 (+52); no EXCLUSIONS line moved.
The measure: class specializations made with no replay - suite 2,038 of 2,184 (REF 500d6651: 1,961; thread-sleep-yield 23, chrono-clocks 21, thread-pool 19, the probe 14); self-host 74,379 of 74,390 beside under the check (REF 74,359; the merge of m8-3at moved the self census's class lines to 74,401); self census 102 sources, nonzero 0, no class differ.
Census (no check 1,348 units, nonzero 353, REF's set): check `equal` 2,152 -> 2,229, no class differ; "a static data member's initialiser naming a template" 128 -> 2 (template-subst-staticid's Late, refused by name as it was).
Gates (REF 500d6651), on n1: identical plain x86_64-linux 1992/0, x86_64-windows 3984/0, arm64-darwin 1992/0, tms6747 1992/0, with `-templates=subst` x86_64-linux 1992/0, x86_64-windows 3984/0, arm64-darwin 1992/0; subst.sh 0 failed, then run.sh 1341/0, run.sh under the flag 1341/0 (985 printed their recorded output, 352 refused as recorded); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 04:28:57-04:31:44, ~3 min), names.sh 996/0, overload.sh 46/0.
**Next**: the pack family (lane m83az), then the member template's return type over a non-type parameter.

## M8.3 slice 3m: an unnamed member enumeration made from the read, read in the body's order among the static data members, under -templates=subst (lane m83ba, 2026-11-25)
Work (0), the census on c0c89b3d (REF, its own build): the suite under the check has 2,414 "before the replay" class lines, 2,044 made from the read (no check: 2,038 made with no replay of 2,044 class lines); left by reason: a member enumeration 167 (basic_string's `enum { small = 15 };`, two in each unit that includes <string>; std::function's 11), a pack or template parameter 50 (lane m83az), a type parameter bound to no scalar, class, reference or array 42, a base a class template-id over its parameters 22, a base with a constructor, destructor or assignment 17, a non-type parameter outside its member bodies 12, a member typedef over a type the binding does not carry 12, a nested class 9, a virtual function 7, a member template beside a non-type parameter 7 (c617b946's, on m8-3aw), a type parameter bound to an incomplete specialization 4, nine of 3 or fewer. The self-host (102 sources, nonzero 0): 75,317 before the replay, 74,401 made from the read, 74,379 beside (22 "a member initialiser"); left: a member enumeration 531, a base a class template-id over its parameters 151, bound to an incomplete specialization 76, bound to no scalar, class, reference or array 73, a base with a constructor, destructor or assignment 63, a signature over an id the program had not declared 14, a member typedef 5, a polymorphic base 2, an array 1.
classBeyond refused every `enum` in a class's braces. memberEnums now names each unnamed `enum { ... };` there - after `;`, `{`, `}` or an access specifier's `:`, no base, no declarator, its braces naming no template (`<`) and no qualified name - and classBeyond refuses only another ("replayed: a member enumeration with a name, a base or a declarator, or naming a template or a qualified name"); classShape refuses one whose braces name a template parameter ("replayed: a member enumeration naming a template parameter", template-enumerator's `ByValue<K>`), so no enumerator made from the read is value-dependent. staticsFromRead reads each by the class body's own path (Parser::specifiers at its `enum`, the made class on classStack_, so its enumerators are `tag::a`) in the body's order among the static data members (`enum { last = before + 1 };` after `static const int before`, `after = last * 2` after the enum). Under the check staticsHeld reads them into `tag::$held` (a class of that tag on top of classStack_, the replay's class as currentClass_ so a static is found) and holds each enumerator's value, type and value-dependence against the replay's `tag::a` ("differ: a member enumerator the replay does not declare", "differ: a member enumerator's value or type"); an enumerator of another scope declared while it reads (a function's own enum, made by an initialiser) is not the class's and is passed over.
Probe template-subst-memberenum (by hand `15 16 64 15 / 13 21 8 / 12 8 4 12 / 3 4 11`; clang 19 and cxx3-msvc on Windows: see the gate line): right plain, under the flag, under the check and by REF on Linux; under the check its 5 classes (Buf x2, Sized x2, Two) `equal` and made from the read, ByValue x3 refused by the new reason.
src 97,676 -> 97,748 (+72); no EXCLUSIONS line moved.
The measure: class specializations made with no replay - suite 2,188 of 2,184 counted as before (the suite's own no-check total is 2,194 class lines; REF c0c89b3d 2,038 of 2,044, +150: 145 over the units that include <string> and std::function's tests, the probe's 5), self-host 74,583 of 74,605 beside under the check (REF 74,379 of 74,401, +204); self census 102 sources, nonzero 0, no class differ; a member enumeration left: suite 24 (20 named, with a base or declarator, or naming a template; 4 naming a parameter), self-host 329.
Census (n3, this commit's build): no check 1,349 units, nonzero 353 (REF's set); check nonzero 353, class `equal` 2,234 (2,229), no class differ.
Gates (REF c0c89b3d's build), on n3: identical plain x86_64-linux 1994/0, x86_64-windows 3988/0, arm64-darwin 1994/0, tms6747 1994/0, with `-templates=subst` x86_64-linux 1994/0, x86_64-windows 3988/0, arm64-darwin 1994/0 (every case, the probe among them); subst.sh 0 failed, then run.sh 1342/0, run.sh under the flag 1342/0 (986 printed their recorded output, 352 refused as recorded); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (all 102 sources, 05:32:06-05:38:52, ~7 min, the box shared with the m6dw44g gate), the probe by clang 19 and cxx3-msvc the same, names.sh 997/0, overload.sh 46/0.
**Next**: a named member enumeration (its type `tag::E` in the made class's signatures and data members: classShape's "a data member of a member enumeration the read named over its parameters"), one naming a parameter (its enumerators read under the values, a data member's bound over one), one naming a template (std::function's `enum { value = !is_same<...>::value }`, the id made where the replay reads it); then a base a class template-id over its parameters (self 151, suite 22).

## M8.3: the measure made one count, and slice 3n: an unnamed member enumeration naming a template, a qualified name or a parameter, made from the read (lane m83bb, 2026-11-25)
**The measure, one line a class specialization.** The numbers did not add up because each was a different population. The no-check `class:` replay lines (m83ba's "2,188 of 2,194") were written only for a class classBefore had already taken, so every class it refused was outside the total; the check's `class (N):` lines mix one verdict a class (`before the replay: ...`, `equal`) with `skip:` lines, `skip, member bodies:` (one a member body) and `made before the class` / `made inside the class` (nested makes, each also counted as its own class); `equal` (m83az's 74,892) is classLaid's verdict over every class laid, including those step (1) refused, so it can exceed `beside` (74,359), which counts only those it took; "2,184" and "74,390" were older totals. Now Templates::specializationCensus_ keeps each tag makeClass makes once with its verdict - `made from the read with no replay`, `made from the read, the replay beside under -check=templates`, or `replayed: <reason>` (step (1)'s, else classBefore's or idsBefore's, as `before the replay` says it) - and builtReport prints `-check=templates specialization: <verdict> | <tag>` for each and `-check=templates specializations: total=T made=M replayed=R again=A` (made + replayed = total; a tag met twice is counted once and said: `again` is 0 on the suite and the self-host) under CXX1_TEMPLATE_CENSUS and -check=templates alike. A unit that exits nonzero prints no census: the suite's 353 are its 352 cases with a `.error` and one `.notarget`, the same set as REF's.
**Slice 3n.** memberEnums takes an unnamed `enum { ... };` whose braces name a template-id or a qualified name (`!same<F, ns::tag>::value`); classBeyond keeps refusing one with a name, a base or a declarator ("replayed: a member enumeration with a name, a base or a declarator"); staticShape holds an enumerator naming a template to the statics' order rule (its lambda namedLater, shared with the static data member's: "replayed: a member enumeration naming a template, before another template named in its class"), since staticsFromRead makes its ids where it reads it; classShape lets a template parameter in the enum's braces through in a class of no member function (`bare`: no read signature and no member template, so no read body can name the read's value-dependent enumerator), and keeps "replayed: a member enumeration naming a template parameter" otherwise. std::function's `__fn_callable` is the self-host's shape.
Probe template-subst-enumid (by hand `1 0 0 10 / 1 7 1 13 / 9 2 / 9`; clang 19 on Windows the same, cxx3-msvc the same): right plain, under the flag, under the check and by REF on Linux; under the check 21 classes `equal` and made from the read, `sized<int>`, `sized<char>` (a member function beside the enum) refused by name.
src 97,748 -> 97,781 (+33; the measure +20, 3n +13); the measure printed under -templates=subst only, so the five `.templates` records (no flag) stay as recorded; EXCLUSIONS: three Templates.cpp citations re-pointed (2243, 2295, 2992 -> 2253, 2305, 3002).
The measure (no check; bce2621b's code as c0 measures it, then this): suite 2,188 made of 2,422, 234 replayed -> 2,207 made of 2,422, 215 replayed (with the probe 2,228 of 2,445); self-host (102 sources, nonzero 0) 74,583 made of 75,319, 736 replayed -> 74,887 made of 75,319, 432 replayed; again 0 on both, the totals unchanged by 3n. A member enumeration left: suite 5 (3 naming a parameter beside a member function, 2 with a name), self-host 25 (with a name, a base or a declarator).
Left by reason, self-host: a base a class template-id over its parameters 151, bound to a specialization the instantiation has not completed 76, bound to no scalar/class/reference/array 73, a base with a constructor/destructor/assignment 63, a member enumeration with a name 25, a member initialiser 22, a signature over an undeclared id 14, member typedef 5, polymorphic base 2, array 1. Suite: a pack or template parameter 50, bound to no scalar/class/reference/array 42, base id over its parameters 22, base with ctor/dtor/assignment 17, non-type parameter outside member bodies 12, member typedef 12, nested class 10, virtual function 7, member template beside a non-type parameter 7, a member enumeration 5, then 31 in smaller reasons.
Gates (REF bce2621b's build). On c1 (3n before the guard): identical plain x86_64-linux 1996/0, x86_64-windows 3992/0, arm64-darwin 1996/0, tms6747 1996/0; with `-templates=subst` x86_64-linux 1996/0, x86_64-windows 3992/0, arm64-darwin 1996/0; run.sh 1343/0 plain and under the flag (987 printed their recorded output, 352 refused as recorded); subst.sh 5 failed - the five `.templates` records (no flag) met the measure's lines, so builtReport prints them under -templates=subst only (c1f, this commit's build); check census over tests/cases nonzero 353 (the `.error` set), no class differ, `equal` 2,256; self check census 102 sources, nonzero 0, no class differ, beside 74,887 of 75,319. On c1f: subst.sh 0 failed, then run.sh 1343/0, run.sh under the flag 1343/0; identical x86_64-linux plain 1996/0 and under the flag 1996/0; probe right four ways; suite census unchanged (2,228 of 2,445); static rc 0 (src 97,781, exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag (c1); g++ -Werror clean; Windows cl /W4 /WX clean - full on c0 (06:00-06:07, ~7 min), delta on c1 (06:15-06:18) and c1f (06:31-06:35), the box shared; clang 19 and cxx3-msvc both probes the same; names.sh 997/0 on c0, 998/0 on c1 and c1f, overload.sh 46/0 on each.
**Next**: a named member enumeration (25 in the self-host, 2 in the suite: its type `tag::E` in the made class's signatures and data members), an enum over a parameter beside member functions (suite 3: the read's enumerator mapped to the made one in a read body); then a base a class template-id over its parameters (self 151, suite 22), a pack or template parameter (suite 50).

## M8.3 slice 3o: a base over the parameters with specials of its own, and one the read laid over a dependent member's class, made from the read under -templates=subst (lane m83bc, 2026-11-25)
Base 701d27a7 (m83bb). The two base reasons the m83bb census left, by class: suite "a base: a class template-id over its parameters" 22 = is_integral 13, is_floating_point 3, is_pointer 1 (each `: _raw<typename remove_cv<T>::type>`, a base the read laid, since the `_raw` primary is laid over nothing) and Tuple 5 (`Tuple<T, Rest...> : Tuple<Rest...>`, a pack); self-host 151 = is_integral 149, is_floating_point 2. "A base over its parameters with a constructor, a destructor or an assignment of its own": suite 17, self-host 63 - `__atomic_integral<T> : __atomic_base<T>` (suite 14, self 56), `atomic<T>` (suite 1, self 7) and two cases' `Q`, `VD` (suite 1 each).
(2), c1: classShape no longer refuses a base with specials of its own. The made class's implicit special members are completeClass's over the bases basesUnder laid (the replay's path: a base's constructors, destructor, deleted or non-trivial copy and `operator=` decide them there); the read's, declared knowing no base, are not used (membersFromRead skips them), so classMembers compares implicit specials only for a class with no base over its parameters - the rule a class-typed data member already had. Each of the 17 takes the base's constructors and destructor through completeClass; m83ar's 15 `differ: the number of member functions` in thread-pool were the check comparing the read's implicit set, not the made class.
(1), c1 and c2: a base the read laid as an id over its parameters goes through slice 3j's mapping (overBases: carriedId, or wanted ahead by earlyArgs); basesUnder substitutes each read base under the binding and re-lays the offsets by layBases; laidFromRead counts the read's own base members by the read's bases. c2: argsWanted takes a dependent member's class (`remove_cv<T>` in `typename remove_cv<T>::type`) as the id to map or make first, as the replay makes it reading the head; earlyArgs already passed such an argument once its class is pending.
Probe template-subst-basespecial (by hand and clang 19 `4 4 2 0 / 11 8 / 3 3 / c 1 / 13 / 14 8 8`): right plain, under the flag, under the check and by REF on Linux, by cxx3-msvc plain and under the flag on Windows; under the check its 7 classes `equal` and made from the read. Its `.nonames`: x86_64-linux 11 and arm64-darwin 5 C1/D1 names cxx1 emits and clang emits only for a use (emission, not spelling); x86_64-windows agrees.
src 97,781 -> 97,784 (c1, +3) -> 97,785 (c2, +1); no EXCLUSIONS line moved (exclusions --check 0 uncited, 0 stale on c1).
The measure (the census's specialization lines, REF 701d27a7's build: suite 2,228 made of 2,445 with m83bb's probe, self-host 74,887 made of 75,319): c1 suite 2,252 made of 2,452 (+17 the atomic bases and Q, VD; +7 the probe), self-host 74,950 made of 75,319 (+63: __atomic_integral 56, atomic 7); c2 suite 2,269 made of 2,452 (+17: is_integral 13, is_floating_point 3, is_pointer 1), self-host 75,101 made of 75,319 (+151: is_integral 149, is_floating_point 2). Left in the two reasons: suite Tuple 5 (a pack base, `Tuple<Rest...>`), `__thread_call` (a polymorphic base) and Q<3> (a base over its parameters that is polymorphic) in the suite; self-host `__thread_call` 2.
Gates (REF 701d27a7), on c1: identical plain at -O0, -O1 and -O2 x86_64-linux 2997/0, x86_64-windows 5994/0, arm64-darwin 2997/0, tms6747 2997/0, with `-templates=subst` x86_64-linux 2997/0, x86_64-windows 5994/0, arm64-darwin 2997/0 (no emitted output changed); subst.sh 0 failed, then run.sh 1344/0, run.sh under the flag 1344/0 (988 printed their recorded output, 352 refused as recorded); check census over tests/cases nonzero 353 (REF's set), no class differ, the four member/frame `differ` units REF's (std-function, template-subst-guardedmember, template-subst-memberctor, thread-pool); static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean; Windows cl /W4 /WX clean (full 07:00-07:05, ~5 min), clang 19 and cxx3-msvc (plain and under the flag) the probe the same, names.sh 998/1 (the probe's emission, now its .nonames), overload.sh 46/0. On c2: suite census as above; Windows cl /W4 /WX delta clean (07:23-07:24), names.sh 999/0, overload.sh 46/0, the probe the same by clang 19 and cxx3-msvc both ways; g++ -Werror clean (its build). Self check census on c1: 102 sources, nonzero 0, no class differ. On c2: check census over tests/cases nonzero 353, no class differ, `equal` 2,297 (c1 2,280), the same four member/frame units; identical plain x86_64-linux 2997/0 (-O0/-O1/-O2); static rc 0 (src 97,785). Owed on c2 at the lane's hour (the box queue): the self check census, identical on the other three targets and under the flag, subst.sh and run.sh.
**Next**: Tuple's pack base (`Tuple<Rest...>`, suite 5), then the packs (suite 50) and a type parameter bound to no scalar, class, reference or array (suite 42, self 73).

## M8.3 slice 3p: a type parameter bound to void, to a function type or to a specialization not yet completed, made from the read under -templates=subst (lane m83be, 2026-11-25)
Base 9adbefdd (m83bc c2). The two binding reasons m83bc's census left, by class (BASE's own census): "a type parameter bound to a type that is no scalar, class, reference or array" self-host 73 = `enable_if<1,void>` 49, `enable_if<0,void>` 14 (void), `__fn_model<F, R, {A...}>` 7 and `function<R (A...)>` 3 (function types); "bound to a class template's specialization the instantiation has not completed" self-host 76 = `__deque_iterator<...>` 28 (`deque<vector<DwarfLocal> >` 20 of them), `map<basic_string<char>, int, less<...> >` 24 and `__map_key<...>` 24. classShape's binding loop now takes void and a function type as the replay does, substituted as written (`T *` over `int (int)` is a pointer to a function), and a struct the instantiation has not completed (`pending`): a data member of it by value stays "a data member of an incomplete type", by name, so only a class that reaches it by pointer, reference or in a member body is made - the body is typed where it is used, after the class is complete, as the replay's. No other path changed; the remaining refusal keeps its text for a pointer to a data member and the like.
Probe template-subst-voidbound (by hand and clang 19 `1 6 10 / 2 8 4`): `when<1,void>`, `ptr_to<void>`, `ptr_to<int (int)>`, `Call<int (int)>` (its `F *fn` called from main: a member body calling it is a read gap REF has too, "unread class-members: expected ''", which failed subst.sh's ceiling on c1) and `Link<struct Node<int>>` (bound while Node<int> is being completed) made from the read; Node<int> stays replayed (a data member of a class the read named over its parameters).
src 97,785 -> 97,786 (+1); no EXCLUSIONS line moved (exclusions --check 0 uncited, 0 stale).
The measure (the census's specialization lines, no check): BASE 9adbefdd suite 2,269 made of 2,452, self-host 75,101 made of 75,319 -> suite 2,321 made of 2,459 (2,315 of 2,452 without the probe: +46; the probe 6 of 7), self-host 75,250 made of 75,319 (+149: void 63, function types 10, a pending specialization 76); again 0. Both reasons 0 left on both; left by reason, self-host: a member enumeration with a name 25, a member initialiser 22, a signature over an undeclared id 14, a member typedef 5, a polymorphic base 2, an array 1 (69); suite: a pack or template parameter 50, then 88 in smaller reasons.
Gates (REF 9adbefdd) on c1: check census over tests/cases nonzero 353 (the `.error` set), no class differ, `equal` 2,349; self check census 102 sources, nonzero 0, no class differ, `equal` 75,277; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean (its build); Windows cl /W4 /WX full clean (21:34:55-21:38:42, ~4 min, the box shared), clang 19 and cxx3-msvc (plain and under the flag) the probe the same, names.sh 1000/0, overload.sh 46/0.
Further gates on c1 (Linux, REF 9adbefdd): self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; run.sh 1345/0, then run.sh under the flag 1345/0 (989 printed their recorded output, 352 refused as recorded); subst.sh 1 failed - the probe's first form (`Call::run` calling `fn(x)` in its body: "unread class-members (1): expected ''", REF's read the same), so the probe calls `c.fn` from main (REF and c1 right plain and under the flag on Linux, no unread). Owed at the hour (the shared box queue): subst.sh on the final probe, identical plain on tms6747 and x86_64-windows and under the flag on three (identical plain x86_64-linux 3000/0 and arm64-darwin 3000/0 at -O0/-O1/-O2 measured on the Windows box, cxx3-msvc REF 9adbefdd against c1), the owed m83bc c2 gate against 701d27a7 (queued in ~/slot).
**Next**: a member enumeration with a name (self 25), a member initialiser (self 22), a signature over an undeclared id (self 14); in the suite the packs (50).

## M8.3 slice 3q: a member initialiser naming no template parameter, made from the read under -templates=subst (lane m83bf, 2026-11-25)
Base fbbc27a1 (m83be). The owed checks first, against REFs built on Linux (9adbefdd, 701d27a7) - their rows are in this lane's second entry below. BASE's census (its own build, no check) is m83be's: suite 2,321 made of 2,459, self-host 75,250 made of 75,319. By class, self-host: "a member enumeration with a name, a base or a declarator" 25 = `EntryOf<opt::MInst, opt::MDirective>` (OptCore.h, `enum Kind { Inst, Label, Directive }; Kind kind = Inst;`), "a member initialiser" 22 = `FlowOf<EntryOf<...> >` (`bool dominated = false; bool solutionsDirty = true;`), the signature over an undeclared id 14; suite: a member initialiser 3 (`Box<int>`, `Box<double>`, `Box<long>`).
c1: fromReadWhyNot no longer refuses every member initialiser - only one whose tokens name a template parameter ("replayed: a member initialiser naming a template parameter"; `T value = T();` - REF refuses it too, "'T' was not declared", so it is no probe) and one beside a written constructor (by the read's constructor key; Built<int> below has its constructor declared and the key does not answer for it, so the guard did not fire there and the class is made and right). classFromRead copies each read member's `memberInit_` and `memberInitCopy_` entry (the token after `=`, or the `{`) from the read's tag to the made tag before completeClass, so the implicit default constructor's `work` and its body are the replay's: Parser::memberInitialiser reads the same tokens in the made class's scope and namespace. Nothing else changed.
Probe template-subst-memberinit (by hand `1 12 7 24 / 1 12 / 5 2`): `Flow<int>`, `Flow<char>` (initialisers, a member function template, no constructor) and `Built<int>` made from the read; right plain, under the flag and by REF on Linux; under the check its 3 classes `equal`.
src 97,786 -> 97,803 (+17); no EXCLUSIONS line moved (exclusions --check 0 uncited, 0 stale).
The measure (no check): suite 2,321 -> 2,325 made of 2,462 (the probe 3 of 3; `Box<long>` +1; `Box<int>`, `Box<double>` now "naming a template parameter"), self-host 75,250 -> 75,272 made of 75,319 (+22: FlowOf); again 0.
Gates on c1 (BASE fbbc27a1's build): static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); Windows cl /W4 /WX delta clean (22:37:25-22:41:11, ~4 min, cl alone). The rest are in the next entry.

## M8.3: the owed checks of slices 3o (c2) and 3p, slice 3q's gates, and a named member enumeration left half made (lane m83bf, 2026-11-25)
**Owed, m83be's slice 3p** (fbbc27a1 against REF 9adbefdd, both built on Linux): identical plain at -O0/-O1/-O2 tms6747 3000/0, x86_64-windows 6000/0; with `-templates=subst` x86_64-linux 3000/0, arm64-darwin 3000/0, tms6747 3000/0; subst.sh 0 failed (the final probe template-subst-voidbound; ceiling self 26 of 26).
**Owed, m83bc's c2** (9adbefdd = 815dde07's src, against REF 701d27a7): self check census 102 sources, nonzero 0, no class differ; subst.sh 0 failed; run.sh 1344/0, run.sh under the flag 1344/0 (988 printed, 352 refused as recorded). Its identical rows on arm64-darwin and tms6747 plain and the three under the flag were still in the box queue at the hour: a first run read 2997/2997 differ because the lane's own c2 chain removed the NEW exe it shared a name with (`~/m83bf-c2.exe`) - void, not a difference; rerun as `o2`: identical plain at -O0/-O1/-O2 arm64-darwin 2997/0, tms6747 2997/0, with `-templates=subst` x86_64-linux 2997/0, arm64-darwin 2997/0, tms6747 2997/0. m83bc's c2 owes nothing now.
**Slice 3q's gates** (c1, 42078132, against BASE fbbc27a1): subst.sh 0 failed, then run.sh 1346/0, run.sh under the flag 1346/0 (990 printed, 352 refused as recorded); check census over tests/cases 1,353 units, nonzero 353 (the `.error` set), no class differ; self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; identical on the Windows box (cxx3-msvc REF fbbc27a1 against c1, -O0/-O1/-O2) plain x86_64-linux 3003/0, arm64-darwin 3006/0, tms6747 3006/0 - x86_64-windows and the four under the flag stopped at the hour, owed; clang 19 and cxx3-msvc (plain and under the flag) the probe the same; names.sh 1000/1 - the probe's first form, then its `.nonames` (x86_64-linux 3 C1 names cxx1 emits and clang emits only for a use; x86_64-windows 7 and arm64-darwin 10 agree), the probe rc 0 with it; overload.sh 46/0. Self check census on c1: 102 sources, nonzero 0, no class differ. No emitted output changed where measured.
**A named member enumeration** (self-host 25, `EntryOf`; `boxkit/m83bf-c2-wip.patch`, its probe `boxkit/m83bf-c2-namedenum.cpp`, not committed): memberEnums takes `enum Kind { ... };`, classBeyond refuses one beside a written member function, readStands maps the read's enumeration (found by a data member of it) to the made class's once staticsFromRead declared it, and classFromRead lays the data members after the statics. cl /W4 /WX clean; the probe right by cxx3-msvc plain and under the flag (`4 3 0 5 5 / 9 4`, clang 19 the same); under the check `Entry<int,char>`, `Entry<short,double>` made from the read but staticsHeld says "differ: a member enumerator's value or type" (its `$held` enumeration is not the replay's `tag::Kind`) and "the class skip before the replay and after it" - what is left before it can land.
**Next**: the named member enumeration's check (staticsHeld's held enumeration mapped to the replay's), then the signature over an undeclared id (self 14); in the suite the packs (50).

## M8.3 slice 3r: a named member enumeration in a class of no written member function, made from the read under -templates=subst (lane m83bh, 2026-11-25)
**The measure** (the census's specialization lines, no check, Linux): BASE 8e869e3c (slice 3q) suite 2,325 made of 2,462, self-host 75,272 made of 75,319 -> suite 2,328 made of 2,465 (+3: the probe's `Entry<int,char>`, `Entry<short,double>`, and one case's class that was "a member enumeration with a name"; the probe's `Tagged<int>` replays), self-host 75,297 made of 75,319 (+25: `EntryOf<opt::MInst, opt::MDirective>`, OptCore.h); again 0. Left, self-host: the undeclared id 14, a member typedef 5, a polymorphic base 2, an array 1 (22).
**Owed by slice 3q** (42078132's src, built on Linux, against REF fbbc27a1): identical at -O0/-O1/-O2 plain x86_64-windows 6006/0; with `-templates=subst` x86_64-linux 3003/0, x86_64-windows 6006/0, arm64-darwin 3003/0, tms6747 3003/0. Slice 3q owes nothing now.
**What it does** (m83bf's `boxkit/m83bf-c2-wip.patch`, finished): memberEnums takes `enum Kind { ... };` (no base, no declarator, ended by its braces), classBeyond refuses one beside a written member function, readStands maps the read's enumeration (found by a data member of it) to the made class's once staticsFromRead declared it, and classFromRead lays the data members after the statics. Two faults kept it from landing: (1) memberTypedefs listed the read's own enumeration as a member typedef (`Kind`, keyed by its own tag - the class's own tag was already skipped), so classFromRead declared `Entry<int>::Kind` as the read's enumeration before staticsFromRead read the real one, readStands mapped the read's to itself, and the data member kept `Entry<$T0>$read0::Kind` (cxx3-msvc under the flag: "'pick' cannot be given a name the linker can hold"; `a.kind = Entry<int>::Label` refused) - memberTypedefs now skips an enumeration at its own tag as it skips a class; (2) under the check staticsHeld held the `tag::$held::Kind` enumerators against the replay's `tag::Kind` ones by type identity - it now takes the held enumeration for the replay's of that name, kind and scopedness. The probe `template-subst-namedenum` (`4 3 0 5 5 / 9 4`; `Entry<int,char>`, `Entry<short,double>` made, `Tagged<int>` replayed beside its member function), its `.nonames` (x86_64-linux 2 C1 names, emission).
**The undeclared id** (self-host 14): all `deque` - `deque<vector<DwarfLocal>>` 10, `deque<string>`, `deque<int>`, `deque<Chunk>`, `deque<Mnemonic>` 1 each: `typedef __deque_iterator<T, T &, T *, deque> iterator;` names the class being made as an argument, which the replay makes inside the class body (idsInside's path for its own template, not idsBefore's); not taken this hour.
**Gates on c3** (this commit's tree, built on Linux, against REF fbbc27a1 unless said): the self check census (-check=templates over the 102 sources, x86_64-linux) nonzero 0, no class differ; the probe's check no differ; subst.sh 0 failed (ceiling self 26 of 26); identical at -O0/-O1/-O2 plain x86_64-linux 3006/0, x86_64-windows 6012/0, arm64-darwin 3006/0, tms6747 3006/0, with `-templates=subst` x86_64-linux 3006/0, x86_64-windows 6012/0 (arm64-darwin and tms6747 under the flag stopped at the hour, owed); run.sh 1347/0, run.sh under the flag 1347/0 (991 printed, 352 refused as recorded); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); cl /W4 /WX clean, full (23:30:06-23:32:51) and the delta (23:38:24-23:42:24, ~4 min), cl alone; on the Windows box clang 19 = cxx3-msvc plain = under the flag on the probe; names.sh on the probe 1 failed then `.nonames` (x86_64-windows 5, arm64-darwin 7 agree); overload.sh 46/0. On the Windows box (target x86_64-windows) the same measure moves alike: suite 2,325 -> 2,328 of 2,465, self 75,257 -> 75,282 of 75,304; its check census says "differ: the class skip before the replay and after it: differ: a held member body's function" (suite 2, self 44) - BASE's as well (BASE built there, AsmExpr.cpp the same line; none under `-arch x86_64-linux`): x86_64-windows's own, not this slice's, owed. The suite's check census on Linux was in the queue at the hour, owed.
src/: 97,803 -> 97,834 lines (Templates.cpp +31).
**Next**: the undeclared id (`deque`'s iterator over the class itself, self 14), then the suite's member typedef (12) and nested class (10); the x86_64-windows check's "a held member body's function" (BASE's as well) named.

## M8.3: the x86_64-windows check differ - two overloads of one read name - and slice 3r's owed checks (lane m83bj, 2026-11-26)

**The measure, unchanged by this commit:** suite 2,328 made of 2,465, self-host 75,297 made of 75,319 (x86_64-linux; x86_64-windows suite 2,328 of 2,465, self 75,282 of 75,304). The commit makes no class; it makes the check's verdict on x86_64-windows true, which every later slice's Windows census leans on.
**The root** (found with a temporary diagnostic, not committed): every one of the differ's lines (suite: `unordered-map` 2, `map-move-only-mapped` 1; self: all 44) is `unordered_map<...>::erase(iterator)` beside `erase(const_iterator)`. A read's class over `$T0` has no Microsoft name, so `Linkage::memberSymbol` gave its members `$pattern$` + the signature by `Type::describe()` - and describe says "dependent member type" for every `typename X::y`, so the two overloads took one read symbol. classMembers' `readSymbol_` (read symbol -> replay's) then held one entry for both, and classSkip found one held body mapped to the other's function. Itanium names the read, so x86_64-linux never met it. The fix, one path: `Linkage::patternSymbol` spells a read member's name by `TypeTable::patternKey` (injective on pattern types: `$T0::iterator`), with its ref-qualifier and const, and the four fallbacks that spelled it by describe (Linkage::memberSymbol, Classes' readName for implicit specials, ParserClass's constructor, ParserTopLevel's member template) call it. `$pattern$` names are never emitted. No wrong output was shown (identical under the flag on x86_64-windows was 0 differ at BASE), so no case.
**The undeclared id** (self 14, all `deque`), not taken: earlyArgs refuses `__deque_iterator<T, T &, T *, deque>` because its last argument is the read class itself (`overParameters`), and before the class door there is no type for `deque<int>` to stand for it - the replay declares the tag, then makes the iterator inside the body. Taking it needs the made tag declared (incomplete) before idsBefore, which moves the order the tag and its ids are declared in; held by the check, that is the next slice's whole question.
**Slice 3r's owed checks** (539d620f/d22007f6 against a REF0 built from 8e869e3c on Linux): identical under `-templates=subst` arm64-darwin 3006/0, tms6747 3006/0 at -O0/-O1/-O2; the suite's check census of d22007f6 on Linux (x86_64-linux, 1354 units): no class differ. Nothing of slice 3r is owed now.
**Gates on this commit's tree**: the x86_64-windows check census (cxx3-msvc, -check=templates over the suite and the 102 self-host sources) class differ 0 on both (BASE: suite 3, self 44), self nonzero 0, the measure there unchanged; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile of the four changed files rc 0 plain and under the flag; g++ -Werror clean; cl /W4 /WX clean (the delta 00:40:15-00:46:01, beside the DIAG census, not alone). names.sh 1002 passed, 0 failed, and overload.sh 46/0 on Windows. The Windows tree carried this commit plus the reverted diagnostic's message text (it prints only on a differ). Owed at the hour (the Linux slots were held by the wave gate; the Windows identical x86_64-windows was stopped unfinished at the hour): identical against REF fbbc27a1 on four targets, plain and under the flag, run.sh both ways, subst.sh, the Linux check censuses of this tree.
src/: 97,834 -> 97,842 lines (Linkage.cpp +6, Linkage.h +2).
**Next**: the undeclared id as above (deque 14), then the suite's member typedef (12) and nested class (10).

## M8.3 slice 3s: an id naming the class itself as an argument (deque's iterator), its tag declared first, made from the read under -templates=subst (lane m83bl, 2026-11-26)
**The measure** (the census's specialization lines, no check, x86_64-linux): BASE 046390a4 suite 2,328 made of 2,465, self-host 75,297 made of 75,319 -> suite 2,336 made of 2,471 (+8: the probe's 6 of 6, `deque<int>` and `deque<basic_string<char>>` of deque-both-ends), self-host 75,311 made of 75,319 (+14: every `deque`, the reason "a member function's signature over a type the binding does not carry: a class template-id over its parameters the program had not declared" now 0 on both); again 0. Left, self-host 8: a member typedef 5, a polymorphic base 2, an array 1.
**What it does.** `typedef __deque_iterator<T, T &, T *, deque> iterator;` names the read class itself as an argument, which earlyArgs refused (the read is over its parameters, and before the class door deque<int> had no type). Templates::namesItself finds such an id among the read's typedefs and signatures; for a primary (no partial) makeClass then declares the made tag first - `structType` with its specialization, namespace and class key, as the `patternUses_` path declares an incomplete one, so the id's names spell the class as a specialization on both ABIs (names.sh on the probe) - and readStands maps the read to it in idsBefore's, classBefore's, the binding's (`bound`) and the held check's substitutions. idsBefore then makes the iterator before the class through makeClass (its D bound to a struct the instantiation has not completed, slice 3p's `pending`), and classFromRead completes the same Type. The order change: the replay declares the tag at its head and makes the iterator reading the body; this declares the tag, makes the iterator, then lays the class - the same order, with no member of deque<int> read between. A c1 without the specialization on the early tag spelled `Udeque<int>@@` in the iterator's Microsoft names (masm refused it): the probe found it.
Probe template-subst-ownarg (`Walk<T, T &, Row>` and `Walk<T, const T &, const Row>` in `Row<T>`, by hand and clang 19 `abc 18 567`): right plain, under the flag, under the check and by REF under the flag on Linux, by cxx3-msvc plain and under the flag on Windows; under the check 6 classes `equal` and made from the read, no differ. Its `.nonames`: x86_64-linux 3 C1 names (emission); x86_64-windows 22 and arm64-darwin 25 agree.
**The suite's member typedef (12) and nested class (10)**, by class (BASE's census, x86_64-windows): member typedef = `ratio_divide<ratio<..>, ratio<..>>` 10 (`typedef typename ratio<R1::num * R2::den, R1::den * R2::num>::type type;` - an id whose arguments are value expressions over another class's static members) and `decay<...>` 2 (a private typedef of `remove_reference<T>::type` feeding nested `conditional<...>::type`s); nested class = Tree<int>, Tree<long long> (nested-class-of-class-template), B<int>, Vec<int>, Vec<Item>, Pair<int,2>, Tree<double>, Tree<int> (template-injected-name-member), IsInt<int *>, IsInt<int> - each a class written inside the class template, which classBeyond refuses whole. Neither taken this hour: ratio_divide wants argValued over `R1::num * R2::den`, decay a chain of dependent member types, and a nested class needs its own door.
src/: 97,842 -> 97,879 lines (Templates.cpp +36, Templates.h +1).
**m83bj's owed checks** (e62d71d1/046390a4, BASE built on Linux and on Windows, against a REF built from fbbc27a1 on each): run.sh 1347/0 plain and under the flag (991 printed, 352 refused as recorded), then subst.sh 0 failed (ceiling self 26 of 26); the suite's check census on Linux 1,354 units, nonzero 353 (the `.error` set), no class differ; the self check census 102 sources, nonzero 0, no class differ, 75,297 made of 75,319. The identicals were run on this slice's tree, which holds BASE whole and changes nothing without the flag (namesItself is asked under `substitute_` alone) - below; and BASE's own x86_64-windows plain run on Windows (cxx3-msvc copied as `m83bl-base.exe`) differs only in 3 refusing cases x 6 (c6x-lowering-refusals, lowering-named-return-in-handler, lowering-return-destructor-in-handler), whose `.err` names the exe by its file name: the copy's name, not a difference. e62d71d1 owes nothing else.
**Gates on c2** (this commit's tree, against REF fbbc27a1): identical at -O0/-O1/-O2 plain x86_64-linux 3009/0, arm64-darwin 3009/0, tms6747 3009/0, with `-templates=subst` x86_64-linux 3009/0, arm64-darwin 3009/0, tms6747 3009/0 (Linux) - the covered shapes include deque-both-ends and the probe, so the tag declared first moves no emitted output; the Windows measure (cxx3-msvc, x86_64-windows) moves alike: suite 2,328 -> 2,336 of 2,471, self 75,282 -> 75,296 of 75,304; self-compile of Templates.cpp and ParserType.cpp rc 0 plain and under the flag; static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean (its build); cl /W4 /WX clean (the delta 01:30:26-01:36:26, ~6 min, beside the BASE identical, not alone); names.sh on the probe 0 failed with its `.nonames`; overload.sh 46/0. Owed at the commit, running: identical x86_64-windows plain and under the flag, the check censuses over the suite and the self-host, run.sh under the flag, subst.sh, the full stage-0 self-host (102 sources) - the next entry, or the next wave's gate.
**Next**: the suite's ratio_divide (10, a value id over static members) and nested classes (10); the self-host's member typedef (5).

## M8.3 slice 3s's late gates, and two x86_64-windows identicals the cap stopped (lane m83bl, 2026-11-26)
**The measure, unchanged by this entry:** suite 2,336 made of 2,471, self-host 75,311 made of 75,319 (28ab26d8).
**Gates on 28ab26d8's tree** (built on Linux): run.sh under the flag 1348/0 (992 printed their recorded output, 352 refused as recorded), then subst.sh 0 failed (ceiling self 26 of 26); the suite's check census 1,355 units, nonzero 353, no class differ, its `differ` lines (member and frame, 12 units) the same as BASE's line for line; the self check census 102 sources, nonzero 0, no class differ, 75,311 made of 75,319.
**Stopped at the 25-minute cap, a defect, not retried** (the Windows box shared, both runs at -O0/-O1/-O2 over both spellings): identical x86_64-windows under `-templates=subst` (28ab26d8's cxx3-msvc against REF fbbc27a1's) compiled every case and was stopped comparing, with no `DIFFERS` line printed; BASE's x86_64-windows plain likewise, its only `DIFFERS` lines the copied exe's name in 3 refusing cases (so the line of the entry above, "differs only in", is what it printed before the cap, not a verdict). Owed: identical x86_64-windows plain and under the flag, split by level so each fits the cap; the full stage-0 self-host of 28ab26d8 (102 sources; queued in ~/slot behind the other lanes' jobs for 17 minutes and stopped by PID at the hour).

## M8.3 slice 3q: a pack counted in a static's initialiser, and a `>` inside parentheses, made from the read under -templates=subst (lane m83bg, 2026-11-25)
Base fbbc27a1 (m83be, slice 3p). The census's reasons by count on BASE (no check): suite 2,321 made of 2,459 - a pack or template parameter 50, a non-type parameter named outside its member bodies 12, a member typedef over a type the binding does not carry 12, a nested class 10, then 54 in smaller reasons; self-host 75,250 made of 75,319 - m83be's six (member enumeration with a name 25, member initialiser 22, undeclared id 14, member typedef 5, polymorphic base 2, array 1), m83bf's. The pack family's 50 by BASE's tags: template template parameters 29 (`Holder<Box,int>`, `H1<Tup>`, `Def<Cell>` and the like: the read's `C<T>` is the stand-in `$TT0<$T1>`, which no substitution maps yet) and non-type packs 21 - counted, `static const int size = sizeof...(Ns)`, 16 (`List`, `Seq`), and `Max<N, M, Rest...>` 5, whose `Max<(N > M ? N : M), Rest...>` closed the scan's `<` at the comparison and left `Rest...` unexpanded.
classShape's first scan lets a pack through where it is counted, `sizeof...(Ts)`, and holds its token for the second scan, which takes it only in a static data member's initialiser (staticInit: staticsFromRead reads it with the pack bound by bindTemplateParameters, the replay's path) or in a bare class's unnamed enumeration (slice 3n's); anywhere else, or a token the second scan does not reach, it stays "a pack or template parameter, bound by name". The bracket scan pops `<` only for a `>` that meets one: inside parentheses `>` compares. No other path changed.
Probe template-subst-packcount (by hand and clang 19 `0 1 3 / 0 30 4 5 / 2 0 2 3 / 9 7`): `Count<{}>`, `Count<{int}>`, `Count<{int,char,long long}>`, `Values<{}>`, `Values<{1,2,3}>`, `Head<int,{char,double}>`, `Head<char,{}>`, `Both<{int,int}>` (two counts in one declaration) and `Max<{3,9,4}>`, `Max<{2,7,1,5}>` with the five classes they make - 15 made from the read, 15 equal under the check.
src 97,786 -> 97,797 (+11, Templates.cpp alone); no EXCLUSIONS line moved (exclusions --check 0 uncited, 0 stale).
The measure (the census's specialization lines, no check): BASE fbbc27a1 suite 2,321 made of 2,459, self-host 75,250 made of 75,319 -> suite 2,357 made of 2,474 (2,342 of 2,459 without the probe: +21 - counted 16, Max 5; the probe 15 of 15), self-host 75,250 made of 75,319 (no pack reason there; unchanged); again 0. The pack reason 50 -> 29 (the template template parameters, all of them); `Derived<{7,8}>` and `Wide<...>`, a non-type pack in a base, stay "a non-type parameter named outside its member bodies", as on BASE.
Gates (REF fbbc27a1) on the final code: static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean (its build); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; Windows cl /W4 /WX full clean on the first half (23:27:25-23:29:25, ~2 min) and delta clean on the final (23:30:20-23:33:06, ~3 min, cl first and alone), clang 19 and cxx3-msvc (plain and under the flag) the probe the same, names.sh 1001/0, overload.sh 46/0. Linux (REF fbbc27a1 against dec996fe's code): subst.sh 0 failed (run 1346/0 at -O0 and -O2, typed 993/0, self 102 of 102, the ceilings held), then run.sh 1346/0 and run.sh under the flag 1346/0 (990 printed their recorded output, 352 refused as recorded); identical plain at -O0/-O1/-O2 x86_64-linux 3003/0, x86_64-windows 6006/0, arm64-darwin 3003/0, tms6747 3003/0, and under -templates=subst x86_64-linux 3003/0, x86_64-windows 6006/0, arm64-darwin 3003/0 - no emitted output changed at any level, the covered shapes (a counted pack in a static, the `Max` recursion) among them through the probe.
**Next**: the template template parameters (suite 29). The read lays `C<T>` as an id over the stand-in `$TT<i>` (bindTemplateParameters' pattern branch), and the binding carries the argument as a template-name type (`binding[i]->templateName()`, its other branch); classShape's binding loop skips such a parameter and its first scan refuses the name. The slice: let `C` through where an id follows (`C<`), carry index -> template name in Substituted, and have specializationUnder (and earlyArgs) rename `$TT<i>` to it before finding or making the specialization, as the replay's `templates_[C]` does; held by the check's class equality. Then a non-type pack in a base (suite 3) and `V<N + K>` in a member template's return type (suite 8).

## M8.3 slice 3s: a template template parameter named as an id, `C<T>`, made from the read under -templates=subst (lane m83bi, 2026-11-26)
Base f83482bc (m83bg, slice 3q). Owed by m83bg, first: BASE's -check=templates censuses (dec996fe's code, REF fbbc27a1's set) - suite 1,353 units, nonzero 353 (the `.error` set, REF's), no class differ, `equal` 2,385; self-host 102 sources, nonzero 0, no class differ; the measure 2,357 made of 2,474 and 75,250 made of 75,319 as recorded.
The suite's 29 template template classes by shape (the pack reason's whole remainder): a data member, signature, typedef or base `C<T>` / `C<int>` / `C<C<T>>` / `typename C<T>::type` over a class template bound by name (Holder, Two, Nest, Typed, Def, W, Kind's partials and the like) 16; the template bound to an alias template (`Holder<Alias, int>`, `Holder<ns::NAlias, ...>`) 4; `C<int>` where the bound template's list differs - a pack (`H1<Tup>`, `H2<Tup>`, `HP<Box>` over `template <class...> class`), a default (`H1<Two>`, `H1<BoxN>`) or a value (`HN<Arr>`, `HN<ArrT>`) 7; a base `: C<int>`, which the read lays as nothing (`Derived<Box>`) 1; the parameter passed on as an argument (`Pass<Cell>`, `Holder<C, int>`) 1.
c1: Substituted carries `templates`, index -> the class template the binding names (classShape's binding loop, from `binding[q]->templateName()` as bindTemplateParameters' second branch takes it; an alias, a template keyed under another name or a stand-in maps nothing); idUnder is readId with `$TT<i>` renamed to it, used by specializationUnder and earlyArgs, so a stand-in id finds or (idsBefore) makes the bound template's specialization under the key the replay's `templates_[C]` gives it. classShape's first scan lets a template template parameter through where an id follows (`C<`); anywhere else it stays "a pack or template parameter, bound by name". c2: a data member `typename X<..>::type t;` whose class the program had not declared wants it first (idsBefore makes it, as the replay does reading the member) - REF's shape too (`TypedO<double>` over `Cell<T>` showed "differ: the class skip before the replay and after it" on BASE), which `Typed<Cell, double>` would have raised as a new class differ. c3: a stand-in id another read laid first (`peek(const C<int> &)` before `Two`'s `C<int>`) is a specialization of `$TT<i>`, which dependentType calls dependent; the data member's refusal waits until the mapping below it. c4: c3's suite check census found two class differ in template-template-members, both `W<Box>` (`template <class U> C<U> make(U)`, "a member template's class binding"): a class naming its template template parameter as an id beside a member template is "replayed: a member template beside a template template parameter", as the non-type rule beside it.
Probe template-subst-ttparam (by hand and clang 19 `7 k 11 / 1 x 2.5 1 0.5 / 10 3 4 / 40 40 14 2 / 12 13 1 7`): right plain, under the flag and under the check on Linux and by cxx3-msvc both ways on Windows; 22 of its 25 specializations made from the read; Derived<Box> (a base laid as nothing), Holder<Alias, int> and Pass<Cell> stay replayed by name; under the check no class differ.
src 97,797 -> 97,826 (c3, +29: Templates.cpp +27, Templates.h +2) -> 97,829 (c4, +3); no EXCLUSIONS line moved (exclusions --check 0 uncited, 0 stale).
The measure (the census's specialization lines, no check, Linux): BASE 2,357 made of 2,474 -> c3 2,395 made of 2,499 -> c4 2,394 made of 2,499 (2,372 of 2,474 without the probe: +15, the template template classes, `W<Box>` back to replayed; the probe 22 of 25); self-host 75,250 made of 75,319 -> 75,611 made of 75,680 - the +361 are `map<int, string>`'s 15 classes in the 24 units that include Templates.h, whose Substituted now names one, all made; replayed 69 on both, unchanged (c3's census; c4 refuses only a class with a template template parameter, which the self-host has none of). The pack reason 29 -> 1 (`Pass<Cell>`); the 13 left above moved to the reasons they meet next (a data member of a class the read named over its parameters, a base laid as nothing). Measured on c3's tree, which lacked m83bg's probe (its 15, all made, unchanged by this slice, added back above).
Gates (REF fbbc27a1) on c3: static rc 0 (exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean (its build); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; Windows cl /W4 /WX full clean on c1 (00:18:53-00:20:38, ~2 min, cl first and alone), delta on c2 (00:22:20-00:26:50) and full on c3 (00:39:21-00:45:36, ~6 min, the box shared); clang 19 and cxx3-msvc (plain and under the flag) the probe the same on c1, c2 and c3; names.sh 1002/0 and overload.sh 46/0 on c2 (c3 and c4 change only paths -templates=subst takes). Linux on c3: subst.sh 0 failed (self 102 of 102, the ceilings held), run.sh 1346/0 (990 printed their recorded output, 352 refused as recorded); check census over tests/cases nonzero 353, `equal` 2,407, the two class differ above (c4's reason). Linux on c4: probe right plain, under the flag and under the check; check census over tests/cases nonzero 353 (the `.error` set), no class differ, `equal` 2,407; static rc 0 (src 97,829, exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag; g++ -Werror clean (its build); subst.sh 0 failed (self 102 of 102, the ceilings held), then run.sh 1346/0 and run.sh under the flag 1346/0 (990 printed their recorded output, 352 refused as recorded). Windows on c4: cl /W4 /WX delta linked (00:50:50-01:00, ~9.5 min, past the 9-minute cap on a box shared with identical and another session - a defect of the box's load, recorded; /WX, so no warning), clang 19 and cxx3-msvc both ways the probe the same, template-template-members under the check no class differ. Identical (REF fbbc27a1 against c4, Linux): under -templates=subst x86_64-linux 3003/0 at -O0/-O1/-O2 (no emitted output changed, the probe and the 15 template template classes among them); self check census on c3 102 sources, nonzero 0, no class differ. Owed at the hour (the box queue: the four plain identicals queued 5 minutes in ~/slot, stopped by PID; the Windows identical on c3 reached no target in 17 minutes, stopped): identical plain on four targets and under the flag on arm64-darwin and x86_64-windows, the self check census on c4.
**Next**: the bound template's list against the id's - a default argument filled (`H1<Two>`, `H1<BoxN>`), a pack parameter given members (`H1<Tup>`), a written value (`HN<Arr>`) - 7; an alias template bound (4); `: C<int>` as a base (1); then the non-type parameters (suite 12: a non-type pack in a base 3, `V<N + K>` in a member template's return type 8).

## M8.3 slice 3t: a template template parameter's id laid by the bound template's list, an alias bound, the parameter passed on, made from the read under -templates=subst (lane m83bk, 2026-11-26)
Base 3d35b666 (m83bi, slice 3s; f520ae79's code). Owed by m83bi, first (REF fbbc27a1 against BASE, Linux, -O0/-O1/-O2): identical plain x86_64-linux 3006/0, arm64-darwin 3006/0, x86_64-windows 6012/0, tms6747 3006/0, and under -templates=subst arm64-darwin 3006/0, x86_64-windows 6012/0 - nothing changed without the flag, nor under it on the two targets m83bi had not reached.
The 14 template template classes m83bi left: c1 lays a stand-in's list by the bound template's (listUnder, from idUnder once `$TT<i>` is renamed): each pack's members one argument, a pack parameter takes the rest, a missing parameter its default through defaultTemplateArgument (the deduced path's, the parameters before it bound), a written value kept (`C<3>`, which earlyArgs and specializationUnder take only in a renamed list) - `HP<Box>`, `H1<Tup>`, `H1<Two>`, `H1<BoxN>`, `H2<Tup>`, `HN<Arr>`, `HN<ArrT>`, 7. c2: an alias template whose type-id is a class template's id over its own type parameters in order (`using Alias = CBox<T>;`, aliasedClass) maps the parameter to that class template - `Holder<Alias, int>`, `Holder<Alias, char>`, `Holder<ns::NAlias, int>`, `Holder<ns::NAlias, char>`, 4. c3: the parameter as an id's whole argument (`Holder<C, int>`): classShape's first scan lets it through, idUnder names it as the bound template, earlyArgs and specializationUnder take a class template argument - `Pass<Cell>`, 1. c4: listUnder's loop by index (a range-based `for` over a `TemplateArg` by value is outside the SSA lowering: Templates.cpp's self census under the flag on c3 failed there, rc 1). Left: `W<Box>` (a member template beside a template template parameter), `Derived<Box>` (a base laid as nothing), `Nest<Cell, int>` (a data member of a class the read named over its parameters).
Probe template-subst-ttlist (by hand and clang 19 `h 11 12 0.5 / 1 2 3 4 5 6 7 8 9 10 / a b z / 4 8 16 8 4 2`): a pack parameter taking one argument and two, a pack's members spread over a class's parameters, defaults (`class U = T` over the parameter before it, `int N = 2`, `class T = char` after a written value), and two aliases (one in a namespace) - 21 of 21 made from the read; template-subst-ttparam's `Holder<Alias, int>` and `Pass<Cell>` made too.
src 97,829 -> 97,906 (+77: Templates.cpp +75, Templates.h +2); EXCLUSIONS' three Templates.cpp citations re-pointed (2253, 2305, 3002 -> 2261, 2313, 3010; exclusions --check 0 uncited, 0 stale).
The measure (the census's specialization lines, no check, Linux; the same on Windows under -arch x86_64-linux): BASE 2,394 made of 2,499 -> 2,428 made of 2,520 (2,407 of 2,499 without the probe: +13; the probe 21 of 21); self-host 75,611 made of 75,680, unchanged (it has no template template parameter); again 0. The pack reason 1 -> 0.
Gates (REF fbbc27a1) on c4 = c307eea8's code: static rc 0 (src 97,906, exclusions 0/0, comments 0, memory 89, ast-residue 2012/298 not raised, ide up to date); g++ -Werror clean (its build); self-compile Templates.cpp and ParserType.cpp rc 0 plain and under the flag (c3's Templates.cpp under the flag was the range-for refusal above); check census over tests/cases (Linux and Windows under -arch x86_64-linux) nonzero 353 (the `.error` set), no class differ, `equal` 2,456; self check census (Windows, -arch x86_64-linux) 102 sources, nonzero 0, no class differ; the probe right plain, under the flag and under the check on Linux, and clang 19 and cxx3-msvc both ways the same on Windows (with template-template-pack, template-subst-ttparam, template-template-alias); run.sh 1348/0 and run.sh under the flag 1348/0 (992 printed their recorded output, 352 refused as recorded), then subst.sh 0 failed (self 102 of 102, the ceilings held); identical plain x86_64-linux 3009/0 and arm64-darwin 3009/0 at -O0/-O1/-O2; Windows cl /W4 /WX full clean on c1 (01:16:22-01:21:23, ~5 min) and delta clean on c2 (01:21:30-01:25:45), c3 (01:26:54-01:31:39) and c4 (01:41:07-01:46:22), cl alone each time; names.sh 1003/0 and overload.sh 46/0 on c4. Then identical plain x86_64-windows 6018/0 and tms6747 3009/0 - no emitted output changed without the flag on any target. Owed at the hour (the Linux slots shared with lane m83bl; my jobs queued 12-17 minutes, stopped by PID at 02:07): identical under -templates=subst on all four targets (the covered shapes: the probe's 21 and the 13 suite classes above).
**Next**: the three template template classes left (`W<Box>`'s member template, `Derived<Box>`'s base `: C<int>`, `Nest<Cell, int>`); then the non-type parameters - "a non-type parameter named outside its member bodies" 12 (`V<2..6>`, `W<2,5,7>`, `P<2,3>`, a non-type pack in a base `Wide<...>`, `Derived<{7,8}>`) and "a member template beside a non-type parameter" 7 (`V<3>`, `V<4>`, `Mx<3,3>`, `Vals<{1,2,3}>`).

## M8.3: m8-3bk merged into m6d-w47 - one path for a template template parameter's `$TT<i>` (lane w47merge, 2026-11-26)
**The measure** (the exact census's specialization lines, no check, x86_64-linux): merged 399be7a5 suite 2,531 made of 2,605, self-host 75,672 made of 75,680; against REF 486b8f08 (the m8-3bl side, measured here) suite 2,450 of 2,544, self 75,311 of 75,319, and m8-3bk 64583d82 suite 2,428 of 2,520, self 75,611 of 75,680 (its own numbers, re-measured). Lost against m8-3bk: none (its probe, renamed below, 23 of 25 as before). Lost against REF: one suite class, `W<Box>` in template-template-members - main's templateUnder made it; the kept path refuses it ("a member template beside a template template parameter", m83bi's c4), and a Windows variant without that refusal makes it with 2 class differ under -check=templates (a member template's class binding; the class skip before the replay and after it): owed to the next slice. The self-host's other differences lie only inside the three sources refused on both trees (below), whose own text differs between them.
**One path.** main's slice 3m (m83az) mapped `$TT<i>` through `templateUnder` over a from/to entry the binding loop pushed; m8-3bk's line wrote `idUnder`, `listUnder`, `aliasedClass` and `Substituted::templates` for the same job and more (an alias bound, a bound list with packs, defaults and values, the parameter as an argument). Kept: m8-3bk's; deleted: `templateUnder`, its declaration and the binding loop's from/to entry. classShape's scan takes m8-3bk's `counted` sizeof... (held in `sized` for staticInit) and `ttId` over 3m's two let-throughs (`sizeof...` anywhere, `C <` at depth 1). "A member template beside a non-type parameter" stays removed as main's c617b946 removed it; the template template one is kept, on `self`/`readTag` (m8-3bk's second `readMembers_.find` dropped). earlyArgs' value test (with m8-3bk's writtenValue) sits where main moved it, once.
**The probe clash**: main's tests/cases/template-subst-ttparam (m83az's) kept as it is; m83bi's probe of that name is tests/cases/template-subst-ttbound. Both right on Linux plain and under the flag, and clang 19 = cxx3-msvc plain = flag on Windows (`7 2 a b 3 1.5 4 / 40 z 9 -1 120 11 / 8 8 8 8`; `7 k 11 / 1 x 2.5 1 0.5 / 10 3 4 / 40 40 14 2 / 12 13 1 7`; ttlist `h 11 12 0.5 / ...`).
**Gates on 399be7a5** (Linux, REF built from 486b8f08): g++ -Werror clean; identical plain at -O0/-O1/-O2 x86_64-linux 3048/0, x86_64-windows 6096/0, arm64-darwin 3048/0, tms6747 3048/0 (no emitted output moved); under -templates=subst x86_64-linux 3048/0 and x86_64-windows 6096/0 (the classes the merge makes or replays otherwise emit the same); run.sh 1360/0 plain and 1360/0 under the flag (1004 printed, 352 refused as recorded), then subst.sh 0 failed (typed 1007/0; 7 counts below the record, not re-recorded); the full stage-0 self-host 99 of 102 - C6xPipe, ParserTopLevel, Templates "outside the SSA lowering", the base's three (lane w46fix's), none added; check censuses (Windows, cxx3-msvc -arch x86_64-linux) suite 1,368 units nonzero 354, no class differ, self 102 nonzero 3, no class differ; static: exclusions --check rc 0 (three Templates.cpp citations re-pointed 2265, 2317, 3014 -> 2261, 2313, 3010), comments 0 over the cap, memory-gate rc 0, ast-residue rc 0 (2012/298, not raised), ide up to date; names.sh 1016/0, overload.sh 46/0; cl /W4 /WX full clean (02:17:04-02:18:50, ~2 min, cl alone).
src/: 97,940 (REF) and 97,906 (m8-3bk) -> 98,043 lines (Templates.cpp: templateUnder's 12 lines and 3m's binding-loop and scan lines out; Templates.h its declaration).
**Next**: `W<Box>` - a member template's class binding under `Substituted::templates` - then m8-3bk's next list (`Derived<Box>`'s base, `Nest<Cell, int>`).

## M8.3 slice 3u: `W<Box>`'s member template, `Derived<Box>`'s base `: C<int>` and a type parameter bound to an array of scalars, made from the read under -templates=subst (lane m83bm, 2026-11-26)
Base 23d3946f (m6d-w47). **The measure** (the exact census's specialization lines, no check, x86_64-linux; Linux g++ build and Windows cxx3-msvc -arch x86_64-linux agree): suite 2,531 -> 2,538 made of 2,605, self-host 75,672 -> 75,673 made of 75,680; again 0. x86_64-windows self 75,657 of 75,665 on the base, the same 8 replayed.
**The self-host's last 8 on the base** (x86_64-linux and x86_64-windows alike): Driver `__thread_call<runCommands::$_0>`, `__thread_call<runJobs::$_0>` - "a base: a polymorphic class" (`__thread_body`, virtual `run`); Driver `decay<std::thread>`, `decay<runCommands::$_0 &>`, `decay<runCommands::$_0>`, `decay<runJobs::$_0>` and `ratio_divide<ratio<1,1000000000>, ratio<1,1000>>` - "a member typedef or alias over a type the binding does not carry" (the suite's decay and ratio_divide shapes, lane m83bn's); ParserInit `remove_reference<const char [7] &>` - "a type parameter bound to an array", made here (c3). 7 left: 2 polymorphic bases, 5 m83bn's reason.
c1: `W<Box>` (template-template-members). Without the refusal the class is made and its member template declared under the real binding (templatesFromRead's bindTemplateParameters, the replay's path); the 2 class differ were the check's: templatesHeld compared the read's stand-in for `C` through `substituted`, which maps no template template parameter - it now holds that binding against the template `Substituted::templates` names (templateNameTypeOf, as idUnder names it), and the skip differ was that differ's echo. "A member template beside a template template parameter" and `ttNamed` deleted. +1. c2: `Derived<Box> : C<int>` - the read kept a `$TT<i>` base as null ("one it cannot keep"); it keeps its type now (ParserType), and classShape's base loop maps it through specializationUnder/earlyArgs, which name it by idUnder already. +2 (template-subst-ttbound, template-template-parameter). c3: a type parameter bound to an array whose element is a scalar (`remove_reference<const char [6] &>`, `Id<const char (&)[5]>`) substitutes as written, as void and `R(A)` do. Suite +4 (c6x-last-walked 2, deque-both-ends, string-literal-array-reference), self +1. Left of the template template classes: `Nest<Cell, int>` (template-subst-ttbound; `C<C<T> >` with `Cell<int>` declared nowhere first - the inner stand-in id is skipped by argsWanted as one the binding carries; `Nest<Box, int>` of template-template-parameter is made, `Box<int>` being declared).
src 98,043 -> 98,047 (Templates.cpp +4, ParserType.cpp 0); no EXCLUSIONS line moved.
Gates (REF built from 23d3946f, Linux): g++ -Werror clean; the probes' cases (template-template-members, template-subst-ttbound, template-template-parameter, c6x-last-walked, deque-both-ends) right plain and under the flag, under the check 0 class differ; identical plain at -O0/-O1/-O2 x86_64-linux 3051/0, x86_64-windows 6102/0, arm64-darwin 3051/0 (no emitted output changed); Windows cl /W4 /WX full clean on c1 (03:03:18-03:07:48, ~4.5 min) and delta on c3 (03:16:48-03:19:48, ~3 min), cl alone; clang 19 = cxx3-msvc = flag on template-template-members (`3 2.5 q 105 6 7`); check censuses (Windows, -arch x86_64-linux) on c1 and c3: suite 1,369 units nonzero 354, no class differ; self 102 nonzero 0, no class differ.
Late gates on c3 = e9ec06c4's code (Linux, REF 23d3946f): identical plain tms6747 3051/0; under -templates=subst x86_64-linux 3051/0 and x86_64-windows 6102/0 at -O0/-O1/-O2 (no emitted output changed either way, the 7 classes made here among them); run.sh 1361/0 plain and 1361/0 under the flag (1005 printed their recorded output, 352 refused as recorded), then subst.sh 0 failed; the FULL stage-0 self-host 102 of 102 (148 s); static rc 0 (exclusions --check, comments 0 over the cap, memory-gate lower, ast-residue 2012/298 not raised, ide up to date); Linux check censuses (e9ec06c4 rebuilt): suite 1,369 units nonzero 354, no class differ, 2,538 made of 2,605; self 102 nonzero 0, no class differ, 75,673 of 75,680. Windows on c3: names.sh 1017/0, overload.sh 46/0.
Tried and dropped (not committed, the measure unmoved): `Nest<Cell, int>` with argsWanted letting a stand-in id inside an id through (`substituted` renames `$TT0<$T1>` to `$TT0<int>`, so it looked carried) and earlyArgs finding a pending stand-in before substituting it - both built (cl /W4 /WX clean), the template template cases 0 differ and clang's output both ways, but `Nest<Cell, int>` stays "a data member of a class the read named over its parameters"; the next lane starts from which test at classShape's data-member loop refuses `C<C<T> > cc` (with `-check=templates` on template-subst-ttbound alone).
**Next**: `Nest<Cell, int>` (above); the self-host's 7 - `__thread_call`'s polymorphic base `__thread_body` (2: a vptr in the made class), and the decay/ratio_divide member typedef reason (5, m83bn's shape in the suite).

## M8.3 slice 3v: a class template over a polymorphic first base (`__thread_call : __thread_body`) and `Nest<Cell, int>`, made from the read under -templates=subst (lane m83bo, 2026-11-26)
Base 574d29bc (m83bm, slice 3u). **The measure** (the exact census's specialization lines, no check, x86_64-linux, Linux g++ build; the REF's lists diffed): suite 2,538 -> 2,543 made of 2,605 -> 2,608 (the probe's 3 of 3, `Nest<Cell, int>` of template-subst-ttbound now 25 of 25, thread-pool's `__thread_call<main::$_0>`), self-host 75,673 -> 75,676 made of 75,680 -> 75,681 (Driver's `__thread_call<runCommands::$_0>` and `<runJobs::$_0>`; the one more class is Templates.cpp's own new text, `remove_reference<pair<const string, vector<VSlot> > &>`, made). Left in the self-host: m83bn's 5 (decay 4, ratio_divide 1). x86_64-windows (cxx3-msvc, check census): suite 2,543 of 2,608, self 75,661 of 75,666 (base 75,657 of 75,665).
**(1) A polymorphic first base.** classShape lets a base that is polymorphic through when it is the first written, of no virtual base, and the read's table (`classes_.vtables_` under the read's tag) has exactly its base's slots - every read virtual an override; a slot of its own, an override left pure, a written destructor, or a non-virtual member named as a base slot (one the binding might make an override) still replay, each by name. The made class takes the parser's paths: basesUnder hands layBases the made tag, so the base's table is seeded as the class body seeds it (the check's calls pass none, so they no longer reseed the read's own table, which a polymorphic base would have); classFromRead sets it polymorphic from the read, as the class tail's rule; membersFromRead puts each override's symbol in the slot the read's took; completeClass's declareImplicitSpecials gives the implicit destructor the `~` slot(s) by registerDestructor and emits the vtable and type_info as for any class. Under the check (and for the made class, either way) classMembers holds the table by vtableHeld: each slot's name, purity and finality, its function the read's under the symbol map, a destructor's slot the class's own, an inherited entry the same. memberSymbol: a virtual implicit destructor is `U` in the Microsoft name (was `Q`; the check's 2 x86_64-windows differ on the probe's `Plus<T>`, found on c1). classLaid's vptr refusal narrowed to a vptr of the class's own. c3: the made class takes Itanium's primary base from the read (`setPrimaryBase`, which the class body sets and completeClass does not) - without it emitItaniumVtables took `__thread_body` for a secondary base, emitted a second table into the group and the constructor stored a second vptr (identical under the flag on c2, x86_64-linux: the probe and thread-pool at -O0/-O1/-O2, 6 differ; x86_64-windows has no primary base and was 6108/0); classLaid now holds the primary base and polymorphism, so the check sees what identical saw.
**(2) `Nest<Cell, int>`** (`C<C<T> > cc` with `Cell<int>` declared nowhere first): the refusal was listUnder's, not argsWanted's - the outer stand-in's argument `$TT0<$T1>` is wanted (pending, not yet mapped), so `substituted` leaves it dependent and listUnder refused the list, and with it idUnder and earlyArgs. listUnder now keeps an argument that is still an id over the parameters (readId, overParameters) for earlyArgs' pending test; a default after one is refused (it would be read over a dependent binding). idsBefore then makes `Cell<int>` and `Cell<Cell<int> >` in order, as the replay reads the member.
Probe template-subst-polybase (clang 19 on Windows `510 815 914 / 10 20 / LEDGER built=4 gone=4 live=0 double=0 phantom=0 over=0 lost=0`): `Call<Tracked>` (a class-typed member, the ledger's), `Plus<short>`, `Plus<char>` over `Body` (virtual destructor, a pure `run`, a `twice` both override), called through the base and a reference.
src 98,047 -> 98,093 (fccb679a: Templates.cpp +45, Templates.h +1) -> 98,095 (c3, Templates.cpp +2); no EXCLUSIONS line moved.
Gates on fccb679a (REF built from 574d29bc, Linux): g++ -Werror clean; identical plain at -O0/-O1/-O2 x86_64-linux 3054/0, x86_64-windows 6108/0, arm64-darwin 3054/0, tms6747 3054/0 (no emitted output changed); under -templates=subst x86_64-windows 6108/0, x86_64-linux 6 differ (c3's, above); run.sh 1362/0 plain and 1362/0 under the flag (1006 printed their recorded output, 352 refused as recorded), then subst.sh 0 failed; static rc 0 (exclusions --check, comments 0 over the cap, memory-gate lower, ast-residue not raised, ide up to date); the FULL stage-0 self-host 102 of 102 (153 s); the probes (template-subst-polybase, template-subst-ttbound, template-template-parameter, template-template-members, thread-pool, thread-sleep-yield) right plain and under the flag, under the check 0 class differ on x86_64-linux and x86_64-windows, clang 19 = cxx3-msvc = flag on Windows for polybase and ttbound (`7 k 11 / ...`); Windows cl /W4 /WX full clean on c1 (04:00:55-04:03:55, 3 min) and delta on c2 (04:07:11-04:11:41, ~4.5 min), cl alone; Windows check census (cxx3-msvc -arch x86_64-windows): suite 1,370 units nonzero 378, no class differ; self 102 nonzero 0, no class differ; Linux check census (c2's build): suite 1,370 units nonzero 354 (the `.error` set), no class differ, 2,543 of 2,608; self 102 nonzero 0, no class differ, 75,676 of 75,681; names.sh 1017/1 - the probe's C1/D1 emission (8 names on x86_64-linux), now its `.nonames`; x86_64-windows agrees on all 66 - and overload.sh 46/0.
On c3 (Windows, cxx3-msvc built from it, cl /W4 /WX delta clean 04:30-04:39 - slowed by names.sh holding the exe, not cl's time): each of template-subst-polybase, thread-pool, thread-sleep-yield and template-subst-ttbound under -templates=subst emits the same `.s` as plain (the replay, which REF agrees with) on x86_64-linux, x86_64-windows and arm64-darwin at -O0/-O1/-O2; the check 0 class differ on both targets; clang 19 = cxx3-msvc = flag on polybase, ttbound and thread-pool (`499500 1000 1 1 1 0`). On Linux (c3's g++ -Werror build, clean): the four cases' `.s` the same as REF's under the flag on the three targets at three levels, the probes right both ways and 0 class differ under the check; identical under -templates=subst over the suite x86_64-linux 3054/0 at -O0/-O1/-O2. Owed at the hour: identical under the flag on x86_64-windows over the suite on c3 (6108/0 on c2, and c3 sets no primary base on that ABI), run.sh/subst.sh/self-host on c3 (c3 changes only the made class's primary base and classLaid's check).
**Next**: m83bn's decay and ratio_divide (the self-host's last 5); the suite's 65 by reason (a constexpr member, a member initialiser naming a parameter, a non-type parameter outside its member bodies, a friend, a using-declaration, a nested class, ...).

## M8.3 slice 3v (m83bn, written as 3u beside m83bm's): a member typedef the binding does not carry, read by the class body's path - ratio_divide and decay - made from the read under -templates=subst (lane m83bn, 2026-11-26)
**The measure** (the exact census's specialization lines, no check, `-arch x86_64-linux`; the suite's the same on Linux): BASE 23d3946f suite 2,531 made of 2,605, self-host 75,672 made of 75,680 -> suite 2,569 made of 2,631 (2,543 of 2,605 without the probe: +12 - `ratio_divide<...>` 10 in chrono-clocks and thread-sleep-yield, `decay<...>` 2 in thread-pool; the probe 26 of 26), self-host 75,677 made of 75,680 (+5, Driver.cpp: `decay<...>` 4, `ratio_divide<ratio<1,1000000000>, ratio<1,1000>>` 1 - the same shape, which m83bm's self-host list held); again 0. "A member typedef or alias over a type the binding does not carry" 12 -> 0 (suite) and 5 -> 0 (self-host).
**BASE's census by reason** (suite, 74 replayed): member typedef 12, nested or elaborated class 10 (Tree<int>, Tree<long long>, Tree<double>, Tree<int> again, B<int>, Vec<int>, Vec<Item>, Pair<int,2>, IsInt<int>, IsInt<int *>), virtual function 7, member enumeration naming a template parameter 5, base over a class template-id 5, non-type parameter named outside its member bodies 4, constexpr member 4, data member the binding does not carry 3, data member of a class read over its parameters 3, then 21 reasons of 1 or 2. Self-host: member typedef 5, polymorphic base 2, array 1.
**What it does.** `typedef typename ratio<R1::num * R2::den, R1::den * R2::num>::type type;` names an id whose arguments are values on another class's static members, and decay's `typedef typename conditional<is_array<U>::value, ...>::type type;` one over the class's own typedef: no substitution can carry either, so typedefsCarried refused the class. Templates::typedefByPath widens ownTypedefAt's door: in a class of no written member function or member template, a typedef written `typedef ... n;` with one declarator whose type the binding alone (values, packs, templates - no ids mapped, so the door, staticsFromRead and the skip give one answer) does not carry is read by staticsFromRead through the parser's own specifiers and declarator under the bound parameters, the replay's path, and declared `tag::n` (held as `tag::$held::n` under the check). staticsFromRead now reads its typedefs in the body's order among the statics and enumerations (ownTypedefAt's follow the statics they name, so nothing of ratio's moved). typedefCarried is typedefsCarried's one test, asked by both. Under the check staticsHeld holds each such typedef against the replay's and the skip compares it there, not by substitution.
Probe template-subst-bypath (by hand and clang 19 `1 1000 / 15 28 / 24 2 / 5 5 1`): `Div`, `Mul` over `R<N, D>` (a nested `Div<Div<..>::type, ..>`) and `Pick<T>` (a private `Strip<T>::type U`, then `If<IsPtr<U>::value, U, U *>::type`) - 26 of 26 made from the read, 26 equal under the check.
src/: 98,043 -> 98,076 lines (Templates.cpp +31, Templates.h +2); no EXCLUSIONS line moved (its three Templates.cpp citations are above every changed line).
**Gates** (REF built from 23d3946f on each box; the Linux runs on c2, which is this tree but for the skip's one census line): Windows cl /W4 /WX full clean on c1 (03:08:20-03:13:20, ~5 min, cl alone), c2 (03:20:02-03:24:47) and the final tree (03:30:18-03:37:34, beside names.sh and an identical); clang 19 = cxx3-msvc plain = under the flag on the probe and template-subst-ratio; overload.sh 46/0; on Linux g++ -Werror clean, the probe, template-subst-ratio, chrono-clocks, thread-sleep-yield and thread-pool right plain, under the flag and under the check; the FULL stage-0 self-compile 102 of 102 (155 s); run.sh under the flag 1362/0 (1006 printed their recorded output, 352 refused as recorded); static rc 0 (exclusions, comments 0 over the cap, memory-gate lower, ast-residue not raised, ide up to date). Check census (Windows, `-arch x86_64-linux`): c2 left 4 suite units and Driver.cpp with "the class skip ... a member typedef's name or type" - the skip still compared a path typedef by substitution; on the final tree those units (the probe, template-subst-ratio, chrono-clocks, thread-pool, thread-sleep-yield) hold no class differ, the made counts unchanged.
**Gates, the rest** (after 8ff3b0e1): identical against REF 23d3946f at -O0/-O1/-O2 plain x86_64-linux 3054/0, arm64-darwin 3054/0, tms6747 3054/0 (Linux), and under `-templates=subst` x86_64-linux, arm64-darwin and tms6747 3054/0 each (Linux) and x86_64-windows 2036/0 at each of -O0, -O1, -O2 (Windows, against a REF cxx3-msvc built from 23d3946f) - the classes now made from the read emit what their replay emitted; run.sh plain 1362/0, then subst.sh 0 failed (Linux); names.sh 1018/0 (Windows); the check census on the final tree (Windows, `-arch x86_64-linux`) suite 1,370 units, nonzero 354 (BASE's 354), no class differ, 2,569 made of 2,631; self-host 102 sources, nonzero 0, no class differ, 75,677 made of 75,680.
**Next**: the nested classes (suite 10: Tree 4, Vec 2, B 1, Pair 1, IsInt 2 - Tree's `It` with out-of-line members the largest door); then the non-type parameters named outside their member bodies (4).
**Owed at the hour** (stopped by PID, not failed): identical plain x86_64-windows (no plain output can move: every changed line is reached under `substitute_` alone, and plain x86_64-linux, arm64-darwin and tms6747 are 0 differ) and the Linux check censuses on the final tree (the Linux runs were on c2, which differs from it only in the skip's census line; Windows' censuses are on the final tree).

## M8.3 slice 3x: a nested class read by the class body's path - B, IsInt, Pair, Tree - made from the read under -templates=subst (lane m83bp, 2026-11-26)
**The measure** (the exact census's specialization lines, no check, `-arch x86_64-linux`; Linux's suite the same): BASE 0ad4f433 suite 2,569 made of 2,631, self-host 75,677 made of 75,680 -> suite 2,583 made of 2,639 (2,575 of 2,631 without the probe: +6 - `B<int>` in qualified-functional-cast, `IsInt<int>` and `IsInt<int *>` in typename-temporary, `Pair<int,2>`, `Tree<int>` and `Tree<double>` in template-injected-name-member; the probe 8 of 8), self-host 75,685 made of 75,688 (+8, the new code's own ids in Templates.cpp, all made; replayed 3 unchanged); again 0. "A nested or elaborated class" 10 -> 4: nested-class-of-class-template's `Tree<int>`, `Tree<long long>` (It's constructor, destructor, `get` and Deep's statics defined out of line) and template-container's `Vec<int>`, `Vec<Item>` (`iterator` named by `begin()`'s signature).
**What it does.** Templates::nestedByPath finds each `struct N { ... };` (or `class`) in a struct's braces that the class body's own path can read with nothing of the read to map: no access label before it, no member function or member template after it (the made class's functions and held bodies stay in the replay's order), N named nowhere else in the braces (no data member or signature carries the read's nested class), nothing of it or of a class it nests declared to be defined out of line (`It(T);`), no static data member, template, friend, virtual, union, enum, using, default/delete or constexpr in it. staticsFromRead reads each through Parser::specifiers at its key in the body's order among the statics, enumerations and typedefs, with the binding bound, the class being made the only class on the stack and specialization bodies deferred - the replay's state at that token - so the nested class is declared `tag::N` with its members, implicit specials and layout by the parser's own path, and its held bodies (pendingBodies_, as a class nested in a specialization holds them) join the class's bodies. classBeyond lets such a class through; fromReadWhyNot and membersFromRead leave the read's held bodies inside it to that path; classSkip compares the other held bodies alone (the nested ones are the same path's on both sides). Under the check staticsHeld reads no nested class (the replay's is the program's).
Probe template-subst-nestpath (by hand and clang 19 `8 2.5 / 7 11 / 9 1 / 4 3`): `Holder<T>::Box` (a member over T, a constructor, a const member), `Scaled<T,N>::In` after a static member function its body calls through the injected name, `Tag<T>::type` reached as `typename Tag<It>::type(t)`, `Outer<T>::Mid` with a class `Deep` nested in it and a static member function - 8 of 8 made from the read. Its x86_64-linux names differ from clang's only in the C1 rows of the nested constructors, as qualified-functional-cast's: `.nonames`.
src/: 98,076 -> 98,155 lines (Templates.cpp +77, Templates.h +2); no EXCLUSIONS line moved (its three Templates.cpp citations are above every changed line).
**Gates** (REF built from 0ad4f433 on each box; a23a31e8's code): Windows cl /W4 /WX full clean (04:13:39-04:19:54, ~6 min, cl alone; c1 before it failed on a shadowed name, fixed); g++ -Werror clean; the probe, qualified-functional-cast, typename-temporary and template-injected-name-member: clang 19 = `.expected` = cxx3-msvc plain = under the flag = under the check (Windows), right plain, under the flag and under the check (Linux); identical against REF at -O0/-O1/-O2 plain x86_64-linux 3057/0, arm64-darwin 3057/0, tms6747 3057/0, x86_64-windows 6114/0, and under `-templates=subst` x86_64-linux, arm64-darwin and tms6747 3057/0 each (Linux); check census (Windows, `-arch x86_64-linux`) suite 1,371 units, nonzero 354 (BASE's 354), no class differ, 2,583 of 2,639; self-host 102 sources, nonzero 0, no class differ, 75,685 of 75,688; static rc 0 (exclusions, comments 0 over the cap, memory-gate lower, ast-residue not raised, ide up to date); names.sh on the four cases: x86_64-windows and arm64-darwin agree, x86_64-linux the probe's C1 rows (`.nonames`); the FULL stage-0 self-compile 102 of 102 (161 s, Linux).
run.sh plain 1363/0 and under the flag 1363/0 (1007 printed their recorded output, 352 refused as recorded; Linux); names.sh 1019/0 (Windows, the `.nonames` in place).
**Owed at the hour** (Linux's gate waited 24 minutes in ~/slot behind the wave gate m6dw47g; stopped by PID, not failed): subst.sh (stopped mid-run; it had printed "6 counts are below the record ... record again" - to be read by the wave gate, likely the self-host's own new ids), the Linux check censuses; on Windows identical under the flag x86_64-windows and overload.sh.
**Next**: a nested class named by the enclosing class's signatures or data members (Vec's `iterator`: the read's nested class mapped to the one the path declares, read before the member functions), then one with members out of line (nested-class-of-class-template's Tree); the member enumeration naming a parameter beside a member function (5: the read's enumerator mapped to the made one in a read body); Tuple's base over a pack (5).

## M8.3 slice 3z: a member enumeration over a non-type parameter beside member functions, and a base of the class's own template over a pack (`Tuple<Rest...>`), made from the read under -templates=subst (lane m83br, 2026-11-26)
**The measure** (the exact census's specialization lines, `-arch x86_64-linux`, Windows cxx3-msvc): BASE 2395504f (m6d-w48 + m8-3bp) suite 2,595 made of 2,642, self-host 75,689 made of 75,689 -> suite 2,609 made of 2,648 (+14: the probe 6 of 6, template-subst-memberenum's `ByValue<3>`, `<4>`, `<5>`, tuple's `Tuple<{int,char,double}>`, `<{char,double}>`, `<{char,char}>`, `<{char,char,char}>`, `<{char,char,char,char}>` - tuple now 8 of 8), self-host 75,689 of 75,689 (unchanged; nothing left replayed); again 0. Left in the suite (39): nested classes 4 (Tree x2, Vec x2 - not reached this hour), sized x2 (below), a named member enumeration beside a member function 2, the virtual functions (m83bq's), then reasons of 1 or 2.
**What it does.** (1) An unnamed member enumeration naming a non-type parameter beside member functions (`ByValue<K>`'s `enum { Twice = K * 2, Next };` and `get()`): classShape lets it through as 3n let it through in a class of none - staticsFromRead reads it under the binding; a body's read of such an enumerator is value-dependent, so memberShape replays that body by name. One naming a type or pack parameter still replays, now by name ("a member enumeration naming a type or pack parameter, beside a member function"): `sized<T>`'s `bytes = sizeof(T) * 2` is not value-dependent, and a body typed from the read took the read's stand-in (`bytes` 0 for the replay's 8 - caught by -check=templates on c1, wrong output under the flag; refused on c2). (2) A base of the class's own template (`Tuple<T, Rest...> : Tuple<Rest...>`): earlyArgs' new `base` mode lets the read's own template through where the id is a base, without the own-name body scan, since the replay makes the base reading the head, before the class begins; idsBefore asks that mode too, so the base is made first through makeClass; a member signature naming the wanted base (`Tuple<Rest...> *tail()`) is let through, carried once it is made.
Probe template-subst-enumbody (by hand and clang 19 `17 30 52 5.0 / 3 7 q 1`): `Scale<3, short>`, `Scale<10, double>` (an enum over K beside two member functions), `List<int, char, double>` down to `List<>` (a base over the pack, a body calling the base's `size()` by qualified name) - 6 of 6 made, 0 class differ on both ABIs.
src/: 98,207 -> 98,212 lines (Templates.cpp +5, Templates.h 0); no EXCLUSIONS line moved.
**Owed by m83bp, on this tree** (the merge's code is in it unchanged): subst.sh's "6 counts are below the record" are the six doors' `read` counts, each above its record with `unread` and `shape` unchanged - aliases 7 -> 11, class-members 12,574 -> 13,492, functions 3,489 -> 3,882 (unread 14 as recorded), member-templates 495 -> 533 (unread 3), out-of-line 24 -> 43, partial-members 606 -> 653: bodies the cases added since the record read, no loss; tests/subst-cases.ceiling re-recorded from this run (SUBST_RECORD's copy). Its self step: 1 count below - functions read 7,403 -> 7,505 (unread 1 as recorded), class-members shape 58 -> 57 - tests/subst-self.ceiling re-recorded the same way; subst.sh 0 failed. overload.sh 46/0 (Windows); the Linux check census and identical under the flag on x86_64-windows below.
**Gates** (REF built from 1996faa8 on each box; 41ed9239): g++ -Werror clean; Windows cl /W4 /WX clean (c1 full 05:03:18-05:11:34, c2 delta 05:13:22-05:20:37, c3 delta 05:22:24-05:33 - past the 9-minute cap beside a census and the other session's jobs, recorded as a defect, not retried; the link finished); identical plain at -O0/-O1/-O2 x86_64-linux, arm64-darwin, tms6747 3063/0 each, and under -templates=subst x86_64-linux, arm64-darwin, tms6747 3063/0 each (Linux) - no emitted output changed, the 14 classes now made emit what their replay emitted; run.sh 1365/0 plain and 1365/0 under the flag (1009 printed their recorded output, 352 refused as recorded); subst.sh run/typed 0 failed, cases ceiling as above; the FULL stage-0 self-compile 102 of 102 (160 s); static rc 0 (exclusions --check, comments 0 over the cap, memory-gate lower, ast-residue 2012/298 not raised, ide up to date); check census (Windows, `-arch x86_64-linux`) suite 1,373 units nonzero 354 (BASE's), no class differ, 2,609 of 2,648; self 102 nonzero 0, no class differ, 75,689 of 75,689; the probe, template-subst-memberenum, template-subst-enumid, tuple and template-variadic: clang 19 = `.expected` = cxx3-msvc plain = under the flag, 0 class differ on x86_64-linux and x86_64-windows.
Identical under -templates=subst on x86_64-windows (Windows, against REF cxx3-msvc 1996faa8): -O2 2042/0, -O0 2042/0. Linux check census (g++ build): suite 1,373 units nonzero 354, no class differ, 2,609 of 2,648; self 102 nonzero 0, no class differ, 75,689 of 75,689.
**Owed at the hour**: identical under the flag on x86_64-windows at -O1 (running at the hour), identical plain on x86_64-windows, names.sh (a new case; over the suite on Windows).
**Next**: sized<T> (an enumerator over a type parameter read in a member body: the body's Name to the made class's enumerator, or marked like a value-dependent one); the nested classes Tree x2 (members defined out of line) and Vec x2 (`iterator` after an access label, named by begin()); the named member enumerations beside a member function (2).

## M8.3: the merged wave m6d-w48's owed checks, and slice 3w: a class template with a vtable of its own made from the read under -templates=subst (lane m83bq, 2026-11-26)
Base 1996faa8 (m6d-w48: m6d-w47 + m8-3bo + m8-3bn), REF built from c087c444 on each box. **The measure** (the exact census's specialization lines, no check): x86_64-linux (Linux, g++ -Werror builds) base suite 2,581 made of 2,634, self-host 75,681 made of 75,681 -> e3ce0397 suite 2,591 made of 2,637 (+7, and the probe's 3 of 3), self-host 75,682 made of 75,682; x86_64-windows (Windows, cxx3-msvc) base suite 2,581 of 2,634, self 75,666 of 75,666 -> suite 2,591 of 2,637, self 75,667 of 75,667 (the one more class is Templates.cpp's own new text, made); again 0. The self-host is every class made on both targets, before and after.
**(0) The merged wave on 1996faa8** (REF c087c444): the exact census above on both targets (suite 2,581 of 2,634 on each; self 75,681 of 75,681 on x86_64-linux, 75,666 of 75,666 on x86_64-windows - the counts differ by ABI, every class made); the x86_64-windows check census (cxx3-msvc -arch x86_64-windows): suite 1,371 units nonzero 378, no class differ; self 102 nonzero 0, no class differ - m83bo's vtable path and m83bn's typedefByPath in one file agree with the replay; identical under -templates=subst at -O0/-O1/-O2 x86_64-linux, arm64-darwin and tms6747 3057/0 each (Linux), x86_64-windows -O0 2038/0 (Windows). The base's 53 replayed by reason (x86_64-windows): nested or elaborated class 10, virtual function 7, a base over a class template-id 6 (tuple 5, `Q<3>`), member enumeration naming a parameter 5, constexpr member 5, non-type parameter outside its member bodies 4, then 16 reasons of 1 to 3. Owed on 1996faa8 alone (stopped by PID, queued behind the coordinator's gate in both Linux slots; e3ce0397's runs below hold the merge's code with this slice's): x86_64-windows -O1/-O2 under the flag, the Linux check census, run.sh both ways, subst.sh and the self-compile.
**(1) The suite's virtual functions.** At the base the reason "a virtual function" held 7 classes, every one a class of no base with a table of its own: polymorphic-class-template's `P<int>`, `P<double>`, `Holder<int>`, `Holder<Holder<int> >`, `Box<int, 3>`, `ns::R<int>` and template-id-qualified-base-call's `V<3>` (m83bo's path takes only a class whose every virtual overrides its first base's slots, so a slot of its own replayed). Slice 3w takes them through the same paths: classShape lets a polymorphic class of no base through (a virtual base still replays); membersFromRead seeds the made tag's table from the read's - each slot's parameters and return under the binding, a deleting destructor's slot (`~$deleting`, or Microsoft's `~`) the made tag's `deletingDestructorSymbol` - and each read virtual's symbol takes its slot as m83bo's loop does; laidFromRead lays the vptr in front as the class tail does (Microsoft 8, Itanium the pointer rounded up to the widest member alignment; the members shifted, the size past it), and classLaid's own-vptr refusal now holds only a vptr beside a non-polymorphic base. Found on c1: a written virtual destructor's body made from its read failed the lowering ("a parameter list the typed tree and the function disagree about") - the read kept `pattern.functions.back()`, which for a virtual destructor is the deleting form synthesizeDeleting pushes after it; c2: madeWithoutReplay replays a virtual destructor's body by name ("a virtual destructor, whose deleting form the replay emits"), so the class is made and its destructor and deleting form come from the class body's path. All 7 made; "a virtual function" 7 -> 0. `Q<3>` (a base over its parameters that is polymorphic) still replays.
Probe template-subst-ownvtable (clang 19 on Windows `9018 115 1.50 6.00 100 / 7 1 / LEDGER built=2 gone=2 live=0 double=0 phantom=0 over=0 lost=0`): `Shape<int>`, `Shape<double>` (a pure slot, a `T grow(T)` slot `Disc` overrides, a written virtual destructor) and `Keep<Tracked>` (a class-typed member, the ledger's) - 3 of 3 made, 0 class differ under the check on both targets.
src/: 98,128 -> 98,147 lines (Templates.cpp +19); EXCLUSIONS' three Templates.cpp citations re-pointed 2261, 2313, 3010 -> 2263, 2315, 3012 (99cb0b83).
**Gates on e3ce0397** (REF built from c087c444 on each box): g++ -Werror clean (Linux); cl /W4 /WX full clean on c1 (04:58:27-05:05:58) and delta on c2 (05:09:45-05:16:46), each beside censuses and an identical, not alone (cl alone at the base: 04:49:24-04:55:25, ~6 min); identical plain at -O0/-O1/-O2 x86_64-linux, arm64-darwin, tms6747 3060/0 each, and under -templates=subst x86_64-linux, arm64-darwin, tms6747 3060/0 each (Linux), x86_64-windows 2040/0 at each of -O0, -O1, -O2 (Windows) - the 10 classes now made emit what their replay emitted; the probe, polymorphic-class-template, template-id-qualified-base-call and template-subst-polybase right plain and under the flag on Linux and on Windows (clang 19 = cxx3-msvc = flag), under the check 0 class differ on x86_64-linux and x86_64-windows, and their flag `.s` = plain `.s` on x86_64-linux, x86_64-windows and arm64-darwin at -O0/-O1/-O2; check census (Windows, cxx3-msvc -arch x86_64-windows): suite 1,372 units nonzero 378, no class differ, 2,591 of 2,637; self 102 nonzero 0, no class differ, 75,667 of 75,667; static (Windows, on 99cb0b83's tree): exclusions --check 0 uncited 0 stale, comment-lines 0 over the cap, memory-gate lower, ast-residue lower (not raised), ide up to date. The FULL stage-0 self-compile 102 of 102 (152 s, Linux).
**Owed at the hour** (stopped by PID, not failed): run.sh plain and under the flag, subst.sh and the Linux check census on e3ce0397 (queued behind the coordinator's gate in both Linux slots from 05:12); identical plain x86_64-windows; overload.sh (Windows); names.sh ran 1019 passed, 1 failed - the probe, template-subst-ownvtable, whose names differ from clang's for a cause not yet read (template-subst-polybase's were the inline C1/D1 emission, its `.nonames`): its `.nonames` or its mend is owed.
**Next** (work (2) not reached this hour): the suite's other reasons by count - constexpr member 5, non-type parameter outside its member bodies 4, data member of a type the binding does not carry 3, data member of a class read over its parameters 3 - leaving the nested classes, member enums naming a parameter and bases over a template-id (with `Q<3>`, m83bo's path over a template-id base) to m83bp.
