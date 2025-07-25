#!/usr/bin/env python3

import sys
import urllib.parse
import os.path
import subprocess
import shlex

SCHEMA = "aperi-show-items://"

if len(sys.argv) != 2 or not sys.argv[1].startswith(SCHEMA):
    sys.stderr.write("Usage: %s %s<percent encoded path>\n" % (sys.argv[0], SCHEMA))
    sys.exit(1)
else:
    decoded = urllib.parse.unquote(sys.argv[1])[len(SCHEMA):]
    dirpath = os.path.abspath(os.path.dirname(decoded))
    filepath = os.path.basename(decoded)
    quoted_path = shlex.quote(filepath)

    subprocess.check_call(["wl-copy", "%s" % quoted_path])
    subprocess.check_call(["wl-copy", "-p", "%s" % quoted_path])
    subprocess.check_call(["alacritty", "--working-directory", dirpath,
                           "-e", "zsh", "-c", 'ls -l %s ; exec zsh -i' % quoted_path])
