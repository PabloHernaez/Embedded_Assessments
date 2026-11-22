#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "acquisition_api.h"
#include "simu_frame.h"
#include "gps_api.h"
#include "tracker_status.h"

K_TIMER_DEFINE(simu_timer, NULL, NULL);

static const struct device *const simu_uart =
    DEVICE_DT_GET(DT_NODELABEL(lpuart1));

int main(void)
{
    char frame[SIMU_FRAME_BUFFER_SIZE];
    uint16_t seq = 0U;
    TrackerHealth health = { .healthy_since_ms = -1 };

    if (!device_is_ready(simu_uart)) {
        return -ENODEV;
    }

    k_timer_start(&simu_timer, K_MSEC(100), K_MSEC(100));

    while (true) {
        k_timer_status_sync(&simu_timer);

        AcquisitionSnapshot snapshot;
        acquisition_snapshot_get(&snapshot);

        GpsSnapshot gps;
        gps_snapshot_get(&gps);

        const int64_t now_ms = k_uptime_get();
        const TrackerStatus status =
            tracker_status_from_snapshots(&health, &snapshot, &gps, now_ms, false);
        const SimuFrameInput input = {
            .acquisition = &snapshot,
            .gps = &gps,
            .status = &status,
            .seq = seq,
            .uptime_ms = (uint64_t)now_ms,
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