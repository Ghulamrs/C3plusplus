#!/bin/sh
# The adapter's Ast.h held against the parser's (M2.11, doc-cxx3/M2-PLAN.md): -check=adapted over every case that
# compiles for this host, which fails where an adapted function's body differs from the parser's; and the case
# compiled with -front=typed and without, at -O0 and -O2, whose assembly must be byte for byte the same.
CXX1="${CXX1:-./cxx3.exe}"
out=tests/out-adapted
rm -rf "$out"; mkdir -p "$out"
export CXX1 out
ls tests/cases/*.cpp | grep -v '\.part\.cpp$' |
    while read src; do [ -f "${src%.cpp}.error" ] || echo "$src"; done |
    xargs -P "${JOBS:-8}" -n 1 sh -c '
        b=$(basename "$0" .cpp)
        "$CXX1" -nologo -check=adapted -S "$0" -o /dev/null > "$out/$b.adapted" 2>&1
        for L in 0 2; do
            "$CXX1" -nologo -O$L -S "$0" -o "$out/$b.O$L.s" > /dev/null 2>&1 || { rm -f "$out/$b.O$L.s"; continue; }
            "$CXX1" -nologo -front=typed -O$L -S "$0" -o "$out/$b.O$L.typed.s" > /dev/null 2>&1
            cmp -s "$out/$b.O$L.s" "$out/$b.O$L.typed.s" || echo "$b -O$L" >> "$out/$b.moved"
        done'
pass=0; fail=0; skipped=0
for r in "$out"/*.adapted; do
    base=$(basename "$r" .adapted)
    line=$(grep -- '^-check=adapted:' "$r")
    if [ -z "$line" ]; then skipped=$((skipped + 1)); continue; fi
    if echo "$line" | grep -q ' 0 differ' && [ ! -f "$out/$base.moved" ]; then pass=$((pass + 1))
    else
        echo "FAIL $base"; grep -A2 ': line' "$r" | head -6 | sed 's/^/      /'
        [ -f "$out/$base.moved" ] && sed 's/^/      assembly moved with -front=typed: /' "$out/$base.moved"
        fail=$((fail + 1))
    fi
done
cat "$out"/*.adapted | grep -- '^-check=adapted:' |
    awk '{f+=$2; a+=$4; s+=$6} END {printf "adapted.sh: %d functions, %d adapted, %d the same as the parser'"'"'s\n", f, a, s}'
echo "adapted.sh: $pass passed, $fail failed, $skipped did not compile for this host"
[ "$fail" -eq 0 ]
