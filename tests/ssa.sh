#!/bin/sh
# The SSA IR verified over every case that compiles (M3b.1, doc-cxx3/M3-PLAN.md): -check=ssa lowers each source body
# the subset reaches and runs the verifier of M3-IR 3.4 over the module; a case fails where any function fails it. A
# case that does not compile for this host - a .error case, or one written for another target - has nothing to hold.
# The summary counts what was lowered and what was left outside, by reason, which is the measure of the subset; then
# the compiler-written functions the Builder lowered (M3b.3), by kind, and what it left, by kind and reason. At SSA_LEVEL
# (default 1) the SSA passes run and are verified after each (M3b.4): the allocas promoted, the folds, the CFG
# simplifications, the edges threaded, the values numbered, the divisions rewritten and the dead code removed are summed,
# with the volatile accesses lowered and kept (a pass but cfg removing one fails the case).
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
    else echo "FAIL $base: the verifier"; grep '^  ' "$r" | grep -v '^  outside\|^  synthesised\|^  inline refused' | head -5 | sed 's/^/      /'; fail=$((fail + 1)); fi
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
cat "$out"/*.ssa | grep -- '^-check=ssa cfg:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: cfg made %d constant branches and %d two-way branches to one place jumps, removed %d blocks no root reaches, merged %d, forwarded %d\n", s["constant"], s["same"], s["unreachable"], s["merged"], s["forwarded"]}'
cat "$out"/*.ssa | grep -- '^-check=ssa dead:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: dead removed %d instructions, %d block parameters, %d stores no load sees, %d frame objects\n", s["insts"], s["params"], s["stores"], s["allocas"]}'
cat "$out"/*.ssa | grep -- '^-check=ssa gvn:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: gvn replaced %d constants and addresses, %d arithmetic, %d comparisons, %d conversions, %d geps, %d selects; %d loads by a load, %d by a store\n", s["constants"], s["arith"], s["compares"], s["casts"], s["geps"], s["selects"], s["loads"], s["stored"]}'
cat "$out"/*.ssa | grep -- '^-check=ssa jumps:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: jumps threaded %d edges past a condbr, %d past a switch\n", s["condbr"], s["switch"]}'
cat "$out"/*.ssa | grep -- '^-check=ssa divide:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: divide made %d divisions shifts, %d a signed and %d an unsigned multiply-high (%d of them 64-bit), left %d by a constant\n", s["shifts"], s["signed"], s["unsigned"], s["wide"], s["left"]}'
cat "$out"/*.ssa | grep -- '^-check=ssa volatile:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: %d volatile loads and stores lowered, %d kept after the passes\n", s["lowered"], s["kept"]}'
cat "$out"/*.ssa | grep -- '^-check=ssa inline:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: inline found %d direct calls of a function of the unit, cloned %d in place, refused %d\n", s["sites"], s["taken"], s["refused"]}'
cat "$out"/*.ssa | grep -- '^  inline refused (' | sed 's/^  inline refused (\([0-9]*\)): /\1\t/' |
    awk -F'\t' '{s[$2]+=$1} END {for (k in s) printf "    %6d  refused: %s\n", s[k], k}' | sort -rn
cat "$out"/*.ssa | grep -- '^-check=ssa loops:' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "ssa.sh: loops found %d loops, made %d preheaders, hoisted %d instructions (%d divisions), widened %d counters\n", s["found"], s["preheaders"], s["hoisted"], s["divisions"], s["widened"]}'
# The probes of the passes: <case>.<pass> is the case's -dump=ssa=<pass> on x86_64-linux, compared byte for byte, at -O1
# (-O2 for divide and loops, which run at that level alone).
if [ "$(uname -s)" = Linux ]; then
    for d in tests/cases/*.promote tests/cases/*.fold tests/cases/*.jumps tests/cases/*.cfg tests/cases/*.gvn tests/cases/*.divide tests/cases/*.loops tests/cases/*.dead tests/cases/*.inline; do
        [ -f "$d" ] || continue
        p=${d##*.}
        lv=1; { [ "$p" = divide ] || [ "$p" = loops ] || [ "$p" = inline ]; } && lv=2
        if "$CXX1" -nologo -O$lv -dump=ssa=$p -S "${d%.*}.cpp" -o /dev/null 2>/dev/null | cmp -s - "$d"; then pass=$((pass + 1))
        else echo "FAIL $(basename "$d"): the IR after $p differs from the recorded probe"; fail=$((fail + 1)); fi
    done
    # Selection's probe (round 7): <case>.allocate is the case's -front=ssa -O1 -dump=mir=allocate, MIR as the allocator left it.
    for d in tests/cases/*.allocate; do
        [ -f "$d" ] || continue
        if "$CXX1" -nologo -front=ssa -O1 -dump=mir=allocate -S "${d%.*}.cpp" -o /dev/null 2>/dev/null | cmp -s - "$d"; then pass=$((pass + 1))
        else echo "FAIL $(basename "$d"): MIR after allocate differs from the recorded probe"; fail=$((fail + 1)); fi
    done
fi
echo "ssa.sh: $pass passed, $fail failed, $skipped did not compile for this host"
[ "$fail" -eq 0 ]
