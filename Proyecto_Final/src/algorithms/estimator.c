#include "estimator.h"
#include "algorithm_config.h"

#include <math.h>

#define DEG_TO_RAD (3.14159265358979323846f / 180.0f)

/* Orientacion identidad, v/p a cero y valid = false. */
void estimator_init(NavigationView *state)
{
    *state = (NavigationView){
        .q = {.w = 1.0f},
        .relative_q = {.w = 1.0f},
        .velocity_mps = {0},
        .position_m = {0},
        .valid = false,
    };
}

/* Producto de Hamilton a * b (no conmutativo). */
static Quaternionf quaternion_multiply(Quaternionf a, Quaternionf b)
{
    return (Quaternionf){
        .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

/* Divide q por su norma; false si la norma es 0 o no finita. */
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

bool estimator_relative_orientation(
    Quaternionf *relative,
    const Quaternionf *initial,
    const Quaternionf *current
)
{
    /* q_rel = q0^-1 * q; para un cuaternion unitario la inversa es el conjugado. */
    Quaternionf initial_inverse = {
        .w = initial->w,
        .x = -initial->x,
        .y = -initial->y,
        .z = -initial->z,
    };

    Quaternionf candidate =
        quaternion_multiply(initial_inverse, *current);

    if (!quaternion_normalize(&candidate)) {
        return false;
    }

    *relative = candidate;
    return true;
}

bool estimator_set_initial_tilt(NavigationView *state, const Vec3f *accel_mean_mps2)
{
    float ax = accel_mean_mps2->x;
    float ay = accel_mean_mps2->y;
    float az = accel_mean_mps2->z;

    float norm = sqrtf(ax * ax + ay * ay + az * az);

    if (!isfinite(norm) ||
        fabsf(norm - TRACKER_GRAVITY_MPS2) >= TRACKER_REST_ACCEL_DELTA_MPS2) {
        return false;
    }

    /* Inclinacion inicial, expresada en radianes. */
    float roll = atan2f(ay, az);
    float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));

    float cr = cosf(0.5f * roll);
    float sr = sinf(0.5f * roll);
    float cp = cosf(0.5f * pitch);
    float sp = sinf(0.5f * pitch);

    /* Cuaternion Hamilton para roll/pitch y yaw inicial cero. */
    Quaternionf candidate = {
        .w = cr * cp,
        .x = sr * cp,
        .y = cr * sp,
        .z = -sr * sp,
    };

    if (!quaternion_normalize(&candidate)) {
        return false;
    }

    state->q = candidate;
    state->relative_q = (Quaternionf){.w = 1.0f};
    state->valid = false;

    return true;
}

/* Euler sobre q: q += 0.5 * q * (0, w) * dt, con w en rad/s, y normalizar. */

bool estimator_orientation_update(Quaternionf *q, const Vec3f *gyro_dps, float dt_s)
{
    const float max_dt_s = TRACKER_IMU_MAX_GAP_MS / 1000.0f;
    if (!isfinite(dt_s) || dt_s < 0.0f || dt_s > max_dt_s) {
        return false;
    }

    Quaternionf omega = {
        .w = 0.0f,
        .x = gyro_dps->x * DEG_TO_RAD,
        .y = gyro_dps->y * DEG_TO_RAD,
        .z = gyro_dps->z * DEG_TO_RAD,
    };

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

/* Rota un vector del marco del sensor al marco local con q unitario. */
static Vec3f quaternion_rotate(Quaternionf q, Vec3f vector)
{
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;

    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;

    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    return (Vec3f){
        .x = (1.0f - 2.0f * (yy + zz)) * vector.x +
             2.0f * (xy - wz) * vector.y +
             2.0f * (xz + wy) * vector.z,

        .y = 2.0f * (xy + wz) * vector.x +
             (1.0f - 2.0f * (xx + zz)) * vector.y +
             2.0f * (yz - wx) * vector.z,

        .z = 2.0f * (xz - wy) * vector.x +
             2.0f * (yz + wx) * vector.y +
             (1.0f - 2.0f * (xx + yy)) * vector.z,
    };
}

static bool vector_is_finite(Vec3f vector)
{
    return isfinite(vector.x) && isfinite(vector.y) && isfinite(vector.z);
}

bool estimator_update(NavigationView *state, const Quaternionf *initial_q,
    const Vec3f *accel_mps2, const Vec3f *gyro_dps, float dt_s)
{
    NavigationView candidate = *state;

    if (!estimator_orientation_update(
            &candidate.q, gyro_dps, dt_s) ||
        !estimator_relative_orientation(
            &candidate.relative_q, initial_q, &candidate.q)) {

        state->valid = false;
        return false;
    }

    /* Aceleracion en el marco local menos la gravedad (Z arriba). */
    Vec3f linear_accel = quaternion_rotate(candidate.q, *accel_mps2);
    linear_accel.z -= TRACKER_GRAVITY_MPS2;

    /* Euler explicito: la posicion utiliza la velocidad anterior. */
    candidate.position_m = (Vec3f){
        .x = state->position_m.x + state->velocity_mps.x * dt_s,
        .y = state->position_m.y + state->velocity_mps.y * dt_s,
        .z = state->position_m.z + state->velocity_mps.z * dt_s,
    };

    candidate.velocity_mps = (Vec3f){
        .x = state->velocity_mps.x + linear_accel.x * dt_s,
        .y = state->velocity_mps.y + linear_accel.y * dt_s,
        .z = state->velocity_mps.z + linear_accel.z * dt_s,
    };

    if (!vector_is_finite(candidate.velocity_mps) ||
        !vector_is_finite(candidate.position_m)) {
        state->valid = false;
        return false;
    }

    candidate.valid = true;
    *state = candidate;
    return true;
}
