#ifndef TEST_ZEPHYR_KERNEL_H
#define TEST_ZEPHYR_KERNEL_H

#include <stdint.h>

#define CONFIG_SYS_CLOCK_TICKS_PER_SEC 10000

/* Reloj de adquisición; implementado por cada ejecutable de ensayo host. */
int64_t k_uptime_ticks(void);
int64_t k_ticks_to_ms_floor64(int64_t ticks);
int64_t k_ms_to_ticks_floor64(int64_t ms);
int64_t k_ms_to_ticks_ceil64(int64_t ms);

#endif
