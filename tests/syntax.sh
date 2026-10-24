#!/bin/sh
# The syntax tree the grammar records (M2 step 2, doc-cxx3/M2-PLAN.md), dumped with -dump=syntax and compared with
# tests/syntax/<case>.syntax. There is no outside oracle for a syntax tree: each recorded dump was read by hand when
# it was written, and what checks the tree is M2.10's, its types against the Ast.h the parser built beside it.
CXX1="${CXX1:-./cxx3.exe}"
pass=0; fail=0
for src in tests/syntax/*.cpp; do
    base=$(basename "$src" .cpp)
    out=tests/out-syntax/$base
    mkdir -p tests/out-syntax
    if ! "$CXX1" -nologo -dump=syntax -S "$src" -o "$out.s" > "$out.syntax" 2> "$out.err"; then
        echo "FAIL $base: did not compile"; sed 's/^/      /' "$out.err" | head -3; fail=$((fail + 1)); continue
    fi
    if diff -u "tests/syntax/$base.syntax" "$out.syntax" > "$out.diff"; then pass=$((pass + 1))
    else echo "FAIL $base: the syntax differs"; head -12 "$out.diff" | sed 's/^/      /'; fail=$((fail + 1)); fi
done
echo "syntax.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
