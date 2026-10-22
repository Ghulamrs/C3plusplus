# Fable 5.1 on `Compiler_Design_Final.md` (Draft 2) - round 2

2026-10-22. Read in full: `Compiler_Design_Review_Fable.md` (my review, verified byte-identical to my copy), `Compiler_Design_Alternate.md` (Opus 5.5, A1), `Compiler_Design_Final.md` (Draft 2). Every `file:line` below was re-read for this round. Nothing in `_NEW_` or the source trees was changed.

## 1. Overall

**Agree with reservations.** The final document keeps every structural correction the review asked for - a new IR with funclet-aware EH, two allocation strategies, constexpr on the tree, handles only in new structures, portable threading in the library, a shared encoder, oracle and determinism gates, a measured M1 - and Opus's A1 improved three things I had left loose (the Clang-style parser/Sema relationship, `SemaC`/`SemaCxx`, `<unordered_map>` over `__tree`). Opus's five disagreements are better than the final's 4-1 score suggests: two of them (C5 and part of C1) are technically right and I concede them below. Three things in the final are factually or internally wrong and must be fixed before I sign: the C4 text says `__cplusplus` "stays 199711L" when cpp11 1.28 already defines `201103L` (`Backend.cpp:104`); M3 deletes the x86 text optimizer that target 3 still needs until M5; and section 3.2's "the parser fills its own name-kind scope, no other lookup" cannot settle the qualified names it claims to settle. All three are fixable in a paragraph each; none changes the architecture.

## 2. Conflicts C1-C5

| # | Stated faithfully? | Outcome accepted? | Note |
|---|---|---|---|
| C1 | Yes, with one simplification: my blocker was "M1 is **uncosted** and its parser-side work is then superseded", not "every M1 item is wasted" | **Yes, and I concede half to Opus.** Opus is right that the gaps my experiment found (braced defaults, `std::function`, `unique_ptr != nullptr`, lambda capture, a qualified base `operator=`) are C++11 conformance items that stay whatever the architecture; "pays twice" applies only to semantic fixes written into parser members that M2 moves. The final's resolution (per-milestone gate + measured M1, sections 10 and 12) is the right one and is what both of us wanted | - |
| C2 | Yes. "New syntax tree and typed tree, built in parallel behind a flag; templates by substitution on the tree", with my fallback (replay kept behind the `Templates` interface) | **Yes, with a sequencing condition** (section 7). Opus's dissent in section 14 is sound and the final's own safeguard ("fall back to extracting Sema in place; the Sema interfaces are the same in both") is the key: since the interfaces are identical, extracting them in place is not an alternative to M2 but its first half. See flag F-12b | - |
| C3 | Yes | **Yes.** Opus's charge that I broke my own one-milestone rule is correct (section 3). The final resolves it honestly by amending R2.1 and naming target 2 as the one exception. I recommend Opus's three-step staging (A1 section 5.2) become the internal structure of M6, gated by sim6747 cycles at each step; it is compatible with "last" | - |
| C4 | Yes | **Concede the precedent, not the sentence.** See below | - |
| C5 | Yes | **I concede to Opus.** A parser-private declarative scope answers unqualified names declared in the same TU, which is the `a * b;` case; it cannot answer `T::U * v` (qualified lookup into a class or namespace, possibly a template specialisation that must be instantiated to know its members), names from base classes, names brought in by `using namespace` or `using Base::name`, or injected-class-names. Those require Sema's `Lookup`. Clang's shape - the parser asks Sema "what kind of name is this?" through one action, and keeps nothing of its own - is the proven form, satisfies R4.2 equally well, and removes a second lookup implementation. The final's section 3.2 should say that. See flag F-3.2 | - |

**C4 in detail.** Opus's fact is correct: MSVC reports `__cplusplus` as `199711L` unless `/Zc:__cplusplus` is given (the switch exists since Visual Studio 2017 15.7), and the MSVC STL keys its feature tests on `_MSVC_LANG` (`_HAS_CXX17` etc.). I could not re-verify it against headers in the tree (no MSVC STL or SDK headers are checked in; `grep _MSVC_LANG` finds nothing), so this is from knowledge, and I have no reason to doubt it. I concede that a compiler can ship with a conservative `__cplusplus` and a project macro, and the owner's decision stands.

What I do not concede is the final's wording: "`__cplusplus` **stays** 199711L until the C++11 suite passes." cpp11 1.28 defines it as `201103L` today, deliberately: `backend/Backend.cpp:102-104` reads "The language this compiler reads is C++11, and a program is entitled to ask. 201103L is the value the standard fixes for it." So R3.1 unchanged means **reverting** a predefined macro, which (a) is a change to working behaviour under R1.1, (b) changes the preprocessed form of every header that tests `__cplusplus >= 201103L`, so the M0 oracle recorded from 1.28 would differ from the first build that applies R3.1, and (c) needs the `-Zc:__cplusplus`-style opt-in that MSVC provides and the final does not mention. The final must state the fact, record the revert as a deliberate R1.1 exception with its oracle note, and add the opt-in switch. Whether to revert at all is the owner's call; I would not, but I accept the weighting.

## 3. Opus's critiques of my plan (A1 section 1.2)

1. **"Your M2 builds a second front end in parallel."** Right. My M2 ran a new syntax tree + typed tree beside the 34,790-line parser for one milestone with an adapter to the old back ends. That is a parallel front end, and the adapter is more than I admitted: cpp11's `Ast.h` tree is already *lowered* (frame offsets in `Parser::Local`, `resultSlot`/`argSlots` on `Call` at `Ast.h:237-238`, `Try`/`FuncletLeave` at `Ast.h:510,536`), so an adapter from a typed tree to it is a lowering pass that M3 then replaces. The final adopts my M2 but adds Opus's fallback; I accept that and ask (section 7) that the in-place extraction of Sema interfaces be M2's first step rather than its fallback, since both plans need those interfaces and extraction is the part that keeps the bootstrap green at every commit.
2. **"Your M1 keeps the old-architecture self-host you called a blocker."** Right on the wording, and I withdraw the word "blocker" for the self-host itself. The blocker was that Draft 1 sized M1 as three items and the experiment showed an unmeasured list; my own M1 ends with a first self-host on the old structure for the same reason A1 gives - it is the largest test case the project has. The final's C1 resolution captures what both of us meant.
3. **"Target 2 last keeps two middle ends alive past your one-milestone rule."** Right. I wrote the rule and then kept the C6000 text path from M3 to M6 without naming the exception. The final names it and amends R2.1; that is the correct repair. I still hold that the VLIW should not be the *first* target to prove a new MIR (A1's M4 does that), but A1's staged M4a/M4b/M6 is a good structure for the move whenever it happens.

One more point from A1 I checked and confirm: "cpp11 has no C dialect mode today" - `Driver.cpp:1124-1136` turns `.c` inputs away by name ("compile it with c90"), and `Lexer.h:30-32` is only the character-literal note. C90 (R3.2) is new work in every plan; the final's M7 is sized accordingly only if the owner knows that.

## 4. Section-by-section flags on `Compiler_Design_Final.md`

Blocking = must change before I sign. Non-blocking = should change in Draft 2.1.

| Flag | Section | Finding | Severity |
|---|---|---|---|
| F-0a | 0, C5 row | Records my position; the position is technically insufficient (section 2, C5). Change the outcome to Opus's or to "the parser asks Sema's `Lookup`" | **blocking** (one sentence) |
| F-0b | 0, C4 row | "as MSVC does" is correct; "R3.1 unchanged" omits that cpp11 already reports 201103L (`Backend.cpp:104`) | **blocking** (fact; see 2) |
| F-1 | 1 | Pipeline is consistent with sections 2-7 and 12. Fine | - |
| F-2a | 2 | "Under `-j`, every job writes its own file, as cpp11 already requires (`Driver.cpp:497`)" - correct use of that line (see section 5). "nothing is printed in thread order" is a *new rule*, not a current property: cpp11 only serialises each diagnostic whole under `diagnosticLock()` (`Source.h:9-10`); order between jobs is thread order today | non-blocking (say it is a rule) |
| F-2b | 2 | "Lint rules forbid ... `__DATE__`/`__TIME__`" - in the compiler's own source, yes; the compiler must keep *defining* them for users (`Backend.cpp:106` `addTranslationTime`). Say "in the compiler's source" | non-blocking |
| F-3.2 | 3.2 | "The parser keeps a declarative name-kind scope ... which settles `a * b;`, `x < y > z` and `T::U * v` ... No other lookup." `T::U` needs qualified lookup into `T`, which may be a class with bases, a namespace with using-directives, or a template specialisation that must be instantiated first. A parser-private scope cannot do that without becoming a second Sema. Replace with: the parser asks Sema's `Lookup` for a name's kind (type / template / value / namespace) and keeps no table of its own | **blocking** (design shape; textual fix) |
| F-3.3a | 3.3 | `Rtti` row: "CodeView types" from `CodeViewTypes.cpp` placed in Sema. CodeView type records are debug-info emission (a back-end sink), not semantics. Keep RTTI/type descriptors in Sema; CodeView records belong with the encoder (section 6) | non-blocking |
| F-3.3b | 3.3 | `Checks` from compilerpp `Semantic.cpp:805` - that function is `resolveOverrides` (override/hide); access control in compilerpp is elsewhere. The citation supports "override/hide" only | non-blocking |
| F-3.4 | 3.4 | "SFINAE is a failed substitution, not a failed re-parse" and "non-dependent names are checked at the definition" are true of the end state only. Under the adopted fallback (token replay), SFINAE *is* a trial re-parse (`Source.h:56-60` `beginTrial`/`endTrial`) and bodies are not parsed at definition. Say "once substitution replaces replay (M8)" | non-blocking |
| F-3.5 | 3.5 | Correct. `long` is 4 on target 1 (`X86_64Windows.cpp:20`), `wchar_t` 16-bit on tms6747 (`Tms6747.h:25`) | - |
| F-4 | 4 | Consistent. One nit: "block arguments instead of phi nodes" plus "mem2reg" - fine, since mem2reg writes branch operands; say so to avoid the Draft 1 confusion | non-blocking |
| F-5a | 5 | "Today cpp11's optimizer works on x86 assembly text (`OptIr.h:66-74`)" - correct. Add: that optimizer serves **both** x86 targets, because `MasmCodeGen : X86_64Linux` (`Masm.h:111`) and x86_64-windows in GNU syntax *is* `X86_64Linux` with the Microsoft ABI (`X86_64Windows.cpp:72-74`). This is what makes F-12c inconsistent | non-blocking here, see F-12c |
| F-5b | 5, table | "Moves to the new middle end: 3 → M5". Same code as target 1; see F-12c | **blocking** via F-12c |
| F-6 | 6 | "extended ... to the 73 mnemonics cpp11 emits" - 73 was my count of literal `ins("...")` calls in `X86_64Linux.cpp`; with `Masm.cpp` it is 74, and the walker also composes mnemonics (`ins(op + ...)`, `ins(sign ...)` - 6 sites each), so the real set is larger. Say "at least 73" or count from the oracle's `-S` output | non-blocking |
| F-7 | 7 | "Its word width and type layout come from the selected target, not the fixed 8-byte `long`" - requires the VM's operand stack to carry target widths; `Bytecode.h:8-13` fixes 8-byte stack values. Fine as a requirement; note it is a VM change, not a configuration | non-blocking |
| F-8 | 8 | `-j` "already exists on `std::thread` (`Driver.cpp:1297-1355`)" - correct (1297 `hardware_concurrency`, 1352-1355 pool). `<unordered_map>` over `__tree` - good; note it also makes iteration order deterministic, which the determinism gate wants | - |
| F-9 | 9 | "`clang -std=c++11 -Wall`" for R9.5 - fine. Codes S/T/E/W/I - matches the count (S 154, T 85, E 31, W 25, I 4) | - |
| F-10a | 10 | "Today that check fails on **five** aggregates" - five brace-initialisation *sites* found in three waves (`Source.cpp:140`, `Type.h:377`, `Mangle.cpp:197`, `Type.cpp:219`, `Spelling.h:70-71`); wave 3 had not finished, so "at least five" | non-blocking |
| F-10b | 10 | "M0 removes them" and section 12 M0 "aggregates removed" agree; M1's "R5.1-R5.5" in the Requirements column then double-counts R5.4. Move R5.4 to M0 only | non-blocking |
| F-11 | 11 | "link compatibility, target 2: objects mixed with cl6x-built objects" - cl6x is TI's compiler, outside the toolchain (R7.2: "nothing outside the toolchain"). Make the suite conditional on `CPP11_TI` being set, as `Driver.cpp`'s usage text already does for linking | non-blocking |
| F-12a | 12 M2 | "token replay as the interim `Templates` strategy; old parser path deleted at the end of M2". Replay *is* parser code (`ParserTemplate.cpp`, 3,672 lines; `Parser.h:310-334` `PendingBody`). Either the new parser re-implements replay (M2 grows by that much) or the old parser is not deleted at M2's end. State which | non-blocking, affects sizing |
| F-12b | 12 M2 | The adapter "feeds the typed tree to the old back ends" - those consume an already-lowered tree (`Ast.h:237-238` slots, `Ast.h:510,536` EH nodes, `Parser::Local::offset`), so the adapter is a lowering pass thrown away in M3. Recommend: M2 begins by extracting Sema's interfaces **in place** (A1 section 3.1, one commit per module, bootstrap + oracle each), and only then introduces the handle syntax tree behind the flag. The interfaces are the same (section 14 says so); the order lowers the parallel-period risk Opus dissents on | **blocking** (sequencing sentence) |
| F-12c | 12 M3/M5 | "old x86 text optimizer ... deleted at [M3's] end" while target 3 (x86_64-linux) moves in **M5**. The text optimizer is the only -O1/-O2 path for x86_64-linux (`X86_64Linux.cpp` is the one user of `Optimizer`; `grep -l Optimizer backend/*.cpp`). As written, target 3 has no optimizer during M4. Fix: move target 3 with target 1 in M3 (same selection code, same MIR, different ABI table), leaving arm64 alone in M5. The "one exception" principle (section 12) then has two named exceptions - arm64's old walker for M3-M4 and C6000's path to M6 - which should be written down | **blocking** (internal inconsistency; the fix also shortens the plan) |
| F-12d | 12 M4 | "parallel with M3: it touches only emission" - the encoder's input is `MInst`, defined by M3's MIR. Parallel after the MIR is fixed, not before | non-blocking |
| F-12e | 12 M5 / 8 | M5 "library threads over pthreads" vs section 8 item 1 "(Win32 and pthreads)" in M1. Pick one: Win32 in M1, pthreads in M5 is sensible | non-blocking |
| F-13 | 13 | R3.1 row: "**Unchanged.** `__cplusplus` stays 199711L" - see F-0b | **blocking** (fact) |
| F-14 | 14 | Dissent recorded fairly. The C2 safeguard should be promoted into M2 (F-12b) | - |
| F-15 | 15 | Item 2 cites `include/functional:8` for `docs/EXCLUSIONS.md` - correct (line 8). Add a third missing doc: `VM6747/TMS6747.md` cited at `Tms6747.h:5` | non-blocking |

## 5. Citations

**`Driver.cpp:497` - Opus is right; I was wrong.** The comment at 497-498 reads "**Two jobs never write one file**: -j runs them at once, and dir1/*.cpp and dir2/*.cpp may each hold an x.cpp whose object is x.o. Refused by name before any job starts." It is `distinctOutputs()`. It says nothing about output *order*. My review's "cpp11's `-j` already keeps order, `Driver.cpp:497`" (section 5.7) is withdrawn; the only ordering guarantee in the code is that each diagnostic is written whole under one lock (`Source.h:9-10`). The final uses the line correctly.

**Citations the final adds beyond my review**, all re-read:

| Citation | Verdict |
|---|---|
| `Backend.cpp` not cited for `__cplusplus` | **missing**: `Backend.cpp:104` `add("__cplusplus", "201103L")` contradicts section 13's "stays 199711L" |
| `Driver.cpp:497` (sec. 2) | correct as used |
| `Driver.cpp:1297-1355` (sec. 8) | correct: 1297 `hardware_concurrency`, 1352-1355 the pool |
| `Spelling.h:70` (sec. 10) | correct as a brace-init site (`reg()` returns `{ Op::Reg, ... }`); the struct is `Spelling.h:41-68` |
| `Mangle.cpp:197`, `Type.cpp:219` (sec. 10) | correct (from my wave 2) |
| `ParserOverload.cpp:601-850` (sec. 3.3) | correct range |
| `SemaExpr.cpp:288`, `IrGen.cpp:4988`, `Semantic.cpp:805` | correct (±1 line from my review's citations) |
| `ParserConst.cpp:105-122, 313-324` | correct |
| `Tms6747.h:25-34` | correct |
| `OptIr.h:66-74`, `C6xModel.h:20-29`, `OptIr.h:17`, `MirAlloc.cpp:259/279/305` | correct |
| `Ir.hpp:1-8`, `Optimizer.cpp:1248,1268`, `X86.cpp:1318`, `Ssa.cpp:314`, `Optimizer.cpp:737`, `:922`, `:274-605` | correct |
| `Coff.hpp:1-10`, `Coff.cpp:373`, `:270` | correct |
| `Bytecode.h:29-45`, `:55`; `VM.cpp:17-20, 664-666` | correct |
| `include/functional:4-18`, `:8` | correct |
| `Mir.h:3` | correct |
| compilerpp `Parser.h:162-171`, `Lexer.cpp:112-141` | correct |
| `Source.h:56`, `Driver.cpp:1223`, `C6xModel.h:36`, `ParserExprNew.cpp:1142`, `ParserStmt.cpp:1438`, `Parser.h:310-334`, `Type.h:55,603` | correct |
| A1's `Lexer.h:30-32` ("only a comment on the character-literal rule") and "no C dialect mode" | correct; `Driver.cpp:1124-1136` refuses `.c` |
| A1's `MirAlloc.cpp:40` | `kOffer` is at 41-42; fine |

No wrong citation added by the final; one material omission (`Backend.cpp:104`).

## 6. Section 13 amendments

| R | Decision | Reason |
|---|---|---|
| R1.2 | **Accept** | Names the measures; section 11 carries them |
| R2.1 | **Accept** | Makes the target-2 exception explicit and gated by R9.4 |
| R4.3 | **Accept**, one caveat | "C++'s semantic rules derive from C's" is an organisation (`SemaCxx : SemaC`), not a language fact: C++ *tightens* C in places (no implicit `void*` → `T*`, no implicit `int`, `'a'` is `char`, `const` objects have internal linkage) and overriding must refuse as well as extend. Fine as wording |
| R4.5 | **Accept** | Matches both documents |
| R5.3 | **Accept** | Add `make_shared`/`weak_ptr`, which section 8 lists and the amendment omits |
| R6.3 | **Accept** | |
| R6.6 | **Accept** | |
| R9.1 | **Accept** | |
| R9.2 | **Accept** | |
| R3.1 | **Reject as worded; accept with a rewrite that states the fact.** | cpp11 1.28 defines `__cplusplus` 201103L (`Backend.cpp:104`); "stays 199711L" is false. Proposed: "R3.1: `__cplusplus` is reported as 199711L and `__CPP11_LANG` as 201103L until the C++11 suite passes, as MSVC does with `_MSVC_LANG` without `/Zc:__cplusplus`; `-Zc:__cplusplus` reports 201103L as cpp11 1.28 does today. The change from 1.28 is recorded in the oracle as a deliberate R1.1 exception." If the owner prefers to keep 1.28's behaviour, drop the first clause; either way the document must not say "stays" |

## 7. Sign-off

**Fable 5.1 agrees to `Compiler_Design_Final.md` as Draft 2, subject to:**

1. **F-0b / F-13:** the R3.1 row and section 0's C4 row state that cpp11 1.28 reports `__cplusplus` 201103L (`Backend.cpp:104`), that R3.1 as kept is a revert recorded as an R1.1 exception in the oracle, and that a `-Zc:__cplusplus`-style switch restores 201103L.
2. **F-0a / F-3.2:** section 3.2 replaces the parser-private name-kind scope with "the parser asks Sema's `Lookup` for a name's kind and keeps no table of its own" (Opus's C5, conceded).
3. **F-12c / F-5b:** target 3 (x86_64-linux) moves to the new middle end in M3 with target 1, since both run the same `X86_64Linux` code and the same text optimizer; arm64 alone remains for M5; the two named exceptions to the one-milestone rule are written into section 12.
4. **F-12b:** M2 opens with the in-place extraction of Sema interfaces (A1 section 3.1), each move gated by bootstrap + oracle, before the handle syntax tree is introduced behind its flag; section 14's fallback becomes M2's first step.

The non-blocking flags (F-2a, F-2b, F-3.3a/b, F-3.4, F-4, F-6, F-7, F-10a/b, F-11, F-12a, F-12d, F-12e, F-15) and the `make_shared`/`weak_ptr` addition to R5.3 are for Draft 2.1.

Corrections to my own review, for the record: `Driver.cpp:497` does not concern output order (section 5); "pays twice" and "blocker" for the M1 self-host were overstated (section 3); the parser-private name-kind scope of my section 7.2 is insufficient for qualified names (section 2, C5).

- Fable 5.1, 2026-10-22
