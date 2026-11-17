/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Protocol-independent pages, reusing the rtcarplay Mobility Cockpit palette,
 * fonts, Lucide assets and language registry. Backends own every side effect. */
#include "demo_ui.h"
#include "demo_dashboard.h"
#include "lvgl.h"
#include "carplay_ui_theme.h"
#include "ui_language.h"
#include "icons/carplay_icons.h"
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(carplay_ui_34);
LV_FONT_DECLARE(carplay_ui_21);
LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_14);
LV_FONT_DECLARE(carplay_display_110);
static demo_ui_backend host;
static demo_ui_status status;
static lv_obj_t *home, *connectivity, *settings, *projection;
static lv_obj_t *state_label, *detail_label, *traffic_label;
static demo_dashboard dashboard;
static lv_obj_t *protocol_buttons[DEMO_PROTOCOL_COUNT], *layout_buttons[2];
static lv_obj_t *strip, *hit_plane, *hint;
static bool pressed, blocked, home_after_stop;
static uint16_t last_x, last_y;
static demo_ui_view view;
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, carplay_style_role_t role)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 20, 0);
    carplay_ui_style(obj, role);
    return obj;
}
static lv_obj_t *label(lv_obj_t *parent, const char *value, int x, int y, int w,
                       const lv_font_t *font, carplay_style_role_t role)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(obj, font, 0);
    carplay_ui_style(obj, role);
    carplay_ui_set_text(obj, value);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *value, int x, int y, int w, int h,
                        lv_event_cb_t callback, uintptr_t data)
{
    lv_obj_t *obj = box(parent, x, y, w, h, CARPLAY_STYLE_CARD);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, (void *)data);
    lv_obj_t *text = label(obj, value, 0, 0, w - 16, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(text);
    return obj;
}
static void icon(lv_obj_t *parent, const lv_image_dsc_t *asset, int x, int y)
{
    lv_obj_t *obj = lv_image_create(parent);
    lv_image_set_src(obj, asset);
    lv_obj_set_pos(obj, x, y);
    carplay_ui_style(obj, CARPLAY_STYLE_ICON_ACCENT);
}
static void cancel_touch(void)
{
    if (pressed && host.touch)
        host.touch(host.ctx, 1, last_x, last_y);
    pressed = false;
    blocked = true;
}
static void open_page(lv_event_t *event)
{
    unsigned page = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    lv_screen_load(page == 1 ? connectivity : page == 2 ? settings : home);
}
static void dashboard_connectivity(lv_event_t *event)
{
    (void)event;
    lv_screen_load(connectivity);
}
static void dashboard_settings(lv_event_t *event)
{
    (void)event;
    lv_screen_load(settings);
}
static void select_protocol(lv_event_t *event)
{
    demo_ui_protocol protocol = (demo_ui_protocol)(uintptr_t)lv_event_get_user_data(event);
    cancel_touch();
    if (!host.select(host.ctx, protocol))
        carplay_ui_set_text(hint, "Not supported on this device");
}
static void select_layout(lv_event_t *event)
{
    bool fusion = (uintptr_t)lv_event_get_user_data(event) != 0;
    cancel_touch();
    if (!host.layout(host.ctx, fusion))
        carplay_ui_set_text(hint, "Layout change failed");
}
static void stop_projection(lv_event_t *event)
{
    home_after_stop = (uintptr_t)lv_event_get_user_data(event) != 0;
    cancel_touch();
    host.select(host.ctx, DEMO_PROTOCOL_LOCAL);
}
static void theme(lv_event_t *event)
{
    (void)event;
    carplay_ui_set_dark(!carplay_ui_dark());
}
static void language(lv_event_t *event)
{
    (void)event;
    carplay_ui_set_chinese(!carplay_ui_is_chinese());
}
static void slider(lv_event_t *event)
{
    unsigned value = (unsigned)lv_slider_get_value(lv_event_get_target_obj(event));
    bool brightness = (uintptr_t)lv_event_get_user_data(event) != 0;
    bool ok = brightness ? host.brightness_set && host.brightness_set(value)
                         : host.volume_set && host.volume_set(value);
    carplay_ui_set_text(hint, ok ? "Settings saved." : "Not supported on this device");
}
static void build_slider(lv_obj_t *parent, const char *caption, int y, bool brightness)
{
    box(parent, 64, y, 896, 86, CARPLAY_STYLE_CARD);
    label(parent, caption, 88, y + 28, 220, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_t *obj = lv_slider_create(parent);
    lv_obj_set_pos(obj, 340, y + 35);
    lv_obj_set_size(obj, 570, 12);
    lv_slider_set_range(obj, brightness ? 5 : 0, 100);
    unsigned value = 0;
    bool ok = brightness ? host.brightness_get && host.brightness_get(&value)
                         : host.volume_get && host.volume_get(&value);
    if (!ok)
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    else
        lv_slider_set_value(obj, (int)value, LV_ANIM_OFF);
    carplay_ui_style_part(obj, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(obj, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);
    carplay_ui_style_part(obj, CARPLAY_STYLE_SLIDER_KNOB, LV_PART_KNOB);
    lv_obj_add_event_cb(obj, slider, LV_EVENT_RELEASED, (void *)(uintptr_t)brightness);
}
static lv_obj_t *page(const char *caption, bool back)
{
    lv_obj_t *obj = lv_obj_create(NULL);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    carplay_ui_style(obj, CARPLAY_STYLE_SCREEN);
    if (back)
    {
        button(obj, "", 64, 35, 48, 42, open_page, 0);
        icon(obj, &carplay_icon_arrow_left_22, 77, 45);
    }
    label(obj, caption, 64, back ? 92 : 48, 896, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    return obj;
}
static void pointer(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev || view == DEMO_VIEW_LOCAL)
        return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    unsigned width = view == DEMO_VIEW_FUSION ? 768 : 1024;
    lv_event_code_t code = lv_event_get_code(event);
    bool inside = point.x >= 0 && point.y >= 0 && point.x < (int)width && point.y < 600;
    if (code == LV_EVENT_PRESSED)
    {
        blocked = !inside;
        if (!blocked)
        {
            last_x = (uint16_t)point.x;
            last_y = (uint16_t)point.y;
            pressed = host.touch && host.touch(host.ctx, 0, last_x, last_y) == 0;
        }
    }
    else if (code == LV_EVENT_PRESSING && pressed && !blocked)
    {
        if (!inside)
        {
            cancel_touch();
            return;
        }
        if (last_x != point.x || last_y != point.y)
        {
            last_x = (uint16_t)point.x;
            last_y = (uint16_t)point.y;
            if (host.touch(host.ctx, 2, last_x, last_y))
                cancel_touch();
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        cancel_touch();
        blocked = false;
    }
}
static void update(lv_timer_t *timer)
{
    (void)timer;
    host.poll(host.ctx, &status);
    demo_ui_view next = status.video && !status.switching
                            ? status.fusion ? DEMO_VIEW_FUSION : DEMO_VIEW_FULLSCREEN
                            : DEMO_VIEW_LOCAL;
    if (next != view)
        cancel_touch();
    bool display_ok = host.display(host.ctx, next);
    if (!display_ok)
    {
        /* Hide projection only after hardware composition can be restored. */
        host.select(host.ctx, DEMO_PROTOCOL_LOCAL);
        next = DEMO_VIEW_LOCAL;
        (void)host.display(host.ctx, next);
    }
    if (next != view)
    {
        view = next;
        if (view == DEMO_VIEW_LOCAL)
        {
            lv_screen_load(home_after_stop ? home : connectivity);
            home_after_stop = false;
        }
        else
        {
            lv_obj_set_width(hit_plane, view == DEMO_VIEW_FUSION ? 768 : 1024);
            if (view == DEMO_VIEW_FUSION)
                lv_obj_remove_flag(strip, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(strip, LV_OBJ_FLAG_HIDDEN);
            lv_screen_load(projection);
        }
    }
    if (view != DEMO_VIEW_LOCAL)
        return;
    const char *state = status.switching                         ? "Releasing previous session"
                        : status.error                           ? "Connection failed"
                        : status.connected                       ? "Connected"
                        : status.selected == DEMO_PROTOCOL_LOCAL ? "Not connected"
                                                                 : "Waiting for phone";
    carplay_ui_set_text(state_label, state);
    carplay_ui_set_text(dashboard.status, status.connected ? "Connected" : "Not connected");
    carplay_ui_style(dashboard.dot,
                     status.connected ? CARPLAY_STYLE_DOT_ON : CARPLAY_STYLE_DOT_OFF);
    char clock[16] = "--:--";
    if (host.clock_get)
        host.clock_get(clock, sizeof(clock));
    carplay_ui_set_text(dashboard.clock, clock);
    demo_dashboard_layout(&dashboard);
    carplay_ui_set_text(detail_label, display_ok ? status.detail ? status.detail : ""
                                                 : "Display configuration failed");
    char text[128];
    snprintf(text, sizeof(text), "Video: %llu KiB   Audio: %llu KiB   Error: %d",
             (unsigned long long)(status.video_bytes / 1024),
             (unsigned long long)(status.audio_bytes / 1024), status.error);
    lv_label_set_text(traffic_label, text);
    for (unsigned i = 1; i < DEMO_PROTOCOL_COUNT; ++i)
    {
        if (status.supported[i] && !status.switching)
            lv_obj_remove_state(protocol_buttons[i], LV_STATE_DISABLED);
        else
            lv_obj_add_state(protocol_buttons[i], LV_STATE_DISABLED);
        carplay_ui_style(protocol_buttons[i], status.selected == (demo_ui_protocol)i
                                                  ? CARPLAY_STYLE_ACCENT_CARD
                                                  : CARPLAY_STYLE_CARD);
        carplay_ui_set_text(lv_obj_get_child(protocol_buttons[i], 1),
                            status.supported[i] ? "Available" : "Not enabled");
    }
    for (unsigned i = 0; i < 2; ++i)
        carplay_ui_style(layout_buttons[i], status.fusion == (i != 0) ? CARPLAY_STYLE_ACCENT_CARD
                                                                      : CARPLAY_STYLE_CARD);
}
void demo_ui_init(const demo_ui_backend *backend)
{
    if (!backend || home)
        return;
    host = *backend;
    home = lv_obj_create(NULL);
    lv_obj_remove_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    carplay_ui_style(home, CARPLAY_STYLE_SCREEN);
    lv_obj_set_style_text_font(home, &carplay_ui_17, 0);
    demo_dashboard_build(home, &dashboard, dashboard_connectivity, dashboard_settings);
    carplay_ui_set_text(dashboard.speed, "26");
    carplay_ui_set_text(dashboard.soc, "78");
    lv_bar_set_value(dashboard.bar, 78, LV_ANIM_OFF);
    connectivity = page("Connectivity", true);
    const char *names[] = {"Local", "Apple CarPlay", "Android Auto", "CarLife+"};
    for (unsigned i = 1; i < DEMO_PROTOCOL_COUNT; ++i)
    {
        int x = 64 + (int)(i - 1) * 304;
        protocol_buttons[i] = button(connectivity, names[i], x, 163, 288, 100, select_protocol, i);
        lv_obj_align(lv_obj_get_child(protocol_buttons[i], 0), LV_ALIGN_TOP_MID, 0, 22);
        lv_obj_t *note =
            label(protocol_buttons[i], "", 16, 67, 256, &carplay_ui_14, CARPLAY_STYLE_FAINT);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    }
    state_label =
        label(connectivity, "Not connected", 64, 300, 896, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    detail_label = label(connectivity, "", 64, 345, 896, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    traffic_label = label(connectivity, "", 64, 440, 896, &carplay_ui_14, CARPLAY_STYLE_FAINT);
    button(connectivity, "Stop / local", 64, 502, 436, 65, stop_projection, 0);
    button(connectivity, "Settings", 524, 502, 436, 65, open_page, 2);
    settings = page("Settings", true);
    button(settings, "Light / dark", 64, 154, 436, 56, theme, 0);
    button(settings, "中文 / English", 524, 154, 436, 56, language, 0);
    layout_buttons[0] = button(settings, "Fullscreen", 64, 230, 436, 56, select_layout, 0);
    layout_buttons[1] = button(settings, "Fusion", 524, 230, 436, 56, select_layout, 1);
    build_slider(settings, "Volume", 310, false);
    build_slider(settings, "Brightness", 416, true);
    hint = label(settings, "", 64, 535, 896, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    projection = lv_obj_create(NULL);
    lv_obj_remove_flag(projection, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(projection, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(projection, 0, 0);
    lv_obj_set_style_pad_all(projection, 0, 0);
    hit_plane = lv_obj_create(projection);
    lv_obj_remove_style_all(hit_plane);
    lv_obj_set_pos(hit_plane, 0, 0);
    lv_obj_set_size(hit_plane, 768, 600);
    lv_obj_remove_flag(hit_plane, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit_plane, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit_plane, pointer, LV_EVENT_ALL, NULL);
    strip = box(projection, 768, 0, 256, 600, CARPLAY_STYLE_STRIP);
    lv_obj_set_style_radius(strip, 0, 0);
    label(strip, "Speed", 26, 55, 204, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    label(strip, "0", 26, 118, 204, &carplay_display_110, CARPLAY_STYLE_TEXT);
    label(strip, "km/h", 26, 254, 204, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    label(strip, "Battery", 26, 323, 204, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    label(strip, "82%", 26, 365, 204, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    label(strip, "Demo values", 26, 427, 204, &carplay_ui_14, CARPLAY_STYLE_FAINT);
    button(strip, "Dashboard", 26, 502, 204, 65, stop_projection, 1);
    lv_screen_load(home);
    lv_timer_create(update, 100, NULL);
    update(NULL);
}
