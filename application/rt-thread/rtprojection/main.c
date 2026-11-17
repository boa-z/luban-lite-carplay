/* SPDX-License-Identifier: GPL-3.0-only */
#include <rtthread.h>
int main(void)
{
    rt_kprintf("Mobility Cockpit shared UI / Android Auto development backend\n");
    rt_kprintf("Phone, board and DHU acceptance: NOT_RUN\n");
    rt_kprintf("Provision /data/androidauto credentials; select Android Auto in Connectivity\n");
    return 0; /* GUI owner initializes its backend, avoiding concurrent init. */
}
