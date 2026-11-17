/* SPDX-License-Identifier: GPL-3.0-only */
#include "projection_policy.h"
#include "androidauto_app.h"
#include "demo_ui.h"
#include "demo_display_lubanlite.h"
#include "audio_volume_lubanlite.h"
#include "backlight_lubanlite.h"
#include <rtthread.h>
#include <stdatomic.h>
#include <string.h>
static projection_policy policy;
static atomic_uint requested;
static atomic_bool fusion = true;
static atomic_uint kick;
static bool initialized, applied_fusion = true;
static void enable(void *ctx, bool value)
{
    (void)ctx;
    androidauto_app_enable(value);
}
static bool retired(void *ctx)
{
    (void)ctx;
    return androidauto_app_retired();
}
static bool select_protocol(void *ctx, demo_ui_protocol protocol)
{
    (void)ctx;
    if (protocol != DEMO_PROTOCOL_LOCAL && protocol != DEMO_PROTOCOL_ANDROIDAUTO)
        return false;
    requested = (unsigned)protocol;
    kick = true;
    return true;
}
static bool layout(void *ctx, bool value)
{
    (void)ctx;
    fusion = value;
    return true;
}
static int touch(void *ctx, uint8_t action, uint16_t x, uint16_t y)
{
    (void)ctx;
    if (policy.owner != PROJECTION_ANDROIDAUTO || policy.stopping)
        return RTAA_ESTATE;
    return androidauto_app_touch(action, x, y);
}
static void poll(void *ctx, demo_ui_status *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->supported[DEMO_PROTOCOL_ANDROIDAUTO] = true;
    if (!initialized)
    {
        int rc = androidauto_app_init();
        if (rc)
        {
            out->error = rc;
            out->detail = "Receiver initialization failed";
            return;
        }
        androidauto_app_set_fusion(applied_fusion);
        policy.backend[PROJECTION_ANDROIDAUTO] = (projection_backend){NULL, enable, retired};
        initialized = true;
    }
    bool mode = fusion;
    if (mode != applied_fusion)
    {
        /* Geometry changes retire the old owner before restarting it. */
        projection_policy_select(&policy, PROJECTION_LOCAL);
        projection_policy_poll(&policy, rt_tick_get_millisecond());
        if (policy.owner == PROJECTION_LOCAL)
        {
            androidauto_app_set_fusion(mode);
            applied_fusion = mode;
        }
    }
    if (mode == applied_fusion)
    {
        projection_protocol next = (projection_protocol)(unsigned)requested;
        bool retry = atomic_exchange(&kick, false);
        if (retry && policy.owner == next && next == PROJECTION_ANDROIDAUTO && !policy.stopping)
            androidauto_app_enable(true);
        projection_policy_select(&policy, next);
        projection_policy_poll(&policy, rt_tick_get_millisecond());
    }
    androidauto_status aa;
    androidauto_app_status(&aa);
    out->selected = (demo_ui_protocol)(unsigned)requested;
    out->switching = policy.stopping || policy.owner != (projection_protocol)(unsigned)requested ||
                     mode != applied_fusion;
    out->connected = aa.state == RTAA_CONNECTED;
    out->video = aa.video;
    out->fusion = applied_fusion;
    out->error = policy.error ? policy.error : aa.error;
    out->video_bytes = aa.statistics.video_bytes;
    out->audio_bytes = aa.statistics.audio_bytes;
    out->detail =
        out->switching ? "Waiting for USB, video and audio resources to retire"
        : aa.error == RTAA_ESECURITY
            ? "Provision client.pem, client.key and the 32-byte peer.sha256 pin"
        : aa.error ? "Session stopped; inspect androidauto status and console diagnostics"
        : requested == PROJECTION_LOCAL
            ? "Select Android Auto. CarPlay and CarLife backends are not enabled in this app."
        : !aa.usb ? "Connect an Android phone to USB1; approve Android Auto on the phone"
        : aa.state == RTAA_CONNECTED ? "Android Auto connected"
        : aa.state == RTAA_TLS       ? "Negotiating TLS"
        : aa.state == RTAA_DISCOVERY ? "Discovering phone services"
                                     : "Negotiating the wired Android Auto session";
}
void projection_demo_gui_init(void)
{
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
int projection_demo_command(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "start") || !strcmp(argv[1], "androidauto")))
        return select_protocol(NULL, DEMO_PROTOCOL_ANDROIDAUTO) ? 0 : -1;
    if (argc == 2 && (!strcmp(argv[1], "stop") || !strcmp(argv[1], "local")))
        return select_protocol(NULL, DEMO_PROTOCOL_LOCAL) ? 0 : -1;
    if (argc == 2 && !strcmp(argv[1], "fullscreen"))
        return layout(NULL, false) ? 0 : -1;
    if (argc == 2 && !strcmp(argv[1], "fusion"))
        return layout(NULL, true) ? 0 : -1;
    androidauto_status aa;
    androidauto_app_status(&aa);
    rt_kprintf("projection androidauto|local|fusion|fullscreen|status: requested=%u state=%u "
               "error=%d video=%u retired=%u\n",
               (unsigned)requested, aa.state, aa.error, aa.video, androidauto_app_retired());
    return 0;
}
MSH_CMD_EXPORT_ALIAS(projection_demo_command, projection, Shared UI Android Auto backend);
