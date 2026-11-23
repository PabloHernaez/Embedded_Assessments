#include "imu_calibration.h"

#include <math.h>

static float norm(const Vec3f *v)
{
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

static void add(Vec3f *sum, const Vec3f *v)
{
    sum->x += v->x;
    sum->y += v->y;
    sum->z += v->z;
}

static Vec3f mean(const Vec3f *sum, float n)
{
    return (Vec3f){ sum->x / n, sum->y / n, sum->z / n };
}

void imu_calibration_init(ImuCalibration *c)
{
    *c = (ImuCalibration){0};
}

void imu_calibration_update(ImuCalibration *c, const Vec3f *accel_mps2, const Vec3f *gyro_dps)
{
    if (c->ready) {
        return;
    }

    const bool at_rest =
        fabsf(norm(accel_mps2) - TRACKER_GRAVITY_MPS2) < TRACKER_REST_ACCEL_DELTA_MPS2 &&
        norm(gyro_dps) < TRACKER_GYRO_CALIBRATION_MAX_DPS;
    if (!at_rest) {
        imu_calibration_init(c); /* se movio: empezar de nuevo */
        return;
    }

    add(&c->gyro_sum_dps, gyro_dps);
    add(&c->accel_sum_mps2, accel_mps2);
    c->count++;

    if (c->count == TRACKER_CALIBRATION_SAMPLES) {
        c->gyro_bias_dps = mean(&c->gyro_sum_dps, (float)c->count);
        c->gravity_mps2 = mean(&c->accel_sum_mps2, (float)c->count);
        c->ready = true;
    }
}
