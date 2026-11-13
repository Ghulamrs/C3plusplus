#!/bin/sh
# winpass.sh <tree> <name> <flags>: a whole Windows pass - every case at <flags> in both spellings - as wincases.sh's
# four jobs, built first by winbuild.sh unless NOBUILD=1. The gate of record: at 3628bd4a, -O2 and -O0 printed
# "winout: 775 same, 0 differ" and "winout\masm: 774 same, 0 differ".
T=$1; N=$2; F=$3; S=$(cd "$(dirname "$0")" && pwd)
[ -n "${NOBUILD:-}" ] || sh "$S/winbuild.sh" "$T" "$N" || exit 1
for SP in gnu masm; do for H in 1 2; do echo "== [$F] $SP $H $(date +%H:%M)"; sh "$S/wincases.sh" "$N" "$F" $SP $H; done; done
echo "== [$F] done $(date +%H:%M)"
