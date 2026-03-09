project('nano-power', 'c',
  version : '0.1',
  default_options : ['c_std=c11', 'warning_level=2'],
)

glib_dep = dependency('glib-2.0', version : '>=2.56')
gio_dep = dependency('gio-2.0', version : '>=2.56')

udev_dep = dependency('libudev', required : false)

c_args = []
deps = [glib_dep, gio_dep]

if udev_dep.found()
  c_args += ['-DHAVE_LIBUDEV=1']
  deps += [udev_dep]
endif

executable('nano-power-daemon',
  sources : [
    'src/main.c',
  ],
  c_args : c_args,
  dependencies : deps,
  install : true,
  install_dir : get_option('bindir'),
)

install_data('data/org.nano.Power.xml',
  install_dir : get_option('datadir') / 'dbus-1' / 'interfaces',
)
