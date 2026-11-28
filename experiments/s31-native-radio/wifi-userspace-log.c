/*
 * SPDX-FileCopyrightText: 2016-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi-userspace-log.h"

#include <stdarg.h>
#include <stdio.h>

/* Adapted from the esp_wifi and esp_phy lib_printf.c wrappers. */
int net80211_printf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    int result = vfprintf(stderr, format, arg);
    va_end(arg);
    return result;
}

int phy_printf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    int result = vfprintf(stderr, format, arg);
    va_end(arg);
    return result;
}

int pp_printf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    int result = vfprintf(stderr, format, arg);
    va_end(arg);
    return result;
}
