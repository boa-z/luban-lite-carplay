/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef STATUS_TEST_RTTHREAD_H
#define STATUS_TEST_RTTHREAD_H
#include <stdint.h>
#include <stdlib.h>
typedef intptr_t rt_base_t;
#define RT_ENOMEM 12
#define rt_malloc malloc
#define rt_free free
#define MSH_CMD_EXPORT_ALIAS(fn, alias, description)
static inline int rt_kprintf(const char *format, ...) {
  (void)format;
  return 0;
}
#endif
