#!/usr/bin/env python3
"""Git post-commit and post-merge hook: tag releases and check the
packaging versions.

Install it, from the top level directory, with:

    ln -s ../../tools/autotag.py .git/hooks/post-commit
    ln -s ../../tools/autotag.py .git/hooks/post-merge

After each commit or merge touching meson.build, debian/changelog or
aperi.spec it checks that their versions agree, and prints a warning if they don't. The
meson version 0.11.0-pre corresponds to the Debian upstream version and to
the RPM version 0.11.0~pre.

On main, if the version in meson.build doesn't contain "pre" and isn't
tagged yet, it creates the annotated tag <version>, unless the versions
disagree: in that case the commit fixing them creates the tag. The tag is
not pushed.

Run it with --check to only check the versions (exit status 1 on mismatch).
"""

import os
import re
import subprocess
import sys

VERSION_FILES = ("meson.build", "debian/changelog", "aperi.spec")


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def read_version(path, regex):
    try:
        with open(path) as f:
            for line in f:
                m = re.match(regex, line)
                if m:
                    return m.group(1)
    except FileNotFoundError:
        pass
    return None


def versions():
    """Return {file: version} for the files carrying the version."""
    meson = read_version("meson.build", r"^\s*project\(.*\bversion:\s*'([^']*)'")
    # aperi (<[epoch:]upstream-revision>) distribution; urgency=...
    deb = read_version("debian/changelog", r"^\S+ \(([^)]*)\)")
    if deb is not None:
        deb = deb.split(":", 1)[-1].rsplit("-", 1)[0]
    rpm = read_version("aperi.spec", r"^Version:\s*(\S+)")
    return {"meson.build": meson, "debian/changelog": deb, "aperi.spec": rpm}


def check_versions():
    """Print a warning and return False if the versions disagree."""
    found = versions()
    meson = found["meson.build"]
    if meson is None:
        print("autotag: version not found in meson.build", file=sys.stderr)
        return False
    expected = meson.replace("-", "~")
    ok = True
    for path, version in found.items():
        if path != "meson.build" and version is not None and version != expected:
            print(f"autotag: WARNING: {path} has version {version}, "
                  f"expected {expected} (meson.build: {meson})", file=sys.stderr)
            ok = False
    if not ok:
        print("autotag: update the version in "
              + ", ".join(p for p in VERSION_FILES if found[p] is not None),
              file=sys.stderr)
    return ok


def main():
    os.chdir(git("rev-parse", "--show-toplevel"))

    if sys.argv[1:] == ["--check"]:
        sys.exit(0 if check_versions() else 1)
    if os.path.basename(sys.argv[0]) == "post-merge":
        # git merge (also fast forward) and git pull: all the changes merged
        changed = git("diff", "--name-only", "ORIG_HEAD", "HEAD")
    elif sys.argv[1:]:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    else:
        changed = git("diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD")
    changed = set(changed.splitlines())
    if not changed.intersection(VERSION_FILES):
        return
    consistent = check_versions()

    if git("rev-parse", "--abbrev-ref", "HEAD") != "main":
        return
    version = versions()["meson.build"]
    if version is None or "pre" in version:
        return
    if version in git("tag", "--list").splitlines():
        return
    if not consistent:
        print(f"autotag: version {version} NOT tagged: it will be tagged by "
              f"the commit fixing the versions", file=sys.stderr)
        return
    git("tag", "-a", "-m", f"Version {version}", version)
    print(f"autotag: tagged version {version}")


if __name__ == "__main__":
    main()
