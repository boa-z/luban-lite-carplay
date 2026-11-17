/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef STATUS_TEST_RTHW_H
#define STATUS_TEST_RTHW_H
#include "rtthread.h"
#include <assert.h>
static unsigned test_irq_depth;
static inline rt_base_t rt_hw_interrupt_disable(void) {
  return test_irq_depth++;
}
static inline void rt_hw_interrupt_enable(rt_base_t flags) {
  assert(test_irq_depth == (unsigned)flags + 1);
  test_irq_depth = (unsigned)flags;
}
#endif
