# Design Requirements - the merged C++ compiler

Draft 2, 2026-10-22. One compiler with the best of cpp11, cppparse and compilerpp.

## 1. Base and goal
- **R1.1** Built on cpp11 1.28 (`toolchain\Compilers\C++Optimize`); nothing working in cpp11 is lost.
- **R1.2** Takes the strongest part of each source compiler (section 4) and beats all three on every measure in section 9.

## 2. Targets, in order of precedence
| # | Target | Object form |
|---|---|---|
| 1 | x86_64-windows | COFF |
| 2 | tms6747 (TMS320C6747) | TI COFF / ELF via asm6x, lnk6x |
| 3 | x86_64-linux | ELF |
| 4 | arm64-darwin | Mach-O |

- **R2.1** Work, tests and fixes go in this order; a release needs targets 1 and 2 passing.

## 3. Language accepted
- **R3.1** ISO/IEC 14882:2011 (C++11) complete on the C++03 base; `__cplusplus` is 201103L only when the C++11 suite passes.
- **R3.2** C90 through the same front end (C layer first, C++ derived from it).
- **R3.3** Anything not supported, and every C++14/17 feature, is refused by name with a suggested fix.

## 4. Architecture
- **R4.1** Stages: preprocessor → parser → typed AST → semantic checks → lowering → SSA IR → optimizer → target code → object or assembly.
- **R4.2** Semantics leave the parser (cppparse's split); one optimizer for all targets on the SSA IR (cppparse's middle end).
- **R4.3** C and C++ as two layers, C++ deriving from C (compilerpp's design).
- **R4.4** Own object writer for target 1 (cppparse's COFF writer: encoding, relocations, unwind, COMDAT, CodeView); masm and clang become optional.
- **R4.5** A bytecode VM (compilerpp's) evaluates `constexpr` and runs a program without linking.
- **R4.6** `--dump=ast,ir,ssa,asm` at every stage; `--health` report on the same front end (cppparse's analyser); files compiled in parallel.

## 5. Implementation language and self-hosting
- **R5.1** The compiler is written in C++11 only; checked with Visual Studio's `clang -std=c++11` until R5.2 holds, then by the compiler itself.
- **R5.2** Self-hosting on target 1: stage 0 is built by cl, stage 1 by stage 0, stage 2 by stage 1, stage 3 by stage 2; stages 2 and 3 produce identical output. Stage 1 is built at -O0 first, then at -O2.
- **R5.3** The compiler's source uses only headers its own library provides. Not available today, so replaced in the source: `<unordered_map>` by `<map>`, `<deque>` by `<vector>`, `<regex>` removed, `<mutex>`, `<atomic>` and `<thread>` behind one Win32 wrapper (R6.5).
- **R5.4** C++14 rules are removed from the source: no struct with member initialisers is initialised as an aggregate (today `Op`, `Type::BaseSpec`).
- **R5.5** Every construct the compiler's source needs is supported on target 1 (today: rethrow `throw;`, `parser/Parser.cpp:1044`).
- **R5.6** Self-hosting on targets 3 and 4 follows the target order; target 2 is never a host.

## 6. Memory and pointer rules (implementation)
- **R6.1** No `new`, `delete`, `malloc`, `calloc`, `realloc`, `free` or `alloca`, and no bare owning pointers.
- **R6.2** Objects are held by value or in standard containers (`std::vector`, `std::string`, `std::map`, `std::set`).
- **R6.3** Nodes refer to each other by index handles into their container, not by pointer; non-owning access by reference.
- **R6.4** Where a polymorphic owner is unavoidable, `std::unique_ptr` made by one C++11 helper, `make_owned<T>(args...)`; its body holds the only `new` in the code base.
- **R6.5** Raw pointers only at an operating-system boundary (Win32 calls), wrapped in one place.
- **R6.6** A build gate rejects any new violation; existing cpp11 code is converted module by module and the count only goes down (start: 1,111 `new`, 23 `delete`).

## 7. Dependencies
- **R7.1** Kept: Microsoft `link.exe`, the CRT, the Windows SDK libraries, cdb. Not built: own debugger, runtime or Windows linker.
  - *Amended 2026-10-25, the user's decision:* the project's own `LINK` (GRIDE's, `link.exe`'s command line, PE32+, a PDB) is the default linker for x86_64-windows's default spelling, `-masm=masm`, with the project's masm. Microsoft's `link.exe` is kept as R9.3's oracle, and is still the linker of the `ml64` fallback taken where no masm.exe is found.
- **R7.2** Target 2 needs nothing outside the toolchain (asm6x, lnk6x, RTS6x, sim6747).

## 8. Diagnostics
- **R8.1** `file:line:col: severity: message`; one line, then a hint line where useful.
- **R8.2** On target 1, the matching cl error number where one exists.

## 9. Acceptance
- **R9.1** Self-hosting on target 1 (R5.2) is the first gate; every later change keeps it.
- **R9.2** A conformance suite (C++98, C++11 parts) grown from the 14 probes; every probe passes on targets 1 and 2.
- **R9.3** Objects link with cl-built objects and Microsoft's CRT.
- **R9.4** Target 2 code measured in sim6747 cycles; no regression.
- **R9.5** The compiler builds with 0 errors and 0 warnings; each change is measured against R9.1-R9.4.

## 10. Not in scope
C++14 and later (until R3.1 is met); an own debugger, runtime, CRT or Windows linker.

## Baseline (2026-10-22)
cpp11 1.28 compiling its own 60 source files: 2 compile. First errors: 28 on the C++14 aggregate rule (R5.4), 29 on missing headers (R5.3: `<unordered_map>` 16, `<mutex>` 9, `<atomic>` 2, `<regex>` 1, `<deque>` 1), 1 to examine (`'LineFields' was not declared`).
