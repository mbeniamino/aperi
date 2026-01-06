project('aperi', 'c', version: '0.10.1')

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

  src_fm1 = ['aperi_fm1.c']
  executable('aperi_fm1', sources: src_fm1,
             dependencies: dbus_dep, install : true)
endif

src_wipewine = ['wipewine.c']
executable('wipewine', sources: src_wipewine,
           install : true)
