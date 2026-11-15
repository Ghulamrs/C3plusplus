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

Gates: GATEROW
src/ lines: SRCBEFORE before, SRCAFTER after.
