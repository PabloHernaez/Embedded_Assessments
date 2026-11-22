#ifndef DRONE_TRACKER_TELEMETRY_ATTITUDE_H
#define DRONE_TRACKER_TELEMETRY_ATTITUDE_H

#include <math.h>

#include "model.h"

/*
 * Cuaternion Hamilton -> ROLL/PITCH/YAW en grados (convencion ZYX).
 * El seno del pitch se acota antes de asin; cerca de +-90 grados (gimbal lock)
 * el yaw no se distingue del roll y se fija a 0.
 */
static inline Vec3f euler_degrees(Quaternionf q)
{
    const float deg = 57.2957795131f;
    const float s = fminf(1.0f, fmaxf(-1.0f, 2.0f * (q.w * q.y - q.x * q.z)));

    if (1.0f - fabsf(s) <= 1.0e-6f) {
        return (Vec3f){
            atan2f(2.0f * (q.w * q.x - q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z)) * deg,
            copysignf(90.0f, s),
            0.0f,
        };
    }

    return (Vec3f){
        atan2f(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * deg,
        asinf(s) * deg,
        atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * deg,
    };
}

#endif
