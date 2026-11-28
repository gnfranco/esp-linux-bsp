/* SPDX-License-Identifier: Apache-2.0 */
#ifndef S31_WIFI_USERSPACE_LOG_H
#define S31_WIFI_USERSPACE_LOG_H

/*
 * Linux userspace thread context only. These calls may lock, format and
 * block on stderr. Keep them out of interrupt and signal callbacks.
 * Return the number of bytes formatted to stderr, or a negative value on
 * formatting/output error, with the usual libc stream buffering semantics.
 */
int net80211_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int phy_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int pp_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
