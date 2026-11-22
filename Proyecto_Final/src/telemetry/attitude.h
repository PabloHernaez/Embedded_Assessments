#ifndef DRONE_TRACKER_TELEMETRY_ATTITUDE_H
#define DRONE_TRACKER_TELEMETRY_ATTITUDE_H

#include "model.h"
#include <math.h>

static inline Vec3f euler_degrees(Quaternionf q, uint16_t *replacements)
{
    if (!isfinite(q.w) || !isfinite(q.x) || !isfinite(q.y) || !isfinite(q.z)) {
        *replacements += 3U;
        return (Vec3f){0};
    }

    const float s = fminf(1.0f, fmaxf(-1.0f, 2.0f * (q.w*q.y - q.x*q.z)));
    const float degrees_per_rad = 57.2957795131f;

    if (1.0f - fabsf(s) <= 1.0e-6f) {
        return (Vec3f){
            atan2f(2.0f * (q.w*q.x - q.y*q.z),
                   1.0f - 2.0f * (q.x*q.x + q.z*q.z)) * degrees_per_rad,
            copysignf(90.0f, s),
            0.0f
        };
    }

    return (Vec3f){
        atan2f(2.0f * (q.w*q.x + q.y*q.z),
               1.0f - 2.0f * (q.x*q.x + q.y*q.y)) * degrees_per_rad,
        asinf(s) * degrees_per_rad,
        atan2f(2.0f * (q.w*q.z + q.x*q.y),
               1.0f - 2.0f * (q.y*q.y + q.z*q.z)) * degrees_per_rad
    };
}

#endif
