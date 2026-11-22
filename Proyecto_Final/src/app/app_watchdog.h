#ifndef DRONE_TRACKER_APP_WATCHDOG_H
#define DRONE_TRACKER_APP_WATCHDOG_H

#include <stdint.h>

void app_watchdog_start(void);
int app_watchdog_observe(uint32_t acq_progress, uint32_t gps_progress);

#endif
