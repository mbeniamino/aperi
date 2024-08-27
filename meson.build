project('aperi', 'c', version: '(use git to check the version)')

vcs_dep = vcs_tag(command: ['git', 'describe', '--always', '--dirty'],
     input: 'git_version.h.in',
     output: 'git_version.h',
     replace_string: '@GIT_VERSION@',
     fallback: 'unknown')

src_aperi = ['aperi.c', vcs_dep]
executable('aperi', sources: src_aperi)

dbus_dep = dependency('dbus-1', required: get_option('dbus'))
if dbus_dep.found()
  src_app_chooser = ['app-chooser.c', vcs_dep]
  executable('app-chooser', sources: src_app_chooser,
             dependencies: dbus_dep)
endif
