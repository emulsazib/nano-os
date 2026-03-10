/*
 * nano-device-daemon — NanoOS Device Abstraction Layer
 *
 * Provides D-Bus interfaces for:
 *  - Backlight control (org.nano.Device.Backlight)
 *  - LED control (org.nano.Device.LED)
 *  - Haptic feedback (org.nano.Device.Haptic)
 *  - Screen orientation (org.nano.Device.Orientation)
 *  - Sensor readings (org.nano.Device.Sensors)
 *
 * All hardware access goes through sysfs. Graceful fallback for QEMU
 * where real hardware is absent.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib.h>

#define BUS_NAME  "org.nano.Device"
#define OBJ_PATH  "/org/nano/Device"
#define SYSFS_BACKLIGHT "/sys/class/backlight"
#define SYSFS_LEDS      "/sys/class/leds"

/* ── Globals ──────────────────────────────────────────────────── */

static GMainLoop *main_loop = NULL;
static GDBusConnection *dbus_conn = NULL;
static guint owner_id = 0;

/* Orientation state */
static gchar *current_orientation = NULL;
static gchar *locked_orientation = NULL;
static gboolean auto_rotate = TRUE;

/* ── Sysfs helpers ────────────────────────────────────────────── */

static gboolean
sysfs_read_int(const gchar *path, gint *value)
{
    gchar *contents = NULL;
    gsize length = 0;
    GError *err = NULL;

    if (!g_file_get_contents(path, &contents, &length, &err)) {
        g_clear_error(&err);
        return FALSE;
    }

    *value = (gint)g_ascii_strtoll(g_strstrip(contents), NULL, 10);
    g_free(contents);
    return TRUE;
}

static gboolean
sysfs_write_int(const gchar *path, gint value)
{
    gchar buf[32];
    g_snprintf(buf, sizeof(buf), "%d", value);

    GError *err = NULL;
    if (!g_file_set_contents(path, buf, -1, &err)) {
        g_warning("Failed to write %s: %s", path, err->message);
        g_clear_error(&err);
        return FALSE;
    }
    return TRUE;
}

/* Find the first backlight device in sysfs */
static gchar *
find_backlight_device(void)
{
    DIR *dir = opendir(SYSFS_BACKLIGHT);
    if (!dir)
        return NULL;

    struct dirent *ent;
    gchar *result = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        result = g_strdup(ent->d_name);
        break;
    }
    closedir(dir);
    return result;
}

/* ── Backlight interface ──────────────────────────────────────── */

static gint
backlight_get_brightness(void)
{
    gchar *dev = find_backlight_device();
    if (!dev) return 80; /* QEMU fallback */

    gchar *path = g_strdup_printf(SYSFS_BACKLIGHT "/%s/brightness", dev);
    gint val = 0;
    if (!sysfs_read_int(path, &val))
        val = 80;

    g_free(path);
    g_free(dev);
    return val;
}

static gint
backlight_get_max(void)
{
    gchar *dev = find_backlight_device();
    if (!dev) return 255;

    gchar *path = g_strdup_printf(SYSFS_BACKLIGHT "/%s/max_brightness", dev);
    gint val = 255;
    sysfs_read_int(path, &val);

    g_free(path);
    g_free(dev);
    return val;
}

static gboolean
backlight_set_brightness(gint brightness)
{
    gchar *dev = find_backlight_device();
    if (!dev) {
        g_debug("No backlight device (QEMU mode), brightness=%d", brightness);
        return TRUE;
    }

    gint max_val = backlight_get_max();
    if (brightness < 0) brightness = 0;
    if (brightness > max_val) brightness = max_val;

    gchar *path = g_strdup_printf(SYSFS_BACKLIGHT "/%s/brightness", dev);
    gboolean ok = sysfs_write_int(path, brightness);

    g_free(path);
    g_free(dev);
    return ok;
}

/* ── LED interface ────────────────────────────────────────────── */

static void
led_set(const gchar *led_name, gint brightness)
{
    gchar *path = g_strdup_printf(SYSFS_LEDS "/%s/brightness", led_name);
    if (!sysfs_write_int(path, brightness)) {
        g_debug("No LED '%s' (QEMU mode)", led_name);
    }
    g_free(path);
}

static GVariant *
led_get_all(void)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(si)"));

    DIR *dir = opendir(SYSFS_LEDS);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.')
                continue;

            gchar *path = g_strdup_printf(SYSFS_LEDS "/%s/brightness",
                                          ent->d_name);
            gint val = 0;
            sysfs_read_int(path, &val);
            g_free(path);

            g_variant_builder_add(&builder, "(si)", ent->d_name, val);
        }
        closedir(dir);
    }

    return g_variant_builder_end(&builder);
}

static void
led_blink(const gchar *led_name, gint on_ms, gint off_ms)
{
    gchar *trigger_path = g_strdup_printf(SYSFS_LEDS "/%s/trigger", led_name);
    gchar *delay_on_path = g_strdup_printf(SYSFS_LEDS "/%s/delay_on", led_name);
    gchar *delay_off_path = g_strdup_printf(SYSFS_LEDS "/%s/delay_off", led_name);

    GError *err = NULL;
    g_file_set_contents(trigger_path, "timer", -1, &err);
    g_clear_error(&err);

    sysfs_write_int(delay_on_path, on_ms);
    sysfs_write_int(delay_off_path, off_ms);

    g_free(trigger_path);
    g_free(delay_on_path);
    g_free(delay_off_path);
}

/* ── Haptic interface ─────────────────────────────────────────── */

typedef struct {
    gint duration_ms;
} VibrateData;

static gboolean
vibrate_stop(gpointer user_data)
{
    /* Try to stop vibration via force-feedback or LED vibrator */
    gchar *path = g_strdup_printf(SYSFS_LEDS "/vibrator/brightness");
    sysfs_write_int(path, 0);
    g_free(path);
    g_free(user_data);
    return G_SOURCE_REMOVE;
}

static void
haptic_vibrate(gint duration_ms)
{
    if (duration_ms <= 0 || duration_ms > 5000)
        duration_ms = 100;

    /* Try LED vibrator interface */
    gchar *path = g_strdup_printf(SYSFS_LEDS "/vibrator/brightness");
    if (sysfs_write_int(path, 255)) {
        VibrateData *data = g_new0(VibrateData, 1);
        data->duration_ms = duration_ms;
        g_timeout_add(duration_ms, vibrate_stop, data);
    } else {
        g_debug("No vibration hardware (QEMU mode), duration=%dms", duration_ms);
    }
    g_free(path);
}

/* ── Orientation interface ────────────────────────────────────── */

static const gchar *
read_accelerometer_orientation(void)
{
    /* Read IIO accelerometer to determine orientation.
     * For QEMU: no accelerometer, return "portrait". */
    gchar *path = NULL;

    /* Scan for IIO accelerometer */
    for (gint i = 0; i < 10; i++) {
        gchar *name_path = g_strdup_printf(
            "/sys/bus/iio/devices/iio:device%d/name", i);
        gchar *name = NULL;
        if (g_file_get_contents(name_path, &name, NULL, NULL)) {
            g_strstrip(name);
            if (g_str_has_prefix(name, "accel")) {
                path = g_strdup_printf(
                    "/sys/bus/iio/devices/iio:device%d", i);
                g_free(name);
                g_free(name_path);
                break;
            }
            g_free(name);
        }
        g_free(name_path);
    }

    if (!path) {
        return "portrait";
    }

    gchar *x_path = g_strdup_printf("%s/in_accel_x_raw", path);
    gchar *y_path = g_strdup_printf("%s/in_accel_y_raw", path);

    gint x_raw = 0, y_raw = 0;
    sysfs_read_int(x_path, &x_raw);
    sysfs_read_int(y_path, &y_raw);

    g_free(x_path);
    g_free(y_path);
    g_free(path);

    /* Simple orientation logic based on gravity vector */
    if (abs(y_raw) > abs(x_raw)) {
        return (y_raw > 0) ? "portrait" : "portrait-inverted";
    } else {
        return (x_raw > 0) ? "landscape-right" : "landscape-left";
    }
}

/* ── Sensors interface ────────────────────────────────────────── */

static gdouble
sensor_get_ambient_light(void)
{
    /* Scan IIO for light sensor */
    for (gint i = 0; i < 10; i++) {
        gchar *path = g_strdup_printf(
            "/sys/bus/iio/devices/iio:device%d/in_illuminance_raw", i);
        gint val = 0;
        if (sysfs_read_int(path, &val)) {
            g_free(path);
            return (gdouble)val;
        }
        g_free(path);
    }
    return 500.0; /* QEMU fallback: ~500 lux (indoor) */
}

static gboolean
sensor_get_proximity(void)
{
    for (gint i = 0; i < 10; i++) {
        gchar *path = g_strdup_printf(
            "/sys/bus/iio/devices/iio:device%d/in_proximity_raw", i);
        gint val = 0;
        if (sysfs_read_int(path, &val)) {
            g_free(path);
            return (val > 0);
        }
        g_free(path);
    }
    return FALSE; /* QEMU fallback: not near */
}

/* ── D-Bus method handler ─────────────────────────────────────── */

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
    (void)user_data;

    /* ── Backlight ──────────────────────────────────────── */
    if (g_strcmp0(interface_name, "org.nano.Device.Backlight") == 0) {
        if (g_strcmp0(method_name, "GetBrightness") == 0) {
            gint b = backlight_get_brightness();
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(i)", b));
        } else if (g_strcmp0(method_name, "SetBrightness") == 0) {
            gint brightness;
            g_variant_get(parameters, "(i)", &brightness);
            backlight_set_brightness(brightness);
            g_dbus_method_invocation_return_value(invocation, NULL);

            /* Emit signal */
            GError *err = NULL;
            g_dbus_connection_emit_signal(dbus_conn, NULL, OBJ_PATH,
                "org.nano.Device.Backlight", "BrightnessChanged",
                g_variant_new("(i)", brightness), &err);
            g_clear_error(&err);
        } else if (g_strcmp0(method_name, "GetMaxBrightness") == 0) {
            gint m = backlight_get_max();
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(i)", m));
        }
        return;
    }

    /* ── LED ────────────────────────────────────────────── */
    if (g_strcmp0(interface_name, "org.nano.Device.LED") == 0) {
        if (g_strcmp0(method_name, "SetLED") == 0) {
            const gchar *name;
            gint brightness;
            g_variant_get(parameters, "(&si)", &name, &brightness);
            led_set(name, brightness);
            g_dbus_method_invocation_return_value(invocation, NULL);
        } else if (g_strcmp0(method_name, "GetLEDs") == 0) {
            GVariant *leds = led_get_all();
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new_tuple(&leds, 1));
        } else if (g_strcmp0(method_name, "BlinkLED") == 0) {
            const gchar *name;
            gint on_ms, off_ms;
            g_variant_get(parameters, "(&sii)", &name, &on_ms, &off_ms);
            led_blink(name, on_ms, off_ms);
            g_dbus_method_invocation_return_value(invocation, NULL);
        }
        return;
    }

    /* ── Haptic ─────────────────────────────────────────── */
    if (g_strcmp0(interface_name, "org.nano.Device.Haptic") == 0) {
        if (g_strcmp0(method_name, "Vibrate") == 0) {
            gint duration;
            g_variant_get(parameters, "(i)", &duration);
            haptic_vibrate(duration);
            g_dbus_method_invocation_return_value(invocation, NULL);
        } else if (g_strcmp0(method_name, "VibratePattern") == 0) {
            GVariantIter *iter;
            g_variant_get(parameters, "(ai)", &iter);
            gint val;
            gint total_delay = 0;
            gboolean vibrate_on = TRUE;
            while (g_variant_iter_next(iter, "i", &val)) {
                if (vibrate_on) {
                    /* Schedule vibration on after total_delay */
                    /* Simple implementation: just vibrate each segment */
                    haptic_vibrate(val);
                }
                total_delay += val;
                vibrate_on = !vibrate_on;
            }
            g_variant_iter_free(iter);
            g_dbus_method_invocation_return_value(invocation, NULL);
        }
        return;
    }

    /* ── Orientation ────────────────────────────────────── */
    if (g_strcmp0(interface_name, "org.nano.Device.Orientation") == 0) {
        if (g_strcmp0(method_name, "GetOrientation") == 0) {
            const gchar *orient;
            if (locked_orientation) {
                orient = locked_orientation;
            } else {
                orient = read_accelerometer_orientation();
            }
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(s)", orient));
        } else if (g_strcmp0(method_name, "LockOrientation") == 0) {
            const gchar *orient;
            g_variant_get(parameters, "(&s)", &orient);
            g_free(locked_orientation);
            locked_orientation = g_strdup(orient);
            auto_rotate = FALSE;
            g_dbus_method_invocation_return_value(invocation, NULL);
        } else if (g_strcmp0(method_name, "UnlockOrientation") == 0) {
            g_free(locked_orientation);
            locked_orientation = NULL;
            auto_rotate = TRUE;
            g_dbus_method_invocation_return_value(invocation, NULL);
        }
        return;
    }

    /* ── Sensors ────────────────────────────────────────── */
    if (g_strcmp0(interface_name, "org.nano.Device.Sensors") == 0) {
        if (g_strcmp0(method_name, "GetAmbientLight") == 0) {
            gdouble lux = sensor_get_ambient_light();
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(d)", lux));
        } else if (g_strcmp0(method_name, "GetAccelerometer") == 0) {
            /* Read raw accelerometer values */
            gdouble x = 0.0, y = 9.8, z = 0.0; /* QEMU fallback: gravity down */
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new("((ddd))", x, y, z));
        } else if (g_strcmp0(method_name, "GetProximity") == 0) {
            gboolean near = sensor_get_proximity();
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(b)", near));
        }
        return;
    }
}

/* ── D-Bus property handler ───────────────────────────────────── */

static GVariant *
handle_get_property(GDBusConnection *connection,
                    const gchar     *sender,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *property_name,
                    GError         **error,
                    gpointer         user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)user_data;

    if (g_strcmp0(interface_name, "org.nano.Device.Orientation") == 0 &&
        g_strcmp0(property_name, "AutoRotate") == 0) {
        return g_variant_new_boolean(auto_rotate);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "Unknown property: %s.%s", interface_name, property_name);
    return NULL;
}

static gboolean
handle_set_property(GDBusConnection *connection,
                    const gchar     *sender,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *property_name,
                    GVariant        *value,
                    GError         **error,
                    gpointer         user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)user_data;

    if (g_strcmp0(interface_name, "org.nano.Device.Orientation") == 0 &&
        g_strcmp0(property_name, "AutoRotate") == 0) {
        auto_rotate = g_variant_get_boolean(value);
        if (auto_rotate) {
            g_free(locked_orientation);
            locked_orientation = NULL;
        }
        return TRUE;
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "Unknown property: %s.%s", interface_name, property_name);
    return FALSE;
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call  = handle_method_call,
    .get_property = handle_get_property,
    .set_property = handle_set_property,
};

/* ── Orientation polling ──────────────────────────────────────── */

static gboolean
poll_orientation(gpointer user_data)
{
    (void)user_data;

    if (!auto_rotate || locked_orientation)
        return G_SOURCE_CONTINUE;

    const gchar *new_orient = read_accelerometer_orientation();
    if (g_strcmp0(new_orient, current_orientation) != 0) {
        g_free(current_orientation);
        current_orientation = g_strdup(new_orient);

        /* Emit OrientationChanged signal */
        if (dbus_conn) {
            GError *err = NULL;
            g_dbus_connection_emit_signal(dbus_conn, NULL, OBJ_PATH,
                "org.nano.Device.Orientation", "OrientationChanged",
                g_variant_new("(s)", current_orientation), &err);
            g_clear_error(&err);
        }
        g_debug("Orientation changed to: %s", current_orientation);
    }

    return G_SOURCE_CONTINUE;
}

/* ── D-Bus bus acquired ───────────────────────────────────────── */

static void
on_bus_acquired(GDBusConnection *connection,
                const gchar     *name,
                gpointer         user_data)
{
    (void)name;
    (void)user_data;

    dbus_conn = connection;

    GError *err = NULL;
    gchar *xml = NULL;
    gsize xml_len = 0;

    /* Load introspection XML from installed location */
    if (!g_file_get_contents("/usr/share/dbus-1/interfaces/org.nano.Device.xml",
                             &xml, &xml_len, &err)) {
        g_warning("Cannot load introspection XML: %s", err->message);
        g_clear_error(&err);
        return;
    }

    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(xml, &err);
    g_free(xml);
    if (!node_info) {
        g_warning("Failed to parse introspection XML: %s", err->message);
        g_clear_error(&err);
        return;
    }

    /* Register each interface */
    for (guint i = 0; node_info->interfaces[i] != NULL; i++) {
        g_dbus_connection_register_object(
            connection,
            OBJ_PATH,
            node_info->interfaces[i],
            &interface_vtable,
            NULL, NULL, &err);

        if (err) {
            g_warning("Failed to register %s: %s",
                      node_info->interfaces[i]->name, err->message);
            g_clear_error(&err);
        } else {
            g_debug("Registered interface: %s", node_info->interfaces[i]->name);
        }
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
    g_message("Acquired D-Bus name: %s", name);
}

static void
on_name_lost(GDBusConnection *connection,
             const gchar     *name,
             gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_warning("Lost D-Bus name: %s", name);
    if (main_loop)
        g_main_loop_quit(main_loop);
}

/* ── Signal handling ──────────────────────────────────────────── */

static void
on_signal(int sig)
{
    (void)sig;
    if (main_loop)
        g_main_loop_quit(main_loop);
}

/* ── Main ─────────────────────────────────────────────────────── */

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    /* Initialize orientation state */
    current_orientation = g_strdup("portrait");

    owner_id = g_bus_own_name(
        G_BUS_TYPE_SYSTEM,
        BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        NULL, NULL);

    /* Poll orientation every 2 seconds */
    g_timeout_add_seconds(2, poll_orientation, NULL);

    main_loop = g_main_loop_new(NULL, FALSE);
    g_message("nano-device-daemon starting...");
    g_main_loop_run(main_loop);

    g_bus_unown_name(owner_id);
    g_main_loop_unref(main_loop);
    g_free(current_orientation);
    g_free(locked_orientation);

    return 0;
}
