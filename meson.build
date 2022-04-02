project('bopen', 'c', version: '0.1')
src = ['bopen.c']

gtk4_dep = dependency('gtk4', required: get_option('gtk4'))
c_args = []
if gtk4_dep.found()
  c_args += '-DHAVE_GTK4'
endif
executable('bopen', sources: src, dependencies: gtk4_dep, c_args: c_args)
