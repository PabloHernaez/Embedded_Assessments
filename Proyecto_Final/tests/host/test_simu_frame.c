#include "simu_frame.h"
#include "attitude.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *what, const char *expected, const char *frame)
{
    fprintf(stderr, "%s\nEsperada: %s\nObtenida: %s\n", what, expected, frame);
    return 1;
}

int main(void)
{
    char frame[SIMU_FRAME_BUFFER_SIZE];

    /* Vector oficial del enunciado (2.4.8), XOR 3F. */
    static const char official[] =
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
        .lat_udeg = 40389462, .lon_udeg = -3627416, .alt_mm = 628400,
        .satellites = 8U, .fix_valid = true, .last_fix_ms = 123000,
    };
    const TrackerStatus flying = { .state = TRACKER_STATE_FLYING, .flags = 0U };
    const SimuFrameInput in = { &acq, &gps, &flying, 152U, UINT64_C(123456) };

    if (simu_frame_encode(&in, frame) != 0 || strcmp(frame, official) != 0) {
        return fail("Vector oficial", official, frame);
    }

    /* Sin fix ni posicion previa: GF=0, SAT=00 y ceros. Valores acotados a su
     * campo y NaN como cero; -0,001 se escribe -0000.00 (formato valido). */
    static const char edge_prefix[] =
        "$SIMU1,65535,9999999999,0,00,+00.000000,+000.000000,+00000.0,"
        "+9999.99,-9999.99,-0000.00,+999.99,-000.01,+000.00,"
        "+000.00,+000.00,+000.00,+000.00,+000.00,+000.00,"
        "+0000.0,+0000.0,-9999.9,100.0,-040.0,000.0,PROBLEM_,0043*";
    const AcquisitionSnapshot extreme = {
        .nav = {
            .relative_q = {1.0f, 0.0f, 0.0f, 0.0f},
            .position_m = {1.0e9f, -12345.0f, -0.001f},
            .velocity_mps = {1000.0f, -0.006f, NAN},
        },
        .imu.gyro_dps = {0.0f, 0.0f, -1.0e6f},
        .light.light_pct = 120.0f,
        .environment = { .temperature_c = -40.0f, .humidity_pct = -3.0f },
    };
    const GpsSnapshot no_gps = { .satellites = 7U, .last_nmea_ms = -1, .last_fix_ms = -1 };
    const TrackerStatus problem = { .state = TRACKER_STATE_PROBLEM, .flags = 0x0043U };
    const SimuFrameInput edge = { &extreme, &no_gps, &problem, 65535U, UINT64_C(9999999999) };

    if (simu_frame_encode(&edge, frame) != 0 || strlen(frame) != SIMU_FRAME_WIRE_SIZE ||
        strncmp(frame, edge_prefix, sizeof(edge_prefix) - 1U) != 0) {
        return fail("Limites", edge_prefix, frame);
    }

    /* Un TMS de 11 cifras no cabe en el campo: no se genera trama. */
    const SimuFrameInput too_late = { &acq, &gps, &flying, 0U, UINT64_C(10000000000) };
    if (simu_frame_encode(&too_late, frame) == 0) {
        return fail("TMS de 11 cifras aceptado", "-EIO", frame);
    }

    /* Euler ZYX: identidad, yaw 90, roll 30 y pitch 90 (finito, sin rama especial). */
    const float s = sqrtf(0.5f);
    const Vec3f e0 = euler_degrees((Quaternionf){1.0f, 0.0f, 0.0f, 0.0f});
    const Vec3f yaw = euler_degrees((Quaternionf){s, 0.0f, 0.0f, s});
    const Vec3f roll = euler_degrees((Quaternionf){cosf(0.2617994f), sinf(0.2617994f), 0.0f, 0.0f});
    const Vec3f pitch = euler_degrees((Quaternionf){s, 0.0f, s, 0.0f});
    if (fabsf(e0.x) + fabsf(e0.y) + fabsf(e0.z) > 1e-4f ||
        fabsf(yaw.z - 90.0f) > 1e-3f || fabsf(yaw.x) + fabsf(yaw.y) > 1e-3f ||
        fabsf(roll.x - 30.0f) > 1e-3f || fabsf(roll.y) + fabsf(roll.z) > 1e-3f ||
        fabsf(pitch.y - 90.0f) > 0.05f || /* en float asin(0,99999994) = 89,98 grados */
        !isfinite(pitch.x) || !isfinite(pitch.z)) {
        fprintf(stderr, "Euler incorrecto: yaw=%f roll=%f pitch=%f\n", yaw.z, roll.x, pitch.y);
        return 1;
    }

    puts("SIMU: vector oficial, limites y Euler OK");
    return 0;
}
