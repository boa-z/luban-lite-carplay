/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ANDROIDAUTO_APP_H
#define ANDROIDAUTO_APP_H
#include <stdbool.h>
#include <stdint.h>
#include "rtaa/receiver.h"
typedef struct
{
    bool armed, usb, video, fusion, wireless;
    rtaa_state state;
    int error;
    uint32_t attempts;
    rtaa_statistics statistics;
} androidauto_status;
int androidauto_app_init(void);
bool androidauto_app_retired(void);
void androidauto_app_enable(bool);
void androidauto_app_set_fusion(bool);
void androidauto_app_status(androidauto_status *);
int androidauto_app_touch(uint8_t action, uint16_t x, uint16_t y);
#endif
