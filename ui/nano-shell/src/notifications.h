#ifndef NANO_SHELL_NOTIFICATIONS_H
#define NANO_SHELL_NOTIFICATIONS_H

#include <gtk/gtk.h>

/*
 * Creates the notification overlay widget.
 * Should be placed in the shell overlay, positioned at the top.
 */
GtkWidget *nano_notifications_new(void);

/*
 * Register on the D-Bus session bus to receive
 * org.freedesktop.Notifications method calls.
 * Call after the widget is realized.
 */
void nano_notifications_register_dbus(GtkWidget *notif_widget);

/*
 * Manually push a notification banner (for testing / internal use).
 */
void nano_notifications_show(GtkWidget  *notif_widget,
                             const gchar *app_name,
                             const gchar *summary,
                             const gchar *body);

#endif /* NANO_SHELL_NOTIFICATIONS_H */
