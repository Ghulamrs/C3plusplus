#!/bin/sh
# The SSA IR verified over every case that compiles (M3b.1, doc-cxx3/M3-PLAN.md): -check=ssa lowers each source body
# the subset reaches and runs the verifier of M3-IR 3.4 over the module; a case fails where any function fails it. A
# case that does not compile for this host - a .error case, or one written for another target - has nothing to hold.
# The summary counts what was lowered and what was left outside, by reason, which is the measure of the subset; then
# the compiler-written functions the Builder lowered (M3b.3), by kind, and what it left, by kind and reason. At SSA_LEVEL
# (default 1) the SSA passes run and are verified after each (M3b.4): the allocas promoted and the folds are summed.
CXX1="${CXX1:-./cxx3.exe}"
SSA_LEVEL="${SSA_LEVEL:-1}"
out=tests/out-ssa
rm -rf "$out"; mkdir -p "$out"
export CXX1 out SSA_LEVEL
ls tests/cases/*.cpp | grep -v '\.part\.cpp$' |
    while read src; do [ -f "${src%.cpp}.error" ] || echo "$src"; done |
    xargs -P "${JOBS:-8}" -n 1 sh -c '"$CXX1" -nologo -O"$SSA_LEVEL" -check=ssa -S "$0" -o /dev/null > "$out/$(basename "$0" .cpp).ssa" 2>&1'
pass=0; fail=0; skipped=0
for r in "$out"/*.ssa; do
    base=$(basename "$r" .ssa)
    line=$(grep -- '^-check=ssa:' "$r")
    if [ -z "$line" ]; then skipped=$((skipped + 1)); continue; fi
    passes=$(grep -- '^-check=ssa passes:' "$r")
    if echo "$line" | grep -q ' failed=0$' && { [ -z "$passes" ] || echo "$passes" | grep -q ' failed=0$'; }; then pass=$((pass + 1))
    else echo "FAIL $base: the verifier"; grep '^  ' "$r" | grep -v '^  outside\|^  synthesised' | head -5 | sed 's/^/      /'; fail=$((fail + 1)); fi
done
cat "$out"/*.ssa | grep -- '^-check=ssa:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: %d source bodies, %d lowered to SSA, %d outside the subset, %d verified, %d failed\n", s["functions"], s["lowered"], s["outside"], s["ok"], s["failed"]}'
cat "$out"/*.ssa | grep -- '^  outside (' | sed 's/^  outside (\([0-9]*\)): /\1\t/' |
    awk -F'\t' '{s[$2]+=$1} END {for (k in s) printf "    %6d  %s\n", s[k], k}' | sort -rn | head -40
cat "$out"/*.ssa | grep -- '^-check=ssa synthesised:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: %d compiler-written functions, %d lowered to SSA, %d outside\n", s["functions"], s["lowered"], s["outside"]}'
cat "$out"/*.ssa | grep -- '^  synthesised lowered (' | sed 's/^  synthesised lowered (\([0-9]*\)): /\1\t/' |
    awk -F'\t' '{s[$2]+=$1} END {for (k in s) printf "    %6d  lowered: %s\n", s[k], k}' | sort -rn
cat "$out"/*.ssa | grep -- '^  synthesised outside (' | sed 's/^  synthesised outside (\([0-9]*\)): /\1\t/' |
    awk -F'\t' '{s[$2]+=$1} END {for (k in s) printf "    %6d  outside: %s\n", s[k], k}' | sort -rn | head -40
cat "$out"/*.ssa | grep -- '^-check=ssa passes:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: at -O%d, %d frame objects, %d of a scalar type, %d promoted, %d failed after a pass\n", ENVIRON["SSA_LEVEL"], s["allocas"], s["scalars"], s["promoted"], s["failed"]}'
cat "$out"/*.ssa | grep -- '^-check=ssa fold:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: fold made %d constants, %d comparisons, %d conversions, %d floating, %d identities, %d copies, %d block arguments one value, %d left to run\n", s["constants"], s["compares"], s["casts"], s["floating"], s["identities"], s["copies"], s["arguments"], s["kept"]}'
# The probes of the passes: <case>.<pass> is the case's -O1 -dump=ssa=<pass> on x86_64-linux, compared byte for byte.
if [ "$(uname -s)" = Linux ]; then
    for d in tests/cases/*.promote tests/cases/*.fold; do
        [ -f "$d" ] || continue
        p=${d##*.}
        if "$CXX1" -nologo -O1 -dump=ssa=$p -S "${d%.*}.cpp" -o /dev/null 2>/dev/null | cmp -s - "$d"; then pass=$((pass + 1))
        else echo "FAIL $(basename "$d"): the IR after $p differs from the recorded probe"; fail=$((fail + 1)); fi
    done
fi
echo "ssa.sh: $pass passed, $fail failed, $skipped did not compile for this host"
[ "$fail" -eq 0 ]
