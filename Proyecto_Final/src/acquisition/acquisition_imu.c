#include "acquisition_imu.h"

#include <math.h>
#include <zephyr/kernel.h>

#include "../algorithms/estimator.h"
#include "../drivers/imu.h"

/*
 * Recuperacion: tras 3 errores seguidos, o si el MPU deja de dar muestras
 * 500 ms (p. ej. un reset silencioso lo deja dormido), se recupera el bus y se
 * reinicializa. Como mucho un intento por segundo.
 */
#define IMU_MAX_ERRORS   3U
#define IMU_SILENT_MS    500
#define IMU_RETRY_MS     1000

static float vector_norm(Vec3f v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

/* Offset y escala del acelerometro medidos en seis posturas (registro D3). */
static Vec3f accel_correct(Vec3f m)
{
    return (Vec3f){
        .x = (m.x - 0.12409f) * 0.9964143f,
        .y = (m.y + 0.06164f) * 0.9966736f,
        .z = (m.z + 0.05113f) * 0.9891509f,
    };
}

static void calibration_reset(AcquisitionImu *ctx)
{
    gyro_calibration_init(&ctx->calibration);
    ctx->calibration_accel_sum = (Vec3f){0};
}

/*
 * Calibracion inicial: 100 muestras seguidas en reposo dan el sesgo del gyro,
 * y la media de la aceleracion de esas mismas muestras da la inclinacion inicial.
 */
static void calibration_step(AcquisitionImu *ctx, Vec3f accel, Vec3f gyro)
{
    if (ctx->calibration.ready) {
        return;
    }

    gyro_calibration_update(&ctx->calibration, &gyro, vector_norm(accel), vector_norm(gyro));
    if (ctx->calibration.count == 0U) {
        ctx->calibration_accel_sum = (Vec3f){0}; /* se movio: empezar de nuevo */
        return;
    }

    ctx->calibration_accel_sum.x += accel.x;
    ctx->calibration_accel_sum.y += accel.y;
    ctx->calibration_accel_sum.z += accel.z;

    if (ctx->calibration.ready) {
        const float n = (float)ctx->calibration.count;
        const Vec3f mean = {
            ctx->calibration_accel_sum.x / n,
            ctx->calibration_accel_sum.y / n,
            ctx->calibration_accel_sum.z / n,
        };

        if (estimator_set_initial_tilt(&ctx->nav, &mean)) {
            ctx->initial_q = ctx->nav.q;
        } else {
            calibration_reset(ctx); /* la media no es gravedad: repetir */
        }
    }
}

/* Movimiento y navegacion con la muestra calibrada que hay en ctx->data. */
static void inertial_step(AcquisitionImu *ctx)
{
    float dt_s = 0.0f;

    if (ctx->previous_imu_ticks >= 0) {
        const int64_t elapsed = ctx->sample_ticks - ctx->previous_imu_ticks;

        if (elapsed > (int64_t)k_ms_to_ticks_floor64(TRACKER_IMU_MAX_GAP_MS)) {
            /* Hueco > 50 ms: no integrar un tramo desconocido. */
            motion_interrupt(&ctx->motion);
            ctx->navigation_time_pending = true;
        } else {
            dt_s = (float)elapsed / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
        }
    }
    ctx->previous_imu_ticks = ctx->sample_ticks;

    ctx->accel_norm_mps2 = vector_norm(ctx->data.accel_mps2);
    ctx->gyro_norm_dps = vector_norm(ctx->data.gyro_dps);
    ctx->moving = motion_update(&ctx->motion, ctx->accel_norm_mps2, ctx->gyro_norm_dps,
                                ctx->data.quality.last_good_ms);

    if (ctx->navigation_time_pending) {
        dt_s = 0.0f; /* retomar el tiempo sin integrar */
    }
    ctx->navigation_time_pending =
        !estimator_update(&ctx->nav, &ctx->initial_q, &ctx->data.accel_mps2,
                          &ctx->data.gyro_dps, dt_s);
}

/*
 * Muestra perdida (error, saturacion o reinicio): calidad invalida y continuidad
 * rota. Se conservan los ultimos valores, q/v/p, el sesgo y q0 ya calculados.
 */
static void imu_interrupt(AcquisitionImu *ctx)
{
    if (!ctx->calibration.ready) {
        calibration_reset(ctx);
    }
    motion_interrupt(&ctx->motion);
    ctx->previous_imu_ticks = -1;
    ctx->navigation_time_pending = true;
    ctx->data.quality.valid = false;
    ctx->nav.valid = false;
}

static void imu_restart(AcquisitionImu *ctx)
{
    imu_interrupt(ctx);
    (void)imu_recover_bus(); /* inocuo con el bus libre; libera un SDA retenido */
    ctx->last_init_ticks = k_uptime_ticks();
    ctx->configured = imu_init() == 0;
    ctx->consecutive_errors = 0U;
    ctx->last_new_ticks = k_uptime_ticks();
    ctx->bus_error = true; /* el Si7021 debe revalidarse tras tocar el bus */
}

void acquisition_imu_init(AcquisitionImu *ctx)
{
    *ctx = (AcquisitionImu){
        .data = {
            .quality = { .valid = false, .last_good_ms = -1 },
            .calibration_pending = true,
        },
        .initial_q = { .w = 1.0f },
        .navigation_time_pending = true,
        .previous_imu_ticks = -1,
        .sample_ticks = -1,
    };
    calibration_reset(ctx);
    motion_init(&ctx->motion);
    estimator_init(&ctx->nav);
    imu_restart(ctx);
}

bool acquisition_imu_update(AcquisitionImu *ctx)
{
    const int64_t now = k_uptime_ticks();

    ctx->bus_error = false;

    if (!ctx->configured || ctx->consecutive_errors >= IMU_MAX_ERRORS ||
        now - ctx->last_new_ticks > (int64_t)k_ms_to_ticks_ceil64(IMU_SILENT_MS)) {
        if (now - ctx->last_init_ticks < (int64_t)k_ms_to_ticks_ceil64(IMU_RETRY_MS)) {
            return false;
        }
        imu_restart(ctx);
        return true;
    }

    ImuReading reading;
    const int ret = imu_read(&reading);

    ctx->sample_ticks = k_uptime_ticks();

    if (ret == IMU_NO_NEW) {
        return false;
    }
    if (ret < 0) {
        ctx->consecutive_errors++;
        ctx->bus_error = true;
        imu_interrupt(ctx);
        return true;
    }

    ctx->consecutive_errors = 0U;
    ctx->last_new_ticks = ctx->sample_ticks;

    if (reading.range_limited) {
        imu_interrupt(ctx);
        ctx->data.range_limited = true;
        return true;
    }

    const Vec3f accel = accel_correct(reading.accel_mps2);
    calibration_step(ctx, accel, reading.gyro_dps);

    const Vec3f bias = ctx->calibration.ready ? ctx->calibration.bias_dps : (Vec3f){0};
    ctx->data = (ImuData){
        .accel_mps2 = accel,
        .gyro_dps = {
            reading.gyro_dps.x - bias.x,
            reading.gyro_dps.y - bias.y,
            reading.gyro_dps.z - bias.z,
        },
        .quality = { .valid = true, .last_good_ms = k_ticks_to_ms_floor64(ctx->sample_ticks) },
        .calibration_pending = !ctx->calibration.ready,
    };

    if (ctx->calibration.ready) {
        inertial_step(ctx);
    }
    return true;
}
