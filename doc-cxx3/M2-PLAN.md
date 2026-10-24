# M2 step 1: Sema's interfaces extracted in place

Started 2026-10-24, from `214ad71`. Section 12 of `Compiler_Design_Final.md` says what M2 is; this
note says how step 1 is cut and how each commit is judged.

## What a commit may change

Nothing the compiler emits. Every commit of step 1 is a move. It is judged by:

| gate | where | what must hold |
| --- | --- | --- |
| `tools/identical.sh REF NEW` | Mac | every case and Compiler++ unit byte-identical at -O0, -O1 and -O2, for x86_64-windows in both spellings and x86_64-linux |
| `tests/emit.sh` against the golden recorded at `214ad71` | Mac | 0 changed (arm64-darwin, x86_64-linux, x86_64-windows) |
| `make test` | Mac, Linux (g++) | run, emit, names, overload, exclusions, comment cap |
| `tools/self-host` | Mac, Linux | stage 1 = stage 2 |
| `tests/tms6747.sh`, `tests/asm6x.sh` | Linux | at the end of a batch, not each commit |
| Windows leg | Windows box | **owed** while the box is offline: run over the whole series when it is back, before any step is called done |

Commits stay local until the Windows leg has passed them.

## How a module leaves the parser

The parser is one class: 679 member functions and 226 data members, every data member ending in `_`.
A module comes out in two moves.

1. **The interface.** The module is a class in `src/sema/`, namespace `sema`, held by value in the
   parser (`lookup_`) and built with a reference back to it. The functions of the module move into it
   with their comments; the parser's call sites call through the member. The bodies still read the
   parser's state, through the back-reference: the move changes where the code is, not what it does.
2. **The state.** The tables the module owns move into it, and the parser reaches them through the
   module. After this the back-reference shrinks to the scope context the module asks of the parser.
   This comes after all six interfaces, not module by module: `functions_` and the name tables are
   read by Lookup, Overload and Classes alike, and which one owns each is clearer once all six exist.

The symbol-table entry types (`Local`, `GlobalSym`, `Signature`, `DynamicSpec`, `TypedefName`,
`EnumConst`, `TableSizes`) go first, to `src/sema/Entities.h`. The interfaces return them, and a type
nested in `Parser` cannot be named before `Parser` is complete. The parser keeps a typedef for each,
so no parser code changes for it.

## Order

| commit | module | grown from |
| --- | --- | --- |
| M2.0 | `src/sema/`, the entry types | `Parser.h` |
| M2.1 | `Lookup`: the four name tables' lookups, using-directives and declarations, inline namespaces, ADL keys, class scope | `Parser.cpp`, `ParserOverload.cpp` (`lookupKeys`), `ParserClass.cpp` (`overloadsOf`), `ParserExprCall.cpp` (`findMemberOwner`) |
| M2.2 | `Overload`: ranking, best viable function, conversions | `ParserOverload.cpp` |
| M2.3 | `Templates`: deduction, partial ordering, specialisation, instantiation by replay | `ParserTemplate.cpp`, `ParserDependent.cpp` |
| M2.4 | `Classes`: layout, vtables, special members | `ParserClass.cpp`, `ParserVtable.cpp` |
| M2.5 | `Constexpr` | `ParserConst.cpp` |
| M2.6 | the parser's mangling glue (`functionSymbol`, `memberSymbol`, ...), beside `src/Mangle.cpp` | `ParserClass.cpp`, `Parser.cpp` |

M2.1 found a gap the self-host needed mended at once: `friend class sema::Lookup;`, a qualified
friend class, was refused with "expected ';'". Each module's commit may carry such a fix, with its case.
M2.2 found another: a local class's member read a private of `Parser` through the module's friendship,
and [class.access]/2 gives a local class the access of the function it is written in - friendship included.
`Rank` and `OperatorChoice` went to `Entities.h` with the entry types, for the same reason they did.

**A `const` method reaches the parser through a non-const reference.** Two parser methods overloaded on
`const` alone, with different bodies, are chosen by the caller's constness - and once the caller is moved,
`p_` picks the non-const one whatever the caller was. M2.3 met it as `decayedType`: deduction's decay
(through references, cv dropped) and the expression's, and `tools/identical.sh` showed 888 outputs
changed. The deduction one is `decayedArgument` now. The one other such pair, `ptrdiffType`, is split
before M2.4, which moves the code that reaches it. The template table's entry types (`TemplateParam`,
`TemplateDecl`, `PackMembers`, `PackBinding`, `Shadow`) went to `Entities.h` with M2.3.
M2.3's self-host found a third gap: a friend naming a class's private nested type (`Parser::PendingBody`)
was refused - the type path asked only whether it was inside the class, never friendship.
M2.5's self-host found a missing `#include <cmath>`: the moved code had it through ParserInit.cpp, and
clang supplied `std::fma` transitively where cxx3's own headers do not. A module carries its sources' includes.

A basename may not repeat across `src/` and its directories (`msvc/build.cmd` writes one flat object
directory), so the M2.6 module is not called `Mangle`.

## Where step 1 stands, 2026-10-24

All six interfaces exist (M2.1 to M2.6): 287 functions in `src/sema/`, 8,377 lines, and `Parser.h`
from 3,013 lines to 2,151. Every commit was byte-identical to the one before over every case and
Compiler++ unit at -O0, -O1 and -O2 for x86_64-windows in both spellings and x86_64-linux, and
self-hosted on the Mac. The state moves come next, then the Windows leg over the whole series.

## The state moves, M2.7

Measured first: of the parser's 226 fields, six were read by one module alone and not by the parser, and
those moved as the modules' private state. The tables a module answers about moved too, public, because the
parser fills them: the four name tables and the three namespace tables to `Lookup`; the template tables and
the specializations to `Templates`; the vtables to `Classes`; the constexpr functions, constructors and
objects to `Constexpr`; the static symbols to `Linkage`. 132 fields are read by the parser alone, and the rest
are the parse position and scope context - the token stream, `currentClass_`, the locals, the namespace stack,
the type table, `src_` - which is what `p_` is for now.

## Step 2: the syntax tree, the typed tree and the adapter

Design section 12: the parser, carrying cpp11's grammar code and token replay, builds the handle syntax tree
behind a flag; Sema produces the typed tree; a thin adapter lowers the typed tree to `Ast.h`; at M2's end the
old parser-to-`Ast.h` path is deleted. A second front end built beside the first is the risk section 14
records, so step 2 is cut so that each commit is checked by the one before it:

| commit | what | checked by |
| --- | --- | --- |
| M2.8 | the handle data model (`src/syntax/`: arena, ids, string table, 32-bit locations), and the grammar's expression productions recording a syntax node beside the `Ast.h` node they return; `-dump=syntax` | output identical: nothing reads the tree yet; the dump on cases |
| M2.9 | statements and declarations recorded the same way; every function body has a syntax root | the same |
| M2.10 | the typed tree, and Sema typing a subset of the syntax tree into it - scalars, operators, locals, calls | `-check=typed`: every typed node's type against the type the parser gave its `Ast.h` node |
| M2.11 | the adapter: typed tree to `Ast.h` for that subset; behind `-front=typed`, a function inside the subset takes the adapter's `Ast.h`, any other the parser's | output identical with the flag on and off |
| M2.12 on | the subset grows - classes, templates by replay, exceptions - until every function takes the adapter's | the same, case by case; then the old path is deleted. **Stopped at M2.12p**: the review of 2026-10-24 and the decision below end M2 at the typed tree, and the adapter is not grown further |

**How M2.9 finds a statement's parts.** Every part records itself, so a statement takes the roots made since its
first token whose location lies inside its own tokens (`Tree::adopt`): a template body replayed from elsewhere, a
default argument and a member initialiser are not its, being written elsewhere. A read that rewinds and reads
again makes a second root at one location, and the later wins; a `Trial`'s nodes are dead. Recorded at M2.9:
every statement, a declaration statement's declarators (`var`), a function's definition, a variable at namespace
scope, and sizeof, alignof, noexcept, new, delete, typeid, the named casts, throw and a lambda (as its tokens).
Not yet: a class's or a template's definition (a member's and a specialization's bodies are recorded as they are
replayed), a lambda's body, a braced list as a node of its own, the second declarator of `int a, b;` at namespace
scope, and parameters (recorded from M2.10, with a prototype as a `function` over them).

**What M2.10 types, and what -check=typed counts.** `src/sema/Typing.{h,cpp}`: a `TypedTree` arena of
`{syntax handle, type, children}`, and a `Typer` that walks each root `function` and types literals ([lex.icon]'s
two ladders, read from the token the parser kept), a name in the function's own scopes (a `var` declares from its
node on) and then at file scope, the scalar operators, a subscript, an assignment, a comma, a conditional, a call of
a name with one non-template function, a cast, and sizeof/alignof/noexcept. Out of the subset, typed null with its
parts still typed: an operand of class type (operator overloading is resolution's), a name a function finds through
a class, a namespace or a template's bindings (`scopedFunctions_`), a qualified name, a member access and a lambda.
`-check=typed` holds each typed node against the innermost `Ast.h` node carrying its handle. At M2.10, over the 647
cases that compile for the host: 118,633 typed, 81,886 the same type, **0 different**, 67 differing in top-level cv
alone (the parser keeps cv on some prvalues and drops it through a reference) and some conditionals whose arms the
parser promotes or decays - both lowerings, named in the report rather than counted - and 36,662 with no `Ast.h`
node: constants the parser folded, and the bodies of inline functions nothing emits. `tests/typed.sh` runs it in
`make test`. Three recording gaps it found and closed: a functional cast `T(x)` made no node, a range-for's
variable had none (so its body read an outer name), and a lambda's body leaked into the enclosing function.

**What M2.11 adapts, and how the flag is held.** `src/sema/Adapter.{h,cpp}` builds a function's body as `Ast.h` from
its typed tree, with the parser's lowerings written out a second time - a declaration as a scope -1 block of
assignments, a conversion as a `Cast` (to bool as `x != 0`), `a op= b` and prefix `++` as an assignment of the
converted result, `~x` as `x ^ -1`, an lvalue conditional of two names as `*(c ? &a : &b)`, a variadic argument
promoted. From the parser it reads layout alone, by syntax handle: a declarator's frame slot, a block's and a `for`'s
scope number, a string's label, a function's symbol. Anything else is outside, with its reason, and keeps the
parser's body: a non-scalar local or return, a function whose body is a region (noexcept, landing pads), member
access, a name a function finds through a class or namespace, an overload set, switch, try, sizeof. `-front=typed`
puts the adapter's body in place of the parser's for every function inside; `-check=adapted` instead holds the two
`Ast.h` bodies against each other as text (`src/AstDump.cpp`, also `-dump=ast`). At M2.11, over the cases that
compile for the host: 1,986 root functions, 316 adapted, **316 the same**; `tools/identical.sh` with the flag on and
off 5,352 of 5,352 the same, and `tests/adapted.sh` (in `make test`) runs both at -O0 and -O2 per case. The commonest
reasons for outside: a local of class or pointer type (521), a body that is a region (399), a member or subscript
expression (284), a name not in the function's scopes or at file scope (168).

**M2.12a, the first growth: what a C program's body holds.** Pointers to objects, to void and to functions;
arrays of scalars, uninitialised; `a[i]` as `*(a + i)`, a pointer and an integer scaled to bytes in the
pointer-width integer and two pointers' difference divided back (`pointerAdd`, `pointerSub`, the parser's own
shapes); a pointer compared as it stands; `&`, `*`, unary `+`; a C-style cast, to void or between scalars;
`o.m` and `p->m` of a plain struct - no constructor, destructor, vptr or copy of its own - the object's const
carried onto the member, and a plain struct assigned or initialised from one of its own type; an unscoped
enumeration's values and enumerators; `sizeof`, `alignof` and `noexcept`, from the value the parser folded
(`keywordValues_`); a function named as a value. The parser's lowerings were read before each was written
(`-dump=ast` on a probe), and two of M2.11's turned out wrong where nothing had reached them: a conversion to
bool compares with zero converted to the source's type (`x != (long)0`, `d != 0.0`), and a conversion of a
pointer is a `Cast` even where only its top-level const changes. At M2.12a: 811 of 1,986 functions adapted, all
the same; identical on and off 5,352 of 5,352; `run.sh` under `-front=typed` 987 of 987 at -O0 and -O2. Kept
outside, commonest first: a body that is a region (399 - noexcept and landing pads, the exceptions step), a
call of what is not a name (member calls), a return of a class or a reference, an overload set.

**M2.12b: references and calls.** A reference is a slot holding an address, every mention read through it, a
binding the address of an lvalue of the referent's type; a reference parameter is bound, a reference returned
read through (`*call`), and a function returning one keeps its declared return (`declaredReturns_`, by
function node, since the definition's own type has lost the `&`). A call reaches an overload set the parser's
own `resolveOverload` ranks, a static member by its qualified name, and a member function of a plain struct
through `.` or `->` - `&o` in front, the call marked `hasThis` - with its arguments as `completeCall` builds
them: a scalar converted, a reference bound, one past the parameters promoted. Outside: a default, a class
passed or returned by value (the parser gives it a frame slot), a template's, a member reached through a base
or a using-declaration, a virtual or ref-qualified one, and any call whose arguments name a class or enum in a
namespace, since argument-dependent lookup could add a candidate. At M2.12b: 874 of 1,986 adapted, all the
same; identical on and off 5,352 of 5,352; `run.sh` under `-front=typed` 987 of 987 at -O0 and -O2. Outside,
commonest first: a body that is a region (399), a return of a class (68), an array with an initialiser (39).

**M2.12c: an array of scalars with an initialiser.** One store per element through `*(a + i)` for each
dimension, as `targetFor` builds it; a list read in order into the leaves, which is the parser's brace elision;
what the list leaves zeroed (`0.0` as a double for a floating element, `0` as the enumeration for one, `0` as
an int else, each converted to the element); a narrow string spelled out byte by byte, the bytes past it zero.
**The syntax tree keeps no braces inside a list** - `{{1, 2}, {3}}` and `{1, 2, 3}` record the same children -
so a list holding a brace stays the parser's, and so does one expanding a pack, which is one child for several
elements. At M2.12c: 878 of 1,986 adapted, all the same; `run.sh` under `-front=typed` 987 of 987 at -O0 and
-O2. Recording a list's braces in the syntax tree is what would take the rest.

**M2.12d: a plain struct by value.** Returned - `return e` with `e` of the function's class, its type set to
the return's as the parser's `convert` does - passed as an argument and returned from a call, and `make().x`,
a member of an rvalue, marked the xvalue [expr.ref]/4 makes it. The frame slots such a call uses, its result's
and each struct argument's, are the parser's: `callSlots_` records them by call node, with the flag that says the
result slot holds a pointer, and is brought up to date where an elision later moves a result. At M2.12d: 900 of
1,986 adapted, all the same.

**M2.12e: what builds nothing.** An `alignas`, a `decltype` or a `static_assert` in a declaration, and an array
parameter's bound, are recorded in the syntax tree beside the declarators and are unevaluated or adjusted away,
so the adapter passes over them. At M2.12e: 928 of 1,986 adapted, all the same.

**M2.12f: calls by a qualified name and through a pointer, and a guard on scope.** `n::f(a)` and `::h(a)` are the
function the qualifier names; `p(a)` and `(*p)(a)` through a pointer to a function are a `Call` whose callee is the
pointer and whose name is its type, as the parser writes it. **The guard is the half that matters**: the parser's
tables are read after the file, when no namespace or class is open, so an unqualified name in a function inside
one could mean `n::f` where the adapter would find `::f`. Such a use is kept outside wherever anything in a scope
is declared under the name - unless the function is at file scope and no using-directive is open, where the two
lookups cannot part. At M2.12f: 958 of 1,986 adapted, all the same.

**M2.12g: the named casts.** `static_cast`, `const_cast` and `reinterpret_cast` to a scalar or to void are the
conversion they name, built as a C-style cast is; their target is now recorded in `castTypes_` beside the
C-style cast's, which also lets Sema type them (`typed.sh`: 1,368 more nodes typed, 0 differ). A named cast to a
reference is a glvalue and `dynamic_cast` a runtime call, and both stay unrecorded. At M2.12g: 965 of 1,986
adapted, all the same.

**M2.12h: objects with constructors and destructors, and the regions that unwind them.** A local of a class the
parser builds by a constructor or destroys at a scope's end - nothing virtual in it - is taken where it is a block's
own statement: `X x;` or `X x(a, b)` with scalar arguments, its constructor resolved as `constructObject` resolves
it. Each block keeps what it built, as `Parser::alive_` does: the `}` destroys it last first, a `return` saves its
value in a `.ret` slot and destroys everything, a `break` or `continue` what its loop built, and each stretch from
one construction to the next is a `Try` whose pad destroys what was alive by then and resumes unwinding -
wrapCleanups' shape, in the two frame slots it chose, which the parser now records by node with the return's slot
(`blockCleanups_`, `returnTemps_`). On the Itanium targets only: a Microsoft cleanup is a funclet and a state. **The
syntax tree records `true`, `false` and `nullptr` now** (`literal`): an initialiser of one had been a `var` with no
part, which the adapter read as no initialiser - found by the first function it took that had one,
`alternative-tokens`' `main`. At M2.12h: 1,027 of 1,986 adapted, all the same.

**M2.12i: copy-initialisation and class temporaries.** `X x = e;` for such a class is its copy constructor where
it has one and the bytes where it is trivially copyable, an lvalue of `X` or of a class derived from it; `X(a, b)`
in an expression is the temporary the parser builds, `*(X::X(&$tmp, a, b), &$tmp)`, in the frame slot it chose,
now recorded by node (`temporarySlots_`). A class with no constructor, destructor or copy of its own is moved by
its bytes wherever the parser does (`byteClass`, which widens M2.12d's plain struct to one with constructors).
At M2.12i: 1,059 of 1,986 adapted, all the same.

**M2.12j: `throw`, and `try` with its handlers.** A throw of a scalar on the Itanium targets is the three statements
`throwStatement` writes - the storage asked for, the value stored through a cast, `__cxa_throw` - from the slot and the
type_info it chose (`throwFacts_`); `throw;` is `rethrowCall`. A `try` with nothing alive round it and no `try` or
handler round it is the body, then the chain on the selector: each handler begins the catch, binds what it caught by
value or by reference, runs its block - inside an end-catch region of its own where it can throw - and ends the catch;
the slots, the labels, the indices and each handler's scope are the parser's (`tryFacts_`). A `return` or a `break`
out of a handler ends the catches it leaves, as `endCatches` does. **The syntax tree records a handler now**: `catch`
holding the caught object's `var` and the block, so a name in the block finds it - it had been a bare block whose
`e` was declared nowhere. A body the parser wraps in a `try` of its own (a `throw(...)` list, a function try block)
stays the parser's (`wrappedBodies_`). At M2.12j: 1,128 of 1,986 adapted, all the same.

**M2.12k: a `try` among objects, and objects in a `try`.** A block's cleanup regions split round a `try` it holds,
which answers for itself; the `try`'s chain and its handlers' pads destroy what is alive round it from where the
parser says it owes (`TryFacts::owesFrom`). A class local in the `try`'s body is a segment whose pad destroys from the
`try`'s entry and hands the selector to the chain, taking the `try`'s types once its handlers are read; one in a
handler hands to that handler's end-catch pad - `Parser::unwindTarget`, kept as a stack (`handovers_`). A throw of a
class copied by its bytes is the scalar's shape. At M2.12k: 1,151 of 1,986 adapted, all the same.

**M2.12l: calls of function templates, and a class with a destructor returned.** A call of a name a function
template shares is the call the parser made: the symbol it chose is recorded by node beside the call's slots
(`CallSlots::symbol`) and the adapter takes that signature from the set - deduction and ranking are not done twice.
**A call with explicit template arguments had no syntax node**: `twice<int>(k)` left a bare `name` and its arguments
loose in whatever held it; it is a `call` of the name as written now. A class with a destructor and no copy of its
own (`dyingBytes`) returned from its local goes out as its bytes, that local not destroyed; one initialised from a
call returning it is built where the call puts its result (`claimedSlot_`, Parser::claimCallResult). At M2.12l: 1,174
of 1,986 adapted, all the same.

**M2.12m: static locals laid down as data, constructor templates, and a value a reference binds.** A static local
whose initialiser is a constant is a global the parser already wrote: the declaration builds nothing and a use is
`Var::global` of its symbol; one built at run time under a guard (`guardedStatics_`, filled by `guardOnce`) stays
outside. A constructor chosen from a set holding a template is resolved by the parser's `resolveOverload`, which
instantiates what deduces as the parser's own call did. And a scalar bound to a const or rvalue reference is copied
into the slot `bindReference` chose, recorded by the bound expression's node (`bindTemps_`); every expression the
adapter builds carries its node for that lookup. At M2.12m: 1,185 of 1,986 adapted, all the same.

**M2.12n: lambdas, switch and goto, using-declarations, and `noexcept` functions.** A lambda whose captures are
scalar locals of the function is the closure the parser built in its slot, each capture assigned or its address
bound into its member (`lambdaFacts_`, recorded by `noteClosure` and moved to the `lambda` node), and a call of a
local of a class is `o.operator()(a)`. A `switch` promotes its condition and takes its cases back in the order
`caseLabel` numbered them (`caseFacts_`: the folded value, default or not, and the number); a `break` out of one
destroys nothing it may not; a label is its statement and a `goto` the parser's one-statement block, where nothing
is alive to destroy. A call through a using-declaration (`std::strcpy`) takes the set the declaration names and
keeps the name as written. And a `noexcept` function is adapted unless the parser wrapped its body in the terminate
region, which is a wrapped body now; a `throw` in one is the abort the parser writes, and stays outside. At M2.12n:
1,229 of 1,986 adapted, all the same.

**M2.12o: a range-based `for` over an array, and a cast to a reference.** The loop is the parser's rewrite: `.rb`
and `.re` in the slots it chose (`rangeFacts_`, kept only where the range is an array and nothing is alive the
loop must destroy), the test `.rb != .re`, the step `.rb = .rb + 1`, and the variable taken from `*.rb` in the
body's own scope. `static_cast<T &>` and `const_cast<T &>` take the type and the xvalue mark the parser gave the
cast (`refCastTypes_`), since the typed tree types neither. At M2.12o: 1,233 of 1,986 adapted, all the same.

**M2.12p: a plain struct's braced list, and temporaries that have a destructor.** `Pt p = {1, 2};` is the parser's
store per leaf, brace elision its and what the list leaves zeroed; a list holding braces stays the parser's, the syntax
tree keeping none. A class temporary or a call's result with a destructor joins the adapter's pending list
(`temporaryGuards_` gives a temporary's guard), and every place the parser ends a full expression the adapter does
too: an expression statement, a condition, a `for`'s parts and a `return` keep the value in the parser's slot
(`fullSlots_`), clear the guards in front and destroy what was built under them; a declaration flushes; a block's pads
release the guarded ones; and the guards a function made are cleared at its entry (`functionGuards_`). A temporary
returned is the caller's and not destroyed. A temporary an operand of `&&`, `||` or `?:` may skip stays the parser's.
At M2.12p: 1,257 of 1,986 adapted, all the same.

**The parser's `Ast.h` is the oracle for the new path while both exist**: the new path is right where it
produces what the old one produced. That is `tools/identical.sh` again, and the reason the old path is kept
until the new one has matched it everywhere.

Data model (design section 2): one arena per tree, a node `{kind, children range, payload}`, handles numbered
in creation order, a `switch` on kind. A trial parse that fails gives its nodes back, so a failed SFINAE
candidate leaves no syntax behind.

## What is not in step 1

The parser's question "what kind of name is this?" (`atTypeName`, `localHidesType`) stays in the
parser and asks `Lookup`; making it one call is step 2's, with the handle syntax tree behind its flag.

## M2's end, decided 2026-10-25 (Fable 5.1)

The interim review (`M2-REVIEW-FABLE-2026-10-24.md`) put a choice before the user and the user delegated it: M2 ends at a
complete, checked typed tree, `Ast.h` stays the oracle until M3, and the adapter is a harness for its subset and is not
grown. Design section 12.1 carries the reasons; the one that decides it is size: the adapter lowers 1,257 of the 1,986
file-scope free functions - about 15% of the 8,227 functions the same cases emit - and reads fifteen fact maps the parser
records while it builds `Ast.h`, so finishing it means writing the parser's layout half a second time for every function
kind, and M3 then deletes it.

**What is done from here, in this order, each commit gated as before** (make test with its seven suites, the emit golden
recorded at `a2b39e7`, `tools/identical.sh` against `a2b39e7`'s binary, `tools/self-host`):

| step | what | checked by |
| --- | --- | --- |
| M2.13 | the pending facts (`pendingTry_`, `pendingThrow_`, `pendingLambda_`, `pendingRange_`, `pendingReturnTemp_`, `lastCleanupSlots_`) into `FunctionState` | identical; a nested-parse probe |
| M2.14 | `-dump=ast` prints a `Try`'s types, indices, `alsoCleanup`, `msExits` and unwind-help slot | adapted.sh still 0 differ |
| M2.15 | the denominator: every emitted function counted - with a syntax root or compiler-written, by kind - and `-check=typed` and `-check=adapted` reporting over all of them; the Typer types every root, a name it cannot resolve typed null and counted | typed.sh 0 differ |
| M2.16 | resolutions recorded per node at parse time - a name's entity, a member's `Member`, a call's and an overloaded operator's chosen signature - read by the Typer and the adapter in place of `findGlobal`, `findEnum`, `overloadsOf` and `resolveOverload` after the file | typed.sh 0 differ, the typed fraction up; adapted.sh 0 differ |
| M2.17 | the Typer's remaining kinds: `this`, member access, class operators, `new`, `delete`, `throw`, lambdas, qualified names, string pieces; the untyped kinds named in the report | typed.sh 0 differ |
| M2.18 | `tools/self-host` with `-front=typed`; a Linux leg (and the Windows leg where the box answers) running run.sh under `-front=typed` at -O0 and -O2 | stage 1 = stage 2; the leg's counts |

**"M2 closed" means** the six points of design section 12.1 hold, with the numbers written here: the count of emitted
functions, of those with a syntax root and typed, of the compiler-written by kind; the typed fraction of nodes and the
kinds outside it; `-check=typed` and `-check=adapted` at 0 differ; `identical.sh` and self-host green with and without
`-front=typed`; the non-Mac leg's counts.

**M2.13 and M2.14** (`2f9e873`): the six pending facts into `FunctionState`; `-dump=ast` prints a `Try`'s types with
their selector indices, `alsoCleanup`, the Microsoft exits and the unwind-help slot. Identical over 5,352 outputs.

**M2.15: the denominator** (2026-10-25). Every emitted function is counted, by `Typer::census`: one the grammar read has
a live `function` node that defined its symbol (`syntaxFunctions_`, recorded once the symbol is final - a constructor
split for a virtual base is renamed to its C2 after the first recording) and one the compiler wrote carries its kind
(`Function::synthesised`, set at the fifteen sites in `Classes.cpp`, `ParserClass.cpp` and `ParserInit.cpp`). Over the
647 cases that compile for the host: **8,227 functions emitted, 7,420 with a source body, 807 compiler-written, 0 neither**
- against the 1,986 the reports counted before, which were the file-scope free functions. The 807 by kind: implicit
destructor 209, implicit default constructor 180, deleting destructor 144, implicit copy or move 97, complete constructor
33 and destructor 23 (a virtual base's split), thunk 20, virtual thunk 29, vector constructor 13 and destructor 25, array
destructor helper 8, init function 25, tls init 1. The Typer types every body and `-check=typed` holds them all:
184,964 syntax nodes under the bodies, 84,821 typed, 83,323 agreeing, 0 differing, 1,407 with no Ast.h node;
**44,220 expression nodes typed null**, by kind: name 18,595, call 7,255, member 6,729, binary 3,917, assign 2,782, unary
1,238, subscript 1,209, this 564, conditional 399, new 368, postfix 343, cast 243, throw 184, delete 139, lambda 125,
sizeof 92, string 34, comma 4. That is the typed tree's gap at M2.15, and M2.16 and M2.17 are what close it. The adapter
is asked for every body too and answers 1,257 of 7,420, the 6,163 outside each with its reason, the commonest "a body
whose names are a class's, a namespace's or a template's".

**Three recording faults the census found**, each a body the parser emitted whose syntax was not the tree's. A lambda's
call operator, replayed from synthesised tokens at the lambda's own position, was killed twice: by `kill(bodySyntax)`,
which ran after the replay and was meant for the in-place readings (moved in front of it), and by `Tree::adopt`, which
killed any earlier root at one location when a later one appeared there - the call operator, `__invoke` and the `lambda`
node share the `[`'s position. A rewound read records the *same* node again, so `adopt` kills only a root of the same
kind and text now, and a lambda's text is its whole token span whichever reading records it (the first read's was `"["`),
so the two readings still dedup; the live `lambda` node takes the replayed bodies from the dead one (`MadeLambda::bodies`,
`Tree::release`). And a body defined inside a reading that is then killed - an inner lambda's call operator made during
the outer lambda's deducing read, whose closure the real read reuses, or a constructor or member template instantiated
inside a `Trial`, whose destructor kills on success as on failure - is revived after the kill (`definedBodies_`,
`reviveDefinedBodies`, `Tree::revive`): the function was emitted, so its syntax is the tree's whatever reading made it.
`tests/syntax/statements.syntax` was re-recorded for the one change, the lambda node holding its call operator.

Gated: `make test` green (emit golden 0 of 1,924 changed, typed.sh and adapted.sh 0 differ), `identical.sh` 5,352 compared
and the known 8 differ.

**M2.16: resolutions recorded per node at parse time** (2026-10-25). `Parser::Resolved` is what a name, a member access, a
call or an overloaded operator resolved to - Local, Global, Function (named as a value), Call (reached by a call, a
constructor's included), Member, Enumerator, StaticMember, This, Temporary - with the entity's type as the expression
has it, its symbol or value, its owner and the name the parser gave its `Var`. It is recorded where the parser resolves
(`localRef`, `globalRef`, `enumeratorValue`, the `this` branch, the four member paths, `staticMemberRef`,
`memberWithoutObject`, `functionAddress`, `completeCall`, `noteTemporarySlot`, and a declaration's constructor by the
local's serial): an `Expr` carries an index into an append-only table, so a record can never be stale, and `note` copies
it onto the syntax node (`resolutions_`). **The Typer looks nothing up after the file**: `named` asks its own scopes and
then the record; `resolvedType` answers a call's return, a function's pointer ([conv.func]), a member's access type, and
`scopedFunctions_` no longer gates it. **Nor does the adapter**: `name`, `call`, `memberCall`, `objectCall`, a declaration's
and a temporary's constructor read the record and find the signature by symbol; `scopedName`, its two tables and every
`findGlobal`/`findEnum`/`overloadsOf`/`resolveOverload` are gone from it (`findTypedef(tag)` of a class stays: a tag is
not a scope-dependent lookup). Over the 647 host cases: **123,874 typed, 110,954 agree, 0 differ, 12,730 with no Ast.h
node, 5,167 untyped** (84,821 / 44,220 at M2.15); the untyped by kind: member 1,987, name 772, unary 526, new 368, cast
243, call 228, throw 184, conditional 160, binary 155, assign 153, delete 139, lambda 125, sizeof 92, string 34, comma 1.
Adapted: 1,266 of 7,420, all the same (1,257 before: the nine are qualified names the heuristic had kept outside).

**Three faults the gate found in the writing.** A record's type for a member is the access's type - the object's const
applied, `const int [3]` for an array member through a const object - not the member's declared type, since a typed node's
type is the expression's (137 differences, one shape). A function reached by a call and one named as a value are two kinds:
`&f` and `f` are the pointer [conv.func] makes, a call is the return. And a member function or member template called by
its bare name reads its own arguments, so `argGen_` did not move and the call was recorded as a `name` whose Ast was a
`Call`; a name whose Ast is a call is a `call` node now. **And one of this tree's own**: three times a `resolve(std::move(e),
...e->type())` read its operand in the argument list beside the move - argument order is unspecified, and `enumeratorValue`'s
crashed every case including `<string>`; the type is taken into a local first, as CLAUDE.md's M2.12n already said.

Gated: `make test` green (emit golden 0 of 1,924 changed, typed.sh and adapted.sh 0 differ), `identical.sh` 5,352 compared
and the known 8 differ.

