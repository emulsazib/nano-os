project('nano-shell', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ])

gnome = import('gnome')

gtk4 = dependency('gtk4', required: false)
gtk3 = dependency('gtk+-3.0', required: false)

deps = []
c_args = []

if gtk4.found()
  deps += gtk4
  c_args += '-DUSE_GTK4=1'
elif gtk3.found()
  deps += gtk3
  c_args += '-DUSE_GTK3=1'
else
  error('Either gtk4 or gtk+-3.0 is required')
endif

adwaita = dependency('libadwaita-1', required: false)
if adwaita.found()
  deps += adwaita
  c_args += '-DHAVE_LIBADWAITA=1'
endif

glib = dependency('glib-2.0')
gio = dependency('gio-2.0')
json_glib = dependency('json-glib-1.0', required: false)
deps += [glib, gio]
if json_glib.found()
  deps += json_glib
  c_args += '-DHAVE_JSON_GLIB=1'
endif

resources = gnome.compile_resources('nano-shell-resources',
  'data/nano-shell.gresource.xml',
  source_dir: 'data')

src = files(
  'src/main.c',
  'src/shell.c',
  'src/statusbar.c',
  'src/launcher.c',
  'src/app-entry.c',
  'src/notifications.c',
  'src/clock.c',
)

executable('nano-shell',
  [src, resources],
  c_args: c_args,
  dependencies: deps,
  install: true)
