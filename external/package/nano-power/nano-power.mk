################################################################################
#
# nano-power
#
################################################################################

NANO_POWER_VERSION = 0.1.0
NANO_POWER_SITE = $(TOPDIR)/../services/nano-power
NANO_POWER_SITE_METHOD = local
NANO_POWER_INSTALL_STAGING = NO
NANO_POWER_INSTALL_TARGET = YES

NANO_POWER_DEPENDENCIES = \
	dbus \
	libglib2

NANO_POWER_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
