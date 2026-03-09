#include <glib.h>
#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NET_BUS_NAME    "org.nano.Network"
#define NET_OBJECT_PATH "/org/nano/Network"
#define NET_IFACE_NAME  "org.nano.Network"
#define NET_POLL_SECS   10

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.nano.Network'>"
    "    <method name='GetStatus'>"
    "      <arg name='connected' direction='out' type='b'/>"
    "      <arg name='type' direction='out' type='s'/>"
    "      <arg name='name' direction='out' type='s'/>"
    "    </method>"
    "    <method name='GetInterfaces'>"
    "      <arg name='interfaces' direction='out' type='a(sbs)'/>"
    "    </method>"
    "    <method name='GetWifiNetworks'>"
    "      <arg name='networks' direction='out' type='a(ssi)'/>"
    "    </method>"
    "    <signal name='NetworkChanged'>"
    "      <arg name='connected' type='b'/>"
    "      <arg name='type' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo   *node_info = NULL;
static GMainLoop       *main_loop = NULL;
static GDBusConnection *connection_ref = NULL;
static gboolean         last_connected = FALSE;
static gchar           *last_net_type = NULL;

static gboolean
is_interface_up (const gchar *ifname)
{
    g_autofree gchar *path = g_strdup_printf (
        "/sys/class/net/%s/operstate", ifname);
    g_autofree gchar *contents = NULL;

    if (!g_file_get_contents (path, &contents, NULL, NULL))
        return FALSE;

    gchar *state = g_strstrip (contents);
    return (g_strcmp0 (state, "up") == 0);
}

static gchar *
get_interface_ip (const gchar *ifname)
{
    /* Use 'ip addr show <ifname>' to get the IP address */
    g_autofree gchar *cmd = g_strdup_printf (
        "ip -4 addr show %s 2>/dev/null | "
        "grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}'",
        ifname);

    g_autofree gchar *output = NULL;
    if (!g_spawn_command_line_sync (cmd, &output, NULL, NULL, NULL))
        return g_strdup ("");

    if (output == NULL || output[0] == '\0')
        return g_strdup ("");

    /* Take the first line */
    gchar *newline = strchr (output, '\n');
    if (newline != NULL)
        *newline = '\0';

    return g_strdup (g_strstrip (output));
}

static gchar *
detect_network_type (const gchar *ifname)
{
    if (g_str_has_prefix (ifname, "wl"))
        return g_strdup ("wifi");
    else if (g_str_has_prefix (ifname, "eth") ||
             g_str_has_prefix (ifname, "enp") ||
             g_str_has_prefix (ifname, "ens"))
        return g_strdup ("ethernet");
    else if (g_str_has_prefix (ifname, "ww"))
        return g_strdup ("cellular");
    else
        return g_strdup ("unknown");
}

static GList *
list_network_interfaces (void)
{
    GList *result = NULL;
    GDir *dir = g_dir_open ("/sys/class/net", 0, NULL);
    if (dir == NULL)
        return NULL;

    const gchar *name;
    while ((name = g_dir_read_name (dir)) != NULL) {
        /* Skip loopback */
        if (g_strcmp0 (name, "lo") == 0)
            continue;
        result = g_list_append (result, g_strdup (name));
    }
    g_dir_close (dir);
    return result;
}

static void
get_status (gboolean *out_connected, gchar **out_type, gchar **out_name)
{
    *out_connected = FALSE;
    *out_type = g_strdup ("none");
    *out_name = g_strdup ("");

    GList *ifaces = list_network_interfaces ();
    for (GList *l = ifaces; l != NULL; l = l->next) {
        const gchar *ifname = (const gchar *)l->data;
        if (is_interface_up (ifname)) {
            *out_connected = TRUE;
            g_free (*out_type);
            g_free (*out_name);
            *out_type = detect_network_type (ifname);
            *out_name = g_strdup (ifname);
            break;
        }
    }

    g_list_free_full (ifaces, g_free);
}

static void
emit_network_changed (gboolean connected, const gchar *type)
{
    if (connection_ref == NULL)
        return;

    GError *error = NULL;
    g_dbus_connection_emit_signal (connection_ref,
        NULL,
        NET_OBJECT_PATH,
        NET_IFACE_NAME,
        "NetworkChanged",
        g_variant_new ("(bs)", connected, type),
        &error);

    if (error != NULL) {
        g_warning ("Failed to emit NetworkChanged: %s", error->message);
        g_clear_error (&error);
    }
}

static gboolean
on_poll_network (gpointer user_data)
{
    (void)user_data;

    gboolean connected;
    g_autofree gchar *type = NULL;
    g_autofree gchar *name = NULL;

    get_status (&connected, &type, &name);

    if (connected != last_connected ||
        g_strcmp0 (type, last_net_type) != 0)
    {
        last_connected = connected;
        g_free (last_net_type);
        last_net_type = g_strdup (type);
        emit_network_changed (connected, type);
    }

    return G_SOURCE_CONTINUE;
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
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)parameters;
    (void)user_data;

    if (g_strcmp0 (method_name, "GetStatus") == 0) {
        gboolean connected;
        g_autofree gchar *type = NULL;
        g_autofree gchar *name = NULL;

        get_status (&connected, &type, &name);

        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(bss)", connected, type, name));

    } else if (g_strcmp0 (method_name, "GetInterfaces") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sbs)"));

        GList *ifaces = list_network_interfaces ();
        for (GList *l = ifaces; l != NULL; l = l->next) {
            const gchar *ifname = (const gchar *)l->data;
            gboolean up = is_interface_up (ifname);
            g_autofree gchar *ip = get_interface_ip (ifname);

            g_variant_builder_add (&builder, "(sbs)", ifname, up, ip);
        }
        g_list_free_full (ifaces, g_free);

        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(a(sbs))", &builder));

    } else if (g_strcmp0 (method_name, "GetWifiNetworks") == 0) {
        /* Placeholder: return empty list for QEMU */
        GVariantBuilder builder;
        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ssi)"));

        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(a(ssi))", &builder));

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
        NET_OBJECT_PATH,
        node_info->interfaces[0],
        &vtable,
        NULL, NULL,
        &error);

    if (error != NULL) {
        g_error ("Failed to register object: %s", error->message);
    }

    g_message ("nano-net-daemon: object registered on D-Bus");
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

    last_net_type = g_strdup ("none");

    guint owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
        NET_BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        NULL, NULL);

    /* Poll network every 10 seconds */
    g_timeout_add_seconds (NET_POLL_SECS, on_poll_network, NULL);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_message ("nano-net-daemon starting...");
    g_main_loop_run (main_loop);

    g_bus_unown_name (owner_id);
    g_main_loop_unref (main_loop);
    g_dbus_node_info_unref (node_info);
    g_free (last_net_type);

    g_message ("nano-net-daemon stopped");
    return 0;
}
