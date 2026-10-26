#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#include "imu.h"
#include "environment.h"
#include "algorithm_config.h"
#include "gyro_calibration.h"
#include "motion.h"

/*Definición del temporizador*/
K_TIMER_DEFINE(sample_timer, NULL, NULL);

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
} SensorTest;

/* Declaraciones para poder situar main antes de las implementaciones. */
static int test_init(SensorTest *ctx);
static ImuSample imu_sample_read(void);
static void calibration_step(SensorTest *ctx, const ImuSample *sample);
static void motion_step(SensorTest *ctx, const ImuSample *sample);
static void environment_step(SensorTest *ctx, const ImuSample *sample);
static void status_report(SensorTest *ctx, const ImuSample *sample);
static float vector_norm(Vec3f value);

int main(void)
{
    SensorTest test;

    if (test_init(&test) < 0) {
        return 0;
    }

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

        motion_step(&test, &sample);
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
    };

    /*Arranca contexto de calibración y movimiento*/
    gyro_calibration_init(&ctx->calibration);
    motion_init(&ctx->motion);

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

    return sample;
}

/* Avanzar la calibracion inicial y mostrar su progreso. */
static void calibration_step(SensorTest *ctx, const ImuSample *sample)
{
    /*Estas condiciones, IMU_NEW y range_limited indican que está en reposo */
    if (sample->result == IMU_NEW && !sample->reading.range_limited) {
        /*Saca la magnitud del vector aceleración y del de giro*/
        float accel_norm = vector_norm(sample->reading.accel_mps2);
        float gyro_norm = vector_norm(sample->reading.gyro_dps);

        /**
         * Entrega a la estructura ImuSample la lectura
         *      Se puebla una nueva calibración
         *      Cada update aumenta un count. Cuando el count llega a
         *      TRACKER_GYRO_CALIBRATION_SAMPLES, entonces .ready = true
         */
        gyro_calibration_update(
            &ctx->calibration,
            &sample->reading.gyro_dps,
            accel_norm,
            gyro_norm
        );
    /*Aquí descarta la medición si se detectó movimiento */
    } else if (sample->result < 0 ||
               (sample->result == IMU_NEW &&
                sample->reading.range_limited)) {
        gyro_calibration_update(&ctx->calibration, NULL, 0.0f, 0.0f);
    }

    /*El objeto de calibración ImuSample tiene ready = true desde gyro_calibration_update() */
    if (ctx->calibration.ready) {
        printk(
            "Sesgo gyro: %.3f %.3f %.3f deg/s\n",
            (double)ctx->calibration.bias_dps.x,
            (double)ctx->calibration.bias_dps.y,
            (double)ctx->calibration.bias_dps.z
        );
    } else if (sample->now_ms >= ctx->next_calibration_report_ms) {
        printk(
            "Calibrando: %u/%u (imu=%d)\n",
            (unsigned int)ctx->calibration.count,
            TRACKER_GYRO_CALIBRATION_SAMPLES,
            sample->result
        );

        ctx->next_calibration_report_ms = sample->now_ms + 1000;
    }
}

/**
 * Función de procesamiento de movimiento
 *  Comprueba continuidad y actualiza la deteccion de movimiento.
 *      *ctx    -> puntero a muestra del sensor de prueba
 *      *sample -> puntero a muestra de movimiento
 *
 *  1. si el resultado es que no es una muestra nueva, suma que contador y sale
 *  2. si es muestra nueva y el rango es limitado,
 *
 */
static void motion_step(SensorTest *ctx, const ImuSample *sample)
{
    if (sample->result == IMU_NO_NEW) {
        ctx->no_new_count++;
        return;
    }

    if (sample->result != IMU_NEW || sample->reading.range_limited) {
        motion_interrupt(&ctx->motion);
        ctx->previous_imu_ticks = -1;

        if (sample->result < 0) {
            ctx->error_count++;
        } else {
            ctx->saturation_count++;
        }

        return;
    }

    ctx->new_count++;

    if (ctx->previous_imu_ticks >= 0 &&
        sample->ticks - ctx->previous_imu_ticks >
            (int64_t)k_ms_to_ticks_floor64(TRACKER_IMU_MAX_GAP_MS)) {
        motion_interrupt(&ctx->motion);
        ctx->gap_count++;
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

    ctx->next_motion_report_ms = sample->now_ms + 1000;
}

static float vector_norm(Vec3f value)
{
    return sqrtf(value.x * value.x +
                 value.y * value.y +
                 value.z * value.z);
}