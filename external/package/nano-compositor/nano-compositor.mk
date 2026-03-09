################################################################################
#
# nano-compositor
#
################################################################################

NANO_COMPOSITOR_VERSION = 0.1.0
NANO_COMPOSITOR_SITE = $(TOPDIR)/../ui/nano-compositor
NANO_COMPOSITOR_SITE_METHOD = local
NANO_COMPOSITOR_INSTALL_STAGING = NO
NANO_COMPOSITOR_INSTALL_TARGET = YES

NANO_COMPOSITOR_DEPENDENCIES = \
	wlroots \
	wayland \
	wayland-protocols \
	libinput \
	libxkbcommon \
	libdrm \
	mesa3d

NANO_COMPOSITOR_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
