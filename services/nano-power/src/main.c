#include <glib.h>
#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define POWER_BUS_NAME    "org.nano.Power"
#define POWER_OBJECT_PATH "/org/nano/Power"
#define POWER_IFACE_NAME  "org.nano.Power"
#define BATTERY_POLL_SECS 30

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.nano.Power'>"
    "    <method name='GetBatteryLevel'>"
    "      <arg name='level' direction='out' type='i'/>"
    "    </method>"
    "    <method name='GetBatteryState'>"
    "      <arg name='state' direction='out' type='s'/>"
    "    </method>"
    "    <method name='GetPowerProfile'>"
    "      <arg name='profile' direction='out' type='s'/>"
    "    </method>"
    "    <method name='SetPowerProfile'>"
    "      <arg name='profile' direction='in' type='s'/>"
    "    </method>"
    "    <method name='Shutdown'/>"
    "    <method name='Reboot'/>"
    "    <signal name='BatteryChanged'>"
    "      <arg name='level' type='i'/>"
    "      <arg name='state' type='s'/>"
    "    </signal>"
    "    <signal name='PowerProfileChanged'>"
    "      <arg name='profile' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo   *node_info = NULL;
static GMainLoop       *main_loop = NULL;
static GDBusConnection *connection_ref = NULL;

static gchar *current_profile = NULL;
static gint   last_battery_level = -1;
static gchar *last_battery_state = NULL;

static gint
read_battery_level (void)
{
    const gchar *path = "/sys/class/power_supply/BAT0/capacity";
    g_autofree gchar *contents = NULL;

    if (!g_file_get_contents (path, &contents, NULL, NULL)) {
        /* Try BAT1 */
        path = "/sys/class/power_supply/BAT1/capacity";
        if (!g_file_get_contents (path, &contents, NULL, NULL)) {
            return 100; /* No battery (QEMU) */
        }
    }

    return CLAMP (atoi (g_strstrip (contents)), 0, 100);
}

static gchar *
read_battery_state (void)
{
    const gchar *path = "/sys/class/power_supply/BAT0/status";
    g_autofree gchar *contents = NULL;

    if (!g_file_get_contents (path, &contents, NULL, NULL)) {
        path = "/sys/class/power_supply/BAT1/status";
        if (!g_file_get_contents (path, &contents, NULL, NULL)) {
            return g_strdup ("full"); /* No battery (QEMU) */
        }
    }

    gchar *status = g_strstrip (contents);

    if (g_ascii_strcasecmp (status, "Charging") == 0)
        return g_strdup ("charging");
    else if (g_ascii_strcasecmp (status, "Discharging") == 0)
        return g_strdup ("discharging");
    else if (g_ascii_strcasecmp (status, "Full") == 0)
        return g_strdup ("full");
    else
        return g_strdup ("unknown");
}

static void
emit_battery_changed (gint level, const gchar *state)
{
    if (connection_ref == NULL)
        return;

    GError *error = NULL;
    g_dbus_connection_emit_signal (connection_ref,
        NULL,
        POWER_OBJECT_PATH,
        POWER_IFACE_NAME,
        "BatteryChanged",
        g_variant_new ("(is)", level, state),
        &error);

    if (error != NULL) {
        g_warning ("Failed to emit BatteryChanged: %s", error->message);
        g_clear_error (&error);
    }
}

static gboolean
on_poll_battery (gpointer user_data)
{
    (void)user_data;

    gint level = read_battery_level ();
    g_autofree gchar *state = read_battery_state ();

    if (level != last_battery_level ||
        g_strcmp0 (state, last_battery_state) != 0)
    {
        last_battery_level = level;
        g_free (last_battery_state);
        last_battery_state = g_strdup (state);
        emit_battery_changed (level, state);
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
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;

    if (g_strcmp0 (method_name, "GetBatteryLevel") == 0) {
        gint level = read_battery_level ();
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(i)", level));

    } else if (g_strcmp0 (method_name, "GetBatteryState") == 0) {
        g_autofree gchar *state = read_battery_state ();
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(s)", state));

    } else if (g_strcmp0 (method_name, "GetPowerProfile") == 0) {
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(s)", current_profile));

    } else if (g_strcmp0 (method_name, "SetPowerProfile") == 0) {
        const gchar *profile;
        g_variant_get (parameters, "(&s)", &profile);

        if (g_strcmp0 (profile, "balanced") != 0 &&
            g_strcmp0 (profile, "power-saver") != 0 &&
            g_strcmp0 (profile, "performance") != 0)
        {
            g_dbus_method_invocation_return_error (invocation, G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "Invalid power profile: %s", profile);
            return;
        }

        g_free (current_profile);
        current_profile = g_strdup (profile);

        /* Emit PowerProfileChanged signal */
        GError *error = NULL;
        g_dbus_connection_emit_signal (connection,
            NULL,
            POWER_OBJECT_PATH,
            POWER_IFACE_NAME,
            "PowerProfileChanged",
            g_variant_new ("(s)", current_profile),
            &error);
        if (error != NULL) {
            g_warning ("Failed to emit PowerProfileChanged: %s", error->message);
            g_clear_error (&error);
        }

        g_dbus_method_invocation_return_value (invocation, NULL);

    } else if (g_strcmp0 (method_name, "Shutdown") == 0) {
        g_message ("Shutdown requested");
        g_dbus_method_invocation_return_value (invocation, NULL);

        /* Execute shutdown asynchronously */
        g_spawn_command_line_async ("/sbin/poweroff", NULL);

    } else if (g_strcmp0 (method_name, "Reboot") == 0) {
        g_message ("Reboot requested");
        g_dbus_method_invocation_return_value (invocation, NULL);

        g_spawn_command_line_async ("/sbin/reboot", NULL);

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
        POWER_OBJECT_PATH,
        node_info->interfaces[0],
        &vtable,
        NULL, NULL,
        &error);

    if (error != NULL) {
        g_error ("Failed to register object: %s", error->message);
    }

    g_message ("nano-power-daemon: object registered on D-Bus");
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

    current_profile = g_strdup ("balanced");
    last_battery_level = read_battery_level ();
    last_battery_state = read_battery_state ();

    guint owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
        POWER_BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        NULL, NULL);

    /* Poll battery every 30 seconds */
    g_timeout_add_seconds (BATTERY_POLL_SECS, on_poll_battery, NULL);

    main_loop = g_main_loop_new (NULL, FALSE);
    g_message ("nano-power-daemon starting...");
    g_main_loop_run (main_loop);

    g_bus_unown_name (owner_id);
    g_main_loop_unref (main_loop);
    g_dbus_node_info_unref (node_info);
    g_free (current_profile);
    g_free (last_battery_state);

    g_message ("nano-power-daemon stopped");
    return 0;
}
