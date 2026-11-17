/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ANDROIDAUTO_WIRELESS_H
#define ANDROIDAUTO_WIRELESS_H
#include "rtaa/core.h"
/* One application worker owns AP, listener, bootstrap and accepted transport. */
int androidauto_wireless_open(rtaa_transport *, uint32_t *);
void androidauto_wireless_stop(void);
bool androidauto_wireless_retired(void);
/* BT callbacks only enqueue bytes/status. The application worker calls poll. */
int androidauto_bt_start(void);
void androidauto_bt_stop(void);
int androidauto_bt_open(rtaa_transport *);
bool androidauto_bt_retired(void);
void androidauto_bt_poll(void);
#endif
