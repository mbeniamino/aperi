project('aperi', 'c', version: '0.9.0')

conf_data = configuration_data()
conf_data.set('version', meson.project_version())
configure_file(input : 'config.h.in',
               output : 'config.h',
               configuration : conf_data)

src_aperi = ['aperi.c']
executable('aperi', sources: src_aperi, install : true)

dbus_dep = dependency('dbus-1', required: get_option('dbus'))
if dbus_dep.found()
  src_app_chooser = ['app-chooser.c']
  executable('app-chooser', sources: src_app_chooser,
             dependencies: dbus_dep, install : true)
endif
