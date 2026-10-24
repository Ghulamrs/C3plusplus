# M2 interim review, Fable 5.1, 2026-10-24

Read-only review of the M2 work from `2e79a0d` (M2.0) to `3edaab2` (M2.12p), asked for by the user
before M2's close. Kept as written; what is done about it goes in M2-PLAN.md.

**Verdict: agree with conditions.** Step 1 (M2.0-M2.7) is done as the design says. Step 2 may
continue only once the conditions below are met; without the first, growing the adapter is not M2
work.

## The denominator

`-check=adapted` counted 1,986 functions, 1,257 adapted, all identical. Over the same 648 cases
`-dump=ast` emits 8,227 functions. The 1,986 are file-scope free functions only:
`ParserTopLevel.cpp` puts every member, every function in a namespace, every template instantiation
and every body with bound template parameters into `scopedFunctions_`, which `Adapter::run` and
`Typer::typeFunction` skip; compiler-written specials, thunks, closures' `operator()` and the init
functions never reach `noteParts`. Checked after the review on a six-function probe (a namespace
function, two members, a template, a free function, `main`): `-check=adapted` reports 2. So the
adapter covers about 15% of the suite's functions, and the 730 "left" are 730 of the easy quarter.

## Against section 12

Section 12, M2 step 2: Sema produces the typed tree and a thin adapter lowers it to `Ast.h`; the
old parser-to-Ast.h path is deleted at M2's end. As built, the adapter does not derive layout from
the typed tree: it reads back the parser's own decisions, several taken off the finished `Ast.h`
nodes (`noteCallSlots`, `noteTemporarySlot` matching `"$tmp"`, `blockCleanups_` from
`lastCleanupSlots_`, `tryFacts_`, `caseFacts_`, `returnTemps_`, `throwFacts_`, `lambdaFacts_`,
`fullSlots_`, `temporaryGuards_`, `functionGuards_`, `rangeFacts_`, `bindTemps_`, `refCastTypes_`,
`keywordValues_`). No commit moves a layout decision into Sema or the adapter, so the adapter cannot
run without the old path, and deleting that path deletes the source of every fact map. The adapter's
`wrapCleanups`, `fullExpression`, `releasePending`, `tryStatement` and `throwSteps` mirror the
parser line for line: rule 3's invariant kept in two places, about 2,000 lines, which M3 deletes.

## Risks found

- **Names looked up again after the file.** The adapter runs after `parse()`, when the namespace
  stack, `currentClass_`, `localTypes_` and block-scope using-directives are gone; it re-resolves
  with `findGlobal`, `findEnum`, `overloadsOf`, `findTypedef` and even `resolveOverload`, which can
  instantiate after the instantiation and pruning passes ran. `scopedName` is a heuristic and is
  wrong for a block-scope `using namespace` truncated at its block's end. Record the resolved
  declaration by node at parse time instead.
- **Fact singletons outside `FunctionState`**: `pendingTry_`, `pendingThrow_`, `pendingLambda_`,
  `pendingRange_`, `lastCleanupSlots_` are set deep in a parse and consumed by order; a nested-parse
  door between the two leaks them - the shape the 2026-09-06 R4 root fixed.
- **Keys** are sound (node ids are never reused, a re-read makes a fresh node), but `adopt`
  de-duplicates roots by location, so two genuine roots at one position from one macro would lose
  the earlier.
- **The dump omits** a `Try`'s types, type indices, `alsoCleanup`, `msExits` and unwind-help slot,
  so two trees differing there compare identical; `adapted.sh`'s assembly comparison covers it on
  the host target only.

## The gate's blind spots

The denominator; the adapter path runs on arm64-darwin only in `make test`; on x86_64-windows the
adapter refuses every function with landing pads, so M2.12h-p is untested against the Microsoft ABI
by construction; `tools/self-host` never passes `-front=typed`; the dump omissions; and post-file
lookup, whose soundness rests on coverage.

## The choice to make before continuing

**A. Amend section 12**: M2 ends when the typed tree is complete and checked - every function typed,
`-check=typed` at 100% of nodes, names resolved by node at parse time - with `Ast.h` kept as the
oracle. The adapter stays a harness for its subset and is not grown; M3's SSA lowering reads the
typed tree and deletes `Ast.h`.

**B. Keep deletion in M2**: change the gate from identical `Ast.h` to identical behaviour (run.sh,
the emit goldens, the ledger, identical.sh across targets), and have the adapter allocate its own
frame slots, labels and guards from the typed tree, fact map by fact map, starting with
`declaredLocals_`; measure the slot-numbering differences first.

## Conditions

1. The choice above taken and written into the design and M2-PLAN.md.
2. The denominator corrected and reported as a fraction of every emitted function.
3. Post-file re-resolution replaced by declarations recorded per node.
4. The fact singletons moved into `FunctionState`.
5. `tools/self-host` and at least one non-Mac leg run `-front=typed`.

Under either choice: count members, namespaces and templates now; dump the missing `Try` fields.
