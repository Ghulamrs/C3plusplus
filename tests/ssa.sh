#!/bin/sh
# The SSA IR verified over every case that compiles (M3b.1, doc-cxx3/M3-PLAN.md): -check=ssa lowers each source body
# the subset reaches and runs the verifier of M3-IR 3.4 over the module; a case fails where any function fails it. A
# case that does not compile for this host - a .error case, or one written for another target - has nothing to hold.
# The summary counts what was lowered and what was left outside, by reason, which is the measure of the subset.
CXX1="${CXX1:-./cxx3.exe}"
out=tests/out-ssa
rm -rf "$out"; mkdir -p "$out"
export CXX1 out
ls tests/cases/*.cpp | grep -v '\.part\.cpp$' |
    while read src; do [ -f "${src%.cpp}.error" ] || echo "$src"; done |
    xargs -P "${JOBS:-8}" -n 1 sh -c '"$CXX1" -nologo -check=ssa -S "$0" -o /dev/null > "$out/$(basename "$0" .cpp).ssa" 2>&1'
pass=0; fail=0; skipped=0
for r in "$out"/*.ssa; do
    base=$(basename "$r" .ssa)
    line=$(grep -- '^-check=ssa:' "$r")
    if [ -z "$line" ]; then skipped=$((skipped + 1)); continue; fi
    if echo "$line" | grep -q ' failed=0$'; then pass=$((pass + 1))
    else echo "FAIL $base: the verifier"; grep '^  ' "$r" | grep -v '^  outside' | head -5 | sed 's/^/      /'; fail=$((fail + 1)); fi
done
cat "$out"/*.ssa | grep -- '^-check=ssa:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: %d source bodies, %d lowered to SSA, %d outside the subset, %d verified, %d failed\n", s["functions"], s["lowered"], s["outside"], s["ok"], s["failed"]}'
cat "$out"/*.ssa | grep -- '^  outside (' | sed 's/^  outside (\([0-9]*\)): /\1\t/' |
    awk -F'\t' '{s[$2]+=$1} END {for (k in s) printf "    %6d  %s\n", s[k], k}' | sort -rn | head -40
echo "ssa.sh: $pass passed, $fail failed, $skipped did not compile for this host"
[ "$fail" -eq 0 ]
