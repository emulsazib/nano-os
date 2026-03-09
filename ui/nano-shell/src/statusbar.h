#ifndef NANO_SHELL_STATUSBAR_H
#define NANO_SHELL_STATUSBAR_H

#include <gtk/gtk.h>

/* Creates the status bar widget (48px tall CenterBox). */
GtkWidget *nano_statusbar_new(void);

/* Force-update the clock label in the status bar. */
void nano_statusbar_update_clock(GtkWidget *statusbar);

#endif /* NANO_SHELL_STATUSBAR_H */
