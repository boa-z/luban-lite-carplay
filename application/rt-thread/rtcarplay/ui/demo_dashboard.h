/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DEMO_DASHBOARD_H
#define DEMO_DASHBOARD_H
#include "lvgl.h"
/* Original V4 view references. Applications provide values and navigation;
 * the renderer has no protocol, vehicle-provider or OS dependency. */
typedef struct
{
    lv_obj_t *dot, *clock, *status, *note, *speed, *soc, *bar;
} demo_dashboard;
void demo_dashboard_build(lv_obj_t *, demo_dashboard *, lv_event_cb_t connectivity,
                          lv_event_cb_t settings);
void demo_dashboard_layout(const demo_dashboard *);
#endif
