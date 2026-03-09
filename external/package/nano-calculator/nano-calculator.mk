################################################################################
#
# nano-calculator
#
################################################################################

NANO_CALCULATOR_VERSION = 0.1.0
NANO_CALCULATOR_SITE = $(TOPDIR)/../apps/nano-calculator
NANO_CALCULATOR_SITE_METHOD = local
NANO_CALCULATOR_INSTALL_STAGING = NO
NANO_CALCULATOR_INSTALL_TARGET = YES

NANO_CALCULATOR_DEPENDENCIES = \
	wayland \
	libgtk3 \
	libglib2

NANO_CALCULATOR_CONF_OPTS = \
	-Dwerror=false

$(eval $(meson-package))
