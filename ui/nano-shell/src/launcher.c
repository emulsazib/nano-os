#include "launcher.h"
#include "app-entry.h"
#include "clock.h"
#ifdef __linux__
#include <gio/gdesktopappinfo.h>
#endif

#define LAUNCHER_COLUMNS 4

/* ── Clock header widget ───────────────────────────────────── */

#define CLOCK_LABEL_KEY "nano-launcher-clock"
#define DATE_LABEL_KEY  "nano-launcher-date"

static gboolean
on_clock_update(gpointer user_data)
{
    GtkWidget *header = GTK_WIDGET(user_data);
    if (!GTK_IS_WIDGET(header) ||
        !gtk_widget_get_realized(header)) {
        return G_SOURCE_REMOVE;
    }

    GtkWidget *clock_label = g_object_get_data(G_OBJECT(header),
                                                CLOCK_LABEL_KEY);
    GtkWidget *date_label = g_object_get_data(G_OBJECT(header),
                                               DATE_LABEL_KEY);
    if (clock_label) {
        gchar *t = nano_clock_get_time_string();
        gtk_label_set_text(GTK_LABEL(clock_label), t);
        g_free(t);
    }
    if (date_label) {
        gchar *d = nano_clock_get_date_string();
        gtk_label_set_text(GTK_LABEL(date_label), d);
        g_free(d);
    }
    return G_SOURCE_CONTINUE;
}

static GtkWidget *
create_clock_header(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(box, 8);

    /* Large time */
    gchar *time_str = nano_clock_get_time_string();
    GtkWidget *clock_label = gtk_label_new(time_str);
    g_free(time_str);
    gtk_widget_add_css_class(clock_label, "clock-large");
    gtk_widget_set_halign(clock_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), clock_label);

    /* Date */
    gchar *date_str = nano_clock_get_date_string();
    GtkWidget *date_label = gtk_label_new(date_str);
    g_free(date_str);
    gtk_widget_add_css_class(date_label, "clock-date");
    gtk_widget_set_halign(date_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), date_label);

    g_object_set_data(G_OBJECT(box), CLOCK_LABEL_KEY, clock_label);
    g_object_set_data(G_OBJECT(box), DATE_LABEL_KEY, date_label);

    /* Update every second for responsiveness */
    g_timeout_add_seconds(1, on_clock_update, box);

    return box;
}

/* ── Built-in fallback apps ────────────────────────────────── */

typedef struct {
    const char *name;
    const char *icon;
    const char *exec;
} BuiltinApp;

static const BuiltinApp builtin_apps[] = {
    { "Terminal",   "utilities-terminal", "xterm"            },
    { "Settings",   "preferences-system", "gnome-control-center" },
    { "Clock",      "preferences-system-time", "gnome-clocks" },
    { "Files",      "system-file-manager",     "nautilus"     },
    { "Calculator", "accessories-calculator",  "gnome-calculator" },
    { "Browser",    "web-browser",             "firefox"      },
    { "Camera",     "camera-photo",            "cheese"       },
    { "Music",      "multimedia-audio-player", "gnome-music"  },
};

static void
add_builtin_apps(GtkFlowBox *grid)
{
    for (gsize i = 0; i < G_N_ELEMENTS(builtin_apps); i++) {
        GtkWidget *entry = nano_app_entry_new(builtin_apps[i].name,
                                               builtin_apps[i].icon,
                                               builtin_apps[i].exec);
        gtk_flow_box_append(grid, entry);
    }
}

/* ── Desktop file scanning ─────────────────────────────────── */

static gboolean
populate_from_desktop_files(GtkFlowBox *grid)
{
#ifdef __linux__
    GList *apps = g_app_info_get_all();
    gboolean found_any = FALSE;

    for (GList *l = apps; l != NULL; l = l->next) {
        GAppInfo *info = G_APP_INFO(l->data);

        if (!g_app_info_should_show(info)) {
            continue;
        }

        GtkWidget *entry = nano_app_entry_new_from_app_info(info);
        if (entry) {
            gtk_flow_box_append(grid, entry);
            found_any = TRUE;
        }
    }

    g_list_free_full(apps, g_object_unref);
    return found_any;
#else
    (void)grid;
    return FALSE;
#endif
}

/* ── Public ────────────────────────────────────────────────── */

GtkWidget *
nano_launcher_new(void)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(outer, "launcher");
    gtk_widget_set_vexpand(outer, TRUE);
    gtk_widget_set_hexpand(outer, TRUE);

    /* Clock header */
    GtkWidget *clock_header = create_clock_header();
    gtk_box_append(GTK_BOX(outer), clock_header);

    /* Scrolled area for app grid */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    /* FlowBox grid */
    GtkWidget *grid = gtk_flow_box_new();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid),
                                            LAUNCHER_COLUMNS);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid),
                                            LAUNCHER_COLUMNS);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid), TRUE);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(grid),
                                    GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid), 12);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid), 4);
    gtk_widget_add_css_class(grid, "launcher-grid");

    /* Try to load real .desktop apps; fall back to built-ins */
    if (!populate_from_desktop_files(GTK_FLOW_BOX(grid))) {
        add_builtin_apps(GTK_FLOW_BOX(grid));
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_box_append(GTK_BOX(outer), scroll);

    return outer;
}
