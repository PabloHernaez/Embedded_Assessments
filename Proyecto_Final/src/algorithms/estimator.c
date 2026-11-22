#include "estimator.h"

#include <math.h>

#define DEG_TO_RAD (3.14159265358979323846f / 180.0f)

void estimator_init(NavigationView *nav)
{
    *nav = (NavigationView){ .q = { .w = 1.0f } };
}

/* Producto de Hamilton a * b (no conmutativo). */
static Quaternionf multiply(Quaternionf a, Quaternionf b)
{
    return (Quaternionf){
        .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

/* Rota v con el cuaternion unitario q: de ejes del sensor a ejes iniciales. */
static Vec3f rotate(Quaternionf q, Vec3f v)
{
    return (Vec3f){
        .x = (1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * v.x +
             2.0f * (q.x * q.y - q.w * q.z) * v.y +
             2.0f * (q.x * q.z + q.w * q.y) * v.z,
        .y = 2.0f * (q.x * q.y + q.w * q.z) * v.x +
             (1.0f - 2.0f * (q.x * q.x + q.z * q.z)) * v.y +
             2.0f * (q.y * q.z - q.w * q.x) * v.z,
        .z = 2.0f * (q.x * q.z - q.w * q.y) * v.x +
             2.0f * (q.y * q.z + q.w * q.x) * v.y +
             (1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * v.z,
    };
}

void estimator_update(NavigationView *nav, const Vec3f *gravity_mps2,
                      const Vec3f *accel_mps2, const Vec3f *gyro_dps, float dt_s)
{
    /*
     * 1. Orientacion: q += 1/2 * q * (0, w) * dt, con w en rad/s, y normalizar.
     *    El incremento es perpendicular a q, asi que la norma nunca baja de 1.
     */
    const Quaternionf w = { 0.0f, gyro_dps->x * DEG_TO_RAD, gyro_dps->y * DEG_TO_RAD,
                            gyro_dps->z * DEG_TO_RAD };
    const Quaternionf dq = multiply(nav->q, w);
    const float h = 0.5f * dt_s;
    Quaternionf q = { nav->q.w + h * dq.w, nav->q.x + h * dq.x,
                      nav->q.y + h * dq.y, nav->q.z + h * dq.z };
    const float n = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    nav->q = (Quaternionf){ q.w / n, q.x / n, q.y / n, q.z / n };

    /* 2. Aceleracion en ejes iniciales menos la gravedad medida en reposo. */
    const Vec3f a = rotate(nav->q, *accel_mps2);
    const Vec3f linear = { a.x - gravity_mps2->x, a.y - gravity_mps2->y, a.z - gravity_mps2->z };

    /* 3. Euler explicito: la posicion usa la velocidad anterior. */
    nav->position_m.x += nav->velocity_mps.x * dt_s;
    nav->position_m.y += nav->velocity_mps.y * dt_s;
    nav->position_m.z += nav->velocity_mps.z * dt_s;
    nav->velocity_mps.x += linear.x * dt_s;
    nav->velocity_mps.y += linear.y * dt_s;
    nav->velocity_mps.z += linear.z * dt_s;
}
