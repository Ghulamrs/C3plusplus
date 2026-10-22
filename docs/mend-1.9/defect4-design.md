# Defect 4: a class template's injected-class-name before `::` in an expression

Design only - nothing under `src/` is changed by this document. Measured on the
worktree `mend/p-parser` at `2aea776`, 2026-10-17, with a *patched copy* of `src/`
built under `tests/out-d4/src-probe/` (the one hunk below applied by a script;
`tests/out-d4/cpp11-probe.exe`), so every claim here was run rather than reasoned.
Probes are `tests/out-d4/{d4,p*,q*,r*}.cpp`; clang is
`clang++ -x c++ -std=c++11 -pedantic-errors`.

## 1. Root cause

**The defect is not about nesting.** The brief's program is refused, and so is the
same `Tree::next(n)` written in `Tree`'s *own* member body (`p1_own.cpp`), in a
member defined out of line (`q4`), in a derived class (`q1`), and in a nested
class's mem-initialiser (`q3`). What is refused is the injected-class-name of a
class template followed by `::` **in an expression**. As a *type* it already works
everywhere - `Tree *p`, `Tree::value_type w`, `typename Tree::value_type v`
(`p10`, `q7`) - and `Tree<T>::next(n)` with the arguments written works (`p2`).

Where the injected name lives: `src/parser/ParserType.cpp:148` -
`declareTypeName(tag + "::" + specializationOf, type)` registers `Tree<int>::Tree`
as a member type name of the specialization when `instantiateClass` replays the
class body. It is reached by `Parser::findTypedef` (`src/parser/Parser.cpp:229`)
through `lookupInClass` (`Parser.cpp:125`), which walks `c->enclosing()` and the
bases; a nested class gets `setEnclosing(within)` at `ParserType.cpp:157-159`, and a
nested class's held bodies are replayed with `currentClass_`/`inlineOwner_` =
`Tree<int>::It`, so `findTypedef("Tree")` from inside `It` *does* answer `Tree<int>`
(that is why the type uses work). So the lookup is not what is missing.

What is missing is the **expression path**. `Parser::primary`
(`src/parser/ParserExpr.cpp:759-765`) asks `isTemplateName(peek().text)` *before*
the plain qualified-class branch at `ParserExpr.cpp:1101` (the one a non-template
`Tree::next` takes, `p6`), and sends every template name to
`Parser::templateCall` (`src/parser/ParserTemplate.cpp:2186`). In `templateCall`
the `decl.isClass` arm handles three shapes after the name: `<` (instantiate, then
`(`, `{` or `::` -> `templateIdMember`), and - the injected-class-name case,
`ParserTemplate.cpp:2210-2219` - `(` alone, which makes a temporary of the
specialization `findTypedef(name)` answers. **There is no `::` arm for the
injected name**: control falls to `at_ = save; refuseTemplateId()`
(`ParserTemplate.cpp:2343`), which sees the `::` and prints the "naming a member
through a class template's argument list" message - a message about a shape the
program did not write. The nested class only makes it common, because
`Tree::next` is how an iterator reaches its container's statics, and `Tree<T>::`
is what nobody writes there.

## 2. The change - one commit, two hunks in `src/parser/ParserTemplate.cpp`

**(a) `Parser::templateCall`, after the injected-name `(` branch (line ~2219),
before `at_ = save; refuseTemplateId();`:**

```cpp
        // **The injected class name before `::`.** `Tree::next(n)` inside
        // Tree<T>'s own members, its nested classes and its derived classes -
        // [temp.local]/1 and [class.member.lookup]; the same road `Tree<T>::` takes.
        if (peek().is("::")) {
            if (const Type *self = findTypedef(name))
                if (self->isStructOrUnion() && self->isSpecialization() &&
                    self->templateName() == name) {
                    at_++;
                    return templateIdMember(self, pos);
                }
        }
```

`at_` is already one past the name here (the `at_++` at line ~2194), so `peek()`
is the `::`. `isSpecialization()`/`templateName()` are `src/Type.h:220-221`;
`templateName_` is the bare key `templates_` is looked up by, so the comparison
holds in a namespace too. Reusing `templateIdMember` (`ParserTemplate.cpp:2583`)
is the point: a static member function call with its access check, a static data
member, an enumerator, a nested type as temporary or functional cast, and an
unevaluated non-static member all come with it, and `Tree::x` and `Tree<T>::x`
agree by construction. Where `findTypedef(name)` answers nothing - file scope,
or any function outside the template - the branch does not fire and
`refuseTemplateId` says what it said (`q5`, which clang refuses too).

**(b) `Parser::templateIdMember`, first thing after `member` is read:** refuse
`member == cls->templateName()`. [class.qual]/2 makes `Tree::Tree` name the
constructor, and clang refuses `Tree::Tree(5)` and `Tree<int>::Tree(5)` alike
("qualified reference to 'Tree' is a constructor name rather than a type in this
context"). Today's `templateIdMember` accepts `Tree<int>::Tree(5)` as a temporary
(`r4`, measured on the unpatched compiler: compiles, exits 0) because
`lookupInClass(cls, "Tree")` finds the injected name registered at
`ParserType.cpp:148`; hunk (a) would widen that over-acceptance to `Tree::Tree(5)`
(`q6`). A plain class already refuses `P::P(5)` (`r2`: "'P' has no member called
'P'"). Suggested words: `"'" + cls->describe() + "::" + member + "' names the
constructor, not a type - write '" + member + "(...)'"`. This hunk is what keeps
the fix from accepting a program clang refuses; it is small and belongs in the
same commit.

Nothing in `atDeclarationStart` needs to change: `Tree::bump();` and
`Tree::g += 10;` as statements inside the nested class are already answered
"expression" by `Parser.cpp:475-495` (findTypedef says `Tree` is a struct,
`qualifiedTypeEnd` stops at it, the next token is `::`) - `p4` passes on the
patched build. No backend, no mangler, no `Type` change.

**No existing emission should move**: the new branch fires only where the old code
refused. Verify with `tests/emit.sh --record` before and `0 of N changed` after.

## 3. Neighbour shapes - measured on the patched copy (`probe:`) and clang

| probe | shape | clang | before | patched |
| --- | --- | --- | --- | --- |
| `d4.cpp` | the brief: `Tree::next(n)` in `It::operator++` | rc 0 | refused | rc 0 |
| `p1_own` | `Tree::next(n)` in Tree's own member | rc 0 | refused | rc 0 |
| `p2_nested_args` | `Tree<T>::next(n)` in the nested class | rc 0 | rc 0 | rc 0 |
| `p4_stmt_start` | `Tree::bump(); Tree::g += 10;` as statements, `g` a static defined out of line | rc 0 | refused | rc 0 |
| `p5_two_levels` | `Tree::next(It::twice(n))` two levels down | rc 0 | refused | rc 0 |
| `p6_nontemplate` | non-template enclosing class, same program | rc 0 | rc 0 | rc 0 (unchanged, never reaches `templateCall`) |
| `p10` / `q7` | `Tree::value_type`, `typename Tree::value_type`, `Tree::A x;` as types in a nested class | rc 0 | rc 0 | rc 0 |
| `q1_derived` | `struct D : Tree<int> { Tree::next(4) }` | rc 0 | refused | rc 0 |
| `q2_two_params` | `template <class T, int N>`, `Tree::next` in nested | rc 0 | refused | rc 0 |
| `q3_meminit` | `It() : n(Tree::next(0)) {}` | rc 0 | refused | rc 0 |
| `q4_outofline_tree_member` | `template <class T> int Tree<T>::f(int n) { return Tree::next(n); }` | rc 0 | refused | rc 0 |
| `r3_p12_qualified_only` | `Tree::Size` (enumerator) `+ Tree::K` (static const) in nested | rc 0 | refused | rc 0 |
| `q8` | a *function* template of the same name as nothing here, called from a nested class | rc 0 | rc 0 | rc 0 |
| `q5_filescope_refused` | `Tree::next(1)` in `main` | refused | refused | refused, same message |
| `q6` / `r4` | `Tree::Tree(5)`, `Tree<int>::Tree(5)` | refused | r4 **accepted** | both accepted without hunk (b); refused with it |
| `p7_nested_template` | a nested class *template* `template <class U> struct It` | rc 0 | "a member class template is not supported yet" | unchanged - stays refused by name |
| `p8_outofline` | `template <class T> struct Tree<T>::It { ... };` | rc 0 | refused: "a partial specialization is a definition, and this one has no body" | unchanged - refused, by a message naming the wrong feature (see below) |
| `p9_outofline_member` | `template <class T> typename Tree<T>::It& Tree<T>::It::operator++()` | rc 0 | refused: "'Tree<$T0>::It' is not a class template, so this defines a member of nothing" | unchanged - refused |

**Mangled names of `Tree<int>::It::operator++` and `Tree<int>::next`**, from the
patched compiler on `d4.cpp` and from clang (`-target x86_64-linux-gnu -S`,
`-target x86_64-pc-windows-msvc -S`), identical:

    Itanium    _ZN4TreeIiE2ItppEv        _ZN4TreeIiE4nextEi
    Microsoft  ??EIt@?$Tree@H@@QEAAAEAU01@XZ   ?next@?$Tree@H@@SAHH@Z

`tools/mangled-names` on `p2_nested_args.cpp` (the same two functions, compiling
today) reads "2 names, all agreeing with clang" on all three targets. The body
does not enter the name, so the fix cannot move it.

**Found beside it, pre-existing, not this fix's** (each reproduces on the unpatched
compiler and in a non-template enclosing class, so it is not defect 4):

- `p3b_nontemplate_ptr`: `struct Tree { int v; struct It { Tree *p; int get() { return p->v; } }; };` -
  "'struct Tree' has no member 'v'": a nested class's held member bodies are replayed
  when the *nested* class closes, before the enclosing class is complete, so a member
  of the enclosing class reached through a pointer or a temporary is not there yet
  (`p3`, `p3c`, `p11`, `q6` are the template spellings). The `Tree *p` *type*
  resolves; the member read does not. Wants the nested class's bodies deferred to
  the outermost class's close - its own round.
- `r1_bareK_nontemplate`: a static data member of the enclosing class named
  *unqualified* from a nested class's body (`K` where `Tree::K` works and the bare
  enumerator `Size` works) - "'K' was not declared". Non-template too.
- `p8`: the out-of-line nested class definition is refused with a message about
  partial specialization; the refusal is right (not supported), the words are not.
  Worth a named refusal in the same area when someone is there.

The test cases below avoid all three shapes, so they measure the fix alone.

## 4. Test cases - every `.expected` is clang's output

All under `tests/cases/`; constructors out of line where a class has one, no
`long`, no `sizeof` printed, one call per statement.

1. `template-injected-name-member.cpp` + `.expected` - the brief's program grown:
   `Tree<T>` with statics `next`, `g` (defined out of line), an enum `Size`, a
   `static const int K`, a typedef; nested `It` with `operator++` using
   `Tree::next`, a mem-initialiser `It() : n(Tree::next(0))`, a method using
   `Tree::bump(); Tree::g += 10;` as statements, `Tree::Size + Tree::K`, a
   `Tree *` member compared against null, `Tree::value_type` as a type; a two-level
   nested `Deep` using `Tree::next(It::twice(n))`; `Tree`'s own member using
   `Tree::next`; an out-of-line `Tree<T>::f` using `Tree::next`; a derived
   `struct D : Tree<int>` using `Tree::next`; a two-parameter `Pair<T, N>` with the
   same shape. `main` instantiates `Tree<int>` and `Tree<double>` and prints the
   values. clang prints (to be recorded from clang): one line per shape, e.g.
   `own 2 / it 2 / meminit 1 / stmt 11 / enum 13 / deep 7 / outofline 2 / derived 5 / pair 4 / double 3`.
2. `template-injected-name-nontemplate.cpp` + `.expected` - `p6`'s shape and the
   same members on a *plain* enclosing class, pinning that `templateCall` is never
   reached and the plain qualified branch is unchanged. clang prints `2 11 13`.
3. `template-injected-name-filescope-refused.cpp` + `.error` - `q5`: `Tree::next(1)`
   in `main`; `.error` text `naming a member through a class template's argument
   list` (the existing message; clang refuses with "use of class template 'Tree'
   requires template arguments").
4. `template-injected-name-constructor-refused.cpp` + `.error` - `Tree::Tree(5)`
   inside the nested class, `.error` text from hunk (b) (`names the constructor`);
   clang refuses "qualified reference to 'Tree' is a constructor name".
5. `template-id-constructor-refused.cpp` + `.error` - `r4`: `Tree<int>::Tree(5)`
   in `main`, same `.error` text; today this compiles and clang refuses it.

`tests/names.sh` on case 1 checks `_ZN4TreeIiE2ItppEv` and the Microsoft name
above against clang on all three targets; define `It`'s constructor out of line so
clang emits both C1 and C2 on x86_64-linux.

## Gates run on the patched copy

`tests/run.sh` with `CXX1=tests/out-d4/cpp11-probe.exe` (headers linked beside the
probe): **702 passed, 0 failed** (453 printed their recorded output, 248 refused as recorded) - identical to `main`'s count, so nothing that compiled or was refused before moves. (A first run without the headers linked showed 64 failures, all "the standard header <...> is not provided" - the probe binary looking for `include/` beside itself.). `tests/emit.sh --record` was not run (no golden in
this worktree); the implementer should record one at `2aea776` first and expect
`0 of N changed`.
