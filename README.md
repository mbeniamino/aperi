BOpen is a resource opener based on file extensions. It allows to configure the
executable to launch to handle a certain extension. If no executable is
configured a standard GTK 4 dialog is used to let the user choose an
appropriate application.

The `extra` directory contains the following files, that can be used if desired:

- `bopen.cfg`: a sample configuration file to be copied in `~/.config` and modified as needed;
- `mimeapps.list`: a file to be copied in `~/.config/` to assign to `BOpen` most mime types;
- `bopen.desktop`: a file to be copied in `~/.local/share/applications/` to enable bopen to handle the mime types (it must be used along with `mimeapps.list` for them to work).

