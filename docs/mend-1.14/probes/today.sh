#!/bin/sh
# What cpp11 answers today for each probe and defect program; names from -arch x86_64-linux where it compiles.
cd "$(dirname "$0")"
CPP11=${CPP11:-../../../cpp11.exe}
for f in d*.cpp p0*.cpp; do
    echo "== $f"
    if "$CPP11" -arch x86_64-linux -S -o "/tmp/today-$$.s" "$f" >/dev/null 2>"/tmp/today-$$.err"; then
        grep -o '^_Z[^:]*' "/tmp/today-$$.s" | head -3
    else
        grep error "/tmp/today-$$.err" | head -2
    fi
done
rm -f "/tmp/today-$$.s" "/tmp/today-$$.err"
