################################################################################
#
# nano-clock
#
################################################################################

NANO_CLOCK_VERSION = 0.1.0
NANO_CLOCK_SITE = $(TOPDIR)/../apps/nano-clock
NANO_CLOCK_SITE_METHOD = local
NANO_CLOCK_INSTALL_STAGING = NO
NANO_CLOCK_INSTALL_TARGET = YES

NANO_CLOCK_DEPENDENCIES = \
	wayland \
	libgtk3 \
	libglib2

NANO_CLOCK_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
