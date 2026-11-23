/*
 * Servicio de adquisicion (D4): un hilo de prioridad 3 que despierta el propio
 * MPU6050 con su pin INT en cada muestra nueva (50 Hz). Cada vuelta atiende luz
 * (2 s), IMU y ambiente (2 s) fuera del mutex y publica despues, en una sola
 * seccion breve, lo que cambio.
 */
#include "acquisition_api.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "../algorithms/statistics.h"
#include "acquisition_environment.h"
#include "acquisition_imu.h"
#include "acquisition_light.h"

#define ACQUISITION_MAX_WAIT_MS 30 /* sin pulso INT (cable suelto) se sondea cada 30 ms */
#define ACQUISITION_STACK_SIZE 2048
#define ACQUISITION_PRIORITY   3
#define ACQUISITION_WDT_MS     1000 /* peor vuelta: reinicio de la IMU, ~150 ms */

K_MUTEX_DEFINE(acquisition_lock);
static K_SEM_DEFINE(imu_sample_ready, 0, 1);

/* INT del MPU6050: pulso de 50 us por muestra (DATA_RDY), configurado en imu_init. */
static const struct gpio_dt_spec imu_int = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), imu_int_gpios);
static struct gpio_callback imu_int_callback;

/* Contextos privados: solo los toca el hilo de adquisicion. */
static AcquisitionLight light;
static AcquisitionImu imu;
static AcquisitionEnvironment environment;

/* Compartido con main: protegido por acquisition_lock. */
static StatsCtx stats;
static AcquisitionSnapshot published = {
    .imu = { .quality.last_good_ms = -1, .calibration_pending = true },
    .light = { .quality.last_good_ms = -1 },
    .environment = { .quality.last_good_ms = -1 },
    .nav = { .q.w = 1.0f },
};

void acquisition_snapshot_get(AcquisitionSnapshot *out)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);
    *out = published;
    out->stats = stats.result;
    k_mutex_unlock(&acquisition_lock);
}

void acquisition_stats_begin(void)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);
    statistics_begin(&stats, k_uptime_get());
    k_mutex_unlock(&acquisition_lock);
}

void acquisition_stats_end(void)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);
    statistics_end(&stats);
    k_mutex_unlock(&acquisition_lock);
}

/* Copia lo nuevo de esta vuelta y acumula las muestras validas en la sesion NORMAL. */
static void publish(bool light_new, bool imu_new, bool environment_new)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);

    /* Primero se cierra la ventana vencida; las muestras de esta vuelta van a la nueva. */
    statistics_advance(&stats, k_uptime_get());

    if (light_new) {
        published.light = light.data;
        if (light.data.quality.valid) {
            statistics_add(&stats, STATS_LIGHT, light.data.light_pct);
        }
    }
    if (imu_new) {
        published.imu = imu.data;
        published.nav = imu.nav;
        published.moving = imu.moving;
        if (imu.data.quality.valid && !imu.data.calibration_pending) {
            statistics_add(&stats, STATS_ACCEL_NORM, imu.accel_norm_mps2);
            statistics_add(&stats, STATS_GYRO_NORM, imu.gyro_norm_dps);
        }
    }
    if (environment_new) {
        published.environment = environment.data;
        if (environment.data.quality.valid) {
            statistics_add(&stats, STATS_TEMP, environment.data.temperature_c);
            statistics_add(&stats, STATS_RH, environment.data.humidity_pct);
        }
    }
    k_mutex_unlock(&acquisition_lock);
}

static void on_imu_int(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    k_sem_give(&imu_sample_ready);
}

/* Si el GPIO falla, la adquisicion sigue funcionando por sondeo (ACQUISITION_MAX_WAIT_MS). */
static void imu_int_init(void)
{
    if (!gpio_is_ready_dt(&imu_int) || gpio_pin_configure_dt(&imu_int, GPIO_INPUT) != 0) {
        return;
    }
    gpio_init_callback(&imu_int_callback, on_imu_int, BIT(imu_int.pin));
    if (gpio_add_callback_dt(&imu_int, &imu_int_callback) == 0) {
        (void)gpio_pin_interrupt_configure_dt(&imu_int, GPIO_INT_EDGE_TO_ACTIVE);
    }
}

static void acquisition_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_sleep(K_MSEC(100)); /* encendido de los sensores */
    acquisition_light_init(&light);
    acquisition_imu_init(&imu);
    acquisition_environment_init(&environment);

    imu_int_init();
    const int wdt = task_wdt_add(ACQUISITION_WDT_MS, NULL, NULL);

    while (true) {
        (void)task_wdt_feed(wdt);
        /* Espera a la siguiente muestra del MPU; sin pulso, a los 30 ms como mucho. */
        (void)k_sem_take(&imu_sample_ready, K_MSEC(ACQUISITION_MAX_WAIT_MS));

        const bool light_new = acquisition_light_update(&light);
        const bool imu_new = acquisition_imu_update(&imu);
        const bool environment_new = acquisition_environment_update(&environment, imu.bus_error);

        publish(light_new, imu_new, environment_new);
    }
}

K_THREAD_DEFINE(acquisition_tid, ACQUISITION_STACK_SIZE, acquisition_entry, NULL, NULL, NULL,
                ACQUISITION_PRIORITY, 0, 0);
