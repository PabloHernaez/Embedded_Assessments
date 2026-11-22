/*
 * Aplicacion (P3), hilo main de prioridad 4, despertado por un k_timer de 100 ms.
 * Cada vuelta: atiende B1 y el cambio de modo, copia los snapshots de adquisicion
 * y GPS, decide el estado (IDLE/FLYING/PROBLEM), pone el LED y, cada N vueltas,
 * envia el mensaje del modo: TEST cada 2 s, NORMAL cada 5 s, SIMU cada 100 ms.
 * Adquisicion (prioridad 3) y GPS (prioridad 5) son hilos propios que arrancan solos.
 */
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "acquisition_api.h"
#include "app/board_ui.h"
#include "app/output.h"
#include "app/tracker_status.h"
#include "gps_api.h"

#define APP_SLOT_MS 100
#define MAIN_WDT_MS 1000

K_TIMER_DEFINE(app_timer, NULL, NULL);

static const struct device *const iwdg = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int watchdog_status;

/*
 * Watchdog por hilo con task_wdt: cada hilo alimenta su canal y, si alguno se
 * bloquea, el sistema se reinicia. El IWDG (2 s) cubre un bloqueo del propio
 * task_wdt. Nivel APPLICATION: queda listo antes de que arranquen los hilos.
 */
static int watchdog_init(void)
{
    const bool have_iwdg = device_is_ready(iwdg);
    const int err = task_wdt_init(have_iwdg ? iwdg : NULL);

    watchdog_status = err != 0 ? err : (have_iwdg ? 0 : -ENODEV);
    return 0;
}
SYS_INIT(watchdog_init, APPLICATION, 0);

/* Vueltas de 100 ms entre mensajes. */
static unsigned int report_slots(AppMode mode)
{
    switch (mode) {
    case APP_MODE_NORMAL:
        return 50;
    case APP_MODE_SIMU:
        return 1;
    default:
        return 20;
    }
}

int main(void)
{
    uint32_t reset_cause = 0U;

    if (hwinfo_get_reset_cause(&reset_cause) == 0) {
        (void)hwinfo_clear_reset_cause();
    }
    (void)board_ui_init(); /* sin LED el tracker sigue midiendo y transmitiendo */
    output_init(reset_cause, watchdog_status);
    const int wdt = task_wdt_add(MAIN_WDT_MS, NULL, NULL);

    AppMode mode = APP_MODE_TEST;
    AppMode requested = APP_MODE_TEST;
    TrackerHealth health = { .healthy_since_ms = -1 };
    unsigned int slots = 0U; /* vueltas desde el ultimo mensaje o cambio de modo */

    k_timer_start(&app_timer, K_MSEC(APP_SLOT_MS), K_MSEC(APP_SLOT_MS));

    while (true) {
        /* Si main se retrasa, los vencimientos perdidos se atienden en una sola vuelta. */
        (void)k_timer_status_sync(&app_timer);
        (void)task_wdt_feed(wdt);
        output_service(k_uptime_get());

        /* B1 avanza TEST -> NORMAL -> SIMU -> TEST; el cambio espera a que acabe el envio. */
        requested = (AppMode)((requested + board_ui_take_presses()) % APP_MODE_COUNT);
        if (requested != mode && !output_busy()) {
            if (mode == APP_MODE_NORMAL) {
                acquisition_stats_end();
            }
            mode = requested;
            if (mode == APP_MODE_NORMAL) {
                acquisition_stats_begin();
            }
            slots = 0U; /* SIMU envia ya; TEST y NORMAL al cumplir su periodo */
        }

        AcquisitionSnapshot acq;
        GpsSnapshot gps;
        acquisition_snapshot_get(&acq);
        gps_snapshot_get(&gps);

        const int64_t now_ms = k_uptime_get();
        const TrackerStatus status = tracker_status_from_snapshots(&health, &acq, &gps, now_ms);
        board_ui_show(mode, status.state);

        if (requested == mode && ++slots >= report_slots(mode)) {
            slots = 0U;
            output_report(mode, &acq, &gps, &status, now_ms);
        }
    }
}
