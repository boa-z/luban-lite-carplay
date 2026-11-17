/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PROJECTION_POLICY_H
#define PROJECTION_POLICY_H
#include <stdbool.h>
#include <stdint.h>
typedef enum
{
    PROJECTION_LOCAL,
    PROJECTION_CARPLAY,
    PROJECTION_ANDROIDAUTO,
    PROJECTION_CARLIFE,
    PROJECTION_COUNT
} projection_protocol;
typedef struct
{
    void *ctx;
    void (*enable)(void *, bool);
    bool (*retired)(void *);
} projection_backend;
typedef struct
{
    projection_backend backend[PROJECTION_COUNT];
    projection_protocol owner, requested;
    bool stopping;
    uint32_t stop_at;
    int error;
} projection_policy;
/* Single caller owns the policy. Backends must acknowledge stop only after
 * all borrowed media/transport resources have been released. No forced grant. */
bool projection_policy_select(projection_policy *, projection_protocol);
void projection_policy_poll(projection_policy *, uint32_t now_ms);
#endif
