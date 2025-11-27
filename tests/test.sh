#!/bin/sh
set -e
BASEDIR=$(dirname "$0")
export XDG_CONFIG_HOME="$BASEDIR/config"
tmpfile=$(mktemp /tmp/aperi_tests.XXXXXX)
exec 3>"$tmpfile"
exec 4<"$tmpfile"
rm "$tmpfile"
for f in files/* http://test http://youtu.be/; do echo "===$f==="; ../build/aperi "$f"; echo; done |\
    sed "s|$(realpath ../tests/files)/||g" >&3
diff --from-file=- reference.out <&4
