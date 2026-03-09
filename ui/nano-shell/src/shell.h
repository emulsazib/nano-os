#ifndef NANO_SHELL_SHELL_H
#define NANO_SHELL_SHELL_H

#include <gtk/gtk.h>

typedef struct _NanoShell NanoShell;

/*
 * Create the main shell window and all child components.
 * The window is set to fullscreen on realize.
 */
NanoShell *nano_shell_new(GtkApplication *app);

/* Access the underlying GtkWindow */
GtkWindow *nano_shell_get_window(NanoShell *shell);

/* Trigger a notification from within the shell (for testing). */
void nano_shell_show_notification(NanoShell   *shell,
                                  const gchar *app_name,
                                  const gchar *summary,
                                  const gchar *body);

/* Free shell resources. Usually not needed if the window owns everything. */
void nano_shell_destroy(NanoShell *shell);

#endif /* NANO_SHELL_SHELL_H */
