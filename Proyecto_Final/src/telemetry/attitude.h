#ifndef DRONE_TRACKER_TELEMETRY_ATTITUDE_H
#define DRONE_TRACKER_TELEMETRY_ATTITUDE_H

#include <math.h>

#include "model.h"

/*
 * Cuaternion -> ROLL/PITCH/YAW en grados (convencion ZYX).
 * El seno del pitch se recorta a [-1, 1]: por redondeo puede pasar de 1 y asinf
 * daria NaN. A +-90 grados de pitch roll y yaw no se distinguen (es propio de los
 * angulos de Euler; el cuaternion no lo sufre) y atan2f(0, 0) devuelve 0.
 */
static inline Vec3f euler_degrees(Quaternionf q)
{
    const float deg = 57.2957795131f;
    const float s = fminf(1.0f, fmaxf(-1.0f, 2.0f * (q.w * q.y - q.x * q.z)));

    return (Vec3f){
        atan2f(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * deg,
        asinf(s) * deg,
        atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * deg,
    };
}

#endif
