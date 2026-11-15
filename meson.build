project('aperi', 'c', version: '0.11.0-pre')

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
sysconfdir = prefix / get_option('sysconfdir')

conf_data = configuration_data()
conf_data.set('version', meson.project_version())
conf_data.set('sysconfdir', sysconfdir)
configure_file(input : 'config.h.in',
               output : 'config.h',
               configuration : conf_data)

src_aperi = ['aperi.c']
aperi_exe = executable('aperi', sources: src_aperi, install : true)

dbus_dep = dependency('dbus-1', required: get_option('dbus'))
session_args = []
if dbus_dep.found()
  src_app_chooser = ['app-chooser.c']
  executable('app-chooser', sources: src_app_chooser,
             dependencies: dbus_dep, install : true)
  session_args += ['-DHAVE_DBUS']
endif

src_session = ['aperi-session.c']
executable('aperi-session', sources: src_session,
           dependencies: dbus_dep, c_args: session_args,
           install : true)

# systemd user unit for aperi-session, with the real installation path
unit_dir = get_option('systemduserunitdir')
if unit_dir == ''
  systemd_dep = dependency('systemd', required: false)
  if systemd_dep.found()
    unit_dir = systemd_dep.get_variable(pkgconfig: 'systemd_user_unit_dir',
                                        pkgconfig_define: ['prefix', prefix])
  else
    unit_dir = prefix / 'lib' / 'systemd' / 'user'
  endif
endif
unit_data = configuration_data()
unit_data.set('bindir', bindir)
configure_file(input : 'extra/aperi-session.service.in',
               output : 'aperi-session.service',
               configuration : unit_data,
               install : unit_dir != 'no',
               install_dir : unit_dir)

# aperi-setup, to configure a user account
setup_data = configuration_data()
setup_data.set('bindir', bindir)
setup_data.set('datadir', datadir)
setup_data.set('examplesdir', datadir / 'doc' / meson.project_name() / 'examples')
setup_data.set('version', meson.project_version())
configure_file(input : 'aperi-setup.in',
               output : 'aperi-setup',
               configuration : setup_data,
               install : true,
               install_dir : bindir,
               install_mode : 'rwxr-xr-x')

# Man pages
man_data = configuration_data()
man_data.set('VERSION', meson.project_version())
man_data.set('BINDIR', bindir)
man_data.set('SYSCONFDIR', sysconfdir)
man_data.set('DATADIR', datadir)
man_data.set('EXAMPLESDIR', datadir / 'doc' / meson.project_name() / 'examples')
man_data.set('SYSTEMDUSERUNITDIR',
             unit_dir == 'no' ? prefix / 'lib' / 'systemd' / 'user' : unit_dir)
man_pages = ['aperi.1', 'aperi-session.1', 'aperi-setup.1']
if dbus_dep.found()
  man_pages += ['app-chooser.1']
endif
foreach page : man_pages
  configure_file(input : page + '.in',
                 output : page,
                 configuration : man_data,
                 install : true,
                 install_dir : prefix / get_option('mandir') / 'man1')
endforeach

# Desktop entry, so that aperi can be chosen as the default application
install_data('extra/aperi.desktop', install_dir : datadir / 'applications')

# Documentation and examples
docdir = datadir / 'doc' / meson.project_name()
install_data('README.md', 'Changelog', install_dir : docdir)
install_data('extra/config', 'extra/mimeapps.list', 'extra/show_items.py',
             install_dir : docdir / 'examples')

test('aperi', find_program('tests/test.sh'),
     env : ['APERI=' + aperi_exe.full_path()],
     protocol : 'tap')
