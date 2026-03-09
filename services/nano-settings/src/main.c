#include <glib.h>
#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>

#include "settings-store.h"

#define SETTINGS_BUS_NAME   "org.nano.Settings"
#define SETTINGS_OBJECT_PATH "/org/nano/Settings"
#define SETTINGS_IFACE_NAME  "org.nano.Settings"
#define SETTINGS_CONF_PATH   "/etc/nano/settings.conf"

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.nano.Settings'>"
    "    <method name='Get'>"
    "      <arg name='section' direction='in' type='s'/>"
    "      <arg name='key' direction='in' type='s'/>"
    "      <arg name='value' direction='out' type='s'/>"
    "    </method>"
    "    <method name='Set'>"
    "      <arg name='section' direction='in' type='s'/>"
    "      <arg name='key' direction='in' type='s'/>"
    "      <arg name='value' direction='in' type='s'/>"
    "    </method>"
    "    <method name='GetAll'>"
    "      <arg name='settings' direction='out' type='a{sa{ss}}'/>"
    "    </method>"
    "    <method name='Save'/>"
    "    <signal name='SettingChanged'>"
    "      <arg name='section' type='s'/>"
    "      <arg name='key' type='s'/>"
    "      <arg name='value' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *node_info = NULL;
static SettingsStore *store = NULL;
static GMainLoop     *main_loop = NULL;
static GDBusConnection *connection_ref = NULL;

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

    if (g_strcmp0 (method_name, "Get") == 0) {
        const gchar *section, *key;
        g_variant_get (parameters, "(&s&s)", &section, &key);

        GError *error = NULL;
        g_autofree gchar *value = settings_store_get (store, section, key, &error);
        if (value == NULL) {
            g_dbus_method_invocation_return_error (invocation, G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "Key '%s' not found in section '%s': %s",
                key, section, error ? error->message : "unknown error");
            g_clear_error (&error);
            return;
        }

        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(s)", value));

    } else if (g_strcmp0 (method_name, "Set") == 0) {
        const gchar *section, *key, *value;
        g_variant_get (parameters, "(&s&s&s)", &section, &key, &value);

        settings_store_set (store, section, key, value);

        /* Emit SettingChanged signal */
        GError *error = NULL;
        g_dbus_connection_emit_signal (connection,
            NULL,
            SETTINGS_OBJECT_PATH,
            SETTINGS_IFACE_NAME,
            "SettingChanged",
            g_variant_new ("(sss)", section, key, value),
            &error);
        if (error != NULL) {
            g_warning ("Failed to emit SettingChanged: %s", error->message);
            g_clear_error (&error);
        }

        g_dbus_method_invocation_return_value (invocation, NULL);

    } else if (g_strcmp0 (method_name, "GetAll") == 0) {
        GVariant *all = settings_store_get_all (store);
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new_tuple (&all, 1));

    } else if (g_strcmp0 (method_name, "Save") == 0) {
        GError *error = NULL;
        if (!settings_store_save (store, SETTINGS_CONF_PATH, &error)) {
            g_dbus_method_invocation_return_gerror (invocation, error);
            g_clear_error (&error);
            return;
        }
        g_dbus_method_invocation_return_value (invocation, NULL);

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
        SETTINGS_OBJECT_PATH,
        node_info->interfaces[0],
        &vtable,
        NULL, NULL,
        &error);

    if (error != NULL) {
        g_error ("Failed to register object: %s", error->message);
    }

    g_message ("nano-settings-daemon: object registered on D-Bus");
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

    store = settings_store_new ();

    GError *error = NULL;
    settings_store_load (store, SETTINGS_CONF_PATH, &error);
    if (error != NULL) {
        g_warning ("Could not load settings: %s", error->message);
        g_clear_error (&error);
    }

    guint owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
        SETTINGS_BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        NULL, NULL);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_message ("nano-settings-daemon starting...");
    g_main_loop_run (main_loop);

    g_bus_unown_name (owner_id);
    g_main_loop_unref (main_loop);
    g_dbus_node_info_unref (node_info);
    settings_store_free (store);

    g_message ("nano-settings-daemon stopped");
    return 0;
}
