project('nano-device', 'c',
  version : '0.1',
  default_options : ['c_std=c11', 'warning_level=2'],
)

glib_dep = dependency('glib-2.0', version : '>=2.56')
gio_dep = dependency('gio-2.0', version : '>=2.56')
math_dep = meson.get_compiler('c').find_library('m', required : false)

executable('nano-device-daemon',
  sources : [
    'src/main.c',
  ],
  dependencies : [glib_dep, gio_dep, math_dep],
  install : true,
  install_dir : get_option('bindir'),
)

install_data('data/org.nano.Device.xml',
  install_dir : get_option('datadir') / 'dbus-1' / 'interfaces',
)
