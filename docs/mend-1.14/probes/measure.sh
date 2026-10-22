#!/bin/sh
# Re-measure every probe: validate as C++11, then the mangled names clang gives on the three ABIs.
# Output: <probe>.<target>.names - one line per defined function symbol, in definition order.
cd "$(dirname "$0")"
for f in p0*.cpp; do
    b=${f%.cpp}
    clang++ -x c++ -std=c++11 -pedantic-errors -fsyntax-only -Wno-return-stack-address "$f" || { echo "$f: not valid C++11"; exit 1; }
    for t in x86_64-linux-gnu arm64-apple-darwin x86_64-pc-windows-msvc; do
        clang++ -x c++ -std=c++11 -target $t -S -O0 -o "$b.$t.s" "$f" 2>/dev/null || { echo "$f $t: clang failed"; exit 1; }
        case $t in
            x86_64-linux-gnu) grep -E '^_Z[A-Za-z0-9_$]*:' "$b.$t.s" | sed 's/:.*$//' ;;
            arm64-apple-darwin) grep -E '^__Z[A-Za-z0-9_$]*:' "$b.$t.s" | sed 's/:.*$//' ;;
            x86_64-pc-windows-msvc) grep -E '^"\?[^"]*":' "$b.$t.s" | sed 's/^"//; s/":.*$//' ;;
        esac > "$b.$t.names"
    done
done
echo measured
