#include "shell.h"
#include "statusbar.h"
#include "launcher.h"
#include "notifications.h"
#include "clock.h"

#ifdef HAVE_LIBADWAITA
#include <adwaita.h>
#endif

struct _NanoShell {
    GtkWindow *window;
    GtkWidget *overlay;
    GtkWidget *statusbar;
    GtkWidget *launcher;
    GtkWidget *notification_area;
    GtkWidget *home_indicator;
};

/* ── Fullscreen on realize ─────────────────────────────────── */

static void
on_realize(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    gtk_window_fullscreen(GTK_WINDOW(widget));
}

/* ── Swipe gesture handling ────────────────────────────────── */

static void
on_swipe(GtkGestureSwipe *gesture, gdouble vx, gdouble vy,
         gpointer user_data)
{
    (void)gesture;
    (void)vx;
    NanoShell *shell = (NanoShell *)user_data;

    /* Swipe up from bottom: could toggle app drawer in the future */
    if (vy < -200.0) {
        g_debug("nano-shell: swipe up detected (velocity %.1f)", vy);
        /* MVP: show a test notification */
        nano_shell_show_notification(shell, "NanoOS",
                                     "Swipe detected",
                                     "Swipe up gesture recognized");
    }
}

/* ── Shell construction ────────────────────────────────────── */

NanoShell *
nano_shell_new(GtkApplication *app)
{
    NanoShell *shell = g_new0(NanoShell, 1);

#ifdef HAVE_LIBADWAITA
    /* Force dark color scheme */
    AdwStyleManager *style_mgr = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style_mgr,
                                       ADW_COLOR_SCHEME_FORCE_DARK);
#endif

    /* ── Window ──────────────────────────────────────────── */
    shell->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(shell->window, "NanoOS");
    gtk_window_set_default_size(shell->window, 720, 1440);
    gtk_window_set_decorated(shell->window, FALSE);
    g_signal_connect(shell->window, "realize",
                     G_CALLBACK(on_realize), NULL);

    /* ── Overlay container ───────────────────────────────── */
    shell->overlay = gtk_overlay_new();
    gtk_window_set_child(shell->window, shell->overlay);

    /* ── Main content: vertical box with launcher + indicator */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(main_box, TRUE);
    gtk_widget_set_hexpand(main_box, TRUE);

    /* Launcher (fills most of the screen) */
    shell->launcher = nano_launcher_new();
    gtk_box_append(GTK_BOX(main_box), shell->launcher);

    /* Home indicator at bottom */
    GtkWidget *indicator_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(indicator_area, "home-indicator-area");
    gtk_widget_set_halign(indicator_area, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(indicator_area, GTK_ALIGN_END);

    shell->home_indicator = gtk_drawing_area_new();
    gtk_widget_add_css_class(shell->home_indicator, "home-indicator");
    gtk_widget_set_size_request(shell->home_indicator, 134, 5);
    gtk_widget_set_halign(shell->home_indicator, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(indicator_area), shell->home_indicator);

    gtk_box_append(GTK_BOX(main_box), indicator_area);

    gtk_overlay_set_child(GTK_OVERLAY(shell->overlay), main_box);

    /* ── Status bar (overlay, top) ───────────────────────── */
    shell->statusbar = nano_statusbar_new();
    gtk_widget_set_halign(shell->statusbar, GTK_ALIGN_FILL);
    gtk_widget_set_valign(shell->statusbar, GTK_ALIGN_START);
    gtk_overlay_add_overlay(GTK_OVERLAY(shell->overlay), shell->statusbar);

    /* ── Notification area (overlay, below status bar) ──── */
    shell->notification_area = nano_notifications_new();
    gtk_widget_set_valign(shell->notification_area, GTK_ALIGN_START);
    gtk_overlay_add_overlay(GTK_OVERLAY(shell->overlay),
                            shell->notification_area);

    /* Register for D-Bus notifications */
    nano_notifications_register_dbus(shell->notification_area);

    /* ── Swipe gesture on the whole window ────────────────── */
    GtkGesture *swipe = gtk_gesture_swipe_new();
    g_signal_connect(swipe, "swipe", G_CALLBACK(on_swipe), shell);
    gtk_widget_add_controller(GTK_WIDGET(shell->window),
                              GTK_EVENT_CONTROLLER(swipe));

    /* ── Show ─────────────────────────────────────────────── */
    gtk_window_present(shell->window);

    return shell;
}

GtkWindow *
nano_shell_get_window(NanoShell *shell)
{
    g_return_val_if_fail(shell != NULL, NULL);
    return shell->window;
}

void
nano_shell_show_notification(NanoShell   *shell,
                             const gchar *app_name,
                             const gchar *summary,
                             const gchar *body)
{
    g_return_if_fail(shell != NULL);
    nano_notifications_show(shell->notification_area,
                            app_name, summary, body);
}

void
nano_shell_destroy(NanoShell *shell)
{
    if (shell) {
        g_free(shell);
    }
}
