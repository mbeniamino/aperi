Aperi is a resource opener based on file extensions. It allows to configure the
executable to launch to handle a certain extension. If no executable is
configured a standard GTK 4 dialog is used to let the user choose an
appropriate application.

## Configuration

The `extra` directory contains the following files, that can be used if desired:

- `aperi.cfg`: a sample configuration file to be copied in `~/.config` and modified as needed;
- `mimeapps.list`: a file to be copied in `~/.config/` to assign to `Aperi` most mime types;
- `aperi.desktop`: a file to be copied in `~/.local/share/applications/` to enable aperi to handle the mime types (it must be used along with `mimeapps.list` for them to work).

## Compilation instructions

### Meson

Aperi uses the meson build framework. For a standard compilation, from the top level directory use:

`meson --buildtype release build && meson compile -C build`

This will create the `aperi` executable in the new directory `build`.

### Manual compilation

To manually compile Aperi you can use something like:

`gcc aperi.c $(pkg-config --libs gtk4) $(pkg-config --cflags gtk4) -O2 -DHAVE_GTK4 -o aperi`

or, if the gtk4 support isn't needed:

`gcc aperi.c -O2 -o aperi`
