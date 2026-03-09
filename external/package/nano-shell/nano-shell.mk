################################################################################
#
# nano-shell
#
################################################################################

NANO_SHELL_VERSION = 0.1.0
NANO_SHELL_SITE = $(TOPDIR)/../ui/nano-shell
NANO_SHELL_SITE_METHOD = local
NANO_SHELL_INSTALL_STAGING = NO
NANO_SHELL_INSTALL_TARGET = YES

NANO_SHELL_DEPENDENCIES = \
	wayland \
	wayland-protocols \
	libgtk3 \
	libglib2 \
	json-glib \
	cairo \
	pango

NANO_SHELL_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
