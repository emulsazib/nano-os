################################################################################
#
# nano-net
#
################################################################################

NANO_NET_VERSION = 0.1.0
NANO_NET_SITE = $(TOPDIR)/../services/nano-net
NANO_NET_SITE_METHOD = local
NANO_NET_INSTALL_STAGING = NO
NANO_NET_INSTALL_TARGET = YES

NANO_NET_DEPENDENCIES = \
	dbus \
	libglib2

NANO_NET_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
