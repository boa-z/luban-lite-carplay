/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only ownership test using the production adapter and explicit OS stubs.
 * This does not simulate IRQ latency, hardware or phone interoperability. */
#include "../backends/androidauto/platform/androidauto_status_info.c"
#include <assert.h>
#include <stdio.h>
int main(void) {
  rtaa_sink sink = {0};
  androidauto_status_info_bind(&sink);
  char source[128];
  memset(source, 'x', 127);
  source[127] = 0;
  rtaa_navigation_notification note = {0};
  note.steps_count = RTAA_NAV_STEPS;
  note.destinations_count = RTAA_NAV_DESTINATIONS;
  for (size_t i = 0; i < note.steps_count; ++i) {
    note.steps[i].has_instruction = true;
    note.steps[i].instruction = source;
    note.steps[i].road_names_count = RTAA_NAV_ROADS;
    for (size_t j = 0; j < RTAA_NAV_ROADS; ++j)
      note.steps[i].road_names[j] = source;
  }
  for (size_t i = 0; i < note.destinations_count; ++i)
    note.destinations[i].address = source;
  assert(!sink.navigation_notification(NULL, &note));
  rtaa_navigation_position pos = {0};
  pos.has_step_distance = pos.has_distance = true;
  pos.distance.display_text = pos.current_road = source;
  pos.destinations_count = RTAA_NAV_DESTINATIONS;
  for (size_t i = 0; i < pos.destinations_count; ++i) {
    pos.destinations[i].distance.display_text = source;
    pos.destinations[i].estimated_time_of_arrival = source;
    pos.destinations[i].has_time_to_arrival = true;
    pos.destinations[i].time_to_arrival_seconds = INT64_C(5000000000);
  }
  assert(!sink.navigation_position(NULL, &pos));
  rtaa_navigation_turn turn = {.road_name = source};
  assert(!sink.navigation_turn(NULL, &turn));
  rtaa_navigation_state state = {true, 0};
  assert(!sink.navigation_state(NULL, &state));
  rtaa_phone_status calls = {0};
  calls.calls_count = RTAA_PHONE_CALLS;
  for (size_t i = 0; i < calls.calls_count; ++i)
    calls.calls[i] = (rtaa_phone_call){4, 0, source, "", NULL};
  assert(!sink.phone_status(NULL, &calls));
  memset(source, 'z', 127);
  assert(navigation.notification.steps[0].instruction[0] == 'x');
  assert(navigation.position.current_road[0] == 'x');
  assert(navigation.turn.road_name[0] == 'x');
  assert(phone.status.calls[0].number[0] == 'x');
  assert(phone.status.calls[0].display_name &&
         !phone.status.calls[0].display_name[0]);
  assert(!phone.status.calls[0].contact_id);
  for (size_t i = 0; i < calls.calls_count; ++i)
    calls.calls[i].number = calls.calls[i].display_name =
        calls.calls[i].contact_id = source;
  assert(!sink.phone_status(NULL, &calls));
  navigation_information *n = malloc(sizeof(*n));
  phone_information *p = malloc(sizeof(*p));
  assert(n && p);
  *n = navigation;
  *p = phone;
  note_text(n, navigation.note_text);
  position_text(n, navigation.position_text);
  size_t used = 0;
  n->turn.road_name =
      retain(n->turn.road_name, n->turn_text, &used, navigation.turn_text);
  phone_text(p, phone.text);
  sink.navigation_reset(NULL);
  sink.phone_reset(NULL);
  assert(!navigation.states && !navigation.notifications &&
         !navigation.positions && !navigation.turns);
  assert(!phone.updates && !phone.status.calls_count);
  assert(n->notification.steps[3].road_names[2][126] == 'x');
  assert(n->notification.destinations[1].address[126] == 'x');
  assert(n->position.destinations[1].estimated_time_of_arrival[126] == 'x');
  assert(n->position.destinations[1].time_to_arrival_seconds ==
         INT64_C(5000000000));
  assert(n->turn.road_name[126] == 'x');
  assert(p->status.calls[3].contact_id[126] == 'z');
  assert(!command(0, NULL) && !test_irq_depth);
  free(n);
  free(p);
  puts("androidauto status ownership: PASS (maximum text capacity, snapshot "
       "relocation, reset)");
  return 0;
}
