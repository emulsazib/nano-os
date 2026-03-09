#include "app-entry.h"
#ifdef __linux__
#include <gio/gdesktopappinfo.h>
#endif

struct _NanoAppEntry {
    GtkBox   parent_instance;

    GAppInfo *app_info;     /* may be NULL if using exec_cmd */
    gchar    *exec_cmd;     /* fallback exec command */
    gchar    *display_name;

    GtkWidget *icon_widget;
    GtkWidget *label_widget;
};

G_DEFINE_FINAL_TYPE(NanoAppEntry, nano_app_entry, GTK_TYPE_BOX)

/* ── Internal helpers ──────────────────────────────────────── */

static guint
name_to_color_index(const gchar *name)
{
    guint hash = 0;
    if (name) {
        for (const gchar *p = name; *p; p++) {
            hash = hash * 31 + (guint)*p;
        }
    }
    return hash % 8;
}

static GtkWidget *
create_fallback_icon(const gchar *name)
{
    /* Colored circle with first letter */
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(frame, GTK_ALIGN_CENTER);

    gchar letter[8] = {0};
    if (name && name[0]) {
        /* Get first UTF-8 character */
        gunichar ch = g_utf8_get_char(name);
        ch = g_unichar_toupper(ch);
        gint len = g_unichar_to_utf8(ch, letter);
        letter[len] = '\0';
    } else {
        letter[0] = '?';
    }

    GtkWidget *label = gtk_label_new(letter);
    gtk_widget_add_css_class(label, "app-icon-fallback");

    gchar class_name[32];
    g_snprintf(class_name, sizeof(class_name), "app-icon-color-%u",
               name_to_color_index(name));
    gtk_widget_add_css_class(label, class_name);

    gtk_frame_set_child(GTK_FRAME(frame), label);
    gtk_widget_add_css_class(frame, "app-icon-fallback");
    gtk_widget_add_css_class(frame, class_name);

    /* Remove the frame's default border */
    gtk_frame_set_label(GTK_FRAME(frame), NULL);

    return frame;
}

static GtkWidget *
create_icon_widget(const gchar *icon_name, const gchar *display_name)
{
    if (icon_name && icon_name[0]) {
        GtkIconTheme *theme = gtk_icon_theme_get_for_display(
            gdk_display_get_default());

        if (gtk_icon_theme_has_icon(theme, icon_name)) {
            GtkWidget *image = gtk_image_new_from_icon_name(icon_name);
            gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
            gtk_widget_add_css_class(image, "app-icon");
            gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
            return image;
        }
    }
    return create_fallback_icon(display_name);
}

static void
on_click_pressed(GtkGestureClick *gesture,
                 gint             n_press,
                 gdouble          x,
                 gdouble          y,
                 gpointer         user_data)
{
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;

    NanoAppEntry *self = NANO_APP_ENTRY(user_data);
    nano_app_entry_launch(self);
}

/* ── GObject lifecycle ─────────────────────────────────────── */

static void
nano_app_entry_dispose(GObject *object)
{
    NanoAppEntry *self = NANO_APP_ENTRY(object);

    g_clear_object(&self->app_info);
    g_clear_pointer(&self->exec_cmd, g_free);
    g_clear_pointer(&self->display_name, g_free);

    G_OBJECT_CLASS(nano_app_entry_parent_class)->dispose(object);
}

static void
nano_app_entry_class_init(NanoAppEntryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = nano_app_entry_dispose;
}

static void
nano_app_entry_init(NanoAppEntry *self)
{
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self),
                                  GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_START);
    gtk_box_set_spacing(GTK_BOX(self), 4);
    gtk_widget_add_css_class(GTK_WIDGET(self), "app-entry");

    /* Touch gesture */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self),
                              GTK_EVENT_CONTROLLER(click));
}

/* ── Public constructors ───────────────────────────────────── */

GtkWidget *
nano_app_entry_new_from_app_info(GAppInfo *app_info)
{
    g_return_val_if_fail(G_IS_APP_INFO(app_info), NULL);

    NanoAppEntry *self = g_object_new(NANO_TYPE_APP_ENTRY, NULL);

    self->app_info = g_object_ref(app_info);
    self->display_name = g_strdup(g_app_info_get_display_name(app_info));

    /* Icon */
    GIcon *gicon = g_app_info_get_icon(app_info);
    const gchar *icon_name = NULL;
    if (gicon && G_IS_THEMED_ICON(gicon)) {
        const gchar * const *names = g_themed_icon_get_names(
            G_THEMED_ICON(gicon));
        if (names && names[0]) {
            icon_name = names[0];
        }
    }

    self->icon_widget = create_icon_widget(icon_name, self->display_name);
    gtk_box_append(GTK_BOX(self), self->icon_widget);

    /* Label */
    self->label_widget = gtk_label_new(self->display_name);
    gtk_label_set_max_width_chars(GTK_LABEL(self->label_widget), 10);
    gtk_label_set_ellipsize(GTK_LABEL(self->label_widget),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(self->label_widget, "app-label");
    gtk_widget_set_halign(self->label_widget, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(self), self->label_widget);

    return GTK_WIDGET(self);
}

GtkWidget *
nano_app_entry_new(const gchar *name,
                   const gchar *icon_name,
                   const gchar *exec_cmd)
{
    NanoAppEntry *self = g_object_new(NANO_TYPE_APP_ENTRY, NULL);

    self->display_name = g_strdup(name ? name : "App");
    self->exec_cmd = g_strdup(exec_cmd);

    /* Icon */
    self->icon_widget = create_icon_widget(icon_name, self->display_name);
    gtk_box_append(GTK_BOX(self), self->icon_widget);

    /* Label */
    self->label_widget = gtk_label_new(self->display_name);
    gtk_label_set_max_width_chars(GTK_LABEL(self->label_widget), 10);
    gtk_label_set_ellipsize(GTK_LABEL(self->label_widget),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(self->label_widget, "app-label");
    gtk_widget_set_halign(self->label_widget, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(self), self->label_widget);

    return GTK_WIDGET(self);
}

/* ── Launch ────────────────────────────────────────────────── */

void
nano_app_entry_launch(NanoAppEntry *self)
{
    g_return_if_fail(NANO_IS_APP_ENTRY(self));

    if (self->app_info) {
        GError *error = NULL;
        if (!g_app_info_launch(self->app_info, NULL, NULL, &error)) {
            g_warning("Failed to launch %s: %s",
                      self->display_name,
                      error ? error->message : "unknown error");
            g_clear_error(&error);
        }
        return;
    }

    if (self->exec_cmd && self->exec_cmd[0]) {
        GError *error = NULL;
        if (!g_spawn_command_line_async(self->exec_cmd, &error)) {
            g_warning("Failed to exec '%s': %s",
                      self->exec_cmd,
                      error ? error->message : "unknown error");
            g_clear_error(&error);
        }
    }
}
