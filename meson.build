project('aperi', 'c', version: '0.2')
src_aperi = ['aperi.c']
executable('aperi', sources: src_aperi)

gtk4_dep = dependency('gtk4', required: get_option('gtk4'))
if gtk4_dep.found()
  src_gtk4_opener = ['gtk4-open.c']
  executable('gtk4-open', sources: src_gtk4_opener, dependencies: gtk4_dep)
endif
