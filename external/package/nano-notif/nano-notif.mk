################################################################################
#
# nano-notif
#
################################################################################

NANO_NOTIF_VERSION = 0.1.0
NANO_NOTIF_SITE = $(TOPDIR)/../services/nano-notif
NANO_NOTIF_SITE_METHOD = local
NANO_NOTIF_INSTALL_STAGING = NO
NANO_NOTIF_INSTALL_TARGET = YES

NANO_NOTIF_DEPENDENCIES = \
	dbus \
	libglib2

NANO_NOTIF_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
