################################################################################
#
# nano-settings
#
################################################################################

NANO_SETTINGS_VERSION = 0.1.0
NANO_SETTINGS_SITE = $(TOPDIR)/../services/nano-settings
NANO_SETTINGS_SITE_METHOD = local
NANO_SETTINGS_INSTALL_STAGING = NO
NANO_SETTINGS_INSTALL_TARGET = YES

NANO_SETTINGS_DEPENDENCIES = \
	dbus \
	libglib2 \
	json-glib

NANO_SETTINGS_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
