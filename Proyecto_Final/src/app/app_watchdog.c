#include "app_watchdog.h"

#include <errno.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>

#define IWDG_TIMEOUT_MS 2000U

static const struct device *const watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int channel = -1;
static int watchdog_error = -ENODEV;
static bool have_baseline;
static uint32_t last_acq_progress;
static uint32_t last_gps_progress;
static bool seen_acq;
static bool seen_gps;

void app_watchdog_start(void)
{
    if (!device_is_ready(watchdog)) {
        return;
    }

    const struct wdt_timeout_cfg config = {
        .window = { .min = 0U, .max = IWDG_TIMEOUT_MS },
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };
    const int installed = wdt_install_timeout(watchdog, &config);
    if (installed < 0) {
        watchdog_error = installed;
        return;
    }

    watchdog_error = wdt_setup(watchdog, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (watchdog_error == 0) {
        channel = installed;
    }
}

int app_watchdog_observe(uint32_t acq_progress, uint32_t gps_progress)
{
    if (channel < 0) {
        return watchdog_error;
    }

    if (!have_baseline) {
        last_acq_progress = acq_progress;
        last_gps_progress = gps_progress;
        have_baseline = true;
        return watchdog_error;
    }

    if (acq_progress != last_acq_progress) {
        last_acq_progress = acq_progress;
        seen_acq = true;
    }
    if (gps_progress != last_gps_progress) {
        last_gps_progress = gps_progress;
        seen_gps = true;
    }

    if (seen_acq && seen_gps) {
        watchdog_error = wdt_feed(watchdog, channel);
        if (watchdog_error == 0) {
            seen_acq = false;
            seen_gps = false;
        }
    }

    return watchdog_error;
}
