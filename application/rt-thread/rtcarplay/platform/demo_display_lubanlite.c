/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Same DE UI crop/alpha policy as rtcarplay/ui/video_layer_policy.c, with a
 * protocol-neutral interface. Include AIC ioctl definitions before libc. */
#include <aic_common.h>
#include <rtthread.h>
#include <rtdevice.h>
#include "artinchip_fb.h"
#include "demo_display_lubanlite.h"
#include <stdio.h>
#include <time.h>
#ifdef RT_USING_RTC
#include <drivers/rtc.h>
#endif
void demo_clock_lubanlite(char *out, size_t size)
{
#ifdef RT_USING_RTC
    rt_device_t device = rt_device_find("rtc");
    time_t seconds = 0;
    if (device && rt_device_control(device, RT_DEVICE_CTRL_RTC_GET_TIME, &seconds) == RT_EOK &&
        seconds > 0)
    {
#ifdef RT_LIBC_DEFAULT_TIMEZONE
        seconds += (time_t)RT_LIBC_DEFAULT_TIMEZONE * 3600;
#endif
        long rem = (long)(seconds % 86400);
        snprintf(out, size, "%02ld:%02ld", rem / 3600, rem / 60 % 60);
        return;
    }
#endif
    snprintf(out, size, "--:--");
}
static rt_device_t fb;
static struct aicfb_layer_data original;
static struct aicfb_alpha_config original_alpha;
static bool captured, applied;
static demo_ui_view applied_view;
bool demo_display_lubanlite(void *ctx, demo_ui_view view)
{
    (void)ctx;
    if (applied && applied_view == view)
        return true;
    if (!fb)
        fb = rt_device_find("aicfb");
    if (!fb)
        return false;
    struct aicfb_layer_data current = {.layer_id = AICFB_LAYER_TYPE_UI, .rect_id = 0};
    struct aicfb_alpha_config alpha = {.layer_id = AICFB_LAYER_TYPE_UI};
    if (rt_device_control(fb, AICFB_GET_LAYER_CONFIG, &current) < 0 ||
        rt_device_control(fb, AICFB_GET_ALPHA_CONFIG, &alpha) < 0)
        return false;
    if (!captured)
    {
        original = current;
        original_alpha = alpha;
        captured = true;
    }
    struct aicfb_layer_data next = current;
    next.pos = original.pos;
    next.scale_size = original.scale_size;
    next.buf.crop_en = original.buf.crop_en;
    next.buf.crop = original.buf.crop;
    struct aicfb_alpha_config next_alpha = original_alpha;
    if (view == DEMO_VIEW_FUSION)
    {
        if (original.buf.crop_en || original.pos.x || original.pos.y ||
            current.buf.size.width != 1024 || current.buf.size.height != 600)
            return false;
        next.pos.x = 768;
        next.pos.y = 0;
        next.buf.crop_en = 1;
        next.buf.crop.x = 768;
        next.buf.crop.y = 0;
        next.buf.crop.width = 256;
        next.buf.crop.height = 600;
        next_alpha.enable = 1;
        next_alpha.mode = AICFB_PIXEL_ALPHA_MODE;
        next_alpha.value = 255;
    }
    else if (view == DEMO_VIEW_FULLSCREEN)
    {
        next_alpha.enable = 1;
        next_alpha.mode = AICFB_GLOBAL_ALPHA_MODE;
        next_alpha.value = 0;
    }
    applied = false;
    if (rt_device_control(fb, AICFB_UPDATE_LAYER_CONFIG, &next) < 0)
        return false;
    if (rt_device_control(fb, AICFB_UPDATE_ALPHA_CONFIG, &next_alpha) < 0 ||
        rt_device_control(fb, AICFB_WAIT_FOR_VSYNC, NULL) < 0)
    {
        rt_device_control(fb, AICFB_UPDATE_LAYER_CONFIG, &current);
        rt_device_control(fb, AICFB_UPDATE_ALPHA_CONFIG, &alpha);
        return false;
    }
    applied = true;
    applied_view = view;
    return true;
}
