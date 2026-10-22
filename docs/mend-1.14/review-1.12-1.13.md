# Review of cpp11 1.12 and 1.13 (`7a323ae..ddb9c13`, `src/` and `include/`)

Independent review, 2026-10-17, of the two releases Opus 5.5 wrote and gated without one.
Method: the diff read whole, then one minimal C++11 program per edge of each change, every
program validated by `clang++ -x c++ -std=c++11 -pedantic-errors`, its accept/reject and
output compared with `cpp11.exe`'s on the Mac (arm64-darwin), names compared with
`tools/mangled-names` for all three targets, and each difference re-run through a 1.11
compiler built from `7a323ae` to tell a regression from an older fault. The probes and the
harness are in `review-probes/` (`probe.sh`, `results.txt` is the full run: 37 agree, 24
differ, of which 24 are listed under a finding or explained below). Baseline before any
probe: `make test` green - run.sh 730/0, emit 1853/0, names 474/0, overload 33/0,
exclusions 129 sites 0/0, comments 0.

**Verdict: 1.12 - block. 1.13 - block.** 1.12 for finding 1, a regression from 1.11 that
turns a template parameter into the wrong type or value whenever an enclosing namespace or
class declares a name of the same spelling - ordinary code, and the non-type case is silent.
1.13 for finding 2, a segfault on a class that inherits constructors and has a virtual base of
its own, which the compiler accepts. The rest of both releases held up well: inheriting
constructors forward references, rvalue references, arrays, member pointers, defaults,
access and `explicit` correctly and balance under the ledger; `template <class T, T v>` binds
and mangles correctly for every integral type but one spelling; the nested-class deferral and
the base's member template are right in every shape tried.

## Findings

### 1. BLOCK (1.12 regression) - a template parameter is hidden by an enclosing scope's name

```cpp
namespace n {
    typedef double T;
    template <class T> struct Box { T v; Box(T x) : v(x) {} T get() const { return v; } };
    int run() { Box<int> b(21); return b.get(); }
}
```
clang: compiles, `21 4 4`. 1.11: `21 4 4`. **1.12/1.13: `'Box<int>' declares no member
'Box<int>' with these parameters`** - inside the replayed body `T` is `n::T`, double. The
function-template form (`template <class T> T twice(T)` beside `typedef double T;`) is
`'T' appears in no parameter`, and a member template whose parameter is named like a
typedef of an *enclosing class* fails the same way (`l01d`). Both were correct in 1.11.

The non-type half is **silent**:
```cpp
namespace n {
    enum { N = 100 };
    template <int N> struct S { int a[N]; int size() const { return N; } };
    int run() { S<3> s; return sizeof(s); }      // clang 12, 1.11 12, 1.12/1.13: 400
}
```
`l02`: cpp11 prints `9 3 400` where clang and 1.11 print `9 3 12` - the array is laid out with
the namespace's `N`. Any namespace-level `N`, `T`, `U`, `size_type` beside a template using
the same letter does this.

**Cause.** `bindTemplateParameters` enters a bound type parameter in the flat typedef table
under its bare name (`src/parser/ParserTemplate.cpp:218`) and a non-type one in the flat
enumerator table (`:238`, `:244`). 1.12's lookup order asks the enclosing classes and
namespaces *before* that table: `findTypedef` at `src/parser/Parser.cpp:279-280`
(`classScopeType`, then `enclosingNamespaceKey`), `findEnum` at `:366`, `findGlobal` at
`:951`. [temp.local]/6-7 and [basic.scope.hiding]: the parameter's scope is nested inside
the namespace's and hides it.

**Fix.** Ask whether the name is a bound template parameter before asking the enclosing
scopes: the parser already counts them (`boundTypeParams_`, and the `Shadow` list knows the
bound non-type names), so `findTypedef`/`findEnum` can return the flat entry first when
`boundTypeParams_[name] > 0` (and a sibling counter for non-type parameters). Add
`l01b`, `l01d` and `l02` as cases; the suite has no template parameter spelled like an
enclosing name, which is why 730 cases stayed green.

### 2. BLOCK (1.13) - `using B::B;` in a class with a virtual base of its own segfaults

```cpp
struct V { int v; V() : v(9) {} };
struct B { int a; B(int x) : a(x) {} };
struct D : B, virtual V { using B::B; };
int main() { D d(5); return d.v; }        // clang: 9. cpp11: compiles, SIGSEGV
```
`i11b`. The refusal at `src/parser/ParserType.cpp:585` covers only inheriting *from* a virtual
base; a virtual base elsewhere in D is accepted and miscompiled.

**Cause.** In `synthesizeDefaultCtor` the inherited-constructor branch
(`src/parser/ParserClass.cpp:3237`) makes `_ZN1DCI11BEi` an alias label on the CI2 body,
which skips the virtual bases by design (`if (bs[i].isVirtual) continue;`), where the
ordinary branch beside it (`if (type->hasVirtualBase())`, ~`:3250`) splits C1 off to build
them and store the vptrs through the VTT. The emitted `x86_64-linux` assembly shows it:
`_ZN1DCI11BEi:` and `_ZN1DCI21BEi:` on one body that calls `_ZN1BC2Ei` and never
`_ZN1VC2Ev`, so a complete `D` has no `V` and no vbase offsets.

**Fix.** Give the inherited constructor the same C1/C2 split (CI1 builds the virtual bases,
stores the vptrs, calls CI2 with the forwarded parameters), or refuse by name for a class
with any virtual base until it does. The Microsoft side (`??0D@@QEAA@H@Z`) compiles with cl's
hidden flag path and was not run here - the box was not in reach of this review; it should be
before the fix is believed.

### 3. SHOULD-FIX (1.12; a claim in README/CLAUDE.md is untrue) - the nearest scope still loses outside the namespace block

README "What 1.12 changes" and CLAUDE.md "Version 1.12" say the nearest scope's name wins and
the `docs/CONFORMANCE.md` entry is gone. It is gone, and the rule still fails wherever a body is
read outside the `namespace { }` block:

| probe | shape | clang | cpp11 (1.11 same) |
| --- | --- | --- | --- |
| `l03` | `int outer::C::h() { return E; }`, E outer's enumerator | 5 | `'E' was not declared` |
| `l09` | `int a::b::L::m() { return k + 7; }`, k in a and at file scope | 9 | 8 |
| `l10` | `int n::S::m = v;` and `int n::S::get() { return v; }`, v in n and globally | 5 5 | **99 99** |
| `l11` | `n::S<double>` and `n::tf(0)` instantiated from `main`, bodies naming n's k, E, T | 2 2 2 1 2 2 | **1 1 1 1 1 1** |

**Cause.** Only a qualifier that *is a namespace* swaps `namespaceStack_`
(`src/parser/ParserTopLevel.cpp:242-262`); `outer::C` is a class, so an out-of-line member's
body, a static member's initialiser and a template's replay in `instantiatePending` run with
the stack of wherever the parse happens to be. The class scope's enclosing namespace is never
consulted.

**Fix.** Derive the enclosing namespace path from the class's tag (or record it on `Type` at
definition) and swap for the member body and the static member's initialiser; record the
namespace path on `TemplateDecl` and swap during the replay. Until then the CONFORMANCE entry
should come back, narrowed to these shapes.

### 4. SHOULD-FIX (1.12) - a default argument on an out-of-line definition reads file scope

```cpp
int k = 131;
namespace outer { int k = 37; int g(int); }
int outer::g(int x = k) { return x; }      // clang: outer::g() is 37. cpp11: 131
```
`l08` (the declaration-side default, `int f(int x = k);` inside the block, is right).

**Cause.** `defaultArgNamespace_[f.symbol] = namespaceStack_` is written in
`declareFunction` (`src/parser/ParserClass.cpp:4331`, `:4380`), which runs after
`inNamespace.leave()` at `src/parser/ParserTopLevel.cpp:760`, so the replay scope recorded is
the file's. **Fix:** record `definedIn` for the defaults, or keep the swap on through the
`declareFunction` call.

### 5. SHOULD-FIX (1.13) - a constructor template of the base is not inherited, and the refusal names nothing

```cpp
struct CT { int v; template <class U> CT(U u) : v((int)u) {} CT(int, int) {} };
struct DCT : CT { using CT::CT; };
int main() { DCT d(7.9); }     // clang: ok. cpp11: no function called 'DCT::DCT' takes these 1 argument(s)
```
`i05b`. `declareInheritingCtors` (`src/parser/ParserClass.cpp` ~`:3010`) reads
`overloadsOf(constructorKey(base))` only; a constructor template lives in `memberTemplates_`
under the constructor key and is skipped silently. **Fix:** inherit it (the 1.5 constructor
template machinery is there), or refuse by name so `tools/exclusions` can list it.

### 6. SHOULD-FIX (1.12 over-acceptance) - a private base's member template is reachable from outside

```cpp
struct B { template <class T> T twice(T x) const { return x + x; } };
struct D : private B { };
int main() { D d; return d.twice(4); }   // clang: 'twice' is a private member. cpp11: compiles, 8
```
`m05`; 1.11 refused it (`'struct D' has no member 'twice'`). **Cause:** `memberTemplateOwner`
(`src/parser/Parser.h:468`) walks `declaringClassOf` (`src/parser/ParserExprCall.cpp:587`),
which checks no access, and `memberTemplateCall` then converts `this` through the base
(`src/parser/ParserTemplate.cpp:2449`, and the `convert()` below it) without asking
`baseStepAccessible`. The member's own access (`memberTemplateAccess_`) is not checked either.
**Fix:** check the path from `named` to `plain` with `accessibleBaseOffset`, and the member's
access as `memberCallWith` does. (`m04`, the same shape from inside D, is right.)

### 7. SHOULD-FIX (1.13) - a non-type argument is not converted to `T`

```cpp
template <class T, T v> struct K { static const T value = v; };
int x = K<bool, 7>::value;   // clang: cannot be narrowed to 'bool'. cpp11: compiles, value is 7
```
`t01b`: `(int)K<bool, 7>::value` prints **7** and `K<char, 300>` is taken. [temp.arg.nontype]/1
wants a converted constant expression of the parameter's type, which forbids narrowing.
**Cause:** `templateArguments` reads `constantExpression` into a `long long`
(`src/parser/ParserTemplate.cpp:1040`) and never checks it against `pt`. **Fix:** refuse
where the value does not round-trip through `pt` (a `bool` other than 0 or 1 included).

### 8. SHOULD-FIX (1.13) - an `unsigned long long` argument past 2^63 is spelled negative on Itanium

`t05`: `void fu(IC<unsigned long long, 18446744073709551615ULL>)` is `_Z2fu2ICIyLyn1EE` where
clang writes `_Z2fu2ICIyLy18446744073709551615EE`; Microsoft agrees on all eight names.
**Cause:** the value is held as `long long` and spelled signed at `src/Mangle.cpp:423-427`.
**Fix:** spell by the argument's type - an unsigned type as `std::to_string((unsigned long
long)value)`. The run-time value is right (`t01` prints `18446744073709551615`); only the
linkage name is.

### 9. SHOULD-FIX (pre-existing, on lines 1.12 touched) - a member template beside an ordinary member of the same name, called from outside

```cpp
struct S { int f(int) { return 1; } template <class T> int f(T) { return 2; } };
int main() { S s; return s.f(1); }       // clang 1 (the non-template wins a tie). cpp11 2
```
`m03c` prints `2 2` for clang's `1 2`. And where a bare-name call inside the class had
already instantiated and pruned the losing `f<int>`, the same outside call references it and
**the link fails**: `m03`, `Undefined symbols: int S::f<int>(int)` (1.11 too).
**Cause:** the `.` and `->` paths (`src/parser/ParserExpr.cpp:2192`, `:2274`) dispatch to
`memberTemplateCall` whenever a template of the name exists, never ranking the ordinary
overloads; the bare-name path inside the class does rank them (the `alone` test). **Fix:**
where ordinary overloads exist, go through resolution with the template as a candidate, as
the bare-name path does.

### 10. NIT (1.13 over-acceptance) - a partial specialization on a dependent-typed value

`template <class T, T v> struct S; template <class T> struct S<T, 0> {};` is ill-formed
([temp.class.spec]/8, a non-type argument specialising a parameter of dependent type); clang
refuses, cpp11 accepts (`t02`). Harmless until someone leans on it.

### 11. NIT (pre-existing) - a relative qualifier inside a namespace block

`namespace a { namespace b { int f(); } int b::f() { ... } }` is `'b' is not a class` (`l13`,
1.11 too): `src/parser/ParserTopLevel.cpp:243` looks the qualifier up as written in
`namespaces_`, which holds `a::b`. The message names nothing about namespaces.

## What was probed and held

Each of these agrees with clang in accept/reject and output; names agree with clang on all
three targets except the recorded C2-only shape for inline constructors on x86_64-linux.

**Lookup order (1.12).** A using-directive's name loses to the enclosing namespace's (`l04`);
a data member, a class enumerator and a class typedef hide the namespace's, directly and
through `[this]` (`l05`); a parameter and a local hide the namespace's (`l07`); inline member
bodies of a class in a namespace see the namespace's `k` and `E` (`l12`, which 1.11 got wrong:
`1 1 0`); an out-of-line namespace *function*'s parameters, body, a local class in it and a
lambda in it see the namespace (`l08` f/h, `l09` f/g/h); a parameter typed by the namespace's
class, `int outer::h(S s)` (`l08`). `::k` in an expression is refused by name as recorded
(`l06`), not a finding.

**Inheriting constructors (1.13).** `i01` under the ledger: `int`, `(int, double, const char
*)`, `long &` (the referent incremented), `M &&` (moved from), `const M &` with a default
argument, `int (&)[3]`, `int B::*`, D's implicit copy, `new`/`delete` - every object built
once and destroyed once, output identical. Access and `explicit` travel with the constructor
(`i02b`, `i02c` refused as clang refuses). Two bases each inherited (`i03`); the same signature
from two bases is ambiguous (`i04`); a dependent base, `using Base<T>::Base;` (`i05c`); a chain
`E : D : B` with D's own `D(int)` hiding B's, `CI1`/`CI2` named for B (`i06`, `i09`); an
inherited default constructor beside D's own `D(int)` (`i07`); a vptr, a member initialiser and
a second base all set (`i08`); a trivially copyable class by value (`i10`); a default argument
evaluated in B's scope - its enumerator and static member (`i12`); copy-initialisation,
braces, `new D{}`, a `D` argument (`i13`); a base in a namespace, qualified both ways (`i14`); a
member with no default constructor refused (`i15`); a class template inheriting a non-dependent
base with a reference parameter (`i16`); the ledger with a class member and a user copy
constructor (`i17`).

**`template <class T, T v>` (1.13).** T bound to `bool`, `char`, an unscoped and a scoped
enumeration, `short`, `unsigned`, `long long` (negative) and `unsigned long long` (the full
range) - values, `sizeof` and comparisons right (`t01`, `t09`); valid partial and explicit
specializations over `<T, v>` (`t02b`); deduction from `integral_constant<T, v>` and explicit
`get<long, 3>()` (`t03`); defaults `T = int, T v = 7`, `T v = T()`, `T w = v + 2` (`t04b`);
`<type_traits>` as a program uses it - `true_type` dispatch, `is_same` on
`integral_constant` instances, `value_type` (`t06`); a member template `<class U, U u>` and
`<T v>` over the outer T inside a class template (`t07`); `T *p` refused by name (`t08`).
Names for `bool`, `char`, enum, `short`, negative `long` arguments agree (`t05`).

**Member templates and nested classes (1.12).** A base's member template at an offset, by
bare name, `B::twice`, `this->twice`, a non-type one, through a const object and a further
derived class (`m01`); a static member template every way, including from a static member
function and `S::twice` from a derived class's static (`m02`); from inside a private base
(`m04`); a class template's nested class with out-of-line constructor, destructor, member,
static member and a nested-nested member, called only from another deferred body, two
instantiations' statics apart (`n01`); an inline member called only from a nested-nested body
(`n02`); a nested class with a virtual function through a base reference (`n03`); one of two
statics used (`n04`); the nested type as a parameter, through `new`, a static named only from
inside (`n05`).

**Not reached by this review.** The Windows box: inheriting constructors' hidden flag and
cl's `this`-in-RAX on the inherited path compile but were not run; `names` agree there.
The tms6747 target was not run.

## Where the two releases stand

- **1.12**: finding 1 is a regression that 1.11 did not have and no suite case reaches; it
  wants a case and the fix before the number is kept. Findings 3 and 4 are the feature's own
  claim not holding one door over, and 6 and 9 are its touched lines. The nested-class and
  base-member-template work is sound.
- **1.13**: finding 2 is a crash on a program the compiler accepts. Findings 5, 7 and 8 are
  edges of the two features; the core of both is correct and measured.
