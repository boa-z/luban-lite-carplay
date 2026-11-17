/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "projection_policy.h"
bool projection_policy_select(projection_policy *p, projection_protocol protocol)
{
    if (!p || (unsigned)protocol >= PROJECTION_COUNT ||
        (protocol != PROJECTION_LOCAL &&
         (!p->backend[protocol].enable || !p->backend[protocol].retired)))
        return false;
    p->requested = protocol;
    return true;
}
void projection_policy_poll(projection_policy *p, uint32_t now)
{
    if (!p)
        return;
    if (p->owner != PROJECTION_LOCAL && (p->stopping || p->owner != p->requested))
    {
        projection_backend *old = &p->backend[p->owner];
        if (!p->stopping)
        {
            old->enable(old->ctx, false);
            p->stopping = true;
            p->stop_at = now;
        }
        if (!old->retired(old->ctx))
        {
            /* Preserve ownership after timeout. A late acknowledgement may
             * finish the handoff; time alone can never release DMA buffers. */
            if ((uint32_t)(now - p->stop_at) >= 10000u)
                p->error = -1;
            return;
        }
        p->owner = PROJECTION_LOCAL;
        p->stopping = false;
        p->error = 0;
    }
    if (p->owner == PROJECTION_LOCAL && p->requested != PROJECTION_LOCAL)
    {
        /* Initial startup also requires every backend to acknowledge idle. */
        for (unsigned i = 1; i < PROJECTION_COUNT; ++i)
            if (p->backend[i].retired && !p->backend[i].retired(p->backend[i].ctx))
                return;
        projection_backend *next = &p->backend[p->requested];
        p->owner = p->requested;
        next->enable(next->ctx, true);
    }
}
