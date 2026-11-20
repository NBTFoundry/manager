// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FIREWALL_H
#define NBT_FIREWALL_H

/* Lets phones reach the worker and door endpoints through the head unit's packet
 * filter while their listeners run. One rule,
 *   pass in quick on <interface> inet proto tcp to <address> port <port>
 *       flags S/SA keep state label "nbt-manager"
 * is inserted at the head of the running main ruleset through /dev/pf
 * (DIOCCHANGERULE), as BMW's own services open ports at run time. Nothing is
 * written to disk: /etc/pf.conf is never touched, and a reboot or a ruleset
 * reload drops the rule; the door refreshes its own nbt-door rule. The filter is never switched on
 * or off. QNX only; elsewhere both calls do nothing. Not thread safe. */

#include <stdint.h>

/* Removes any earlier rule of ours, then inserts one for this listener.
 * `address` is in network byte order. Returns 0, or -1 with errno. */
int nbt_firewall_open(const char *interface_name, uint32_t address, uint16_t port);
/* Removes every rule of ours (also leftovers after a crash). Returns the
 * number removed, or -1 with errno. */
int nbt_firewall_close(void);
int nbt_firewall_close_label(const char *label);
int nbt_firewall_open_label(const char *interface_name, uint32_t address, uint16_t port,
                            const char *label);
/* An application's rules (capability `net`, docs/app-bridge.md): label
 * "nbt-app-<app>", TCP to any address of `interface_name` on `port`; an
 * application may have several. Added without removing others. */
int nbt_firewall_app_add(const char *app, const char *interface_name, uint16_t port);
/* Removes the application's rules, or with `app` NULL every application's
 * (leftovers after a crash). Returns the number removed, or -1 with errno. */
int nbt_firewall_app_close(const char *app);

#endif
