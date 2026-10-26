#!/bin/sh
# **c90 1.4's suite, through `cxx3 -std=c90`** (M7, doc-cxx3/M7-PLAN.md). The 431 cases of
# tests/c90/cases and the 8 directories of tests/c90/multi are c90's own, verbatim: each says what its
# program exits with on its first line, `// expect: N`, and is compiled twice - by this compiler and by
# the host's C compiler, `cc -w`, as c90's run.sh compiled it with gcc - both run, and the two must agree
# on stdout and on the exit status, which must be N. That is the oracle for C90 the plan writes down:
# c90 agrees with gcc on every one of these, so agreeing with the host's compiler here is agreeing with
# c90, and a case where they part is read against clang -std=c89 and recorded.
#
#   tests/c90.sh                      the host target, every case and directory
#   tests/c90.sh ar_index             the cases whose name is that, or that directory
#   tests/c90.sh -arch tms6747        the C6000: the assembly on vm6747, and as a TI program on sim6747
#                                     (asm6x, lnk6x, the run-time library), the host's compiler the reference;
#                                     tests/c90/tms6747-lp64.txt names the cases whose reference needs a
#                                     64-bit long, tests/c90/tms6747-sim.txt the ones TI's rts6740 cannot run
#   tests/c90.sh -arch x86_64-windows compile and assemble only (no Windows box this round): the MASM spelling
#                                     written and assembled in process (-c), the GNU spelling assembled by clang
#   CXX1=<cxx3.exe>                   the compiler under test; CXX1_FLAGS="-O2" runs the cases through the optimizer
#   C90CC=<c90.exe>                   measure c90 1.4 itself through the same harness (it takes .c, and no -std)
#
# tests/c90/refused/*.c, each beside its .error, are the C++-only constructs the C90 dialect refuses by name;
# a compile that succeeds, or stops with another message, is a failure. They run in the host leg.
set -u
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx3.exe}"
CXX1_FLAGS="${CXX1_FLAGS:-}"
C90CC="${C90CC:-}"
ARCH=host
only=""
while [ $# -gt 0 ]; do
    case "$1" in
        -arch) ARCH=$2; shift 2 ;;
        --one) shift; break ;;
        *) only=$1; shift ;;
    esac
done
case "$ARCH" in host|tms6747|x86_64-windows) ;; *) echo "c90.sh: -arch takes tms6747 or x86_64-windows; the host is the default"; exit 2 ;; esac
OUT=tests/out-c90; [ "$ARCH" = host ] || OUT=tests/out-c90-$ARCH
SRC=tests/c90/cases
HOSTCC="${HOSTCC:-cc}"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) LIBM= ;; *) LIBM=-lm ;; esac

# The compile, as one command: this compiler reading C90, or c90 1.4 reading its own .c.
compile() {  # compile <out> <src>... ; the arch flag only where one was asked for
    out=$1; shift
    archFlag=""; [ "$ARCH" = host ] || archFlag="-arch $ARCH"
    if [ -n "$C90CC" ]; then ( ulimit -t 20; "$C90CC" -S $archFlag $CXX1_FLAGS "$@" -o "$out" < /dev/null )
    else ( ulimit -t 20; "$CXX1" -nologo -std=c90 -S $archFlag $CXX1_FLAGS "$@" -o "$out" < /dev/null ); fi
}

# The C6000 tools, each named, else where the sibling checkouts keep it, else ~/ti6x, else on PATH.
find_tool() { for t in "$@"; do [ -n "$t" ] && [ -x "$t" ] && { echo "$t"; return; }; done; }
if [ "$ARCH" = tms6747 ]; then
    VM=$(find_tool "${VM:-}" ../../TI/VM6747/vm6747.exe "$HOME/ti6x/VM6747/vm6747.exe" "$(command -v vm6747 2>/dev/null)")
    ASM6X=$(find_tool "${ASM6X:-}" ../../TI/ASM6x/build/asm6x.exe "$HOME/ti6x/ASM6x/build/asm6x.exe" "$(command -v asm6x 2>/dev/null)")
    LNK6X=$(find_tool "${LNK6X:-}" ../../TI/LNK6x/build/lnk6x.exe "$HOME/ti6x/LNK6x/build/lnk6x.exe" "$(command -v lnk6x 2>/dev/null)")
    SIM6747=$(find_tool "${SIM6747:-}" ../../TI/SIM6747/sim6747.exe "$HOME/ti6x/SIM6747/sim6747.exe" "$(command -v sim6747 2>/dev/null)")
    SIM="${SIM:-1}"
    RTSLIB="${RTSLIB:-rts6x.lib}"
    if [ "$RTSLIB" = rts6x.lib ]; then
        TIRTS="${TIRTS:-}"
        [ -n "$TIRTS" ] || for d in ../../TI/RTS6x/build "$HOME/ti6x/RTS6x/build"; do [ -f "$d/rts6x.lib" ] && { TIRTS=$d; break; }; done
    else TIRTS="${TIRTS:-${C6747_EHLIB:-$HOME/c6747-lib}}"; fi
fi

# What the two programs printed and returned, compared; the verdict in $OUT/<name>.verdict.
compareRuns() {  # compareRuns <name> <expect> <ours-binary-or-"vm"...>
    name=$1; expect=$2
    ours_out=$(cat "$OUT/$name.ours.out"); ours_rc=$(cat "$OUT/$name.ours.rc")
    ref_out=$(cat "$OUT/$name.ref.out"); ref_rc=$(cat "$OUT/$name.ref.rc")
    if [ "$ours_rc" = 152 ] || [ "$ours_rc" = 137 ]; then echo "FAIL $name - our program did not finish in time"; echo fail > "$OUT/$name.verdict"; return; fi
    if [ "$ours_rc" != "$ref_rc" ]; then echo "FAIL $name - ours exited $ours_rc, $HOSTCC's $ref_rc"; echo fail > "$OUT/$name.verdict"; return; fi
    if [ -n "$expect" ] && [ "$ours_rc" != "$expect" ]; then echo "FAIL $name - both exited $ours_rc, the case expects $expect"; echo fail > "$OUT/$name.verdict"; return; fi
    if [ "$ours_out" != "$ref_out" ]; then
        echo "FAIL $name - same exit status, different output:"
        printf '%s\n' "$ref_out" > "$OUT/$name.ref.txt"; printf '%s\n' "$ours_out" > "$OUT/$name.ours.txt"
        diff "$OUT/$name.ref.txt" "$OUT/$name.ours.txt" | head -6 | sed 's/^/       /'
        echo fail > "$OUT/$name.verdict"; return
    fi
    echo pass > "$OUT/$name.verdict"
}
runHost() {  # runHost <binary> <name> <side>   -> $OUT/<name>.<side>.out and .rc
    { ( ulimit -t 5; "$1" < /dev/null ) > "$OUT/$2.$3.out" 2>/dev/null; echo $? > "$OUT/$2.$3.rc"; } 2>/dev/null
    # The reference's spelling of a null %p and a NaN sign differ by host; the C6000 side writes 0x0 and nan.
    [ "$ARCH" = tms6747 ] && [ "$3" = ref ] && sed -i.bak 's/(nil)/0x0/g; s/-nan/nan/g' "$OUT/$2.$3.out" && rm -f "$OUT/$2.$3.out.bak"
    return 0
}

# One case or one directory: the sources, the expectation, and which leg.
one() {
    kind=$1; name=$2
    if [ "$kind" = case ]; then srcs="$SRC/$name.c"; main="$SRC/$name.c"
    else srcs=$(ls tests/c90/multi/$name/*.c); main="tests/c90/multi/$name/main.c"; name="multi.$name"; fi
    expect=$(sed -n '1s|^// expect: *\([0-9-]*\).*|\1|p' "$main")
    [ -n "$expect" ] || { echo "FAIL $name - no '// expect: N' on line 1"; echo fail > "$OUT/$name.verdict"; return; }
    if [ "$ARCH" = tms6747 ] && [ -z "$only" ] && grep -q "^${name#multi.}[[:space:]]" tests/c90/tms6747-lp64.txt; then echo lp64 > "$OUT/$name.verdict"; return; fi
    # A case whose reference is Linux's (tests/c90/linux-only.txt) on another host, said aloud.
    if [ "$ARCH" = host ] && [ "$(uname -s)" != Linux ] && [ -z "$only" ] && grep -q "^${name#multi.}[[:space:]]" tests/c90/linux-only.txt; then
        echo "  skip $name on $(uname -s): $(grep "^${name#multi.}[[:space:]]" tests/c90/linux-only.txt | sed 's/^[^ ]*[[:space:]]*//')"
        echo hostonly > "$OUT/$name.verdict"; return
    fi

    # Every unit compiled on its own: a translation unit knows nothing of its neighbours until the link.
    asm=""
    for s in $srcs; do
        b=$(basename "$s" .c)
        if ! compile "$OUT/$name.$b.s" "$s" 2>> "$OUT/$name.err"; then
            echo "FAIL $name - refused $(basename "$s"):"; sed 's/^/       /' "$OUT/$name.err" | head -4
            echo fail > "$OUT/$name.verdict"; return
        fi
        asm="$asm $OUT/$name.$b.s"
    done

    if [ "$ARCH" = x86_64-windows ]; then
        # The MASM spelling assembled in process, and the GNU spelling by clang; nothing runs.
        for s in $srcs; do
            b=$(basename "$s" .c)
            if ! ( ulimit -t 20; "$CXX1" -nologo -std=c90 -c -arch x86_64-windows $CXX1_FLAGS "$s" -o "$OUT/$name.$b.obj" < /dev/null ) 2>> "$OUT/$name.err"; then
                echo "FAIL $name - the MASM spelling of $(basename "$s") did not assemble:"; sed 's/^/       /' "$OUT/$name.err" | head -4
                echo fail > "$OUT/$name.verdict"; return
            fi
            if ! ( ulimit -t 20; "$CXX1" -nologo -std=c90 -S -arch x86_64-windows -masm=gnu $CXX1_FLAGS "$s" -o "$OUT/$name.$b.gnu.s" < /dev/null ) 2>> "$OUT/$name.err" ||
               ! clang -target x86_64-pc-windows-msvc -c "$OUT/$name.$b.gnu.s" -o "$OUT/$name.$b.gnu.obj" 2>> "$OUT/$name.err"; then
                echo "FAIL $name - the GNU spelling of $(basename "$s") did not assemble:"; sed 's/^/       /' "$OUT/$name.err" | head -4
                echo fail > "$OUT/$name.verdict"; return
            fi
        done
        echo pass > "$OUT/$name.verdict"; return
    fi

    # The reference: the host's own compiler over the sources, as c90's suite built it.
    if ! $HOSTCC -w $srcs -o "$OUT/$name.ref" $LIBM 2>> "$OUT/$name.err"; then
        echo "FAIL $name - $HOSTCC rejected the case itself (the case is wrong)"; echo fail > "$OUT/$name.verdict"; return
    fi
    runHost "$OUT/$name.ref" "$name" ref

    if [ "$ARCH" = host ]; then
        if ! $HOSTCC $asm -o "$OUT/$name.ours" $LIBM 2>> "$OUT/$name.err"; then
            echo "FAIL $name - our assembly would not assemble and link:"; sed 's/^/       /' "$OUT/$name.err" | head -4
            echo fail > "$OUT/$name.verdict"; return
        fi
        runHost "$OUT/$name.ours" "$name" ours
        compareRuns "$name" "$expect"; return
    fi

    # tms6747: the assembly on the emulator, and the machine code on sim6747 through asm6x and lnk6x.
    { ( ulimit -t 20; "$VM" $asm < /dev/null ) > "$OUT/$name.ours.out" 2>&1; echo $? > "$OUT/$name.ours.rc"; } 2>/dev/null
    compareRuns "$name" "$expect"
    [ "$(cat "$OUT/$name.verdict")" = pass ] || return
    if [ "$SIM" = 1 ]; then
        if [ "$RTSLIB" != rts6x.lib ] && grep -q "^${name#multi.}[[:space:]]" tests/c90/tms6747-sim.txt; then echo simskip > "$OUT/$name.verdict"; return; fi
        objs=""
        for a in $asm; do
            o=${a%.s}.obj
            "$ASM6X" "$a" -o "$o" >> "$OUT/$name.ti.log" 2>&1 < /dev/null || { echo "FAIL $name (sim6747) - asm6x refused it:"; sed 's/^/       /' "$OUT/$name.ti.log" | head -3; echo fail > "$OUT/$name.verdict"; return; }
            objs="$objs $o"
        done
        if ! "$LNK6X" -mv6740 --abi=eabi -i "$TIRTS" "$OUT/link.cmd" $objs -l "$RTSLIB" -o "$OUT/$name.ti.out" >> "$OUT/$name.ti.log" 2>&1 < /dev/null; then
            echo "FAIL $name (sim6747) - lnk6x refused it:"; sed 's/^/       /' "$OUT/$name.ti.log" | head -3; echo fail > "$OUT/$name.verdict"; return
        fi
        { ( ulimit -t 20; "$SIM6747" --run --main-status "$OUT/$name.ti.out" < /dev/null ) > "$OUT/$name.sim.out" 2>&1; echo $? > "$OUT/$name.sim.rc"; } 2>/dev/null
        if [ "$(cat "$OUT/$name.sim.out")" != "$(cat "$OUT/$name.ref.out")" ] || [ "$(cat "$OUT/$name.sim.rc")" != "$(cat "$OUT/$name.ref.rc")" ]; then
            echo "FAIL $name (sim6747) - disagrees with $HOSTCC: sim rc=$(cat "$OUT/$name.sim.rc") ref rc=$(cat "$OUT/$name.ref.rc")"
            echo fail > "$OUT/$name.verdict"; return
        fi
    fi
}

# A refusal probe: the compile must fail, saying what its .error says.
refusal() {
    name=$1; src=tests/c90/refused/$name.c
    want=$(cat "tests/c90/refused/$name.error")
    if compile "$OUT/refused.$name.s" "$src" 2> "$OUT/refused.$name.err"; then
        echo "FAIL refused/$name - compiled, and should not have"; echo fail > "$OUT/refused.$name.verdict"
    elif grep -qF -- "$want" "$OUT/refused.$name.err"; then echo pass > "$OUT/refused.$name.verdict"
    else echo "FAIL refused/$name - wanted \"$want\", got:"; sed 's/^/      /' "$OUT/refused.$name.err"; echo fail > "$OUT/refused.$name.verdict"; fi
}

case "${1:-}" in
    case|multi) one "$1" "$2" > "$OUT/${3}.report" 2>&1; exit 0 ;;
    refused) refusal "$2" > "$OUT/refused.$2.report" 2>&1; exit 0 ;;
esac

rm -rf "$OUT"; mkdir -p "$OUT"
if [ "$ARCH" = tms6747 ]; then
    [ -n "$VM" ] || { echo "c90.sh: no vm6747 - name it with VM="; exit 1; }
    if [ "$SIM" = 1 ]; then
        for need in "asm6x:$ASM6X" "lnk6x:$LNK6X" "sim6747:$SIM6747"; do
            [ -n "${need#*:}" ] || { echo "c90.sh: no ${need%%:*} - build it, name it, or SIM=0 to leave the sim6747 leg out"; exit 1; }
        done
        [ -f "$TIRTS/$RTSLIB" ] || { echo "c90.sh: no $RTSLIB in $TIRTS - set TIRTS and RTSLIB, or SIM=0"; exit 1; }
        cat > "$OUT/link.cmd" <<'MAP'
--rom_model
--stack_size=0x100000
--heap_size=0x100000
MEMORY { RAM : origin = 0xC0000000, length = 0x04000000 }
SECTIONS
{
    .text > RAM  .const > RAM  .data > RAM  .bss > RAM  .far > RAM  .fardata > RAM
    .neardata > RAM  .rodata > RAM  .cinit > RAM  .init_array > RAM  .switch > RAM
    .cio > RAM  .stack > RAM  .sysmem > RAM  .vm6747.eh > RAM
}
MAP
    else echo "c90.sh: SIM=0 - the sim6747 leg is left out; only the assembly is run, on vm6747"; fi
fi
jobs() {
    for src in $SRC/*.c; do b=$(basename "$src" .c); [ -z "$only" ] || [ "$b" = "$only" ] || continue; echo "case $b $b"; done
    for d in tests/c90/multi/*/; do b=$(basename "$d"); [ -z "$only" ] || [ "$b" = "$only" ] || continue; echo "multi $b multi.$b"; done
    if [ "$ARCH" = host ] && [ -z "$only" ] && [ -z "$C90CC" ]; then
        for src in tests/c90/refused/*.c; do [ -f "$src" ] && echo "refused $(basename "$src" .c) refused.$(basename "$src" .c)"; done
    fi
}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
export CXX1 CXX1_FLAGS C90CC HOSTCC ARCH OUT SRC LIBM only
export VM ASM6X LNK6X SIM6747 SIM RTSLIB TIRTS 2>/dev/null
jobs | xargs -P "$JOBS" -L 1 sh "$0" -arch "$ARCH" --one
pass=0; fail=0; lp64=0; simskip=0; refusedOk=0; hostonly=0
jobs | while read -r kind b report; do cat "$OUT/$report.report"; done
for v in "$OUT"/*.verdict; do
    case "$(cat "$v")" in pass) pass=$((pass + 1)); case "$v" in */refused.*) refusedOk=$((refusedOk + 1));; esac ;; simskip) pass=$((pass + 1)); simskip=$((simskip + 1)) ;; fail) fail=$((fail + 1)) ;; lp64) lp64=$((lp64 + 1)) ;; hostonly) hostonly=$((hostonly + 1)) ;; esac
done
who="cxx3 -std=c90"; [ -z "$C90CC" ] || who="$C90CC"
echo "c90.sh ($ARCH, ${CXX1_FLAGS:--O0}, $who): $pass passed, $fail failed, $lp64 skipped for a 64-bit long, $hostonly Linux-only, $refusedOk refused as recorded"
[ "$ARCH" = tms6747 ] && [ "$SIM" = 1 ] && echo "c90.sh: every pass also on sim6747 (asm6x, lnk6x, $RTSLIB) but $simskip in tests/c90/tms6747-sim.txt"
[ "$fail" -eq 0 ]
