project('nano-compositor', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ])

cc = meson.get_compiler('c')

# Dependencies
wlroots = dependency('wlroots', version: '>=0.17', required: true)
wayland_server = dependency('wayland-server', required: true)
wayland_protos = dependency('wayland-protocols', required: true)
xkbcommon = dependency('xkbcommon', required: true)
libinput = dependency('libinput', required: true)
pixman = dependency('pixman-1', required: true)
math = cc.find_library('m', required: false)

# Wayland protocol scanner
wayland_scanner = find_program('wayland-scanner', required: true)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

# Generate xdg-shell protocol
xdg_shell_xml = wl_protocol_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml'
xdg_shell_c = custom_target('xdg-shell-protocol-c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
xdg_shell_h = custom_target('xdg-shell-protocol-h',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

src = files(
  'src/main.c',
  'src/server.c',
  'src/output.c',
  'src/input.c',
  'src/xdg.c',
  'src/layer.c',
)

nano_comp = executable('nano-compositor',
  [src, xdg_shell_c, xdg_shell_h],
  dependencies: [wlroots, wayland_server, xkbcommon, libinput, pixman, math],
  include_directories: include_directories('include'),
  install: true)