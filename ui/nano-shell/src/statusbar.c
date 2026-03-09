#include "statusbar.h"
#include "clock.h"

/* We store the clock label as widget data so we can update it. */
#define CLOCK_LABEL_KEY "nano-statusbar-clock-label"

static gboolean
on_clock_tick(gpointer user_data)
{
    GtkWidget *statusbar = GTK_WIDGET(user_data);
    if (!GTK_IS_WIDGET(statusbar) ||
        !gtk_widget_get_realized(statusbar)) {
        return G_SOURCE_REMOVE;
    }

    nano_statusbar_update_clock(statusbar);
    return G_SOURCE_CONTINUE;
}

static void
on_statusbar_realize(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    /* Initial update */
    nano_statusbar_update_clock(widget);
    /* Tick every 60 seconds */
    g_timeout_add_seconds(60, on_clock_tick, widget);
}

GtkWidget *
nano_statusbar_new(void)
{
    GtkWidget *bar = gtk_center_box_new();
    gtk_widget_add_css_class(bar, "status-bar");
    gtk_widget_set_hexpand(bar, TRUE);
    gtk_widget_set_valign(bar, GTK_ALIGN_START);

    /* Left: clock */
    GtkWidget *clock_label = gtk_label_new("00:00");
    gtk_widget_add_css_class(clock_label, "status-clock");
    gtk_widget_set_halign(clock_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(clock_label, 8);
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(bar), clock_label);

    g_object_set_data(G_OBJECT(bar), CLOCK_LABEL_KEY, clock_label);

    /* Center: NanoOS branding */
    GtkWidget *center_label = gtk_label_new("NanoOS");
    gtk_widget_set_halign(center_label, GTK_ALIGN_CENTER);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(bar), center_label);

    /* Right: battery + wifi indicators */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(right_box, GTK_ALIGN_END);
    gtk_widget_set_margin_end(right_box, 8);

    GtkWidget *wifi_label = gtk_label_new("WiFi");
    gtk_widget_add_css_class(wifi_label, "status-indicator");
    gtk_box_append(GTK_BOX(right_box), wifi_label);

    GtkWidget *battery_label = gtk_label_new("100%");
    gtk_widget_add_css_class(battery_label, "status-indicator");
    gtk_box_append(GTK_BOX(right_box), battery_label);

    gtk_center_box_set_end_widget(GTK_CENTER_BOX(bar), right_box);

    g_signal_connect(bar, "realize", G_CALLBACK(on_statusbar_realize), NULL);

    return bar;
}

void
nano_statusbar_update_clock(GtkWidget *statusbar)
{
    GtkWidget *clock_label = g_object_get_data(G_OBJECT(statusbar),
                                                CLOCK_LABEL_KEY);
    if (clock_label && GTK_IS_LABEL(clock_label)) {
        gchar *time_str = nano_clock_get_time_string();
        gtk_label_set_text(GTK_LABEL(clock_label), time_str);
        g_free(time_str);
    }
}
