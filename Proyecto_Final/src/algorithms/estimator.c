#include "estimator.h"
#include "algorithm_config.h"

#include <math.h>

#define DEG_TO_RAD (3.14159265358979323846f / 180.0f)

/*El estimador arranca con un cuaternión identidad*/
void estimator_init(NavigationView *state)
{
    *state = (NavigationView){
        .q = {
            .w = 1.0f,
            .x = 0.0f,
            .y = 0.0f,
            .z = 0.0f,
        },
        .velocity_mps = {0.0f, 0.0f, 0.0f},
        .position_m = {0.0f, 0.0f, 0.0f},
        .valid = false,
    };
}

/*
 * Producto Hamilton: el orden a * b importa.
 *  Recibe dos cuaterniones, a y b.
 *  La función relaciona orientación actual con giro medido
 */
static Quaternionf quaternion_multiply(Quaternionf a, Quaternionf b)
{
    return (Quaternionf){
        .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

/**
 *  Normalización del cuaternión
 *      *q  -> puntero a cuaternión
 *
 *      1. Halla la magnitud del vector cuaternión
 *      2. Normaliza el vector dividiéndolo por su magnitud
 */
static bool quaternion_normalize(Quaternionf *q)
{
    float norm = sqrtf(
        q->w * q->w +
        q->x * q->x +
        q->y * q->y +
        q->z * q->z
    );

    if (!isfinite(norm) || norm <= 0.0f) {
        return false;
    }

    q->w /= norm;
    q->x /= norm;
    q->y /= norm;
    q->z /= norm;

    return true;
}

/**
 *  Estimador de la orientación del cuaternión
 *      *q          -> puntero a cuaternión de orientación actual
 *      *gyro_dps   -> puntero a giro
 *      dt_s        ->
 *
 *
 *
 *
 */

bool estimator_orientation_update(Quaternionf *q, const Vec3f *gyro_dps, float dt_s)
{
    /*Se comprueba que el intervalo sea máximo de TRACKER_IMU_MAX_GAP_MS (50ms)*/
    const float max_dt_s = TRACKER_IMU_MAX_GAP_MS / 1000.0f;
    if (!isfinite(dt_s) || dt_s < 0.0f || dt_s > max_dt_s) {
        return false;
    }

    /*omega es el giro en rad/s como cuaternión con w=0*/
    Quaternionf omega = {
        .w = 0.0f,
        .x = gyro_dps->x * DEG_TO_RAD,
        .y = gyro_dps->y * DEG_TO_RAD,
        .z = gyro_dps->z * DEG_TO_RAD,
    };

    /**
     *  candidate es el avance. Se multiplica (mult. Hamilton) orientación actual
     *      por cuaternión de giro omega
     */

    Quaternionf product = quaternion_multiply(*q, omega);
    float half_dt = 0.5f * dt_s;

    Quaternionf candidate = {
        .w = q->w + product.w * half_dt,
        .x = q->x + product.x * half_dt,
        .y = q->y + product.y * half_dt,
        .z = q->z + product.z * half_dt,
    };

    if (!quaternion_normalize(&candidate)) {
        return false;
    }

    *q = candidate;
    return true;
}
