# Expression SFINAE: a function template whose signature depends on its parameters through an expression

Design only - nothing under `src/`, `include/`, `lib/`, `tests/` or `tools/` is changed by
this document. Measured on `main` at `ddb9c13` (release 1.13), 2026-10-17, on the Mac.
clang is the oracle: `clang++ -x c++ -std=c++11 -pedantic-errors` validates every probe,
and `clang++ -std=c++11 -target {x86_64-linux-gnu, arm64-apple-darwin, x86_64-pc-windows-msvc} -S`
gives the names. Everything in `probes/` is re-measurable: `probes/measure.sh` validates
the seven probe programs and writes `<probe>.<target>.names` for each target;
`probes/today.sh` runs this tree's `cpp11.exe` over the same programs and prints what it
answers now. The Linux box and cl were not available for this round; the Windows names
here are clang's spelling of the Microsoft ABI, and section 1.4 says what that costs.

The three shapes the brief names, and what cpp11 answers today (`probes/today.sh`):

```cpp
template <class T> typename enable_if<sizeof(T) == 4, int>::type f(T);   // 'sizeof' of a template parameter in a signature is not supported yet
template <class T> auto g(T t) -> decltype(t.size());                    // a function template whose trailing return type is 'decltype(...)' is not supported yet
template <class T> decltype(T() + T()) h(T);                             // 'template parameter' and 'template parameter' cannot be combined like that
```

The first two are refusals by name - `src/parser/ParserExpr.cpp:2596` and
`src/parser/ParserTopLevel.cpp:1840` (`refuseTemplateTrailingDecltype`), both cited in
`docs/EXCLUSIONS.md`. **The third names nothing**, and is defect D3 below.

## 0. Where the work is, in one paragraph

Half of expression SFINAE already works. The *substituted* read of a signature - the one
`Parser::instantiate` (`src/parser/ParserTemplate.cpp:1089`) makes first, with concrete
types bound - folds `sizeof(int) == 4` to 1, instantiates `enable_if<1, int>`, looks `type`
up, and fails where there is none; that failure is raised inside a `Trial` at every site
that instantiates a candidate (`ParserTemplate.cpp:2374`, `:2672`, `:2823`) and becomes a
`SubstitutionFailure` that drops the candidate. `tests/cases/template-sfinae.cpp` proves
that road for `Value<T>::type`. What stops the three shapes above is the **second** read,
the *pattern* read with `Kind::TemplateParam` bound, which exists because the Itanium ABI
spells a specialization's return type and parameters from the template's pattern - and a
pattern holding `sizeof(T) == 4` or `t.size()` is spelled as **the expression itself**:
`_Z1fIiEN9enable_ifIXeqstT_Li4EEiE4typeES1_`, `_Z1gI1SEDTcldtfp_4sizeEET_`. The ordinary
expression parser cannot read an expression over a `Kind::TemplateParam` - that is D3's
message - and the mangler cannot write one. So the work is: a way to **read an expression
over dependent operands as a tree without typing it**, a **type kind and an argument form
that carry that tree**, and a **mangler that spells it**. Microsoft needs nothing (1.4).

## 1. Mangling, measured

Seven probe programs, 150 function templates, every one valid C++11 by clang's own word.
`probes/p01_sizeof.cpp` is `sizeof` and the operators in an `enable_if` argument;
`p02_decltype.cpp` the trailing `decltype`; `p03_leading.cpp` the leading `decltype`, where
no parameter is in scope; `p04_subst.cpp` the substitution table; `p05_fail.cpp`
substitution failure and the `f(...)` fallback (its `.expected` is clang's run);
`p06_misc.cpp` non-dependent expressions, defaulted arguments, a non-type parameter inside
`decltype`, a class-template argument that is an expression, pointers and references to a
dependent member type; `p07_unresolved.cpp` the `sr T_` qualified name. **arm64-darwin
agrees with x86_64-linux on every name but the leading underscore** - the two Itanium
targets want one spelling (`measure.sh`'s check; the only difference it printed is C1
against C2 for a constructor defined inline, the recorded emission difference, not a name).

### 1.1 Where the expression goes

Three places, each a production of [mangle] (the Itanium C++ ABI, section 5.1.5 and 5.1.6):

| written | spelled | measured |
| --- | --- | --- |
| a non-type template argument that is an expression of the parameters | `X <expression> E` | `enable_if<sizeof(T) == 4, int>` is `9enable_ifIXeqstT_Li4EEiE` (f1); `Box<sizeof(T)>` as a *parameter* type is `3BoxIXstS0_EE` (m7) |
| `decltype(e)` where `e` is an id-expression or a class member access, not parenthesised | `Dt <expression> E` | `-> decltype(t.v)` is `Dtdtfp_1vE` (g3); `-> decltype(u)` is `Dtfp0_E` (g12); `-> decltype(N)` is `DtT0_E` (m6); `-> decltype(T::value)` is `DtsrT_5valueE` (q2); `-> decltype(t.next->v)` is `Dtptdtfp_4next1vE` (g5) |
| `decltype(e)` of anything else | `DT <expression> E` | `-> decltype(t.size())` is `DTcldtfp_4sizeEE` (g1); `-> decltype((t))` is `DTfp_E` (g21 - the parentheses make it DT); `decltype(T() + T())` is `DTplcvT__EcvS0__EE` (h1) |

A `Dt`/`DT` is a *type*: it goes where the return type goes in a template specialization
(`_Z2g1I1SE` `DT...E` `T_`), and `P`, `R`, `K` wrap it like any type (m9 `PN9enable_if...E`,
m10 `RN9...E`, m11 `KN9...E`). A dependent member of a template-id whose arguments hold an
expression is the existing `N <prefix> <name> E` with the `X...E` inside the prefix - the
`Kind::DependentMember` spelling at `src/Mangle.cpp:533` unchanged, only the template-id's
argument learning a third form beside a type and a value.

### 1.2 The expression grammar to implement - each production measured

Operands first. `T_`, `T0_`, `T1_` are the template parameters by index (f27, f30 -
`eqmlT0_T1_Li6E` for `N * M == 6`); `fp_` is the first function parameter and `fp0_` the
second, `fp1_` the third ([mangle] `fp <top-level CV-qualifiers> _` for parameter 0,
`fp <number> _` for parameter n+1) - g9 `plfp_fp_`, g11 `plfp_fp0_`, s8 `plfp0_fp_`; a
reference parameter is still `fp_` (s9, `const T &t`). **A parameter is reachable only in a
trailing return type**, [dcl.fct]/2 - in a leading `decltype` the names resolve to types,
templates and globals alone (p03 has no `fp` anywhere).

| production | written | spelled | measured |
| --- | --- | --- | --- |
| integer literal | `4`, `4u`, `4L`, `4ULL`, `2ll` | `L <type> <value> E`, the type the literal's own | `Li4E` f1, `Lj4E` f31, `Ll4E` f32, `Ly4E` f33, `Lx2E` m20, `Lm1E` h21 |
| other literals | `true`, `'a'`, `4.0`, `1.5f` | `Lb1E`; `Lc97E`; `Ld<16 hex digits>E`; `Lf<8 hex digits>E` - the IEEE bits | f22 `Lb1ELb0E`, f34 `Lc97E`, f35 `Ld4010000000000000E`, h23 `Lf3fc00000E` |
| negative literal | `-1` | **not** `Lin1E` - it is the unary minus applied: `ngLi1E` | h24 `cvT_ngLi1EE` |
| `sizeof(T)`, `sizeof(T *)`, `sizeof(T[2])` | dependent type | `st <type>` | f1 `stT_`, f23 `stPT_`, f24 `stA2_T_` |
| `sizeof(expr)` | `sizeof(t)`, `sizeof(T(1))` | `sz <expression>` | g23 `szfp_`, f26 `szcvT_Li1E` |
| `sizeof(int)` - a type that depends on nothing | | **a literal of `size_t`**, [mangle]: "if the operand of a sizeof or alignof operator is not instantiation-dependent it is encoded as an integer literal reflecting the result" | f25 `eqstT_Lm4E`; m24 `eqszfp_stT_` keeps both dependent halves |
| binary operators | `+ - * / % & \| ^ << >> == != < <= > >= && \|\| , =` | `pl mi ml dv rm an or eo ls rs eq ne lt le gt ge aa oo cm aS`, prefix, two operands | f7-f18, f17 `aaeq..ne..`, g28 `cmfp_Li1E`, g29 `aSfp_fp_`, g18 `ltfp_fp_` |
| unary operators | `- ! ~ & * +` | `ng nt co ad de ps`, prefix, one operand | f20 `ngstT_`, f19 `nteq..`, f21 `costT_`, g15 `adfp_`, g14 `defp_`, q5 `psfp_` |
| prefix `++`/`--` | `++t` | `pp`/`mm` | g31 `ppfp_` |
| postfix `++`/`--` | `t++` | clang writes `ppfp_`, the same as the prefix form, where [mangle] has `pp_`. **Refused in the first cut** (1.5) | g32 |
| `?:` | `c ? a : b` | `qu <c> <a> <b>` | f22 `queqstT_Li4ELb1ELb0E`, m22 `queqcldtfp_4sizeELi1Efp_fp_` |
| parentheses | `(e)` | nothing - the tree, not the spelling, carries them (and they turn a `Dt` into a `DT`) | f3 `gtstT_Li2E` for `(sizeof(T) > 2)`; g21 |
| member access | `t.v`, `t->v` | `dt <expression> <source-name>`, `pt <expression> <source-name>` | g3 `dtfp_1v`, g5 `ptdtfp_4next1v` |
| member call | `t.size()`, `t->size()` | `cl <callee> <args>* E` with the callee a `dt`/`pt` | g1 `cldtfp_4sizeE`, s10 `clptfp_4sizeE` |
| free call | `free1(t)` | `cl <source-name> <args>* E` - the unresolved name as length-and-letters | g2 `cl5free1fp_E`, h5 `cl5free1cvT__EE` |
| qualified name through a parameter | `T::count()`, `T::value` | `sr T_ <source-name>` (`sr <unresolved-type> <base-unresolved-name>`) | g20 `clsrT_5countE`, q2 `srT_5value` |
| functional cast, one argument | `T(x)`, `T(1)`, `int(t)` | `cv <type> <expression>` | g8 `cvT_fp_`, h10 `cvT_Li1E` |
| functional cast, zero or several arguments | `T()`, `T(1, 2)` | `cv <type> _ <expression>* E` | g7 `cvT__E`, h11 `cvT__Li1ELi2EE` |
| C-style cast | `(int)t`, `(T *)0` | `cv <type> <expression>` - the same as a one-argument functional cast | g26 `cvifp_`, h17 `cvPT_Li0E` |
| `static_cast<T>(e)` | | `sc <type> <expression>` | g25 `scifp_`, m25 `scT_Li1E` |
| subscript | `t.arr[0]`, `t[0]` | `ix <expression> <expression>` | g6 `ixdtfp_3arrLi0E`, m18 `ixfp_Li0E` |
| a non-dependent subexpression | `'a' - 93`, `(int)4.0`, `1 - 2` | kept as written, not folded - only a whole type or argument that depends on nothing is resolved (1.3) | f34 `miLc97ELi93E`, f35 `cviLd...E`, m31 `mistT_miLi1ELi2E` |

Associativity is the tree's: `sizeof(T) - 1 - 2` is `mimistT_Li1ELi2E` (m30) and
`sizeof(T) - (1 - 2)` is `mistT_miLi1ELi2E` (m31), so the reader must build the same tree
the expression grammar does and the spelling then follows from a preorder walk.

### 1.3 What the substitution table does with an expression

Read off `p04_subst` and `p07_unresolved`, with the seq-id rule already in `Mangle.cpp`
(`S_` is candidate 0, `S0_` candidate 1, `S1_` candidate 2):

- **A type operand inside an expression is a candidate like any type.** In
  `_Z3f17IiEN9enable_ifIXaaeqstT_Li4EnestS1_Li2EEiE4typeES1_` the second `sizeof(T)` writes
  `S1_` for T: candidates are [f17, enable_if, T]. The same for `cv T_` (h1
  `plcvT__EcvS0__E`), `sc T_`, `st T_`.
- **The expression itself is not a candidate, nor are `fp_`, literals and operators.**
  Nothing in s5 (`t.size() + t.size()`, `plcldtfp_4sizeEcldtfp_4sizeE`) is abbreviated.
- **A `Dt`/`DT` type is a candidate as a whole, pushed at its `E`**, after whatever type
  operands it holds were pushed. h1 `_Z2h1IiEDTplcvT__EcvS0__EES0_`: candidates [h1, T, DT]
  (the argument `i` is a builtin and no candidate), so the second `cv T` inside is `S0_` and
  the parameter T outside is `S0_` too. s4 `_Z2s4I1SEDTplcldtfp_4sizeEcldtfp0_4sizeEET_S2_`:
  [s4, **S**, DT, T] - **the template argument `1S` written in `I...E` is a candidate like
  any class type**, which the existing `templateFunction` already does through
  `templateArgument()` → `type()`; nothing inside this DT is a type, so the first parameter
  is written `T_` and takes index 3, and the second is `S2_`. Every name in `probes/`
  recounts to this one rule: f17 [f17, enable_if, T] → `S1_`; s3 [s3, enable_if, T,
  `enable_if<...>`, `N...E`] → T `S1_`, the second `enable_if<...>::type` `S3_`; s2's
  `NS0_IXeqstS1_...` → enable_if `S0_`, T `S1_`.
- **`sr T_ name` pushes T once, as the type operand it is, and `sr` itself is no candidate.**
  g20 `_Z3g20I1SEDTclsrT_5countEES1_` and q2 `_Z2q2I1SEDtsrT_5valueES1_`: [name, S, T, Dt/DT]
  and the parameter T is `S1_`; q1 `...srT_5countEfp0_ES1_T0_` beside a second parameter the
  same, U written `T0_` at its first mention. (A first reading of these names forgot the `1S`
  argument and invented a second push; the recount is in the paragraph above and there is
  none.)
- **An unresolved name is not a candidate.** h5 `_Z2h5IiEDTcl5free1cvT__EEES0_`: were
  `5free1` pushed, T would be index 2 and the parameter `S1_`; it is `S0_`.

So the mangler needs no new rule about the table: `type()` pushes every type it writes
(`Mangle.cpp:504`, `:548`), the expression walker calls `type()` for its type operands and
pushes the `Dt`/`DT` when it closes, and writes `fp_`, literals, operators and unresolved
names without touching `subs_`.

### 1.4 Microsoft: nothing to write

Measured with `-target x86_64-pc-windows-msvc` over all seven probes: **every name is the
substituted signature**. `f1` is `??$f1@H@@YAHH@Z`; `g1` is `??$g1@US@@@@YAHUS@@@Z`;
`Box<sizeof(T)>` as a parameter is `U?$Box@$03@@` (m7, the value 4 as `$03`); the
`enable_if` never appears; `decltype(T::value)` for a `static const int` is `?BH` (q2), the
declared `const int`. `microsoftTemplateFunctionName` (`Mangle.cpp:1545`) is already
handed `fn`, the substituted type, and `Parser::instantiate` already builds it. **No line
of the Microsoft mangler changes**, and `tests/names.sh`'s Windows column must read the
same before and after. cl was not asked this round; clang's Microsoft spelling has agreed
with cl on every template name measured since rung 5.2, and `names-vs-cl.cmd` on the box
is the gate when it is back.

## 2. Representation

### 2.1 What is kept: a small immutable tree, interned by its spelling

`struct DepExpr` in `src/Type.h`, owned and interned by `TypeTable` the way `Type`s are:

```cpp
struct DepExpr {
    enum Op { Literal, TemplateParam, FunctionParam, SizeofType, SizeofExpr, Unary, Binary,
              Conditional, Member, Arrow, Call, UnresolvedName, QualifiedParamName,
              FunctionalCast, StaticCast, Subscript };
    Op op;
    std::string code;                 // the two-letter operator code, "pl", "eq", "ng" ...
    const Type *type = nullptr;       // Literal: its type; SizeofType / casts: the type operand
    long long value = 0;              // Literal (integer); the bits, for a floating one
    bool floating = false;
    int index = -1;                   // TemplateParam / FunctionParam: which
    std::string name;                 // Member / Arrow / UnresolvedName / QualifiedParamName
    std::vector<const DepExpr *> kids;
    bool idExpression = false;        // decides Dt against DT at the top
    std::string key;                  // the Itanium spelling with no substitutions - the identity
};
```

**Why a tree and not the tokens.** Spelling needs what only the parser knows at the moment
of reading: that `t` is function parameter 0 (not a global), that `T` is template parameter
1, that `S` in `S().size()` is a type and `free1` is not, and what type a literal has. A
token range re-read at mangling time would need the parser's tables again, and the mangler
is deliberately a function of `Type`s with no parser in reach. **Why not the ordinary
`Expr` AST.** `Expr` nodes are typed (`Binary` wants two typed operands, which is D3's
message) and are owned by the `Program`; the pattern read builds nothing the program keeps.
And `Kind::DependentMember` already set the precedent: a pattern-only `Type` carrying the
written shape (`T::type`), interned in `TypeTable::derived_`, spelled by the mangler and
meaning nothing to any backend.

**Why `key` is the identity.** Two declarations that write equivalent expressions -
[temp.over.link]/5, "equivalent" being the same tokens modulo parameter renaming - produce
the same tree with the same indices and so the same key, and intern to one `DepExpr *` and
one `Type *`. `describe()` of the new kind answers the key. That is what
`functionExplicitSpecialization` (`ParserTemplate.cpp:709`, `made->describe() !=
wanted->describe()`) and `matchPattern`'s final `pattern != arg` compare, so an explicit
specialization and partial ordering over such a signature need nothing more. **Deferred, and
why it can be:** today a function template is pushed to `fnTemplates_` only when *defined*
(`ParserTemplate.cpp:593`), and a prior declaration is simply overwritten by its definition
(`:597`) - no code asks whether two function template declarations are the same template, so
[temp.over.link] has no caller yet. The key makes the answer available for when one exists;
"functionally equivalent but not equivalent" is ill-formed NDR and is not diagnosed, which
is what clang does too.

### 2.2 Two carriers for the tree

- **`Kind::DependentExpr`** - a new `Type` kind after `DependentMember`, `pointee_` null,
  `expr_` (one new `const DepExpr *` field on `Type`) the tree, `length_` 1 for a `Dt`
  (id-expression or member access, unparenthesised) and 0 for a `DT`.
  `TypeTable::dependentExpr(const DepExpr *, bool idExpression)` interns on the pair. Like
  `TemplateParam` and `DependentMember` it exists only in a pattern: no size, no alignment,
  `isComplete()` false, reaches no backend. It is what `decltype(...)` in a pattern answers.
- **`TemplateArg::expr`** - a fourth form of argument beside a type, a value and `isParam`
  (`src/Type.h:53`): `const DepExpr *expr = nullptr`, `isType = false`, `type` the
  parameter's declared type as a value argument carries it. It is what
  `enable_if<sizeof(T) == 4, int>` and `Box<sizeof(T)>` carry in a pattern. `specializationKey`
  (`ParserTemplate.cpp:1049`) keys it `"$X" + expr->key`, as `isParam` is keyed `$P<n>` - so
  `enable_if<sizeof(T)==4,int>` and `enable_if<sizeof(T)==8,int>` are two shallow pattern
  types, which `instantiateClass`'s `patternOnly_` branch (`:2135`) makes under those tags.

### 2.3 The reader: `Parser::dependentExpression`, a parser of its own over the tokens

A new file, `src/parser/ParserDependent.cpp` (the twelve-file rule allows a thirteenth; it
is one subject). A recursive-descent reader with the same precedence ladder
`ParserOperator.cpp` has - assignment, conditional, `||`, `&&`, `|`, `^`, `&`, equality,
relational, shift, additive, multiplicative, unary, postfix, primary - over the token
stream, building `DepExpr` nodes and **typing nothing**. It is entered only in a pattern
read (`patternOnly_`) and only where `dependentAhead()` says the operand mentions a
dependent name; everything else keeps today's road. Its leaves ask the parser's tables:

| token | asked | leaf |
| --- | --- | --- |
| an identifier bound as a type parameter (`typedefIndex_` entry whose type is `Kind::TemplateParam`) | | `TemplateParam`, index `type->length()`; followed by `(` it is a functional cast, by `::` a `QualifiedParamName` |
| an identifier in `nonTypePatternParams_` | | `TemplateParam` with that index (`T0_` for `N`, m6) |
| an identifier `findLocal` answers with `isParameter` | the trailing-return scope `readTrailingReturn` opens | `FunctionParam`, index = its position among the parameters declared in that scope (1.2, `fp_`/`fpN_`) |
| an identifier that `atTypeName` answers for | | a type-id, read by `specifiers()`/`declarator(…, true)` as the cast or `sizeof` operand - `S` in `S().size()` |
| any other identifier followed by `(` | | `UnresolvedName` - `5free1`; it is **not looked up**, which is what an unresolved name is |
| any other identifier | | **refused by name** (1.5) - a global variable is `L_Z1gE` and was not measured |
| a literal | `primary()` under `Unevaluated`, then `fold`/`foldFloating` for its value, its type from the `Num` | `Literal` |
| `sizeof` | `(` + type-id that closes with `)` (the `parenHoldsTypeId` trial the C-cast already uses) → `SizeofType`; a type that mentions no dependent kind folds to `Literal` of `target_.sizeType()` ([mangle]'s rule, f25); otherwise `SizeofExpr` | |
| `(` | a type-id closing with `)` → C-style cast `cv`; else a parenthesised expression, which clears the `inTemplateArgs_` flag as `primary` does so `>` is an operator again | |
| `static_cast` `<` type `>` `(` e `)` | | `StaticCast` |

The reader restores nothing but reads tokens forward; it adds no parser state, so `Trial`'s
put-back list (`ParserTemplate.cpp:1806`) does not grow. A shape it has no production for
stops with `src_.fail(pos, "... is not supported yet in a template signature ...")` - a
refusal by name, which 3.2 says must propagate.

`dependentAhead(stop)` is a token scan to the first `,`, `>`, `>>` or unbalanced `)` at
depth 0 (counting `(`/`)` and a `<` after a template name as `expandPackPattern` does,
`ParserTemplate.cpp:1726`): true if any identifier is a bound type parameter, a
`nonTypePatternParams_` name, a `packs_` name, or a parameter local of the open trailing
scope. It is the same question clang asks ("instantiation-dependent") and it decides 1.2's
last row: a non-dependent argument keeps today's `constantExpression` and so is folded to a
value `L...E` with the *parameter's* type (`enable_if<true, T>` → `Lb1E`, m2, which cpp11
already writes - `probes/today.sh`, d2's neighbour n2), and a non-dependent `decltype(1 + 2)`
keeps today's `decltypeSpecifier` and answers `int` (m4 `_Z2m4IiEiT_`).

### 2.4 The two defects the pattern read has today, which this must fix first

Both are in `probes/d1_nondependent_hard_error.cpp` and `d2_nondependent_wrong_name.cpp`,
and both predate this feature:

**D1.** `template <class T> typename enable_if<2 + 2 == 4, int>::type m1(T)` is a hard error
- "'m1' is a function template and no specialization of it could be made" - on a valid
program. `instantiateClass` under `patternOnly_` (`ParserTemplate.cpp:2135`) makes a
*shallow* struct for **every** template-id it meets and registers it under the real tag
with `declareTypeName(tag, shallow)`; `enable_if<1,int>` depends on nothing, so its tag is
the one the substituted read later asks `findTypedef` for (`:2150`), finds the shallow,
memberless class, and `type` is not there. The comment at `:2130` says the shallow type is
for `Holder<T>`, "T has no size" - true of an argument that mentions a parameter and false
of one that does not. **D2** is the same fault from the other side: with `enable_if<true,
int>` interned for real before the template is read, the pattern read's `memberTypeWalk`
(`ParserType.cpp:19`) still makes a `DependentMember` of a specialization that depends on
nothing, and the name comes out `_Z2m5IiEN9enable_ifILb1EiE4typeET_` where clang writes
`_Z2m5IiEiT_` - a wrong linkage name, silently. **The fix is one predicate asked at both
sites**: `dependentType(const Type *)` - a `TemplateParam`, a `DependentMember`, a
`DependentExpr`, a specialization any of whose arguments is `isParam`, carries `expr`, or
is a dependent type, or a pointer/reference/array/function over one - and the shallow
branch and the `DependentMember` branch fire only where it answers yes. This is a change to
the pattern read that stands on its own (its own commit, its own case, 6.1), and nothing in
sections 2.1-2.3 is correct without it: `enable_if<sizeof(T) == 4, int>` *is* dependent and
takes the shallow road, `enable_if<true, int>` is not and must take the real one.

## 3. Substitution failure

### 3.1 Where the expression is substituted

Nowhere new. The substituted read is `readTemplateDeclaration` with concrete types bound,
and under it `sizeof(int) == 4` folds in `constantExpression`, `t.size()` is an ordinary
member call on a concrete `t` in `readTrailingReturn`'s parameter scope, and
`T() + T()` is an ordinary expression in `decltypeSpecifier`. The failure messages those
roads give today were measured (scratchpad, 2026-10-17): `'.' needs a struct or union, not
'int'`; `'struct S' has no member 'size'`; `'struct S' and 'struct S' cannot be combined
with '+' - one of them is a class, and no 'operator+' is declared that takes them`; `'*'
needs a pointer, not 'int'`; `no function called 'NoDef::NoDef' takes these 0 argument(s)`.
**None contains the four markers** `Source::fail` reads as a refusal (`src/Source.cpp:108`:
"not supported", "not implemented", "is C++1", " yet"), so inside a `Trial` each becomes a
plain `SubstitutionFailure` and the candidate is dropped - which is what
`instantiateViableTemplates` (`ParserTemplate.cpp:2672`) and `templateCall` (`:2374`)
already do. `probes/p05_fail.expected` is the program that proves all three shapes choose
the way clang chooses: `1 2 3 / 13 20 / 30 40`.

The pattern read runs **inside** the same `Trial` in `instantiate` (`:1118`, after the
substituted read) and **outside** any trial in `deduceTemplateArguments` (`:1597`) and in
`templatedName` at the declaration (`:174`). A dependent expression the reader can read
never fails there; one it cannot read fails by name at the declaration, which is where a
refusal belongs.

### 3.2 What stays a hard error

- A refusal naming a feature, from the reader or from the mangler (`refuse("... yet")`,
  `Mangle.cpp:640`): `f.unsupported` re-raises it at all four catch sites today
  (`ParserOperator.cpp:47`, `ParserTemplate.cpp:687`, `:2380`, `:2678`, `:2831`), and
  "A refusal is not a substitution failure" in CLAUDE.md is the rule. Every message the
  reader and the new mangler branch raise must carry " yet" or "not supported" - and every
  message a *substituted* read raises must not. The second half is the one to watch: a
  message in `ParserExpr.cpp` or `ParserExprCall.cpp` that happens to end in "yet" turns a
  legitimate SFINAE failure into a hard error, and the existing `template-sfinae.cpp` says
  that has not happened for the `Value<T>::type` road; the new cases say it for these.
- A failure in the **body** of a chosen specialization - [temp.deduct]/8's line, which
  `memberTemplateBody_` already draws for member templates (`:2826`) and the deferred
  replay in `instantiatePending` draws for free ones by running outside any trial.
- The refusals of 1.5, at the declaration.

## 4. Ordering and overloading

Nothing changes in the machinery, and the measured p05 says the outcome: `f(1)` takes the
`sizeof(T) == 4` template, `f(1.5)` the `!= 4` one, `f(1, 2)` the `f(...)`.

- `instantiateViableTemplates` deduces each overload of the name, instantiates it in a
  `Trial`, and the specializations land in `functions_` under the plain name where
  `resolveOverload` ranks them beside the non-template - an ellipsis is the worst rank, so
  `f(...)` is taken only where no template survives. [over.match.best]'s non-template tie
  rule (`betterCandidate`, `ParserOverload.cpp:794`) is untouched.
- Deduction never looks inside the expression. In a *parameter* type - m12's
  `typename enable_if<sizeof(T) == 4, int>::type`, m7's `Box<sizeof(T)>` - the new kinds are
  a non-deduced context: `deduceOne` (`:1403`) already falls through to "Nothing to deduce
  here" for a `DependentMember` and skips a non-type argument that is not `isParam`
  (`if (!p.isType || !a.isType) continue;`); a `TemplateArg::expr` is `!isType` and takes
  that `continue`. [temp.deduct.type]/5 says exactly that.
- Partial ordering (`moreSpecializedFunction`, `ParserOverload.cpp:797`) matches *parameter*
  patterns with `matchPattern`; a `DependentExpr` parameter type reaches the final
  `pattern != arg` and two equivalent expressions are one interned type (2.1), two different
  ones are neither more specialized - conservative and correct. Two templates differing only
  in their `enable_if` return type have identical parameters; where SFINAE leaves both
  viable, clang calls the call ambiguous and so does `betterCandidate`'s tie.
- `matchPattern`'s specialization branch compares non-type arguments by `p.value != a.value`;
  it gains `if (p.expr || a.expr) return p.expr == a.expr;` (interned, so pointer equality).

## 5. Refused by name in the first cut

Each with a message carrying "not supported yet", at the declaration, and each cited in
`docs/EXCLUSIONS.md` after `tools/exclusions --check` reads 0/0:

| shape | why |
| --- | --- |
| a pack in the expression - `sizeof...(Ts)`, `f(ts...)` | `sZ`, `sp` and `fp` numbering past an expansion: unmeasured |
| a qualified name that is not `T::name` - `t.S::size()` (m27 `sr1SE4size`), `T::U::v`, `Box<T>::value` | three more `sr` forms, one measured; the template-id form (`srN...`) not |
| a template-id inside the expression - `f<T>(t)`, `Box<T>()` as a cast operand is fine (a type-id) | unresolved template-ids are their own grammar |
| postfix `++`/`--` | clang spells `pp` where the ABI says `pp_` (g32); refuse rather than pick a side |
| `dynamic_cast`, `reinterpret_cast`, `const_cast`, `typeid`, `noexcept(e)`, `alignof`, `new`, `delete`, `throw`, a lambda, a string literal, `.*`/`->*` | unmeasured codes (`dc rc cc ti nx at nw dl tw ...`) |
| a global variable or function named as a value, `-> decltype(g)` | `L_Z1gE` form unmeasured |
| a **member** function template with a `decltype` or expression signature | its Itanium name goes through `memberFunctionTemplate` (`Mangle.cpp:235`), a second road; the free-function road first |
| a template parameter's expression as an **array bound** in a parameter, `T (&)[sizeof(T)]` | `A <expression> _`, the array branch spells only `T_` today (`Mangle.cpp:574`) |
| `noexcept(expr)` depending on a parameter | not part of the C++11 name and not read in a pattern today; stays refused where `exceptionSpecification` meets it |
| `decltype(auto)`, `-> auto`, `auto f(T)` | C++14; the refusals naming the version stay exactly as they are |

The two existing refusals become unreachable for the shapes above and are **replaced, not
deleted**: `ParserExpr.cpp:2596` keeps a reworded fallback for a dependent `sizeof` reached
outside the reader's doors (an array bound, a `noexcept`), and `refuseTemplateTrailingDecltype`
goes - its one shape is now read.

## 6. Test plan

Every `.expected` is clang's output (`tools/expected-from-clang`); constructors out of
line; no `long`, no printed `sizeof`, one call per statement.

1. **`template-nondependent-argument.cpp`** - D1 and D2 (2.4), with the fix of 2.4 alone:
   `enable_if<2 + 2 == 4, int>::type`, `enable_if<true, int>::type` with the real class
   interned before and after the template, `enable_if<true>::type`, `enable_if<true,
   T>::type`. `names.sh` must agree on `_Z2m1IiEiT_`, `_Z2m3IiEvT_`, `_Z2m5IiEiT_`,
   `_Z2m2IiEN9enable_ifILb1ET_E4typeES1_` on both Itanium targets. **This case exists before
   the reader does**, and is the measurement that the gate of 2.4 moved nothing else: the
   emit golden at -O0 must read `0 of N changed` for it.
2. **`template-sfinae-expression.cpp`** - today an `.error` case; becomes an `.expected`
   case holding p05's `f` family and p01's operators (the whole table of 1.2's first half
   over `enable_if`), printing which overload each call chose. `names.sh` on every `f1`-`f37`
   name of `probes/p01_sizeof.x86_64-linux-gnu.names`.
3. **`trailing-return-template-decltype.cpp`** - today `-refused.error`; p02's `g` family,
   p06's `m5`/`m6`/`m14`-`m32`, with `Dt` against `DT` decided per row of 1.1 and
   `fp_`/`fp0_`, and p07's `T::count()`/`T::value` (`sr T_`, 1.3). `names.sh` on all of
   them, and one shape p02 lacks: an **unnamed** parameter before the one the return type
   reads, `-> decltype(u)` on `(T, U u)`, which the ABI numbers `fp0_` and no probe here
   measured.
4. **`template-sfinae-leading-decltype.cpp`** - p03's `h` family: `cv T_ _ E`, `cv T_ <e>`,
   `cv T_ _ <e> <e> E`, every literal type of 1.2, `sr T_`, a free call over `T()`.
5. **`template-sfinae-fallback.cpp`** - p05 as written, `probes/p05_fail.expected` its
   `.expected`: the three shapes with a template that fails and an `f(...)`.
6. **`template-sfinae-substitution-table.cpp`** - p04: the same type twice inside and outside
   the expression, a parameter named twice, `Dt`/`DT` as a candidate; this is the case the
   candidate-order risk of 1.3 is gated on.
7. **`template-nontype-expr.cpp`** loses its `.nonames` for x86_64-linux and arm64-darwin:
   `V<N + M>` is spelled `VIXplT_T0_EE` once a non-type argument that is an expression goes
   through `TemplateArg::expr` - the recorded divergence closes, and `names.sh` says so.
   The run-time half of that case is unchanged.
8. **`template-sfinae-expression-refused.cpp`** (`.error`) - one shape per row of section 5
   that the sweep's grid can reach, each refused at the declaration with its own text.
9. `tools/feature-sweep`: the `sfinae` row moves from "refused by name" to "compiles"
   (71/23/0 → 72/22/0 of 94); add rows `sfinae-decltype-trailing` and
   `sfinae-decltype-leading` as accept rows.
10. `tests/overload/`: `sfinae-expression-ambiguous.cpp` - two templates both viable after
    substitution, differing only in their `enable_if` return types, which clang refuses as
    ambiguous and cpp11 must too; and `sfinae-expression-nontemplate-wins.cpp`, a
    non-template `f(int)` beside a viable template.

**Per target**, `tests/names.sh`: x86_64-linux and arm64-darwin must agree on every symbol
in `probes/*.x86_64-linux-gnu.names` and `*.arm64-apple-darwin.names` that the cases carry;
x86_64-windows must agree on `probes/*.x86_64-pc-windows-msvc.names` **and must not have
moved** for any existing case - the Microsoft mangler is not touched, and a moved Windows
name is a sign the substituted signature changed.

**At risk** - every case whose signature is read as a pattern: `template-sfinae`,
`template-member`, `template-member-deduced`, `template-nontype-expr`,
`template-id-qualified-and-deduced`, `template-dependent-base`, `tuple`,
`forwarding-reference`, `alias-template`, everything including `<type_traits>` (`enable_if`,
`_bool_constant`, `integral_constant` - the 1.13 `template <class T, T v>`), `<utility>`
(`move`/`forward`, the one case whose emission moved in WS-E1) and `<memory>`. The emit
golden recorded at `ddb9c13` must read `0 of N changed` after step 2.4 and after the
reader, with the only changed files being the new cases' own and `template-nontype-expr`'s
two Itanium symbols. `tests/run.sh` 725/0, `tests/overload.sh` 33/0,
`tools/exclusions --check` 0/0, `make comments` 0, then `tools/verify-three` for the Linux
g++ build (the design's C++14 is ordinary; a `std::string_view` cannot slip in through a
`DepExpr`) and the Windows names-vs-cl.

## 7. Files and functions, in order, with sizes

| step | file | change | lines |
| --- | --- | --- | --- |
| 1 | `src/parser/ParserTemplate.cpp` `instantiateClass` (`:2135`), `src/parser/ParserType.cpp` `memberTypeWalk` (`:19`); a `Parser::dependentType(const Type *)` in `Parser.h`/`ParserTemplate.cpp` | the shallow and the `DependentMember` roads fire only for a dependent type (2.4). Commit on its own with case 1 | ~35 |
| 2 | `src/Type.h`, `src/Type.cpp` | `struct DepExpr`; `Kind::DependentExpr`; `Type::expr_` + `expr()`; `TemplateArg::expr`; `TypeTable::dependentExpr`, `TypeTable::depExpr(...)` interning by key; `describe()` row | ~90 |
| 3 | `src/parser/ParserDependent.cpp` (new), `Parser.h` | `dependentAhead`, `dependentExpression` and its ladder, the leaf classification of 2.3, the key builder (one preorder walk writing the Itanium spelling with no substitutions), the refusals of section 5 | ~450 |
| 4 | `src/parser/ParserTemplate.cpp` `templateArguments` (`:1021`), `specializationKey` (`:1049`), `matchPattern` (`:1872`) | a non-type argument that `dependentAhead` says is dependent → `a.expr`; `$X<key>`; `expr` equality | ~25 |
| 5 | `src/parser/ParserExpr.cpp` `decltypeSpecifier` (`:1645`), `ParserTopLevel.cpp` `readTrailingReturn` (`:1763`), `ParserTemplate.cpp` `readTemplateDeclaration` (`:345`), `ParserExpr.cpp:2596` | under `patternOnly_` a dependent operand goes to the reader and answers `types_.dependentExpr(...)`; the parameters declared in the trailing scope carry their index (a field on `Local` or a count of `isParameter` entries in the scope); `refuseTemplateTrailingDecltype` deleted (`ParserTopLevel.cpp:1838-1849`, the `Parser.h` declaration, the call at `:345`); the `sizeof` refusal reworded as the fallback of section 5 | ~40, -15 |
| 6 | `src/Mangle.cpp` `Itanium` | `expression(const DepExpr *)` (~120: the operator table of 1.2, `L<type><value>E` with `Ld`/`Lf` hex, `st`/`sz`, `cv` one- and many-argument forms, `sc`, `cl`, `dt`/`pt`, `sr T_`, `ix`, `qu`, `fp`; type operands through `type()`, which pushes them); `type()` branch for `Kind::DependentExpr` (`Dt`/`DT`, pushed at its `E`); `templateArgument()` branch for `a.expr` (`X...E`) | ~150 |
| 7 | `tests/cases/`, `tests/overload/`, `tools/feature-sweep`, `docs/EXCLUSIONS.md` (regenerated), `docs/CONFORMANCE.md` (the postfix-increment choice if ever taken), CLAUDE.md section | section 6 | cases only |

About 800 lines added and 20 removed, across 8 source files and one new one. Step 1 is
shippable alone and fixes two defects; steps 2-6 are one feature and land together, since a
`DepExpr` the mangler cannot spell is worse than the refusal it replaces.

### What must NOT change

- **The two reads of a signature** - substituted first, pattern second, Itanium spelled from
  the pattern and Microsoft from the substituted - in `Parser::instantiate`. The expression
  is a third thing the pattern carries, not a reason to read once.
- **The Microsoft mangler**, byte for byte (1.4). A diff of `Mangle.cpp` that touches
  `class Microsoft` is wrong.
- **`Source::fail`'s refusal rule** (`Source.cpp:108`) and the four `f.unsupported`
  re-raises. The reader's messages are written to pass it; nothing is added to the marker
  list to make them pass.
- **`Trial`'s put-back list** (`ParserTemplate.cpp:1806`): the reader adds no parser state,
  so the list does not grow. If a later shape wants state, the list grows *first*.
- **The shallow instantiation for a dependent template-id** (`:2135`) and the
  `DependentMember` for a dependent owner (`ParserType.cpp:19`) - they are narrowed to
  dependent types and kept, not replaced by the reader: `Holder<T>` still has no size to
  lay out.
- **`deduceOne`'s "Nothing to deduce here" fallthrough** (`:1532`) - the new kinds are a
  non-deduced context *because* of it.
- **`Kind::TemplateParam`'s index in `length_` and `T_`/`T0_` at `Mangle.cpp:546`**, which
  the expression's `TemplateParam` leaf reuses by calling `type()` on the same `Type`.
- **Absolute token indices** - the reader reads forward from `at_` and never inserts.
- **`constantExpression` for a non-dependent argument** - `enable_if<true, T>` must still
  fold to `Lb1E` with the parameter's type (m2), which today's road does.
- **The declaration-time read in `templatedName`** reading the whole declarator: a refusal
  of section 5 fires there, at the declaration, as today's two do.

## 8. Risks, in order

1. **The reader's dependence gate and the pattern read's shallow instantiation** are two
   predicates that must agree about what "depends on a parameter" means (2.3, 2.4): a
   template-id gated dependent by one and not the other is a shallow class the substituted
   read finds (D1) or a `DependentMember` of a resolved type (D2) in a new place. One
   function, `dependentType`, asked by both, and the sweep of every template-using case
   under the golden. The substitution table is *not* a risk: 1.3 recounts every probe to
   the rule the mangler already follows, and case 6 holds the names that would say otherwise.
2. **A substituted-read failure message that ends in "yet"** turns a dropped candidate into
   a hard error (3.2). Grep `src/parser/*.cpp` for messages ending " yet" that a
   `decltype` operand or a member call can reach before writing the cases, and write case 5's
   failing shapes to meet them.
3. **The non-dependent gate of 2.4** changes which template-ids are instantiated for real
   during a pattern read. A real instantiation in the middle of a signature can declare
   members and grow `functions_` - the C-01 shape - so the first commit is step 1 alone,
   under the golden and all four suites.
4. **`fp_` numbering counts unnamed parameters** by the ABI's rule; no probe here has an
   unnamed parameter before a named one that the return type reads. Add one to case 3 and
   let clang settle it.
5. **A `DependentExpr` type leaking past the pattern** - into `size()`, a backend, a
   `describe()` printed to a user. `TemplateParam` set the precedent and nothing leaks it;
   the new kind follows the same three doors (`isComplete()` false, `size()` refuses, the
   backends never see a pattern).
