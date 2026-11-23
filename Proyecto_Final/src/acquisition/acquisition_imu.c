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
        .x = (m.x - TRACKER_ACCEL_OFFSET_X) * TRACKER_ACCEL_SCALE_X,
        .y = (m.y - TRACKER_ACCEL_OFFSET_Y) * TRACKER_ACCEL_SCALE_Y,
        .z = (m.z - TRACKER_ACCEL_OFFSET_Z) * TRACKER_ACCEL_SCALE_Z,
    };
}

/* Movimiento y navegacion con la muestra calibrada que hay en ctx->data. */
static void inertial_step(AcquisitionImu *ctx)
{
    float dt_s = 0.0f; /* primera muestra o tras una ruptura: no integrar */

    if (ctx->previous_imu_ticks >= 0) {
        const int64_t elapsed = ctx->sample_ticks - ctx->previous_imu_ticks;

        if (elapsed <= (int64_t)k_ms_to_ticks_floor64(TRACKER_IMU_MAX_GAP_MS)) {
            dt_s = (float)elapsed / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
        } else {
            motion_interrupt(&ctx->motion); /* hueco > 50 ms: tramo desconocido */
        }
    }
    ctx->previous_imu_ticks = ctx->sample_ticks;

    ctx->accel_norm_mps2 = vector_norm(ctx->data.accel_mps2);
    ctx->gyro_norm_dps = vector_norm(ctx->data.gyro_dps);
    ctx->moving = motion_update(&ctx->motion, ctx->accel_norm_mps2, ctx->gyro_norm_dps,
                                ctx->data.quality.last_good_ms);

    estimator_update(&ctx->nav, &ctx->calibration.gravity_mps2, &ctx->data.accel_mps2,
                     &ctx->data.gyro_dps, dt_s);
}

/*
 * Muestra perdida (error, saturacion o reinicio): calidad invalida y continuidad
 * rota. Se conservan los ultimos valores, q/v/p, el sesgo y g0 ya calculados.
 */
static void imu_interrupt(AcquisitionImu *ctx)
{
    if (!ctx->calibration.ready) {
        imu_calibration_init(&ctx->calibration);
    }
    motion_interrupt(&ctx->motion);
    ctx->previous_imu_ticks = -1;
    ctx->data.quality.valid = false;
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
        .previous_imu_ticks = -1,
        .sample_ticks = -1,
    };
    imu_calibration_init(&ctx->calibration);
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
    const int64_t sample_ms = k_ticks_to_ms_floor64(ctx->sample_ticks);

    if (reading.range_limited) {
        imu_interrupt(ctx);
        ctx->data.range_limited = true;
        ctx->data.last_impact_ms = sample_ms; /* un eje pasa de 4 g: golpe */
        return true;
    }

    /* Hasta tener 100 muestras en reposo no hay sesgo ni g0: solo se calibra. */
    const Vec3f accel = accel_correct(reading.accel_mps2);
    const bool impact = vector_norm(accel) >= TRACKER_IMPACT_ACCEL_MPS2;
    imu_calibration_update(&ctx->calibration, &accel, &reading.gyro_dps);

    const Vec3f bias = ctx->calibration.ready ? ctx->calibration.gyro_bias_dps : (Vec3f){0};
    ctx->data = (ImuData){
        .accel_mps2 = accel,
        .gyro_dps = {
            reading.gyro_dps.x - bias.x,
            reading.gyro_dps.y - bias.y,
            reading.gyro_dps.z - bias.z,
        },
        .quality = { .valid = true, .last_good_ms = sample_ms },
        .calibration_pending = !ctx->calibration.ready,
        .last_impact_ms = impact ? sample_ms : ctx->data.last_impact_ms,
    };

    if (ctx->calibration.ready) {
        inertial_step(ctx);
    }
    return true;
}
