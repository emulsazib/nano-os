project('nano-settings', 'c',
  version : '0.1',
  default_options : ['c_std=c11', 'warning_level=2'],
)

glib_dep = dependency('glib-2.0', version : '>=2.56')
gio_dep = dependency('gio-2.0', version : '>=2.56')

executable('nano-settings-daemon',
  sources : [
    'src/main.c',
    'src/settings-store.c',
  ],
  dependencies : [glib_dep, gio_dep],
  install : true,
  install_dir : get_option('bindir'),
)

install_data('data/org.nano.Settings.xml',
  install_dir : get_option('datadir') / 'dbus-1' / 'interfaces',
)
