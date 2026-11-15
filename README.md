Aperi, a simple resource opener.

## Description

Aperi is a resource opener based on file extensions. It allows to launch a
configurable executable to handle a certain file or uri based on its extension
or schema. The project also contains a utility that launches via dbus the
standard system chooser for opening a file: the common use case it to associate
this utility to all files without a more specific association.

## Configuration

The program should be invoked with a single argument, which can be a URL or a
path to open. It then reads its configuration from
`$XDG_CONFIG_HOME/aperi/config` (usually `$HOME/.config/aperi/config`) and
launches the associated program, if any. A global configuration in
`/etc/aperi/config` is also supported if the user one is missing.
If the program argument starts with `file://` this prefix will be automatically
stripped and percent decoding of the remaining string will be performed. If the
argument is a file or a directory it will be normalized to an absolute path
pointing to the file.

The configuration file consists of a sequence of lines. Empty lines or lines
starting with `#` are ignored. The remaining lines define the executable to use
to handle the file or url passed as the program argument and must follow this
syntax:

`<rule>[,<rule>]...=[%]<executable> [<arg> ]...`

`<rule>` can be:

 * a string containing `://` . This rule matches a url starting with `<rule>`.
   For example `http://,https://=firefox` will launch firefox to open urls
   starting with `http://` or `https://`, while `https://youtu.be/=mpv` will
   launch mpv to handle youtube videos (but be aware that the first matching
   rule will be chosen, so put more specific rules **before** more generic
   ones);
 * the special string '/'. This rule matches if the argument is a directory;
 * the special string '/\*'. This rule matches any argument;
 * any other string `<string>`. This rule matches a file ending with
   `.<string>` (that is, any file with that extension).

`<executable>` can be either the full path to an executable or the name of an
executable in the PATH. The executable will be launched passing all
the specified `<arg>`s. By default the `aperi` argument will be also appended
to the arguments of the executable, but putting a `%` character after the `=`
changes this behaviour: in this case the aperi argument won't be automatically
passed as an extra argument, and placeholder will be expanded. Placeholders are
strings in the form "%\<char\>".
For the moment these placeholders are supported:
 * `%f` : will be replaced with the full path to the aperi argument;
 * `%%` : will be replaced with a verbatim `%`.

Using other combinations is invalid and will result in undefined behaviour (but
not in a crash or the program being stuck in a loop).

In order to specify a rule containing commas (`,`) and equal signs (`=`) or an
argument containing spaces or a starting percent character (`%`), surround them
with double quotes (`"`).
To insert a verbatim double quote write two sequential double quotes (`""`).

For example:
```
test.1=%echo "The argument ""%f"" is testing the use of the ""%%f"" placeholder"
test.2=%echo "This is a test showing a verbatim %% sign"
```

Rules are checked in order. The first matching rule will be used.

A sample configuration file is installed as
`<prefix>/share/doc/aperi/examples/config` (`extra/config` in the source tree):
copy it to `~/.config/aperi/config` and modify it as needed (see [Setting up a
user account](#setting-up-a-user-account)).

In addition to the config file, aperi searches for executable files named as
its argument extension inside the `~/.config/aperi/wrappers` directory.

If one is found, that file is invoked with a single argument that is the full
path to the aperi argument.

This allows to handle special cases such as launching a program ignoring the
aperi argument or passing the argument in a special way that doesn't match the
standard behaviour of appending it at the end.

For example, invoking `aperi foo.jpg` would execute
`~/.config/aperi/config/wrappers/jpg foo.jpg`, if `jpg` exists and is
executable.

Wrappers files have higher priority than lines contained in the config file.

For multiple extensions, longers extensions have higher priority (for example,
the wrapper `tar.gz` has higher priority than `gz`).

## Build instructions

### Meson

Aperi uses the meson build framework. For a standard compilation, from the top
level directory use:

`meson setup --buildtype release build && meson compile -C build`

This will create the `aperi` and `aperi-session` and, only if dbus development
files are available, `app-chooser` executables in the new directory `build`.
Without dbus, `aperi-session` is built without the FileManager1 service.

### Manual compilation

To manually compile `Aperi`, `app-chooser` and `aperi-session` you can use something like
(run `meson setup build` first, or create `config.h` from `config.h.in`):

`gcc aperi.c -o aperi`

`gcc app-chooser.c $(pkg-config --libs dbus-1) $(pkg-config --cflags dbus-1) -O2 -o app-chooser`

`gcc -DHAVE_DBUS -Ibuild aperi-session.c $(pkg-config --libs dbus-1) $(pkg-config --cflags dbus-1) -O2 -o aperi-session`

## Installation

Aperi can be installed system wide or for a single user. Both install the
same files under a prefix (`/usr` or `~/.local` in the examples below):

| File                                    | Purpose                                  |
|-----------------------------------------|------------------------------------------|
| `<prefix>/bin/aperi`, `app-chooser`, `aperi-session` | executables                 |
| `<prefix>/share/applications/aperi.desktop` | desktop entry, needed by `mimeapps.list` |
| systemd user unit `aperi-session.service` | starts `aperi-session` (see below)     |
| `<prefix>/share/doc/aperi/examples/`    | example `config`, `mimeapps.list`, `show_items.py` |

The global configuration directory is `<sysconfdir>/aperi/` (`/etc/aperi/`
with the `/usr` prefix): it is used only by users who don't have a
`~/.config/aperi/` directory.

### System wide

On Arch Linux, a `PKGBUILD` is in the top level directory: from there run
`makepkg -si`.

On other distributions:

```
meson setup --prefix /usr --buildtype release build
meson compile -C build
sudo meson install -C build
```

The user unit is installed in the systemd user unit directory (usually
`/usr/lib/systemd/user`).

### Single user

Install in `~/.local` (make sure `~/.local/bin` is in your `PATH`). The unit
directory must be passed explicitly, since systemd looks for user units in
`~/.local/share/systemd/user`:

```
meson setup --prefix ~/.local --buildtype release \
    -Dsystemduserunitdir="$HOME/.local/share/systemd/user" build
meson compile -C build
meson install -C build
```

If you prefer to copy the executables from the `build` directory to a
directory of your choice, the example files are in the `extra` directory of
the source tree and the unit is generated in `build/aperi-session.service`:
copy them where needed, and fix the `ExecStart` path in the unit.

## Setting up a user account

In the following, `<examples>` is `<prefix>/share/doc/aperi/examples` (e.g.
`/usr/share/doc/aperi/examples` or `~/.local/share/doc/aperi/examples`), or
the `extra` directory of the source tree.

1. Create the configuration (see [Configuration](#configuration)), starting
   from the example and adapting the rules to your programs:

   ```
   mkdir -p ~/.config/aperi
   cp <examples>/config ~/.config/aperi/config
   ```

   A handy last rule is `/*=app-chooser`, to choose an application for all
   files not associated with anything else.

2. Make aperi the default application for other programs, as described in
   [xdg-open](#xdg-open) and [GIO's GAppInfo](#gios-gappinfo). Other programs
   implement application associations in different ways: for a very good
   overview see [this
   article](https://wiki.archlinux.org/title/Default_applications) in the
   Arch Wiki.

3. Start `aperi-session` at login, with the systemd user unit or from the
   compositor configuration (see [Session
   helper](#session-helper-aperi-session)).

4. If you use the FileManager1 service of `aperi-session`, add a rule for
   the `aperi-show-items://` schema (see [org.freedesktop.FileManager1 D-Bus
   interface](#orgfreedesktopfilemanager1-d-bus-interface)), for example:

   ```
   install -Dm755 <examples>/show_items.py ~/.local/bin/show_items.py
   echo 'aperi-show-items://=show_items.py' >> ~/.config/aperi/config
   ```

   Rules are matched in order: put it before any catch all rule.

## Integration with other programs

### xdg-open

Put a link to `aperi` named `xdg-open` in your path in a directory with higher
precedence than the one containing the system `xdg-open`, e.g.:

```
ln -s "$(command -v aperi)" ~/.local/bin/xdg-open
```

This will use `aperi` to open url and files in place of `xdg-open` for example
for opening files downloaded by chrome/chromium.

### GIO's GAppInfo

`aperi.desktop` must be installed (see [Installation](#installation)). Copy
the file `mimeapps.list` from `<examples>` in the xdg user config directory
(usually `~/.config`). If you already have a `mimeapps.list`, merge the two
files instead.

This file associates most known mime types with `aperi`.

As noted in the Wiki article, many applications still read the `mimeapps.list` file
from the deprecated location `~/.local/share/applications/mimeapps.list`.
To simplify maintenance, create a link to the deprecated location:

```
ln -s ~/.config/mimeapps.list ~/.local/share/applications/mimeapps.list
```

### Midnight Commander file manager

`mc` uses `xdg-open` by default. See above. Alternatively, if you prefer to open
files in a non-blocking way, add this script in your path:

```
#!/bin/bash
nohup aperi "$@" &
```

make it executable and set the `MC_XDG_OPEN` env variable to the script path.

### Yazi terminal file manager

Set this parameters in your `yazi.toml` config file:

```
[opener]
open = [
    { run = "xdg-open %s", desc = "Open" },
]

[open]
rules = [
    { url = "*", use = "open" },
]
```

### org.freedesktop.FileManager1 D-Bus interface

For programs that use the D-Bus interface to open a resource be sure that all
the relevant environment variables are set. For example, most browsers use the
`org.freedesktop.FileManager1` to launch a file manager when opening a folder
from the downloads list. If you need to set the `PATH` to launch `aperi` use a
command like this (this also set the `LANG` variable even if not strictly
needed):

`dbus-update-activation-environment --systemd LANG PATH`

Aperi includes `aperi-session`, a session helper (see [Session
helper](#session-helper-aperi-session)) that, among other things, provides a
bare minimal implementation of this D-Bus service that just implements the
ShowItems method. When invoked, the service expects to find `aperi` in the path
and will spawn `aperi` passing a URI in the form
`aperi-show-items://<percent encoded path>`. You can configure `aperi` to handle
these requests as usual. In `<examples>` you can find an example
script (`show_items.py`) that can handle such requests. This script copies the
item path to the clipboard (already escaped for the shell) and spawn a
terminal, in this case `alacritty`, in the folder containing the file. It then
launch a `zsh` shell: adapt it to your terminal and shell. It needs `python`
and `wl-clipboard`.

### Session helper (aperi-session)

`aperi-session` is a small daemon meant to be started at the beginning of the
graphical session by a systemd user unit. It provides two features, each of
which can be disabled from the command line:

- the `org.freedesktop.FileManager1` D-Bus service described above
  (`-F`/`--no-fm1` to disable; always disabled if built without dbus);
- removal of the file associations wine creates (see [Systemd service to wipe
  wine file associations](#systemd-service-to-wipe-wine-file-associations);
  `-W`/`--no-wipewine` to disable).

The user unit `aperi-session.service` is installed together with aperi (see
[Installation](#installation)). Enable it with:

```
systemctl --user daemon-reload
systemctl --user enable --now aperi-session.service
```

To pass options (e.g. `--no-wipewine`) override `ExecStart` with
`systemctl --user edit aperi-session.service`:

```
[Service]
ExecStart=
ExecStart=/usr/bin/aperi-session --no-wipewine
```

(use the path of your installation, e.g. `%h/.local/bin/aperi-session`). Use
the meson option `-Dsystemduserunitdir=no` to not install the unit at all.

The unit is bound to `graphical-session.target`, so your compositor or desktop
must start that target (most desktop environments do; with sway you can use
e.g. [sway-systemd](https://github.com/alebastr/sway-systemd) or `uwsm`).
Programs spawned via ShowItems inherit the environment of the systemd user
manager: if `aperi` is not in its `PATH` export it with:

`dbus-update-activation-environment --systemd LANG PATH`

Only one instance per user can run: a second one prints a message and exits
successfully. `aperi-session` exits cleanly on SIGTERM, SIGINT and SIGHUP.

#### Running without systemd

`aperi-session` does not need systemd: it can be started by the compositor
itself. With `-s`/`--exit-with-session` it terminates when the Wayland
compositor (`$WAYLAND_DISPLAY`) exits, instead of outliving the session. In
this mode it inherits the compositor environment, so programs spawned via
ShowItems find `PATH` and `WAYLAND_DISPLAY` without further configuration.

Starting it from a login shell profile (e.g. `.zprofile`) before the
compositor is possible but not recommended: `--exit-with-session` cannot be
used there, since the compositor is not running yet, the session bus might not
exist yet (e.g. when the compositor is started via `dbus-run-session`), and
spawned programs would not inherit `WAYLAND_DISPLAY`.

#### Sway

If you are not using the systemd unit, add to the sway configuration:

`exec aperi-session --exit-with-session`

Use `exec`, not `exec_always`, to not start it again at each config reload
(the single instance check makes it harmless anyway).

If you are using the systemd unit you can add:

`exec dbus-update-activation-environment --systemd LANG PATH`

at the end of the configuration file to export LANG and PATH env variables if
needed.

## Troubleshooting

### Wine takes over as the default application

#### Remove wine file associations

By default Wine will associate some common file formats, such as txt and jpeg,
with wine clones of windows programs such as notepad and internet explorer. In
order to remove the current associations delete the related desktop files in
the user home:

```
rm -f ~/.local/share/applications/wine-extension*.desktop
rm -f ~/.local/share/icons/hicolor/*/*/application-x-wine-extension*
```

remove the old mime type cache:

```
rm -f ~/.local/share/applications/mimeinfo.cache
rm -f ~/.local/share/mime/packages/x-wine*
rm -f ~/.local/share/mime/application/x-wine-extension*
```

and rebuild it:

```
update-desktop-database ~/.local/share/applications
update-mime-database ~/.local/share/mime/
```

#### Prevent wine from creating file associations again

To prevent wine from creating the file associations again you can disable this
behaviour **per prefix**. Substitute `<prefix>` with the path of the wine
prefix and run:

```
WINEPREFIX=<prefix> wine reg add "HKEY_CURRENT_USER\Software\Wine\FileOpenAssociations" /v Enable /d N
```

In order to disable this in newly created prefixes you could edit
`wine.inf` often installed in `/usr/share/wine` and add the line

```
HKCU,"Software\Wine\FileOpenAssociations","Enable",2,"N"
```

under the `[Services]` section.

Depending on your distro and wine installation method, modifying this file
could change a package installed file. This can lead to warnings or problems
during system updates or to this file being overwritten.
Check your distro documentation for specific fixes, such as creating post
install hooks.

For ArchLinux check the [article of the
Wiki](https://wiki.archlinux.org/title/Wine) , and in particular the paragraphs
"Unregister existing Wine file associations" and "Prevent Wine from creating
filetype associations".

#### Systemd service to wipe wine file associations

`aperi-session` (see [Session helper](#session-helper-aperi-session)) removes
the `wine-extension*.desktop` and `wine-protocol*.desktop` files wine creates in
`$XDG_DATA_HOME/applications` (default `~/.local/share/applications`) as soon as
they are created, and those already present at startup. Run it with the
`aperi-session.service` user unit.

## Author

Aperi was written by Matteo Beniamino (m.beniamino@tautologica.org).
