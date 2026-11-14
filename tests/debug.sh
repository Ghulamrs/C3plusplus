#!/bin/sh
# **A debugger in batch over tests/m10.** Each program with a command file (tests/m10/<w>.cdb or <w>.gdb) is built
# -g -O0, run under the debugger - stops at named source lines, locals with their values, the backtrace - and the
# output, normalised, is compared with tests/m10/<w>.<dbg>.expected, written from the source's values by hand.
#
#   sh tests/debug.sh cdb [compiler]     the Windows box, Git bash: cdb on x86_64-windows, DEBUG_FLAGS=-masm=gnu etc.
#   sh tests/debug.sh gdb [compiler]     the Linux box: gdb on x86_64-linux
#
# In an .expected, `name = *` (or a line `*`-valued after ` = `) is a value the source leaves unset at that stop.
# Frames are normalised to `function @ line`, addresses to 0x#; frames outside the program's own file are dropped.
# -O2 is not run: cl's own -O2 keeps no slot for a local (history, M10 W1-W5), and cxx3's records none there.
set -u
cd "$(dirname "$0")/.."
DBG=${1:?usage: tests/debug.sh cdb|gdb [compiler]}
CXX1=${2:-${CXX1:-./cxx3.exe}}
case $CXX1 in /*) ;; *) CXX1=$(pwd)/$CXX1;; esac   # the builds run in $OUT
FLAGS=${DEBUG_FLAGS:-}
CDB=${CDB:-"/c/Users/xrotrxt/dbgtools/x/Windows Kits/10/Debuggers/x64/cdb.exe"}
GDB=${GDB:-gdb}
OUT=tests/out-debug
rm -rf "$OUT"; mkdir -p "$OUT"
pass=0; fail=0
# The debugger's text, kept from the first `==` marker: frames made `f @ line`, addresses masked, noise dropped -
# blank lines too, which gdb prints before every stop a `continue` reaches (g++'s binary the same).
normal() {
    tr -d '\r' | sed -n '/^==/,$p' | sed -e '/^quit:/,$d' -e '/^$/d' -e '/^Breakpoint [0-9]* hit/d' -e '/^Child-SP/d' \
        -e '/^Breakpoint [0-9]*[,] /d' -e '/^Breakpoint [0-9]* at /d' -e '/^[0-9][0-9]*	/d' -e '/^\[/d' -e '/^Continuing\./d' \
        -e 's/^.*[ !]\([A-Za-z_][A-Za-z0-9_:<>~]*\)+0x[0-9a-f]* \[.*[\\/]\([^\\/]*\) @ \([0-9]*\)\]$/\1 @ \3|\2/' \
        -e 's/^#[0-9]* .* \([A-Za-z_][A-Za-z0-9_:<>~]*\) (.*) at \(.*[\\/]\)\{0,1\}\([^\\/ ]*\):\([0-9]*\)$/\1 @ \4|\3/' \
        -e 's/0x[0-9a-f`]*/0x#/g' |
    awk -v file="$1" -F'|' 'NF == 2 { if ($2 == file) print $1; next } { print }' |
    grep -v '^[0-9a-f]\{8\}`[0-9a-f]\{8\} ' | grep -v '^0x# .*!'
}
for cmds in tests/m10/*."$DBG"; do
    w=$(basename "$cmds" ".$DBG")
    exp=tests/m10/$w.$DBG.expected
    [ -f "$exp" ] || { echo "FAIL $w: no $exp"; fail=$((fail + 1)); continue; }
    cp "tests/m10/$w.cpp" "$OUT/"
    if ! (cd "$OUT" && timeout 180 "$CXX1" -nologo -g -O0 $FLAGS "$w.cpp" -o "$w.exe") > "$OUT/$w.build" 2>&1; then
        echo "FAIL $w: does not build: $(head -c 200 "$OUT/$w.build")"; fail=$((fail + 1)); continue
    fi
    if [ "$DBG" = cdb ]; then
        (cd "$OUT" && timeout 120 "$CDB" -lines -c "$(grep -v '^#' "../m10/$w.cdb" | tr '\n' ';')q" "$w.exe") > "$OUT/$w.raw" 2>&1
    else
        (cd "$OUT" && timeout 120 "$GDB" -q -batch -nx -iex "set debuginfod enabled off" -x "../m10/$w.gdb" "./$w.exe") > "$OUT/$w.raw" 2>&1
    fi
    normal "$w.cpp" < "$OUT/$w.raw" > "$OUT/$w.got"
    # Equal line for line, a `= *` value in the expectation matching any value of that name and type.
    if awk 'NR == FNR { e[++n] = $0; next } { g[++m] = $0 }
            END { if (n != m) { print "  " n " lines expected, " m " got"; bad = 1 }
                  for (i = 1; i <= (n > m ? n : m); i++) {
                      x = e[i]; ok = x == g[i]
                      if (!ok && x ~ / = \*$/) ok = substr(g[i], 1, length(x) - 1) == substr(x, 1, length(x) - 1)
                      if (!ok) { print "  line " i ": expected `" x "`, got `" g[i] "`"; bad = 1 } }
                  exit bad }' "$exp" "$OUT/$w.got" > "$OUT/$w.why"; then
        echo "ok   $w"; pass=$((pass + 1))
    else
        echo "FAIL $w"; head -12 "$OUT/$w.why"; fail=$((fail + 1))
    fi
done
echo "debug.sh $DBG${FLAGS:+ $FLAGS}: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
