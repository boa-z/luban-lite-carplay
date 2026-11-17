/* SPDX-License-Identifier: GPL-3.0-or-later */
/* CarPlay application adapter for the exact same protocol-neutral UI source
 * linked by rtprojection. No CarPlay type leaks into demo_ui.h or demo_ui.c. */
#include "demo_ui.h"
#include "demo_display_lubanlite.h"
#include "connection_manager.h"
#include "fusion_display.h"
#include "audio_volume_lubanlite.h"
#include "backlight_lubanlite.h"
#include "rtcp/input.h"
#include <rtthread.h>
static bool enabled, initialized;
static bool select_protocol(void *ctx, demo_ui_protocol protocol)
{
    (void)ctx;
    if (protocol != DEMO_PROTOCOL_LOCAL && protocol != DEMO_PROTOCOL_CARPLAY)
        return false;
    enabled = protocol == DEMO_PROTOCOL_CARPLAY;
    carplay_connection_enable(enabled);
    if (enabled)
        carplay_connection_retry();
    return true;
}
static bool layout(void *ctx, bool fusion)
{
    (void)ctx;
    return carplay_connection_set_display_mode(fusion ? CARPLAY_FUSION : CARPLAY_FULLSCREEN);
}
static int touch(void *ctx, uint8_t action, uint16_t x, uint16_t y)
{
    (void)ctx;
    carplay_fusion_snapshot_t s;
    carplay_fusion_snapshot(rt_tick_get_millisecond(), &s);
    uint16_t px, py;
    if (!rtcp_display_map_point(&s.video_rect, s.logical_width, s.logical_height, x, y, &px, &py))
        return -1;
    uint8_t phase = action == 0 ? RTCP_TOUCH_DOWN : action == 1 ? RTCP_TOUCH_UP : RTCP_TOUCH_MOVE;
    return carplay_connection_touch(phase, 0, px, py);
}
static void poll(void *ctx, demo_ui_status *out)
{
    (void)ctx;
    *out = (demo_ui_status){0};
    out->supported[DEMO_PROTOCOL_CARPLAY] = true;
    if (!initialized)
    {
        int rc = carplay_connection_manager_start();
        if (rc)
        {
            out->error = rc;
            out->detail = "Receiver initialization failed";
            return;
        }
        initialized = true;
    }
    demo_status_t cp = {0};
    carplay_connection_status(&cp);
    carplay_fusion_snapshot_t display;
    carplay_fusion_snapshot(rt_tick_get_millisecond(), &display);
    out->selected = enabled ? DEMO_PROTOCOL_CARPLAY : DEMO_PROTOCOL_LOCAL;
    out->switching = (!enabled && !carplay_connection_retired()) || cp.display_reconfiguring;
    out->connected = cp.stage == DEMO_LIVE;
    out->video = enabled && display.video_active;
    out->fusion = cp.display_requested != CARPLAY_FULLSCREEN;
    out->error = cp.error;
    out->detail =
        cp.pin[0]
            ? "Approve the displayed pairing PIN on your iPhone; see the console"
            : "CarPlay backend. Android Auto is developed in the independent rtprojection app.";
}
void carplay_demo_launcher_init(void)
{
    (void)carplay_fusion_init(1024, 600);
    demo_ui_backend backend = {.poll = poll,
                               .select = select_protocol,
                               .layout = layout,
                               .touch = touch,
                               .display = demo_display_lubanlite,
                               .volume_get = carplay_volume_get,
                               .volume_set = carplay_volume_set,
                               .brightness_get = carplay_backlight_get,
                               .brightness_set = carplay_backlight_set,
                               .clock_get = demo_clock_lubanlite};
    demo_ui_init(&backend);
}
