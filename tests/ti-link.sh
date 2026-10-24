#!/bin/sh
# The tms6747 target taken all the way to a TI program by the driver, on a box with CCS: every case
# with recorded output is compiled with `cxx3 -arch tms6747`, which assembles what it wrote with asm6x
# and links the object with TI's own lnk6x against rts6740_elf_eh.lib into <case>.out. The linker is
# the judge: it takes every object asm6x wrote, or names the one it does not. tools/windows/ti-link.cmd
# is the same on a Windows box; this is it on the Linux box, whose CCS 5.5 has lnk6x 7.4.4.
#   CPP11_TI     the C6000 compiler directory (default ~/ti/ccsv5/tools/compiler/c6000_7.4.4)
#   CPP11_TILIB  a directory holding rts6740_elf_eh.lib (default ~/c6747-lib, mklib's build of it)
#   CPP11_AS     asm6x (default ~/ti6x/ASM6x/build/asm6x.exe, tools/ti6x-build.sh's)
#   CXX1_TI_FLAGS goes on every compile - -O1 or -O2 links the optimizer's code; JOBS, how many at once.
set -u
if [ "$(uname -s)" = Darwin ]; then echo "ti-link.sh: the C6000 runs on the Windows and Linux boxes, not on the Mac"; exit 2; fi
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx3.exe}"
export CPP11_TI="${CPP11_TI:-$HOME/ti/ccsv5/tools/compiler/c6000_7.4.4}"
export CPP11_TILIB="${CPP11_TILIB:-$HOME/c6747-lib}"
export CPP11_AS="${CPP11_AS:-$HOME/ti6x/ASM6x/build/asm6x.exe}"
CXX1_TI_FLAGS="${CXX1_TI_FLAGS:-}"
OUT=tests/out-ti
[ -x "$CPP11_TI/bin/lnk6x" ] || [ -x "$CPP11_TI/bin/lnk6x.exe" ] || { echo "ti-link.sh: no lnk6x under $CPP11_TI - name it with CPP11_TI"; exit 1; }
[ -f "$CPP11_TILIB/rts6740_elf_eh.lib" ] || { echo "ti-link.sh: no rts6740_elf_eh.lib in $CPP11_TILIB - see tools/ti6x-build.sh"; exit 1; }
[ -x "$CPP11_AS" ] || { echo "ti-link.sh: no asm6x at $CPP11_AS - name it with CPP11_AS"; exit 1; }
[ -x "$CXX1" ] || { echo "ti-link.sh: no $CXX1 - build it first"; exit 1; }

# One case: its verdict on the first line of <case>.verdict, the linker's words in <case>.log.
if [ "${1:-}" = --one ]; then
    base=$2
    if [ -f "tests/cases/$base.notarget" ] && grep -q "^tms6747[[:space:]]" "tests/cases/$base.notarget"; then
        echo skipped > "$OUT/$base.verdict"; exit 0
    fi
    part=""
    [ -f "tests/cases/$base.part.cpp" ] && part="tests/cases/$base.part.cpp"
    if ( ulimit -t 60; "$CXX1" -arch tms6747 -nologo $CXX1_TI_FLAGS "tests/cases/$base.cpp" $part -o "$OUT/$base.out" < /dev/null ) > "$OUT/$base.log" 2>&1 \
       && [ -f "$OUT/$base.out" ]; then
        echo linked > "$OUT/$base.verdict"
    else
        echo failed > "$OUT/$base.verdict"
    fi
    exit 0
fi

rm -rf "$OUT" && mkdir -p "$OUT"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
for e in tests/cases/*.expected; do basename "$e" .expected; done | xargs -P "$JOBS" -I{} sh "$0" --one {}
count() { grep -lx "$1" "$OUT"/*.verdict 2>/dev/null | wc -l | tr -d ' '; }
for v in $(grep -lx failed "$OUT"/*.verdict 2>/dev/null); do
    base=$(basename "$v" .verdict)
    echo "FAIL $base"; grep -v '^$' "$OUT/$base.log" | head -4 | sed 's/^/      /'
done
linked=$(count linked); failed=$(count failed); skipped=$(count skipped)
echo "ti-link.sh: $linked programs linked by TI's lnk6x ($CPP11_TI), $failed failed, $skipped not for this target${CXX1_TI_FLAGS:+, at $CXX1_TI_FLAGS}"
[ "$failed" = 0 ]
