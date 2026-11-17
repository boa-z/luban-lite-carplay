/* SPDX-License-Identifier: GPL-3.0-only */
#include "androidauto_app.h"
#include "androidauto_platform.h"
#include <rtthread.h>
#include <rthw.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

typedef struct
{
    uint8_t action;
    uint16_t x, y;
} touch;
static atomic_bool armed, fusion, input_lost;
static atomic_bool retired;
static atomic_uint requests;
static rt_mq_t inputs;
static rt_thread_t worker;
static androidauto_status snapshot;
static uint8_t certificate[8192], key[8192], peer_pin[32];
static size_t certificate_size, key_size;
static int load_file(const char *name, uint8_t *out, size_t capacity, size_t *size, bool pem)
{
    char path[192];
    int n = snprintf(path, sizeof(path), "%s/%s", RTANDROIDAUTO_CREDENTIAL_DIR, name);
    if (n < 0 || (size_t)n >= sizeof(path))
        return RTAA_EBOUNDS;
    FILE *file = fopen(path, "rb");
    if (!file)
        return RTAA_ESECURITY;
    *size = fread(out, 1, capacity - (pem ? 1 : 0), file);
    int extra = fgetc(file);
    bool failed = ferror(file) || extra != EOF || !*size;
    fclose(file);
    if (failed)
        return RTAA_ESECURITY;
    if (pem)
        out[(*size)++] = 0;
    return 0;
}
static void erase_credentials(void)
{
    volatile uint8_t *p = key;
    for (size_t i = 0; i < sizeof(key); ++i)
        p[i] = 0;
    memset(certificate, 0, sizeof(certificate));
    memset(peer_pin, 0, sizeof(peer_pin));
    certificate_size = key_size = 0;
}
static int credentials(void)
{
    erase_credentials();
    size_t size;
    int rc = load_file("client.pem", certificate, sizeof(certificate), &certificate_size, true);
    if (!rc)
        rc = load_file("client.key", key, sizeof(key), &key_size, true);
    if (!rc)
        rc = load_file("peer.sha256", peer_pin, sizeof(peer_pin), &size, false);
    if (!rc && size != sizeof(peer_pin))
        rc = RTAA_ESECURITY;
    if (rc)
        erase_credentials();
    return rc;
}
static int verify_pin(void *ctx, const uint8_t hash[32])
{
    (void)ctx;
    unsigned difference = 0;
    for (unsigned i = 0; i < 32; ++i)
        difference |= hash[i] ^ peer_pin[i];
    return difference ? RTAA_ESECURITY : 0;
}
static void publish(rtaa_receiver *r, unsigned attempts, int error, bool applied_fusion)
{
    androidauto_status next = {0};
    next.armed = armed;
    next.usb = androidauto_usb_present();
    next.video = androidauto_media_presented();
    next.fusion = applied_fusion;
    next.attempts = attempts;
    next.error = error;
    next.state = r ? rtaa_receiver_state(r) : error ? RTAA_FAILED : RTAA_IDLE;
    if (r)
        rtaa_receiver_statistics(r, &next.statistics);
    rt_base_t flags = rt_hw_interrupt_disable();
    snapshot = next;
    rt_hw_interrupt_enable(flags);
}
static int create_receiver(rtaa_receiver **out, uint32_t *generation, bool mode)
{
    *out = NULL;
    if (!androidauto_media_retired())
        return RTAA_ESTATE;
    int rc = credentials();
    if (rc)
        return rc;
    rtaa_receiver_config c = {0};
    c.protocol_minor = RTANDROIDAUTO_PROTOCOL_MINOR;
    c.memory = rtaa_rtthread_allocator();
    c.tls.memory = c.memory;
    c.tls.certificate = certificate;
    c.tls.certificate_size = certificate_size;
    c.tls.private_key = key;
    c.tls.private_key_size = key_size;
    c.tls.random = androidauto_random;
    c.tls.peer_fingerprint = verify_pin;
    c.geometry = (rtaa_geometry){1280, 720, mode ? 512 : 256, 120, 0, 0, mode ? 768 : 1024, 600};
    rc = androidauto_media_set_geometry(&c.geometry);
    if (rc)
        return rc;
    c.sink = androidauto_media_sink();
#ifdef RTANDROIDAUTO_VIDEO
    c.channels[RTAA_VIDEO] = (rtaa_channel_config){1, 512 * 1024};
#endif
#ifdef RTANDROIDAUTO_AUDIO
    c.channels[RTAA_MEDIA] = (rtaa_channel_config){3, 32768};
    c.channels[RTAA_SYSTEM] = (rtaa_channel_config){5, 32768};
    c.channels[RTAA_SPEECH] = (rtaa_channel_config){7, 32768};
#endif
    c.channels[RTAA_INPUT] = (rtaa_channel_config){9, 4096};
    c.channels[RTAA_SENSOR] = (rtaa_channel_config){11, 4096};
    c.channels[RTAA_MEDIA_INFO] = (rtaa_channel_config){13, 32768};
    c.channels[RTAA_NAVIGATION] = (rtaa_channel_config){15, 32768};
    c.channels[RTAA_PHONE] = (rtaa_channel_config){17, 32768};
    /* Mic stays unadvertised until a bounded capture/AEC adapter exists. */
    c.name = "Luban-Lite Android Auto";
    c.manufacturer = "boa-z";
    c.model = "D213 development demo";
    c.io_timeout_ms = 20;
    c.session_timeout_ms = 5000;
    c.heartbeat_ms = 1000;
    c.memory_limit = 2 * 1024 * 1024;
    rc = androidauto_usb_open(&c.transport, generation);
    if (rc)
        return rc;
    rc = rtaa_receiver_create(&c, out);
    if (rc)
        c.transport.close(c.transport.ctx);
    else
        rc = rtaa_receiver_start(*out);
    return rc;
}
static void run(void *unused)
{
    (void)unused;
    rtaa_receiver *r = NULL;
    unsigned attempts = 0, seen_request = requests;
    uint32_t generation = 0, retry_at = 0;
    bool mode = false, finger_down = false;
    int error = 0;
    for (;;)
    {
        uint32_t now = rt_tick_get_millisecond();
        if (requests != seen_request)
        {
            seen_request = requests;
            attempts = 0;
            error = 0;
            retry_at = now;
        }
        bool changed = r && (mode != fusion || !armed || input_lost ||
                             generation != androidauto_usb_generation());
        if (changed)
        {
            if (rtaa_receiver_state(r) == RTAA_CONNECTED && rtaa_receiver_shutdown(r) == RTAA_OK)
            {
                uint32_t started = rt_tick_get_millisecond();
                while (rtaa_receiver_state(r) == RTAA_CLOSING &&
                       (uint32_t)(rt_tick_get_millisecond() - started) < 200)
                    if (rtaa_receiver_poll(r) < 0)
                        break;
            }
            rtaa_receiver_destroy(r);
            r = NULL;
            finger_down = false;
            erase_credentials();
            touch ignored;
            while (rt_mq_recv(inputs, &ignored, sizeof(ignored), 0) == RT_EOK)
            {
            }
            if (input_lost)
            {
                error = RTAA_EBOUNDS;
                armed = false;
                input_lost = false;
            }
            attempts = 0;
        }
        if (!r && !androidauto_media_retired())
        {
            int cleanup = androidauto_media_retire();
            if (cleanup)
                error = cleanup;
        }
        bool available = !r && androidauto_media_retired();
        if (!r && armed && androidauto_usb_present() && attempts < 3 && available &&
            (int32_t)(now - retry_at) >= 0)
        {
            retired = false;
            mode = fusion;
            touch ignored;
            while (rt_mq_recv(inputs, &ignored, sizeof(ignored), 0) == RT_EOK)
            {
            }
            ++attempts;
            error = create_receiver(&r, &generation, mode);
            if (error && r)
            {
                rtaa_receiver_destroy(r);
                r = NULL;
            }
            if (!r)
                erase_credentials();
            if (error == RTAA_AGAIN)
            {
                --attempts;
                error = 0;
            }
            else if (error && error != RTAA_EIO && error != RTAA_ETIMEOUT)
                armed = false;
            retry_at = now + (1000u << attempts);
        }
        if (r)
        {
            int rc = rtaa_receiver_poll(r);
            if (rc >= 0 && rtaa_receiver_state(r) == RTAA_CONNECTED)
            {
                /* The diagnostic consumer is ready before discovery. Retry
                 * until this optional service opens; repeat starts are local
                 * no-ops, and a close/reopen requires a fresh start. */
                int nav = rtaa_receiver_navigation(r, true);
                if (nav != RTAA_ESTATE && nav < 0)
                    rc = nav;
            }
            touch point;
            while (rc >= 0 && rt_mq_recv(inputs, &point, sizeof(point), 0) == RT_EOK)
            {
                if (rtaa_receiver_state(r) != RTAA_CONNECTED)
                    continue;
                if ((point.action == 0 && finger_down) || (point.action != 0 && !finger_down))
                    continue;
                int result = rtaa_receiver_touch(r, point.action, 0, point.x, point.y,
                                                 (uint64_t)rt_tick_get_millisecond() * 1000);
                if (!result)
                    finger_down = point.action != 1;
                else if (result == RTAA_ESTATE)
                    finger_down = false;
                /* Local input may arrive before the peer binds its channel. */
                if (result != RTAA_ESTATE && result != RTAA_EBOUNDS)
                    rc = result;
            }
            if (androidauto_media_error())
                rc = androidauto_media_error();
            if (rc < 0 || rtaa_receiver_state(r) == RTAA_CLOSED)
            {
                error = rc < 0 ? rc : 0;
                rtaa_receiver_destroy(r);
                r = NULL;
                finger_down = false;
                erase_credentials();
                if (!error || (error != RTAA_EIO && error != RTAA_ETIMEOUT))
                    armed = false;
                retry_at = now + (1000u << attempts);
            }
        }
        if (!androidauto_usb_present() && !r)
            attempts = 0;
        publish(r, attempts, error, mode);
        rt_base_t flags = rt_hw_interrupt_disable();
        retired = !armed && requests == seen_request && !r && androidauto_media_retired();
        rt_hw_interrupt_enable(flags);
        rt_thread_mdelay(5);
    }
}
int androidauto_app_init(void)
{
    if (worker)
        return 0;
    inputs = rt_mq_create("aa-input", sizeof(touch), 32, RT_IPC_FLAG_FIFO);
    if (!inputs)
        return RTAA_EBOUNDS;
    worker = rt_thread_create("aa-session", run, NULL, 32768, 16, 10);
    if (!worker)
    {
        rt_mq_delete(inputs);
        inputs = NULL;
        return RTAA_EBOUNDS;
    }
#ifdef RTANDROIDAUTO_AUTO_START
    armed = true;
#endif
    int rc = rt_thread_startup(worker);
    if (rc != RT_EOK)
    {
        rt_thread_delete(worker);
        worker = NULL;
        rt_mq_delete(inputs);
        inputs = NULL;
    }
    return rc == RT_EOK ? 0 : RTAA_EIO;
}
void androidauto_app_enable(bool enable)
{
    rt_base_t flags = rt_hw_interrupt_disable();
    retired = false;
    armed = enable;
    ++requests;
    rt_hw_interrupt_enable(flags);
}
bool androidauto_app_retired(void)
{
    return retired && !armed;
}
void androidauto_app_set_fusion(bool value)
{
    fusion = value;
    ++requests;
}
void androidauto_app_status(androidauto_status *out)
{
    if (!out)
        return;
    rt_base_t flags = rt_hw_interrupt_disable();
    *out = snapshot;
    rt_hw_interrupt_enable(flags);
}
int androidauto_app_touch(uint8_t action, uint16_t x, uint16_t y)
{
    touch point = {action, x, y};
    if (!inputs || action > 2)
        return RTAA_EINVAL;
    if (rt_mq_send(inputs, &point, sizeof(point)) == RT_EOK)
        return 0;
    input_lost = true;
    return RTAA_EBOUNDS;
}
static int command(int argc, char **argv)
{
#ifdef RTPROJECTION_DEMO
    extern int projection_demo_command(int, char **);
    return projection_demo_command(argc, argv);
#else
    if (argc == 2 && !strcmp(argv[1], "start"))
        androidauto_app_enable(true);
    else if (argc == 2 && !strcmp(argv[1], "stop"))
        androidauto_app_enable(false);
    else if (argc == 2 && !strcmp(argv[1], "fusion"))
        androidauto_app_set_fusion(true);
    else if (argc == 2 && !strcmp(argv[1], "fullscreen"))
        androidauto_app_set_fusion(false);
    else if (argc == 2 && !strcmp(argv[1], "status"))
    {
        androidauto_status s;
        androidauto_app_status(&s);
        rt_kprintf(
            "androidauto: armed=%u usb=%u state=%u error=%d attempts=%u video=%u fusion=%u\n",
            s.armed, s.usb, s.state, s.error, s.attempts, s.video, s.fusion);
    }
    else
        rt_kprintf("androidauto start|stop|status|fullscreen|fusion\n");
    return 0;
#endif
}
MSH_CMD_EXPORT_ALIAS(command, androidauto, Android Auto receiver owner);
