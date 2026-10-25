# M3's two IRs: MIR below the walkers, SSA above them

Written 2026-10-25 by Fable 5.1 as review point 1's deliverable, beside `M3-PLAN.md`, from `7d593d5`. Part 2 is
the MIR M3a puts below the existing walkers and is **fixed at review point 2a**, after M3a.1 has built it and
the round trip has been measured; M4's encoder is written against it from then on. Part 3 is the SSA IR M3b
lowers the typed tree into and is **fixed at review point 3**. Anything marked *open* is a decision deferred to
the review point named; everything else is decided here, and Opus may build M3a.1 from part 2 without another
design question. Where this document and the code disagree, the code at `7d593d5` is what was read, and the
line is cited so the disagreement can be settled by reading it again.

## 1. Where each IR sits

```
typed tree  →  Lowering  →  SSA IR  →  SSA passes (-O1, -O2)  →  Selection  →  MIR  →  MIR passes, allocation  →  Spelling  →  text
(M2, sema)     (M3b)        (M3b)       (M3b.4, M3b.5)          (M3b: the      (M3a)   (M3a: today's pipeline     (GNU, COFF,
                                                                 x86 walker            on MInst)                   MASM; today's)
                                                                 re-pointed)
```

Until M3b the walkers stand where "Lowering → SSA → Selection" will: they read `Ast.h` and write `Spelling::ins`
calls, which M3a holds as MIR. GCC's shape: the SSA IR is GIMPLE's place (target-neutral, typed values, where the
scalar passes run), MIR is RTL's (the target's own instructions over pseudo registers, where allocation and the
late cleanups run). Both IRs are handle-based arenas (design section 2); both have a dump (`-dump=mir`,
`-dump=ssa`) and a check (`-check=mir`, `-check=ssa`).

## 2. MIR - M3a

### 2.1 What exists, and what becomes of each file

The x86 optimizer already works on a parsed form and not on text: the walker's `Spelling::ins(m, a, b)` call is
held as an `opt::Instr{std::string m; Operand a, b; int operands; RegSet args; bool exactArgs}` (`OptIr.h:65-78`),
its operands parsed once from the `Op` the walker built (`Operand::from`, `OptIr.cpp:49-88`) and written back by
`Operand::op()` (`:114-132`); a label or a replayed spelling call is an `Entry` beside it (`OptCore.h:55-65`,
`OptIr.h:82-83`). The flow graph, liveness, dominators, loops, webs, promotable locals and the allocator are
built on that. MIR is this with four things made data that are not: the mnemonic (a string, looked up in
`OptTable.cpp`'s table and in a dozen `m == "..."` tests), the events (closures), the register (a number with no
class), and the -O0 path (none - the holder exists only at -O1 and -O2, `X86_64Linux.cpp:1560-1566`).

| file | at `7d593d5` | in M3a |
| --- | --- | --- |
| `OptIr.h`, `OptIr.cpp` | `Reg`, `Operand`, `Instr`, `Entry`, `Stream`; `parseReg`, `regName` | **retired at M3a.5**; their content moves to `Mir.h` as `MOperand`, `MInst`, `MEntry`, `MStream` (2.2-2.4) |
| `OptTable.h`, `OptTable.cpp` | `Opcode{name, kind, flags, width}`, ~105 rows, four prefix families, `isMovQ` and friends, `conditionOf` | **kept and grown**: the one table (2.6); the string API goes at M3a.3 |
| `OptCore.h` | `RegSet`, `kPhysical = 64`, `Effects`, `Control`, `Region`, `EntryOf`, `Edge` (with `Eh`), `Live`, `Block`, `FlowOf`, `removeDeadIn`, `removeUnreachableIn` | **kept**; `EntryOf`'s payload becomes `MDirective` (2.4) |
| `OptFunction.h` | `opt::Function`: stream, convention, flow, props, homes, slots, temporaries, locals, shared, regions, frame | **kept as `MFunction`** (2.5); `homes`+`slots` become `pseudos` at M3a.2; funclet blocks at M3a.4 |
| `OptEffects.*`, `OptFlow.*` | `effectsOf(Instr, Convention)`, `rolesOf`, `controlOf`, `Flow::build` | **kept**, reading the opcode row instead of the mnemonic |
| `Mir.h`, `MirWebs.cpp`, `MirLocals.*`, `MirAlloc.*` | `Webs` (split pinned occurrences, build, assign), `Locals::promote`, `Allocator` (Chaitin-Briggs over `kOffer`'s 14 general registers, `MirAlloc.cpp:40`) | **kept and generalised** to register classes at M3a.2; `Mir.h` becomes the IR's one header |
| `OptPass.*`, `OptPipeline.*`, `OptCosts.h`, `OptDeclines.h`, `OptLoops.*`, `OptDataflow.*` | the manager, the pipeline declaration, the levels, the declines | **kept**; `dumpStream` becomes `-dump=mir`'s writer |
| `OptValues`, `OptDead`, `OptJumps`, `OptMemory`, `OptFrame`, `OptHoist`, `OptDivide`, `OptShrink`, `OptAlign` | the passes, by mnemonic | **kept**, re-keyed to the table at M3a.3 |
| `Optimizer.h/.cpp` | the `Spelling` in front of the spelling that holds, improves and replays | **kept**, created at every level (M3a.1); `funcletBegin/End` go at M3a.4 |
| `Inliner.*` | over `Ast.h` | **untouched in M3a**; re-pointed to `ir::Function` at M3b.5 |
| `A64Peep.*`, `C6xSched.*`, `C6xPipe.*`, `C6xModel.h`, `DivMagic.h` | arm64's text peephole, the C6000's text passes, the shared magic numbers | **untouched** (exceptions 1 and 2; `DivMagic.h` shared with M3b.4) |

### 2.2 `MOperand`

The operand kinds are the `Op` kinds the walker builds (`Spelling.h:41-54`), one to one, so the round trip is a
copy and not a translation:

```cpp
namespace mir {

// A register: a physical one (0..63 as OptIr.h numbers them: RAX..R15, xmm0-15 as 16-31) or a pseudo (>= 64).
struct Reg {
    int id = -1;         // -1: a register this model does not hold (the x87 stack, kept by its text)
    int width = 0;       // the bytes the spelling named: 8, 4, 2, 1; 16 for an xmm
};

struct MOperand {
    enum Kind : std::uint8_t { None, Register, Immediate, Memory, RipSymbol, Indirect, Label, SegMemory, TlsSymbol };
    Kind kind = None;
    Reg reg;             // Register; Indirect's target; Memory's and TlsSymbol's base
    Reg index;           // Memory: `disp(base,index,scale)`; scale 0 is none
    int scale = 0;
    long long disp = 0;  // Memory, SegMemory
    bool hasDisp = false;
    long long value = 0; // Immediate, when numeric
    bool numeric = false;
    StrId text;          // an Immediate that is not numeric, a symbol, a label, an x87 register's spelling
    bool unsignedSpelling = false;   // an imm(unsigned) past LLONG_MAX, spelled back as written; gone at M3a.3
};
```

This is `opt::Operand` (`OptIr.h:38-63`) with one change: `text` is a `StrId` into the process's registry
(`OptStrings.h`), written under a lock and read lock-free, rather than a `std::string`, so an `MOperand` is a plain
value a vector copies cheaply; an index never reaches a dump or an output, which print the text. *(Amended at RP2a,
2026-10-25: this said `StringId` into the function's string table.)* `unsignedSpelling` keeps the walker's
`imm(unsigned long long)` past `LLONG_MAX` spelled as written at -O0; `improve` clears it before the passes so -O1
and -O2 write what they always did, and M3a.3 folds it into the table's widths or keeps it at every level.
`Operand::from(const Op &)` and `Operand::op()` are kept as `MOperand::from` and `MOperand::op`, byte for byte
in behaviour: the immediate normalisation (`immText("8")` read as the number 8 when it spells back the same,
`OptIr.cpp:63-67`), the register name canonicalised through `regName`, an x87 `%st(0)` kept as text with
`reg.id < 0`, `Op::Seg` and `Op::Tls` passed through.

**Pseudo registers** are numbered from `kPhysical` (64) upward, as today (`Mir.h:13`); a `RegSet` holds physical
registers only. A pseudo's class, home and slot live in `MFunction::pseudos` (2.5), indexed by `id - 64`.

### 2.3 `MInst`

```cpp
struct MInst {
    OpId op;             // an index into the opcode table (2.6); never a string
    std::uint8_t operands = 0;   // 0, 1 or 2
    MOperand a, b;       // AT&T order: a the source, b the destination
    RegSet args = 0;     // a call's argument registers as the walker placed them (callArguments)
    bool exactArgs = false;
};
```

What `Instr` carries today, with `m` replaced by `OpId`. The width the suffix names, the kind, the flags, the
roles and the implicit registers are all the row's and not the instruction's, so an `MInst` is 40 bytes of
plain data. A mnemonic the table does not hold is interned as an unlisted `OpId` whose row is its prefix
family's or `kUnknown` *(amended at RP2a, 2026-10-25: this said it "cannot be built")*, and the passes treat it as
opaque; M3a.1 keeps that row *and* counts it, so that
`CPP11_MIR_STRICT=1` can refuse the function naming the mnemonic - which is how the table's completeness is
measured over the corpus (M3-PLAN, M3a.1's gate) and how M4's mnemonic list is taken from the oracle rather
than from the source (design section 6).

### 2.4 `MEntry` and the directives

```cpp
struct MEntry {
    enum Kind : std::uint8_t { Inst, Label, Directive };
    Kind kind;
    MInst ins;                 // Inst
    std::string label;         // Label
    bool state = false;        // Label: a Microsoft state label (Spelling::stateLabel)
    MDirective dir;            // Directive; its kind Deferred holds a closure - M3a.1 only, gone at M3a.4
    bool dead = false;
};
```

**A directive is a `Spelling` call that is not an instruction, held as data.** Today each is a closure
(`Optimizer.cpp:231-280`) that cannot be dumped, compared or reordered by a pass that sees only "<event>"
(`OptPass.cpp:150`). The kinds are exactly the `Spelling` virtuals the `Optimizer` overrides, and the replay is a
`switch` that makes the same call with the same arguments:

| `MDirective::Kind` | arguments | from |
| --- | --- | --- |
| `Prologue` | `frameSize, lsda, outgoing, realign`, and the `saves` `FinishFrame` adds (`OptPipeline.cpp:162`) | `Spelling::prologue`, `calleeSaves` |
| `FileEntry` | `n, name` | `fileEntry` |
| `Location` | `file, line, column` | `location` |
| `FuncletLinesBegin`, `FuncletLinesEnd` | - | `funcletLinesBegin/End` |
| `Globl`, `WeakDefinition`, `ObjectType` | `name` | |
| `ObjectSize` | `name, size` | |
| `TextSection`, `RodataSection`, `DataSection`, `BssSection`, `TlsDataSection`, `TlsBssSection` | - | |
| `TlsInitialiserEntry`, `InitialiserEntry` | `fn`; `fn, dsoHandle` | |
| `Align`, `LoopAlign`, `Zero` | `n`; `bytes`; `n` | `OptAlign.cpp:138-140` makes `LoopAlign` today as a closure |
| `DataInt`, `DataSym`, `DataBytes` | `size, v`; `sym, off`; `bytes` | |
| `NoteHasEh` | `yes` | |
| `RelroSection` | - | `relroSection`: a vtable on a PIE target (b9628c6); added at RP2a, 2026-10-25 |
| `Deferred` | a closure | the five text-lifting `atOutput` calls below, M3a.1 to M3a.4 |
| `FuncletBegin`, `FuncletEnd`, `FuncletLeave` | `symbol, cleanup, pos`; `resume`; `label` | **M3a.4**: the funclet's head and tail, written by the emitter in place of `closeFunclet`'s lifted text |

`Deferred` is a directive kind, not an `MEntry` kind, and a directive's string argument (an LSDA name, a
`DataBytes` payload) is a `std::string`, there being nothing to gain from interning one *(amended at RP2a,
2026-10-25: the struct above had `Deferred` beside `Directive` and the string as a `StringId`)*. It holds the
five `atOutput` closures that cut text out of `out_` and move it (`X86_64Linux.cpp:2017,
2152, 2544`; `Masm.cpp:891, 977`). They are the one thing in the stream that is not data at M3a.1, they are
named here so that the count is known to be five, and M3a.4 removes them with the funclet directives above; the
`emitTlsWrappers` one (`:2544`) is a raw-text emission after the functions and becomes a `DataBytes`-shaped
directive at the same step.

**The prologue directive carries `realign`.** Today the `Optimizer` asserts it is 0 (`Optimizer.cpp:233`) and a
realigned frame is written past the holder (`X86_64Linux.cpp:1604-1608`). At -O0 the holder sees every function,
so the directive carries it and the passes decline such a function as they do now (`realigned-frame-no-optimizer`);
M3a.2 keeps rbx out of its palette there.

### 2.5 `MFunction`

`opt::Function` (`OptFunction.h:28-88`) as it stands - `name`, `stream`, `convention`, `flow`, `props`, the
temporaries' region (`tempFrom`, `tempBase`, `tempCount`), `locals`, `shared`, `whole`, `jumpOnly`, `regions`,
the frame (`prologueAt`, `frameSize`, `inlineTop`, `lsda`, `outgoing`, `saves`, `size`) - with two additions:

```cpp
struct PseudoInfo {        // M3a.2: one record per pseudo, replacing homes[] + slots[]
    RegClass cls;          // Gpr or Xmm (2.8)
    int home = -1;         // a web's: the physical register it was found in; -1 for a promoted local
    Local slot;            // a promoted local's frame slot, to give back
    int width = 8;
};
std::vector<PseudoInfo> pseudos;      // indexed by id - kPhysical
std::vector<FuncletRange> funclets;   // M3a.4: [begin, end) of the stream, each a funclet's blocks, with its symbol and kind
```

*(Amended at RP2a, 2026-10-25: a per-function `StringTable strings` stood here; `StrId` is process-wide, 2.2,
and a dump is complete because it prints the text.)* A funclet range is a slice of the parent's stream - the
parent and its funclets are one `MFunction` from M3a.4, which is what lets the allocator see the whole (P1's
funclet decision) - and emission writes each range to `.text$x` after the function, with its own prologue and
unwind data, exactly the text `closeFunclet` composes today (`X86_64Linux.cpp:2083-2168`).

### 2.6 The opcode table

One table, `OptTable.cpp`'s grown, keyed by `OpId` (its row index) and looked up by AT&T name only at the two
doors where a string arrives: `Optimizer::instruction` (the walker's `ins`) and the table-check tool. Every
constraint the allocator honours is a column, which is P1's third prerequisite and `OPTIMIZER-IR.md`'s "about a
dozen mnemonic lists become one table with flags":

| column | what it says | today |
| --- | --- | --- |
| `att` | the mnemonic as the GNU spelling writes it, `"movslq"` | `Opcode::name` |
| `masm` | the mnemonic as MASM writes it, `"movsxd"`, and the width its `PTR` needs | `MasmSpelling::kRules` (`Masm.cpp:109`, 156 rows) - merged at M3a.3 so the two cannot drift |
| `kind` | how `effectsOf` reads it: `Call, Ret, Jmp, Jcc, Setcc, Push, Pop, Leave, SignExtendAx, ExtendAx, StringMove, Div, Compare, Unary, Move, Rmw, X87, Unknown` | `Opcode::Kind` |
| `flags` | `kMergeXmm, kNoFlags, kExplicit, kWritesOnly, kImmSource, kRenamable, kShift, kZeroIdiom, kAddress, kCvt` | `Opcode::flags` |
| `width` | the bytes the suffix names, 0 where only a register can | `Opcode::width` |
| `roles` | what the instruction does with `a` and with `b`: read, write, keep (written in part), address | `rolesOf` (`OptEffects.cpp`), computed from kind today; a column from M3a.3 |
| `implicit` | registers read and written without being named: `cqo` rdx:rax, `idiv` rdx:rax, `rep movsq` rcx/rsi/rdi, a shift's count in `%cl`, `call` by the convention | spread through `effectsOf`'s `switch` |
| `classes` | which operand kinds and register classes each operand may take: `a: Gpr|Imm|Mem`, `b: Gpr|Mem`; `movsd`: `a: Xmm|Mem, b: Xmm|Mem`; `movd`/`movq` between files | **new** at M3a.2: what an allocator giving a pseudo a class must know, and what today's `candidate()` (general registers only, `MirWebs.cpp:15`) hard-codes |
| `fixed` | an operand pinned to one register: the shift count to `%cl`; `Div`'s dividend | `Webs::occurrencesOf`'s `isShift` (`MirWebs.cpp:68`) |

The rows: the ~105 of `OptTable.cpp:16-126`, plus every mnemonic the walker writes that the prefix families
catch today - the 73 literal mnemonics in `X86_64Linux.cpp` (`grep -o 'ins("[a-z0-9 ]*"'`) and the composed ones
at its 20 `ins(sign ? ... : ...)`, `ins(op, ...)`, `ins(std::string("add") + sfx, ...)` sites: `movsbq/movswq/
movzbq/movzwq` by size, `sar/shr` by sign, `idiv/div`, `add/sub/mul/div` + `ss/sd`, `ucomiss/ucomisd`, the
`cvt*` family, `set*` and `j*` by condition, the x87 `f*` (`fld, fldl, flds, fldt, fldz, fld1, fstp, fstpl,
fstps, fstpt, fildq, fistpq, faddp, fsubp, fmulp, fdivp, fchs, fxch, fucomip, fldcw, fnstcw`). The four prefix
families (`OptTable.cpp:129-134`) stay as the fallback that marks a row `Unknown` for the strict check to count;
M3a.1's gate is that the count is zero over the corpus and Compiler++ on both x86 targets at every level.

**The table is read-only once initialised** (C7, the WS-F race at `OptTable.cpp:150-151`): its rows are built once,
before any job reads them, and no pass writes one. What grows at run time is the registry an `OpId` indexes - a
mnemonic no row lists is interned once, under a lock, and published by address after its record is complete - so a
reader takes no lock and never sees a record half made. `StrId`'s registry is the same shape (M3a.1).

**The quirk rows are kept at M3a.1 and settled at M3a.3**, one commit each, because each changes what is emitted
and `identical.sh` must name the files: `addq`/`subq` (`Unknown` with a width, `OptTable.cpp:119-120`), `sal`,
`rol`, `ror` (shifts by flag, opaque by kind), `testb/testl/testq` (not `kExplicit` where `test` is).

### 2.7 The round trip, byte for byte

What the gate asks is that a function held as MIR and replayed produces the bytes the walker's direct calls would
have, at every level, for both spellings. It holds today at -O1 and -O2 by construction - the `Optimizer` is in
front of the spelling and the passes' output is what is written - and it holds at -O0 for the one reason that
there is no holder there. M3a.1 makes it hold at -O0 the same way: the holder is created at every level
(`X86_64Linux::setOptimize`, `X86_64Linux.cpp:1560`, no longer returns for `level <= 0`), `improve` runs no pass
at level 0 (`Costs::forLevel(0)` answers a level whose pipeline is empty, or `flush` skips `improve` - Opus's
choice, said in the commit), and `flush` replays. The replay is `Optimizer::replay` (`Optimizer.cpp:211-229`)
with the `switch` over directives in place of `e.event(under_)`.

What byte-identity rests on, so that each is kept:

1. **`MOperand::op()` is `Operand::op()`**: a register spelled through `regName(id, width)` - which is why
   `Reg::width` is kept exactly as the walker named it; a numeric immediate through `imm(value)`, a textual one
   through `immText`; a memory operand with `hasDisp` deciding whether `0(%rbp)` or `(%rbp)` is written (the two
   differ in bytes and the walker writes both); `scale == 0` meaning no index.
2. **A label is replayed as `defLabel` or `stateLabel`** by its mark (`Optimizer.cpp:222-224`): the MASM spelling
   pads a state label after a call (`stateLabel`, `Spelling.h:131`), so the mark must survive.
3. **A directive's arguments are copied whole** at the time of the call, as the closures capture them by value
   today; a `std::string` argument is copied whole, and an operand's text is a `StrId` that comes back as the
   same string *(amended at RP2a, 2026-10-25)*. An immediate's `unsignedSpelling` (2.2) is the one field the
   passes' levels clear.
4. **The `Deferred` closures run where they stood** (`event` semantics), until M3a.4.
5. **The -O0 walker's behaviour does not change**: `optimizing()` (`X86_64Linux.h:158`) is renamed to say what it
   means - `passesRun()`, true at -O1 and -O2 - and every site that reads it (`tempsInSlots`, `copiesByString`,
   `callArguments`, `jumpOnly`, `exceptionRegion`, `frame`, `temporaries`, `inlineBegin`, `sharedSlot`,
   `returnsPair`, `funcletBegin`, `finish`) keeps asking that question. The holder being in front is a different
   fact, `a_ == holder`, and only `emittedSize()` (`:133`) and `atOutput` need it.

**`-check=mir`** is the in-compiler form of the gate, the device `-check=adapted` uses: the driver generates the
unit twice, once with the holder bypassed (`-mir=off`, a flag kept through M3a and removed at M3a.5) and once
through it, and compares the two texts; `-check=mir: functions=N same=N differ=0` on stdout, the first differing
function named with its first differing line. `identical.sh` against the base binary is the external form; both
are run, since the second also catches a change to the walker.

### 2.8 Register classes and the target description - M3a.2

```cpp
enum class RegClass : std::uint8_t { Gpr, Xmm, X87 };    // X87: never allocated; its operands are opaque text

struct TargetDesc {
    RegSet classMembers[3];       // Gpr: the 16 general registers less rsp and rbp; Xmm: 16-31; X87: none
    Convention convention;        // arguments, clobbered, returned, preserved - conventionOf(abi) today (OptEffects.cpp)
    RegSet frame;                 // rsp, rbp; rbx too while a frame is realigned
    int offer[16]; int offered;   // the order registers are offered in: kOffer's today (MirAlloc.cpp:40)
    const char *scratch;          // the ABI's scratch (Abi::scratch), which the walker borrows and the passes leave
};
```

Both x86 targets share one `TargetDesc` but for the convention, which `conventionOf(const Abi &)` already reads
from the ABI table (`Abi.h`: `intRegs`, `sseRegs`, `preservedRegs`). The allocator's palette becomes per class
(`Allocator::colour`, today `kOffer` filtered by `candidate` and `preserved`); a pseudo's class comes from the
operand it was made from (`Reg::id` in 16-31 → `Xmm`) and from the row's `classes` column where an operand is a
memory slot being promoted (a floating `Local` promotes to an `Xmm` pseudo: `Local::floating`, `OptPasses.h:50`,
stops meaning "never promoted"). Two classes cannot interfere, so the interference matrix is built per class and
the two colourings are independent; a copy between files (`movd`, `movq` xmm↔gpr, `cvt*`) is not a coalescing
candidate. The webs' `candidate()` (`MirWebs.cpp:15`) takes any allocatable class.

### 2.9 Flow and exceptions in MIR

Nothing changes in the model: a region (`Region{begin, end, target}`, `OptCore.h:49-51`) is told by the walker,
`Flow::build` gives every call under one an `Eh` edge to its pad's block (`OptCore.h:180-182, 213-218`), the call
stays inside its block (`:69`), and liveness joins the pad's live-in at the call (`joinPads`). P1's four
prerequisites: the edge exists; pins are on an occurrence (`Webs::splitPinned`); the table is M3a.1/3; the inliner
has a budget (`OptCosts.h:33-41`).

**Funclets (M3a.4).** P1's decision: *allocate the whole function before funclets are outlined, and keep any value
live into a funclet in a frame slot.* In MIR that is: a funclet is a `FuncletRange` of its parent's stream, its
code blocks of the parent's flow graph reached by no edge from the body (the runtime calls it; its entry is a
`FuncletBegin` directive, its exits `FuncletEnd`/`FuncletLeave`, which `controlOf` reads as a return); the
allocator colours the whole; a frame slot a funclet reads or writes is in `SharedSlots` (`OptPasses.h:60`) as
today, so promotion leaves it and dead-store removal keeps its stores; a funclet's own registers are its own
prologue's concern and it saves none (it is `push %rbp; sub $32, %rsp; mov %rdx, %rbp` and back,
`X86_64Linux.cpp:2105-2113`), so a pseudo whose live range crosses a funclet boundary is not allowed - the
walker's own code never produces one (a funclet body begins from the frame), and the verifier of `-check=mir`
asserts it. *Open for RP2b*: whether a cleanup funclet that runs a destructor may take a callee-saved register
the parent saved - cl does not (its funclets are leaf-shaped), and the first version follows cl.

### 2.10 `-dump=mir`

`dumpStream` (`OptPass.cpp:122`) as it stands - blocks with their edges, live-in and live-out, loop depth, each
entry numbered, a dead entry marked `-` - with a directive printed by kind and arguments in place of `<event>`
and a pseudo printed `%p<n>:<class>`. `-dump=mir` writes every function after the pipeline; `-dump=mir=<pass>`
after the named pass, which is `CPP11_DUMP_MIR` today (`OptPass.h:64`), retired in M3a.5. Two runs give the same
bytes (`tools/determinism.sh` runs it).

### 2.11 Open at review point 2

- the x87 stack: operands with `reg.id < 0` stay opaque text and the `X87` class is never allocated; x86_64-linux's
  `long double` is the only user. Not modelled in M3; said in `OPTIMIZER-IR.md` at M3a.5.
- whether `MasmSpelling`'s width-inference rules (`ptrFor`, the `movq`-with-xmm rename, `Masm.cpp:287-305`) move
  into the `masm` column whole or stay beside it - decided at M3a.3 by which leaves `identical.sh` at 0.
- the cleanup-funclet register question above (RP2b).
- the arm64 and C6000 descriptions are not written: exceptions 1 and 2.

## 3. The SSA IR - M3b

### 3.1 Data model

`src/ir/Ir.h`, namespace `ir`. One arena per function for instructions, one for blocks, one for values; one
module per translation unit for functions. Handles are `std::uint32_t` wrapped one type apiece - `ValueId`,
`BlockId`, `InstId`, `FuncId` - with 0 as none, numbered in creation order, so two runs make the same IR and
stage 2 = stage 3 has what it needs (design section 2). A node is `{kind, operands range, payload}` and every
reader is a `switch` on kind. **Types are `const Type *`** from the front end's `TypeTable`, which interns them
for the unit (`Type.h`, "Surviving cpp11 modules keep their shape: the TypeTable", section 2): the design's
`TypeId` is that pointer, and a wrapper buys nothing while the table is pointer-interned. A value's type is the
front end's type - `int`, `unsigned char`, `double`, `bool`, a pointer, an enumeration - sized by the `Target`;
there is no separate `i32`/`i64` alphabet.

```cpp
struct Value { const Type *type; InstId def; BlockId param; std::uint16_t index; };   // defined by an instruction, or block param i
struct Inst  { Op op; const Type *type; std::uint32_t first, count;   // operands in Function::operands
               Payload payload; syntax::NodeId at; };                 // the syntax node, for diagnostics and -g
struct Block { std::vector<ValueId> params; std::vector<InstId> insts; InstId term; bool pad; };
struct Function {
    std::string symbol, alias, displayName; bool isStatic, isInline, variadic, hasThis, noexcept_;
    const Type *returns; std::vector<ValueId> params; ValueId sret;      // the hidden result pointer, where the ABI gives one
    std::vector<AllocaInfo> frame;                                       // every alloca: type, align, syntax var
    BlockId entry; std::vector<BlockId> blocks;                          // entry first; the rest in creation order
    std::vector<std::string> spec;                                       // throw(T...)'s type_info symbols, as Ast.h's Function carries them
    std::string synthesised; int frameAlign; std::size_t pos; ...
};
```

**No first-class aggregates.** A class or array value lives in memory - an `alloca`, a global, a `sret` object -
and is moved by `memcpy`; a function returning a class through a hidden pointer has `sret` as a parameter and
`ret` carries nothing. The walkers already treat a class value this way (`X86_64Linux::classifyEightbytes`,
`placeArguments`, `msAggregateToRax` read the *type*, not a value), so selection keeps every ABI decision where
it is. A scalar is the only thing that becomes an SSA value, which is also what mem2reg promotes.

### 3.2 Instruction set

Three-address, typed by the result; `T` is the result type unless said.

| group | instruction | operands → result | notes |
| --- | --- | --- | --- |
| constants | `iconst T, k` | → T | integer, enumeration, bool, pointer-as-integer |
| | `fconst T, bits` | → T | the IEEE bits, never the host's double: the host-rounding faults of C-03 |
| | `null T` | → pointer | |
| | `addr @sym [+off]` | → pointer | a global, function or string literal by symbol |
| | `tlsaddr @sym` | → pointer | a `thread_local`'s address; selection writes the ABI's walk (`tlsAddress`) |
| | `undef T` | → T | an indeterminate value (an uninitialised scalar read) |
| memory | `alloca T, align` | → pointer | a frame object; in the entry block only |
| | `load T, p [volatile]` | → T | the width is T's |
| | `store p, v [volatile]` | | |
| | `memcpy dst, src, n` / `memset dst, byte, n` | | n a constant |
| | `gep p, off` | → pointer | a byte offset, constant or value: the typed tree's `pointerAdd` already scaled by the element (`Adapter.cpp`'s shape), so the IR does not scale |
| integer | `add sub mul sdiv udiv srem urem and or xor shl lshr ashr` | T, T → T | two's-complement wrap; a shift count `& (bits-1)` is the target's |
| | `neg not` | T → T | |
| | `icmp cc, a, b` | T, T → bool | `eq ne slt sle sgt sge ult ule ugt uge` |
| floating | `fadd fsub fmul fdiv fneg` | | `float`, `double`, `long double` (x87 on linux: selection's) |
| | `fcmp cc, a, b` | → bool | ordered and unordered forms, as `ucomisd` + `setp` need |
| conversions | `trunc sext zext fptosi fptoui sitofp uitofp fpext fptrunc bitcast ptrtoint inttoptr` | T → U | `tobool` is `icmp ne v, 0`, which is the parser's lowering already |
| control | `br ^B(args)` | | |
| | `condbr c, ^B1(args), ^B2(args)` | | |
| | `switch v, ^default(args), [k → ^Bk(args)]...` | | |
| | `ret v` / `ret` | | |
| | `unreachable` | | after `__cxa_throw`, `abort` |
| calls | `call f(args) : T` | → T or none | `CallInfo` payload (3.5) |
| | `invoke f(args) : T to ^normal(args) unwind ^pad` | → T | the only instruction with an unwind edge |
| exceptions | `catchswitch [parent] handlers ^h1 ... ^hn [unwind ^outer]` | → token | the `try`'s dispatch; `^hi` begins with `catchpad` |
| | `catchpad %cs, @type_info, slot, flags` | → token | one handler's entry; `@type_info` empty for `catch (...)`; flags: by reference, const pointer |
| | `cleanuppad [parent]` | → token | a destructor region's entry |
| | `catchret %pad, ^resume` | | the handler done; where the function carries on |
| | `cleanupret %pad unwind ^outer` / `unwind to caller` | | |
| | `rethrow` | | `__cxa_rethrow` / `_CxxThrowException(0, 0)` |
| misc | `select c, a, b` | → T | a scalar `?:` whose arms have no side effects |
| | `va_start p` / `va_arg p : T` / `va_end p` | | selection's ABI code (`VaStart`, `VaArg` visitors) |
| | `trap` | | `ud2` / `int3`: not used by the lowering today, kept for `__builtin_trap` |

**What is deliberately absent**: `phi` (block arguments instead), first-class aggregates, `nsw`/`nuw` flags
(wrap semantics are what the targets do, and the constexpr evaluator rejects UB on its own ground, R4.5), inline
assembly (cxx3 has none), `landingpad` (one EH model, 3.6).

### 3.3 Example

```
function @_Z3sumPii(%a: int *, %n: int) -> int {
^entry:
  br ^loop(iconst int 0, iconst int 0)
^loop(%i: int, %s: int):
  %c = icmp slt %i, %n
  condbr %c, ^body, ^exit
^body:
  %p = gep %a, mul %i, iconst long 4
  %v = load int, %p
  %s1 = add %s, %v
  %i1 = add %i, iconst int 1
  br ^loop(%i1, %s1)
^exit:
  ret %s
}
```

(The dump writes one instruction a line and names constants inline where they are used once; `-dump=ssa` is the
authority for the format, and its two runs must be the same bytes.)

### 3.4 Blocks and block arguments

A block has parameters, typed; a branch to it supplies one argument per parameter. There is no phi. SSA
construction is design section 4's: lowering gives every local an `alloca`, and `mem2reg` (M3b.4) promotes the
allocas whose address is never taken, writing the value a slot holds at the end of each predecessor as that
predecessor's branch argument into the merge block - the renaming is done over the dominator tree, and what
would be a phi is the merge block's parameter. The verifier (`-check=ssa`) holds: every use is dominated by its
definition; a block's parameter count and types match every branch into it; one terminator a block; a pad block
begins with its pad instruction and is reached only by unwind edges and `catchswitch`.

### 3.5 Calls

```cpp
struct CallInfo {
    std::string symbol;       // the callee, or empty for a call through a value (the first operand then)
    const Type *signature;    // the function type called through: the parameter types the ABI classifies
    bool variadic; int namedArgs;
    bool hasThis;             // the first argument is `this`: cl puts it before the hidden result pointer (A-01)
    bool sret;                // the result goes through a pointer argument, which is the operand after `this`
    bool noexcept_;           // for the region builder: no unwind edge is made
};
```

These are the facts `Ast.h`'s `Call` carries (`Ast.h:254-300`: `name`, `symbol`, `callee`, `variadic`,
`namedArgs`, `hasThis`, `resultSlot`/`resultIndirect`) less the frame slots, which selection allocates. An
argument is a scalar value or a pointer to an aggregate; how it travels - register, two eightbytes, by address,
the Microsoft by-reference rule - is `placeArguments`' (`X86_64Linux.h:299-321`) and is not in the IR.

### 3.6 Exceptions: one model, two lowerings

Design section 4: *`invoke` with a normal and an unwind successor, plus funclet pads; target 1's funclets are
visible to every pass, and targets 3 and 4 lower the same pads to Itanium landing pads.* So the IR carries LLVM's
Windows EH shape for every target, and the selector decides:

- **Microsoft** (`usesFunclets()`): a `catchswitch` with its `catchpad`s is one `try` of the FH3 state tree, each
  handler block's code a funclet, `catchret`'s target the resume label - `Walker::msTryStatement`'s shape -
  and a `cleanuppad` is a cleanup state whose funclet is the pad's code. The per-handler facts `MsHandler`
  carries (`Ast.h:563-571`: descriptor, object slot, by reference, const pointer) are the `catchpad`'s operands,
  the slot being an `alloca` of the handler.
- **Itanium**: the `catchswitch`'s handlers become the one landing pad and the selector chain
  (`Walker::visit(const Try &)`: `landingPad`, then `if (sel == n)` per type in the parser's index order), a
  `cleanuppad` becomes a cleanup region's pad ending in `_Unwind_Resume` (or TI's `__cxa_end_cleanup`), and the
  regions are registered **disjoint and in nesting order, never sorted** (`Walker.h:119`, `openRegion`/
  `closeRegion`) - the lowering numbers them as the walker does and the selector emits rows in that order.
  `__cxa_begin_catch`/`end_catch` on every way out of a handler (the 2026-10-09 rule: a `return`, `break`,
  `continue` or `goto` leaving a handler ends its catch) are explicit `call`s the lowering writes before the
  `catchret`, as the parser writes them today.

The selector indices (`Try::typeIndices`) are the parser's and travel on the `catchpad`s; the type_info symbols
are Sema's (`typeInfoSymbolFor`). The dynamic exception specification (`throw(T...)`) is a `cleanuppad` whose
code calls `__cxa_call_unexpected`, with the filter types on the function (`Function::spec`), as today's
`-1`-indexed region; `noexcept` is a `cleanuppad` calling `std::terminate`.

### 3.7 Memory, volatile, TLS

`load`/`store` carry a `volatile` bit that no pass moves or merges across (design section 4) - **open for RP3**:
the type system drops `volatile` (the volatile sweep, CLAUDE.md) and the typed tree has no record of it; today
`Program::usesVolatile` turns -O off for the unit (`Driver.cpp`). The lowering needs the qualifier on the `var`'s
declared type or a bit on the syntax node; which, and whether M3b.4 can lift the -O-off rule, is decided at RP3
with a measurement of the four corpus cases that write `volatile`. `tlsaddr` is the one TLS instruction;
selection writes local-exec, the `_ZTW` wrapper call, or cl's `_tls_index` walk as `X86_64Linux::tlsAddress`
does. A global, a string literal, a vtable and the RTTI records stay `Program`'s data (`Global`, `StringLit`,
`rtti`, `msThrows`) and are not IR.

### 3.8 Lowering from the typed tree - what it reads and what it makes

It reads: the typed tree (`TypedNode`: syntax handle → kind, text, children, location; type; `decl`; and from
M3b.0 the resolution, the cast type, the folded value, the literal token), the `TypeTable` (layout: `size`,
`alignOf`, members, bases, vtables, `nonTrivialCopy`, `hasDestructor`), the `Target`, and the program's data. It
is constructed with no `Parser` in reach - the compile-time gate of M3b.0.

It makes, from the typed tree's scopes and nothing recorded by the parser: every frame object (`alloca`) and its
alignment; the temporaries' lifetimes (end of full expression, the skipped operand of `&&`/`||`/`?:`); the
destruction order at a scope's end, a `return`, `break`, `continue`, `goto`; the cleanup pads and their nesting
(`alive_`'s successor); the handler regions and the end-catch calls; the guard flags the parser writes today
only where a temporary may or may not have been built (a pad destroys under a flag) - which mem2reg then
promotes. The fifteen layout maps the adapter reads (`callSlots_`, `blockCleanups_`, `returnTemps_`, `tryFacts_`,
`throwFacts_`, `temporarySlots_`, `bindTemps_`, `lambdaFacts_`, `caseFacts_`, `rangeFacts_`, `fullSlots_`,
`temporaryGuards_`, `functionGuards_`, `declaredLocals_`, `syntaxScopes_`; `Parser.h:570-690`) are not read:
12.1 says the layout is M3's own, and the adapter that read them is deleted at M3b.7.

**The record moved onto the typed tree (M3b.0).** `Parser::Resolved` (`Parser.h:512-526`: `what` in {Local,
Global, Function, Call, Callee, Member, Enumerator, StaticMember, This, Temporary, TypeName, Value}, `type`,
`owner`, `symbol`, `serial`, `value`, `isConst`, `isThreadLocal`, `name`) becomes `sema::Resolution` in
`Typing.h`, held in `TypedTree::resolutions_`; `TypedNode` gains `std::uint32_t resolved` (0 none), `const Type
*castType`, and a `Folded {long long v; const Type *t; bool has}` for `sizeof`/`alignof`/`noexcept` and a
literal's value - the parser's `castTypes_`, `keywordValues_`, `literalTokens_`, `refCastTypes_` (`Parser.h:
562-565, 680`) copied in by the Typer as it types each node, from the maps the parser fills at parse time in the
scope it had then (12.1's point 3 is unchanged by the copy). `declaredTypes_` is already `TypedNode::type` for a
`var`. The parser's maps stay until M3b.7 for the adapter and `-check=typed`'s cross-check.

### 3.9 Selection into MIR

The x86 walker (`X86_64Linux`, both targets) becomes the selector: `select(const ir::Function &)` in place of
`emit(const Function &)`, a `switch` over `ir::Op` in place of the `Visitor`. Each SSA value gets a pseudo of its
class (`Gpr` for integers and pointers, `Xmm` for `float`/`double`, memory for x87); a block is a label; a
terminator is `jmp`/`jcc`/the `switch` ladder `caseBranch` writes; an `invoke` is a `call` with the walker's
`exceptionRegion` around it. **What is kept, re-pointed and not rewritten** - the ABI code P1 names, by
function: `placeArguments`, `placeCall`, `returnsThroughPointer`, `classifyEightbytes`, `receiveParameters`,
`storeTailFromReg`, `copyTailMem`, `loadTailToReg`, `msAggregateToRax`, `msCopyToSlot`, `copyBlock`,
`genConversion` and the x87 set (`loadX87Const`, `x87ToInt`, `intToX87`, `genX87Binary`), `bitFieldExtract`/
`Insert`/`UnitAddr`, `tlsAddress`, `msTlsBlock`, `emitTlsWrappers`, `landingPad`, `emitLsda`, `lsdaTable`,
`emitCoffEhTables`, `beginFunclet`/`endFunclet`/`funcletLeave`/`storeUnwindHelp`, `emitCoffClassRtti`,
`emitCoffThrowInfo`, `emitData`, `emitGlobal`, and the DWARF/CodeView record builders. What is replaced: the
accumulator discipline (`%rax`/`%xmm0` with `push`/`pop`, `tempSlot`) - a value is its pseudo; and the statement
visitors (`Walker::visit(const If &)` and the rest), whose work the SSA blocks already did.

### 3.10 `-dump=ssa` and `-check=ssa`

`-dump=ssa` writes every function of the unit in the format of 3.3 after lowering, and `-dump=ssa=<pass>` after
the named pass once M3b.4 has passes (design 4: "a pass counts as ported when its `--dump=ssa` effect ...
match[es]"). `-check=ssa` runs the verifier of 3.4 and reports `-check=ssa: functions=N ok=N failed=0`.

### 3.11 Open at review point 3

- `volatile` on `load`/`store` (3.7).
- the record's `Temporary` and `TypeName` kinds: whether a class temporary is an `alloca` made by the lowering
  with a `call` of the constructor, or carries a slot the parser chose - the first is 12.1's answer and the one
  proposed; the second is what the adapter does (`temporarySlots_`).
- the lambda: the closure class is Sema's (`Classes`); its capture copies are stores the lowering writes; whether
  `__invoke` and the conversion function are compiler-written functions of M3b.3's kind or lowered with the
  lambda - proposed: M3b.3's.
- whether the Itanium selector emits pads per region as today or merges equal pads - today's, until the regions
  gate holds.
- `select` vs a branch for a scalar `?:`: the parser lowers `?:` of lvalues to `*(c ? &a : &b)`; the IR keeps a
  `select` for the rvalue scalar case and blocks for the rest.

## 4. Kept, changed, retired - the one table

| | kept | changed | retired |
| --- | --- | --- | --- |
| M3a | `OptCore.h`, `OptFunction.h` (as `MFunction`), `OptEffects`, `OptFlow`, the passes, `OptPass`, `OptPipeline`, `OptCosts`, `OptDeclines`, `OptLoops`, `OptDataflow`, `Mir*` (webs, locals, allocator), `Optimizer` as the holder, `Spelling`/`GnuSpelling`/`CoffSpelling`/`MasmSpelling` as the writers, the walkers | the mnemonic → `OpId`; events → directives; register → class; allocator → classes; funclets → ranges of one `MFunction`; `MasmSpelling::kRules` → a column; the holder at every level | `OptIr.h/.cpp`, `Instr::m`, `Entry::Event`, `CPP11_DUMP_MIR`, the `isMovQ` family, `Optimizer::funcletBegin/End`, the five `atOutput` closures, `new Optimizer` |
| M3b | the typed tree, the record, `TypeTable`, Sema's modules, the walker's ABI code (3.9), the spellings, M3a's MIR and passes, `Program`'s data, `DivMagic.h`, the DWARF/CodeView writers | the walker → selector; `Inliner` → over `ir::Function`; `Program::functions` → `ir::Function`; `Classes`/`Linkage` build SSA through `ir::Builder` | `Ast.h`'s node classes and `Visitor`, `AstDump.cpp`, `Adapter.*`, `-front=typed`, `-check=adapted`, `tests/adapted.sh`, the parser's `Ast.h`-building half and its fifteen layout maps, `alive_`, `pendingTemps_`, `wrapCleanups`, `Program::usesVolatile`'s -O-off rule (if RP3 lifts it) |

## 5. Flags, in one place

| flag | from | what |
| --- | --- | --- |
| `-dump=mir`, `-dump=mir=<pass>` | M3a.1 | the MIR of every function, after the pipeline or after a pass |
| `-check=mir` | M3a.1 | the round trip: the unit through the holder against the unit past it, text for text; `-mir=off` the bypass, both gone at M3a.5 |
| `CPP11_MIR_STRICT=1` | M3a.1 | a mnemonic the table lacks stops the compile naming it: the completeness measure |
| `-dump=ssa`, `-dump=ssa=<pass>` | M3b.1 | the SSA IR of every function |
| `-check=ssa` | M3b.1 | the verifier |
| `-front=ssa` | M3b.1 | a function the lowering reaches takes the SSA path, any other the parser's; the only path from M3b.7, the flag then gone |
