# Testing status

What has been verified so far and what still needs testing, with
suggestions on how to do it. Remove items once they are verified.

## Automated tests

`meson test -C build` (also run by the Arch, Debian and RPM package builds)
runs `tests/test.sh`, which covers only `aperi`: rule matching,
placeholders, quoting, wrappers, directories, URIs, the fall-through to the
next rule when a program can't be executed and the exit status for a
missing argument.

There are no automated tests for `aperi-session`, `aperi-setup` and
`app-chooser`.

## Verified manually

- `aperi-session`: both features against a private D-Bus session
  (`dbus-run-session`) and a temporary `XDG_DATA_HOME`, the single instance
  lock, `SIGHUP`, `--exit-with-session` against a fake Wayland socket,
  builds with and without dbus.
- `aperi-setup`: fresh `--yes` run and rerun with a fake `HOME`, the three
  interactive mimeapps merge choices (through `script`), rule insertion
  before the catch all rule, existing user `xdg-open`, invalid options;
  syntax with `bash --posix`.
- `aperi`: exit status 1 when a launch fails with no other matching rule
  (manually, since the test config ends with a catch all rule).
- Arch package: built with `makepkg`, contents inspected.
- RPM: built with `rpmbuild` on Arch Linux with stand-in meson/systemd
  macros and `--nodeps`; contents, dependencies and configured paths
  inspected.
- Single user install: `meson install` into a fake `~/.local` prefix.
- Man pages: `groff -man -ww` without warnings, rendered with `man -l`.

## Still to test

### Debian/Ubuntu package (never built)

- [ ] Build on Ubuntu 26.04 (e.g. in a container) with
  `dpkg-buildpackage -us -uc -b`; the tests must pass in `dh_auto_test`.
- [ ] `lintian ../aperi_*.changes` and fix the warnings.
- [ ] `dpkg -c`: the examples in `/usr/share/doc/aperi/examples` must not be
  compressed (`aperi-setup` reads them), the man pages must be compressed.
- [ ] After installing, `aperi-session.service` must not be enabled for any
  user (no links in `/etc/systemd/user/*.wants/`).
- [ ] Install, upgrade and remove with `apt`.
- [ ] Optionally build on Debian stable too.

### RPM package on Fedora 44

- [ ] `dnf builddep aperi.spec` and `rpmbuild -bb` with the real Fedora
  macros (the meson flags and the systemd scriptlets were only tested with
  stand-ins).
- [ ] `rpmlint` on the spec and the built packages.
- [ ] The license must end up in `/usr/share/licenses/aperi/`.
- [ ] Install and remove with `dnf`: the `%systemd_user_post` preset must
  not enable `aperi-session.service`.
- [ ] Optionally build on openSUSE.

### Arch package

- [ ] Install and upgrade with `pacman -U` and check the `aperi.install`
  messages (`post_install`, and `post_upgrade` from a version < 0.11.0).

### aperi-setup

- [ ] `session` step with systemd (never run, to avoid touching the user
  manager): `--session systemd` and the interactive choice, with
  `graphical-session.target` active (unit enabled and started) and not
  active (only enabled); `--yes` detection of the systemd user manager;
  error when `aperi-session.service` is not installed.
- [ ] The interactive questions of the other steps in a real terminal
  (only the mimeapps ones were tested interactively).
- [ ] Non default `XDG_CONFIG_HOME` and `XDG_DATA_HOME`.
- [ ] `HOME` containing spaces (the ShowItems rule is quoted).
- [ ] Other shells: `dash` (Debian/Ubuntu `/bin/sh`), `busybox sh`; run
  `shellcheck` on `aperi-setup.in`.
- [ ] `--examples extra` from the source tree without an installed aperi.

### aperi-session in a real session

- [ ] systemd unit with `graphical-session.target` (e.g. sway with uwsm or
  sway-systemd): `Type=notify` readiness, `Restart=on-failure`, and with
  `KillMode=process` a terminal opened via ShowItems must survive
  `systemctl --user restart aperi-session`.
- [ ] Unit installed in `~/.local/share/systemd/user` by a single user
  install found by `systemctl --user`.
- [ ] `exec aperi-session --exit-with-session` in a real sway session: it
  must exit when sway exits.
- [ ] ShowItems from real applications ("Show in folder" in Firefox and
  Chromium downloads) and `show_items.py` end to end.
- [ ] wipewine while installing a real program with wine.
- [ ] Single instance lock with `XDG_RUNTIME_DIR` unset (`/tmp` fallback).
- [ ] Clean exit on `SIGHUP` when started from a login shell that logs out.

### app-chooser

- [ ] Not tested in this round (unchanged): opening a file and a URI
  through `xdg-desktop-portal` with the gtk and gnome backends.

### Man pages

- [ ] `mandoc -Tlint` and lintian's man page checks.

## Known gaps (not bugs in the tests)

- A wrapper whose interpreter is missing (bad `#!` line) fails with
  `ENOENT`, like a missing wrapper, so it is silently ignored and aperi
  exits with status 0 if no rule matches.
- Exiting with status 0 when no rule matches is intentional but differs
  from `xdg-open`, which exits with status 3 when no handler is found.
