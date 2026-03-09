#ifndef NANO_SHELL_CLOCK_H
#define NANO_SHELL_CLOCK_H

#include <glib.h>

/* Returns "HH:MM" formatted string. Free with g_free(). */
gchar *nano_clock_get_time_string(void);

/* Returns "Day, Month DD" formatted string. Free with g_free(). */
gchar *nano_clock_get_date_string(void);

/* Returns "HH:MM:SS" formatted string. Free with g_free(). */
gchar *nano_clock_get_full_time_string(void);

#endif /* NANO_SHELL_CLOCK_H */
