#!/bin/sh
# M8.2's switch held by a gate (R109): -templates=subst is off by default, so nothing else runs it.
#   1. run.sh and typed.sh with CXX1_TEMPLATE_SUBST=1, at -O0 and -O2 (this host's target);
#   2. the census over every case with the switch on: the function door, each body made with no replay or replayed by reason;
#   3. the probes, on M8.2's census: template-subst's shapes made with no replay, template-subst-call's calls bound at the
#      definition ([temp.nondep]), template-subst-depcall's dependent-name calls resolved at the instantiation, the rest by reason;
#   4. bodies (M8.1, kept past M8.2): -check=templates over every case - each template body a replay reads, held against
#      the first read of the same body, summed; a case with a .templates beside it prints that record.
#   5. self (M8.1): -check=templates over the compiler's own sources, as tools/self-host lists them.
# **The ceiling** (M8.1 commit 5's first condition): a body a read declines is a refusal by name, and bodies and self
# hold each door's count by reason under tests/subst-cases.ceiling and tests/subst-self.ceiling - a reason not recorded
# or a count above its record fails; a count below asks for the record again (SUBST_RECORD=1 writes both).
# It fails on any failed case or a probe that does not count as recorded.
#   tests/subst.sh [run|typed|census|bodies|self]...   (all five by default)
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx3.exe}"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
OUT=tests/out-subst
export CXX1 OUT
steps="${*:-run typed census bodies self}"
bad=0
mkdir -p "$OUT"
# ceiling <corpus> <suffix>: from $OUT/*.<suffix>, the unread bodies ("count door | reason"), each door's sums ("door <name>
# bodies= read= unread= shape=") and each read whose tree differs in shape from its replay ("shape <file:line:col>"), against
# the record: a reason new or above its count, a door's read below or shape above its record, or a new shape fails (M8rev M3).
ceiling() {
    rec=tests/subst-$1.ceiling; now=$OUT/$1.unread
    { cat "$OUT"/*."$2" | tr -d '\r' | sed -n 's/^-check=templates unread\([^(]*\)(\([0-9]*\)): /\2 \1| /p' |
          awk '{n=$1; $1=""; s[$0]+=n} END {for (k in s) printf "%d%s\n", s[k], k}' | LC_ALL=C sort -t' ' -k2
      cat "$OUT"/*."$2" | tr -d '\r' | sed -n 's/^-check=templates declarations: functions=/door functions bodies=/p
          s/^-check=templates declarations \([^:]*\): bodies=/door \1 bodies=/p' |
          awk '{if (!($2 in b)) d[++k] = $2; for (i = 3; i <= NF; i++) { split($i, kv, "="); v[$2, kv[1]] += kv[2] } b[$2] = 1}
               END {for (i = 1; i <= k; i++) printf "door %s bodies=%d read=%d unread=%d shape=%d\n", d[i], v[d[i], "bodies"], v[d[i], "read"], v[d[i], "unread"], v[d[i], "shape"]}' | LC_ALL=C sort
      cat "$OUT"/*."$2" | tr -d '\r' | sed -n 's|^-check=templates declaration shape: \(.*[/\\]\)\{0,1\}\([^/\\ ]*:[0-9]*:[0-9]*\)$|shape \2|p' | LC_ALL=C sort -u
    } > "$now"
    [ -z "$SUBST_RECORD" ] || cp "$now" "$rec"
    [ -f "$rec" ] || { echo "FAIL $1: no $rec (SUBST_RECORD=1 writes it)"; bad=$((bad + 1)); return; }
    awk -v corpus="$1" -v rec="$rec" '
        function field(line, name,   i, kv, w) { split(line, w, " "); for (i in w) if (split(w[i], kv, "=") == 2 && kv[1] == name) return kv[2] + 0; return 0 }
        FILENAME == rec && $1 == "door" { door[$2] = $0; next }
        FILENAME == rec && $1 == "shape" { shape[$2] = 1; ns++; next }
        FILENAME == rec { n = $1; $1 = ""; c[$0] = n; ct += n; nr++; next }
        $1 == "door" { sd[$2] = 1
          if (!($2 in door)) { printf "FAIL %s: a door the ceiling does not record: %s\n", corpus, $0; f++; next }
          if (field($0, "read") < field(door[$2], "read")) { printf "FAIL %s: door %s read %d, below the floor of %d\n", corpus, $2, field($0, "read"), field(door[$2], "read"); f++ }
          else if (field($0, "read") > field(door[$2], "read")) low++
          if (field($0, "shape") > field(door[$2], "shape")) { printf "FAIL %s: door %s shape %d, above the record of %d\n", corpus, $2, field($0, "shape"), field(door[$2], "shape"); f++ }
          next }
        $1 == "shape" { if (!($2 in shape)) { printf "FAIL %s: a read differs in shape from its replay at %s, not recorded\n", corpus, $2; f++ } else nsn++; next }
        { n = $1; $1 = ""; seen[$0] = 1; t += n
          if (!($0 in c)) { printf "FAIL %s: a reason the ceiling does not record (%d):%s\n", corpus, n, $0; f++ }
          else if (n > c[$0]) { printf "FAIL %s: %d above the ceiling of %d:%s\n", corpus, n, c[$0], $0; f++ }
          else if (n < c[$0]) low++ }
        END { for (k in c) if (!(k in seen)) low++
              for (k in door) if (!(k in sd)) { if (field(door[k], "read") > 0) { printf "FAIL %s: door %s read nothing, below the floor of %d\n", corpus, k, field(door[k], "read"); f++ } }
              printf "subst.sh ceiling %s: %d unread under a ceiling of %d in %d reasons; %d shapes of %d recorded\n", corpus, t, ct, nr, nsn, ns
              if (low) printf "    %d counts are below the record (or read above it): record again (SUBST_RECORD=1 sh tests/subst.sh bodies self)\n", low
              exit f != 0 }' "$rec" "$now" || bad=$((bad + 1))
}
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
        xargs -P "$JOBS" -n 1 sh -c 'CXX1_TEMPLATE_SUBST=1 CXX1_TEMPLATE_CENSUS=1 $CXX1 -nologo -S "$0" -o /dev/null > "$OUT/$(basename "$0" .cpp).census" 2>&1'
    cat "$OUT"/*.census | grep '^template-census:' | tr ' ' '\n' | grep = | tr '=' ' ' |
        awk -v units="$(cat "$OUT"/*.census | grep -c '^template-census:')" '{s[$1]+=$2} END {printf "subst.sh census: %d by substitution, %d through the function door of %d function bodies over %d units\n", s["by-substitution"], s["function"], s["function"] + s["by-substitution"], units}'
    # M8.2's own, of the bodies through the door: made with no replay, or replayed by reason.
    cat "$OUT"/*.census | tr -d '\r' | sed -n 's/^-check=templates replay (\([0-9]*\)): /\1 /p' |
        awk '{n=$1; $1=""; s[$0]+=n} END {for (k in s) printf "    %6d M8.2:%s\n", s[k], k}' | sort -rn
    # The probes, on M8.2's census (2026-11-19, decision ii): what substitutes with no replay, and beside one under the check;
    # each refused shape replayed with its reason.
    for p in template-subst template-subst-refused template-subst-call template-subst-depcall template-subst-deduce template-subst-order template-subst-ref template-subst-arg template-subst-class template-subst-adl template-subst-nest template-subst-classarg template-subst-value; do for how in plain check g; do
        case $how in plain) f= ;; check) f=-check=templates ;; g) f=-g ;; esac
        CXX1_TEMPLATE_CENSUS=1 $CXX1 -nologo $f -templates=subst -S tests/cases/$p.cpp -o /dev/null 2>&1 | tr -d '\r' > "$OUT/$p.$how"
    done; done
    probe() { grep -qxF -- "$3" "$OUT/$1.$2" || { echo "FAIL $1 ($2): no \"$3\""; bad=$((bad + 1)); }; }      # <case> <how> <line>
    for how in plain g; do
        probe template-subst $how "-check=templates replay (42): substituted with no replay"
        probe template-subst-refused $how "-check=templates replay (12): substituted with no replay"
        probe template-subst-call $how "-check=templates replay (10): substituted with no replay"
        probe template-subst-depcall $how "-check=templates replay (10): substituted with no replay"
        probe template-subst-deduce $how "-check=templates replay (15): substituted with no replay"
        probe template-subst-order $how "-check=templates replay (14): substituted with no replay"
        probe template-subst-ref $how "-check=templates replay (14): substituted with no replay"
        probe template-subst-arg $how "-check=templates replay (18): substituted with no replay"
        probe template-subst-class $how "-check=templates replay (4): substituted with no replay"
        probe template-subst-adl $how "-check=templates replay (9): substituted with no replay"
        probe template-subst-nest $how "-check=templates replay (9): substituted with no replay"
        probe template-subst-classarg $how "-check=templates replay (18): substituted with no replay"
    done
    probe template-subst check "-check=templates replay (42): beside a replay, under -check=templates"
    probe template-subst check "-check=templates made (42): equal"
    probe template-subst-refused check "-check=templates replay (12): beside a replay, under -check=templates"
    probe template-subst-call check "-check=templates made (10): equal"
    probe template-subst-depcall check "-check=templates made (10): equal"
    probe template-subst-deduce check "-check=templates made (15): equal"
    probe template-subst-order check "-check=templates made (14): equal"
    probe template-subst-ref check "-check=templates made (14): equal"
    probe template-subst-arg check "-check=templates made (18): equal"
    probe template-subst-class check "-check=templates made (4): equal"
    probe template-subst-adl check "-check=templates made (9): equal"
    probe template-subst-nest check "-check=templates made (9): equal"
    probe template-subst-classarg check "-check=templates made (18): equal"
    probe template-subst-adl check "-check=templates subst (2): replayed: a type parameter bound to a class or an array"
    probe template-subst-classarg check "-check=templates subst (2): replayed: a deduced call of a function over a class, a default, or a reference that binds no local as it is"
    probe template-subst-value check "-check=templates subst (5): replayed: a non-type, pack or template parameter, bound by name"
    ;;
bodies)
    ls tests/cases/*.cpp | grep -v '\.part\.cpp$' | while read src; do [ -f "${src%.cpp}.error" ] || echo "$src"; done |
        xargs -P "$JOBS" -n 1 sh -c '( ulimit -t 20; $CXX1 -nologo -check=templates -S "$0" -o /dev/null > "$OUT/$(basename "$0" .cpp).bodies" 2> /dev/null )'
    # Every unit reaches the count but one this host's target cannot compile (its .notarget names the target).
    case "$(uname -s)" in Linux) host=x86_64-linux ;; Darwin) host=arm64-darwin ;; *) host=x86_64-windows ;; esac
    ls tests/cases/*.cpp | grep -v '\.part\.cpp$' | while read src; do b=${src%.cpp}; [ -f "$b.error" ] && continue
        [ -f "$b.notarget" ] && grep -q "^$host\b" "$b.notarget" && continue; echo "$(basename "$b")"; done > "$OUT/units"
    for b in $(cat "$OUT/units"); do grep -qs '^-check=templates: bodies=' "$OUT/$b.bodies" || echo "    no count: $b"; done > "$OUT/uncounted"
    echo "subst.sh bodies: $(($(wc -l < "$OUT/units") - $(wc -l < "$OUT/uncounted"))) of $(wc -l < "$OUT/units" | tr -d ' ') cases this target compiles reached the count"
    [ -s "$OUT/uncounted" ] && { head -10 "$OUT/uncounted"; bad=$((bad + 1)); }
    cat "$OUT"/*.bodies | grep '^-check=templates: bodies=' | tr ' ' '\n' | grep = | tr '=' ' ' |
        awk -v units="$(cat "$OUT"/*.bodies | grep -c '^-check=templates: bodies=')" '{s[$1]+=$2} END {printf "subst.sh bodies: %d replayed over %d units: %d first reads, %d the same as the first, %d differ in text alone, %d differ in shape\n", s["bodies"], units, s["held"], s["same"], s["text"], s["shape"]}'
    echo "    $(cat "$OUT"/*.bodies | grep -c '^-check=templates shape: ') bodies whose replays differ in shape, in $(grep -l '^-check=templates shape: ' "$OUT"/*.bodies | wc -l | tr -d ' ') units"
    for rec in tests/cases/*.templates; do base=$(basename "$rec" .templates)
        # a Windows build writes its standard output with CRLF and a checkout may give the record CRLF: compare lines
        tr -d '\r' < "$OUT/$base.bodies" > "$OUT/$base.bodies.lf"; tr -d '\r' < "$rec" > "$OUT/$base.rec.lf"
        cmp -s "$OUT/$base.bodies.lf" "$OUT/$base.rec.lf" || { echo "FAIL $base: -check=templates is not the record"; diff "$OUT/$base.bodies.lf" "$OUT/$base.rec.lf" | head -10; bad=$((bad + 1)); }
    done
    ceiling cases bodies ;;
self)
    W=$(pwd); export W; rm -f "$OUT"/*.selfbodies
    srcs=$(ls src/*.cpp src/parser/*.cpp src/backend/*.cpp src/optimizer/*.cpp src/sema/*.cpp src/syntax/*.cpp src/ir/*.cpp src/ir/opt/*.cpp src/encoder/*.cpp)
    echo "$srcs" | xargs -P "$JOBS" -n 1 sh -c '$CXX1 -nologo -check=templates -S -I src -DCXX1_INCLUDE_DIR="\"$W/lib\"" -DCXX1_CXX_INCLUDE_DIR="\"$W/include\"" "$0" -o "$OUT/self.$$.s" > "$OUT/$(basename "$0" .cpp).selfbodies" 2> /dev/null; rm -f "$OUT/self.$$.s"'
    n=$(echo "$srcs" | wc -l | tr -d ' '); got=$(cat "$OUT"/*.selfbodies | grep -c '^-check=templates: bodies=')
    echo "subst.sh self: $got of $n sources reached the count"
    [ "$got" -eq "$n" ] || bad=$((bad + 1))
    ceiling self selfbodies ;;
*) echo "subst.sh: no step '$step' (run, typed, census, bodies, self)"; exit 2 ;;
esac; done
echo "subst.sh: $bad failed"
[ "$bad" -eq 0 ]
