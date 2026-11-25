/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <stdint.h>
void *aicos_malloc(unsigned int type, size_t size);
void aicos_free(unsigned int type, void *ptr);
size_t aicos_enter_critical_section(void);
void aicos_leave_critical_section(size_t flags);
void aicos_msleep(uint32_t ms);
