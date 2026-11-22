#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>

#include "acquisition_api.h"
#include "app/app_watchdog.h"
#include "app/board_ui.h"
#include "app/tracker_status.h"
#include "gps_api.h"
#include "app/output.h"

#define APP_SLOT_MS 100

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
    int err = board_ui_init();
    if (err != 0) {
        return err;
    }

    uint32_t reset_cause = 0U;
    if (hwinfo_get_reset_cause(&reset_cause) == 0) {
        (void)hwinfo_clear_reset_cause();
    } else {
        reset_cause = 0U;
    }

    output_init();
    app_watchdog_start();
    AppMode mode = APP_MODE_TEST;
    AppMode requested = APP_MODE_TEST;
    TrackerHealth health = { .healthy_since_ms = -1 };
    const int64_t start_ms = k_uptime_get();
    int64_t next_report_ms = start_ms + report_period_ms(mode);

    while (true) {
        const bool tx_failed = output_service(k_uptime_get());
        const unsigned int presses = board_ui_take_presses();
        requested = (AppMode)(((unsigned int)requested + presses % 3U) % 3U);
        if (requested != mode && !output_busy()) {
            if (mode == APP_MODE_NORMAL) {
                acquisition_stats_end();
            }
            mode = requested;
            if (mode == APP_MODE_NORMAL) {
                acquisition_stats_begin();
            }
            next_report_ms = k_uptime_get();
            if (mode != APP_MODE_SIMU) {
                next_report_ms += report_period_ms(mode);
            }
        }

        AcquisitionSnapshot acq;
        GpsSnapshot gps;
        acquisition_snapshot_get(&acq);
        gps_snapshot_get(&gps);
        const int watchdog_error = app_watchdog_observe(acq.progress, gps.progress);

        const int64_t now_ms = k_uptime_get();
        const TrackerStatus status =
            tracker_status_from_snapshots(&health, &acq, &gps, now_ms, tx_failed);
        board_ui_show(mode, status.state);

        if (requested == mode && now_ms >= next_report_ms) {
            /* Con retraso se envia un solo mensaje actual y se salta al siguiente periodo. */
            const int64_t period_ms = report_period_ms(mode);
            next_report_ms += ((now_ms - next_report_ms) / period_ms + 1) * period_ms;
            (void)output_report(mode, &acq, &gps, &status, now_ms, reset_cause, watchdog_error);
        }

        const int64_t elapsed_ms = k_uptime_get() - start_ms;
        const int64_t next_slot_ms = start_ms + (elapsed_ms / APP_SLOT_MS + 1) * APP_SLOT_MS;
        k_sleep(K_TIMEOUT_ABS_MS(next_slot_ms));
    }
}
