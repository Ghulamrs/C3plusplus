#!/bin/sh
# probe.sh prog.cpp [...]: each program through clang++ -std=c++11 -pedantic-errors and through
# cpp11, accept/reject and output compared. The oracle is clang; a difference is a finding.
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${PROBE_OUT:-/tmp/review-probes-out}
mkdir -p "$OUT"
for f in "$@"; do
    b=$(basename "$f" .cpp)
    if clang++ -x c++ -std=c++11 -pedantic-errors -w -o "$OUT/$b.clang" "$f" > "$OUT/$b.clang.err" 2>&1; then
        cres=accept; "$OUT/$b.clang" > "$OUT/$b.clang.out" 2>&1; echo "rc=$?" >> "$OUT/$b.clang.out"
    else cres=reject; fi
    if "$ROOT/cpp11.exe" "$f" -o "$OUT/$b.cpp11" > "$OUT/$b.cpp11.err" 2>&1; then
        xres=accept; "$OUT/$b.cpp11" > "$OUT/$b.cpp11.out" 2>&1; echo "rc=$?" >> "$OUT/$b.cpp11.out"
    else xres=reject; fi
    if [ "$cres" != "$xres" ]; then
        echo "DIFF   $b: clang $cres, cpp11 $xres"
        [ "$cres" = reject ] && sed -n 1,3p "$OUT/$b.clang.err" | sed 's/^/    clang: /'
        [ "$xres" = reject ] && sed -n 1,3p "$OUT/$b.cpp11.err" | sed 's/^/    cpp11: /'
    elif [ "$cres" = accept ] && ! cmp -s "$OUT/$b.clang.out" "$OUT/$b.cpp11.out"; then
        echo "DIFF   $b: output differs"
        diff "$OUT/$b.clang.out" "$OUT/$b.cpp11.out" | sed 's/^/    /'
    else
        echo "same   $b ($cres)"
    fi
done
