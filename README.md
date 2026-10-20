# cxx1 — a C++03 compiler with C++11 extensions

`cxx1` (`cpp11.exe`) compiles **C++03 with C++11 extensions** to native
assembly for **x86_64-linux**, **x86_64-windows**, **arm64-darwin** and
**tms6747** (TI's C6747 DSP). It is written in ISO C++14 and depends on no
compiler framework: its own lexer, preprocessor, parser, type system and four
code generators.

**The headline is measured, and it changed on 2026-10-08.** Every document in
this tree used to say "C++11 minus `docs/EXCLUSIONS.md`"; an independent review
that day put 122 C++11 feature probes through the compiler and 56 were refused -
`enum class`, `override`, `= default`, delegating constructors, alias
declarations, forwarding references and list-initialisation among them - which
is roughly half of what C++11 added to C++03, and under a fifth of its library.
What *is* here of C++11: `auto`, `decltype`, lambdas with every capture, rvalue
references and move, variadic type packs, `static_assert`, `constexpr`
functions, `nullptr`, range-based `for`, `noexcept`, `explicit` conversion
functions, `char16_t`/`char32_t`, `alignas`. The complete list of what is not
is `docs/EXCLUSIONS.md`, a refusal site per entry, cited by file and line and
checked against the source by `tools/exclusions --check` on every `make test`.
The headline goes back to "C++11" when that list says it may.

`CLAUDE.md` opens with **"Reading this tree"**, which is the order to read this
one in - the ladder, the pipeline by file, a case per rung, and the suites - and
then holds the measurement behind each decision.

## Build

```
make
```

Needs a C++14 compiler and nothing else. `clang++` on a Mac, `g++` on Linux;
both are checked, and `cl` is the third, through `msvc\build.cmd` on the
Windows box. `tools/verify-three` builds and tests on all three from the Mac,
and runs the tms6747 leg on TI's own simulator from the Windows box.

## Use

```
cxx1 f.cpp -o f          compile, assemble and link for this machine
cxx1 -c f.cpp            stop at an object file
cxx1 -S f.cpp -o f.s     stop at assembly
cxx1 -S -arch x86_64-linux f.cpp
cxx1 -I dir f.cpp        add a directory to the ones <...> searches
```

`-D` defines a macro and `-U` removes one, `-j` sets how many files compile at
once, `-g` writes a line table for a debugger - DWARF on x86_64-linux and
arm64-darwin, CodeView on x86_64-windows through the `gnu` spelling (since
1.6), and on tms6747 cl6x's `.dwpsn` lines and `.dwtag` variables (since 1.8; refused there
with `-O`, the scheduler carrying no position) - `-masm` picks the assembly syntax for
x86_64-windows (`masm` for the project's own masm and link, the default where
they sit beside the compiler, `gnu` for clang, the default elsewhere and for
`-g`, `ml64` for Microsoft's assembler, one translation unit per program),
`-O1`/`-O2`/`-Os` optimise, and `-time` reports each phase. `cxx1` with no
input prints the whole list, and `-version` the release and its seal file.

Targets are named `x86_64-linux`, `x86_64-windows`, `arm64-darwin` and
`tms6747`. A host target that is not this machine implies `-S`: the compiler
will not ask the local assembler to build for hardware it is not on. `tms6747`
is different, since no host is a C6747: `-c` assembles what the compiler wrote
with asm6x, the project's own C6000 assembler (`CPP11_AS` names it, else the
one beside `cpp11`, else `asm6x` on PATH), into a TI object on any machine;
without `-c` the objects go to TI's `lnk6x` into a `.out`, against the run-time
`-rts=` picks: a directory holding RTS6x's `rts6x.lib`, or the `.lib` itself, or
`ti` for TI's `rts6740`. Unsaid, cpp11 links RTS6x when `RTS6X` names it or
`lib/rts6x-tms6747` is beside `cpp11` - as RIDE installs it - and neither
`CPP11_TI` nor `CPP11_TILIB` names TI's; TI's otherwise, `rts6740_elf.lib` or
`rts6740_elf_eh.lib`, which a C++ program that throws needs, where CCS is:
`CPP11_TI` names its C6000 compiler directory, `CPP11_TILIB` a directory holding
the exception-handling runtime, `CPP11_LD` the linker itself. The link prints
which run-time it took, and a `-rts=` naming no `.lib` that exists is refused.
`tests/asm6x.sh` holds every case's driver-made object to asm6x's own;
`tools/windows/ti-link.cmd` links every case on the box, and `verify-three`
runs both.

## The library

**A subset ships, and it is a subset on purpose.** `include/` holds 35 C++
headers and `lib/` 19 C ones (counted 2026-10-13, WS-L). They are here
for two reasons: the core language cannot do without some of them -
`<initializer_list>` is what a braced initialiser *means* - and a compiler with
no containers cannot be pointed at a real program. **They are not an
implementation of the C++11 library and are not becoming one.**

| | |
| --- | --- |
| containers | `<vector>` `<string>` `<map>` `<set>` `<array>` |
| algorithms | `<algorithm>` `<numeric>` `<utility>` `<functional>` |
| ownership | `<memory>` - `unique_ptr<T>`, `shared_ptr<T>` |
| streams | `<iostream>` `<ostream>` `<istream>` `<sstream>` `<fstream>` `<ios>` `<iomanip>` |
| exceptions | `<exception>` `<stdexcept>` |
| language support | `<initializer_list>` `<limits>` `<new>` `<typeinfo>` `<type_traits>` |
| C, inside `std` | `<cassert>` `<cctype>` `<cfloat>` `<climits>` `<cmath>` `<cstddef>` `<cstdint>` `<cstdio>` `<cstdlib>` `<cstring>` `<cstdarg>` |

`lib/` holds 19 C headers - `assert.h` through `time.h` - and the ten `<c…>`
names forward into it. **The forwarding is a using-declaration**, `namespace std
{ using ::size_t; }`, rather than a repeated typedef: a typedef would be a
second type that happened to agree, and the two would differ to an overload.
Measured complete - every standard name `stdio.h`, `stdlib.h`, `string.h`,
`ctype.h` and `math.h` declare is named in the `<c…>` beside it, and the only
names left behind are the platform's own, `fileno` and `__acrt_iob_func`, which
do not belong in `std`. `<cassert>`, `<cfloat>` and `<climits>` forward nothing
and are right not to: what they provide is macros, and a macro belongs to no
namespace.

**How a header is found.** `<...>` searches every `-I` directory in the order
written, then `include/`, then `lib/`. C++ comes before C because `<cstddef>`
has to be found before it can include `<stddef.h>`. Both directories are
compiled into the binary as absolute paths - `$(CURDIR)/include` and
`$(CURDIR)/lib` at build time - so a `cxx1.exe` copied elsewhere still reads
the headers of the tree it was built in, and stops finding them if that tree
moves.

### What these headers are honest about

Every one of them documents its own limits at the top. The ones that change
what a program *means*:

- **`std::cout`, `cerr`, `clog` and `cin` are `static`** - one set per
  translation unit rather than one per program. A header-only library has
  nowhere to put a definition. They also have no constructor: `ostream` is an
  aggregate built from constant initialisers, and the `FILE *` is resolved at
  the point of use, so nothing has to run before `main`.
- **`vector<T>::iterator` is `T *`**, not a class. That is what keeps the header
  short - `++it`, `*it`, `it->m` and `it != v.end()` are all built in - and the
  cost is that any insertion invalidates every iterator, `begin()` on an empty
  vector answers null, and a `vector<int>` iterator compared against a
  `vector<char>` one is not caught.
- **`vector` owns its elements** (2026-10-13): built in place by placement new, destroyed on
  every way out, `at()` checked, `emplace_back` forwarding its arguments.
- **`map` and `set` are sorted vectors**, binary-searched (L01, declined for this round). The
  interface is the standard's and the complexity is not: an insertion is O(n) and moves elements,
  so it invalidates pointers into the container where [associative.reqmts] keeps them. The design
  for the round that replaces it is a red-black tree with node-stable iterators, which wants a
  class iterator - range-for over one works since WS-D.
- **`sort` is an introsort** (2026-10-13): O(n log n), not stable; `stable_sort` is absent.
- **`numeric_limits` means something only where it is specialised.** The primary
  template answers `T()` to every query.
- The library's free functions are `inline` and its function templates plain,
  as the standard has them - one definition merged across translation units
  (since 2026-09-19; they were `static` while cxx1 had no weak linkage). Only
  the four stream objects in `<iostream>` stay `static`: an `inline` variable
  is C++17, and there is no object file to define them in.

### What is absent

**`<stdexcept>` is here as of 2026-09-07**, with the standard's nine classes over a `std::string`
and `std::exception` in `<exception>` beside it. `vector::at`, `array::at` and the `sto*` family
throw `std::out_of_range` and `std::invalid_argument` as the standard has them, except on
x86_64-windows, where a class cannot be thrown yet and they print and abort instead.

**Absent, each for a reason** (the preprocessor refuses every one of these by name, L19):

- `<tuple>` (L10): the shape is the recursive `Tuple` of `tests/cases/tuple.cpp`; `get<I>` needs a
  non-type template argument written beside deduced ones, which this compiler does not do yet.
- `<unordered_map>`, `<unordered_set>` (L12): separate chaining over a `vector` of buckets, with
  `std::hash` for the integral types and `string`, is the design; not written this round.
- `<thread>`, `<atomic>`, `<mutex>` (L13, L14): there are no threads on the C6000 and no
  `thread_local`; declined.
- `<chrono>` (L15): it wants `<ratio>`'s arithmetic over non-type template packs; declined.
- `<regex>` (L16) and `<random>` (L17): declined; `minstd_rand`, `mt19937` and
  `uniform_int_distribution` are fully specified by the standard and are the place to start.
- From headers that are here: `std::function`, `bind` and `ref`; `unique_ptr<T[]>`, `weak_ptr` and
  `make_shared`; `stable_sort`. `docs/EXCLUSIONS.md` gives each its reason.

`<new>`, `<typeinfo>` and `<type_traits>` are here: a class's own `operator new` and `operator
delete`, the placement and nothrow forms, `std::bad_alloc` from the platform runtime, `typeid` and
`dynamic_cast` all work.

## What 1.22 changes

The last C++11 language rows the feature grid refused but one, and the fixes a real project met. Each
compiles on all four targets but where a line below says otherwise, and agrees with clang:

* **A `goto` out of a `catch` handler** ends the handling; a jump *into* a `try` block or a handler is
  refused as clang refuses it.
* **Function try blocks**, a constructor's covering its mem-initialisers; and **a constructor that
  throws destroys what it had built** - bases, members, array elements, virtual bases - where it
  destroyed none of it.
* **Dynamic exception specifications**, `throw(T...)`, kept by `std::unexpected` on the Itanium targets
  and stated in the table on the C6000; read and ignored on x86_64-windows, as cl does.
* **`thread_local`** on the three hosts, and **`alignas` on a local past the stack's alignment**; both
  still refused by name on tms6747.
* **A universal character name or UTF-8 in an identifier**, and **user-defined literals**.
* **`constexpr` objects of class type**, and a `static constexpr` member of any literal type.
* **An overloaded function's name passed as an argument**, picked by the parameter it is passed to.
* **The ArrowsMetal Core sources build and run as clang's build does**: an aggregate with members of a
  class with constructors, a typedef naming an unnamed class for linkage, a data member hiding a class
  template, a temporary in a namespace-scope initialiser, and a template read beside a caller's local.
* **On x86_64-windows a constructor that throws before the first `try` unwinds correctly**: the
  exception-state word is set to -2 on entry, as cl's prologue writes it.

`tools/feature-sweep` reads **87 compile, 9 refused by name, 0 naming nothing, of 96**: four C++14 forms
refused as they must be, four library rows, and one C++11 language row, the unrestricted union.

## What 1.21 changes

Six of the seven programs `tests/open` held - each a known wrong answer or refusal against clang -
are mended and moved into the suite; the seventh, the over-acceptance two-phase lookup costs, is kept by design:

* **Slicing**: `T t = u;`, `w = u`, `take(u)` for `take(T)` and a return copy the base part of a derived
  object; a derived object passed by value to a base ranks as a Conversion. A private base is refused.
* **A non-type template parameter has its declared type**: `unsigned long long N` past LLONG_MAX, `unsigned`
  arithmetic, `bool`, `char`, `short` - every one had been an `int`.
* **`offsetof` is a constant expression**, in a `static_assert`, an array bound, an enumerator, a `case`
  label and a template argument.
* **`struct lib::P *` and `struct lib::Box<char> *`**, an elaborated name with a qualifier or a
  template-id; and a block declaration beginning with `class`.
* **An overloaded function's name picked by its target type** ([over.over]): `&f` and `&S::f` take the one
  whose type an initialiser, an assignment, a cast or a return asks for. An argument does not pick yet.
* **Two namespaces' function templates of one name**: `m::twice(2)` called `n::twice` - a silent wrong
  value; a call now takes the nearest namespace's templates, or the one a qualifier names.

## What 1.20 changes

The user's CCS sample projects, SampleExt and SampleExt1, compile and run unchanged - but for
SampleExt1's own `template <bool> struct GCompileCheck;` written inside `#ifndef assert`, which every
compiler refuses. What they needed, each legal C++11 and each a case recorded from clang:

* **Unnamed template parameters**, `template <bool> struct GCompileCheck;`, `template <class>`,
  `template <int = 3>` - every one was refused, the non-type form naming no feature.
* **A member template's signature over its class's parameter**, `CVector<N + K>` inside
  `CVector<N>`, named as clang names it (`S_IXplLm3ET_EE`).
* **A base copied in a mem-initialiser**, `: B(b)`, for a base with no copy constructor of its own.
* **`p->template f<3>()`**; **`V<I>::operator*(s)`** in a member of a class derived from `V<I>`,
  called on `this` and never dispatched; **`B(d)`**, a functional cast from a derived object;
  **`(const V &)q`**, a cast to a reference naming the base subobject; and **a const reference to a
  base bound to a derived class temporary**, `f(D(3))` for `f(const B &)`.
* **A `static` function's Itanium name**: a function template's had no `L`, and every one in a
  namespace had it after `_Z` - `_ZLN1n1fEi` where clang writes `_ZN1nL1fEi`. Only the names were wrong.

## What 1.19 changes

Two more refused C++11 features, built by Fable 5.1 and measured on all three boxes, and three wrong
answers mended:

* **Template template parameters**: `template <template <class> class C, class T> struct Holder { C<T> c; };`
  takes a class template, an alias template or a namespaced one, deduces `C` and `T` from `Box<int>`,
  and specialises on `C<int>`; named as clang and cl name it (`6HolderI3BoxiE`, `?$Holder@UBox@@H@@`).
  `std::vector<std::vector<int>>` is now `St6vectorIS_IiEE`, as clang writes it, where a `St` was repeated.
* **Non-type parameter packs**: `template <int... Ns>`, `template <class T, T... Vs>` - the shape
  `std::integer_sequence` takes - expanded in braces, calls and template arguments (`(Ns * 2)...`),
  deduced from `List<Ns...>`, peeled head and tail by a partial specialisation. A type pack is now
  deduced from a class template argument too, `size(Tuple<Ts...>)`.
* **A named union's member initialiser is applied**: `union U { int i = 3; double d; }; U u;` gives 3,
  where `u.i` held the stack - a silent wrong value.
* **`int v; int v = 5;` at namespace scope is refused** as a second definition, as clang refuses it;
  C's tentative definition had been accepted.
* **A union holding a class with its own constructor, destructor or assignment is refused by name**
  (C++11's unrestricted union). Without a constructor of its own it compiled and built nothing; with one
  it compiled and destroyed the member twice, the union's destructor and then cpp11 itself running `~S`.

`tools/feature-sweep` reads 79 compile, 17 refused by name and 0 naming nothing, of 96 - one fewer
compiling than the round's packs alone read, because the `union-nontrivial-member` probe it had counted
as compiling was that double destruction.

## What 1.18 changes

Three C++ features that were refused by name, each built by Fable 5.1 in a round of its own and
measured on all three boxes before it was merged:

* **Ref-qualifiers on member functions**: `int take() &` and `int take() &&` are two functions, an
  lvalue object choosing the first and an rvalue the second ([over.match.funcs]/4); mixing a
  qualified and an unqualified overload is refused as clang refuses it. Named `_ZNR6Buffer4takeEv` /
  `_ZNO...` and `?take@Buffer@@QEGAAHXZ` / `QEHAA` - the Microsoft spelling confirmed by cl itself.
* **Inline namespaces**: the members of `inline namespace v2` inside `lib` are found as `lib::f`,
  by unqualified lookup, `using namespace` and argument-dependent lookup, and a class template in one
  may be specialised from the enclosing namespace; the name keeps the real namespace,
  `_ZN3lib2v27versionEv`.
* **Anonymous unions** in a class, at namespace scope (`static`, or in an unnamed namespace) and as a
  local: the members are the enclosing scope's own, sharing one slot, laid out as clang lays them on
  every ABI, aggregate- and mem-initialised as [class.union] says. A union member whose class has a
  constructor, destructor or assignment (C++11's unrestricted union) is still refused by name.
* **The MASM spelling renames a name MASM cannot hold** - the anonymous union's hidden object is
  `?$S1@@3T<unnamed-type-$S1>@@A` to cl, and the project's masm refused the `<`. Such a name is always
  internal; an exported one would be refused rather than renamed.

`tools/feature-sweep` reads 78 compile, 18 refused by name and 0 naming nothing, of 96.

## What 1.17 changes

* **A `const` return type is part of a Microsoft name**, as cl writes it: `const int f()` is
  `?f@@YA?BHXZ`, a const class `?BUS@@`, a const enumeration `?BW4E@@`. The `const` had been
  dropped, so such a function compiled by cl did not link with cxx1's.
* **A pointer to a function returning a class or enumeration** is named as cl names it,
  `P6A?AUS@@XZ` where cxx1 wrote `P6AUS@@XZ` - the same link failure, older.
* **`decltype` of a non-class prvalue has no cv** ([expr]/6): `decltype(k + u)` over a
  `static const int k` is `int`, where it had been `const int`.

## What 1.16 changes

The two nits the review of 1.12 and 1.13 left, and an older fault beside the first:

* **A relative namespace qualifier**: `int b::f()` written inside `namespace a` defines `a::b::f`,
  where it was "'b' is not a class". Once found, the definition had been registered under a second
  name, so a default argument it added was never seen at the call.
* **A namespace variable defined through a qualifier reads its initialiser in its namespace**
  ([basic.lookup.unqual]/13): `int a::b::v = k + 100;` read the global `k` - a silent wrong value.
* **A partial specialization may not give `T v` a value while `T` is still a parameter**
  ([temp.class.spec]/8): `template <class T> struct S<T, 0>` is refused, as clang refuses it.



**Expression SFINAE.** A function template's signature may depend on its parameters through an
expression, and a substitution failure there drops the candidate:

```cpp
template <class T> auto size(const T &t) -> decltype(t.size()) { return t.size(); }
template <class T> decltype(T() + T()) sum(T a, T b) { return a + b; }
template <class T> typename enable_if<sizeof(T) == 4, int>::type four(T) { return 4; }
```

* **The names are clang's** on the Itanium targets - the expression itself, `DTcldtfp_4sizeEE`,
  `XeqstT_Li4EE` - and cl's on x86_64-windows, which spells the substituted signature. A form not
  measured (a postfix `++`, a lambda, `new`, `typeid`) is refused by name rather than spelled by guess.
* **`decltype` answers [dcl.type.simple]/4 in full**: a cast, a functional cast, a class temporary and
  unary `+` are prvalues (`T`), an assignment and a prefix `++` lvalues (`T &`), and an unparenthesised
  member access the member's declared type. These were wrong outside templates too.
* **Two templates differing only in a `decltype` return are two candidates**, so a call that matches
  both is ambiguous as clang says; before, the second was folded into the first.
* A reference binds to an assignment, `return t = t;` into an `int &`.



A review of 1.12 and 1.13 found two blocking faults and seven more, and an expression-SFINAE
design found three; all are mended or refused by name (the review's two nits are left):

* **A bound template parameter hides the enclosing class's and namespace's names again**
  ([temp.local]/6). 1.12 asked the enclosing scopes first, so inside a namespace with `typedef
  double T`, `Box<int>`'s `T` was `double`, and a member template's own non-type parameter lost
  to a class enumerator or member of the same name - silent wrong values.
* **An inheriting constructor in a class with a virtual base** builds it once: CI1 builds the
  virtual base and calls CI2, where 1.13 made the two one function and the program crashed. Beside
  it, a derived class's member initialiser past a base with a virtual base is no longer skipped.
* **Namespace scope one door over**: a member of a class in a namespace defined out of line, a
  static member's definition, a default argument on an out-of-line definition, and a namespace's
  function template instantiated from outside all read their names in that namespace. Such a
  template is now named with its namespace on both ABIs, `_ZN1n5twiceIiEET_S1_` and
  `??$twice@H@n@@YAHH@Z`, and so is a function template of `std` - `_ZSt4moveI...` - as clang
  and cl name it, where the namespace had been dropped.
* **Member templates**: access and the path to the declaring class are checked, and beside an
  ordinary member of the name, `s.f(1)` ranks the template rather than taking it.
* **Non-type arguments**: one that narrows to its parameter's type is refused, and an unsigned one
  is spelled unsigned in an Itanium name.
* **A non-dependent template argument is a class** in a signature - `enable_if<2 + 2 == 4,
  int>::type` and `V<N + M>` kept their members. Refused by name: `decltype` of an expression over
  a template parameter in a signature (Itanium would have to spell the expression), and a base's
  constructor template inherited.

## What 1.13 changes

The plan's third day, and the last of its items:

* **Inheriting constructors**, `using B::B;` ([class.inhctor], as clang applies it under
  `-std=c++11`): each constructor of B, but its default, copy and move ones, becomes one of D's, with
  B's parameters, default arguments, access and `explicit`, building the rest of D as the implicit
  default constructor would. Itanium names it `CI1`/`CI2` plus the base (`_ZN1DCI11BEi`); Microsoft as
  an ordinary constructor. Refused by name: of a virtual base, a constructor with `...`, and one taking
  a class by value whose copy is not trivial.
* **`template <class T, T v>`** - a non-type parameter whose type is an earlier type parameter - and
  with it **`std::integral_constant`**, the base of every trait in `<type_traits>`, as C++11 writes it
  (no `operator()`, which is C++14). `true_type` and `false_type` are its two `bool` instances.
* **The headline, re-measured**: `tools/feature-sweep` reads 72 compile, 22 refused by name, 0 naming
  nothing, of 94. The feature the review's Rec 1 named has landed; the headline stays "C++03 with
  C++11 extensions" while fourteen C++11 language rows are refused.

## What 1.12 changes

The five items the 1.9 round found and did not mend, and an older silent wrong value found beside the first:

* **A namespace member defined out of line**, `int outer::f(S s) { S t; return k; }`, reads its
  parameters and body in that namespace ([basic.lookup.unqual]/6 and /8); `S` was "not declared".
* **The nearest scope's name wins.** Inside a namespace, a global object, enumerator or type of the
  same name beat the namespace's own - a silent wrong value, 131 where clang prints 37 - and inside a
  member function a global type beat the class's own member type (`sizeof` 26 where clang gives 81).
  Recorded in `docs/CONFORMANCE.md` until now; the entry is gone.
* **A member function template of a base** is reached through a derived object, by its bare name and
  qualified, with `this` moved to where the base sits; **a static member template** is called with no
  `this` - qualified, through an object, or by its bare name from a static member function.
* **A class template's nested class**: its constructor, destructor, member functions and static data
  members defined out of line; `Tree<int>::It::Deep::twice(2)` in an expression; and its inline members
  compiled only where called, so a static data member an uncalled one names is no longer defined.

## What 1.11 changes

The two defects GRIDEx2's showcase met (the Windows handoff `gridex2-2026-10-17c`):

* **A skipped group expanded macros.** Every line of a group a conditional skips was expanded on its
  own, so a function-like macro call spanning two lines there - an `assert` in a header included a
  second time behind its include guard - was "missing its ')'", and a skipped `__COUNTER__` counted.
  [cpp.cond]/6: a skipped line is scanned for comments and directives and nothing is expanded.
* **A braced element of an `initializer_list` of a class with constructors**, `{{1, 2}, {3, 4}}`, is
  built in place by the constructor its items choose ([over.match.list]): never an explicit one, and
  each item checked for narrowing. It was refused by name; `docs/EXCLUSIONS.md` loses the entry.

## What 1.10 changes

`-g` on tms6747: a variable of an inner block is written inside that block's `DW_TAG_lexical_block`,
named by the block's begin and end labels, so a debugger lists it only while the stop is inside the
block - GRIDEx2's phase F, brought from the Windows handoff of 2026-10-17. No instruction changes:
`-g` adds `.dw` records and block labels, which `tests/run.sh` holds. Read by asm6x 1.5 and vm6747
1.2; GRIDE's sim6747 and vm6747 show the blocks. Measured on the boxes: the referee's 445 images agree
between sim6747 and TI's CCS 5.5 simulator, 0 differ.

## What 1.9 changes

The first round of the 1.9 mending plan (2026-10-16), and the work brought from GRIDE's toolchain copy:

* **Four parser defects, each with clang-checked cases.** A constructor initialising a member that
  hides a base's member of the same name crashed the compiler; a list entry naming only a base's
  member is refused now, as clang refuses it. A nested namespace named relative to the one around
  it, `inner::S` inside `outer`, was refused - and a nearer class or namespace now hides a
  file-scope one of the same name, where a type and an expression had answered differently.
  `typename X<It>::type()` makes a temporary, a member function template is called by its bare
  name inside a member and ranked beside an ordinary member of that name, `(S()).k` reads a
  temporary, and a cast to a function type, `(int()) + 1`, is refused. A class template's own name
  before `::` - `Tree::next(n)` inside `Tree<T>` and its nested classes - is the specialization,
  and `Tree::Tree(5)` is refused as naming the constructor.
* **tms6747**: a parameter `T (&)[N]`, and with `-g` the `.dwtag` records that let sim6747 and
  vm6747 show parameters, locals and their types.
* `Preprocessor::fail` no longer ends the whole process from inside a worker, so one file's
  `#error` leaves the other jobs' output written. README documents `-rts=`.
* `make test` passes again: `docs/EXCLUSIONS.md`'s citations name the lines that refuse (126 sites,
  0 uncited, 0 stale) and no comment is over the cap. `tools/feature-sweep` is unchanged at 71
  compiled, 23 refused by name and 0 naming nothing, of 94, so the headline stays.

## What 1.8 changes

`-g` for tms6747: the statement's source position goes into the assembly as
cl6x's `.dwpsn`, so VM6747 1.1 (`vm6747 --debug`) can stop on a line and step
under RIDE 5.13's Debug session. `-g` with `-O` stays refused for that target.
The tms6747 suite leaves its liveness self-check off for `exception-table-large`.

## What 1.7 changes

The second half of the review round of 2026-10-08 (`docs/REVIEW-PLAN-2026-10-08.md`;
1.6 carried the first half - `enum class`, `override`/`final`, `= default`/`= delete`,
forwarding references, alias templates and the Windows MASM path). In the
language: delegating constructors, trailing return types, raw string literals,
alias declarations, attributes read where the grammar places them,
list-initialisation in every form and a braced call argument, `auto x = {...}`,
a capture-less lambda converting to a function pointer, `dynamic_cast` to a
reference, a `static_cast` downcast, a namespace alias, `__COUNTER__`, a const
member initialised in a constructor's list, and range-based `for` over a class
iterator, free `begin`/`end`, a temporary and a braced list. In the library
(above): a `vector` that owns its elements, a checked `at()`, `emplace_back`, an
O(n log n) `sort`, `<array>`, `<memory>`, `<functional>`'s function objects,
`<cstdarg>`, `std::nullptr_t`, the `sto*` family and `setfill`. In the tools:
`cpp11 -E`, and every `.expected` re-asked of clang. The headline was
re-measured and stays "C++03 with C++11 extensions" - CLAUDE.md's "The
headline, re-measured, 2026-10-13" says why, row by row.

## What 1.4 changes

The cl review of 2026-09-17 (`VM6747/CXX1I-REVIEW-2026-09-17.md`, made against
`cl.exe` where every earlier review had been made against clang) found nine
wrong answers, every one at the boundary with cl-compiled code or in layout
and so invisible to a program compiled entirely by cxx1. All nine are fixed
in Compiler-Cppi and cherry-picked here, six commits, verified on the Mac,
the Linux box and the Windows box - where a new pair suite, `tests/winlink/`,
links each half of a program compiled by cxx1 against the other compiled by
cl, both ways. On x86_64-windows: a constructor returns `this` in RAX, which
cl's callers use; a class's own overloads of one virtual name take cl's
vftable slots; a prototype's by-value parameter no longer leaks into the next
definition's destructor list; two adjacent empty bases get cl's byte between
them; `#pragma pack` is honoured; a pointer to a member is sized by cl's
inheritance model (4/4/8 and 8/16/16). On every target: an empty base goes
where Itanium puts it too (at 0 unless a same-type empty subobject is there);
`int B::*` converts to `int D::*` ([conv.mem]); a null data member pointer is
-1, not 0; `<ostream>` prints `signed char` and `unsigned char` as
characters; `<istream>` stops a number at its last digit and stores 0 on
failure; unary `+` promotes; and an override must keep the return type, or
return a covariant pointer or reference (cl's C2555 otherwise). Re-sealed the
same day with two small fixes: the driver finds its include directory by
`cstddef` rather than by a C header, so an installation whose C headers sit
in a `lib/` beside it - RIDE's, cc1's - is found; and an unreachable `return`
after a diagnostic is gone (cl's C4702).

## What 1.3 changed

Seven fixes and one region, every one cherry-picked from Compiler-Cppi - the
clone that carries this compiler's fourth target - and verified here on the
Mac, the Linux box and the Windows box: a mem-initialiser may name a
template-id base (`: P<int>(x)`); a lambda's body sees the statics,
enumerators, typedefs and nested classes of the class it was written in; a
lambda's return type is deduced from every `return` in its body, through a
`try` or an `if`; a floating constant expression folds (`constexpr double`, a
`static_assert` over one, an array bound cast from one); a static local is
named by its function's mangled symbol, so two functions of one name, an
overload, a destructor and a lambda may each have one; and two terminate
scopes on the Itanium targets - a destructor that throws while an exception
unwinds, and a by-value `catch` whose copy throws, both call `std::terminate`
as the standard says. `tests/open` holds the two Itanium virtual-inheritance
defects below and nothing else; Compiler-Cppi has closed those too, with the
vtable group, `_ZTv` thunks, VTT and construction vtables, and that work is
not in 1.3.

## Known shortcomings in 1.22

Everything here is measured and written down elsewhere in full; this is the
short list a user of **1.22** should have in front of them. `docs/EXCLUSIONS.md`
is the complete one - 136 refusal sites on 2026-10-21 (131 on 2026-10-08),
each citing the source line that refuses it, and `make test` fails when the two
disagree. The heading
above is written by `tools/version-lines` from `src/Version.h`; it said 1.4 for
two releases before that tool existed.

**The language is C++03 with C++11 extensions, and the one C++11 language feature the probe grid still
finds refused, by name, is a union holding a class with special members of its own (an unrestricted
union)**; most of the library is refused too. A user-defined literal, `thread_local`, a `constexpr`
object of class type, `alignas` past the stack, a dynamic exception specification, a universal character
name in an identifier, a `goto` out of a handler and a function try block landed in 1.22.
`tools/feature-sweep` counts them: on 2026-10-21, 87 of its 96 probes compile, 9 are refused by name
and none by a message naming nothing.
**Landed in the review round of 2026-10-08 to 2026-10-13**: `enum class`;
`override` and `final`; `= default` and `= delete`; delegating constructors;
`using X = T;` and alias templates; forwarding references and `std::forward`;
list-initialisation that calls a constructor, `P p{1, 2}`, `T{...}`, `new
T{...}` and a braced argument; a trailing return type; a `constexpr`
constructor, run at run time; raw string literals; `extern template`, read and
suppressing nothing; a capture-less lambda converting to a function pointer;
range-based `for` over a class iterator, a temporary and a braced list. What
works besides, measured against clang in the suites: `auto`, `decltype`,
lambdas with every C++11 capture, rvalue references and move, variadic type packs,
`static_assert`, `constexpr` functions, `nullptr`, range-based `for`
over an array or a container, `noexcept`, `explicit` conversion
functions, `alignas`, `char16_t`/`char32_t`, `dynamic_cast`, `typeid`, a
class's own `operator new`, exceptions with class types, virtual inheritance on
the Itanium targets, partial specialization, SFINAE - on an expression in a signature too, since 1.15 -
member and constructor templates.

**Templates instantiate by replaying tokens**, so every name in a template body
is looked up at instantiation - MSVC's old model. cxx1 therefore *accepts*
some programs clang refuses, which `docs/CONFORMANCE.md` records rather than
hides. The four shapes met while writing `<type_traits>` all compile now, the last
being `template <class T, T v>` - so `std::integral_constant` is the base of every trait.

**Two namespaces' function templates of one name are one overload set**: templates are keyed by
their bare name, so `a::twice(3)` can reach `b::twice`. Registered in `tests/open`
(`namespace-function-template-name`); clang prints `6 2 6` where cxx1 prints `6 2 4`.

**Six programs clang compiles or refuses differently are registered** in `tests/open`, each with
clang's answer: four refusals of ordinary code - `T t = u;` copying a derived object into its base,
`struct lib::P *` (an elaborated type with a namespace qualifier), `offsetof` as a constant expression,
and `&S::f` choosing between `f() &` and `f() &&` by the target type - and a non-type parameter of a
wide unsigned type read as an `int` past `INT_MAX` (`nontype-parameter-typed-int`), besides the
namespace-template one above and two-phase lookup's.

**`std::string` and its neighbours are a subset**: building GRIDE's `cppparse` example with cpp11
stops in 33 of its 39 files - on `<iosfwd>` and `<ctime>`, `string::compare`, `insert` and `rfind`
forms, a `vector` range constructor, `ostream::setf`, `std::stable_sort`, `set::insert`'s pair, and
one condition cpp11 parses wrongly (`if (std::string(b) == "}")`). Recorded, not mended in 1.19.

**Pointers to members** stop short of two things: a pointer to a *virtual*
member function is refused by name (Compiler-Cppi has it), and `&D::f` for an
`f` that D inherited is not found - write `&B::f`, which converts.

**x86_64-windows lags the two Itanium targets on exceptions and layout**, and
each refusal names itself:

* a destructible local and a `try` in one function, function-wide;
* `return`, `break` or `continue` that leaves a `catch` - a handler is a
  funclet there;
* a `try` inside another; a class-typed `throw`; a rethrow from inside a
  handler;
* an override from a base that is not the first, where cl biases `this`
  instead of emitting a thunk;
* a `volatile` object with external linkage, whose name cl decorates.

**Virtual bases are no longer among them.** The Microsoft layout - the vbptr,
one vbtable per pointer, the store in every constructor and both walks that
read one - landed on 2026-09-10 and matches `cl` byte for byte, diamonds
included; so did cl's hidden most-derived flag, which is how that ABI builds a
virtual base once rather than once per class that names it. What remains is
three defects the work uncovered, all of them **Itanium-side or shared** and
all registered in `tests/open`: a second base's own virtual base is read
through the wrong pointer, and `delete` through a pointer to a polymorphic
virtual base misses the most-derived destructor. Two more that the work found
are **fixed**: the first non-virtual base carrying a vptr is laid at offset 0
on the Itanium targets whatever its place in the base-clause, and a class that
introduces its own vptr puts it in front of every base rather than over the
first one's members - with `__vmi_class_type_info` beside it, so that a
`catch` by a base at a non-zero offset lands where the object is.

**That Windows defect is closed as of 1.2**: a function other than `main` that
owned an unwind region used to emit a `.pdata` entry pointing at a `$cppxdata`
label the compiler never laid down, and `ml64` answered
`A2006: undefined symbol`. Both spellings take the answer from one place now,
and `by-value-parameter-unwind.cpp` is the case that keeps it that way.

**`-masm=ml64` cannot link a program of more than one file**, and that is
ml64's limit rather than this compiler's. A member function defined inside its
class becomes a strong symbol in every object that includes the header - this
tree's own `<string>` defines its members that way - and folding them needs a
COMDAT section, which **ml64 has no directive for at all**. `link.exe` answers
LNK2005 for each duplicate.

The two other spellings both mark it: `gnu`, which clang turns into an ordinary
MSVC-ABI COFF object (the default since 1.2 where no masm sits beside the
compiler, and what `-g` needs, CodeView living in that spelling alone), and
`masm`, which the project's own masm assembles with the COMDAT written as cl
writes it and the project's link links (the default in RIDE's `bin`, where both
sit beside `cpp11.exe`). So a multi-file Windows project links through either.
`-masm=ml64` is for a machine with neither clang nor the project's masm, and
`examples/unity.cpp` shows the single-translation-unit build it needs. The
`masm` spelling's own gaps - ten of 379 cases refused by the project's masm on
2026-10-08 - are workstream F's in `docs/REVIEW-PLAN-2026-10-08.md`.

This is still why most `.nocl` and `.nonames` entries in `tests/cases/` exist:
cl folds an inline definition into a COMDAT and drops it when nothing calls it,
where cxx1 emits it.

**One program is known to answer differently from clang**, kept in
`tests/open/` with clang's answer beside it: a name declared after a template
is found inside it, where two-phase lookup would refuse it - the replay design
of rung 5, recorded in `docs/CONFORMANCE.md`. The five that stood here before
1.3 are closed and are suite cases. `make open` runs it and gates nothing.

**Two smaller ones worth knowing.** A static local's symbol is spelled
`f.name` rather than the Itanium `_ZZ1fvE4name` - nothing links against it, a
static local having no linkage, but a debugger will show the other name. And
`Driver::runTool` names a temporary and then renames it, which on the Windows
box can collide in a one-shot multi-file build: compile with `-c` and link
separately if that appears.

**What is verified, and where.** Every release is held to `tools/verify-three`:
the four suites on macOS, the same on a Linux box with real g++, the case suite
at -O0 and -O2 plus a name-by-name comparison against `cl` on a Windows box, and
the C6747 leg on TI's own simulator. `make test` is the local half of that, and
on the Windows box `tools/windows/names-overload.cmd` runs the two clang-asking
suites under Git's bash with Visual Studio's clang. The sources this release was
built from are sealed - see `README.1ST`, section 6.

**The four suites are four different questions, and the numbers mean different
things.** `tests/run.sh` compiles and runs every case on this machine: on
2026-10-13 that is 702 checks, of which **453 have a recorded output** and must
print it, and **248 have a recorded refusal** and must stop with that message -
the second number is the compiler saying no where it should, which proves the
refusal names the feature and nothing about what compiles; `run.sh` prints the
two apart. `tests/emit.sh` compiles every case for all four targets and stops at
assembly; its *golden* is a recording of what this tree emitted before a change,
read file by file after it, and **it is not an oracle** - it never fails the run
and says only what moved, which is what a refactor needs and no correctness
claim may rest on. `tests/names.sh` and `tests/overload.sh` ask clang on every
run, and are the two that compare this compiler against another. The optimizer
has no IR verifier; what gates it is `run.sh` at -O2 on the Linux box and the
Windows cases at -O2 (since 2026-10-07), `tools/identical.sh` across levels, and
`CPP11_C6XLIVECHECK` on the C6000 - a `Mir` consistency check (every use after
its definition, every label a target) is a later round, recorded in
`docs/review-2026-10-08/ws-a.md`.

**What the review of 2026-10-08 measured, for the record.** The compiler builds
clean from `src/` with g++ 11 under `-Wall -Wextra -Werror -pedantic`; its
x86_64-linux assembly for about 70 probe programs and a 16-file, 14,000-line
project assembles with clang and links with lld; its arm64-darwin output,
transliterated to ELF, runs correctly on real AArch64 hardware for about 60
probes - lambdas, virtual, multiple and virtual inheritance, RTTI, member
pointers, templates, nested exceptions, rethrow and cleanups; its tms6747
output goes through the project's own `asm6x` for 397 of 397 suite cases; and
`-j` is deterministic, byte-identical output on all four targets whatever the
thread count. The same review looked for fabricated output and for a silent
fallback to cl, gcc, ml64, cl6x or lnk6x in this compiler and its seven sibling
tools and found none - zero `system()`, `popen()` or spawn sites in their
sources; the one "use the vendor's tools?" fallback in RIDE is prompted, logged
and refused in a headless build.

## What it is not

**It cannot compile itself, and never will.** `src/` is C++14 and `cxx1`
accepts C++03 with C++11 extensions, so the source is out of reach of the
program by construction. Correctness is established by differential testing
against gcc, clang and cl rather than by bootstrapping.

**It is not a conforming implementation**, and the headline says so: the
language is C++03 plus the C++11 features `docs/EXCLUSIONS.md` does not list,
with the library above rather than the standard's. A refusal that names a
feature is the intended answer; a refusal naming nothing is a defect, and
finding those is what the sweeps in `CLAUDE.md` are.

## Where it came from

Forked from Compiler-C, a C90 compiler sharing the three code generators, on
2026-08-26.
