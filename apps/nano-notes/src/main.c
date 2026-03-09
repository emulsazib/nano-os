#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_LIBADWAITA
#include <adwaita.h>
#endif

#define APP_ID "org.nano.notes"
#define NOTES_DIR_NAME "nano-notes"

typedef struct {
    gchar *filename;
    gchar *title;
    gchar *preview;
    gchar *date_str;
} NoteEntry;

typedef struct {
    GtkApplication *app;
    GtkWidget      *window;
    GtkWidget      *stack;

    /* List view */
    GtkWidget *list_box;
    GtkWidget *empty_label;

    /* Editor view */
    GtkWidget *editor_header;
    GtkWidget *text_view;
    GtkTextBuffer *text_buffer;
    gchar     *editing_filename;

    gchar *notes_dir;
} NotesApp;

static NotesApp notes = { 0 };

static void refresh_note_list(void);
static void show_editor(const gchar *filename);

static gchar *
get_notes_dir(void)
{
    const gchar *data_home = g_get_user_data_dir();
    gchar *dir = g_build_filename(data_home, NOTES_DIR_NAME, NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static void
note_entry_free(NoteEntry *entry)
{
    g_free(entry->filename);
    g_free(entry->title);
    g_free(entry->preview);
    g_free(entry->date_str);
    g_free(entry);
}

static NoteEntry *
load_note_entry(const gchar *dir, const gchar *name)
{
    gchar *path = g_build_filename(dir, name, NULL);
    gchar *contents = NULL;
    gsize length = 0;

    if (!g_file_get_contents(path, &contents, &length, NULL)) {
        g_free(path);
        return NULL;
    }

    NoteEntry *entry = g_new0(NoteEntry, 1);
    entry->filename = g_strdup(name);

    /* First line is title */
    gchar *newline = strchr(contents, '\n');
    if (newline) {
        entry->title = g_strndup(contents, (gsize)(newline - contents));
        /* Next 80 chars are preview */
        const gchar *rest = newline + 1;
        gsize rest_len = strlen(rest);
        if (rest_len > 80) rest_len = 80;
        entry->preview = g_strndup(rest, rest_len);
        /* Strip newlines from preview */
        for (gchar *p = entry->preview; *p; p++) {
            if (*p == '\n') *p = ' ';
        }
    } else {
        entry->title = g_strndup(contents, length > 40 ? 40 : length);
        entry->preview = g_strdup("");
    }

    if (entry->title[0] == '\0') {
        g_free(entry->title);
        entry->title = g_strdup("Untitled");
    }

    /* Parse date from filename (timestamp) */
    long ts = atol(name);
    if (ts > 0) {
        GDateTime *dt = g_date_time_new_from_unix_local(ts);
        if (dt) {
            entry->date_str = g_date_time_format(dt, "%b %d, %Y");
            g_date_time_unref(dt);
        } else {
            entry->date_str = g_strdup("");
        }
    } else {
        entry->date_str = g_strdup("");
    }

    g_free(contents);
    g_free(path);
    return entry;
}

static GtkWidget *
create_note_row(NoteEntry *entry)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);

    GtkWidget *title = gtk_label_new(entry->title);
    gtk_widget_add_css_class(title, "note-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), title);

    if (entry->preview && entry->preview[0]) {
        GtkWidget *preview = gtk_label_new(entry->preview);
        gtk_widget_add_css_class(preview, "note-preview");
        gtk_label_set_xalign(GTK_LABEL(preview), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(preview), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(preview), 50);
        gtk_box_append(GTK_BOX(box), preview);
    }

    if (entry->date_str && entry->date_str[0]) {
        GtkWidget *date = gtk_label_new(entry->date_str);
        gtk_widget_add_css_class(date, "note-date");
        gtk_label_set_xalign(GTK_LABEL(date), 0.0);
        gtk_box_append(GTK_BOX(box), date);
    }

    return box;
}

static void
on_note_selected(GtkListBox *list_box, GtkListBoxRow *row, gpointer user_data)
{
    (void)list_box;
    (void)user_data;

    if (!row) return;

    const gchar *filename = g_object_get_data(G_OBJECT(row), "filename");
    if (filename) {
        show_editor(filename);
    }
}

static void
save_current_note(void)
{
    if (!notes.editing_filename) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(notes.text_buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(notes.text_buffer, &start, &end, FALSE);

    gchar *path = g_build_filename(notes.notes_dir, notes.editing_filename, NULL);
    GError *error = NULL;
    if (!g_file_set_contents(path, text, -1, &error)) {
        g_warning("Failed to save note: %s", error->message);
        g_clear_error(&error);
    }

    g_free(text);
    g_free(path);
}

static void
on_back_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    save_current_note();
    g_clear_pointer(&notes.editing_filename, g_free);

    refresh_note_list();
    gtk_stack_set_visible_child_name(GTK_STACK(notes.stack), "list");
}

static void
on_delete_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    if (!notes.editing_filename) return;

    gchar *path = g_build_filename(notes.notes_dir, notes.editing_filename, NULL);
    g_remove(path);
    g_free(path);

    g_clear_pointer(&notes.editing_filename, g_free);
    refresh_note_list();
    gtk_stack_set_visible_child_name(GTK_STACK(notes.stack), "list");
}

static void
show_editor(const gchar *filename)
{
    g_free(notes.editing_filename);
    notes.editing_filename = g_strdup(filename);

    gchar *path = g_build_filename(notes.notes_dir, filename, NULL);
    gchar *contents = NULL;

    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        gtk_text_buffer_set_text(notes.text_buffer, contents, -1);
        g_free(contents);
    } else {
        gtk_text_buffer_set_text(notes.text_buffer, "", -1);
    }

    g_free(path);
    gtk_stack_set_visible_child_name(GTK_STACK(notes.stack), "editor");
    gtk_widget_grab_focus(notes.text_view);
}

static void
on_new_note(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    gchar *filename = g_strdup_printf("%lld.txt", (long long)now);

    gchar *path = g_build_filename(notes.notes_dir, filename, NULL);
    g_file_set_contents(path, "", -1, NULL);
    g_free(path);

    show_editor(filename);
    g_free(filename);
}

static void
refresh_note_list(void)
{
    /* Remove all existing rows */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(notes.list_box)))) {
        gtk_list_box_remove(GTK_LIST_BOX(notes.list_box), child);
    }

    GDir *dir = g_dir_open(notes.notes_dir, 0, NULL);
    if (!dir) return;

    GPtrArray *entries = g_ptr_array_new_with_free_func(
        (GDestroyNotify)note_entry_free);

    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        NoteEntry *entry = load_note_entry(notes.notes_dir, name);
        if (entry) {
            g_ptr_array_add(entries, entry);
        }
    }
    g_dir_close(dir);

    /* Sort by filename (timestamp) descending - newest first */
    for (guint i = 0; i < entries->len; i++) {
        for (guint j = i + 1; j < entries->len; j++) {
            NoteEntry *a = g_ptr_array_index(entries, i);
            NoteEntry *b = g_ptr_array_index(entries, j);
            if (g_strcmp0(a->filename, b->filename) < 0) {
                entries->pdata[i] = b;
                entries->pdata[j] = a;
            }
        }
    }

    if (entries->len == 0) {
        gtk_widget_set_visible(notes.empty_label, TRUE);
    } else {
        gtk_widget_set_visible(notes.empty_label, FALSE);

        for (guint i = 0; i < entries->len; i++) {
            NoteEntry *entry = g_ptr_array_index(entries, i);
            GtkWidget *row_content = create_note_row(entry);
            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_content);
            g_object_set_data_full(G_OBJECT(row), "filename",
                                   g_strdup(entry->filename), g_free);
            gtk_list_box_append(GTK_LIST_BOX(notes.list_box), row);
        }
    }

    g_ptr_array_unref(entries);
}

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *paths[] = {
        "/usr/share/nano-notes/style.css",
        "data/style.css",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        if (g_file_test(paths[i], G_FILE_TEST_EXISTS)) {
            gtk_css_provider_load_from_path(provider, paths[i]);
            gtk_style_context_add_provider_for_display(
                gdk_display_get_default(),
                GTK_STYLE_PROVIDER(provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            return;
        }
    }
    g_object_unref(provider);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    load_css();

    notes.notes_dir = get_notes_dir();
    notes.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(notes.window), "Notes");
    gtk_window_set_default_size(GTK_WINDOW(notes.window), 360, 640);

    notes.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(notes.stack),
                                   GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_window_set_child(GTK_WINDOW(notes.window), notes.stack);

    /* ── List view ── */
    GtkWidget *list_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *list_header = gtk_header_bar_new();
    GtkWidget *title_label = gtk_label_new("Notes");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(list_header), title_label);
    gtk_box_append(GTK_BOX(list_page), list_header);

    /* Overlay for FAB button */
    GtkWidget *list_overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(list_overlay, TRUE);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *list_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    notes.list_box = gtk_list_box_new();
    gtk_widget_add_css_class(notes.list_box, "note-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(notes.list_box),
                                    GTK_SELECTION_NONE);
    g_signal_connect(notes.list_box, "row-activated",
                     G_CALLBACK(on_note_selected), NULL);
    gtk_box_append(GTK_BOX(list_content), notes.list_box);

    notes.empty_label = gtk_label_new("No notes yet.\nTap + to create one.");
    gtk_widget_add_css_class(notes.empty_label, "empty-state");
    gtk_widget_set_valign(notes.empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(notes.empty_label, TRUE);
    gtk_box_append(GTK_BOX(list_content), notes.empty_label);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list_content);
    gtk_overlay_set_child(GTK_OVERLAY(list_overlay), scroll);

    /* Floating action button */
    GtkWidget *fab = gtk_button_new_with_label("+");
    gtk_widget_add_css_class(fab, "new-note-button");
    gtk_widget_set_halign(fab, GTK_ALIGN_END);
    gtk_widget_set_valign(fab, GTK_ALIGN_END);
    gtk_widget_set_margin_end(fab, 16);
    gtk_widget_set_margin_bottom(fab, 16);
    g_signal_connect(fab, "clicked", G_CALLBACK(on_new_note), NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(list_overlay), fab);

    gtk_box_append(GTK_BOX(list_page), list_overlay);
    gtk_stack_add_named(GTK_STACK(notes.stack), list_page, "list");

    /* ── Editor view ── */
    GtkWidget *editor_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(editor_page, "editor-view");

    notes.editor_header = gtk_header_bar_new();

    GtkWidget *back_btn = gtk_button_new_with_label("Back");
    gtk_widget_add_css_class(back_btn, "back-button");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(notes.editor_header), back_btn);

    GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(delete_btn, "delete-button");
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete_clicked), NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(notes.editor_header), delete_btn);

    gtk_box_append(GTK_BOX(editor_page), notes.editor_header);

    GtkWidget *editor_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(editor_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(editor_scroll, TRUE);

    notes.text_view = gtk_text_view_new();
    notes.text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(notes.text_view));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(notes.text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(notes.text_view), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(notes.text_view), 16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(notes.text_view), 12);
    gtk_widget_add_css_class(notes.text_view, "editor-text");

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(editor_scroll),
                                   notes.text_view);
    gtk_box_append(GTK_BOX(editor_page), editor_scroll);

    gtk_stack_add_named(GTK_STACK(notes.stack), editor_page, "editor");

    /* Start on list view */
    refresh_note_list();
    gtk_stack_set_visible_child_name(GTK_STACK(notes.stack), "list");

    gtk_window_present(GTK_WINDOW(notes.window));
}

int main(int argc, char *argv[]) {
#ifdef HAVE_LIBADWAITA
    adw_init();
#endif

    notes.app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(notes.app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(notes.app), argc, argv);

    g_free(notes.notes_dir);
    g_free(notes.editing_filename);
    g_object_unref(notes.app);
    return status;
}
