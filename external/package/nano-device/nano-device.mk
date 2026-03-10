################################################################################
#
# nano-device
#
################################################################################

NANO_DEVICE_VERSION = 0.1.0
NANO_DEVICE_SITE = $(TOPDIR)/../services/nano-device
NANO_DEVICE_SITE_METHOD = local
NANO_DEVICE_INSTALL_STAGING = NO
NANO_DEVICE_INSTALL_TARGET = YES

NANO_DEVICE_DEPENDENCIES = \
	dbus \
	libglib2

NANO_DEVICE_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
