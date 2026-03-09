project('nano-clock', 'c',
  version : '0.1',
  default_options : ['c_std=c11', 'warning_level=2'],
)

gtk_dep = dependency('gtk4', version : '>=4.0')

adw_dep = dependency('libadwaita-1', required : false)

c_args = []
deps = [gtk_dep]

if adw_dep.found()
  c_args += ['-DHAVE_LIBADWAITA=1']
  deps += [adw_dep]
endif

executable('nano-clock',
  sources : [
    'src/main.c',
  ],
  c_args : c_args,
  dependencies : deps,
  install : true,
  install_dir : get_option('bindir'),
)

install_data('data/org.nano.clock.desktop',
  install_dir : get_option('datadir') / 'applications',
)

install_data('data/style.css',
  install_dir : get_option('datadir') / 'nano-clock',
)
