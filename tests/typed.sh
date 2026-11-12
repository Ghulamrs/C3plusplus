#!/bin/sh
# Sema's typing of the syntax tree (M2.10; since M3b.7 with no Ast.h to hold it against): -check=typed over every case
# that compiles, which fails a case whose census does not hold (functions = bodies + compiler-written) or whose copied
# record differs from the parser's. The sum of untyped= may not grow past tests/typed.untyped. A case that does not
# compile for this host - a .error case, or one written for another target - has nothing to count.
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
    line=$(grep -- '^-check=typed: functions=' "$r")
    if [ -z "$line" ]; then skipped=$((skipped + 1)); continue; fi
    if echo "$line" | grep -q ' copy=0 ' && ! grep -q 'neither a body nor compiler-written' "$r"; then pass=$((pass + 1))
    else echo "FAIL $base: the census or a copied record"; grep "not the parser's\|neither a body" "$r" | head -5 | sed 's/^/      /'; fail=$((fail + 1)); fi
done
# The report is key=value fields (M2.15): the census of every emitted function, then the nodes under the bodies.
sums=$(cat "$out"/*.typed | grep -- '^-check=typed: functions=' | tr ' ' '\n' | grep = | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "%d %d %d %d %d %d %d %d", s["functions"], s["bodies"], s["synthesised"], s["nodes"], s["typed"], s["untyped"], s["recorded"], s["entity"]}')
set -- $sums
echo "typed.sh: $1 functions emitted, $2 with a source body, $3 compiler-written; $4 nodes, $5 typed, $6 untyped ($7 typed from a Value record, $8 from an entity's)"
cat "$out"/*.typed | grep -- '^  compiler-written:' | sed 's/^  compiler-written: //' | tr ',' '\n' | sed 's/^ //' | tr '=' ' ' |
    awk '{s[$0]=0} {n=$NF; $NF=""; s[$0]+=n} END {for (k in s) if (s[k] > 0) printf "    %s%d\n", k, s[k]}' | sort
cat "$out"/*.typed | grep -- '^  untyped:' | sed 's/^  untyped: //' | tr ',' '\n' | sed 's/^ //' | tr '=' ' ' |
    awk '{s[$1]+=$2} END {printf "typed.sh: untyped by kind:"; for (k in s) printf " %s=%d", k, s[k]; printf "\n"}'
# The number is recorded per host (uname -s): a case written for another target is not compiled here.
held=$(awk -v h="$(uname -s)" '$1 == h {print $2}' tests/typed.untyped)
if [ -z "$held" ]; then echo "typed.sh: untyped $6, nothing recorded for $(uname -s) in tests/typed.untyped"
elif [ "$6" -gt "$held" ]; then echo "FAIL untyped: $6, more than the $held recorded in tests/typed.untyped"; fail=$((fail + 1))
elif [ "$6" -lt "$held" ]; then echo "typed.sh: untyped $6 is below the $held recorded - re-record tests/typed.untyped"; fi
echo "typed.sh: $pass passed, $fail failed, $skipped did not compile for this host"
[ "$fail" -eq 0 ]
