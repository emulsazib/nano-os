#include "clock.h"

gchar *
nano_clock_get_time_string(void)
{
    GDateTime *now = g_date_time_new_now_local();
    if (!now) {
        return g_strdup("--:--");
    }
    gchar *str = g_date_time_format(now, "%H:%M");
    g_date_time_unref(now);
    return str;
}

gchar *
nano_clock_get_date_string(void)
{
    GDateTime *now = g_date_time_new_now_local();
    if (!now) {
        return g_strdup("---");
    }
    gchar *str = g_date_time_format(now, "%A, %B %d");
    g_date_time_unref(now);
    return str;
}

gchar *
nano_clock_get_full_time_string(void)
{
    GDateTime *now = g_date_time_new_now_local();
    if (!now) {
        return g_strdup("--:--:--");
    }
    gchar *str = g_date_time_format(now, "%H:%M:%S");
    g_date_time_unref(now);
    return str;
}
