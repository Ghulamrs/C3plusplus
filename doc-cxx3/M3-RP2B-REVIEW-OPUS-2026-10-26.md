# Review point 2b (M3a's gate), reviewed by Claude Opus 5.5 - 2026-10-26

For this point the roles were swapped: Fable 5.1 implemented M3a.3-M3a.5 on branch `m3-fable` (`6552790`..`d72c23a`,
from `478f504`), and Opus reviews. The packet is M3-PLAN's "Where it stands", entries R1.2 to M3a.5.

**Verdict: agree with conditions.** M3a closes once the conditions below are met. One of them is a wrong answer at
-O1/-O2 on x86_64-windows that M3a.4 introduced and nothing in the packet could see. It is mended in `36f5f8d`
(branch `fix-funclet-jump`, on `d72c23a`).

## Re-measured outside the writer's tree

Each number was re-measured on the Mac from `git archive`s of `a601975` and `df365f0`, built fresh under
`~/cxxopt-build/rp2b-*`, not in the agent's worktree:

- `tools/identical.sh` against `a601975`: **6,039 compared, 0 differ**. M3a.5 changes nothing emitted.
- `make test` green (run.sh 988/0, names.sh 655/0, typed.sh 648/0, exclusions 126 sites 0/0, comment cap 0) and the
  memory gate held.
- `tools/determinism.sh`: **16,414 outputs, 3,980 of them `-dump=mir`, 0 differ** (RP2a condition 2).
- `tools/self-host`: **71 of 71 at -O0 and at -O2**.

Linux (the agent's runs, read from the box): at `a601975`, run.sh 990/0 at -O0, -O1 and -O2; tms6747.sh 621/0 at
-O0 and -O2 with every pass also on sim6747; ti-link.sh 630 linked at both levels. C7's Linux leg (`m3b0`): run.sh
990/0 at -O0 and -O2, typed.sh and adapted.sh 648/0.

## What the packet could not see: M3a.4 broke nested handlers on Windows at -O2

**The Wine stand-in** (Linux box) works like this:
- cxx3's x86_64-windows objects (`-S` and masm `6bcb5d2`, as the default spelling writes them) are linked by the
  project's LINK against the Microsoft CRT and SDK libraries `xwin` 0.10.0 unpacked (the user accepted Microsoft's
  terms);
- the programs run under Wine 6.0.3 in an Ubuntu 22.04 container.

**Its results at `df365f0`:**
- -O0: **616 of 618** cases print their `.expected`.
- -O2: **614**.

**The two failing at both levels**, `noexcept-terminates` and `temporary-unwind`, are Wine 6.0's unwinder, and not
cxx3: the same two crash at the same address when built by `7d593d5`, M3's base, and both pass on a real Windows box
at M2.18.

**The two failing at -O2 alone**, `nested-handler-exits` and `goto-out-of-handler`, are M3a.4's:
- `nested-handler-exits` prints `23 1 16 103` for `23 5 2016 103`;
- `goto-out-of-handler` loses loop iterations and leaks an object (ledger `live=1 over=3`);
- from M3a.4's parent, `7040edf`, both print the right values.

**The cause** is in `remove-unreachable`'s rule that drops a jump to the next label:
- M3a.4 put an inner handler's funclet blocks between the outer handler's `caught` label and the code that label
  leads to;
- the rule looked past directives for the jump's target, and so took `jmp $msskip15` beside
  `$msbrk14: .funclet-leave; $msskip15:` for a jump to the next label;
- so every way out of a nested handler took the break path.

**The mend** (`36f5f8d`): a funclet's boundary ends the look.
- `identical.sh` against `df365f0` moves exactly those two cases' eight -O1/-O2 Windows outputs.
- Wine reads **616 of 618 at -O2**: the two failures left are Wine's.

This is the case for the Wine leg: x86_64-linux has no funclets, and the structural checks (`winchk`) passed because
the tables were well formed.

## Conditions

1. **`36f5f8d` comes into the M3 line**: done by the integration merge that follows this review.
2. **RP2a condition 6, TSan in RP2b's gate on Linux.** Queued by the agent and running on the box at this writing;
   its result is to be recorded in "Where it stands".
3. **M3a.5's own Linux leg and the bench stand-in.** Queued on the box behind TSan; to be recorded. M3a.5 changes
   nothing emitted, so a601975's Linux numbers carry, but the leg is the proof.
4. **The Windows rows.** The Wine stand-in now answers `run-cases.cmd`'s two levels, recorded as a stand-in and not
   as the box. Still owed to a Windows box: cl building the tree, the GNU-spelling pass, `catch-check`,
   `throw-check`, `sret-check`, `winlink-check`, `self-host.cmd`, `bench.cmd`, cdb on the `-g` funclet cases, and
   RP2a condition 4's ml64 baseline of main.
5. **Compile time.** R1.2's row was taken on a loaded Mac. 6552790 takes the x86_64-linux -O2 corpus from about
   410 s to about 65 s, and that holds: `ParserTopLevel.cpp` alone went from 180 s to 7 s. The row's number is still
   to be taken quietly.
6. **The self-host blocker.** `512fc7b` (`fix-nsdmi-enum`, on `f5967f9`) makes a default member initialiser read in
   its class's namespace. With it, `OptValues.cpp:18` may lose the `opt::` that M3a.5 added as a workaround. The
   integration takes it, and Linux's self-host, which stopped at stage 1 at `a601975`, is re-run on the merged line.

## Decisions accepted

- M3a.5 keeps the IR's types in `namespace opt` (M3-IR 2.5's reason).
- `FuncletRange` is not built; `Block::funclet` takes its place.
- A funclet's pseudo is forbidden every preserved register, and none spans a funclet's boundary (2.9's open question,
  cl's leaf-shaped rule).
- The R1.2 allocator fix is its own commit, output byte-identical.
