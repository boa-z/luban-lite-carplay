/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PROJECTION_DEMO_UI_H
#define PROJECTION_DEMO_UI_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Shared Mobility Cockpit presentation contract. No receiver, OS, USB, MPP
 * or protocol header belongs here. Instances of this UI have one GUI owner. */
typedef enum
{
    DEMO_PROTOCOL_LOCAL,
    DEMO_PROTOCOL_CARPLAY,
    DEMO_PROTOCOL_ANDROIDAUTO,
    DEMO_PROTOCOL_CARLIFE,
    DEMO_PROTOCOL_COUNT
} demo_ui_protocol;
typedef enum
{
    DEMO_VIEW_LOCAL,
    DEMO_VIEW_FUSION,
    DEMO_VIEW_FULLSCREEN
} demo_ui_view;
typedef struct
{
    bool supported[DEMO_PROTOCOL_COUNT];
    demo_ui_protocol selected;
    bool switching, connected, video, fusion;
    int error;
    const char *detail; /* Stable string, consumed during the GUI poll. */
    uint64_t video_bytes, audio_bytes;
} demo_ui_status;
typedef struct
{
    void *ctx;
    void (*poll)(void *, demo_ui_status *);
    bool (*select)(void *, demo_ui_protocol);
    bool (*layout)(void *, bool fusion);
    /* PRESS=0, RELEASE=1, DRAG=2, coordinates relative to viewport. */
    int (*touch)(void *, uint8_t action, uint16_t x, uint16_t y);
    /* GUI-owned hardware composition. Return false on failure. */
    bool (*display)(void *, demo_ui_view);
    bool (*volume_get)(unsigned *), (*volume_set)(unsigned);
    bool (*brightness_get)(unsigned *), (*brightness_set)(unsigned);
    void (*clock_get)(char *, size_t);
} demo_ui_backend;
void demo_ui_init(const demo_ui_backend *);
#endif
