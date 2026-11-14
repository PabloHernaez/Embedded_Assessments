#ifndef TEST_COORDINATOR_ZEPHYR_KERNEL_H
#define TEST_COORDINATOR_ZEPHYR_KERNEL_H

#include "../../fakes/zephyr/kernel.h"

#define CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC 48000000

/* Ensayo determinista: conserva la entrada del hilo, pero no la arranca. */
struct k_mutex { unsigned int depth; };
struct k_timer { unsigned int unused; };

#define K_MUTEX_DEFINE(name) struct k_mutex name = {0}
#define K_TIMER_DEFINE(name, expiry, stop) struct k_timer name = {0}
#define K_THREAD_DEFINE(name, stack_size, entry, ...) \
    static void (*const name)(void *, void *, void *) = entry
#define K_FOREVER (-1)
#define K_MSEC(ms) (ms)
#define ARG_UNUSED(arg) ((void)(arg))

int k_mutex_lock(struct k_mutex *mutex, int timeout);
int k_mutex_unlock(struct k_mutex *mutex);
uint32_t k_timer_status_sync(struct k_timer *timer);
void k_timer_start(struct k_timer *timer, int delay, int period);
int32_t k_sleep(int timeout);
uint32_t k_cycle_get_32(void);

#endif
