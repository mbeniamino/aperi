project('aperi', 'c', version: '0.4-rc1')
src_aperi = ['aperi.c']
executable('aperi', sources: src_aperi)

dbus_dep = dependency('dbus-1', required: get_option('dbus'))
if dbus_dep.found()
  src_app_chooser = ['app-chooser.c']
  executable('app-chooser', sources: src_app_chooser, dependencies: dbus_dep)
endif
