// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_BUNDLED_H
#define NBT_BUNDLED_H

/* The manager's own applications (docs/self-test.md): packages the release
 * carries as named components, signed with the manager key, installed from
 * the running slot's release when a self test starts. Their ids start with
 * "manager-": never from a phone, a session or provisioning, and left out of
 * nbt_fs_list_apps (so of LIST, the phone's lists, autostart and the slot
 * check). */

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_BUNDLED_PREFIX "manager-"
#define NBT_BUNDLED_COUNT 3u

typedef struct {
    const char *component; /* the release's named component */
    const char *app;       /* the application id it installs */
    const char *form;      /* native, guest or module: the self test's group */
} nbt_bundled_app;

/* In the order a self test runs them: the module, which the driver acts on,
 * last. */
extern const nbt_bundled_app nbt_bundled_apps[NBT_BUNDLED_COUNT];

/* 1 when `app` is one of the manager's ids ("manager-..."), else 0. */
int nbt_app_bundled(const char *app);

#ifdef __cplusplus
}
#endif

#endif
