/* SPDX-License-Identifier: GPL-3.0-only */
#include <aic_common.h>
#include "androidauto_platform.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <stdatomic.h>
#include <string.h>
#include "rng.h"
#ifdef RTANDROIDAUTO_VIDEO
#include "mpp_decoder.h"
#include "artinchip_fb.h"
#endif

int androidauto_random(void *ctx, unsigned char *out, size_t n)
{
    (void)ctx;
    if (!out || n > 0xffffu)
        return RTAA_EINVAL;
    if (aic_rng_get_bytes(out, (unsigned)n) == (int)n)
        return 0;
    memset(out, 0, n);
    return RTAA_EIO;
}
static atomic_int media_error;
static rtaa_geometry geometry = {1280, 720, 256, 120, 0, 0, 1024, 600};
static atomic_bool presented;
#ifdef RTANDROIDAUTO_VIDEO
#define VIDEO_QUEUE 8u
typedef struct
{
    uint8_t *data;
    size_t size;
    uint64_t us;
} video_packet;
static struct
{
    struct mpp_decoder *decoder;
    rt_device_t fb;
    rt_mutex_t lock;
    rt_sem_t wake;
    atomic_bool stop, running;
    video_packet queue[VIDEO_QUEUE];
    unsigned head, count;
    struct mpp_frame held, pending;
    bool have_held, have_pending;
} video;
static int present(const struct mpp_frame *frame)
{
    struct aicfb_layer_data layer = {0};
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer.enable = 1;
    layer.buf = frame->buf;
    int width = frame->buf.crop_en ? frame->buf.crop.width : frame->buf.size.width;
    int height = frame->buf.crop_en ? frame->buf.crop.height : frame->buf.size.height;
    if (width != geometry.frame_width || height != geometry.frame_height)
        return RTAA_ENOTSUP;
    layer.buf.crop.x = (frame->buf.crop_en ? frame->buf.crop.x : 0) + geometry.margin_width / 2;
    layer.buf.crop.y = (frame->buf.crop_en ? frame->buf.crop.y : 0) + geometry.margin_height / 2;
    layer.buf.crop.width = width - geometry.margin_width;
    layer.buf.crop.height = height - geometry.margin_height;
    layer.buf.crop_en = 1;
    layer.scale_size.width = geometry.viewport_width;
    layer.scale_size.height = geometry.viewport_height;
    layer.pos.x = geometry.viewport_x;
    layer.pos.y = geometry.viewport_y;
    if (rt_device_control(video.fb, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0 ||
        rt_device_control(video.fb, AICFB_WAIT_FOR_VSYNC, NULL) < 0)
        return RTAA_EIO;
    presented = true;
    return 0;
}
static void pump_video(void)
{
    if (mpp_decoder_decode(video.decoder) < DEC_OK)
    {
        media_error = RTAA_EIO;
        return;
    }
    for (unsigned i = 0; i < 8; ++i)
    {
        struct mpp_frame frame = {0};
        if (mpp_decoder_get_frame(video.decoder, &frame) != DEC_OK)
            break;
        int rc = present(&frame);
        if (rc)
        {
            /* A failed vsync may already have scheduled this buffer. Retain
             * both old and new scanout until disable+vsync succeeds. */
            video.pending = frame;
            video.have_pending = true;
            media_error = rc;
            return;
        }
        if (video.have_held)
            mpp_decoder_put_frame(video.decoder, &video.held);
        video.held = frame;
        video.have_held = true;
    }
}
static void video_worker(void *unused)
{
    (void)unused;
    while (!video.stop && !media_error)
    {
        video_packet p = {0};
        rt_mutex_take(video.lock, RT_WAITING_FOREVER);
        if (video.count)
        {
            p = video.queue[video.head];
            video.head = (video.head + 1) % VIDEO_QUEUE;
            --video.count;
        }
        rt_mutex_release(video.lock);
        if (p.data)
        {
            struct mpp_packet packet = {0};
            int rc = -1;
            for (unsigned retry = 0; retry < 100 && !video.stop && !media_error; ++retry)
            {
                rc = mpp_decoder_get_packet(video.decoder, &packet, (int)p.size);
                if (rc == DEC_OK)
                    break;
                pump_video();
                rt_thread_mdelay(1);
            }
            if (rc == DEC_OK)
            {
                memcpy(packet.data, p.data, p.size);
                packet.size = p.size;
                packet.pts = p.us;
                if (mpp_decoder_put_packet(video.decoder, &packet) != DEC_OK)
                    media_error = RTAA_EIO;
            }
            else if (!video.stop)
                media_error = RTAA_ETIMEOUT;
            rt_free(p.data);
        }
        if (!media_error)
            pump_video();
        if (!p.data)
            rt_sem_take(video.wake, rt_tick_from_millisecond(5));
    }
    video.running = false;
}
static int stop_video(void)
{
    if (!video.decoder)
        return 0;
    video.stop = true;
    rt_sem_release(video.wake);
    uint32_t start = rt_tick_get_millisecond();
    while (video.running && (uint32_t)(rt_tick_get_millisecond() - start) < 3000)
        rt_thread_mdelay(5);
    if (video.running)
        return RTAA_ETIMEOUT;
    struct aicfb_layer_data layer = {0};
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    if (rt_device_control(video.fb, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0 ||
        rt_device_control(video.fb, AICFB_WAIT_FOR_VSYNC, NULL) < 0)
        return RTAA_EIO;
    if (video.have_held)
        mpp_decoder_put_frame(video.decoder, &video.held);
    if (video.have_pending)
        mpp_decoder_put_frame(video.decoder, &video.pending);
    while (video.count)
    {
        rt_free(video.queue[video.head].data);
        video.head = (video.head + 1) % VIDEO_QUEUE;
        --video.count;
    }
    mpp_decoder_destory(video.decoder);
    rt_sem_delete(video.wake);
    rt_mutex_delete(video.lock);
    memset(&video, 0, sizeof(video));
    presented = false;
    return 0;
}
static int start_video(void)
{
    if (video.decoder)
        return RTAA_ESTATE;
    video.fb = rt_device_find("aicfb");
    if (!video.fb)
        return RTAA_EIO;
    video.decoder = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_H264);
    if (!video.decoder)
        return RTAA_EBOUNDS;
    struct decode_config config = {0};
    config.pix_fmt = MPP_FMT_NV12;
    config.bitstream_buffer_size = 1024 * 1024;
    config.packet_count = 2;
    config.extra_frame_num = 2;
    if (mpp_decoder_init(video.decoder, &config) != DEC_OK)
        goto failed;
    video.lock = rt_mutex_create("aa-video", RT_IPC_FLAG_PRIO);
    video.wake = rt_sem_create("aa-video", 0, RT_IPC_FLAG_PRIO);
    if (!video.lock || !video.wake)
        goto failed;
    rt_thread_t thread = rt_thread_create("aa-video", video_worker, NULL, 16384, 14, 10);
    if (!thread)
        goto failed;
    video.running = true;
    if (rt_thread_startup(thread) == RT_EOK)
        return 0;
    video.running = false;
    rt_thread_delete(thread);
failed:
    if (video.lock)
        rt_mutex_delete(video.lock);
    if (video.wake)
        rt_sem_delete(video.wake);
    mpp_decoder_destory(video.decoder);
    memset(&video, 0, sizeof(video));
    return RTAA_EIO;
}
static int video_data(const uint8_t *data, size_t n, uint64_t us)
{
    if (!video.decoder || !video.running || media_error)
        return RTAA_EIO;
    if (!n || n > 512 * 1024)
        return RTAA_EBOUNDS;
    uint8_t *copy = rt_malloc(n);
    if (!copy)
        return RTAA_EBOUNDS;
    memcpy(copy, data, n);
    if (rt_mutex_take(video.lock, rt_tick_from_millisecond(10)) != RT_EOK)
    {
        rt_free(copy);
        return RTAA_ETIMEOUT;
    }
    int rc = RTAA_EBOUNDS;
    if (video.count < VIDEO_QUEUE)
    {
        video.queue[(video.head + video.count++) % VIDEO_QUEUE] = (video_packet){copy, n, us};
        rc = 0;
    }
    rt_mutex_release(video.lock);
    if (rc)
        rt_free(copy);
    else
        rt_sem_release(video.wake);
    return rc;
}
#endif

#ifdef RTANDROIDAUTO_AUDIO
#define PCM_SAMPLES 32768u
static struct
{
    rt_device_t dev;
    rt_mutex_t lock;
    rt_sem_t wake;
    atomic_bool stop, running, duck;
    unsigned active;
    struct
    {
        int16_t data[PCM_SAMPLES];
        unsigned head, count;
    } lanes[3];
} audio;
static void audio_worker(void *unused)
{
    (void)unused;
    int16_t pcm[960]; /* 10 ms at 48 kHz, stereo */
    while (!audio.stop && !media_error)
    {
        bool any = false;
        rt_mutex_take(audio.lock, RT_WAITING_FOREVER);
        for (unsigned i = 0; i < 960; ++i)
        {
            int sum = 0;
            for (unsigned lane = 0; lane < 3; ++lane)
            {
                if (!audio.lanes[lane].count)
                    continue;
                int sample = audio.lanes[lane].data[audio.lanes[lane].head];
                audio.lanes[lane].head = (audio.lanes[lane].head + 1) % PCM_SAMPLES;
                --audio.lanes[lane].count;
                if (!lane && audio.duck)
                    sample /= 4;
                sum += sample;
                any = true;
            }
            pcm[i] = (int16_t)(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
        }
        rt_mutex_release(audio.lock);
        if (any)
        {
            size_t offset = 0;
            while (offset < sizeof(pcm) && !audio.stop)
            {
                rt_size_t n =
                    rt_device_write(audio.dev, 0, (uint8_t *)pcm + offset, sizeof(pcm) - offset);
                if (!n || n > sizeof(pcm) - offset)
                {
                    media_error = RTAA_EIO;
                    break;
                }
                offset += n;
            }
        }
        else
            rt_sem_take(audio.wake, rt_tick_from_millisecond(10));
    }
    audio.running = false;
}
static int stop_audio(void)
{
    if (!audio.dev)
        return 0;
    audio.stop = true;
    rt_sem_release(audio.wake);
    uint32_t start = rt_tick_get_millisecond();
    while (audio.running && (uint32_t)(rt_tick_get_millisecond() - start) < 5000)
        rt_thread_mdelay(5);
    if (audio.running)
        return RTAA_ETIMEOUT;
    int stream = AUDIO_STREAM_REPLAY;
    if (rt_device_control(audio.dev, AUDIO_CTL_STOP, &stream) != RT_EOK ||
        rt_device_close(audio.dev) != RT_EOK)
        return RTAA_EIO;
    rt_mutex_delete(audio.lock);
    rt_sem_delete(audio.wake);
    memset(&audio, 0, sizeof(audio));
    return 0;
}
static int start_audio(void)
{
    if (audio.dev)
        return audio.running ? 0 : RTAA_ESTATE;
    rt_device_t dev = rt_device_find("sound0");
    if (!dev || rt_device_open(dev, RT_DEVICE_OFLAG_WRONLY) != RT_EOK)
        return RTAA_EIO;
    struct rt_audio_caps caps = {0};
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = 48000;
    caps.udata.config.channels = 2;
    caps.udata.config.samplebits = 16;
    if (rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
    {
        rt_device_close(dev);
        return RTAA_ENOTSUP;
    }
    audio.lock = rt_mutex_create("aa-audio", RT_IPC_FLAG_PRIO);
    audio.wake = rt_sem_create("aa-audio", 0, RT_IPC_FLAG_PRIO);
    if (!audio.lock || !audio.wake)
        goto failed;
    rt_thread_t thread = rt_thread_create("aa-audio", audio_worker, NULL, 8192, 13, 10);
    if (!thread)
        goto failed;
    audio.dev = dev;
    audio.running = true;
    if (rt_thread_startup(thread) == RT_EOK)
        return 0;
    audio.running = false;
    rt_thread_delete(thread);
failed:
    if (audio.lock)
        rt_mutex_delete(audio.lock);
    if (audio.wake)
        rt_sem_delete(audio.wake);
    rt_device_close(dev);
    memset(&audio, 0, sizeof(audio));
    return RTAA_EIO;
}
static int audio_data(rtaa_role role, const uint8_t *data, size_t n)
{
    unsigned lane = role - RTAA_MEDIA;
    unsigned expand = role == RTAA_MEDIA ? 1 : 6;
    if (!audio.running || media_error || !(audio.active & (1u << lane)))
        return RTAA_ESTATE;
    if (n % (role == RTAA_MEDIA ? 4 : 2) || n / 2 > PCM_SAMPLES / expand)
        return RTAA_EBOUNDS;
    if (rt_mutex_take(audio.lock, rt_tick_from_millisecond(10)) != RT_EOK)
        return RTAA_ETIMEOUT;
    int rc = 0;
    unsigned samples = (unsigned)(n / 2 * expand);
    if (samples > PCM_SAMPLES - audio.lanes[lane].count)
        rc = RTAA_EBOUNDS;
    else
        for (size_t i = 0; i < n; i += 2)
        {
            int16_t value = (int16_t)((unsigned)data[i] | (unsigned)data[i + 1] << 8);
            for (unsigned j = 0; j < expand; ++j)
            {
                unsigned pos = (audio.lanes[lane].head + audio.lanes[lane].count++) % PCM_SAMPLES;
                audio.lanes[lane].data[pos] = value;
            }
        }
    rt_mutex_release(audio.lock);
    if (!rc)
        rt_sem_release(audio.wake);
    return rc;
}
#endif
static int sink_start(void *ctx, rtaa_role role, uint32_t rate, uint8_t channels, uint8_t bits)
{
    (void)ctx;
    (void)rate;
    (void)channels;
    (void)bits;
    if (media_error)
        return media_error;
#ifdef RTANDROIDAUTO_VIDEO
    if (role == RTAA_VIDEO)
        return start_video();
#endif
#ifdef RTANDROIDAUTO_AUDIO
    if (role >= RTAA_MEDIA && role <= RTAA_SPEECH)
    {
        int rc = start_audio();
        if (!rc)
            audio.active |= 1u << (role - RTAA_MEDIA);
        return rc;
    }
#endif
    return RTAA_ENOTSUP;
}
static int sink_data(void *ctx, rtaa_role role, const uint8_t *data, size_t n, uint64_t us)
{
    (void)ctx;
    (void)data;
    (void)n;
    (void)us;
#ifdef RTANDROIDAUTO_VIDEO
    if (role == RTAA_VIDEO)
        return video_data(data, n, us);
#endif
#ifdef RTANDROIDAUTO_AUDIO
    if (role >= RTAA_MEDIA && role <= RTAA_SPEECH)
        return audio_data(role, data, n);
#endif
    return RTAA_ENOTSUP;
}
static void sink_stop(void *ctx, rtaa_role role)
{
    (void)ctx;
    int rc = 0;
#ifdef RTANDROIDAUTO_VIDEO
    if (role == RTAA_VIDEO)
        rc = stop_video();
#endif
#ifdef RTANDROIDAUTO_AUDIO
    if (role >= RTAA_MEDIA && role <= RTAA_SPEECH && audio.dev)
    {
        unsigned lane = role - RTAA_MEDIA;
        rt_mutex_take(audio.lock, RT_WAITING_FOREVER);
        audio.active &= ~(1u << lane);
        audio.lanes[lane].count = 0;
        rt_mutex_release(audio.lock);
        if (!audio.active)
            rc = stop_audio();
    }
#endif
    if (rc)
        media_error = rc;
}
static int audio_focus(void *ctx, unsigned request)
{
    (void)ctx;
#ifdef RTANDROIDAUTO_AUDIO
    audio.duck = request == 3;
#endif
    return request == 4 ? 3 : 1;
}
static int video_focus(void *ctx, bool focused)
{
    (void)ctx;
    /* This development profile cannot release/reacquire display mid-stream. */
    return focused ? 0 : RTAA_ENOTSUP;
}
static int navigation_focus(void *ctx, unsigned requested)
{
    (void)ctx;
    /* This demo has no native navigation owner to preempt. Grant projected
     * navigation or acknowledge its release; a native navigator must replace
     * this policy with actual resource arbitration when added. */
    return (int)requested;
}
rtaa_sink androidauto_media_sink(void)
{
    rtaa_sink sink = {0};
    sink.start = sink_start;
    sink.data = sink_data;
    sink.stop = sink_stop;
    sink.audio_focus = audio_focus;
    sink.video_focus = video_focus;
    sink.navigation_focus = navigation_focus;
    return sink;
}
bool androidauto_media_retired(void)
{
#ifdef RTANDROIDAUTO_VIDEO
    if (video.decoder)
        return false;
#endif
#ifdef RTANDROIDAUTO_AUDIO
    if (audio.dev)
        return false;
#endif
    return true;
}
int androidauto_media_error(void)
{
    return media_error;
}
int androidauto_media_retire(void)
{
    int rc = 0;
#ifdef RTANDROIDAUTO_VIDEO
    rc = stop_video();
#endif
#ifdef RTANDROIDAUTO_AUDIO
    int audio_rc = stop_audio();
    if (!rc)
        rc = audio_rc;
#endif
    return rc;
}
bool androidauto_media_presented(void)
{
    return presented;
}
int androidauto_media_set_geometry(const rtaa_geometry *g)
{
    if (!androidauto_media_retired() || rtaa_geometry_validate(g))
        return RTAA_ESTATE;
    geometry = *g;
    media_error = 0;
    return 0;
}
