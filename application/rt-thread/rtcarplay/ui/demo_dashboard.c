/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Extracted unchanged from the Mobility Cockpit V4 dashboard. Layout, fonts,
 * colours and navigation geometry are shared by both application adapters. */
#include "demo_dashboard.h"
#include "carplay_ui_theme.h"
#include "ui_language.h"
#include "icons/carplay_icons.h"
#include <string.h>
#define lv_label_set_text carplay_ui_set_text
LV_FONT_DECLARE(carplay_display_220);
LV_FONT_DECLARE(carplay_display_110);
LV_FONT_DECLARE(carplay_ui_34);
LV_FONT_DECLARE(carplay_ui_21);
LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_14);

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                       carplay_style_role_t role)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    carplay_ui_style(obj, role);
    return obj;
}

static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int top, int w,
                      const lv_font_t *font, carplay_style_role_t role)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_line_space(obj, 0, 0);
    carplay_ui_style(obj, role);
    lv_obj_set_pos(obj, x, top);
    if (w > 0)
    {
        lv_obj_set_width(obj, w);
        lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    }
    lv_label_set_text(obj, value);
    return obj;
}

static lv_obj_t *icon_mask(lv_obj_t *parent, const lv_image_dsc_t *mask, int x, int y,
                           carplay_style_role_t role)
{
    lv_obj_t *obj = lv_image_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(obj, mask);
    lv_obj_set_pos(obj, x, y);
    carplay_ui_style(obj, role);
    return obj;
}

static lv_obj_t *stroke(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                        carplay_style_role_t role)
{
    return panel(parent, x, y, w, h, radius, role);
}

static lv_obj_t *divider(lv_obj_t *parent, int x, int y, int w, int h)
{
    return panel(parent, x, y, w, h, 0, CARPLAY_STYLE_LINE_BORDER);
}

#define DASH_CLOCK_RIGHT 964
#define DASH_STATUS_X 847
#define DASH_STATUS_GAP 16 /* minimum clear space before the clock ink */
#define DASH_STATUS_MIN_X 560
#define DASH_DOT_SIZE 11
#define DASH_DOT_GAP 12

static int32_t label_width(lv_obj_t *label)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
    const char *value = lv_label_get_text(label);
    return lv_text_get_width(value, strlen(value), font, 0);
}

/* Places the dot and the status label from their rendered widths. Called on
 * every content or language change; writes only when the geometry moved. */
void demo_dashboard_layout(const demo_dashboard *view)
{
    if (!view->status || !view->clock || !view->dot)
        return;
    int32_t width = label_width(view->status) + 2; /* rounding slack, no clipping */
    int32_t limit = DASH_CLOCK_RIGHT - label_width(view->clock) - DASH_STATUS_GAP;
    int32_t left = DASH_STATUS_X;
    if (left + width > limit)
        left = limit - width;
    if (left < DASH_STATUS_MIN_X)
        left = DASH_STATUS_MIN_X;
    int32_t dot_x = left - DASH_DOT_GAP - DASH_DOT_SIZE;
    if (lv_obj_get_x(view->status) != left || lv_obj_get_width(view->status) != width)
    {
        lv_obj_set_pos(view->status, left, 64);
        lv_obj_set_width(view->status, width);
    }
    if (lv_obj_get_x(view->dot) != dot_x)
        lv_obj_set_pos(view->dot, dot_x, 63);
}

void demo_dashboard_build(lv_obj_t *parent, demo_dashboard *view, lv_event_cb_t open_connectivity,
                          lv_event_cb_t open_settings)
{
    text(parent, "Dashboard", 64, 54, 520, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    view->dot = stroke(parent, 824, 63, 11, 11, LV_RADIUS_CIRCLE, CARPLAY_STYLE_DOT_OFF);
    /* The clock is right-aligned against the reference anchor x = 964; it owns
     * that slot and the state dot plus label are placed to its left. */
    view->clock =
        text(parent, "--:--", DASH_CLOCK_RIGHT - 84, 62, 84, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_align(view->clock, LV_TEXT_ALIGN_RIGHT, 0);
    view->status =
        text(parent, "Not connected", DASH_STATUS_X, 64, 210, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    demo_dashboard_layout(view);
    view->note = text(parent, "Demo values", 64, 92, 300, &carplay_ui_14, CARPLAY_STYLE_FAINT);

    text(parent, "Speed", 70, 160, 300, &carplay_ui_21, CARPLAY_STYLE_MUTED);
    view->speed = text(parent, "--", 62, 236, 380, &carplay_display_220, CARPLAY_STYLE_TEXT);
    text(parent, "km/h", 473, 353, 160, &carplay_ui_34, CARPLAY_STYLE_MUTED);

    divider(parent, 649, 152, 1, 316);
    text(parent, "Battery", 711, 160, 260, &carplay_ui_21, CARPLAY_STYLE_MUTED);
    view->soc = text(parent, "--", 699, 247, 240, &carplay_display_110, CARPLAY_STYLE_TEXT);
    text(parent, "%", 880, 301, 80, &carplay_ui_34, CARPLAY_STYLE_MUTED);
    view->bar = lv_bar_create(parent);
    lv_obj_remove_flag(view->bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(view->bar, 718, 367);
    lv_obj_set_size(view->bar, 224, 8);
    lv_bar_set_range(view->bar, 0, 100);
    lv_obj_set_style_radius(view->bar, 4, 0);
    lv_obj_set_style_radius(view->bar, 4, LV_PART_INDICATOR);
    carplay_ui_style_part(view->bar, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(view->bar, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);

    divider(parent, 64, 485, 896, 1);

    lv_obj_t *connectivity = panel(parent, 64, 505, 437, 67, 14, CARPLAY_STYLE_ACCENT_CARD);
    lv_obj_add_flag(connectivity, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(connectivity, open_connectivity, LV_EVENT_CLICKED, NULL);
    icon_mask(connectivity, &carplay_icon_smartphone_24, 19, 22, CARPLAY_STYLE_ICON_ACCENT);
    lv_obj_t *connectivity_text =
        text(connectivity, "Connectivity", 0, 0, 0, &carplay_ui_18, CARPLAY_STYLE_ACCENT_TEXT);
    lv_obj_center(connectivity_text);

    lv_obj_t *settings = panel(parent, 523, 505, 437, 67, 14, CARPLAY_STYLE_CARD);
    lv_obj_add_flag(settings, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings, open_settings, LV_EVENT_CLICKED, NULL);
    icon_mask(settings, &carplay_icon_settings_24, 19, 21, CARPLAY_STYLE_ICON_TEXT);
    lv_obj_t *settings_text =
        text(settings, "Settings", 0, 0, 0, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_center(settings_text);
}
