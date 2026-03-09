#include <gtk/gtk.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_LIBADWAITA
#include <adwaita.h>
#endif

#define APP_ID "org.nano.clock"

/* ── Application state ── */

typedef struct {
    GtkApplication *app;
    GtkWidget      *window;
    GtkWidget      *stack;

    /* Clock */
    GtkWidget *clock_label;
    GtkWidget *date_label;
    guint      clock_timer_id;

    /* Stopwatch */
    GtkWidget *stopwatch_label;
    gboolean   stopwatch_running;
    gint64     stopwatch_start_time;
    gint64     stopwatch_elapsed;   /* microseconds accumulated */
    guint      stopwatch_timer_id;

    /* Timer */
    GtkWidget *timer_label;
    GtkWidget *timer_spin;
    GtkWidget *timer_start_btn;
    gboolean   timer_running;
    gint       timer_remaining_ms;
    guint      timer_timer_id;
} ClockApp;

static ClockApp app_state = { 0 };

/* ── Clock tab ── */

static gboolean
update_clock (gpointer user_data)
{
    (void)user_data;

    time_t now = time (NULL);
    struct tm *tm = localtime (&now);

    char time_buf[16];
    strftime (time_buf, sizeof (time_buf), "%H:%M:%S", tm);
    gtk_label_set_text (GTK_LABEL (app_state.clock_label), time_buf);

    char date_buf[64];
    strftime (date_buf, sizeof (date_buf), "%A, %B %d, %Y", tm);
    gtk_label_set_text (GTK_LABEL (app_state.date_label), date_buf);

    return G_SOURCE_CONTINUE;
}

static GtkWidget *
create_clock_page (void)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand (box, TRUE);

    app_state.clock_label = gtk_label_new ("00:00:00");
    gtk_widget_add_css_class (app_state.clock_label, "clock-time");
    gtk_box_append (GTK_BOX (box), app_state.clock_label);

    app_state.date_label = gtk_label_new ("");
    gtk_widget_add_css_class (app_state.date_label, "clock-date");
    gtk_box_append (GTK_BOX (box), app_state.date_label);

    update_clock (NULL);
    app_state.clock_timer_id = g_timeout_add (1000, update_clock, NULL);

    return box;
}

/* ── Stopwatch tab ── */

static void
format_stopwatch_time (gint64 elapsed_us, char *buf, gsize buf_len)
{
    gint64 ms = elapsed_us / 1000;
    gint hours = (gint)(ms / 3600000);
    ms %= 3600000;
    gint mins = (gint)(ms / 60000);
    ms %= 60000;
    gint secs = (gint)(ms / 1000);
    gint centis = (gint)((ms % 1000) / 10);

    if (hours > 0)
        snprintf (buf, buf_len, "%02d:%02d:%02d.%02d", hours, mins, secs, centis);
    else
        snprintf (buf, buf_len, "%02d:%02d.%02d", mins, secs, centis);
}

static gboolean
update_stopwatch (gpointer user_data)
{
    (void)user_data;

    if (!app_state.stopwatch_running)
        return G_SOURCE_REMOVE;

    gint64 now = g_get_monotonic_time ();
    gint64 total = app_state.stopwatch_elapsed + (now - app_state.stopwatch_start_time);

    char buf[32];
    format_stopwatch_time (total, buf, sizeof (buf));
    gtk_label_set_text (GTK_LABEL (app_state.stopwatch_label), buf);

    return G_SOURCE_CONTINUE;
}

static void
on_stopwatch_start_stop (GtkButton *button, gpointer user_data)
{
    (void)user_data;

    if (app_state.stopwatch_running) {
        /* Stop */
        app_state.stopwatch_running = FALSE;
        gint64 now = g_get_monotonic_time ();
        app_state.stopwatch_elapsed += (now - app_state.stopwatch_start_time);
        gtk_button_set_label (button, "Start");
    } else {
        /* Start */
        app_state.stopwatch_running = TRUE;
        app_state.stopwatch_start_time = g_get_monotonic_time ();
        gtk_button_set_label (button, "Stop");
        app_state.stopwatch_timer_id = g_timeout_add (33, update_stopwatch, NULL);
    }
}

static void
on_stopwatch_reset (GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    app_state.stopwatch_running = FALSE;
    app_state.stopwatch_elapsed = 0;
    gtk_label_set_text (GTK_LABEL (app_state.stopwatch_label), "00:00.00");
}

static GtkWidget *
create_stopwatch_page (void)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand (box, TRUE);

    app_state.stopwatch_label = gtk_label_new ("00:00.00");
    gtk_widget_add_css_class (app_state.stopwatch_label, "stopwatch-display");
    gtk_box_append (GTK_BOX (box), app_state.stopwatch_label);

    GtkWidget *btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign (btn_box, GTK_ALIGN_CENTER);

    GtkWidget *start_btn = gtk_button_new_with_label ("Start");
    gtk_widget_add_css_class (start_btn, "action-button");
    g_signal_connect (start_btn, "clicked", G_CALLBACK (on_stopwatch_start_stop), NULL);
    gtk_box_append (GTK_BOX (btn_box), start_btn);

    GtkWidget *reset_btn = gtk_button_new_with_label ("Reset");
    gtk_widget_add_css_class (reset_btn, "action-button");
    gtk_widget_add_css_class (reset_btn, "reset");
    g_signal_connect (reset_btn, "clicked", G_CALLBACK (on_stopwatch_reset), NULL);
    gtk_box_append (GTK_BOX (btn_box), reset_btn);

    gtk_box_append (GTK_BOX (box), btn_box);

    return box;
}

/* ── Timer tab ── */

static gboolean
update_timer (gpointer user_data)
{
    (void)user_data;

    if (!app_state.timer_running)
        return G_SOURCE_REMOVE;

    app_state.timer_remaining_ms -= 100;

    if (app_state.timer_remaining_ms <= 0) {
        app_state.timer_remaining_ms = 0;
        app_state.timer_running = FALSE;
        gtk_label_set_text (GTK_LABEL (app_state.timer_label), "00:00");
        gtk_button_set_label (GTK_BUTTON (app_state.timer_start_btn), "Start");
        gtk_widget_set_sensitive (app_state.timer_spin, TRUE);

        /* Alert: flash the display */
        gtk_widget_add_css_class (app_state.timer_label, "destructive");
        g_message ("Timer done!");
        return G_SOURCE_REMOVE;
    }

    gint total_secs = app_state.timer_remaining_ms / 1000;
    gint mins = total_secs / 60;
    gint secs = total_secs % 60;

    char buf[16];
    snprintf (buf, sizeof (buf), "%02d:%02d", mins, secs);
    gtk_label_set_text (GTK_LABEL (app_state.timer_label), buf);

    return G_SOURCE_CONTINUE;
}

static void
on_timer_start_stop (GtkButton *button, gpointer user_data)
{
    (void)user_data;

    if (app_state.timer_running) {
        /* Stop */
        app_state.timer_running = FALSE;
        gtk_button_set_label (button, "Start");
        gtk_widget_set_sensitive (app_state.timer_spin, TRUE);
    } else {
        /* Start */
        gtk_widget_remove_css_class (app_state.timer_label, "destructive");

        if (app_state.timer_remaining_ms <= 0) {
            gint minutes = gtk_spin_button_get_value_as_int (
                GTK_SPIN_BUTTON (app_state.timer_spin));
            if (minutes <= 0) return;
            app_state.timer_remaining_ms = minutes * 60 * 1000;
        }

        app_state.timer_running = TRUE;
        gtk_button_set_label (button, "Stop");
        gtk_widget_set_sensitive (app_state.timer_spin, FALSE);
        app_state.timer_timer_id = g_timeout_add (100, update_timer, NULL);
    }
}

static void
on_timer_reset (GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    app_state.timer_running = FALSE;
    app_state.timer_remaining_ms = 0;
    gtk_label_set_text (GTK_LABEL (app_state.timer_label), "00:00");
    gtk_button_set_label (GTK_BUTTON (app_state.timer_start_btn), "Start");
    gtk_widget_set_sensitive (app_state.timer_spin, TRUE);
    gtk_widget_remove_css_class (app_state.timer_label, "destructive");
}

static GtkWidget *
create_timer_page (void)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand (box, TRUE);

    app_state.timer_label = gtk_label_new ("00:00");
    gtk_widget_add_css_class (app_state.timer_label, "timer-display");
    gtk_box_append (GTK_BOX (box), app_state.timer_label);

    /* Minutes spinner */
    GtkWidget *spin_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign (spin_box, GTK_ALIGN_CENTER);

    GtkWidget *spin_label = gtk_label_new ("Minutes:");
    gtk_widget_add_css_class (spin_label, "timer-input");
    gtk_box_append (GTK_BOX (spin_box), spin_label);

    GtkAdjustment *adj = gtk_adjustment_new (5, 1, 180, 1, 5, 0);
    app_state.timer_spin = gtk_spin_button_new (adj, 1, 0);
    gtk_widget_add_css_class (app_state.timer_spin, "timer-input");
    gtk_box_append (GTK_BOX (spin_box), app_state.timer_spin);

    gtk_box_append (GTK_BOX (box), spin_box);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign (btn_box, GTK_ALIGN_CENTER);

    app_state.timer_start_btn = gtk_button_new_with_label ("Start");
    gtk_widget_add_css_class (app_state.timer_start_btn, "action-button");
    g_signal_connect (app_state.timer_start_btn, "clicked",
                      G_CALLBACK (on_timer_start_stop), NULL);
    gtk_box_append (GTK_BOX (btn_box), app_state.timer_start_btn);

    GtkWidget *reset_btn = gtk_button_new_with_label ("Reset");
    gtk_widget_add_css_class (reset_btn, "action-button");
    gtk_widget_add_css_class (reset_btn, "reset");
    g_signal_connect (reset_btn, "clicked", G_CALLBACK (on_timer_reset), NULL);
    gtk_box_append (GTK_BOX (btn_box), reset_btn);

    gtk_box_append (GTK_BOX (box), btn_box);

    return box;
}

/* ── Tab switching ── */

static void
on_tab_clicked (GtkToggleButton *button, gpointer user_data)
{
    if (!gtk_toggle_button_get_active (button))
        return;

    const gchar *page_name = (const gchar *)user_data;
    gtk_stack_set_visible_child_name (GTK_STACK (app_state.stack), page_name);
}

/* ── CSS loading ── */

static void
load_css (void)
{
    GtkCssProvider *provider = gtk_css_provider_new ();

    /* Try installed path first, then local */
    const gchar *paths[] = {
        "/usr/share/nano-clock/style.css",
        "data/style.css",
        NULL
    };

    for (int i = 0; paths[i] != NULL; i++) {
        if (g_file_test (paths[i], G_FILE_TEST_EXISTS)) {
            gtk_css_provider_load_from_path (provider, paths[i]);
            gtk_style_context_add_provider_for_display (
                gdk_display_get_default (),
                GTK_STYLE_PROVIDER (provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            return;
        }
    }

    g_warning ("Could not find style.css");
    g_object_unref (provider);
}

/* ── App activate ── */

static void
on_activate (GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    load_css ();

    app_state.window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (app_state.window), "Clock");
    gtk_window_set_default_size (GTK_WINDOW (app_state.window), 360, 640);

    GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child (GTK_WINDOW (app_state.window), main_box);

    /* Stack for pages */
    app_state.stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (app_state.stack),
                                   GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand (app_state.stack, TRUE);

    gtk_stack_add_named (GTK_STACK (app_state.stack),
                         create_clock_page (), "clock");
    gtk_stack_add_named (GTK_STACK (app_state.stack),
                         create_stopwatch_page (), "stopwatch");
    gtk_stack_add_named (GTK_STACK (app_state.stack),
                         create_timer_page (), "timer");

    gtk_box_append (GTK_BOX (main_box), app_state.stack);

    /* Bottom tab bar */
    GtkWidget *tab_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign (tab_bar, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand (tab_bar, TRUE);

    GtkWidget *clock_btn = gtk_toggle_button_new_with_label ("Clock");
    GtkWidget *sw_btn = gtk_toggle_button_new_with_label ("Stopwatch");
    GtkWidget *timer_btn = gtk_toggle_button_new_with_label ("Timer");

    gtk_widget_add_css_class (clock_btn, "tab-button");
    gtk_widget_add_css_class (sw_btn, "tab-button");
    gtk_widget_add_css_class (timer_btn, "tab-button");

    gtk_widget_set_hexpand (clock_btn, TRUE);
    gtk_widget_set_hexpand (sw_btn, TRUE);
    gtk_widget_set_hexpand (timer_btn, TRUE);

    /* Group toggle buttons */
    gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (sw_btn),
                                 GTK_TOGGLE_BUTTON (clock_btn));
    gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (timer_btn),
                                 GTK_TOGGLE_BUTTON (clock_btn));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (clock_btn), TRUE);

    g_signal_connect (clock_btn, "toggled", G_CALLBACK (on_tab_clicked), "clock");
    g_signal_connect (sw_btn, "toggled", G_CALLBACK (on_tab_clicked), "stopwatch");
    g_signal_connect (timer_btn, "toggled", G_CALLBACK (on_tab_clicked), "timer");

    gtk_box_append (GTK_BOX (tab_bar), clock_btn);
    gtk_box_append (GTK_BOX (tab_bar), sw_btn);
    gtk_box_append (GTK_BOX (tab_bar), timer_btn);

    gtk_box_append (GTK_BOX (main_box), tab_bar);

    gtk_window_present (GTK_WINDOW (app_state.window));
}

int
main (int argc, char *argv[])
{
#ifdef HAVE_LIBADWAITA
    adw_init ();
#endif

    app_state.app = gtk_application_new (APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect (app_state.app, "activate", G_CALLBACK (on_activate), NULL);

    int status = g_application_run (G_APPLICATION (app_state.app), argc, argv);

    if (app_state.clock_timer_id > 0)
        g_source_remove (app_state.clock_timer_id);

    g_object_unref (app_state.app);
    return status;
}
