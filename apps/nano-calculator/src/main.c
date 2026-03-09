/* ═══════════════════════════════════════════════════════════════
 *  NanoOS Calculator
 *  Tabs: Calculator · Unit Converter · Scratchpad · History
 * ═══════════════════════════════════════════════════════════════ */

#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef HAVE_LIBADWAITA
#include <adwaita.h>
#endif

#define APP_ID      "org.nano.calculator"
#define MAX_D       64
#define MAX_EXPR    256
#define MAX_HIST    100
#define MAX_UNITS   10
#define NUM_CATS    6

/* ═══ Unit conversion data ══════════════════════════════════ */

typedef struct { const char *name; const char *sym; double f; } U;

typedef struct {
    const char *name;
    int         n;
    U           u[MAX_UNITS];
    gboolean    temp;
} Cat;

static const Cat cats[NUM_CATS] = {
    { "Area", 7, {
        { "Square meter",      "m²",  1.0 },
        { "Square centimeter", "cm²", 1e-4 },
        { "Square millimeter", "mm²", 1e-6 },
        { "Square kilometer",  "km²", 1e6 },
        { "Square foot",       "ft²", 0.09290304 },
        { "Square inch",       "in²", 6.4516e-4 },
        { "Hectare",           "ha",  10000.0 },
    }, FALSE },
    { "Length", 8, {
        { "Meter",      "m",  1.0 },
        { "Centimeter", "cm", 0.01 },
        { "Millimeter", "mm", 0.001 },
        { "Kilometer",  "km", 1000.0 },
        { "Inch",       "in", 0.0254 },
        { "Foot",       "ft", 0.3048 },
        { "Yard",       "yd", 0.9144 },
        { "Mile",       "mi", 1609.344 },
    }, FALSE },
    { "Temp", 3, {
        { "Celsius",    "°C", 0 },
        { "Fahrenheit", "°F", 0 },
        { "Kelvin",     "K",  0 },
    }, TRUE },
    { "Volume", 7, {
        { "Liter",      "L",    1.0 },
        { "Milliliter", "mL",   0.001 },
        { "Cubic meter","m³",   1000.0 },
        { "Gallon (US)","gal",  3.78541 },
        { "Quart",      "qt",   0.946353 },
        { "Pint",       "pt",   0.473176 },
        { "Fluid ounce","fl oz",0.0295735 },
    }, FALSE },
    { "Mass", 6, {
        { "Gram",       "g",  1.0 },
        { "Milligram",  "mg", 0.001 },
        { "Kilogram",   "kg", 1000.0 },
        { "Ounce",      "oz", 28.3495 },
        { "Pound",      "lb", 453.592 },
        { "Metric ton", "t",  1e6 },
    }, FALSE },
    { "Speed", 4, {
        { "m/s",  "m/s",  1.0 },
        { "km/h", "km/h", 0.277778 },
        { "mph",  "mph",  0.44704 },
        { "Knot", "kn",   0.514444 },
    }, FALSE },
};

static double temp_to_k(double v, int idx) {
    switch (idx) {
    case 0: return v + 273.15;
    case 1: return (v - 32.0) * 5.0/9.0 + 273.15;
    case 2: return v;
    default: return v;
    }
}

static double k_to_temp(double k, int idx) {
    switch (idx) {
    case 0: return k - 273.15;
    case 1: return (k - 273.15) * 9.0/5.0 + 32.0;
    case 2: return k;
    default: return k;
    }
}

/* ═══ History entry ═════════════════════════════════════════ */

typedef struct { char expr[MAX_EXPR]; char res[MAX_D]; } Hist;

/* ═══ App state ═════════════════════════════════════════════ */

typedef struct {
    GtkApplication *app;
    GtkWidget *win;

    /* tabs */
    GtkWidget *stack;
    GtkWidget *tab_btn[4];

    /* ── calc ── */
    GtkWidget *c_expr;
    GtkWidget *c_res;
    char  c_cur[MAX_D];
    char  c_ebuf[MAX_EXPR];
    double c_acc;
    char   c_op;
    gboolean c_await, c_dot, c_done;

    /* ── converter ── */
    int  cv_cat;
    int  cv_from, cv_to;
    char cv_in[MAX_D];
    gboolean cv_dot;
    gboolean cv_updating;
    GtkWidget *cv_from_drop, *cv_to_drop;
    GtkWidget *cv_from_val, *cv_to_val;
    GtkWidget *cv_from_sym, *cv_to_sym;
    GtkWidget *cv_cat_btns[NUM_CATS];

    /* ── scratchpad ── */
    GtkWidget *sp_text;

    /* ── history ── */
    GtkWidget *h_box;
    Hist  h[MAX_HIST];
    int   h_n;
} A;

static A a;

/* ═══ Formatting ════════════════════════════════════════════ */

static void fmtn(char *b, size_t sz, double v) {
    if (fabs(v) < 1e15 && v == (double)(long long)v)
        snprintf(b, sz, "%lld", (long long)v);
    else
        snprintf(b, sz, "%.10g", v);
}

static const char *opsym(char o) {
    switch (o) {
    case '+': return " + "; case '-': return " − ";
    case '*': return " × "; case '/': return " ÷ ";
    default:  return "";
    }
}

static double calc(double a, char o, double b) {
    switch (o) {
    case '+': return a+b; case '-': return a-b;
    case '*': return a*b; case '/': return b?a/b:0;
    default: return b;
    }
}

/* ═══ History ═══════════════════════════════════════════════ */

static void h_rebuild(void);

static void h_add(const char *ex, const char *rs) {
    if (a.h_n >= MAX_HIST) {
        memmove(&a.h[0], &a.h[1], (MAX_HIST-1)*sizeof(Hist));
        a.h_n = MAX_HIST - 1;
    }
    g_strlcpy(a.h[a.h_n].expr, ex, MAX_EXPR);
    g_strlcpy(a.h[a.h_n].res,  rs, MAX_D);
    a.h_n++;
}

/* ═══ Calculator callbacks ══════════════════════════════════ */

#define CEXPR(s) gtk_label_set_text(GTK_LABEL(a.c_expr),(s))
#define CRES(s)  gtk_label_set_text(GTK_LABEL(a.c_res),(s))

static void cc_digit(GtkButton *b, gpointer u) {
    (void)u;
    const char *d = gtk_button_get_label(b);
    if (a.c_done) {
        a.c_cur[0]=a.c_ebuf[0]=0; a.c_acc=0; a.c_op=0; a.c_done=FALSE;
    }
    if (a.c_await) { a.c_cur[0]=0; a.c_await=FALSE; a.c_dot=FALSE; }
    size_t l = strlen(a.c_cur);
    if (l >= MAX_D-2) return;
    if (l==1 && a.c_cur[0]=='0' && d[0]=='0') return;
    if (l==1 && a.c_cur[0]=='0') a.c_cur[0]=0;
    strcat(a.c_cur, d);
    CRES(a.c_cur);
}

static void cc_dot(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    if (a.c_dot) return;
    a.c_dot = TRUE;
    if (a.c_done) { a.c_cur[0]=a.c_ebuf[0]=0; a.c_acc=0; a.c_op=0; a.c_done=FALSE; }
    if (a.c_await) { strcpy(a.c_cur,"0"); a.c_await=FALSE; }
    if (!a.c_cur[0]) strcpy(a.c_cur,"0");
    strcat(a.c_cur,".");
    CRES(a.c_cur);
}

static void cc_op(GtkButton *b, gpointer u) {
    (void)u;
    const char *l = gtk_button_get_label(b);
    char op=0;
    if (!strcmp(l,"+")) op='+'; else if (!strcmp(l,"−")) op='-';
    else if (!strcmp(l,"×")) op='*'; else if (!strcmp(l,"÷")) op='/';
    if (!op) return;

    if (a.c_done) {
        char t[MAX_D]; fmtn(t,sizeof(t),a.c_acc);
        snprintf(a.c_ebuf,MAX_EXPR,"%s%s",t,opsym(op));
        a.c_done=FALSE; a.c_await=TRUE; a.c_op=op; a.c_dot=FALSE;
        CEXPR(a.c_ebuf); return;
    }
    double v = atof(a.c_cur[0]?a.c_cur:"0");
    if (a.c_op && !a.c_await) {
        a.c_acc = calc(a.c_acc, a.c_op, v);
        char t[MAX_D]; fmtn(t,sizeof(t),a.c_acc); CRES(t);
    } else if (!a.c_await) a.c_acc = v;

    char t[MAX_D]; fmtn(t,sizeof(t),a.c_acc);
    snprintf(a.c_ebuf,MAX_EXPR,"%s%s",t,opsym(op));
    a.c_op=op; a.c_await=TRUE; a.c_dot=FALSE;
    CEXPR(a.c_ebuf);
}

static void cc_eq(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    if (a.c_done) return;
    double v = atof(a.c_cur[0]?a.c_cur:"0");
    char res[MAX_D];
    if (a.c_op) {
        double r = calc(a.c_acc, a.c_op, v);
        fmtn(res, sizeof(res), r);
        char full[MAX_EXPR];
        snprintf(full,MAX_EXPR,"%s%s =", a.c_ebuf, a.c_cur[0]?a.c_cur:res);
        g_strlcpy(a.c_ebuf, full, MAX_EXPR);
        CEXPR(a.c_ebuf); CRES(res);
        h_add(full, res); h_rebuild();
        a.c_acc = r;
    } else {
        fmtn(res,sizeof(res),v);
        snprintf(a.c_ebuf,MAX_EXPR,"%s =",res);
        CEXPR(a.c_ebuf); CRES(res);
        a.c_acc = v;
    }
    g_strlcpy(a.c_cur, res, MAX_D);
    a.c_op=0; a.c_done=TRUE; a.c_dot=FALSE;
}

static void cc_c(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    a.c_cur[0]=0; a.c_dot=FALSE; CRES("0");
}

static void cc_ac(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    a.c_acc=0; a.c_op=0; a.c_await=a.c_dot=a.c_done=FALSE;
    a.c_cur[0]=a.c_ebuf[0]=0;
    CRES("0"); CEXPR("");
}

static void cc_bs(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    if (a.c_done||a.c_await) return;
    size_t l=strlen(a.c_cur);
    if (l>0) {
        if (a.c_cur[l-1]=='.') a.c_dot=FALSE;
        a.c_cur[l-1]=0;
        CRES(a.c_cur[0]?a.c_cur:"0");
    }
}

static void cc_neg(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    if (!a.c_cur[0]||!strcmp(a.c_cur,"0")) return;
    if (a.c_cur[0]=='-') memmove(a.c_cur, a.c_cur+1, strlen(a.c_cur));
    else { memmove(a.c_cur+1, a.c_cur, strlen(a.c_cur)+1); a.c_cur[0]='-'; }
    CRES(a.c_cur);
}

static void cc_pct(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    double v=atof(a.c_cur[0]?a.c_cur:"0");
    if (a.c_op=='+'||a.c_op=='-') v=a.c_acc*(v/100.0); else v/=100.0;
    fmtn(a.c_cur, MAX_D, v);
    CRES(a.c_cur);
}

/* ═══ Converter logic ═══════════════════════════════════════ */

static void cv_recalc(void) {
    double in = atof(a.cv_in[0] ? a.cv_in : "0");
    const Cat *cat = &cats[a.cv_cat];
    double out;

    if (cat->temp) {
        double k = temp_to_k(in, a.cv_from);
        out = k_to_temp(k, a.cv_to);
    } else {
        double base = in * cat->u[a.cv_from].f;
        out = base / cat->u[a.cv_to].f;
    }

    /* update from display */
    gtk_label_set_text(GTK_LABEL(a.cv_from_val),
                       a.cv_in[0] ? a.cv_in : "0");
    gtk_label_set_text(GTK_LABEL(a.cv_from_sym),
                       cat->u[a.cv_from].sym);

    /* update to display */
    char buf[MAX_D];
    if (fabs(out) < 1e-10 && out != 0.0)
        snprintf(buf, sizeof(buf), "%.6e", out);
    else if (fabs(out) < 1e15 && out == (double)(long long)out)
        snprintf(buf, sizeof(buf), "%lld", (long long)out);
    else
        snprintf(buf, sizeof(buf), "%.4f", out);
    gtk_label_set_text(GTK_LABEL(a.cv_to_val), buf);
    gtk_label_set_text(GTK_LABEL(a.cv_to_sym),
                       cat->u[a.cv_to].sym);
}

static void cv_update_drops(void) {
    const Cat *cat = &cats[a.cv_cat];
    const char *names[MAX_UNITS + 1];
    for (int i = 0; i < cat->n; i++) names[i] = cat->u[i].name;
    names[cat->n] = NULL;

    a.cv_updating = TRUE;

    GtkStringList *m1 = gtk_string_list_new(names);
    GtkStringList *m2 = gtk_string_list_new(names);
    gtk_drop_down_set_model(GTK_DROP_DOWN(a.cv_from_drop), G_LIST_MODEL(m1));
    gtk_drop_down_set_model(GTK_DROP_DOWN(a.cv_to_drop),   G_LIST_MODEL(m2));
    g_object_unref(m1);
    g_object_unref(m2);

    if (a.cv_from >= cat->n) a.cv_from = 0;
    if (a.cv_to   >= cat->n) a.cv_to   = (cat->n > 1) ? 1 : 0;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a.cv_from_drop), a.cv_from);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a.cv_to_drop),   a.cv_to);

    a.cv_updating = FALSE;
    cv_recalc();
}

/* converter callbacks */

static void cv_digit(GtkButton *b, gpointer u) {
    (void)u;
    const char *d = gtk_button_get_label(b);
    size_t l = strlen(a.cv_in);
    if (l >= MAX_D - 2) return;
    if (l==1 && a.cv_in[0]=='0' && d[0]=='0') return;
    if (l==1 && a.cv_in[0]=='0') a.cv_in[0] = 0;
    strcat(a.cv_in, d);
    cv_recalc();
}

static void cv_dot_cb(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    if (a.cv_dot) return;
    a.cv_dot = TRUE;
    if (!a.cv_in[0]) strcpy(a.cv_in, "0");
    strcat(a.cv_in, ".");
    cv_recalc();
}

static void cv_bs_cb(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    size_t l = strlen(a.cv_in);
    if (l > 0) {
        if (a.cv_in[l-1] == '.') a.cv_dot = FALSE;
        a.cv_in[l-1] = 0;
        cv_recalc();
    }
}

static void cv_clear_cb(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    a.cv_in[0] = 0;
    a.cv_dot = FALSE;
    cv_recalc();
}

static void cv_neg_cb(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    if (!a.cv_in[0] || !strcmp(a.cv_in, "0")) return;
    if (a.cv_in[0] == '-')
        memmove(a.cv_in, a.cv_in+1, strlen(a.cv_in));
    else {
        memmove(a.cv_in+1, a.cv_in, strlen(a.cv_in)+1);
        a.cv_in[0] = '-';
    }
    cv_recalc();
}

static void cv_swap_cb(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    int tmp = a.cv_from; a.cv_from = a.cv_to; a.cv_to = tmp;
    const char *res = gtk_label_get_text(GTK_LABEL(a.cv_to_val));
    g_strlcpy(a.cv_in, res, MAX_D);
    a.cv_dot = (strchr(a.cv_in, '.') != NULL);
    cv_update_drops();
}

static void cv_cat_cb(GtkButton *b, gpointer u) {
    (void)b;
    int idx = GPOINTER_TO_INT(u);
    a.cv_cat = idx;
    a.cv_from = 0;
    a.cv_to = 1;
    a.cv_in[0] = 0;
    a.cv_dot = FALSE;

    for (int i = 0; i < NUM_CATS; i++) {
        if (i == idx)
            gtk_widget_add_css_class(a.cv_cat_btns[i], "cat-active");
        else
            gtk_widget_remove_css_class(a.cv_cat_btns[i], "cat-active");
    }
    cv_update_drops();
}

static void cv_from_changed(GObject *o, GParamSpec *p, gpointer u) {
    (void)o;(void)p;(void)u;
    if (a.cv_updating) return;
    a.cv_from = gtk_drop_down_get_selected(GTK_DROP_DOWN(a.cv_from_drop));
    cv_recalc();
}

static void cv_to_changed(GObject *o, GParamSpec *p, gpointer u) {
    (void)o;(void)p;(void)u;
    if (a.cv_updating) return;
    a.cv_to = gtk_drop_down_get_selected(GTK_DROP_DOWN(a.cv_to_drop));
    cv_recalc();
}

/* ═══ Scratchpad callbacks ══════════════════════════════════ */

static void sp_clear_cb(GtkButton *b, gpointer u) {
    (void)b;(void)u;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a.sp_text));
    gtk_text_buffer_set_text(buf, "", 0);
}

/* ═══ Tab switching ═════════════════════════════════════════ */

static void switch_tab(int i) {
    const char *n[] = {"calc","converter","scratch","history"};
    gtk_stack_set_visible_child_name(GTK_STACK(a.stack), n[i]);
    for (int j=0;j<4;j++) {
        if (j==i) gtk_widget_add_css_class(a.tab_btn[j],"tab-active");
        else      gtk_widget_remove_css_class(a.tab_btn[j],"tab-active");
    }
}

static void cb_tab(GtkButton *b, gpointer u) {
    (void)b;
    switch_tab(GPOINTER_TO_INT(u));
}

/* ═══ Button helper ═════════════════════════════════════════ */

static GtkWidget *mkbtn(const char *lbl, const char *css, GCallback cb) {
    GtkWidget *b = gtk_button_new_with_label(lbl);
    gtk_widget_add_css_class(b, "calc-btn");
    if (css) gtk_widget_add_css_class(b, css);
    gtk_widget_set_hexpand(b, TRUE);
    gtk_widget_set_vexpand(b, TRUE);
    g_signal_connect(b, "clicked", cb, NULL);
    return b;
}

/* ═══ Build: Calculator view ════════════════════════════════ */

static GtkWidget *build_calc(void) {
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* display */
    GtkWidget *disp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(disp, "display-area");

    a.c_expr = gtk_label_new("");
    gtk_widget_add_css_class(a.c_expr, "expr-label");
    gtk_label_set_xalign(GTK_LABEL(a.c_expr), 1.0);
    gtk_label_set_ellipsize(GTK_LABEL(a.c_expr), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(disp), a.c_expr);

    a.c_res = gtk_label_new("0");
    gtk_widget_add_css_class(a.c_res, "result-label");
    gtk_label_set_xalign(GTK_LABEL(a.c_res), 1.0);
    gtk_box_append(GTK_BOX(disp), a.c_res);

    gtk_box_append(GTK_BOX(vb), disp);

    /* grid */
    GtkWidget *g = gtk_grid_new();
    gtk_widget_add_css_class(g, "calc-grid");
    gtk_grid_set_row_homogeneous(GTK_GRID(g), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    gtk_widget_set_vexpand(g, TRUE);

    /* row 0 */
    gtk_grid_attach(GTK_GRID(g), mkbtn("%","accent",G_CALLBACK(cc_pct)), 0,0,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("(","paren",G_CALLBACK(cc_digit)),1,0,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn(")","paren",G_CALLBACK(cc_digit)),2,0,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("⌫","func",G_CALLBACK(cc_bs)),   3,0,1,1);

    /* row 1 */
    gtk_grid_attach(GTK_GRID(g), mkbtn("+","oper",G_CALLBACK(cc_op)),0,1,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("−","oper",G_CALLBACK(cc_op)),1,1,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("×","oper",G_CALLBACK(cc_op)),2,1,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("÷","oper",G_CALLBACK(cc_op)),3,1,1,1);

    /* row 2 */
    gtk_grid_attach(GTK_GRID(g), mkbtn("7","num",G_CALLBACK(cc_digit)),0,2,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("8","num",G_CALLBACK(cc_digit)),1,2,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("9","num",G_CALLBACK(cc_digit)),2,2,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("C","clear",G_CALLBACK(cc_c)),  3,2,1,1);

    /* row 3 */
    gtk_grid_attach(GTK_GRID(g), mkbtn("4","num",G_CALLBACK(cc_digit)),0,3,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("5","num",G_CALLBACK(cc_digit)),1,3,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("6","num",G_CALLBACK(cc_digit)),2,3,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("AC","accent",G_CALLBACK(cc_ac)),3,3,1,1);

    /* rows 4-5 */
    gtk_grid_attach(GTK_GRID(g), mkbtn("1","num",G_CALLBACK(cc_digit)),0,4,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("2","num",G_CALLBACK(cc_digit)),1,4,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("3","num",G_CALLBACK(cc_digit)),2,4,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("=","equals",G_CALLBACK(cc_eq)),3,4,1,2);
    gtk_grid_attach(GTK_GRID(g), mkbtn("+/−","func",G_CALLBACK(cc_neg)),0,5,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn("0","num",G_CALLBACK(cc_digit)),  1,5,1,1);
    gtk_grid_attach(GTK_GRID(g), mkbtn(".","num",G_CALLBACK(cc_dot)),     2,5,1,1);

    gtk_box_append(GTK_BOX(vb), g);
    return vb;
}

/* ═══ Build: Converter view ═════════════════════════════════ */

static GtkWidget *build_converter(void) {
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(vb, "conv-view");

    /* title */
    GtkWidget *title = gtk_label_new("Unit Converter");
    gtk_widget_add_css_class(title, "view-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_box_append(GTK_BOX(vb), title);

    /* category pills */
    GtkWidget *pill_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pill_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(pill_scroll, -1, 46);

    GtkWidget *pills = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(pills, "cat-pills");

    for (int i = 0; i < NUM_CATS; i++) {
        GtkWidget *b = gtk_button_new_with_label(cats[i].name);
        gtk_widget_add_css_class(b, "cat-pill");
        if (i == 0) gtk_widget_add_css_class(b, "cat-active");
        g_signal_connect(b, "clicked", G_CALLBACK(cv_cat_cb),
                         GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(pills), b);
        a.cv_cat_btns[i] = b;
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pill_scroll), pills);
    gtk_box_append(GTK_BOX(vb), pill_scroll);

    /* ── from unit row ── */
    GtkWidget *from_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(from_box, "conv-row");

    a.cv_from_drop = gtk_drop_down_new_from_strings(
        (const char *[]){ "loading...", NULL });
    gtk_widget_add_css_class(a.cv_from_drop, "conv-drop");
    gtk_widget_set_halign(a.cv_from_drop, GTK_ALIGN_START);
    g_signal_connect(a.cv_from_drop, "notify::selected",
                     G_CALLBACK(cv_from_changed), NULL);
    gtk_box_append(GTK_BOX(from_box), a.cv_from_drop);

    GtkWidget *from_vals = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(from_vals, GTK_ALIGN_END);
    a.cv_from_val = gtk_label_new("0");
    gtk_widget_add_css_class(a.cv_from_val, "conv-value");
    a.cv_from_sym = gtk_label_new("");
    gtk_widget_add_css_class(a.cv_from_sym, "conv-sym");
    gtk_box_append(GTK_BOX(from_vals), a.cv_from_val);
    gtk_box_append(GTK_BOX(from_vals), a.cv_from_sym);
    gtk_box_append(GTK_BOX(from_box), from_vals);

    gtk_box_append(GTK_BOX(vb), from_box);

    /* ── swap button ── */
    GtkWidget *swap = gtk_button_new_with_label("⇅");
    gtk_widget_add_css_class(swap, "swap-btn");
    gtk_widget_set_halign(swap, GTK_ALIGN_CENTER);
    g_signal_connect(swap, "clicked", G_CALLBACK(cv_swap_cb), NULL);
    gtk_box_append(GTK_BOX(vb), swap);

    /* ── to unit row ── */
    GtkWidget *to_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(to_box, "conv-row");

    a.cv_to_drop = gtk_drop_down_new_from_strings(
        (const char *[]){ "loading...", NULL });
    gtk_widget_add_css_class(a.cv_to_drop, "conv-drop");
    gtk_widget_set_halign(a.cv_to_drop, GTK_ALIGN_START);
    g_signal_connect(a.cv_to_drop, "notify::selected",
                     G_CALLBACK(cv_to_changed), NULL);
    gtk_box_append(GTK_BOX(to_box), a.cv_to_drop);

    GtkWidget *to_vals = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(to_vals, GTK_ALIGN_END);
    a.cv_to_val = gtk_label_new("0");
    gtk_widget_add_css_class(a.cv_to_val, "conv-value");
    a.cv_to_sym = gtk_label_new("");
    gtk_widget_add_css_class(a.cv_to_sym, "conv-sym");
    gtk_box_append(GTK_BOX(to_vals), a.cv_to_val);
    gtk_box_append(GTK_BOX(to_vals), a.cv_to_sym);
    gtk_box_append(GTK_BOX(to_box), to_vals);

    gtk_box_append(GTK_BOX(vb), to_box);

    /* ── converter numpad ── */
    GtkWidget *g2 = gtk_grid_new();
    gtk_widget_add_css_class(g2, "conv-grid");
    gtk_grid_set_row_homogeneous(GTK_GRID(g2), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(g2), TRUE);
    gtk_widget_set_vexpand(g2, TRUE);

    const char *digits[] = {"7","8","9","4","5","6","1","2","3","0"};
    int positions[][2] = {
        {0,0},{1,0},{2,0}, {0,1},{1,1},{2,1},
        {0,2},{1,2},{2,2}, {1,3}
    };
    for (int i = 0; i < 10; i++) {
        GtkWidget *b = gtk_button_new_with_label(digits[i]);
        gtk_widget_add_css_class(b, "calc-btn");
        gtk_widget_add_css_class(b, "num");
        gtk_widget_set_hexpand(b, TRUE);
        gtk_widget_set_vexpand(b, TRUE);
        g_signal_connect(b, "clicked", G_CALLBACK(cv_digit), NULL);
        gtk_grid_attach(GTK_GRID(g2), b,
                        positions[i][0], positions[i][1], 1, 1);
    }

    GtkWidget *bs = mkbtn("⌫","func",G_CALLBACK(cv_bs_cb));
    gtk_grid_attach(GTK_GRID(g2), bs, 3, 0, 1, 1);

    GtkWidget *cl = mkbtn("C","clear",G_CALLBACK(cv_clear_cb));
    gtk_grid_attach(GTK_GRID(g2), cl, 3, 1, 1, 1);

    GtkWidget *ng = mkbtn("+/−","func",G_CALLBACK(cv_neg_cb));
    gtk_grid_attach(GTK_GRID(g2), ng, 0, 3, 1, 1);

    GtkWidget *dt = mkbtn(".","num",G_CALLBACK(cv_dot_cb));
    gtk_grid_attach(GTK_GRID(g2), dt, 2, 3, 1, 1);

    GtkWidget *sw = mkbtn("⇅","swap-pad",G_CALLBACK(cv_swap_cb));
    gtk_grid_attach(GTK_GRID(g2), sw, 3, 2, 1, 2);

    gtk_box_append(GTK_BOX(vb), g2);

    /* initialize dropdowns */
    a.cv_cat = 0; a.cv_from = 0; a.cv_to = 1;
    strcpy(a.cv_in, "1");
    cv_update_drops();

    return vb;
}

/* ═══ Build: Scratchpad view ════════════════════════════════ */

static GtkWidget *build_scratch(void) {
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(vb, "scratch-view");

    /* header */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(hdr, "scratch-header");

    GtkWidget *t = gtk_label_new("Scratchpad");
    gtk_widget_add_css_class(t, "view-title");
    gtk_widget_set_hexpand(t, TRUE);
    gtk_label_set_xalign(GTK_LABEL(t), 0.0);
    gtk_box_append(GTK_BOX(hdr), t);

    GtkWidget *clr = gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(clr, "scratch-clear-btn");
    g_signal_connect(clr, "clicked", G_CALLBACK(sp_clear_cb), NULL);
    gtk_box_append(GTK_BOX(hdr), clr);

    gtk_box_append(GTK_BOX(vb), hdr);

    /* text area */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    a.sp_text = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(a.sp_text), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(a.sp_text), 20);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(a.sp_text), 20);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(a.sp_text), 16);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(a.sp_text), 16);
    gtk_widget_add_css_class(a.sp_text, "scratch-text");

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a.sp_text));
    gtk_text_buffer_set_text(buf,
        "Jot down calculations, notes, or formulas here...\n", -1);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), a.sp_text);
    gtk_box_append(GTK_BOX(vb), scroll);

    return vb;
}

/* ═══ Build: History view ═══════════════════════════════════ */

static void h_rebuild(void) {
    if (!a.h_box) return;
    GtkWidget *ch;
    while ((ch = gtk_widget_get_first_child(a.h_box)))
        gtk_box_remove(GTK_BOX(a.h_box), ch);

    if (a.h_n == 0) {
        GtkWidget *e = gtk_label_new("No calculations yet.\n\nDo some math and your\nhistory will appear here.");
        gtk_widget_add_css_class(e, "hist-empty");
        gtk_label_set_justify(GTK_LABEL(e), GTK_JUSTIFY_CENTER);
        gtk_box_append(GTK_BOX(a.h_box), e);
        return;
    }

    for (int i = a.h_n - 1; i >= 0; i--) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_add_css_class(row, "hist-row");

        GtkWidget *ex = gtk_label_new(a.h[i].expr);
        gtk_widget_add_css_class(ex, "hist-expr");
        gtk_label_set_xalign(GTK_LABEL(ex), 1.0);
        gtk_label_set_ellipsize(GTK_LABEL(ex), PANGO_ELLIPSIZE_START);
        gtk_box_append(GTK_BOX(row), ex);

        GtkWidget *rs = gtk_label_new(a.h[i].res);
        gtk_widget_add_css_class(rs, "hist-result");
        gtk_label_set_xalign(GTK_LABEL(rs), 1.0);
        gtk_box_append(GTK_BOX(row), rs);

        gtk_box_append(GTK_BOX(a.h_box), row);
    }
}

static GtkWidget *build_history(void) {
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *t = gtk_label_new("History");
    gtk_widget_add_css_class(t, "view-title");
    gtk_label_set_xalign(GTK_LABEL(t), 0.5);
    gtk_box_append(GTK_BOX(vb), t);

    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sc, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    a.h_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(a.h_box, "hist-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), a.h_box);
    gtk_box_append(GTK_BOX(vb), sc);

    h_rebuild();
    return vb;
}

/* ═══ Build: Tab bar ════════════════════════════════════════ */

static GtkWidget *build_tabs(void) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(bar, "tab-bar");

    struct { const char *icon; const char *tip; } t[] = {
        { "⊞", "Calculator" }, { "⇄", "Converter" },
        { "✎", "Scratch" },    { "◷", "History" },
    };
    for (int i = 0; i < 4; i++) {
        GtkWidget *b = gtk_button_new_with_label(t[i].icon);
        gtk_widget_add_css_class(b, "tab-btn");
        gtk_widget_set_hexpand(b, TRUE);
        gtk_widget_set_tooltip_text(b, t[i].tip);
        g_signal_connect(b, "clicked", G_CALLBACK(cb_tab),
                         GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(bar), b);
        a.tab_btn[i] = b;
    }
    return bar;
}

/* ═══ CSS ═══════════════════════════════════════════════════ */

static void load_css(void) {
    GtkCssProvider *p = gtk_css_provider_new();
    const char *paths[] = { "data/style.css",
                            "/usr/share/nano-calculator/style.css", NULL };
    for (int i = 0; paths[i]; i++) {
        if (g_file_test(paths[i], G_FILE_TEST_EXISTS)) {
            gtk_css_provider_load_from_path(p, paths[i]);
            gtk_style_context_add_provider_for_display(
                gdk_display_get_default(), GTK_STYLE_PROVIDER(p),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            return;
        }
    }
    g_object_unref(p);
}

/* ═══ Activate ══════════════════════════════════════════════ */

static void on_activate(GtkApplication *ap, gpointer u) {
    (void)u;
    load_css();

    a.win = gtk_application_window_new(ap);
    gtk_window_set_title(GTK_WINDOW(a.win), "Calculator");
    gtk_window_set_default_size(GTK_WINDOW(a.win), 380, 740);

    GtkWidget *mb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(mb, "main-container");
    gtk_window_set_child(GTK_WINDOW(a.win), mb);

    a.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(a.stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(a.stack), 200);
    gtk_widget_set_vexpand(a.stack, TRUE);

    gtk_stack_add_named(GTK_STACK(a.stack), build_calc(),      "calc");
    gtk_stack_add_named(GTK_STACK(a.stack), build_converter(), "converter");
    gtk_stack_add_named(GTK_STACK(a.stack), build_scratch(),   "scratch");
    gtk_stack_add_named(GTK_STACK(a.stack), build_history(),   "history");

    gtk_box_append(GTK_BOX(mb), a.stack);
    gtk_box_append(GTK_BOX(mb), build_tabs());

    switch_tab(0);
    gtk_window_present(GTK_WINDOW(a.win));
}

/* ═══ main ══════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
#ifdef HAVE_LIBADWAITA
    adw_init();
#endif
    a.app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(a.app, "activate", G_CALLBACK(on_activate), NULL);
    int st = g_application_run(G_APPLICATION(a.app), argc, argv);
    g_object_unref(a.app);
    return st;
}
