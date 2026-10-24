#!/bin/sh
# Sema's typing of the syntax tree held against the parser's Ast.h (M2.10, doc-cxx3/M2-PLAN.md): -check=typed over
# every case that compiles, which fails a case where any typed node differs from the type the parser gave it. A case
# that does not compile for this host - a .error case, or one written for another target - has nothing to hold.
CXX1="${CXX1:-./cxx3.exe}"
out=tests/out-typed
rm -rf "$out"; mkdir -p "$out"
export CXX1 out
ls tests/cases/*.cpp | grep -v '\.part\.cpp$' |
    while read src; do [ -f "${src%.cpp}.error" ] || echo "$src"; done |
    xargs -P "${JOBS:-8}" -n 1 sh -c '"$CXX1" -nologo -check=typed -S "$0" -o /dev/null > "$out/$(basename "$0" .cpp).typed" 2>&1'
pass=0; fail=0; skipped=0
for r in "$out"/*.typed; do
    base=$(basename "$r" .typed)
    line=$(grep -- '^-check=typed:' "$r")
    if [ -z "$line" ]; then skipped=$((skipped + 1)); continue; fi
    if echo "$line" | grep -q ' 0 differ'; then pass=$((pass + 1))
    else echo "FAIL $base: a typed node differs"; grep 'the parser says' "$r" | head -5 | sed 's/^/      /'; fail=$((fail + 1)); fi
done
cat "$out"/*.typed | grep -- '^-check=typed:' |
    awk '{t+=$2; a+=$4; m+=$8} END {printf "typed.sh: %d nodes typed, %d agree, %d with no Ast.h node\n", t, a, m}'
echo "typed.sh: $pass passed, $fail failed, $skipped did not compile for this host"
[ "$fail" -eq 0 ]
