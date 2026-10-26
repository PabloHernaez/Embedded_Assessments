#include "gyro_calibration.h"

#include <math.h>
#include <stddef.h>

void gyro_calibration_init(GyroCalibration *ctx)
{
    *ctx = (GyroCalibration){0};
}

bool gyro_calibration_update(
    GyroCalibration *ctx,
    const Vec3f *gyro_dps,
    float accel_norm,
    float gyro_norm
)
{
    /* Conservar una calibracion ya terminada. */
    if (ctx->ready) {
        return true;
    }

    bool stationary =
        gyro_dps != NULL &&
        fabsf(accel_norm - TRACKER_GRAVITY_MPS2) < TRACKER_REST_ACCEL_DELTA_MPS2 &&
        gyro_norm < TRACKER_REST_GYRO_MAX_DPS;

    if (!stationary) {
        ctx->sum_dps = (Vec3f){0};
        ctx->count = 0;
        return false;
    }

    ctx->sum_dps.x += gyro_dps->x;
    ctx->sum_dps.y += gyro_dps->y;
    ctx->sum_dps.z += gyro_dps->z;
    ctx->count++;

    if (ctx->count == TRACKER_GYRO_CALIBRATION_SAMPLES) {
        float divisor = (float)ctx->count;

        ctx->bias_dps = (Vec3f){
            .x = ctx->sum_dps.x / divisor,
            .y = ctx->sum_dps.y / divisor,
            .z = ctx->sum_dps.z / divisor,
        };

        ctx->ready = true;
    }

    return ctx->ready;
}