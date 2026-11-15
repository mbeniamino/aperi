#!/bin/sh
# aperi tests. Each case runs aperi on an argument and compares its output
# with the expected one. Rules are in config/aperi/config. Output is TAP.
#
#   file NAME EXPECTED   create the empty file NAME and open it
#   dir  NAME EXPECTED   create the directory NAME and open it
#   uri  URI  EXPECTED   open URI
#
# The temporary directory path is stripped from aperi output.
#
# Usage: test.sh [path to aperi executable, default ../build/aperi]

cd "$(dirname "$0")" || exit 1
APERI=$(realpath "${1:-${APERI:-../build/aperi}}") || exit 1
export XDG_CONFIG_HOME="$PWD/config"
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
tmp=$(realpath "$tmp")

n=0
failed=0

check() { # check ARG EXPECTED NAME
    n=$((n + 1))
    got=$("$APERI" "$1" 2>&1 | sed "s|$tmp/||g")
    if [ "$got" = "$2" ]; then
        echo "ok $n - $3"
    else
        failed=$((failed + 1))
        echo "not ok $n - $3"
        echo "#   expected: '$2'"
        echo "#        got: '$got'"
    fi
}

file() { : > "$tmp/$1" && check "$tmp/$1" "$2" "$1"; }
dir() { mkdir -p "$tmp/$1" && check "$tmp/$1" "$2" "$1/"; }
uri() { check "$1" "$2" "$1"; }

# Extensions
file test.a       '1 test.a'
file test.b       '2 test.b'
file test.c       '2 test.c'
file test.http    '.http test.http'
file 'test.*'     '999 test.*'

# Literal ',', '=' and '"' as extensions
file 'test.,'     '19 test.,'
file 'test.='     '20 test.='
file 'test."'     '21 test."'

# Placeholders
file test.f       '6'
file test.g       '7 %f'
file test.h       '8 test.h foo'
file test.i       '9 %% test.i'
file test.j       '10 %f test.j'
file test.o       '22 testtest.o-test.otest'

# Quoting
file test.k       'test.k 12 13 '
file test.l       '13"14 test.l'
file test.m       '"1516 test.m'
file test.n       '1718" test.n'

# Wrappers
file test.wrapper '998 test.wrapper'

# Directories
dir dir           '5 dir'

# URIs
uri http://test       'http:// http://test'
uri http://youtu.be/  'http://youtu.be/ http://youtu.be/'

echo "1..$n"
[ "$failed" -eq 0 ]
