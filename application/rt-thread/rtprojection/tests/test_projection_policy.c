/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "projection_policy.h"
#include <assert.h>
#include <stdio.h>
typedef struct
{
    bool active, idle;
    unsigned starts, stops;
} fake;
static fake a, b;
static void enable(void *ctx, bool value)
{
    fake *f = ctx;
    if (value)
    {
        assert(!a.active && !b.active);
        f->active = true;
        f->idle = false;
        ++f->starts;
    }
    else
    {
        f->active = false;
        ++f->stops;
    }
}
static bool retired(void *ctx)
{
    return ((fake *)ctx)->idle;
}
int main(void)
{
    projection_policy p = {0};
    p.backend[PROJECTION_CARPLAY] = (projection_backend){&a, enable, retired};
    p.backend[PROJECTION_ANDROIDAUTO] = (projection_backend){&b, enable, retired};
    assert(projection_policy_select(&p, PROJECTION_ANDROIDAUTO));
    projection_policy_poll(&p, 0);
    assert(!a.starts && !b.starts); /* Initial pending cleanup blocks grants. */
    a.idle = b.idle = true;
    projection_policy_poll(&p, 1);
    assert(b.starts == 1 && p.owner == PROJECTION_ANDROIDAUTO);
    assert(!projection_policy_select(&p, PROJECTION_CARLIFE));
    projection_policy_poll(&p, 2);
    assert(!b.stops && p.requested == PROJECTION_ANDROIDAUTO);
    assert(projection_policy_select(&p, PROJECTION_CARPLAY));
    projection_policy_poll(&p, UINT32_MAX - 100);
    assert(b.stops == 1 && !a.starts && p.stopping);
    projection_policy_poll(&p, 10000);
    assert(p.error && !a.starts && p.owner == PROJECTION_ANDROIDAUTO);
    assert(projection_policy_select(&p, PROJECTION_ANDROIDAUTO));
    projection_policy_poll(&p, 10001);
    assert(b.starts == 1 && b.stops == 1); /* No early reacquisition. */
    b.idle = true;
    projection_policy_poll(&p, 10002);
    assert(b.starts == 2 && !p.stopping && !p.error);
    assert(projection_policy_select(&p, PROJECTION_CARPLAY));
    projection_policy_poll(&p, 10003);
    b.idle = true;
    projection_policy_poll(&p, 10004);
    assert(a.starts == 1 && p.owner == PROJECTION_CARPLAY);
    assert(projection_policy_select(&p, PROJECTION_LOCAL));
    projection_policy_poll(&p, 10005);
    assert(a.stops == 1 && p.owner == PROJECTION_CARPLAY);
    a.idle = true;
    projection_policy_poll(&p, 10006);
    assert(p.owner == PROJECTION_LOCAL && !p.stopping && !a.active && !b.active);
    assert(!projection_policy_select(&p, (projection_protocol)99));
    puts("projection policy: startup, contention, unsupported, late release, rollover PASS");
    return 0;
}
