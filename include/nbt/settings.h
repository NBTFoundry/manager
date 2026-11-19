// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_SETTINGS_H
#define NBT_SETTINGS_H

/* Application settings, declared in the signed package manifest and chosen
 * by the user in the manager. Each setting is a choice among a few tokens:
 *
 *   setting=<key>|<label>|<default>|<choice>,<choice>...
 *
 * key: 1-16 of [a-z0-9_], starting with a letter; label: 1-24 printable ASCII
 * characters other than '|'; choices: 1-6 distinct tokens of 1-12 [a-z0-9_-];
 * the default is one of them. A package declares at most NBT_SETTINGS_MAX
 * settings, with distinct keys. The application receives each value in its
 * environment as NBT_SETTING_<KEY>=<value> (key in capitals).
 *
 * The parser only parses and formats; src/app_settings.c stores them.
 * src/settings_file.c provides the optional application-side snapshot reader. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_SETTINGS_MAX 12
#define NBT_SETTINGS_VALUES_MAX                                                                    \
    (12 + NBT_SETTINGS_MAX * (NBT_SETTING_KEY_MAX + NBT_SETTING_CHOICE_MAX + 2))
#define NBT_SETTING_CHOICES_MAX 6
#define NBT_SETTING_KEY_MAX 16
#define NBT_SETTING_LABEL_MAX 24
#define NBT_SETTING_CHOICE_MAX 12
/* One declaration line's value, with its terminator. */
#define NBT_SETTING_TEXT_MAX                                                                       \
    (NBT_SETTING_KEY_MAX + NBT_SETTING_LABEL_MAX + NBT_SETTING_CHOICE_MAX +                        \
     NBT_SETTING_CHOICES_MAX * (NBT_SETTING_CHOICE_MAX + 1) + 4)

typedef struct {
    char key[NBT_SETTING_KEY_MAX + 1];
    char label[NBT_SETTING_LABEL_MAX + 1];
    char value[NBT_SETTING_CHOICE_MAX + 1]; /* the default, or the current value */
    char choices[NBT_SETTING_CHOICES_MAX][NBT_SETTING_CHOICE_MAX + 1];
    size_t choice_count;
} nbt_setting;

typedef struct {
    nbt_setting items[NBT_SETTINGS_MAX];
    size_t count;
} nbt_settings;

/* Parses "key|label|value|choice,choice"; the value must be a choice. */
int nbt_setting_parse(const char *text, nbt_setting *out);
/* Formats a setting the same way; returns its length or -1. */
int nbt_setting_format(const nbt_setting *setting, char *out, size_t size);
/* Appends a parsed declaration; rejects a full list and duplicate keys. */
int nbt_settings_add(nbt_settings *settings, const char *text);
/* Index of `key`, or -1. */
int nbt_settings_find(const nbt_settings *settings, const char *key);
/* 1 when `value` is one of the setting's choices. */
int nbt_setting_allows(const nbt_setting *setting, const char *value);

/* The stored declaration: "NBTSETTINGS1\n" then one "setting=" line each. */
int nbt_settings_schema_parse(const char *text, size_t size, nbt_settings *out);
int nbt_settings_schema_format(const nbt_settings *settings, char *out, size_t size);
/* The user's values: "NBTVALUES1\n" then "<key>=<value>\n" lines. Values
 * replace the declared defaults where the key is declared and the value is
 * one of its choices; other lines are ignored, so values survive a package
 * that changes its declaration. A malformed file returns -1 and leaves the
 * defaults. */
int nbt_settings_values_apply(nbt_settings *settings, const char *text, size_t size);
/* Read one bounded private regular snapshot, opening afresh each time. On
 * I/O, unsafe file or parse failure, leave the caller's last valid values
 * unchanged. A successful read applies only declared, allowed choices. */
int nbt_settings_values_read(const char *path, nbt_settings *settings);
int nbt_settings_values_format(const nbt_settings *settings, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
