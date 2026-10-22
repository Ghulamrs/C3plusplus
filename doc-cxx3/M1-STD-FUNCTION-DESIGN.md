# `std::function` for `include/functional` - design, Fable 5.1, 2026-10-23

**What this is.** The library half of M1's root L1: a `std::function` that cxx3 itself compiles,
that `src/` can use at its fourteen sites unchanged, and that costs what the design's R6 allows. The
language half - a function type as a template argument - is a dependency, named in section 5 with
what it must do, measured. Every fact below says how it was measured; the probes are in the
scratchpad (`.../scratchpad/review/probes/erase*.cpp`, `pack*.cpp`, `mangle/m.cpp`).

## 1. How `src/` uses it, every site

| site | signature | stored, copied, called |
| --- | --- | --- |
| `OptIr.h:82` | `EntryOf<Instr, std::function<void(Spelling &)>>` - a member of **every** `Entry`, instruction entries included | default-constructed per entry; moved on `vector<Entry>` regrowth (`include/vector` moves, 1.27); **copied** in `OptJumps.cpp:114` (`struct Insert { int at; Entry e; }`), `OptDivide.cpp:78`, `OptHoist.cpp:398/500`, `OptAlign.cpp:137`; **assigned from a lambda** in `OptPipeline.cpp:162` and `OptAlign.cpp:140`; called once at `Optimizer.cpp:224` (`e.event(under_)`) |
| `Optimizer.h:122`, `Optimizer.cpp:46` | `void event(std::function<void(Spelling &)> call)` | by value from 24 lambdas (`[=]` capturing ints, bools, `std::string`s), moved into the entry |
| `Optimizer.h:26`, `Optimizer.cpp:111` | `void defer(std::function<void()> call)` | **captured by copy** inside another lambda: `event([call](Spelling &) { call(); })` |
| `X86_64Linux.h:125` | `void atOutput(std::function<void()> f)` | `std::move(f)` into `defer`, or called at once; six callers |
| `CodeView.h:32` | `typedef std::function<std::string(const std::string &)> Spell;` | by value, called per record |
| `Parser.h:1811`, `ParserClass.cpp:2284` | `const std::function<int()> &declareLate` | a `const &` parameter, called once |
| `Parser.h:2091`, `ParserStmt.cpp:1421` | `const std::function<StmtPtr()> &bodyFn = nullptr` | **default argument `nullptr`**; tested `if (!bodyFn)`, then `bodyFn ? bodyFn() : block()` - a `StmtPtr` is a `unique_ptr`, so the return is **move-only** |
| `ParserClass.cpp:2309` | `std::function<void(const Type *, long long)> readList = [&](...) {...}` | **recursive**: the lambda names `readList` in its own body through `[&]` ([basic.scope.pdecl], landed 2026-10-06) |
| `C6xPipe.cpp:1392`, `OptFrame.cpp:58` | `const std::function<void(int)> &f`, `const std::function<void(Candidate &)> &f` | a lambda parameter of a lambda, called in a loop |

**Nothing in `src/` compares a `std::function` with `nullptr`, converts one to `bool` except
`!bodyFn`, or asks `target()`/`target_type()`.** Everything is construct-from-callable, copy, move,
assign-from-lambda, call, and one `nullptr` default. That decides what is built first and what is
left out by name.

**Where the allocations are.** `Optimizer::event` allocates only inside a function
(`if (!inFunction_) { call(under_); return; }`), so a unit's data events cost nothing; inside a
function the events are the prologue, `calleeSaves`, `noteHasEh`, `loopAlign`, and - under `-g` -
`location` once per statement. The `Entry` copies in the passes are the only other source. So the
count of allocations per compile is "one per event held, plus one per copied event entry", small
without `-g` and linear in statements with it. **That is a claim and wants its oracle**: when the
header lands, a counter behind `CPP11_FN_ALLOCS=1` (or the memory gate's `--include` mode below)
prints the number on one compile of `Parser.cpp`, and the number goes into this file.

## 2. The class, and why it is three words

```cpp
template <class Signature> class function;                 // declared, never defined
template <class R, class... Args> class function<R(Args...)> {
    typedef __fn_ops<R, Args...> Ops;                       // class-scope typedef: see section 5, L16
    R (*call_)(void *, Args...);
    void *(*manage_)(__fn_op, void *);
    void *target_;
public:
    typedef R result_type;
    function() noexcept;                     function(std::nullptr_t) noexcept;
    function(const function &);              function(function &&) noexcept;
    template <class F> function(F f);
    ~function();
    function &operator=(const function &);   function &operator=(function &&) noexcept;
    function &operator=(std::nullptr_t) noexcept;
    template <class F> function &operator=(F f);
    void swap(function &) noexcept;
    explicit operator bool() const noexcept;
    R operator()(Args... args) const;
};
template <class R, class... Args> bool operator==(const function<R(Args...)> &, std::nullptr_t) noexcept;  // and the three mirrors
template <class R, class... Args> void swap(function<R(Args...)> &, function<R(Args...)> &) noexcept;
class bad_function_call : public exception { const char *what() const noexcept override; };
```

**Type erasure by two function pointers and an owned object**, the libstdc++ shape without its
small buffer: `call_` is the thunk `R (void *, Args...)` for the stored `F`, `manage_` clones or
destroys it under an opcode, `target_` is the `F` on the heap or null. Three words, 24 bytes.

**Why no small-buffer optimisation.** `Entry` carries one `function` per *instruction* and nearly
every one is empty: a 16-byte buffer would add 16 bytes to every entry of every stream for the few
that are events, and a captured `std::string` (the prologue's `lsda`, `globl`'s `name`) does not
fit any buffer a plain function pointer does. libc++'s object is 48 bytes (measured, arm64 host),
MSVC's 64; this one is 24 and allocates once per non-empty object. The copy cost that remains is
per copied *event* entry, which section 1 counts in the handful. If the counter in section 1 says
otherwise, a buffer for a function pointer alone (8 bytes, no extra size - it fits where `target_`
is) is the one worth adding.

**The manager with an opcode** (`__fn_op { __fn_clone, __fn_destroy }`) rather than two pointers is
what keeps it at three words; `target_type`/`target` would be a third opcode and are left out
(section 4).

**The helpers live at namespace scope**, as `__fn_ops<R, Args...>` holding non-template statics
over one `F`... except that they cannot, today: see section 5, L16. The header's shape is fixed by
that dependency, and `__fn_model<F, R, Args...>` with `static R call(void *, Args...)` and
`static void *manage(__fn_op, void *)` is what the constructor template names
(`&__fn_model<F, R, Args...>::call`) once L16 lets a member template see the class's pack. Probe
`erase6.cpp` is that header over a stand-in key `Fn<R, Args...>`; it is exactly right under clang
(`6 8 30 30 0 1 1 / 6 0 / 8 / 11 / live 2 4 5 / live 1 0 / live 0`) and fails in cxx3 only at L16.

## 3. Each member, and what it does

| member | what it does | note |
| --- | --- | --- |
| `function()`, `function(nullptr_t)` | three null words | `nullptr_t` non-explicit, so `= nullptr` as a default argument and `bodyFn = nullptr` work |
| `template <class F> function(F f)` | `target_ = new F(std::move(f))`; `call_ = &__fn_model<F,R,Args...>::call`; `manage_ = &...::manage` | takes a lambda, a function pointer, a functor, another `function` of a different signature. A *function pointer* `F` is stored on the heap too (one word boxed) - the buffer-for-a-pointer refinement of section 2 is where that would change. **Not** SFINAE-constrained on callability ([func.wrap.func.con]/7's "shall not participate" is C++14 wording; C++11 has no constraint) |
| `function(const function &o)` | `o.manage_ ? o.manage_(__fn_clone, o.target_) : 0` | the clone is `new F(*static_cast<F *>(p))`, so `F`'s copy constructor runs - a captured `std::string` is copied, not shared |
| `function(function &&o)` | steal the three words, null `o` | no allocation; this is what `vector<Entry>` regrowth and `std::move(call)` take |
| `~function()` | `if (manage_) manage_(__fn_destroy, target_)` | `delete static_cast<F *>(p)` |
| `operator=(const function &)`, `(function &&)` | copy-and-swap, move-and-swap | `fn.stream[k].event = [=](Spelling &sp) {...}` goes through `template <class F> operator=(F)`, which builds `function(std::move(f)).swap(*this)` |
| `operator=(nullptr_t)` | `function().swap(*this)` | |
| `swap` | three `std::swap`s | `std::swap` is `<utility>`'s |
| `explicit operator bool` | `manage_ != 0` | `!bodyFn` is a contextual conversion, which cxx3 has had since `nullptr` landed |
| `operator()(Args... args) const` | `if (!call_) __throw_bad_function_call(); return call_(target_, std::forward<Args>(args)...);` | **`const`**: `defer`'s `[call](Spelling &) { call(); }` calls through a const closure member. `Args` are taken as declared ([func.wrap.func.inv]: `Args&&...` is C++11's wording, which cxx3 reads - forwarding references landed in WS-E1) |
| `== nullptr`, `!= nullptr`, both orders | `!f`, `bool(f)` | the four free templates; none of `src/` uses them, `include/` gets them for completeness and the case holds them |

**`bad_function_call`** derives from `std::exception` and is declared in `<functional>`, which
includes `<exception>`; its `what()` is defined inline (the platform runtime defines its own
`std::bad_function_call` in libc++/libstdc++, but cxx3's `<exception>` already keeps its own
`std::exception` on the hosts over the runtime's vtable, and nothing in cxx3 links against a
runtime's `bad_function_call` - section 6). **It is thrown on every target.** The brief says a
class cannot be thrown on x86_64-windows; that is stale: `throw std::out_of_range("x")` with a
`catch (const std::exception &)` compiles for `-arch x86_64-windows` with `cxx3.exe` at `3b7ff04`
(probe `winthrow.cpp`), and `include/vector:46` has thrown `out_of_range` on every target since
1.27. Whether it *runs* on the box is the box's to confirm with the case; if it does not, the
fallback is `include/string`'s own device - `__throw_bad_function_call()` declared in the header
and defined at the end to print `what()` and `abort()` under `_WIN32` - and the case's `.notarget`
says so. `std::terminate` is not the right answer: the standard says throw, and a wrong-platform
difference belongs in `docs/CONFORMANCE.md`, not in a quiet abort on every target.

## 4. Left out, each by name

| not built | why, and what the header says |
| --- | --- |
| `target_type()`, `target<T>()` | they need `typeid(F)` kept per model - a third opcode returning `&typeid(F)` - and a `type_info` comparison; `typeid` works here (`tests/cases/typeid.cpp`), so it is a cost rather than a wall: one word of RTTI per instantiated `F` (Itanium emits the `_ZTI` on demand) and `<typeinfo>` in every unit that includes `<functional>`. **Nothing in `src/` asks**, so they are left out with a `static_assert`-style named refusal, as the current header refuses `function` itself |
| `std::bind`, placeholders, `std::ref`/`reference_wrapper`, `mem_fn` | separate features, still refused by name as today's header refuses them |
| `function(allocator_arg_t, ...)` | C++11 has them, every implementation deprecated them; left out by name |
| a buffer for small targets | section 2; added only when the allocation counter says to |
| `function<R(Args...) const>` and friends | not C++11 |
| the primary `template <class> class function;` | **declared and not defined.** `std::function<int>` is then an incomplete type where it is used - clang's answer - rather than today's `static_assert`. Keep today's `__cxx1_never` assert as the primary's *definition* instead if a message naming the mistake is preferred: both are one line, and the message is better |

## 5. What the language half must do, measured

Each of these was found by writing the header one way and watching where cxx3 stopped; each is
valid C++11 by clang, and each is a dependency, not a wish.

**L1a - parse the type.** `std::function<void(Spelling &)>`: a type-id that is a *function type*
inside a template argument list, `R(Args...)`, with `R(Args..., ...)` for a variadic one, and
nested (`function<void(function<void(int)>)>`). Refused by name at `ParserTemplate.cpp:1310`/`2649`.
`TypeTable` already interns a function type for pointers to functions; the argument is that type
bare. **The `>>` of `EntryOf<Instr, std::function<void(Spelling &)>>` must still split**, which it
will once the inner list is read rather than refused.

**L1b - match and deduce through a function type.** `matchPattern` (partial specialization,
[temp.deduct.type]) and `deduceOne` (calls, [temp.deduct.call]) do not descend into a function
type's return and parameters: `template <class R, class A> struct F<R (*)(A)>` is refused
"'R' is never used in this specialization's arguments" (probe `erase3.cpp` - a *pointer* to a
function, no pack, so this predates L1a), and `template <class R, class... A> int arity(R (*)(A...))`
given `int (int, char)` fails deduction "takes a different number of parameters from 'template
parameter (parameter pack expansion)'" (probe `arity.cpp`; clang `2 30`). The partial specialization
`function<R(Args...)>` is exactly the pattern: a function type whose return is a parameter and
whose parameter list is a pack. Both walkers need the case: return type, then the parameter list
with a trailing pack taking the rest, varargs matched to varargs.

**L16 - a class template's pack inside its member templates.** `template <class F> function(F f)`
is a member template of `function<R(Args...)>` and must name `Args...` (to pick
`__fn_model<F, R, Args...>::call`). Today the pack reads as **empty** there, silently: probe
`pack2.cpp` prints `0` where clang prints `2`; the loud form is `erase6.cpp`'s
"`__fn_model<int (*)(int),int,{}>` has no static member 'call'". The second binding layer
(`instantiateMemberTemplate`, CLAUDE.md "Member function templates, and the two binding layers")
records the class's `instantiatingParams_`/`Binding_`/`Values_` and not `packs_`. **Every route
round it is itself refused**: a class-scope typedef of the model needs `F`, which only the member
template has; a member *class* template `Model<F>` is refused by name (`erase4.cpp`); a static
member function template over the pack loses the pack the same way (`pack5.cpp`, `pack7.cpp`); a
namespace-scope partial specialization re-deducing the pack from a `__fn_sig<R, Args...>` argument
is refused by name - "a pack expanded inside a nested template-id in a partial specialization's
arguments" (`erase7.cpp`); and deducing the pack from the member's own `R (*&)(void *, Args...)`
is L1b (`erase8.cpp`). So L16 is a hard dependency of the header, and a miscompile in its own right
that wants a case whether or not `std::function` lands.

**Also wanted, smaller:** `A::template f<T>` in an expression (`pack4.cpp`: `'O' was not declared`;
only `.template`/`->template` are read) - the header avoids it; and a `noexcept` on the members is
read and ignored today, which is fine.

**Nothing in the backends.** The header is ordinary code once the three above hold: a class with
three pointer members, calls through function pointers, `new`/`delete` of `F`. Checked by the
stand-in `Fn<R, Args...>` of `erase6.cpp` reaching the backend on the host and for
`-arch x86_64-windows` without a backend diagnostic (it stops at L16, in the parser).

### The manglings, measured

`clang++ -std=c++11 -S -O0` on the Mac, `-target x86_64-linux-gnu` and `-target x86_64-pc-windows-msvc`,
over `scratchpad/review/mangle/m.cpp` (a stand-in `std::function` declaration; the name does not
depend on the body). Nothing links against another compiler's `std::function` (section 6), but a
*function taking one* in a cxx3 object must have the name a clang or cl object calls it by, and
`tests/names.sh` compares every one of these:

| C++ | Itanium (clang) | Microsoft (clang) |
| --- | --- | --- |
| `void a(std::function<void(int)>)` | `_Z1aSt8functionIFviEE` | `?a@@YAXV?$function@$$A6AXH@Z@std@@@Z` |
| `void b(std::function<void(ns::Spelling &)>)` | `_Z1bSt8functionIFvRN2ns8SpellingEEE` | `?b@@YAXV?$function@$$A6AXAEAUSpelling@ns@@@Z@std@@@Z` |
| `void c(const std::function<int()> &)` | `_Z1cRKSt8functionIFivEE` | `?c@@YAXAEBV?$function@$$A6AHXZ@std@@@Z` |
| `void e(std::function<void()>, std::function<void()>)` | `_Z1eSt8functionIFvvEES1_` | `?e@@YAXV?$function@$$A6AXXZ@std@@0@Z` |
| `void e3(std::function<void(int)>, void (*)(int))` | `_Z2e3St8functionIFviEEPS0_` | `?e3@@YAXV?$function@$$A6AXH@Z@std@@P6AXH@Z@Z` |
| `void j(function<int(int, char)>, function<const int *(const char *, long)>)` | `_Z1jSt8functionIFiicEES_IFPKiPKclEE` | `?j@@YAXV?$function@$$A6AHHD@Z@std@@V?$function@$$A6APEBHPEBDJ@Z@2@@Z` |
| `void k(std::function<void(int, ...)> *)` | `_Z1kPSt8functionIFvizEE` | `?k@@YAXPEAV?$function@$$A6AXHZZ@std@@@Z` |
| `void l(function<void(function<void(int)>)>)` | `_Z1lSt8functionIFvS_IFviEEEE` | `?l@@YAXV?$function@$$A6AXV?$function@$$A6AXH@Z@std@@@Z@std@@@Z` |
| `void h(Box<void(int)> *)`, `Box` a struct | `_Z1hP3BoxIFviEE` | `?h@@YAXPEAU?$Box@$$A6AXH@Z@@@Z` |
| `std::function<void(int)> g;` | `g` (a variable, undecorated) | `?g@@3V?$function@$$A6AXH@Z@std@@A` |
| `int S::call(std::function<int(int, char)>)` | `_ZN1S4callESt8functionIFiicEE` | `?call@S@@QEAAHV?$function@$$A6AHHD@Z@std@@@Z` |

What the mangler has to learn, from those:

- **Itanium**: a function type as a template argument is `F <return> <params> E` bare, `v` for an
  empty parameter list (`FvvE`), `z` for varargs (`FvizE`) - the same `F...E` `PFviE` already
  writes. **It is a substitution candidate**: in `e3`, the plain pointer after it is `PS0_`, where
  `S_` is `St8function` (the template name, as every template-id's name is) and `S0_` is `FviE`;
  the whole `St8functionIFviEE` is `S1_`, which is what `e`'s second parameter writes. Nested, the
  inner `std::function<void(int)>` is `S_IFviEE` (`l`). `St` takes no slot of its own (CLAUDE.md,
  "`std` is written `St`").
- **Microsoft**: `$$A` then the function type's own encoding `6A<ret><params>@Z` - `6` function,
  `A` cdecl, `X` void, `H` int, `@Z` the end - with no `P6` since nothing points at it; varargs are
  `ZZ` in place of `@Z` (`k`). The class key is `V` (the standard writes `class function`), `U` for a
  struct holding one (`h`). A second mention is an argument back-reference (`0` in `e`); `std` is a
  name back-reference inside the outer list (`@2@` in `j`'s second parameter), and a nested
  template-id carries its own tables and spells `std` in full (`l`).

cl itself was not asked - the Windows box was not this review's - so the Microsoft column is
clang's second implementation; `tools/windows/names-vs-cl.cmd` is where it is confirmed.

## 6. ABI, memory, and the two rules

**ABI: nothing has to match.** `std::function` is a class template every program instantiates in
its own object; a cxx3 object never links against libc++'s, libstdc++'s or MSVC's `std::function`
(their layouts differ from each other anyway: 48, 32, 64 bytes). What must match is only the
*mangled names* of functions taking one, so that a cxx3 object and a clang or cl object agree on
`void f(std::function<void(int)>)` - section 5's table. `bad_function_call` is the one object with a
runtime twin: libc++abi exports `_ZTISt17bad_function_call` and its vtable, and a `catch
(std::bad_function_call &)` across a cxx3/clang boundary would compare `type_info`s by address on
Darwin. **Not needed**: the only thrower is cxx3's own header and the only catcher is a cxx3
program; the class is defined inline with cxx3's `std::exception` under it, exactly as
`<stdexcept>`'s nine are, and the name is `_ZTISt17bad_function_call`/`??_R0?AVbad_function_call@std@@@8`
whichever side emits it.

**Memory, R6.** The design's R6.1 forbids `new`/`delete` in the *compiler's* source and R6.6 lets
their count only fall; `tools/memory-gate` walks `src/` and nothing else (checked: `SRC = ROOT/src`;
baseline `new 1118`). The library is where ownership lives - `vector`, `unique_ptr`, `string` all
allocate there - and `std::function` is a library class that owns its target, so its one `new`
(the constructor and the clone) and one `delete` (destroy) sit in `include/functional` beside
`vector`'s and are accounted as the library's. **Make the accounting visible**: give the gate an
`--include` mode that counts `include/` separately with its own baseline, so the header's three
sites are a number that only goes down too, rather than a place the gate does not look.
`std::function` adds no `new` to `src/`: the fourteen sites exist already.

**Ownership is one object, deep.** A copy of a `function` is a copy of its target (the clone), so
an `Entry` copied in a pass carries its own closure and two events never share one; a move hands
the target over. There is no shared state and no reference count, which is what lets `Entry` stay
a plain value in a `std::vector` and lets the eventual handle-based IR (R6.3) drop the member
without a lifetime question.

## 7. The cases

Each `.expected` is clang's output of the same program (`clang++ -std=c++11 -pedantic-errors`);
each runs on the host, and `emit.sh` compiles each for all four targets. `tests/names.sh` compares
the manglings of `std-function-names` against clang on three targets.

| case | what it holds |
| --- | --- |
| `std-function-basic` | construct from a function pointer, a functor, a capturing lambda and `nullptr`; call each; `bool`, `== nullptr`, `!= nullptr` in both orders; `result_type`. Expected `6 8 30 0 1 1` |
| `std-function-copy-move` | under `tests/cases/lifetime.h`: a target with a counted constructor and destructor, copied, moved, assigned, swapped, reset by `= nullptr`; the ledger balances (`live=0 double=0 phantom=0`), the copy clones (two live after one copy), the move does not (two live after a move). Counts constructions as *live objects*, never as calls - the standing rule |
| `std-function-assign-lambda` | `f = [=](int) {...}` on an existing object, then on an empty one; `OptPipeline.cpp:162`'s shape |
| `std-function-in-lambda` | a `function<void()>` captured by copy in a lambda stored in a `function<void(int &)>` and called through the const call operator; `defer`'s shape |
| `std-function-recursive` | `std::function<int(int)> fact = [&](int n) { return n ? n * fact(n - 1) : 1; };` - the `readList` shape; expected `120` |
| `std-function-default-nullptr` | `void f(const std::function<int()> &g = nullptr)` tested with `!g` and called when given; `tryStatement`'s shape |
| `std-function-move-only-result` | `std::function<std::unique_ptr<int>()>` returning a move-only object; `bodyFn()`'s shape |
| `std-function-in-vector` | a `struct Entry { int kind; std::function<void(int &)> event; }` in a `std::vector` grown past its capacity and copied element-wise; every event still calls, the ledger balances - `OptIr.h`'s shape, and the one that measures the regrowth |
| `std-function-signatures` | `function<void()>`, `function<void(int, ...)>` is **not** attempted (the standard's partial specialization is `R(ArgTypes...)`, which a variadic function type does not match; measured, clang with libc++ refuses `std::function<void(int, ...)> f;` as "implicit instantiation of undefined template") - instead `function<const int *(const char *, long)>`, a reference parameter, a class by value, a nested `function<void(function<void(int)>)>` |
| `std-function-names` | the twelve declarations of section 5's table, compiled with `-S` for the three host targets and compared by `names.sh`; `.nonames`-free on all three |
| `std-function-empty-call` | an empty `function<int()>` called inside `try`, caught as `const std::bad_function_call &` then as `const std::exception &`, `what()` printed; expected clang's text. `.notarget x86_64-windows` only if the box says the throw does not run there |
| `std-function-target-refused` | `f.target_type()` refused by name, `.error` holding the message |
| `std-function-not-a-function-type-refused` | `std::function<int> x;` refused (incomplete, or the named assert); `.error` |
| `pack-in-member-template` | **L16's own case, independent of std::function**: `template <class R, class... Args> struct Fn { template <class F> Fn(F) : v(sizeof...(Args)) {} int v; };` and the `Box<Args...>::n()` form; expected `2 2`, where cxx3 today prints `0 0` with no diagnostic |
| `template-arg-function-type` | L1a/L1b's own case: `F<int(char)>`, `F<void()>`, `F<int (*)(int)>` partial specializations deducing `R` and the pack; expected from clang |

## 8. Order of work for L1, as this review sees it

1. **L16**, the pack in a member template - a miscompile, one place (`instantiateMemberTemplate`'s
   second layer takes `packs_` beside the bindings), its own case. Nothing in the header can be
   written round it.
2. **L1b**, the function type in `matchPattern` and `deduceOne` - return, parameters, trailing
   pack, varargs; the pointer-to-function form is a case the suite lacks today (`erase3.cpp`,
   `arity.cpp`).
3. **L1a**, the bare function type as a template argument in the parser and `TypeTable`, and the
   two manglers from section 5's table; `names.sh` is the gate.
4. **The header**, `erase6.cpp`'s shape over `R(Args...)`, with `bad_function_call`, the four
   `nullptr` comparisons, `swap`, and the two named refusals; then the memory gate's `--include`
   mode and the allocation counter's first number in section 1.
5. Rerun `self-compile.cmd`, both runs, on the box.
