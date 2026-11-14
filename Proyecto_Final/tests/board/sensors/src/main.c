#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#include "imu.h"
#include "environment.h"
#include "algorithm_config.h"
#include "gyro_calibration.h"
#include "motion.h"
#include "estimator.h"

/*Definición del temporizador*/
K_TIMER_DEFINE(sample_timer, NULL, NULL);

static const float seconds_per_tick = 1.0f / CONFIG_SYS_CLOCK_TICKS_PER_SEC;

/* Lectura y marca temporal de una sola vuelta. */
typedef struct {
    ImuReading reading;
    int result;
    int64_t ticks;
    int64_t now_ms;
} ImuSample;

/* Estado privado del ensayo, conservado entre vueltas. */
typedef struct {
    GyroCalibration calibration;
    MotionDetector motion;

    int64_t previous_imu_ticks;
    int64_t next_calibration_report_ms;
    int64_t next_motion_report_ms;
    int64_t next_environment_ms;
    int64_t environment_ready_ticks;

    uint32_t new_count;
    uint32_t no_new_count;
    uint32_t error_count;
    uint32_t saturation_count;
    uint32_t gap_count;

    NavigationView nav;
    bool navigation_reference_pending;
    Quaternionf initial_q;

    float last_dt_s;
    float min_dt_s;
    float max_dt_s;

    uint32_t navigation_error_count;
    Vec3f calibration_accel_sum;

} SensorTest;

/* Declaraciones para poder situar main antes de las implementaciones. */
static int test_init(SensorTest *ctx);
static ImuSample imu_sample_read(void);
static void calibration_step(SensorTest *ctx, const ImuSample *sample);
static void inertial_step(SensorTest *ctx, const ImuSample *sample);
static void navigation_step(SensorTest *ctx, const Vec3f *accel_mps2, const Vec3f *gyro_dps, float dt_s);
static void environment_step(SensorTest *ctx, const ImuSample *sample);
static void status_report(SensorTest *ctx, const ImuSample *sample);
static float vector_norm(Vec3f value);
//static void accel_calibration_run(void);
static Vec3f accel_correct(Vec3f measured);
static void calibration_finish(SensorTest *ctx);

int main(void)
{
    SensorTest test;

    if (test_init(&test) < 0) {
        return 0;
    }

    //accel_calibration_run();
    //return 0;

    while (true) {
        /**
         * Espera que el temporizador venza.
         * El hilo QUEDA BLOQUEADO. Eso libera al kernel para que haga otras cosas
         */
        k_timer_status_sync(&sample_timer);

        /*
         * Se lee el giroscopio y se guarda la lectura
         * El objeto ImuSample toma un ImuReading, el resultado, los ticks y el ts
         *      el ImuReading toma aceleración, giro y un indicador de saturación
         *      el resultado es nuevo, ninguna nueva o un error
         *          Solo si result = IMU_NEW se toma para la calibración
         *      ticks es la marca del reloj luego de leer
         *      now_ms es la misma marca pero en ms
        */
        ImuSample sample = imu_sample_read();

        /* Se realiza la calibración. Sample se renueva, test es acumulativo*/
        if (!test.calibration.ready) {
            calibration_step(&test, &sample);
            /**
             * El continue garantiza que se repite el muestreo mientras que .ready no sea true
             *      el true solo se obtiene
             */
            continue;
        }

        inertial_step(&test, &sample);
        environment_step(&test, &sample);
        status_report(&test, &sample);
    }

    return 0;
}

/* Inicializacion de sensores, contextos y temporizador. */
static int test_init(SensorTest *ctx)
{
    /*Estabilización de alimentación*/
    k_msleep(100);
    printk("\nEnsayo MPU6050 + Si7021\n");

    /*Inicialización de MPU6050 (Giroscopio acelerómetro)*/
    int ret = imu_init();

    printk("imu_init: %d\n", ret);
    if (ret < 0) {
        return ret;
    }
    /*Inicialización de Si7021 (termohigrómetro)*/
    ret = environment_init();

    printk("environment_init: %d\n", ret);
    if (ret < 0) {
        return ret;
    }

    /*
     * Los -1 son para indicar que no hay muestra para comparar tiempos ni conversión
     * ambiental pendiente
     */
    *ctx = (SensorTest){
        .previous_imu_ticks = -1,
        .environment_ready_ticks = -1,
        .navigation_reference_pending = true,
        .initial_q = {.w = 1.0f},
    };

    /*Arranca contexto de calibración y movimiento*/
    gyro_calibration_init(&ctx->calibration);
    motion_init(&ctx->motion);
    estimator_init(&ctx->nav);

    /*Arranca temporizador de cada 20ms, 50 lecturas por segundo*/
    k_timer_start(&sample_timer, K_MSEC(20), K_MSEC(20));

    printk("Calibrando: mantener el MPU quieto y nivelado.\n");

    return 0;
}

/* Tomar la marca temporal inmediatamente despues de leer la IMU. */
static ImuSample imu_sample_read(void)
{
    ImuSample sample = {0};

    sample.result = imu_read(&sample.reading);
    sample.ticks = k_uptime_ticks();
    sample.now_ms = k_ticks_to_ms_floor64(sample.ticks);

    if (sample.result == IMU_NEW &&
        !sample.reading.range_limited) {
        sample.reading.accel_mps2 =
            accel_correct(sample.reading.accel_mps2);
    }

    return sample;
}

/* Avanzar la calibracion inicial y mostrar su progreso. */
static void calibration_step(SensorTest *ctx, const ImuSample *sample)
{
    if (sample->result == IMU_NEW &&
        !sample->reading.range_limited) {

        float accel_norm = vector_norm(sample->reading.accel_mps2);
        float gyro_norm = vector_norm(sample->reading.gyro_dps);

        gyro_calibration_update(
            &ctx->calibration,
            &sample->reading.gyro_dps,
            accel_norm,
            gyro_norm
        );

        /*
         * Mientras la calibracion esta pendiente:
         * count == 0 significa que se rechazo la muestra.
         * count > 0 significa que se incorporo.
         */
        if (ctx->calibration.count == 0) {
            ctx->calibration_accel_sum = (Vec3f){0};
        } else {
            ctx->calibration_accel_sum.x +=
                sample->reading.accel_mps2.x;

            ctx->calibration_accel_sum.y +=
                sample->reading.accel_mps2.y;

            ctx->calibration_accel_sum.z +=
                sample->reading.accel_mps2.z;
        }

    } else if (sample->result < 0 ||
               (sample->result == IMU_NEW &&
                sample->reading.range_limited)) {

        gyro_calibration_update(&ctx->calibration, NULL, 0.0f, 0.0f);
        ctx->calibration_accel_sum = (Vec3f){0};
    }

    if (ctx->calibration.ready) {
        calibration_finish(ctx);
        return;
    }

    if (sample->now_ms >= ctx->next_calibration_report_ms) {
        printk(
            "Calibrando: %u/%u (imu=%d)\n",
            (unsigned int)ctx->calibration.count,
            TRACKER_GYRO_CALIBRATION_SAMPLES,
            sample->result
        );

        ctx->next_calibration_report_ms = sample->now_ms + 1000;
    }
}

static void calibration_finish(SensorTest *ctx)
{
    float divisor = (float)ctx->calibration.count;

    Vec3f accel_mean = {
        .x = ctx->calibration_accel_sum.x / divisor,
        .y = ctx->calibration_accel_sum.y / divisor,
        .z = ctx->calibration_accel_sum.z / divisor,
    };

    if (!estimator_set_initial_tilt(&ctx->nav, &accel_mean)) {
        printk("Inclinacion inicial no utilizable. Repitiendo calibracion.\n");

        gyro_calibration_init(&ctx->calibration);
        ctx->calibration_accel_sum = (Vec3f){0};
        return;
    }

    ctx->initial_q = ctx->nav.q;

    printk(
        "Sesgo gyro: %.3f %.3f %.3f deg/s\n",
        (double)ctx->calibration.bias_dps.x,
        (double)ctx->calibration.bias_dps.y,
        (double)ctx->calibration.bias_dps.z
    );

    printk(
        "ACC inicial media: %.4f %.4f %.4f m/s2 | norma=%.4f\n",
        (double)accel_mean.x,
        (double)accel_mean.y,
        (double)accel_mean.z,
        (double)vector_norm(accel_mean)
    );
}

/**
 *
 *
 *
 *
 *
 */

static void inertial_step(SensorTest *ctx, const ImuSample *sample)
{
    if (sample->result == IMU_NO_NEW) {
        ctx->no_new_count++;
        return;
    }

    if (sample->result != IMU_NEW || sample->reading.range_limited) {
        motion_interrupt(&ctx->motion);
        ctx->previous_imu_ticks = -1;

        ctx->navigation_reference_pending = true;
        ctx->nav.valid = false;

        if (sample->result < 0) {
            ctx->error_count++;
        } else {
            ctx->saturation_count++;
        }

        return;
    }

    ctx->new_count++;
    float dt_s = 0.0f;

    if (ctx->previous_imu_ticks >= 0) {
        int64_t elapsed_ticks =
            sample->ticks - ctx->previous_imu_ticks;

        if (elapsed_ticks >
            (int64_t)k_ms_to_ticks_floor64(TRACKER_IMU_MAX_GAP_MS)) {
            motion_interrupt(&ctx->motion);
            ctx->navigation_reference_pending = true;
            ctx->gap_count++;
        } else {
            dt_s = (float)elapsed_ticks * seconds_per_tick;
        }
    }

    ctx->previous_imu_ticks = sample->ticks;

    Vec3f corrected_gyro = {
        .x = sample->reading.gyro_dps.x - ctx->calibration.bias_dps.x,
        .y = sample->reading.gyro_dps.y - ctx->calibration.bias_dps.y,
        .z = sample->reading.gyro_dps.z - ctx->calibration.bias_dps.z,
    };

    float accel_norm = vector_norm(sample->reading.accel_mps2);
    float gyro_norm = vector_norm(corrected_gyro);
    bool was_moving = ctx->motion.moving;

    motion_update(&ctx->motion, accel_norm, gyro_norm, sample->now_ms);

    if (ctx->motion.moving != was_moving) {
        printk(
            "t=%lld ms | moving=%d | a=%.3f m/s2 | g=%.3f deg/s\n",
            (long long)sample->now_ms,
            (int)ctx->motion.moving,
            (double)accel_norm,
            (double)gyro_norm
        );
    }

    navigation_step(ctx, &sample->reading.accel_mps2, &corrected_gyro, dt_s);
}

static void navigation_step(SensorTest *ctx, const Vec3f *accel_mps2, const Vec3f *gyro_dps, float dt_s)
{
    if (ctx->navigation_reference_pending) {
        dt_s = 0.0f;
    }

    ctx->last_dt_s = dt_s;

    if (!estimator_update(
            &ctx->nav,
            &ctx->initial_q,
            accel_mps2,
            gyro_dps,
            dt_s)) {

        ctx->navigation_error_count++;
        ctx->navigation_reference_pending = true;
        return;
    }

    ctx->navigation_reference_pending = false;

    if (dt_s > 0.0f) {
        if (ctx->min_dt_s == 0.0f || dt_s < ctx->min_dt_s) {
            ctx->min_dt_s = dt_s;
        }

        if (dt_s > ctx->max_dt_s) {
            ctx->max_dt_s = dt_s;
        }
    }
}

/* Iniciar o recoger una medida ambiental sin esperar su conversion. */
static void environment_step(SensorTest *ctx, const ImuSample *sample)
{
    /* Tras un error de IMU, finalizar el trabajo de bus de esta vuelta. */
    if (sample->result < 0) {
        ctx->environment_ready_ticks = -1;
        ctx->next_environment_ms = sample->now_ms + 2000;
        return;
    }

    if (ctx->environment_ready_ticks >= 0) {
        /*Aquí se garantiza linealidad de sucesos*/
        if (k_uptime_ticks() < ctx->environment_ready_ticks) {
            return;
        }

        EnvironmentReading environment;
        int ret = environment_read(&environment);

        ctx->environment_ready_ticks = -1;

        if (ret == 0) {
            printk(
                "Temperatura: %.2f C | Humedad: %.2f %%\n",
                (double)environment.temperature_c,
                (double)environment.humidity_pct
            );
        } else {
            printk("environment_read: %d\n", ret);
        }

        return;
    }

    if (sample->now_ms < ctx->next_environment_ms) {
        return;
    }

    int ret = environment_start();

    ctx->next_environment_ms = k_uptime_get() + 2000;

    if (ret == 0) {
        /* Contar desde el final de F5 y conservar el tick de margen. */
        ctx->environment_ready_ticks =
            k_uptime_ticks() +
            k_ms_to_ticks_ceil64(ENVIRONMENT_CONVERSION_WAIT_MS) + 1;
    } else {
        printk("environment_start: %d\n", ret);
    }
}

/* Mostrar un resumen cada segundo. */
static void status_report(SensorTest *ctx, const ImuSample *sample)
{
    if (sample->now_ms < ctx->next_motion_report_ms) {
        return;
    }

    if (sample->result == IMU_NEW && !sample->reading.range_limited) {

    Vec3f accel = sample->reading.accel_mps2;

    Vec3f gyro = {
        .x = sample->reading.gyro_dps.x - ctx->calibration.bias_dps.x,
        .y = sample->reading.gyro_dps.y - ctx->calibration.bias_dps.y,
        .z = sample->reading.gyro_dps.z - ctx->calibration.bias_dps.z,
    };

    printk(
        "q_SR4=%.4f %.4f %.4f %.4f\n",
        (double)ctx->nav.relative_q.w,
        (double)ctx->nav.relative_q.x,
        (double)ctx->nav.relative_q.y,
        (double)ctx->nav.relative_q.z
    );

    printk(
        "ACC: %.4f %.4f %.4f m/s2 | norma=%.4f\n",
        (double)accel.x,
        (double)accel.y,
        (double)accel.z,
        (double)vector_norm(accel)
    );

    printk(
        "GYRO corregido: %.4f %.4f %.4f deg/s | norma=%.4f\n",
        (double)gyro.x,
        (double)gyro.y,
        (double)gyro.z,
        (double)vector_norm(gyro)
    );
}

    printk(
        "t=%lld | moving=%d | imu=%d | "
        "NEW=%u NO_NEW=%u ERR=%u SAT=%u GAP=%u\n",
        (long long)sample->now_ms,
        (int)ctx->motion.moving,
        sample->result,
        (unsigned int)ctx->new_count,
        (unsigned int)ctx->no_new_count,
        (unsigned int)ctx->error_count,
        (unsigned int)ctx->saturation_count,
        (unsigned int)ctx->gap_count
    );

        printk(
        "NAV=%d dt=%.1f min=%.1f max=%.1f ms NAV_ERR=%u\n",
        (int)ctx->nav.valid,
        (double)(ctx->last_dt_s * 1000.0f),
        (double)(ctx->min_dt_s * 1000.0f),
        (double)(ctx->max_dt_s * 1000.0f),
        (unsigned int)ctx->navigation_error_count
    );

    printk(
        "q=%.4f %.4f %.4f %.4f\n"
        "v=%.3f %.3f %.3f m/s | p=%.3f %.3f %.3f m\n",
        (double)ctx->nav.q.w,
        (double)ctx->nav.q.x,
        (double)ctx->nav.q.y,
        (double)ctx->nav.q.z,
        (double)ctx->nav.velocity_mps.x,
        (double)ctx->nav.velocity_mps.y,
        (double)ctx->nav.velocity_mps.z,
        (double)ctx->nav.position_m.x,
        (double)ctx->nav.position_m.y,
        (double)ctx->nav.position_m.z
    );

    ctx->next_motion_report_ms = sample->now_ms + 1000;
}

static float vector_norm(Vec3f value)
{
    return sqrtf(value.x * value.x +
                 value.y * value.y +
                 value.z * value.z);
}

static Vec3f accel_correct(Vec3f measured)
{
    return (Vec3f){
        .x = (measured.x - 0.12409f) * 0.9964143f,
        .y = (measured.y + 0.06164f) * 0.9966736f,
        .z = (measured.z + 0.05113f) * 0.9891509f,
    };
}

/* static void accel_calibration_run(void)
{
    const uint32_t target_samples = 500U;
    Vec3f sum = {0};
    uint32_t count = 0;

    printk("\nCalibracion estatica del acelerometro.\n");
    printk("Mantener la postura. Inicio en 3 segundos.\n");

    k_msleep(3000);

    printk("Midiendo 500 muestras. Mantener quieto unos 10 segundos.\n");

    while (count < target_samples) {
        k_timer_status_sync(&sample_timer);

        ImuSample sample = imu_sample_read();

        if (sample.result == IMU_NO_NEW) {
            continue;
        }

        if (sample.result != IMU_NEW ||
            sample.reading.range_limited) {
            printk(
                "Medicion interrumpida: imu=%d, saturacion=%d.\n",
                sample.result,
                (int)sample.reading.range_limited
            );
            return;
        }

        sum.x += sample.reading.accel_mps2.x;
        sum.y += sample.reading.accel_mps2.y;
        sum.z += sample.reading.accel_mps2.z;
        count++;
    }

    Vec3f average = {
        .x = sum.x / (float)count,
        .y = sum.y / (float)count,
        .z = sum.z / (float)count,
    };

    printk(
        "ACC_PROMEDIO: %.5f %.5f %.5f m/s2 | "
        "norma=%.5f | muestras=%u\n",
        (double)average.x,
        (double)average.y,
        (double)average.z,
        (double)vector_norm(average),
        (unsigned int)count
    );

    printk("Medicion terminada.\n");
} */
