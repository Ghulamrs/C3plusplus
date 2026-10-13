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
| L02, L03, L04, L06, L08 | **not started** | - |
| L01, L10, L12-L17 (documentation and declines) | not started; L01's paragraph still says what it said | - |

## Oracle

`.expected` for `nullptr-t` and `cstdarg` from VS clang 19.1.5 on the box. **VS clang cannot be
the oracle for a case that includes the C++ library**: at `-std=c++11` Microsoft's own `<vector>`,
`<algorithm>`, `<array>` and `<stdexcept>` do not compile (they use C++14 deduced return types and
C++14 constexpr). `functional-less`, `array-header` and `array-at-throws` therefore take the Mac's
clang 17 against libc++, which compiles them at `-std=c++11 -pedantic-errors`; rule 6 wants the
box's clang, and this is the deviation, said here.

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

## Gates (Mac, 2026-10-13)

run.sh 689 / 0; names.sh 443 / 4 (WS-H's four, which its branch's `.nonames` close); each new
case carries a `.nonames` for the two cross targets the Mac's clang has no headers for, and the
library ones for libc++'s `std::__1` and ABI tag. `tools/exclusions --check` 126 sites, 0 / 0;
`make comments` 0. Box gates not run yet.
