#include <gtk/gtk.h>
#include "shell.h"

#ifdef HAVE_LIBADWAITA
#include <adwaita.h>
#endif

static NanoShell *g_shell = NULL;

static void
load_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider,
                                        "/org/nano/shell/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    load_css();
    g_shell = nano_shell_new(app);
}

static void
on_shutdown(GtkApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;

    if (g_shell) {
        nano_shell_destroy(g_shell);
        g_shell = NULL;
    }
}

int
main(int argc, char *argv[])
{
#ifdef HAVE_LIBADWAITA
    AdwApplication *app = adw_application_new("org.nano.shell",
                                               G_APPLICATION_NON_UNIQUE);
#else
    GtkApplication *app = gtk_application_new("org.nano.shell",
                                               G_APPLICATION_NON_UNIQUE);
#endif

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
