#ifndef NANO_SHELL_LAUNCHER_H
#define NANO_SHELL_LAUNCHER_H

#include <gtk/gtk.h>

/*
 * Creates the app launcher widget.
 * Contains a clock display at top, then a scrollable grid of app entries.
 */
GtkWidget *nano_launcher_new(void);

#endif /* NANO_SHELL_LAUNCHER_H */
