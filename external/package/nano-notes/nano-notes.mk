################################################################################
#
# nano-notes
#
################################################################################

NANO_NOTES_VERSION = 0.1.0
NANO_NOTES_SITE = $(TOPDIR)/../apps/nano-notes
NANO_NOTES_SITE_METHOD = local
NANO_NOTES_INSTALL_STAGING = NO
NANO_NOTES_INSTALL_TARGET = YES

NANO_NOTES_DEPENDENCIES = \
	wayland \
	libgtk3 \
	libglib2

NANO_NOTES_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
