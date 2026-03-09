#include "notifications.h"
#include <gio/gio.h>

/*
 * Notification overlay widget.
 *
 * This is a vertical GtkBox placed at the top of the shell overlay.
 * Each notification is a GtkRevealer containing a banner box.
 * Banners auto-dismiss after 5 seconds.
 */

#define NOTIFICATION_TIMEOUT_SECONDS 5
#define NOTIF_LIST_KEY "nano-notif-list"
#define DBUS_OWNER_ID_KEY "nano-notif-dbus-owner"

/* ── Banner dismiss helpers ────────────────────────────────── */

typedef struct {
    GtkWidget *revealer;
    GtkWidget *container;
} BannerDismissData;

static void
banner_dismiss_data_free(gpointer data)
{
    g_free(data);
}

static void
remove_banner_after_transition(GtkRevealer *revealer, GParamSpec *pspec,
                               gpointer user_data)
{
    (void)pspec;
    GtkWidget *container = GTK_WIDGET(user_data);

    if (!gtk_revealer_get_child_revealed(revealer)) {
        GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(revealer));
        if (parent && GTK_IS_BOX(parent)) {
            gtk_box_remove(GTK_BOX(parent), GTK_WIDGET(revealer));
        }
        (void)container;
    }
}

static gboolean
on_dismiss_timeout(gpointer user_data)
{
    BannerDismissData *d = user_data;
    if (d->revealer && GTK_IS_REVEALER(d->revealer)) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(d->revealer), FALSE);
    }
    return G_SOURCE_REMOVE;
}

/* ── Swipe-to-dismiss ──────────────────────────────────────── */

static void
on_banner_swipe(GtkGestureSwipe *gesture, gdouble vx, gdouble vy,
                gpointer user_data)
{
    (void)gesture;
    (void)vx;
    GtkRevealer *revealer = GTK_REVEALER(user_data);

    /* Swipe up or fast horizontal swipe dismisses */
    if (vy < -50.0 || fabs(vx) > 200.0) {
        gtk_revealer_set_reveal_child(revealer, FALSE);
    }
}

/* ── D-Bus notification server ─────────────────────────────── */

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.Notifications'>"
    "    <method name='Notify'>"
    "      <arg direction='in'  type='s' name='app_name'/>"
    "      <arg direction='in'  type='u' name='replaces_id'/>"
    "      <arg direction='in'  type='s' name='app_icon'/>"
    "      <arg direction='in'  type='s' name='summary'/>"
    "      <arg direction='in'  type='s' name='body'/>"
    "      <arg direction='in'  type='as' name='actions'/>"
    "      <arg direction='in'  type='a{sv}' name='hints'/>"
    "      <arg direction='in'  type='i' name='expire_timeout'/>"
    "      <arg direction='out' type='u' name='id'/>"
    "    </method>"
    "    <method name='GetCapabilities'>"
    "      <arg direction='out' type='as' name='capabilities'/>"
    "    </method>"
    "    <method name='GetServerInformation'>"
    "      <arg direction='out' type='s' name='name'/>"
    "      <arg direction='out' type='s' name='vendor'/>"
    "      <arg direction='out' type='s' name='version'/>"
    "      <arg direction='out' type='s' name='spec_version'/>"
    "    </method>"
    "    <method name='CloseNotification'>"
    "      <arg direction='in'  type='u' name='id'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static guint notification_id_counter = 0;

static void
handle_method_call(GDBusConnection       *connection,
                   const gchar           *sender,
                   const gchar           *object_path,
                   const gchar           *interface_name,
                   const gchar           *method_name,
                   GVariant              *parameters,
                   GDBusMethodInvocation *invocation,
                   gpointer               user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    GtkWidget *notif_widget = GTK_WIDGET(user_data);

    if (g_strcmp0(method_name, "Notify") == 0) {
        const gchar *app_name = NULL;
        guint32 replaces_id = 0;
        const gchar *app_icon = NULL;
        const gchar *summary = NULL;
        const gchar *body = NULL;

        g_variant_get(parameters, "(&su&s&s&s^a&sa{sv}i)",
                      &app_name, &replaces_id, &app_icon,
                      &summary, &body, NULL, NULL, NULL);

        nano_notifications_show(notif_widget, app_name, summary, body);

        notification_id_counter++;
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(u)", notification_id_counter));

    } else if (g_strcmp0(method_name, "GetCapabilities") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&builder, "s", "body");
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(as)", &builder));

    } else if (g_strcmp0(method_name, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(ssss)",
                          "nano-shell", "NanoOS", "0.1.0", "1.2"));

    } else if (g_strcmp0(method_name, "CloseNotification") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call  = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

static void
on_bus_acquired(GDBusConnection *connection,
                const gchar     *name,
                gpointer         user_data)
{
    (void)name;
    GtkWidget *notif_widget = GTK_WIDGET(user_data);

    GDBusNodeInfo *node_info =
        g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    if (!node_info) {
        g_warning("Failed to parse notification D-Bus introspection XML");
        return;
    }

    GError *error = NULL;
    g_dbus_connection_register_object(
        connection,
        "/org/freedesktop/Notifications",
        node_info->interfaces[0],
        &interface_vtable,
        notif_widget,
        NULL,
        &error);

    if (error) {
        g_warning("Failed to register notification object: %s",
                  error->message);
        g_error_free(error);
    }

    g_dbus_node_info_unref(node_info);
}

static void
on_name_acquired(GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_debug("Acquired D-Bus name: %s", name);
}

static void
on_name_lost(GDBusConnection *connection,
             const gchar     *name,
             gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_warning("Lost D-Bus name: %s (another notification daemon running?)",
              name);
}

/* ── Public API ────────────────────────────────────────────── */

GtkWidget *
nano_notifications_new(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "notification-area");
    gtk_widget_set_halign(box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(box, TRUE);
    return box;
}

void
nano_notifications_register_dbus(GtkWidget *notif_widget)
{
    guint owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        "org.freedesktop.Notifications",
        G_BUS_NAME_OWNER_FLAGS_REPLACE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        notif_widget,
        NULL);

    g_object_set_data(G_OBJECT(notif_widget),
                      DBUS_OWNER_ID_KEY,
                      GUINT_TO_POINTER(owner_id));
}

void
nano_notifications_show(GtkWidget   *notif_widget,
                        const gchar *app_name,
                        const gchar *summary,
                        const gchar *body)
{
    g_return_if_fail(GTK_IS_BOX(notif_widget));

    /* Create banner content */
    GtkWidget *banner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(banner, "notification-banner");

    if (app_name && app_name[0]) {
        GtkWidget *app_label = gtk_label_new(app_name);
        gtk_widget_add_css_class(app_label, "notification-app-name");
        gtk_label_set_xalign(GTK_LABEL(app_label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(app_label),
                                PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(banner), app_label);
    }

    if (summary && summary[0]) {
        GtkWidget *sum_label = gtk_label_new(summary);
        gtk_widget_add_css_class(sum_label, "notification-summary");
        gtk_label_set_xalign(GTK_LABEL(sum_label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(sum_label),
                                PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(banner), sum_label);
    }

    if (body && body[0]) {
        GtkWidget *body_label = gtk_label_new(body);
        gtk_widget_add_css_class(body_label, "notification-body");
        gtk_label_set_xalign(GTK_LABEL(body_label), 0.0f);
        gtk_label_set_max_width_chars(GTK_LABEL(body_label), 50);
        gtk_label_set_ellipsize(GTK_LABEL(body_label),
                                PANGO_ELLIPSIZE_END);
        gtk_label_set_lines(GTK_LABEL(body_label), 2);
        gtk_box_append(GTK_BOX(banner), body_label);
    }

    /* Wrap in revealer for slide-in animation */
    GtkWidget *revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 300);
    gtk_revealer_set_child(GTK_REVEALER(revealer), banner);

    /* Swipe-to-dismiss gesture */
    GtkGesture *swipe = gtk_gesture_swipe_new();
    g_signal_connect(swipe, "swipe",
                     G_CALLBACK(on_banner_swipe), revealer);
    gtk_widget_add_controller(banner, GTK_EVENT_CONTROLLER(swipe));

    /* Remove from container once hidden */
    g_signal_connect(revealer, "notify::child-revealed",
                     G_CALLBACK(remove_banner_after_transition),
                     notif_widget);

    /* Add to notification area and reveal */
    gtk_box_append(GTK_BOX(notif_widget), revealer);
    /* Reveal on next idle so the transition actually plays */
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), TRUE);

    /* Auto-dismiss after timeout */
    BannerDismissData *dd = g_new0(BannerDismissData, 1);
    dd->revealer = revealer;
    dd->container = notif_widget;
    g_timeout_add_seconds_full(G_PRIORITY_DEFAULT,
                               NOTIFICATION_TIMEOUT_SECONDS,
                               on_dismiss_timeout, dd,
                               banner_dismiss_data_free);
}
