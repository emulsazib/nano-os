#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <glib.h>
#include <gio/gio.h>

typedef struct _SettingsStore SettingsStore;

SettingsStore *settings_store_new          (void);
void           settings_store_free         (SettingsStore *store);

gboolean       settings_store_load         (SettingsStore *store,
                                            const gchar   *path,
                                            GError       **error);
gboolean       settings_store_save         (SettingsStore *store,
                                            const gchar   *path,
                                            GError       **error);

gchar         *settings_store_get          (SettingsStore *store,
                                            const gchar   *section,
                                            const gchar   *key,
                                            GError       **error);
void           settings_store_set          (SettingsStore *store,
                                            const gchar   *section,
                                            const gchar   *key,
                                            const gchar   *value);

GVariant      *settings_store_get_all      (SettingsStore *store);

#endif /* SETTINGS_STORE_H */
