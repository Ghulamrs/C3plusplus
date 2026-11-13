# The optimizer's IR

cxx1 1.5's -O1 and -O2 worked on the assembly the walker had already written. They
cleaned up after a stack machine: pushes paired with pops, reloads forwarded, a few
locals moved into callee-saved registers. That approach has a ceiling. On the
three-box run of 2026-09-23, Compiler++ at cxx1 -O2 was still 2.3x slower than cl
and g++. The step taken since is the one GCC takes: optimize an intermediate
representation with pseudo registers, then allocate real registers once.

This document records what was learned from studying GCC and what cxx1 has
built from it. **It states the tree at `f231c040` (M3b.8, 2026-11-13)**; every
`file:line` below is that commit's, under `src/`. How each stage was built and
measured is `doc-cxx3/M3-PLAN.md`.

## What GCC does, and what cxx1 learned from it

Read from GCC 14.2's `passes.def`, `opts.cc` and the Internals manual. The code was
read for its design only and nothing of it is copied: GCC is GPL-3 and cxx1 is
not.

- **Two IRs.** GIMPLE is a target-neutral three-address form in SSA, where most
  scalar optimization happens. RTL is target-shaped: instruction patterns over
  pseudo registers, where combine, register allocation and the late cleanups run.
- **-O1 is a cheap core.** Constant, copy and forward propagation (`ccp`,
  `copy_prop`, `forwprop`), full-redundancy value numbering (`fre`), dead code and
  dead stores (`dce`, `dse`), scalar replacement (`sra`), sinking, the register
  allocator, and combining stack adjustments.
- **-O2 is -O1 and more.** Partial redundancy (`pre`, `gcse`), value ranges
  (`vrp`), CSE across jumps, inlining of small functions, interprocedural
  constant propagation, `peephole2`, scheduling, crossjumping, store merging.
  Alignment is "speed only", the one axis where -O2 and -Os part.
- **Register allocation comes last and is global** (IRA then LRA). Every pseudo in
  the function competes for every register, with costs; spilling is the
  allocator's decision, not the code generator's.

## Where cxx1's two IRs sit, on x86

x86 (x86_64-linux and x86_64-windows) has GCC's two levels. Every function is
lowered to the SSA IR and selected from it, and there is no other path: a function
the lowering leaves outside fails the compile, named (`Driver.cpp:1588`,
`Driver.cpp:1609-1611`, `X86_64Linux.cpp:1115-1118`).

1. **Lower.** The typed tree becomes the SSA IR (`src/ir/`: `Lowering.cpp`, and
   `Builder.cpp` for the compiler-written functions; `lowerSsa`,
   `Driver.cpp:1348`). Every local starts as an `alloca`; a `volatile` access is
   marked on its instruction (`Ir.h:73`).
2. **Inline,** the unit's pass, at -O2 (`inlineCalls`, `SsaInline.cpp:305`, called
   at `Driver.cpp:1507`): `Inliner`'s budgets decide over `ir::Function`s, and a
   site taken has its callee's blocks cloned in before promote.
3. **The SSA passes,** per function, in `SsaOptimize.cpp:14-30`'s order (the table
   below). `-check=ssa` verifies the IR after each, and `-dump=ssa=<pass>` writes
   it after one (`Driver.cpp:1150`).
4. **Select.** `SsaSelect.cpp` writes each function into the x86 generator's
   instruction stream (`X86_64Linux : Regions`, `X86_64Linux.h:44`; `Regions`
   reads no `Ast.h` node, `Regions.h:13`).
5. **MIR,** GCC's RTL level: that stream with every operand parsed once and every
   register a pseudo (`Mir.h:3-5`, one opcode table, `OptTable.h:276`), run
   through `OptPipeline.cpp`'s passes, then spelled GNU or MASM
   (`Optimizer : Spelling`, `Optimizer.h:16`).

The x87 stack is not modelled in MIR: an operand with `reg.id < 0` is opaque text
and the `X87` class is never allocated (`Mir.h:116`); x86_64-linux's
`long double` is its only user.

**arm64-darwin and tms6747 have neither IR.** They are `Walker`s over `Ast.h`
(`Arm64Darwin.h:39`, `Tms6747.h:53`; `Walker : AstCodeGen`, `Walker.h:92`), each
with its own optimizer over the text it wrote: arm64's peephole (`a64Peephole`,
`A64Peep.h:12`, called at `Arm64Darwin.cpp:1223`) and the C6000's scheduler
(`c6xSchedule`, `C6xSched.h:10`) with its software pipeliner (`C6xPipe.cpp`,
`C6xSched.cpp:2283`). tms6747 alone still uses the `Ast.h` `Inliner`
(`Tms6747.cpp:1792`), and divides by a constant with `DivMagic.h`'s numbers
(`Tms6747.cpp:1387,1397`). Under `-dump=ssa` or `-check=ssa` both are lowered
too, but nothing is selected from it (`Driver.cpp:1599`).

## The SSA passes

`SsaOptimize.cpp:14-30`. A pass marked *again* runs only if an earlier pass
changed something (`SsaOptimize.cpp:107`).

| pass | level | file | what |
|---|---|---|---|
| promote | 1 | `SsaPromote.cpp` | mem2reg; incoming values as jump arguments, never a phi |
| fold | 1 | `SsaFold.cpp` | constants, copies, algebra, at the selector's width |
| jumps | 1 | `SsaJumps.cpp` | jump threading into a block that only tests |
| cfg | 1 | `SsaCfg.cpp` | constant branches, unreached and empty blocks; pads fixed |
| gvn | 1 | `SsaValues.cpp` | value numbering, loads forwarded, decided branches |
| divide | 2 | `SsaDivide.cpp` | division by a constant, `DivMagic.h`'s numbers |
| loops | 2 | `SsaLoops.cpp` | preheaders, invariants hoisted, a counter carried at 64 bits |
| unroll | 2 | `SsaUnroll.cpp` | a constant-counted innermost loop, whole or 4 or 2 times |
| rotate | 2 | `SsaLoops.cpp:613` | a loop's head test moved to its latch |
| dead | 1 | `SsaDead.cpp` | dead code and dead stores |
| refold, rejumps, recfg, regvn, redead | 1, again | as above | |

`SsaDominators.cpp` is the one dominator tree, the verifier's and mem2reg's.

## The MIR pipeline

`OptPipeline.cpp:226-250`, in order; the code is the authority.

    rounds            forward-values, remove-unreachable, remove-dead,
                      coalesce-copies, fold-loads, fold-offsets, thread-jumps
                      (:194-203); repeated until one finds nothing
    frame             while the prologue is held (:184)
      webs            every register web a pseudo, pinned occurrences split off
      locals          every promotable scalar local a pseudo
      allocate        Chaitin-Briggs over register classes (MirAlloc.h)
      rounds          if a local was promoted or a register saved (:190)
      dse-loop        remove-dead-stores, then rounds; up to three times
    fold-index        an index register into the memory operand
    hoist-invariants  (-O2, :57) after allocation, into a register the loop leaves free
    hoist-xmm-slots   (-O2, :64) a double the loop only reads, in a free xmm register
    dse-late          remove-dead-stores and rounds once more (:242)
    finish-frame      unused saves dropped, the values' slots closed up, the prologue rewritten
    shrink-loop       shrink, then rounds; up to three times
    align-loops       (-O2, :172) a loop of one 64-byte line kept inside one

A function whose funclets' blocks are in the stream is allocated whole: a funclet
saves no register, and a value live into one stays in its frame slot
(`Optimizer.cpp:191-193`, `MirAlloc.cpp:182-193`).

## The levels

cxx1's levels mean what cl's do: -O1 is small code, -O2 is fast code. A pass asks
the level's `Costs`, never the level (`OptCosts.h:3-5`).

| | -O1 (size) | -O2 (speed) |
|---|---|---|
| SSA: promote, fold, jumps, cfg, gvn, dead | yes | yes |
| SSA: divide, loops, unroll, rotate | no | yes |
| MIR: rounds, webs, locals, allocate, dse, fold-index, shrink | yes | yes |
| register allocation | size costs | speed costs, loop-weighted |
| inlining | none (`OptCosts.h:68-71`) | budgets per site, caller and unit (`OptCosts.h:92-98`) |
| MIR: hoist-invariants, hoist-xmm-slots, align-loops | no | yes |
| PRE | no | no |

**A `volatile` access is kept at every level on x86** (R8, 2026-11-13): the SSA passes see
each access marked (`Inst::isVolatile`), and the selector marks the MIR instruction of each
volatile load and store (`MInst::vol`, `-dump=mir` prints `; volatile`); forward-values,
remove-dead, coalesce-copies, fold-loads, locals, remove-dead-stores, hoist-xmm-slots and
shrink leave a marked access as it is, and `tests/ssa.sh` holds, case by case, that the
accesses `-check=ssa` kept are the lines MIR marked. **arm64 and the C6000 have no mark**:
there a file that writes `volatile` is compiled without -O, said in a warning
(`Driver.cpp:1586-1597`).

## What M3b removed, said once

- **The `Ast.h` path on x86** (M3b.7): the walker fallback and `CXX1_SSA_STRICT`
  (deletion 1); `X86_64Linux` left `Walker` and `Visitor` for `Regions`, with its
  `visit`s, `genTruth`, `walkInPlace` and `inlineTarget`; then the last two reads
  of the parser's tree, the funclet frame's inline reserve and the shadow area
  sized from `Ast.h` calls, now sized from the `ir::Function`'s calls
  (deletion 3, `f231c040`).
- **`-front=ssa` and `-front=typed`**: refused by name, "the SSA lowering is the
  only path on x86" (`Driver.cpp:1138-1140`); with them the adapter
  (`Adapter.*`), `-check=adapted` and `AstDump.*` (M3b.7 round 1).
- **Two MIR passes only the `Ast.h` path fed** (deletion 2): divide-by-constant,
  which SSA's `divide` does (`OptDivide.cpp` keeps only the magic numbers,
  `OptDivide.cpp:7,28,44`), and widen-counters, which SSA's `loops` does.
- **`fromSsa`** on `MFunction` and `Optimizer`, read by nothing once every x86
  function came from SSA (deletion 3).

## The 2026-09-23 review's four prerequisites, where they stand

Fable 5.1 reviewed branch `opt` from `02c665b` and named four things the
allocator could not be built without. All four are in the tree:

1. **A throwing call's edge to its landing pad**: the flow is built over the
   region each pad covers (`OptCore.h:46-50`, `OptFunction.h:88`).
2. **Pin an occurrence, not a web**: `mir::Webs::splitPinned` (`MirWebs.h:20`).
3. **One opcode table**: `OptTable.h:276`.
4. **A cost for the inliner**: `Inliner`'s three budgets from `OptCosts.h`; -O1
   inlines nothing until a cost in bytes can tell a body smaller than its call
   (`OptCosts.h:68-71`).

The funclet question it left open is settled as above: the whole function is
allocated with its funclets in place, and what a funclet touches stays in memory.

## What is not done

- **arm64-darwin on the IRs** (the 2026-09 plan's stage 5): it still optimizes
  its own text, as tms6747 does.
- **PRE**, which the 2026-09 plan put at -O2.
- **x87 in MIR**: opaque, as above.
