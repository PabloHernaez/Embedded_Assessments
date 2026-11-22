/*
 * Ensayo P4-14: con el firmware de produccion intacto, bloquea un hilo a los
 * 10 s del arranque. Su canal de task_wdt deja de alimentarse y el sistema se
 * reinicia; tras el reinicio el informe TEST muestra la causa (RESET=...).
 *   acquisition / gps / main: se suspende ese hilo -> task_wdt -> sys_reboot
 *   irq: interrupciones bloqueadas -> task_wdt no puede alimentar el IWDG -> IWDG
 */
#include <zephyr/kernel.h>

#define BLOCK_AFTER_MS 10000

extern const k_tid_t acquisition_tid; /* K_THREAD_DEFINE en acquisition.c */
extern const k_tid_t gps_tid;         /* K_THREAD_DEFINE en gps.c */
extern struct k_thread z_main_thread; /* hilo main del kernel (solo para el ensayo) */

static void blocker(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_msleep(BLOCK_AFTER_MS);

#if defined(WDT_CASE_acquisition)
    k_thread_suspend(acquisition_tid);
#elif defined(WDT_CASE_gps)
    k_thread_suspend(gps_tid);
#elif defined(WDT_CASE_main)
    k_thread_suspend(&z_main_thread);
#elif defined(WDT_CASE_irq)
    (void)irq_lock();
    while (true) {
    }
#else
#error "WDT_CASE desconocido"
#endif
}

/* Prioridad 1: por encima de los tres hilos del firmware para que el bloqueo sea inmediato. */
K_THREAD_DEFINE(blocker_tid, 512, blocker, NULL, NULL, NULL, 1, 0, 0);
