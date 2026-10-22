# Compiler Design - cpp11 2

Draft 1, 2026-10-22. The design that meets `Compiler_Design_Requirements.md` (Draft 2); requirement numbers (R*n.n*) are cited where a decision serves one.

## 0. Authorship and sources

| Role | Who |
|---|---|
| Requirements | **G. R. Akhtar** (owner): target order, C++11, written in C++11, self-hosting, no hand-managed memory |
| Design (this document) | **Claude** (Anthropic, model Opus 5.5), working in Claude Code at G. R. Akhtar's request; not yet reviewed |
| Source compilers | **G. R. Akhtar**: cpp11 1.28 (`toolchain\Compilers\C++Optimize`), cppparse v6 (`examples\projects\cppparse`), compilerpp (`examples\projects\compilerpp`) |
| Outside ideas | published designs only, no code taken: LLVM (MC layer, IR), GCC (bootstrap compare, IRA, ipa-inline), Cranelift and MLIR (block arguments), Clang (32-bit source locations, fix-it hints), Zig and Carbon (data-oriented trees in arrays), Cytron et al. (SSA), Briggs (graph-colouring allocation), Rau (iterative modulo scheduling), Microsoft's PDB/CodeView documentation |

The five sources: (1) the requirements, (2) cpp11, (3) cppparse, (4) compilerpp, (5) the outside ideas above. Each section below names what it takes from which.

## 1. Shape

The product keeps the name and command line of cpp11 (GRIDE's `toolchain.json` calls it) and becomes version 2. One executable, one front end for C90 and C++11, one middle end, four back ends:

```
source → Preprocessor → Lexer → Parser (C layer, C++ layer) → Syntax tree
       → Sema → Typed tree → Lowering → SSA IR → Optimizer
       → Target: selection → Machine IR → allocation → scheduling (C6000)
       → Emitter: assembly text | object file (target 1)
```

Every arrow is a module boundary with a dump (`--dump=tokens,syntax,typed,ssa,mir,asm`, R4.6), from compilerpp's `-ast/-ir/-bc`.

## 2. Data model - no pointers, no hand allocation (R6)

This is the decision everything else rests on.

- **Arenas of values.** Each kind of node lives in a `std::vector` owned by its stage: `Arena<SyntaxNode>`, `Arena<TypedNode>`, `Arena<Type>`, `Arena<Block>` ... Nodes refer to each other by **handles**: `struct ExprId { std::uint32_t index; }`, one handle type per arena so a type mismatch is a compile error. (Zig's and Carbon's array-of-nodes trees; cppparse's IR already numbers blocks and temporaries this way, `middle/Ir.hpp`.)
- **Variants without heap polymorphism.** A node is `{ kind, first, count, payload }`; children are a range in a side vector of handles; payloads of each kind sit in their own vector. A `switch` on `kind` replaces virtual dispatch over heap objects. Where a polymorphic object really helps (a target, a pass), it is a stack object or a `std::unique_ptr` from `make_owned` (R6.4), owned by the driver.
- **Interning.** Identifiers and strings in one `StringTable` (handle `Name`); types in a `TypeTable` that hands out one `TypeId` per distinct type (cpp11's and cppparse's tables already intern).
- **Source locations** are one 32-bit offset into a file table (Clang's `SourceLocation`), so every node carries its place for diagnostics and line tables at four bytes.
- **Determinism.** Handles are numbered in creation order, never by address, so two runs give the same output: the fixed point of self-hosting (R5.2) depends on it.
- **Operating system.** File, process, thread and time calls live in one `os` module, the only place with raw pointers (R6.5): `os::File`, `os::Process`, `os::Pool` (worker threads over Win32, replacing `<thread>`, `<mutex>`, `<atomic>`, R5.3).
- **Gate.** A check run in every build counts `new`, `delete`, `malloc`/`free` family and owning raw pointers outside `make_owned` and `os` (R6.6); clang-tidy's `cppcoreguidelines-owning-memory` and `-no-malloc` confirm it.

## 3. Front end

**Preprocessor and lexer** - cpp11's (`src/Preprocessor.cpp`, `src/Lexer.cpp`), which already reads MSVC's and the SDK's headers; tokens go into one `std::vector<Token>` per translation unit, and a token range is a pair of indices.

**Parser - two layers (R4.3, compilerpp).** `CParser` parses C90 and builds syntax nodes; `CxxParser` derives from it and adds classes, templates, namespaces, overloaded operators, lambdas, exceptions through virtual hooks, as compilerpp's `Parser1` derives from `Parser`. The parser only builds syntax: no lookup, no types (R4.2). c90's separate compiler retires when the C layer passes c90's tests (R3.2).

- Ambiguities that need a name's kind (`a * b;`, `T(x)`) are kept as an *ambiguous* node and settled by Sema, the approach of C++ front ends that parse before lookup.
- Member bodies are kept as token ranges and parsed when the class is complete; template bodies are kept as token ranges and parsed per instantiation - cpp11's token replay (`src/parser/ParserTemplate.cpp`), unchanged in substance.

**Sema - the typed tree.** Takes the work out of cpp11's parser (34,790 lines today) into its own modules, each a few thousand lines:

| Module | Content | From |
|---|---|---|
| `Lookup` | scopes, using-directives, ADL [basic.lookup.argdep] | cpp11 `ParserOverload.cpp:978` |
| `Overload` | implicit conversion sequences, ranking [over.ics], tournament | cpp11 `ParserOverload.cpp:603, 842`; cppparse `SemaExpr.cpp:293` |
| `Templates` | deduction, partial ordering, partial specialisation, instantiation by token replay; deduction with partial explicit arguments added | cpp11 `ParserTemplate.cpp`; cppparse `SemaExpr.cpp:616` |
| `Classes` | layout, vtables, virtual bases, vtordisp, special members, implicit moves | cpp11 `ParserClass.cpp`, `ParserVtable.cpp`; cppparse `IrGen.cpp:4989` |
| `Constexpr` | constant evaluation by the VM (section 6) | compilerpp `VM.cpp` |
| `Checks` | access, override/hide, the health rules (section 8) | compilerpp `Semantic.cpp:804`; cppparse `analyzer/report/Health` |

Refusals (R3.3) come from one table: feature → message → hint, compilerpp's style (`Lexer.cpp:108-141`) with cpp11's wording ("... is C++14, and this compiler is C++11").

**Lowering.** The typed tree becomes C-level operations: constructor and destructor calls, cleanups at every block exit (compilerpp `Lower1.h:13`), temporaries' lifetimes (cpp11 1.26), exception regions and funclets (cpp11 `Ast.h` `Try`, `FuncletLeave`). Output: the SSA IR.

## 4. Middle end - one optimizer for all targets (R4.2)

**IR** - cppparse's `IrFunction / IrBlock / IrInst` (`middle/Ir.hpp:64-105`), extended:
- **Block arguments instead of phi nodes** (Cranelift, MLIR): a branch passes values to its target block. SSA construction stays Cytron's (cppparse `middle/Ssa.cpp`: dominators, frontiers, placement, renaming); destruction becomes copies on edges.
- Types are `TypeId`s; operands are value handles; exception edges are explicit (`invoke`-style: a call with a normal and an unwind successor), which keeps cleanups visible to the passes.

**Passes**, with a pass manager per optimisation level:

| -O0 | none (straight lowering; fast, for debugging and stage 1) |
|---|---|
| -O1 | constant folding, copy propagation, algebraic simplification, CSE/GVN, dead code and dead stores, CFG simplification (cppparse `Optimizer.cpp:274-605`, `Ssa.cpp` GVN); jump threading, division by magic numbers (cpp11 `OptJumps`, `OptDivide`) |
| -O2 | -O1, then inlining with GCC-style budgets (cpp11 `Inliner.h`), loop-invariant motion and counter widening (cpp11 `OptHoist`, `OptLoops`; cppparse LICM), unrolling (cppparse), register promotion with exceptions in mind (cppparse `Optimizer.cpp:1117`) |

`volatile` is an IR property (a volatile load/store is never moved or merged), not a reason to switch -O off (cpp11 today, `Driver.cpp:1218`).

## 5. Back ends, in target order (R2)

Each target supplies: instruction selection (SSA → Machine IR with pseudo registers, cpp11's `mir`), its ABI (calls, frames, unwind), allocation, and emission.

| | 1 x86_64-windows | 2 tms6747 | 3 x86_64-linux | 4 arm64-darwin |
|---|---|---|---|---|
| Selection | cpp11 `X86_64Windows`, Microsoft x64 ABI | cpp11 `Tms6747`, C64x+ compact forms | cpp11 `X86_64Linux`, System V | cpp11 `Arm64Darwin` |
| Allocation | Chaitin-Briggs (cpp11 `MirAlloc`, after GCC's IRA) at -O1+; linear scan (cppparse `Lower.cpp:44`, `X86.cpp:374`) at -O0 | same | same | same |
| Scheduling | - | packets, NOP padding, modulo scheduling of inner counted loops (cpp11 `C6xSched`, `C6xPipe`; Rau's iterative method) | - | - |
| Exceptions | `__CxxFrameHandler3` tables, funclets; **rethrow and try-in-catch added** (R5.5) | RTS6x's model | Itanium, `__gxx_personality_v0` | Itanium |
| Debug info | CodeView | `.dwpsn` | DWARF | DWARF |
| Output | object file or MASM text | asm6x text → asm6x, lnk6x (R7.2) | GNU text | GNU text |

**Emitter - one instruction stream, two sinks** (LLVM's MC layer). A target emits `MInst`s into an `Emitter` interface with two implementations: `AsmEmitter` (text for masm, asm6x, GNU as) and, for target 1, `CoffEmitter` - cppparse's writer (`backend/Coff.cpp`: REX/ModRM/SIB encoding, REL32/ADDR64/ADDR32NB relocations, `.pdata`/`.xdata`, COMDAT, CodeView `.debug$S`). Assembly and object come from the same instructions, so `-S` output assembled by masm must equal `-c` output byte for byte - a built-in test of both. masm and clang become optional (R4.4); since `cf1e607` the toolchain's masm writes CodeView and its link writes a PDB, so the text route also needs no Visual Studio tool for a Debug build.

## 6. The VM (R4.5)

compilerpp's stack machine (`Bytecode.h`, `VM.cpp`), fed from the SSA IR instead of its own IR:
- **constexpr**: Sema lowers the constexpr function to SSA, the VM runs it with limits (steps, memory - compilerpp's 50M-step cap and guards), and the result becomes a constant; a fault is a diagnostic at the call.
- **`--run`**: the whole program in the VM on any host, with natives for the C library calls - quick tries without a linker, and a reference to compare target code against in tests.
- Its memory is one `std::vector<std::uint8_t>`; addresses are offsets (R6).

## 7. Driver and library

- **Driver**: cpp11's options stay. `-j N` compiles translation units in parallel on `os::Pool` (cppparse `common/Parallel.cpp`); output order stays deterministic.
- **Library** (`include\`, `lib\`): cpp11's 37 headers, then what the compiler's own source and the suite need, in that order: `make_shared`, `weak_ptr` (cpp11's `<memory>` notes them missing), `<unordered_map>` later; the C library over the UCRT (target 1), RTS6x (target 2).

## 8. Diagnostics and health (R8, R4.6)

- One `Diagnostics` sink: code, severity, location, message, optional hint and fix-it (a replacement text at a range, Clang's style). Codes keep cppparse's families (E/W/I/T) and gain cl's number on target 1 where one matches (cppparse `(cl C2027)`).
- `--health` runs cppparse's analyser rules on the typed tree, so they now understand C++11 and stop guessing at macros (cppparse evaluates no `#if` today); reports in text, HTML, PDF, SVG as cppparse writes them (`analyzer/report`).

## 9. Self-hosting and bootstrap (R5, R9.1)

1. Stage 0: cpp11 2 built by cl (`/std:c++14`, the closest cl has) and checked with `clang -std=c++11`.
2. Stage 1: built by stage 0 at -O0; stage 2 by stage 1 at -O2; stage 3 by stage 2 at -O2.
3. Stage 2 and stage 3 must give identical assembly and objects (GCC's bootstrap compare). The handles of section 2 make output independent of addresses, which this needs.
4. The test suite runs on stage 3.

Baseline today: 2 of cpp11's 60 files compile with cpp11; first errors are the C++14 aggregate rule (28 files) and headers its library lacks (29 files) - work for milestone M1.

## 10. Tests (R9)

| Suite | What | Gate |
|---|---|---|
| probes → conformance | C++98 and C++11 programs that print and return 0, grown from the 14 probes; refusal tests for C++14/17 | all pass on targets 1, 2 |
| bootstrap | section 9 | stage 2 = stage 3 |
| link compatibility | objects mixed with cl-built objects, CRT, exceptions across the boundary | links, runs, exit 0 |
| `-S` = `-c` | masm of the text equals the object writer's object | identical bytes |
| C6000 cycles | sim6747 cycle counts of a benchmark set | no regression |
| VM reference | `--run` output equals the target program's output | identical |
| memory gate | section 2 count | only goes down |

## 11. Milestones from cpp11 1.28

| M | Delivers | Requirements |
|---|---|---|
| M0 | conformance suite, benchmarks, gates in the build | R9 |
| M1 | source made C++11-clean, headers replaced, rethrow and try-in-catch on target 1; **self-hosting at -O0** | R5.1-R5.5, R9.1 |
| M2 | arenas, handles, `make_owned`, `os` module; conversion module by module | R6 |
| M3 | Sema split out of the parser; typed tree; C and C++ layers | R4.2, R4.3 |
| M4 | SSA IR and middle end; target 1, then target 2 moved onto it | R4.1, R4.2 |
| M5 | Emitter with `CoffEmitter` for target 1 | R4.4 |
| M6 | C90 through the C layer; c90 retired | R3.2 |
| M7 | VM for constexpr and `--run` | R4.5 |
| M8 | `--health`, dumps, `-j` | R4.6 |
| M9 | targets 3 and 4 on the new middle end; self-hosting there | R5.6 |

Each milestone ships on its own and keeps every gate of the ones before it.
