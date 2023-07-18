Aperi, a simple resource opener by Matteo Beniamino.

## Description

Aperi is a resource opener based on file extensions. It allows to configure the
executable to launch to handle a certain extension. The project also contains
a utility that launches a standard GTK 4 dialog to let the user choose an
application for opening a file: the common use case it to associate this utility
to all files without a more specific association.

## Configuration

The program should be invoked with a single argument that is a URL or a path to
open. It then reads its configuration from `$HOME/.config/aperi/config` and
launches the associated program, if any. If the argument of the program starts
with `file://` this prefix will be automatically stripped. If the argument of
the program is a file or a directory it will be normalized to an absolute path
pointing to the file.

The configuration file is a sequence of lines. Empty lines or lines starting with (`#`) are ignored.

The remaining lines define the executable to use to handle file or url passed
as the argument of the program and must follow this syntax:

`<rule>[,<rule>]...=<executable> [<arg> ]...`

`<rule>` can be:

 * a string ending with `://` . This rule matches a url starting with `<rule>`.
   For example `http://,https://=firefox` will launch firefox to open urls
   starting with `http://` or `https://`;
 * the special string '/'. This rule will match if the argument is a directory;
 * the special string '\*'. This rule will match any argument;
 * any other string `<string>`. This rule will match a file ending with
   `.<string>` (that is any file with that extension).

`<executable>` can either be the full path to an executable, or the name of an
executable in the PATH. The executable will be launched passing all the specified
`<arg>`s and the `aperi` argument.
At the moment there is no way to pass arguments containing spaces or to not pass
the aperi argument as an extra argument to `<executable>`, but as a workaround
you can write a small shell script embedding the command (in real life, me,
the author, never had to write one to overcome this limitation).

Rules are checked in order. The first matching rule will be used.

The `extra` directory contains a sample configuration file to be copied to
`~/.config/aperi/config` and modified as needed;

## Compilation instructions

### Meson

Aperi uses the meson build framework. For a standard compilation, from the top
level directory use:

`meson --buildtype release build && meson compile -C build`

This will create the `aperi` and, only if dbus development files are available,
`app-chooser` executables in the new directory `build`.

### Manual compilation

To manually compile Aperi and app-chooser you can use something like:

`gcc aperi.c -o aperi`

`gcc app-chooser.c $(pkg-config --libs dbus-1) $(pkg-config --cflags dbus-1) -O2 -o app-chooser`

## Installation

Either use `aperi` as a standalone executable or put a link named `xdg-open` in
your path in a directory with higher precedence than the one containing the
system `xdg-open` (this will use `aperi` to open url and files in place of
`xdg-open` for example for opening files downloaded by chrome/chromium). It's
also suggested to put `app-chooser` in PATH and use the rule `*=app-chooser` at the
end of the config file to have a handy way to open all files not associated
with anything else.
