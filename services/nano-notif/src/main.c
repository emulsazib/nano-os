#include <glib.h>
#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NOTIF_BUS_NAME    "org.freedesktop.Notifications"
#define NOTIF_OBJECT_PATH "/org/freedesktop/Notifications"
#define NOTIF_IFACE_NAME  "org.freedesktop.Notifications"

/* Close reasons per spec */
#define CLOSE_REASON_EXPIRED   1
#define CLOSE_REASON_DISMISSED 2
#define CLOSE_REASON_CLOSED    3

typedef struct {
    guint32  id;
    gchar   *app_name;
    gchar   *app_icon;
    gchar   *summary;
    gchar   *body;
    gint32   expire_timeout;
    gint64   timestamp;
    guint    timeout_source_id;
} Notification;

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.Notifications'>"
    "    <method name='Notify'>"
    "      <arg name='app_name' direction='in' type='s'/>"
    "      <arg name='replaces_id' direction='in' type='u'/>"
    "      <arg name='app_icon' direction='in' type='s'/>"
    "      <arg name='summary' direction='in' type='s'/>"
    "      <arg name='body' direction='in' type='s'/>"
    "      <arg name='actions' direction='in' type='as'/>"
    "      <arg name='hints' direction='in' type='a{sv}'/>"
    "      <arg name='expire_timeout' direction='in' type='i'/>"
    "      <arg name='id' direction='out' type='u'/>"
    "    </method>"
    "    <method name='CloseNotification'>"
    "      <arg name='id' direction='in' type='u'/>"
    "    </method>"
    "    <method name='GetCapabilities'>"
    "      <arg name='caps' direction='out' type='as'/>"
    "    </method>"
    "    <method name='GetServerInformation'>"
    "      <arg name='name' direction='out' type='s'/>"
    "      <arg name='vendor' direction='out' type='s'/>"
    "      <arg name='version' direction='out' type='s'/>"
    "      <arg name='spec_version' direction='out' type='s'/>"
    "    </method>"
    "    <signal name='NotificationClosed'>"
    "      <arg name='id' type='u'/>"
    "      <arg name='reason' type='u'/>"
    "    </signal>"
    "    <signal name='ActionInvoked'>"
    "      <arg name='id' type='u'/>"
    "      <arg name='action_key' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo   *node_info = NULL;
static GMainLoop       *main_loop = NULL;
static GDBusConnection *connection_ref = NULL;
static GArray          *notifications = NULL;
static guint32          next_id = 1;

static void
notification_free_contents (Notification *n)
{
    if (n->timeout_source_id > 0)
        g_source_remove (n->timeout_source_id);
    g_free (n->app_name);
    g_free (n->app_icon);
    g_free (n->summary);
    g_free (n->body);
}

static Notification *
find_notification_by_id (guint32 id)
{
    for (guint i = 0; i < notifications->len; i++) {
        Notification *n = &g_array_index (notifications, Notification, i);
        if (n->id == id)
            return n;
    }
    return NULL;
}

static void
emit_notification_closed (guint32 id, guint32 reason)
{
    if (connection_ref == NULL)
        return;

    GError *error = NULL;
    g_dbus_connection_emit_signal (connection_ref,
        NULL,
        NOTIF_OBJECT_PATH,
        NOTIF_IFACE_NAME,
        "NotificationClosed",
        g_variant_new ("(uu)", id, reason),
        &error);

    if (error != NULL) {
        g_warning ("Failed to emit NotificationClosed: %s", error->message);
        g_clear_error (&error);
    }
}

static void
remove_notification (guint32 id, guint32 reason)
{
    for (guint i = 0; i < notifications->len; i++) {
        Notification *n = &g_array_index (notifications, Notification, i);
        if (n->id == id) {
            emit_notification_closed (id, reason);
            notification_free_contents (n);
            g_array_remove_index (notifications, i);
            return;
        }
    }
}

typedef struct {
    guint32 id;
} ExpireData;

static gboolean
on_notification_expired (gpointer user_data)
{
    ExpireData *data = (ExpireData *)user_data;
    guint32 id = data->id;
    g_free (data);

    Notification *n = find_notification_by_id (id);
    if (n != NULL) {
        n->timeout_source_id = 0; /* already fired */
        remove_notification (id, CLOSE_REASON_EXPIRED);
    }

    return G_SOURCE_REMOVE;
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;

    if (g_strcmp0 (method_name, "Notify") == 0) {
        const gchar *app_name, *app_icon, *summary, *body;
        guint32 replaces_id;
        gint32 expire_timeout;
        GVariantIter *actions_iter;
        GVariantIter *hints_iter;

        g_variant_get (parameters, "(&su&s&s&sasa{sv}i)",
                       &app_name, &replaces_id, &app_icon,
                       &summary, &body,
                       &actions_iter, &hints_iter,
                       &expire_timeout);
        g_variant_iter_free (actions_iter);
        g_variant_iter_free (hints_iter);

        /* If replaces_id is set and exists, replace it */
        if (replaces_id > 0) {
            Notification *existing = find_notification_by_id (replaces_id);
            if (existing != NULL) {
                if (existing->timeout_source_id > 0) {
                    g_source_remove (existing->timeout_source_id);
                    existing->timeout_source_id = 0;
                }
                g_free (existing->app_name);
                g_free (existing->app_icon);
                g_free (existing->summary);
                g_free (existing->body);
                existing->app_name = g_strdup (app_name);
                existing->app_icon = g_strdup (app_icon);
                existing->summary = g_strdup (summary);
                existing->body = g_strdup (body);
                existing->expire_timeout = expire_timeout;
                existing->timestamp = g_get_real_time ();

                if (expire_timeout > 0) {
                    ExpireData *ed = g_new0 (ExpireData, 1);
                    ed->id = existing->id;
                    existing->timeout_source_id = g_timeout_add (
                        (guint)expire_timeout, on_notification_expired, ed);
                }

                g_dbus_method_invocation_return_value (invocation,
                    g_variant_new ("(u)", existing->id));
                return;
            }
        }

        /* Create new notification */
        Notification n = { 0 };
        n.id = next_id++;
        n.app_name = g_strdup (app_name);
        n.app_icon = g_strdup (app_icon);
        n.summary = g_strdup (summary);
        n.body = g_strdup (body);
        n.expire_timeout = expire_timeout;
        n.timestamp = g_get_real_time ();
        n.timeout_source_id = 0;

        if (expire_timeout > 0) {
            ExpireData *ed = g_new0 (ExpireData, 1);
            ed->id = n.id;
            n.timeout_source_id = g_timeout_add (
                (guint)expire_timeout, on_notification_expired, ed);
        }

        g_array_append_val (notifications, n);
        g_message ("Notification %u: [%s] %s", n.id, app_name, summary);

        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(u)", n.id));

    } else if (g_strcmp0 (method_name, "CloseNotification") == 0) {
        guint32 id;
        g_variant_get (parameters, "(u)", &id);
        remove_notification (id, CLOSE_REASON_CLOSED);
        g_dbus_method_invocation_return_value (invocation, NULL);

    } else if (g_strcmp0 (method_name, "GetCapabilities") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
        g_variant_builder_add (&builder, "s", "body");
        g_variant_builder_add (&builder, "s", "actions");
        g_variant_builder_add (&builder, "s", "persistence");
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(as)", &builder));

    } else if (g_strcmp0 (method_name, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(ssss)",
                           "NanoOS Notifications",
                           "NanoOS",
                           "0.1",
                           "1.2"));

    } else {
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method: %s", method_name);
    }
}

static const GDBusInterfaceVTable vtable = {
    handle_method_call,
    NULL,
    NULL,
    { 0 }
};

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    (void)name;
    (void)user_data;

    connection_ref = connection;

    GError *error = NULL;
    g_dbus_connection_register_object (connection,
        NOTIF_OBJECT_PATH,
        node_info->interfaces[0],
        &vtable,
        NULL, NULL,
        &error);

    if (error != NULL) {
        g_error ("Failed to register object: %s", error->message);
    }

    g_message ("nano-notif-daemon: object registered on D-Bus");
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_message ("Acquired bus name: %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_warning ("Lost bus name: %s, exiting", name);
    g_main_loop_quit (main_loop);
}

static void
on_signal (int signum)
{
    (void)signum;
    if (main_loop != NULL)
        g_main_loop_quit (main_loop);
}

int
main (int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    signal (SIGTERM, on_signal);
    signal (SIGINT, on_signal);

    node_info = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
    g_assert (node_info != NULL);

    notifications = g_array_new (FALSE, TRUE, sizeof (Notification));

    guint owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
        NOTIF_BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        NULL, NULL);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_message ("nano-notif-daemon starting...");
    g_main_loop_run (main_loop);

    /* Cleanup */
    for (guint i = 0; i < notifications->len; i++) {
        notification_free_contents (&g_array_index (notifications, Notification, i));
    }
    g_array_free (notifications, TRUE);

    g_bus_unown_name (owner_id);
    g_main_loop_unref (main_loop);
    g_dbus_node_info_unref (node_info);

    g_message ("nano-notif-daemon stopped");
    return 0;
}
