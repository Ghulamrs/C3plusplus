#!/bin/sh
# M8.1-early's switch held by a gate (R109): the stand-in M8.2 deletes (Q6) is off by default, so nothing else runs it.
#   1. run.sh and typed.sh with CXX1_TEMPLATE_SUBST=1, at -O0 and -O2 (this host's target);
#   2. the census over every case with the switch on: by-substitution against the function door, each replay counted by reason;
#   3. the probes: template-subst substitutes, template-subst-refused substitutes none of its refused shapes;
#   4. on = off: -S text and diagnostics of every case that reaches the function door, for x86_64-linux, x86_64-windows
#      (masm, or gnu for a .nomasm case) and arm64-darwin at -O0 and -O2 - the C6000 is refused whole, so not compared.
#   5. bodies (M8.1, kept past M8.2): -check=templates over every case - each template body a replay reads, held against
#      the first read of the same body, summed; a case with a .templates beside it prints that record.
# It fails on any failed case, a probe that does not count as recorded, or one file that differs.
#   tests/subst.sh [run|typed|census|same|bodies]...   (all five by default)
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx3.exe}"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
OUT=tests/out-subst
export CXX1 OUT
steps="${*:-run typed census same bodies}"
bad=0
mkdir -p "$OUT"
for step in $steps; do case "$step" in
run|typed)
    for level in -O0 -O2; do
        # typed.sh quotes $CXX1 as one word: the level goes in through a wrapper.
        log="$OUT/$step$level.log"; w="$(pwd)/$OUT/cxx$level"
        printf '#!/bin/sh\nexec %s %s "$@"\n' "$CXX1" "$level" > "$w"; chmod +x "$w"
        CXX1_TEMPLATE_SUBST=1 CXX1="$w" JOBS=$JOBS sh tests/$step.sh > "$log" 2>&1; rc=$?
        line=$(grep "^$step.sh: [0-9]* passed" "$log" | tail -1)
        echo "subst.sh $step $level: $line (rc $rc)"
        case "$line" in "$step.sh: 0 passed"*|"") rc=1 ;; esac
        [ "$rc" -eq 0 ] || { grep '^FAIL' "$log" | head -10; bad=$((bad + 1)); }
    done ;;
census)
    ls tests/cases/*.cpp | grep -v '\.part\.cpp$' | while read src; do [ -f "${src%.cpp}.error" ] || echo "$src"; done |
        xargs -P "$JOBS" -n 1 sh -c 'CXX1_TEMPLATE_SUBST=1 CXX1_TEMPLATE_CENSUS=1 $CXX1 -nologo -S "$0" -o /dev/null 2> "$OUT/$(basename "$0" .cpp).census" > /dev/null'
    cat "$OUT"/*.census | grep '^template-census:' | tr ' ' '\n' | grep = | tr '=' ' ' |
        awk -v units="$(cat "$OUT"/*.census | grep -c '^template-census:')" '{s[$1]+=$2} END {printf "subst.sh census: %d by substitution, %d by replay of %d function bodies over %d units\n", s["by-substitution"], s["function"], s["function"] + s["by-substitution"], units}'
    cat "$OUT"/*.census | sed -n 's/^template-replayed (\([0-9]*\)): /\1 /p' |
        awk '{n=$1; $1=""; s[$0]+=n} END {for (k in s) printf "    %6d%s\n", s[k], k}' | sort -rn
    # The probes, against what they were written to show.
    got=$(sed -n 's/.* by-substitution=\([0-9]*\).*/\1/p' "$OUT/template-subst.census")
    [ "$got" = 16 ] || { echo "FAIL template-subst: by-substitution=$got, not 16"; bad=$((bad + 1)); }
    r="$OUT/template-subst-refused.census"
    got=$(sed -n 's/.* by-substitution=\([0-9]*\).*/\1/p' "$r")
    [ "$got" = 1 ] || { echo "FAIL template-subst-refused: by-substitution=$got, not 1 (ident<unsigned> alone)"; bad=$((bad + 1)); }
    for why in "a conversion between two parameters' types" "a bool argument" "a name the template does not declare" "a token outside the subset: a literal"; do
        grep -q "^template-replayed ([0-9]*): $why" "$r" || { echo "FAIL template-subst-refused: no replay counted as \"$why\""; bad=$((bad + 1)); }
    done
    # -g refuses every one: a substituted body has no block table.
    got=$(CXX1_TEMPLATE_CENSUS=1 $CXX1 -nologo -g -templates=subst -S tests/cases/template-subst.cpp -o /dev/null 2>&1 | sed -n 's/.* by-substitution=\([0-9]*\).*/\1/p')
    [ "$got" = 0 ] || { echo "FAIL template-subst -g: by-substitution=$got, not 0"; bad=$((bad + 1)); } ;;
same)
    [ -n "$(ls "$OUT"/*.census 2>/dev/null)" ] || { echo "subst.sh same: run the census step first"; exit 2; }
    grep -l '^template-census: function=[1-9]' "$OUT"/*.census | sed 's,.*/,,; s,\.census$,,' > "$OUT/reached"
    for t in x86_64-linux x86_64-windows arm64-darwin; do for level in -O0 -O2; do sed "s/\$/ $t $level/" "$OUT/reached"; done; done |
        xargs -P "$JOBS" -n 3 sh -c '
            base=$0; t=$1; level=$2; src=tests/cases/$base.cpp; o=$OUT/$base.$t$level; spell=
            [ "$t" = x86_64-windows ] && [ -f "tests/cases/$base.nomasm" ] && spell=-masm=gnu
            if [ -f "tests/cases/$base.notarget" ] && grep -q "^$t\b" "tests/cases/$base.notarget"; then exit 0; fi
            ( ulimit -t 10; $CXX1 -nologo -S -arch $t $spell $level "$src" -o $o.off.s > $o.off.err 2>&1 )
            ( ulimit -t 10; CXX1_TEMPLATE_SUBST=1 $CXX1 -nologo -S -arch $t $spell $level "$src" -o $o.on.s > $o.on.err 2>&1 )
            if cmp -s $o.off.s $o.on.s && cmp -s $o.off.err $o.on.err; then echo same > $o.verdict; else echo differ > $o.verdict; fi
            rm -f $o.off.s $o.on.s'
    n=$(ls "$OUT"/*.verdict 2>/dev/null | wc -l); d=$(grep -l differ "$OUT"/*.verdict 2>/dev/null | wc -l)
    echo "subst.sh same: $(wc -l < "$OUT/reached") cases reach the function door; $n compared on against off, $((n - d)) the same, $d differ"
    grep -l differ "$OUT"/*.verdict 2>/dev/null | sed 's,.*/,    differ ,; s,\.verdict$,,' | head -20
    [ "$d" -eq 0 ] || bad=$((bad + 1)) ;;
bodies)
    ls tests/cases/*.cpp | grep -v '\.part\.cpp$' | while read src; do [ -f "${src%.cpp}.error" ] || echo "$src"; done |
        xargs -P "$JOBS" -n 1 sh -c '( ulimit -t 20; $CXX1 -nologo -check=templates -S "$0" -o /dev/null > "$OUT/$(basename "$0" .cpp).bodies" 2> /dev/null )'
    cat "$OUT"/*.bodies | grep '^-check=templates: bodies=' | tr ' ' '\n' | grep = | tr '=' ' ' |
        awk -v units="$(cat "$OUT"/*.bodies | grep -c '^-check=templates: bodies=')" '{s[$1]+=$2} END {printf "subst.sh bodies: %d replayed over %d units: %d first reads, %d the same as the first, %d differ in text alone, %d differ in shape\n", s["bodies"], units, s["held"], s["same"], s["text"], s["shape"]}'
    echo "    $(cat "$OUT"/*.bodies | grep -c '^-check=templates shape: ') bodies whose replays differ in shape, in $(grep -l '^-check=templates shape: ' "$OUT"/*.bodies | wc -l | tr -d ' ') units"
    for rec in tests/cases/*.templates; do base=$(basename "$rec" .templates)
        cmp -s "$OUT/$base.bodies" "$rec" || { echo "FAIL $base: -check=templates is not the record"; diff "$OUT/$base.bodies" "$rec" | head -10; bad=$((bad + 1)); }
    done ;;
*) echo "subst.sh: no step '$step' (run, typed, census, same, bodies)"; exit 2 ;;
esac; done
echo "subst.sh: $bad failed"
[ "$bad" -eq 0 ]
