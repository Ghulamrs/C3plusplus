# M4, reviewed by Claude Opus 5.5 - 2026-10-26

Fable 5.1 implemented M4 on branch `m4` (`d02b7ad`..`d41275b`, from `b8290cb`) and masm's `m4-rebase` (`920928c`, from
`6bcb5d2`). For this round the roles were swapped: Fable writes, Opus reviews. **Verdict: agree with conditions.** M4
may stand as implemented, and may not be called done until the Windows rows below have passed on a Windows box.

## Re-measured outside the writer's tree

These were re-measured on the Mac from `git archive d41275b` and `git archive 6bcb5d2` (masm), each built fresh under
`~/cxxopt-build/rv-m4/`, not in the agent's worktree:

- `LEVELS="0 2" tools/coff-equal.sh` against masm `6bcb5d2`: **1,342 pairs, 1,302 objects compared, 0 differ**. This
  is the agent's number.
- `make test`, exit 0: run.sh 988/0, names.sh 655/0, coff.sh 1,310 pairs and 1,270 objects with 0 differ,
  exclusions 126 sites 0/0, comment cap 0.

The agent's Linux leg (g++ 11.5, run.sh 990/0 at -O0 and -O2, coff.sh 0 differ) and its re-based masm numbers (enc.sh
23/23, its 48 test objects byte-identical to `6bcb5d2`'s) were not re-run here.

## What is right

- **P2 was taken whole.** About half of a Windows object is composed as MASM text in `Masm.cpp`: unwind data, FH3
  tables, RTTI and funclet lifting. Keeping masm's statement layer inside the library means `-c` and `-S` + masm
  share one encoder from the first commit. The byte equality is therefore real, and is not a semantic comparison
  standing in for one.
- **Instructions now reach the encoder as data.** The structural door (M4.4) passes them as `masm::Operand`s.
  Only the funclet head and tail stay as text, and removing them is M3a.4's job (the `FuncletBegin`/`End`
  directives). That boundary is the one M3-PLAN draws.
- **The code follows the tree's rules.** The memory gate holds at 1,063. The comment cap holds. Basenames are
  renamed where msvc's flat object directory would collide (`AsmLexer`, `AsmCodeView`). arm64-darwin and tms6747
  emit as before.

## Conditions

1. **Windows rows, owed and not claimed.** The next Windows box must run, in order:
   - cl 19.51 building `msvc/build.cmd` with `src/encoder/` under `/W4 /WX`. cl has not seen this code, and its
     `/WX` refused a hidden name in M3a.1 (C4459).
   - `run-cases.cmd` at -O0 and -O2 through `-c`, in both spellings.
   - `throw-check`, `catch-check`, `sret-check` and `winlink-check`.
   - `examples/` linked by LINK and by link.exe against the CRT, with `demo.exe` run.
   - cdb on `-g` objects, as M10 W1-W5 measured.
   - `self-host.cmd` through `-c`.
2. **The ml64 fallback is gone, and that must be recorded as a decision.** The user's toolchain decision of
   2026-10-25 made Visual Studio's ml64 and link.exe the fallback on a Windows host with no project masm. With the
   assembler now inside cxx3, nothing needs assembling outside it, so the fallback is moot for assembly.
   - `-masm=ml64` stays by name.
   - The linker half still stands: the project's LINK by default, link.exe as the oracle.
   - The section CLAUDE.md gains in this branch must say this in one line. It must not leave "-masm=masm ... falls
     back to Visual Studio's ml64" standing as current.
3. **`tools/verify-three` runs `tools/coff-equal.sh`** in its Mac step, as the agent itself noted, so that the gate
   is not left to memory.
4. **Merge order.** `m4` and `m3a` both edit `CLAUDE.md`, and `Masm.cpp` is also touched by M3a.4's funclet work.
   Merge `m4` into the M3 line after M3a.4, so the funclet head/tail text the structural door leaves is replaced in
   one place, and re-run `coff-equal.sh` on the merge.
