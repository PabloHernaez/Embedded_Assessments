/*
 * Aplicacion (P3), hilo main de prioridad 4. Cada 100 ms: atiende B1 y el
 * cambio de modo, copia los snapshots de adquisicion y GPS, decide el estado
 * (IDLE/FLYING/PROBLEM), actualiza el LED y, si toca, envia el mensaje del modo:
 * TEST cada 2 s, NORMAL cada 5 s, SIMU cada 100 ms.
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

static int64_t report_period_ms(AppMode mode)
{
    switch (mode) {
    case APP_MODE_NORMAL:
        return 5000;
    case APP_MODE_SIMU:
        return APP_SLOT_MS;
    default:
        return 2000;
    }
}

int main(void)
{
    uint32_t reset_cause = 0U;

    if (hwinfo_get_reset_cause(&reset_cause) == 0) {
        (void)hwinfo_clear_reset_cause();
    }
    if (board_ui_init() != 0) {
        return -ENODEV;
    }
    output_init();
    const int wdt = task_wdt_add(MAIN_WDT_MS, NULL, NULL);

    AppMode mode = APP_MODE_TEST;
    AppMode requested = APP_MODE_TEST;
    TrackerHealth health = { .healthy_since_ms = -1 };
    const int64_t start_ms = k_uptime_get();
    int64_t next_report_ms = start_ms + report_period_ms(mode);

    while (true) {
        (void)task_wdt_feed(wdt);
        const bool tx_failed = output_service(k_uptime_get());

        /* B1 avanza TEST -> NORMAL -> SIMU -> TEST; el cambio espera a que acabe el envio. */
        requested = (AppMode)((requested + board_ui_take_presses()) % 3U);
        if (requested != mode && !output_busy()) {
            if (mode == APP_MODE_NORMAL) {
                acquisition_stats_end();
            }
            mode = requested;
            if (mode == APP_MODE_NORMAL) {
                acquisition_stats_begin();
            }
            /* SIMU envia su primera trama ya; TEST y NORMAL al cumplir su periodo. */
            next_report_ms = k_uptime_get() + (mode == APP_MODE_SIMU ? 0 : report_period_ms(mode));
        }

        AcquisitionSnapshot acq;
        GpsSnapshot gps;
        acquisition_snapshot_get(&acq);
        gps_snapshot_get(&gps);

        const int64_t now_ms = k_uptime_get();
        const TrackerStatus status =
            tracker_status_from_snapshots(&health, &acq, &gps, now_ms, tx_failed);
        board_ui_show(mode, status.state);

        if (requested == mode && now_ms >= next_report_ms) {
            /* Con retraso se envia un solo mensaje actual y se salta al siguiente periodo. */
            const int64_t period_ms = report_period_ms(mode);
            next_report_ms += ((now_ms - next_report_ms) / period_ms + 1) * period_ms;
            (void)output_report(mode, &acq, &gps, &status, now_ms, reset_cause, watchdog_status);
        }

        /* Siguiente slot de 100 ms contado desde el arranque (sin acumular deriva). */
        const int64_t elapsed_ms = k_uptime_get() - start_ms;
        k_sleep(K_TIMEOUT_ABS_MS(start_ms + (elapsed_ms / APP_SLOT_MS + 1) * APP_SLOT_MS));
    }
}
