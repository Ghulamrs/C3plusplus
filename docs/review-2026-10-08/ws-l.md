# WS-L - the library: handover

Branch `review/l-library`, opened 2026-10-13 from `review/e2-init-expr` (which carries WS-D), not
from main: WS-L owns `include/` and E2 changed `<utility>` and `<initializer_list>`, so branching
from E2 keeps the later rebase trivial. Nothing pushed or merged.

## Items

| item | state | case |
| --- | --- | --- |
| F26 `std::nullptr_t` | landed: `typedef decltype(nullptr) nullptr_t;` in `<cstddef>` | `nullptr-t.cpp` |
| L18 `<cstdarg>` | landed: the C wrapper, `std::va_list`; **`va_copy` added to `lib/stdarg.h`** (C99, which [cstdarg.syn] takes) for both va_list shapes, and **`vsnprintf`** beside the other three v-functions, which `<cstdio>` now exports under `std::` | `cstdarg.cpp` |
| L09 `<functional>` | landed: the 17 function objects of [function.objects] with C++11's typedefs; `std::function`, `bind`, `ref` named absent in the header and in `docs/EXCLUSIONS.md` | `functional-less.cpp` |
| L11 `<array>` | landed: an aggregate over `T elems_[N]`, pointer iterators, `size/empty/front/back/data/fill/swap`, `==`/`!=`, `array<T, 0>` as a partial specialization; `at()` throws `std::out_of_range`, and on x86_64-windows prints and aborts (L04's rule) | `array-header.cpp`, `array-at-throws.cpp` |
| L07 `sto*` | landed: `stoi stol stoul stoll stoull stof stod stold` over strtoll/strtoull/strtold, `invalid_argument` and `out_of_range` (x86_64-windows aborts, as `<array>`'s at()); in `<__string_conversions>`, which `<string>` and `<stdexcept>` both include at their ends so either order works; `lib/stdlib.h` gained C99's `strtoll`, `strtoull`, `strtof`, `strtold` | `string-stoi.cpp` |
| L05 `setfill` | landed: `ostream::fill_` (0 in the aggregate initialisers reads as a space), set by `setfill` and `fill(c)`, sticky as [std.manip] has it, used by the width padding | `iomanip-setfill.cpp` |
| L03 `vector` owns its elements | landed: raw storage, placement new for every element made (copy, `T()`), a destructor call on every way one leaves - `pop_back`, `clear`, `erase`, `resize` down, assignment, regrowth, `~vector` (which destroyed none before) - and a shift through the element's own assignment | `vector-erase-destroys.cpp` (ledger, `LF_BALANCE_ONLY`) |
| L04 `at()` checked | landed for `vector` and `array`: `std::out_of_range`, and on x86_64-windows a message and abort | `vector-at-throws.cpp`, `array-at-throws.cpp` |
| L02 `sort` O(n log n) | landed: introsort - median-of-three Hoare partition to runs of 16, heapsort past 2 log2(n) depth, one insertion pass; random-access iterators (every container's are pointers); `stable_sort` named absent | `algorithm-sort-large.cpp` (200000 elements, six shapes, a comparator, strings; 0.56 s on the Mac; not on tms6747, minutes on the emulator) |
| L06, L08 | **not started** | - |
| L01, L10, L12-L17 (documentation and declines) | not started; L01's paragraph still says what it said | - |

## Oracle

`.expected` for `nullptr-t` and `cstdarg` from VS clang 19.1.5 on the box. **VS clang cannot be
the oracle for a case that includes the C++ library**: at `-std=c++11` Microsoft's own `<vector>`,
`<algorithm>`, `<array>` and `<stdexcept>` do not compile (they use C++14 deduced return types and
C++14 constexpr). `functional-less`, `array-header` and `array-at-throws` therefore take the Mac's
clang 17 against libc++, which compiles them at `-std=c++11 -pedantic-errors`; rule 6 wants the
box's clang, and this is the deviation, said here.

## Two compiler mends the library needed

- **`new (p) T(t)` for a class whose copy is trivial** found no constructor - cxx1 declares no
  function for a trivial copy - so `vector<pair<int,int>>` stopped compiling once `vector` built
  elements in place. The new-expression copies the bytes there now, as a declaration `T u(t);`
  already did (`ParserExprNew.cpp`).
- **String literals of a pruned inline function stayed in the object.** `pruneUnusedInline` drops
  an unused inline body; its literals were kept, so every unit including `<string>` carried the
  `sto*` family's names. The pass records each literal a kept body or global names and keeps only
  those: 24 golden files outside the changed headers lost dead strings and nothing else (checked:
  no added line in any of them).

## Found and not mended (compiler, not library)

- Two listed limits shaped `<__string_conversions>`: a call giving some template arguments and
  deducing the rest, and a leading `::` in an expression - the helpers are plain functions and
  call `strtoll` unqualified.

- **A function template whose signature names `std::vector<int>` before anything has
  instantiated it leaves `vector<int>` incomplete**: `template <class Op> int fold(const
  std::vector<int> &, Op)` then `std::vector<int> v;` is "'v' has an incomplete type".
  `functional-less.cpp` takes a pointer range instead. Likely the pattern read's shallow type
  (`patternOnly_`) interning the tag before the real instantiation.
- **A class template member in `namespace std` cannot call a free function of `std`
  unqualified** when the body is replayed - "'__array_range_fail' was not declared". `<array>`
  qualifies the call.

## Golden

Recorded from `review/e2-init-expr` on the Mac: **109 of 1723 files changed, 30 added, 0
removed**. 85 are the 20 cases that include a changed header (`<vector>`, `<string>`, the
streams, `<stdexcept>`, `<map>`/`<set>` over vector) - the rewritten vector, `ostream::fill_`, and
dead strings gone; 24 are other cases losing only dead string literals. `long double` cannot spell
`FLT_MAX` exactly on x86_64-linux, so `stof`'s bound is the double that is FLT_MAX, cast.

## C6000

tms6747.sh (RTS6x, both legs) at -O0 passes everything but two cases now marked not for this
target: RTS6x and the vm6747 runtime have no `vsnprintf`, `strtoll`, `strtoull` or `strtold` yet
(C99) - a gap in those repositories, recorded in `cstdarg.notarget` and `string-stoi.notarget`.

## Gates (Mac, 2026-10-13)

run.sh 692 / 0; names.sh 446 / 4 (WS-H's four, which its branch's `.nonames` close); each new
case carries a `.nonames` for the two cross targets the Mac's clang has no headers for, and the
library ones for libc++'s `std::__1` and ABI tag. `tools/exclusions --check` 126 sites, 0 / 0;
`make comments` 0. Box gates not run yet.
