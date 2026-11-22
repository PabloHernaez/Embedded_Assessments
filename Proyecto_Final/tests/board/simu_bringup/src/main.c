#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "acquisition_api.h"
#include "simu_frame.h"

K_TIMER_DEFINE(simu_timer, NULL, NULL);

static const struct device *const simu_uart =
    DEVICE_DT_GET(DT_NODELABEL(lpuart1));

static const GpsSnapshot gps_without_fix = {
    .last_nmea_ms = -1,
    .last_fix_ms = -1,
};

static TrackerStatus status_from_snapshot(const AcquisitionSnapshot *snapshot)
{
    TrackerStatus status = {
        .state = snapshot->moving ? TRACKER_STATE_FLYING : TRACKER_STATE_IDLE,
        .flags = TRACKER_FLAG_GPS_INVALID |
                 TRACKER_FLAG_INERTIAL_ONLY |
                 TRACKER_FLAG_YAW_UNRELIABLE,
    };

    if (!snapshot->imu.quality.valid || snapshot->imu.range_limited) {
        status.flags |= TRACKER_FLAG_IMU_INVALID;
    }
    if (snapshot->imu.calibration_pending) {
        status.flags |= TRACKER_FLAG_CALIBRATION_PENDING;
    }
    if (!snapshot->light.quality.valid || snapshot->light.clipped) {
        status.flags |= TRACKER_FLAG_LIGHT_WARNING;
    }
    if (!snapshot->environment.quality.valid) {
        status.flags |= TRACKER_FLAG_ENV_INVALID;
    }

    return status;
}

int main(void)
{
    char frame[SIMU_FRAME_BUFFER_SIZE];
    uint16_t seq = 0U;

    if (!device_is_ready(simu_uart)) {
        return -ENODEV;
    }

    k_timer_start(&simu_timer, K_MSEC(100), K_MSEC(100));

    while (true) {
        k_timer_status_sync(&simu_timer);

        AcquisitionSnapshot snapshot;
        acquisition_snapshot_get(&snapshot);

        const TrackerStatus status = status_from_snapshot(&snapshot);
        const SimuFrameInput input = {
            .acquisition = &snapshot,
            .gps = &gps_without_fix,
            .status = &status,
            .seq = seq,
            .uptime_ms = (uint64_t)k_uptime_get(),
        };

        size_t len;
        uint16_t replacements;
        int err = simu_frame_encode(&input, frame, sizeof(frame),
                                    &len, &replacements);
        if (err != 0 || len != SIMU_FRAME_WIRE_SIZE) {
            return err != 0 ? err : -EIO;
        }

        for (size_t i = 0U; i < len; ++i) {
            uart_poll_out(simu_uart, (unsigned char)frame[i]);
        }
        ++seq;
    }
}