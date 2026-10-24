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
| M2.12 on | the subset grows - classes, templates by replay, exceptions - until every function takes the adapter's | the same, case by case; then the old path is deleted |

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

**The parser's `Ast.h` is the oracle for the new path while both exist**: the new path is right where it
produces what the old one produced. That is `tools/identical.sh` again, and the reason the old path is kept
until the new one has matched it everywhere.

Data model (design section 2): one arena per tree, a node `{kind, children range, payload}`, handles numbered
in creation order, a `switch` on kind. A trial parse that fails gives its nodes back, so a failed SFINAE
candidate leaves no syntax behind.

## What is not in step 1

The parser's question "what kind of name is this?" (`atTypeName`, `localHidesType`) stays in the
parser and asks `Lookup`; making it one call is step 2's, with the handle syntax tree behind its flag.
