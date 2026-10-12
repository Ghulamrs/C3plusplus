# Compiler-Si against Shalimar-Codex: compilation power, output size and speed

**2026-10-12, Windows box (12th Gen Intel Core i7-12700, Windows 11 Pro, Visual Studio 2022 17.14, cl 19.44).**
Both compilers built from source on the box and pointed at the same eight Shalimar programs. Every number
below was produced by one scripted run (`match.cmd`) and nothing was taken from either project's documents.

## The contestants

| | Compiler-Si (`shc.exe`, Shalimar 1.2) | Shalimar-Codex (`shalimarc.exe`, Rev3) |
| --- | --- | --- |
| built with | cl 19.44, `/O2 /std:c++14`, 719,872 bytes | cl 19.44 through CMake, Release, 585,216 bytes |
| build note | `build.bat` had never gained `Optimize.cpp` and `Tms6747.cpp` (the Makefile has both); the link failed with 27 unresolved symbols until they were added on the box | built as shipped, 9 warnings |
| x86-64 path tried | `shc prog.shm -o prog.exe` (assemble with ml64, link with the Shalimar runtime) | `shalimarc prog.shm --object prog.obj --release --target=windows-x86_64` (its own COFF writer; the only x86-64 mode the Rev3 driver has) |
| C6747 path tried | `shc prog.shm -S --target=tms6747` | `shalimarc prog.shm -S prog.asm --target=tms6747 --release` |

## The programs

Four of the twelve originals both projects carry (`invert`, `gaussseidel`, `rotmat`, `rotations`) and four scaled
to be hard, each printing a checksum so a wrong answer shows as a wrong number:

| program | what it does |
| --- | --- |
| bench_matmul | three 160x160 real matrix products in a chain, traces and two corners printed |
| bench_invert | a 120x120 diagonally dominant matrix inverted in place by Gauss-Jordan, then multiplied by a copy of the original; the trace must be 120 |
| bench_gaussseidel | Gauss-Seidel on a 300x300 system to 1e-10, at most 2000 sweeps |
| bench_rotations | 200,000 rotation matrices from sin and cos, each composed of three axis rotations by two 3x3 products and applied to a vector |

## Results

| program | shc compile ms | shc .obj bytes | shc .exe bytes | shc run ms (best of 3) | shc C6747 .asm bytes | Codex `--check` | Codex x86-64 object | Codex C6747 assembly |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| invert | 224 | 8,561 | 192,512 | 11 | 58,876 | passes | refused | refused |
| gaussseidel | 110 | 5,501 | 189,440 | 12 | 33,894 | passes | refused | refused |
| rotmat | 115 | 9,651 | 197,120 | 12 | 73,663 | passes | refused | refused |
| rotations | 112 | 5,289 | 193,024 | 12 | 31,941 | passes | refused | refused |
| bench_matmul | 113 | 6,404 | 189,440 | 150 | 43,655 | passes | refused | refused |
| bench_invert | 116 | 9,696 | 192,000 | 56 | 72,685 | passes | refused | refused |
| bench_gaussseidel | 110 | 5,885 | 189,440 | 24 | 37,538 | passes | refused | refused |
| bench_rotations | 104 | 10,723 | 198,656 | 271 | 82,991 | passes | refused | refused |

- **Compiler-Si: 8 of 8 compiled, assembled, linked and ran, with correct output.** The four originals print
  exactly the lines recorded in their `// expected:` comments. `bench_invert` prints `trace of M^-1 M 120.0000000`,
  which is the identity. The run column is the best of three; the first run of each binary was 60 to 400 ms
  slower (page-in), e.g. invert 82, 12, 11 ms and bench_rotations 692, 271, 278 ms.
- **Compiler-Si also emitted C6747 assembly for all 8** (`shc_dsp_rc 0`). It was not assembled or run in this
  session, so that column is a size, not a verdict.
- **Shalimar-Codex: 8 of 8 pass its front end, 0 of 8 produce code on either target.** Its `--check` answers
  "S-mode frontend and IR lowering passed for windows-x86_64 (not target code-generation or native debug
  verification)" for every program. Its x86-64 object writer refuses each in about 85 ms with
  `S-mode COFF function <name>: rev36 supports only zero-argument int functions with one basic block`, naming the
  first function it meets (`invert`, `solve`, `ROX`, `matmul`). Its C6747 path refuses with
  `TMS6747: array/reference/char parameter unsupported`, or `arrays unsupported` for `rotations`.
- The `.exe` column is the whole program: the Shalimar runtime and the static CRT are most of the 190 KB; the
  `.obj` column is the compiled program alone, 5 to 11 KB.

## What the match says

**Power.** On these programs the match has one side. Compiler-Si compiles the whole language and every one of the
eight ran correctly; Shalimar-Codex parses and checks the whole language and generates code for none of them,
because every program here passes an array to a function, and its two code generators accept neither arrays nor
parameters. This agrees with Codex's own `WINDOWS_COMPILER_QUALIFICATION.md` ("none of the 12 specification
example programs ... generates a Windows native object") and its Rev3 driver, which answers `--emit-c` with
"C backends and C compiler options have been removed". The C backend that once ran the twelve originals on
Linux is no longer in the tree.

**Size and speed** can therefore only be reported for Compiler-Si. Its code is a stack machine calling the
runtime for every arithmetic operation, which is what the numbers show: 12.3 million multiply-adds of
`bench_matmul` in 150 ms (about 80 M/s) and 200,000 rotations, 36 products each with sin and cos, in 271 ms.
A native cl build of the same algorithms would be an order of magnitude faster; no such reference was run here.

**Two things to carry back to the repositories.**
1. Compiler-Si's `build.bat` must list `src\backend\Optimize.cpp` and `src\backend\Tms6747.cpp`, as the Makefile
   does. Without them `shc.exe` cannot be built on Windows at all.
2. Shalimar-Codex's `--check` success is not compilation. A reader who stops at "passed" is misled; the object
   step is where the answer is.

## Reproduction

`C:\cxx1\shm-match` on the box holds both trees, `programs\`, `match.cmd` and `results\`. The script builds both
compilers, then for each program times the `shc` compile (PowerShell `Measure-Command`), records `.obj`, `.asm`
and `.exe` sizes, runs the executable three times, and records Codex's `--check`, `--object` and `-S` verdicts
verbatim. `report.py` turns `results\*.txt` into the tables above.

## The scalar round: Codex's own 142 fixtures through both compilers

Run the same evening on the same box. Codex's `tests/**/*.shl` were copied, and the 139 written `fun main()`
plus two other bare `fun name(` lines were rewritten to the spec's `fun <> = name(`, which is the only form
§8.1 gives and the only one `shc` reads. Nothing else was changed. Each fixture went through `shc` (x86-64
executable, `-S --target=tms6747`) and `shalimarc` (`--check`, `--object --target=windows-x86_64`,
`-S --target=tms6747`), all `--release`.

| of 142 fixtures | shc | Codex |
| --- | --- | --- |
| front end accepts | 91 (builds and links an x86-64 program) | 120 (`--check`) |
| x86-64 code produced | 91 executables | 27 objects (88 to 229 bytes, one function each, no `main` that runs) |
| C6747 assembly produced | 91 | 21 |
| both accept / both refuse | 85 / 16 | |

**Where the front ends disagree, the spec was read for each rule.**

| shc refuses, Codex accepts (35) | spec |
| --- | --- |
| 33: commas in a print list, `? a, b, c` | §7.8 is `? expr expr ...`, space-separated. A Codex extension; the fixtures are off-spec. |
| 1: `return (12)` from a function declared `<int, char, int>` | §8.1: "a declared output must be returned". shc is right; Codex over-accepts. |
| 1: a program redefining `pi` | line 1143: `pi : 3` may not quietly overwrite the constant. shc refuses as the spec says. |

| Codex refuses, shc accepts (6) | spec |
| --- | --- |
| 3: an array literal with more entries than the extent, `int v[2] : {1, 2, 3}` | §9 says what a short literal does and nothing about a long one. Codex is stricter; the spec is silent. |
| 3: "function calls are not permitted inside return parentheses", `return (int(x), 2)` | no such rule in the document. A Codex rule of its own, refusing programs the spec allows. |

**The runtime-negative fixtures separate the two cleanly.** Nine fixtures exist to fail at run time, an index of
-1, an `int` overflow, a `char` of -1, an array of size 0. `shc` builds all nine and each program stops with the
spec's diagnostic (`Index -1 out of range 0...2`, `int overflow in '+' - use real`). Codex's `--check` passes
them too, correctly, and then has nothing that runs.

**Size, where both produce code.** 21 fixtures reach C6747 assembly from both. As assembly text, `shc`'s is
2.8 to 12.2 KB and Codex's 0.3 to 8.3 KB; neither was assembled here, and `shc`'s carries the runtime calls and
overflow checks the language requires while Codex's scalar bridge performs none, so the bytes are not the same
contract. On x86-64 the comparison is between whole programs (`shc`: 145 KB executables, 1.0 to 2.3 KB objects)
and bare functions (Codex: objects under 230 bytes with no startup and no runtime), which is no comparison.

**Speed could not be measured.** Every one of the twelve `tms6747` fixtures has an empty `main` and prints
nothing: the `shc` executables run in 5 to 6 ms, which is process start-up, and Codex's objects are not
runnable without its DLL harness. A speed match on this subset needs programs that do work, and the scalar
subset Codex generates for excludes arrays, so such programs would have to be written for it.

**What the scalar round adds to the verdict.** Codex's front end is wide and slightly off-spec in both
directions; its code generators are narrow. `shc` follows the document line by line, refuses what it refuses,
and runs everything it accepts, including the fixtures written to break at run time. Of Codex's 142 fixtures,
the 35 it accepts and `shc` does not are, with one exception, fixtures that the specification does not accept
either.

Reproduction: `C:\cxx1\shm-match\fixtures\`, `scalar.cmd`, `timing.cmd`; results in `results\scalar\`
(`summary.csv`, one `.txt` per fixture, `timing.txt`).
