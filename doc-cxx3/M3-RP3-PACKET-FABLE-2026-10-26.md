# Review point 3: the first lowering - packet for Claude Opus 5.5 (2026-10-26, Fable 5.1)

Branch `m3b1`, off `1b350ae` (`m3-line`), local and unpushed. Four commits: `2a46504`, `d5ea461`, `cb33a21` the code; the
one after them the record. M3-PLAN's "Where it stands", entry "M3b.1", has the numbers; M3-IR 3.12 has the design as built and the answers
to 3.11. This packet says what to read, in what order, and what I would question if I were reviewing it.

## What to read

1. `src/ir/Ir.h` - the data model (120 lines). The question for the reviewer: is `Inst` the node 3.1 meant, and are
   `Succ`, `cases`, `call`, `pos` the right places for what 3.1 did not list?
2. `src/ir/IrText.cpp` - the dump and the verifier. The verifier is the oracle `-check=ssa` runs; its dominance is
   Cooper-Harvey-Kennedy over the reachable blocks, unreachable blocks skipped (nothing there is read).
3. `src/ir/Lowering.cpp` - the lowering. Read `lower`, `stmt`, `declaration`, `localArray`, `expr`, `lvalue`,
   `arithmetic`, `convert`, `call` in that order; each refusal is a named reason the report counts.
4. `src/backend/SsaSelect.cpp` and the four lines in `X86_64Linux::emit` and `walkInPlace` - selection.
5. `src/Driver.cpp`, `lowerSsa` - the glue: what the driver hands the lowering, and the one check made there (an
   over-aligned local).
6. `src/sema/Typing.cpp` - three facts added to the typed tree and two accessors; `recordDiffers` exempts the `var`
   record. The M7 agent owns this file's `Kind::Bool` sites; these edits touch none of them.

## What I would question

- **The subset is narrower than the adapter's.** The row says "the adapter's subset"; this lowers its scalar core and
  refuses class objects, temporaries, struct arguments and results, guards and the rest by name. The reason is 12.1:
  every one of those the adapter did by reading the parser's layout maps, which the lowering must not; done the
  lowering's own way they are M3b.2's rows, where class objects and exceptions live anyway. The count is honest:
  1,288 of 7,425 source bodies, and the report names the 6,137 outside by reason.
- **The parser's parameter slots are read.** `receiveParameters` stores the arguments where `Function::params()`
  says and the selector copies them from there into the values' slots. One borrowed layout fact, named in M3-IR 3.12,
  to go when M3b.2 re-points the receive. Nothing else of the parser's layout is read: the allocas, the value slots
  and the frame size above the parser's frame are the selector's.
- **A `var`'s bound and its values are one list on the typed tree**, so `int a[3] = {3, 1, 2}` cannot be told from
  `int a[] = {3, 1, 2}` and is refused rather than guessed; a `for` with a part missing the same. Both want a mark on
  the syntax node, which M3b.2 should add before it grows the subset.
- **-O2 under the flag is the MIR passes over the selector's output**, which is what the row asked; the slots are
  handed over as promotable locals and an inlined callee's frame goes below the selector's layout (`walkInPlace`),
  found by reading, not by a case - the Linux -O2 run under the flag is its oracle.
- **`-g` under the flag** writes line marks from `Inst::pos` but describes the parser's locals at the parser's slots,
  which the selector does not use; a debugger shows wrong values for locals of a selected function. Recorded, not
  mended: the DWARF writer wants the allocas, which is the frame layout M3b.2 owns.

## What is owed

- The Linux leg of `cb33a21` itself: `d5ea461`'s ran (987/4 under the flag, the four mended in `cb33a21`), the final
  one was queued and killed before it started. Its command is `sh ~/m3b1-leg.sh c` over the tree's tarball - M3-PLAN's
  entry says what it prints.

- Windows: a Windows box. The Wine stand-in's rows are recorded as a stand-in.
- The three `volatile` questions of 3.7, with the four corpus cases measured, at M3b.2.
- `tools/self-host` is not run here: at the base `1b350ae` its hand-written source list lacks `src/encoder/*.cpp`
  (M4 added it to the Makefile alone), so stage 1 fails on undefined `masm::` symbols before any of this branch is
  reached. The coordinator is mending it on `m3-line`; M3b.1's gate has no self-host row, so the fix is not duplicated
  on `m3b1`.
