/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ANDROIDAUTO_PLATFORM_H
#define ANDROIDAUTO_PLATFORM_H
#include "rtaa/receiver.h"
#include "rtaa/rtthread.h"
/* All session/media lifecycle operations have one application worker owner. */
int androidauto_usb_open(rtaa_transport *, uint32_t *generation);
uint32_t androidauto_usb_generation(void);
bool androidauto_usb_present(void);
int androidauto_random(void *, unsigned char *, size_t);
rtaa_sink androidauto_media_sink(void);
void androidauto_media_info_bind(rtaa_sink *);
void androidauto_status_info_bind(rtaa_sink *);
bool androidauto_media_retired(void);
int androidauto_media_retire(void);
int androidauto_media_error(void);
bool androidauto_media_presented(void);
int androidauto_media_set_geometry(const rtaa_geometry *);
#endif
