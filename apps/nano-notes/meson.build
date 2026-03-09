project('nano-notes', 'c',
  version : '0.1',
  default_options : ['c_std=c11', 'warning_level=2'],
)

gtk4_dep = dependency('gtk4', required : false)
gtk3_dep = dependency('gtk+-3.0', required : false)

c_args = []
deps = []

if gtk4_dep.found()
  deps += [gtk4_dep]
  c_args += ['-DUSE_GTK4=1']
elif gtk3_dep.found()
  deps += [gtk3_dep]
  c_args += ['-DUSE_GTK3=1']
else
  error('Either gtk4 or gtk+-3.0 is required')
endif

adw_dep = dependency('libadwaita-1', required : false)
if adw_dep.found()
  c_args += ['-DHAVE_LIBADWAITA=1']
  deps += [adw_dep]
endif

glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')
deps += [glib_dep, gio_dep]

executable('nano-notes',
  sources : ['src/main.c'],
  c_args : c_args,
  dependencies : deps,
  install : true,
  install_dir : get_option('bindir'),
)

install_data('data/org.nano.notes.desktop',
  install_dir : get_option('datadir') / 'applications',
)

install_data('data/style.css',
  install_dir : get_option('datadir') / 'nano-notes',
)
