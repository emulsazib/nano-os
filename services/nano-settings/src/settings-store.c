#include "settings-store.h"
#include <string.h>

struct _SettingsStore {
    GKeyFile *keyfile;
    gchar    *file_path;
};

SettingsStore *
settings_store_new (void)
{
    SettingsStore *store = g_new0 (SettingsStore, 1);
    store->keyfile = g_key_file_new ();
    store->file_path = NULL;
    return store;
}

void
settings_store_free (SettingsStore *store)
{
    if (store == NULL)
        return;

    g_key_file_free (store->keyfile);
    g_free (store->file_path);
    g_free (store);
}

gboolean
settings_store_load (SettingsStore *store,
                     const gchar   *path,
                     GError       **error)
{
    g_return_val_if_fail (store != NULL, FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    g_free (store->file_path);
    store->file_path = g_strdup (path);

    if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
        g_warning ("Settings file %s does not exist, starting with empty config", path);
        return TRUE;
    }

    gboolean ret = g_key_file_load_from_file (store->keyfile, path,
                                               G_KEY_FILE_KEEP_COMMENTS, error);
    if (!ret) {
        g_warning ("Failed to load settings from %s", path);
    }

    return ret;
}

gboolean
settings_store_save (SettingsStore *store,
                     const gchar   *path,
                     GError       **error)
{
    g_return_val_if_fail (store != NULL, FALSE);

    const gchar *save_path = (path != NULL) ? path : store->file_path;
    if (save_path == NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "No file path specified for saving");
        return FALSE;
    }

    /* Ensure parent directory exists */
    g_autofree gchar *dir = g_path_get_dirname (save_path);
    if (g_mkdir_with_parents (dir, 0755) != 0) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Failed to create directory %s", dir);
        return FALSE;
    }

    gsize length;
    g_autofree gchar *data = g_key_file_to_data (store->keyfile, &length, error);
    if (data == NULL)
        return FALSE;

    return g_file_set_contents (save_path, data, (gssize)length, error);
}

gchar *
settings_store_get (SettingsStore *store,
                    const gchar   *section,
                    const gchar   *key,
                    GError       **error)
{
    g_return_val_if_fail (store != NULL, NULL);
    g_return_val_if_fail (section != NULL, NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_key_file_get_string (store->keyfile, section, key, error);
}

void
settings_store_set (SettingsStore *store,
                    const gchar   *section,
                    const gchar   *key,
                    const gchar   *value)
{
    g_return_if_fail (store != NULL);
    g_return_if_fail (section != NULL);
    g_return_if_fail (key != NULL);
    g_return_if_fail (value != NULL);

    g_key_file_set_string (store->keyfile, section, key, value);
}

GVariant *
settings_store_get_all (SettingsStore *store)
{
    g_return_val_if_fail (store != NULL, NULL);

    GVariantBuilder outer;
    g_variant_builder_init (&outer, G_VARIANT_TYPE ("a{sa{ss}}"));

    gsize n_groups = 0;
    g_auto(GStrv) groups = g_key_file_get_groups (store->keyfile, &n_groups);

    for (gsize i = 0; i < n_groups; i++) {
        GVariantBuilder inner;
        g_variant_builder_init (&inner, G_VARIANT_TYPE ("a{ss}"));

        gsize n_keys = 0;
        g_auto(GStrv) keys = g_key_file_get_keys (store->keyfile, groups[i],
                                                    &n_keys, NULL);
        if (keys != NULL) {
            for (gsize j = 0; j < n_keys; j++) {
                g_autofree gchar *val = g_key_file_get_string (store->keyfile,
                                                                groups[i],
                                                                keys[j], NULL);
                if (val != NULL) {
                    g_variant_builder_add (&inner, "{ss}", keys[j], val);
                }
            }
        }

        g_variant_builder_add (&outer, "{sa{ss}}", groups[i], &inner);
    }

    return g_variant_builder_end (&outer);
}
