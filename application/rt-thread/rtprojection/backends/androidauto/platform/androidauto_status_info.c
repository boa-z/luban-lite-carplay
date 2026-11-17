/* SPDX-License-Identifier: GPL-3.0-only */
#include "androidauto_platform.h"
#include <rthw.h>
#include <rtthread.h>
#include <string.h>

/* The SDK limits each retained string to 127 bytes plus NUL. Separate arenas
 * keep every update, including optional empty strings, owned by this consumer.
 */
#define NOTE_TEXTS                                                             \
  (RTAA_NAV_STEPS * (1 + RTAA_NAV_ROADS) + RTAA_NAV_DESTINATIONS)
#define POSITION_TEXTS (2 + 2 * RTAA_NAV_DESTINATIONS)
typedef struct {
  unsigned states, notifications, positions, turns;
  rtaa_navigation_state state;
  rtaa_navigation_notification notification;
  rtaa_navigation_position position;
  rtaa_navigation_turn turn;
  char note_text[NOTE_TEXTS * 128], position_text[POSITION_TEXTS * 128],
      turn_text[128];
} navigation_information;
typedef struct {
  unsigned updates;
  rtaa_phone_status status;
  char text[RTAA_PHONE_CALLS * 3 * 128];
} phone_information;
static navigation_information navigation;
static phone_information phone;

/* old_base == NULL retains borrowed text. Otherwise rebase a coherent snapshot
 * already copied under the lock, without reading the live record again. */
static const char *retain(const char *s, char *text, size_t *used,
                          const char *old_base) {
  if (!s)
    return NULL;
  if (old_base)
    return text + (s - old_base);
  size_t n = strlen(s) + 1;
  char *out = text + *used;
  memcpy(out, s, n);
  *used += n;
  return out;
}
static void note_text(navigation_information *v, const char *old_base) {
  size_t used = 0;
  rtaa_navigation_notification *n = &v->notification;
  for (size_t i = 0; i < n->steps_count; ++i) {
    rtaa_nav_step *s = &n->steps[i];
    s->instruction = retain(s->instruction, v->note_text, &used, old_base);
    for (size_t j = 0; j < s->road_names_count; ++j)
      s->road_names[j] =
          retain(s->road_names[j], v->note_text, &used, old_base);
  }
  for (size_t i = 0; i < n->destinations_count; ++i)
    n->destinations[i].address =
        retain(n->destinations[i].address, v->note_text, &used, old_base);
}
static void position_text(navigation_information *v, const char *old_base) {
  size_t used = 0;
  rtaa_navigation_position *p = &v->position;
  p->distance.display_text =
      retain(p->distance.display_text, v->position_text, &used, old_base);
  p->current_road = retain(p->current_road, v->position_text, &used, old_base);
  for (size_t i = 0; i < p->destinations_count; ++i) {
    rtaa_nav_destination_distance *d = &p->destinations[i];
    d->distance.display_text =
        retain(d->distance.display_text, v->position_text, &used, old_base);
    d->estimated_time_of_arrival =
        retain(d->estimated_time_of_arrival, v->position_text, &used, old_base);
  }
}
static void phone_text(phone_information *v, const char *old_base) {
  size_t used = 0;
  for (size_t i = 0; i < v->status.calls_count; ++i) {
    rtaa_phone_call *c = &v->status.calls[i];
    c->number = retain(c->number, v->text, &used, old_base);
    c->display_name = retain(c->display_name, v->text, &used, old_base);
    c->contact_id = retain(c->contact_id, v->text, &used, old_base);
  }
}
static int state_update(void *ctx, const rtaa_navigation_state *value) {
  (void)ctx;
  rt_base_t flags = rt_hw_interrupt_disable();
  navigation.state = *value;
  ++navigation.states;
  rt_hw_interrupt_enable(flags);
  return 0;
}
static int notification_update(void *ctx,
                               const rtaa_navigation_notification *value) {
  (void)ctx;
  rt_base_t flags = rt_hw_interrupt_disable();
  navigation.notification = *value;
  note_text(&navigation, NULL);
  ++navigation.notifications;
  rt_hw_interrupt_enable(flags);
  return 0;
}
static int position_update(void *ctx, const rtaa_navigation_position *value) {
  (void)ctx;
  rt_base_t flags = rt_hw_interrupt_disable();
  navigation.position = *value;
  position_text(&navigation, NULL);
  ++navigation.positions;
  rt_hw_interrupt_enable(flags);
  return 0;
}
static int turn_update(void *ctx, const rtaa_navigation_turn *value) {
  (void)ctx;
  rt_base_t flags = rt_hw_interrupt_disable();
  navigation.turn = *value;
  size_t used = 0;
  navigation.turn.road_name =
      retain(value->road_name, navigation.turn_text, &used, NULL);
  ++navigation.turns;
  rt_hw_interrupt_enable(flags);
  return 0;
}
static int phone_update(void *ctx, const rtaa_phone_status *value) {
  (void)ctx;
  rt_base_t flags = rt_hw_interrupt_disable();
  phone.status = *value;
  phone_text(&phone, NULL);
  ++phone.updates;
  rt_hw_interrupt_enable(flags);
  return 0;
}
static void navigation_reset(void *ctx) {
  (void)ctx;
  rt_base_t flags = rt_hw_interrupt_disable();
  memset(&navigation, 0, sizeof(navigation));
  rt_hw_interrupt_enable(flags);
}
static void phone_reset(void *ctx) {
  (void)ctx;
  rt_base_t flags = rt_hw_interrupt_disable();
  memset(&phone, 0, sizeof(phone));
  rt_hw_interrupt_enable(flags);
}
void androidauto_status_info_bind(rtaa_sink *sink) {
  sink->navigation_state = state_update;
  sink->navigation_notification = notification_update;
  sink->navigation_position = position_update;
  sink->navigation_turn = turn_update;
  sink->navigation_reset = navigation_reset;
  sink->phone_status = phone_update;
  sink->phone_reset = phone_reset;
}
static int command(int argc, char **argv) {
  (void)argc;
  (void)argv;
  navigation_information *n = rt_malloc(sizeof(*n));
  phone_information *p = rt_malloc(sizeof(*p));
  if (!n || !p) {
    if (n)
      rt_free(n);
    if (p)
      rt_free(p);
    return -RT_ENOMEM;
  }
  rt_base_t flags = rt_hw_interrupt_disable();
  *n = navigation;
  *p = phone;
  rt_hw_interrupt_enable(flags);
  note_text(n, navigation.note_text);
  position_text(n, navigation.position_text);
  size_t used = 0;
  n->turn.road_name =
      retain(n->turn.road_name, n->turn_text, &used, navigation.turn_text);
  phone_text(p, phone.text);
  rt_kprintf(
      "navigation: states=%u notifications=%u positions=%u legacy_turns=%u\n",
      n->states, n->notifications, n->positions, n->turns);
  if (n->state.has_state)
    rt_kprintf("state=%u (0 unavailable, 1 active, 2 inactive, 3 rerouting)\n",
               n->state.state);
  rt_kprintf("steps=%u destinations=%u\n",
             (unsigned)n->notification.steps_count,
             (unsigned)n->notification.destinations_count);
  for (size_t i = 0; i < n->notification.steps_count; ++i) {
    const rtaa_nav_step *s = &n->notification.steps[i];
    if (s->instruction)
      rt_kprintf("step[%u]=%s\n", (unsigned)i, s->instruction);
    if (s->has_maneuver && s->maneuver.has_type)
      rt_kprintf("maneuver=%u\n", s->maneuver.type);
  }
  if (n->position.distance.display_text)
    rt_kprintf("step_distance=%s\n", n->position.distance.display_text);
  if (n->position.current_road)
    rt_kprintf("current_road=%s\n", n->position.current_road);
  for (size_t i = 0; i < n->position.destinations_count; ++i)
    if (n->position.destinations[i].estimated_time_of_arrival)
      rt_kprintf("destination[%u] eta=%s\n", (unsigned)i,
                 n->position.destinations[i].estimated_time_of_arrival);
  rt_kprintf("phone: updates=%u calls=%u\n", p->updates,
             (unsigned)p->status.calls_count);
  if (p->status.has_signal_strength)
    rt_kprintf("signal_strength=%u\n", p->status.signal_strength);
  for (size_t i = 0; i < p->status.calls_count; ++i) {
    const rtaa_phone_call *c = &p->status.calls[i];
    rt_kprintf("call[%u] state=%u duration_seconds=%u\n", (unsigned)i, c->state,
               c->duration_seconds);
    if (c->number)
      rt_kprintf("number=%s\n", c->number);
    if (c->display_name)
      rt_kprintf("name=%s\n", c->display_name);
  }
  rt_free(n);
  rt_free(p);
  return 0;
}
MSH_CMD_EXPORT_ALIAS(command, androidauto_info,
                     Latest Android Auto navigation and phone updates);
