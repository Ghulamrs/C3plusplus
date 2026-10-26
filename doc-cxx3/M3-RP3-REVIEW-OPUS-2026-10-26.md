# Review point 3: the first lowering - Opus 5.5's review (2026-10-26)

Of branch `m3b1` (Fable 5.1, `2a46504`, `d5ea461`, `cb33a21`, `2da6019`), merged into `m3-line` at `12dc8dc`, read
with `doc-cxx3/M3-RP3-PACKET-FABLE-2026-10-26.md` and M3-IR 3.12. **Verdict: agree with conditions.** The IR, its
dump and verifier are what part 3 asked; the lowering takes the typed tree with no `Parser` in reach and no layout
map read but one, named; selection re-points the walker so every ABI rule stays in one place. The conditions below
are what M3b.2 owes before it grows the subset far, and two were found by this review's own re-measurement.

## Re-measured outside the writer's tree

A `git archive` of `2da6019` into `~/cxxopt-build/rv-m3b1`, built with clang on the Mac:

| gate | writer's | re-measured |
| --- | --- | --- |
| `make test` | green | green - run.sh 989/0, emit 1,933/0, names 656/0, typed and adapted 649/0, exclusions 126/0/0, comment cap 0 |
| `ssa.sh` | 777 lowered, 777 verified | 777 lowered of 7,425, 777 verified, 0 failed |
| run.sh under `-front=ssa` (Mac) | 989/0 at -O0 and -O2 | 989/0 at -O0 and -O2 |
| `identical.sh`, flag off, against the REF of `1b350ae` | 6,048, 0 differ | 6,048, 0 differ |
| `determinism.sh` with `-dump=ssa` | 21,368, 0 differ | 21,368 (2,988 `-dump=ssa`), 0 differ |
| run.sh under `-front=ssa` (Linux, x86 selection) | 987/4 at `d5ea461`, `cb33a21` owed | **992/0 at -O0 and at -O2** on the merged tree `a877396` (g++ 11.5); `ssa.sh` there 778 lowered, 778 verified |
| Wine stand-in under `-front=ssa` | 617/2 at -O0 and -O2 | not re-run: the two are Wine's own, as on every tree |

**The Mac rows prove less than they look**: the Mac is arm64-darwin, where `-front=ssa` lowers and verifies and
selects nothing - so run.sh under the flag there says the typed tree is read without a fault and nothing about the
code. The proof of selection is the Linux row and the Wine row, both x86.

## Conditions

1. **The packet's count is stale.** It says 1,288 of 7,425 bodies lowered; the gate measures 777 (782 on the merged
   tree with M7's cases). The record in M3-PLAN gives 777 and is right; the packet is the one to read as superseded.
2. **The self-host found what no suite did** - stage 0 of the merged tree did not build stage 1:
   `SsaSelect.cpp`'s block-scope `using ir::Op;` is a form cxx3 refuses by name (a using-declaration inside a block).
   Mended on `m3b2` by qualifying the opcodes. The rule for M3b.2 and after: `tools/self-host` is part of every step's
   gate, not only M3b.6's - the compiler's own source is the largest program it compiles.
3. **The one borrowed layout fact goes at M3b.2**: `receiveParameters` stores the arguments where the parser's
   `Function::params()` says and the selector copies them out. M3b.2 gives the selector its own receive.
4. **`-g` under the flag describes the parser's locals at the parser's slots**, which a selected function does not
   use. Recorded by the writer; it must be mended before `-front=ssa` is the only path (M3b.7), and a `-g` case under
   the flag added to the gate when it is.
5. **Every value takes its own 8-byte slot**, never reused: a selected function's frame grows with its instruction
   count at -O0. Correct, and the frames shrink at -O2 where the MIR passes promote the slots; M3b.4's mem2reg is the
   real answer. Until then a large -O0 function under the flag reaches the stack probe sooner than the parser's path.
6. **A `var`'s bound and its initialiser are one list on the typed tree** (`int a[3] = {...}` against `int a[] =
   {...}`), refused rather than guessed. Agreed with the packet: M3b.2 adds the mark on the syntax node before the
   subset grows past it.
7. **`Typer::fromTemplate` is a linear search per call** over every function the parser declared, asked once per body:
   quadratic in a unit's functions. Harmless at the census's sizes; it becomes a map when M3b.2 asks it per body of a
   large unit.

## What the design asked, item by item

- *An IR with handles, not pointers, numbered in creation order* - yes (`Ir.h`, no raw pointer in `src/ir`, the
  second memory count held at 44).
- *A verifier the lowering is held to* - yes; `-check=ssa` over every case, in `make test` as `tests/ssa.sh`.
- *The lowering reads the typed tree and its record, not the parser* - yes but for the parameter slots (condition 3),
  and the driver's census, which reads the parser and is the harness's, not the lowering's.
- *Selection by the walker re-pointed, the ABI code unchanged* - yes; a call is the walker's own `Call` node over
  `Var`s naming the selector's slots.
- *The flag off changes nothing* - yes, `identical.sh` 0 differ.

## For M3b.2

The order the measurement suggests, by bodies outside: a template's body (3,279), landing pads (529), constructors
and destructors (whose syntax has no node naming what each mem-initialiser initialises - a mark the parser must add
first), a reference to a class returned (~150), sret (77), class-type locals.
