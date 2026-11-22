/*
 * Servicio de adquisicion (D4): un hilo de prioridad 3 despertado por un
 * k_timer de 20 ms. Cada vuelta atiende luz (2 s), IMU (50 Hz) y ambiente (2 s)
 * fuera del mutex y publica despues, en una sola seccion breve, lo que cambio.
 */
#include "acquisition_api.h"

#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "../algorithms/statistics.h"
#include "acquisition_environment.h"
#include "acquisition_imu.h"
#include "acquisition_light.h"

#define ACQUISITION_PERIOD_MS  20
#define ACQUISITION_STACK_SIZE 2048
#define ACQUISITION_PRIORITY   3
#define ACQUISITION_WDT_MS     1000 /* peor vuelta: reinicio de la IMU, ~150 ms */

K_MUTEX_DEFINE(acquisition_lock);
K_TIMER_DEFINE(acquisition_timer, NULL, NULL);

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
    statistics_begin(&stats, k_uptime_ticks(), CONFIG_SYS_CLOCK_TICKS_PER_SEC);
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

    if (light_new) {
        published.light = light.data;
        if (light.data.quality.valid) {
            statistics_add(&stats, STATS_LIGHT, light.data.light_pct, light.sample_ticks);
        }
    }
    if (imu_new) {
        published.imu = imu.data;
        published.nav = imu.nav;
        published.moving = imu.moving;
        if (imu.data.quality.valid && !imu.data.calibration_pending) {
            statistics_add(&stats, STATS_ACCEL_NORM, imu.accel_norm_mps2, imu.sample_ticks);
            statistics_add(&stats, STATS_GYRO_NORM, imu.gyro_norm_dps, imu.sample_ticks);
        }
    }
    if (environment_new) {
        published.environment = environment.data;
        if (environment.data.quality.valid) {
            statistics_add(&stats, STATS_TEMP, environment.data.temperature_c,
                           environment.sample_ticks);
            statistics_add(&stats, STATS_RH, environment.data.humidity_pct,
                           environment.sample_ticks);
        }
    }
    statistics_advance(&stats, k_uptime_ticks()); /* cerrar la ventana aunque no haya muestras */

    k_mutex_unlock(&acquisition_lock);
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

    const int wdt = task_wdt_add(ACQUISITION_WDT_MS, NULL, NULL);
    k_timer_start(&acquisition_timer, K_MSEC(ACQUISITION_PERIOD_MS), K_MSEC(ACQUISITION_PERIOD_MS));

    while (true) {
        (void)task_wdt_feed(wdt);
        /* Si se perdieron vencimientos se hace una sola lectura actual. */
        (void)k_timer_status_sync(&acquisition_timer);

        const bool light_new = acquisition_light_update(&light);
        const bool imu_new = acquisition_imu_update(&imu);
        const bool environment_new = acquisition_environment_update(&environment, imu.bus_error);

        publish(light_new, imu_new, environment_new);
    }
}

K_THREAD_DEFINE(acquisition_tid, ACQUISITION_STACK_SIZE, acquisition_entry, NULL, NULL, NULL,
                ACQUISITION_PRIORITY, 0, 0);
