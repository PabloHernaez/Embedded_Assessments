#include "simu_frame.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char expected[] =
        "$SIMU1,00152,0000123456,1,08,+40.389462,-003.627416,+00628.4,"
        "+0012.34,-0003.20,+0015.00,+000.50,-000.10,+001.20,"
        "+002.40,-001.10,+045.00,-000.12,+000.04,+009.81,"
        "+0000.5,-0001.2,+0000.1,073.5,+024.6,045.2,FLYING__,0000*3F\r\n";

    const AcquisitionSnapshot acq = {
        .nav = {
            .relative_q = {0.9235574231f, 0.0230200168f, -0.0008525778f, 0.3827676033f},
            .position_m = {12.34f, -3.20f, 15.00f},
            .velocity_mps = {0.50f, -0.10f, 1.20f},
        },
        .imu = {
            .accel_mps2 = {-0.12f, 0.04f, 9.81f},
            .gyro_dps = {0.5f, -1.2f, 0.1f},
        },
        .light = { .light_pct = 73.5f },
        .environment = { .temperature_c = 24.6f, .humidity_pct = 45.2f },
    };
    const GpsSnapshot gps = {
        .lat_udeg = 40389462,
        .lon_udeg = -3627416,
        .alt_mm = 628400,
        .satellites = 8U,
        .fix_valid = true,
        .last_fix_ms = 123000,
    };
    const TrackerStatus status = { .state = TRACKER_STATE_FLYING, .flags = 0U };
    const SimuFrameInput in = { &acq, &gps, &status, 152U, UINT64_C(123456) };
    char frame[SIMU_FRAME_BUFFER_SIZE];
    size_t len;
    uint16_t replacements;

    const int err = simu_frame_encode(&in, frame, sizeof(frame), &len, &replacements);
    if (err != 0 || len != SIMU_FRAME_WIRE_SIZE || replacements != 0U) {
        fprintf(stderr, "Encoder: err=%d, len=%zu, reemplazos=%u\n",
                err, len, (unsigned int)replacements);
        return 1;
    }

    if (memcmp(frame, expected, sizeof(expected)) != 0) {
        fprintf(stderr, "Esperada: %sObtenida: %s", expected, frame);
        return 1;
    }

    puts("SIMU: vector oficial OK");
    return 0;
}