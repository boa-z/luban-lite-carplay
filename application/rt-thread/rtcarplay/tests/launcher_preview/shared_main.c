/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ui/demo_ui.c"
static uint8_t pixels[1024 * 600 * 4];
static demo_ui_status model;
static unsigned selections, touches, releases;
static uint16_t released_x;
static demo_ui_view display_mode;
static bool display_fail;
static lv_point_t finger;
static bool down;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *p)
{
    (void)a;
    (void)p;
    lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point = finger;
    d->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void poll(void *ctx, demo_ui_status *s)
{
    (void)ctx;
    *s = model;
}
static bool choose_protocol(void *ctx, demo_ui_protocol protocol)
{
    (void)ctx;
    assert(protocol == DEMO_PROTOCOL_LOCAL || model.supported[protocol]);
    ++selections;
    model.selected = protocol;
    return true;
}
static bool layout(void *ctx, bool fusion)
{
    (void)ctx;
    model.fusion = fusion;
    return true;
}
static int touch(void *ctx, uint8_t action, uint16_t x, uint16_t y)
{
    (void)ctx;
    assert(x < (model.fusion ? 768 : 1024) && y < 600);
    ++touches;
    if (action == 1)
    {
        ++releases;
        released_x = x;
    }
    return 0;
}
static bool display(void *ctx, demo_ui_view mode)
{
    (void)ctx;
    display_mode = mode;
    return !display_fail;
}
static void screenshot(const char *directory, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    lv_refr_now(NULL);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n1024 600\n255\n");
    for (unsigned i = 0; i < 1024 * 600; ++i)
    {
        uint8_t rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        assert(fwrite(rgb, 1, 3, f) == 3);
    }
    assert(fclose(f) == 0);
}
static void pump(lv_indev_t *indev, int x, int y, bool held)
{
    finger = (lv_point_t){x, y};
    down = held;
    lv_tick_inc(20);
    lv_indev_read(indev);
    lv_timer_handler();
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_display_t *d = lv_display_create(1024, 600);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(d, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_pointer);
    model.supported[DEMO_PROTOCOL_ANDROIDAUTO] = true;
    model.fusion = true;
    model.detail = "Select Android Auto. Other backends are not enabled in this app.";
    demo_ui_backend backend = {.poll = poll,
                               .select = choose_protocol,
                               .layout = layout,
                               .touch = touch,
                               .display = display};
    demo_ui_init(&backend);
    assert(lv_screen_active() == home);
    screenshot(argv[1], "dashboard-dark");
    lv_screen_load(connectivity);
    update(NULL);
    assert(lv_obj_has_state(protocol_buttons[DEMO_PROTOCOL_CARPLAY], LV_STATE_DISABLED));
    assert(lv_obj_has_state(protocol_buttons[DEMO_PROTOCOL_CARLIFE], LV_STATE_DISABLED));
    assert(!lv_obj_has_state(protocol_buttons[DEMO_PROTOCOL_ANDROIDAUTO], LV_STATE_DISABLED));
    lv_obj_send_event(protocol_buttons[DEMO_PROTOCOL_ANDROIDAUTO], LV_EVENT_CLICKED, NULL);
    assert(selections == 1 && model.selected == DEMO_PROTOCOL_ANDROIDAUTO);
    update(NULL);
    screenshot(argv[1], "connectivity-dark");
    model.error = -7;
    model.detail = "Provision client.pem, client.key and the 32-byte peer.sha256 pin";
    update(NULL);
    screenshot(argv[1], "credentials-error");
    carplay_ui_set_dark(false);
    update(NULL);
    screenshot(argv[1], "connectivity-light");
    lv_screen_load(settings);
    screenshot(argv[1], "settings-light");
    model.error = 0;
    model.connected = model.video = true;
    update(NULL);
    assert(display_mode == DEMO_VIEW_FUSION && lv_screen_active() == projection);
    screenshot(argv[1], "fusion-light");
    pump(indev, 100, 100, true);
    assert(pressed);
    pump(indev, 200, 100, true);
    pump(indev, 790, 100, true); /* Crossing into the local strip releases at x=200. */
    assert(!pressed && releases == 1 && released_x == 200);
    pump(indev, 790, 100, false);
    unsigned old = touches;
    pump(indev, 900, 100, true);
    pump(indev, 900, 100, false);
    assert(touches == old); /* Right-side local touches never reach the receiver. */
    pump(indev, 100, 100, true);
    assert(pressed);
    model.switching = true;
    update(NULL);
    assert(!pressed && releases == 2);
    pump(indev, 100, 100, false);
    assert(display_mode == DEMO_VIEW_LOCAL);
    model.switching = false;
    model.fusion = false;
    update(NULL);
    assert(display_mode == DEMO_VIEW_FULLSCREEN && lv_obj_has_flag(strip, LV_OBJ_FLAG_HIDDEN));
    model.video = false;
    update(NULL);
    assert(lv_screen_active() == connectivity);
    display_fail = true;
    update(NULL);
    assert(model.selected == DEMO_PROTOCOL_LOCAL);
    puts("shared UI: real LVGL rendering, routing, capture/release, layout and failure recovery "
         "PASS");
    return 0;
}
