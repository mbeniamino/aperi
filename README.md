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
launches the associated program, if any. If the program argument starts with
`file://` this prefix will be automatically stripped and percent decoding of
the remaining string will be performed. If the argument is a file or a
directory it will be normalized to an absolute path pointing to the file.

The configuration file consists of a sequence of lines. Empty lines or lines
starting with `#` are ignored. The remaining lines define the executable to use
to handle the file or url passed as the program argument and must follow this
syntax:

`<rule>[,<rule>]...=[%]<executable> [<arg> ]...`

`<rule>` can be:

 * a string ending with `://` . This rule matches a url starting with `<rule>`.
   For example `http://,https://=firefox` will launch firefox to open urls
   starting with `http://` or `https://`;
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

The `extra` directory contains a sample configuration file to be copied to
`~/.config/aperi/config` and modified as needed;

In addition to the config file, aperi searches for executable files named as
its argument extension inside the `~/.config/aperi/config/wrappers` directory.

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

This will create the `aperi` and, only if dbus development files are available,
`app-chooser` executables in the new directory `build`.

### Manual compilation

To manually compile Aperi and app-chooser you can use something like:

`gcc aperi.c -o aperi`

`gcc app-chooser.c $(pkg-config --libs dbus-1) $(pkg-config --cflags dbus-1) -O2 -o app-chooser`

Notice that this way of building prevents generated executables to report
their version.

## Installation

`aperi` can be used as a standalone executable to open resources from the
command line. It's also suggested to put `app-chooser` in PATH and use the rule
`*=app-chooser` at the end of the config file to have a handy way to open all
files not associated with anything else.

Other programs implement application associations in different ways.
For a very good overview see [this
article](https://wiki.archlinux.org/title/Default_applications) in the Arch
Wiki. To integrate `aperi` with other programs read below.

### xdg-open

Put a link to `aperi` named `xdg-open` in your path in a directory with higher
precedence than the one containing the system `xdg-open`. This will use `aperi`
to open url and files in place of `xdg-open` for example for opening files
downloaded by chrome/chromium.

### GIO's GAppInfo

Copy `aperi.desktop` from the `extra` directory to `~/.local/share/applications`.
Copy the file `mimeapps.list` from the `extra` directory in the xdg user config
directory (usually `~/.config`).

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
    { run = 'xdg-open "$@"', desc = "Open" },
]

[open]
rules = [
    { name = "*", use = "open" },
]
```

## Author

Aperi was written by Matteo Beniamino (m.beniamino@tautologica.org).
