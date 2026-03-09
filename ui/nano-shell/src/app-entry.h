#ifndef NANO_SHELL_APP_ENTRY_H
#define NANO_SHELL_APP_ENTRY_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NANO_TYPE_APP_ENTRY (nano_app_entry_get_type())

G_DECLARE_FINAL_TYPE(NanoAppEntry, nano_app_entry, NANO, APP_ENTRY, GtkBox)

/*
 * Create an app entry from a GAppInfo (e.g. GDesktopAppInfo).
 * Takes a reference on app_info.
 */
GtkWidget *nano_app_entry_new_from_app_info(GAppInfo *app_info);

/*
 * Create an app entry from explicit name, icon name, and exec command.
 * Used for built-in fallback entries.
 */
GtkWidget *nano_app_entry_new(const gchar *name,
                              const gchar *icon_name,
                              const gchar *exec_cmd);

/* Launch the application associated with this entry. */
void nano_app_entry_launch(NanoAppEntry *self);

G_END_DECLS

#endif /* NANO_SHELL_APP_ENTRY_H */
