/* SPDX-License-Identifier: GPL-3.0-only */
#include "androidauto_platform.h"
#include <rtthread.h>
#include <rthw.h>
#include <string.h>

/* Latest individual updates, not a fabricated track or merged phone state.
 * One receiver worker writes; the console takes a short coherent copy. */
typedef struct
{
    unsigned status_updates, metadata_updates;
    rtaa_media_status status;
    rtaa_media_metadata metadata;
} media_information;
static media_information latest;

static int status_update(void *ctx, const rtaa_media_status *value)
{
    (void)ctx;
    rt_base_t flags = rt_hw_interrupt_disable();
    latest.status = *value;
    ++latest.status_updates;
    rt_hw_interrupt_enable(flags);
    return 0;
}
static int metadata_update(void *ctx, const rtaa_media_metadata *value)
{
    (void)ctx;
    rt_base_t flags = rt_hw_interrupt_disable();
    latest.metadata = *value;
    ++latest.metadata_updates;
    rt_hw_interrupt_enable(flags);
    return 0;
}
static void reset(void *ctx)
{
    (void)ctx;
    rt_base_t flags = rt_hw_interrupt_disable();
    memset(&latest, 0, sizeof(latest));
    rt_hw_interrupt_enable(flags);
}
void androidauto_media_info_bind(rtaa_sink *sink)
{
    sink->media_status = status_update;
    sink->media_metadata = metadata_update;
    sink->media_info_reset = reset;
}
static int command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* Keep the bounded ~1 KiB snapshot off the small shell stack. */
    media_information *copy = rt_malloc(sizeof(*copy));
    if (!copy)
        return -RT_ENOMEM;
    rt_base_t flags = rt_hw_interrupt_disable();
    *copy = latest;
    rt_hw_interrupt_enable(flags);
    rt_kprintf("androidauto media: status_updates=%u metadata_updates=%u\n",
               copy->status_updates, copy->metadata_updates);
    const rtaa_media_status *s = &copy->status;
    const rtaa_media_metadata *m = &copy->metadata;
    if (s->has_state)
        rt_kprintf("state=%u (0 unknown, 1 stopped, 2 playing, 3 paused, 4 error)\n", s->state);
    if (s->has_source_app)
        rt_kprintf("source=%s\n", s->source_app);
    if (s->has_position_seconds)
        rt_kprintf("position_seconds=%u\n", s->position_seconds);
    if (s->has_shuffle)
        rt_kprintf("shuffle=%u\n", s->shuffle);
    if (s->has_repeat)
        rt_kprintf("repeat=%u\n", s->repeat);
    if (s->has_repeat_one)
        rt_kprintf("repeat_one=%u\n", s->repeat_one);
    if (m->has_title)
        rt_kprintf("title=%s\n", m->title);
    if (m->has_artist)
        rt_kprintf("artist=%s\n", m->artist);
    if (m->has_album)
        rt_kprintf("album=%s\n", m->album);
    rt_free(copy);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(command, androidauto_media, Latest Android Auto text media updates);
