#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef HAVE_LIBADWAITA
#include <adwaita.h>
#endif

#define APP_ID "org.nano.calculator"
#define MAX_EXPR 256

typedef struct {
    GtkApplication *app;
    GtkWidget      *window;
    GtkWidget      *expression_label;
    GtkWidget      *result_label;

    char   expression[MAX_EXPR];
    double accumulator;
    double operand;
    char   pending_op;
    gboolean clear_on_next;
    gboolean has_decimal;
} CalcApp;

static CalcApp calc = { 0 };

static void update_display(void) {
    gtk_label_set_text(GTK_LABEL(calc.expression_label), calc.expression);
}

static void set_result(double val) {
    char buf[64];
    if (val == (long long)val && fabs(val) < 1e15) {
        snprintf(buf, sizeof(buf), "%lld", (long long)val);
    } else {
        snprintf(buf, sizeof(buf), "%.8g", val);
    }
    gtk_label_set_text(GTK_LABEL(calc.result_label), buf);
}

static double parse_display_number(void) {
    const char *text = gtk_label_get_text(GTK_LABEL(calc.result_label));
    return atof(text);
}

static void do_operation(void) {
    double current = parse_display_number();

    switch (calc.pending_op) {
    case '+': calc.accumulator += current; break;
    case '-': calc.accumulator -= current; break;
    case '*': calc.accumulator *= current; break;
    case '/':
        if (current != 0.0)
            calc.accumulator /= current;
        break;
    default:
        calc.accumulator = current;
        break;
    }
}

static void on_number(GtkButton *button, gpointer user_data) {
    (void)user_data;
    const char *label = gtk_button_get_label(button);

    if (calc.clear_on_next) {
        gtk_label_set_text(GTK_LABEL(calc.result_label), "0");
        calc.clear_on_next = FALSE;
        calc.has_decimal = FALSE;
    }

    const char *current = gtk_label_get_text(GTK_LABEL(calc.result_label));
    char buf[64];

    if (strcmp(current, "0") == 0 && strcmp(label, ".") != 0) {
        snprintf(buf, sizeof(buf), "%s", label);
    } else {
        snprintf(buf, sizeof(buf), "%s%s", current, label);
    }

    gtk_label_set_text(GTK_LABEL(calc.result_label), buf);

    size_t len = strlen(calc.expression);
    if (len < MAX_EXPR - 2) {
        strcat(calc.expression, label);
        update_display();
    }
}

static void on_decimal(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    if (calc.has_decimal) return;
    calc.has_decimal = TRUE;

    if (calc.clear_on_next) {
        gtk_label_set_text(GTK_LABEL(calc.result_label), "0.");
        calc.clear_on_next = FALSE;
        size_t len = strlen(calc.expression);
        if (len < MAX_EXPR - 3) strcat(calc.expression, "0.");
    } else {
        const char *current = gtk_label_get_text(GTK_LABEL(calc.result_label));
        char buf[64];
        snprintf(buf, sizeof(buf), "%s.", current);
        gtk_label_set_text(GTK_LABEL(calc.result_label), buf);
        size_t len = strlen(calc.expression);
        if (len < MAX_EXPR - 2) strcat(calc.expression, ".");
    }
    update_display();
}

static void on_operator(GtkButton *button, gpointer user_data) {
    (void)user_data;
    const char *label = gtk_button_get_label(button);
    char op = label[0];

    if (op == '\xc3') op = '/'; /* UTF-8 division sign fallback */

    do_operation();
    set_result(calc.accumulator);
    calc.pending_op = op;
    calc.clear_on_next = TRUE;
    calc.has_decimal = FALSE;

    size_t len = strlen(calc.expression);
    if (len < MAX_EXPR - 4) {
        char opstr[4];
        snprintf(opstr, sizeof(opstr), " %c ", op);
        strcat(calc.expression, opstr);
        update_display();
    }
}

static void on_equals(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    do_operation();
    set_result(calc.accumulator);

    calc.pending_op = 0;
    calc.clear_on_next = TRUE;
    calc.has_decimal = FALSE;
    calc.expression[0] = '\0';
    update_display();
}

static void on_clear(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    calc.accumulator = 0;
    calc.operand = 0;
    calc.pending_op = 0;
    calc.clear_on_next = FALSE;
    calc.has_decimal = FALSE;
    calc.expression[0] = '\0';

    gtk_label_set_text(GTK_LABEL(calc.result_label), "0");
    update_display();
}

static void on_clear_entry(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    calc.clear_on_next = FALSE;
    calc.has_decimal = FALSE;
    gtk_label_set_text(GTK_LABEL(calc.result_label), "0");
}

static void on_negate(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    double val = parse_display_number();
    val = -val;
    set_result(val);
}

static void on_percent(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    double val = parse_display_number();
    val = val / 100.0;
    set_result(val);
}

static GtkWidget *make_button(const char *label, const char *css_class,
                               GCallback callback) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(btn, "calc-button");
    gtk_widget_add_css_class(btn, css_class);
    gtk_widget_set_hexpand(btn, TRUE);
    gtk_widget_set_vexpand(btn, TRUE);
    g_signal_connect(btn, "clicked", callback, NULL);
    return btn;
}

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *paths[] = {
        "/usr/share/nano-calculator/style.css",
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

    calc.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(calc.window), "Calculator");
    gtk_window_set_default_size(GTK_WINDOW(calc.window), 360, 640);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(calc.window), main_box);

    /* Display area */
    GtkWidget *display = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(display, "display-area");
    gtk_widget_set_valign(display, GTK_ALIGN_END);

    calc.expression_label = gtk_label_new("");
    gtk_widget_add_css_class(calc.expression_label, "expression-label");
    gtk_label_set_xalign(GTK_LABEL(calc.expression_label), 1.0);
    gtk_label_set_ellipsize(GTK_LABEL(calc.expression_label), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(display), calc.expression_label);

    calc.result_label = gtk_label_new("0");
    gtk_widget_add_css_class(calc.result_label, "result-label");
    gtk_label_set_xalign(GTK_LABEL(calc.result_label), 1.0);
    gtk_box_append(GTK_BOX(display), calc.result_label);

    gtk_box_append(GTK_BOX(main_box), display);

    /* Button grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "calc-grid");
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_widget_set_vexpand(grid, TRUE);

    /* Row 0: C CE +/- % */
    gtk_grid_attach(GTK_GRID(grid),
        make_button("C", "function", G_CALLBACK(on_clear)), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("CE", "function", G_CALLBACK(on_clear_entry)), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("+/-", "function", G_CALLBACK(on_negate)), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("%", "function", G_CALLBACK(on_percent)), 3, 0, 1, 1);

    /* Row 1: 7 8 9 / */
    gtk_grid_attach(GTK_GRID(grid),
        make_button("7", "number", G_CALLBACK(on_number)), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("8", "number", G_CALLBACK(on_number)), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("9", "number", G_CALLBACK(on_number)), 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("/", "operator", G_CALLBACK(on_operator)), 3, 1, 1, 1);

    /* Row 2: 4 5 6 * */
    gtk_grid_attach(GTK_GRID(grid),
        make_button("4", "number", G_CALLBACK(on_number)), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("5", "number", G_CALLBACK(on_number)), 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("6", "number", G_CALLBACK(on_number)), 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("*", "operator", G_CALLBACK(on_operator)), 3, 2, 1, 1);

    /* Row 3: 1 2 3 - */
    gtk_grid_attach(GTK_GRID(grid),
        make_button("1", "number", G_CALLBACK(on_number)), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("2", "number", G_CALLBACK(on_number)), 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("3", "number", G_CALLBACK(on_number)), 2, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("-", "operator", G_CALLBACK(on_operator)), 3, 3, 1, 1);

    /* Row 4: 0 . = + */
    gtk_grid_attach(GTK_GRID(grid),
        make_button("0", "number", G_CALLBACK(on_number)), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button(".", "number", G_CALLBACK(on_decimal)), 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("=", "equals", G_CALLBACK(on_equals)), 2, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        make_button("+", "operator", G_CALLBACK(on_operator)), 3, 4, 1, 1);

    gtk_box_append(GTK_BOX(main_box), grid);

    gtk_window_present(GTK_WINDOW(calc.window));
}

int main(int argc, char *argv[]) {
#ifdef HAVE_LIBADWAITA
    adw_init();
#endif

    calc.app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(calc.app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(calc.app), argc, argv);
    g_object_unref(calc.app);
    return status;
}
