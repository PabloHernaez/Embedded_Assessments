#include "acquisition_api.h"
#include <math.h>
#include <zephyr/kernel.h>

#include "../algorithms/statistics.h"
#include "acquisition_light.h"
#include "acquisition_imu.h"
#include "acquisition_environment.h"

K_MUTEX_DEFINE(acquisition_lock);

#define ACQUISITION_PERIOD_MS  20
#define ACQUISITION_NOMINAL_BUDGET_MS 10
#define ACQUISITION_STACK_SIZE 2048
#define ACQUISITION_PRIORITY  3

static AcquisitionLight light_context;
static AcquisitionImu imu_context;
static AcquisitionEnvironment environment_context;

K_TIMER_DEFINE(acquisition_timer, NULL, NULL);

/* Un unico escritor (adquisicion); volatile conserva los diagnosticos leidos por SWD. */
static volatile struct {
    uint64_t expired_periods;
    uint64_t missed_periods;
    uint32_t clock_hz;
    uint32_t counter_pair_cycles;
    uint32_t startup_cycles;
    uint32_t normal_count;
    uint32_t normal_max_cycles;
    uint32_t normal_max_at_ms;
    uint32_t normal_ge_10_ms;
    uint32_t normal_ge_20_ms;
    uint32_t maintenance_count;
    uint32_t maintenance_max_cycles;
    uint32_t maintenance_max_at_ms;
    uint32_t maintenance_ge_20_ms;
} timing = {
    .clock_hz = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC,
};

static StatsCtx stats;

/* Datos publicados. El resultado estadistico vive en stats.result. */
static struct {
    ImuData imu;
    LightData light;
    EnvironmentData environment;

    NavigationView nav;
    bool moving;
    uint32_t progress;
} published = {
    .imu = {
        .quality = {
            .valid = false,
            .last_good_ms = -1,
        },
        .calibration_pending = true,
    },

    .light = {
        .quality = {
            .valid = false,
            .last_good_ms = -1,
        },
    },

    .environment = {
        .quality = {
            .valid = false,
            .last_good_ms = -1,
        },
    },

    .nav = {
        .q = {
            .w = 1.0f,
        },
        .relative_q = {
            .w = 1.0f,
        },
        .valid = false,
    },
};

/**
 * Obtiene una copia coherente del último estado publicado y sus estadísticas.
 *
 * Entradas: out debe apuntar a memoria válida y escribible. Lee published
 *           y stats.result; requiere contexto de hilo para tomar el mutex.
 * Retorno: ninguno (void).
 * Modifica: sobrescribe *out bajo acquisition_lock. Conserva los datos
 *           publicados y el estado de la sesión estadística.
 */
void acquisition_snapshot_get(AcquisitionSnapshot *out)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);

    *out = (AcquisitionSnapshot){
        .imu = published.imu,
        .light = published.light,
        .environment = published.environment,
        .nav = published.nav,
        .moving = published.moving,
        .stats = stats.result,
        .progress = published.progress,
    };

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Inicia o reinicia una sesión estadística con una primera ventana de 30 s.
 *
 * Entradas: ninguna explícita; usa el tick actual y la frecuencia del kernel.
 * Retorno: ninguno (void).
 * Modifica: reinicializa stats bajo acquisition_lock; activa la sesión,
 *           fija sus tiempos y descarta acumuladores y resultados anteriores.
 */
void acquisition_stats_begin(void)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);

    statistics_begin(&stats, k_uptime_ticks(), CONFIG_SYS_CLOCK_TICKS_PER_SEC);

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Finaliza la sesión estadística y descarta sus datos acumulados.
 *
 * Entradas: ninguna explícita; utiliza el contexto global stats.
 * Retorno: ninguno (void).
 * Modifica: pone stats a cero bajo acquisition_lock, incluida la ventana
 *           parcial y el último resultado. Las lecturas publicadas se conservan.
 */
void acquisition_stats_end(void)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);

    statistics_end(&stats);

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Espera un vencimiento del timer y registra cuántos periodos transcurrieron.
 *
 * Entradas: ninguna explícita; acquisition_timer debe estar arrancado.
 * Retorno: ninguno (void). Puede bloquear el hilo mientras espera el timer.
 * Modifica: consume el estado de vencimientos del timer y actualiza
 *           timing.expired_periods y timing.missed_periods. Si hay varios
 *           vencimientos, cuenta como omitidos todos salvo el de la vuelta actual.
 */
static void acquisition_wait_for_period(void)
{
    uint32_t expirations = k_timer_status_sync(&acquisition_timer);

    timing.expired_periods += expirations;

    if (expirations > 1U) {
        timing.missed_periods += expirations - 1U;
    }
}

/**
 * Conserva el maximo observado del trabajo de una vuelta, sin imprimir ni publicar.
 * Entradas: begin_cycles es k_cycle_get_32 tras esperar el timer; maintenance
 *           indica configuracion/revision o fallo antes/durante esta vuelta.
 *           El intervalo debe ser menor que una vuelta completa del contador
 *           de 32 bits (unos 89 s a 48 MHz); la resta admite cruzar su desborde.
 * Retorno: ninguno (void).
 * Modifica: cuenta/maximo/fecha y excesos de timing para nominal o mantenimiento.
 *           Nominal cuenta duraciones >=10 ms y >=20 ms; mantenimiento >=20 ms.
 *           Incluye esperas I2C/mutex e interrupciones ocurridas entre las marcas;
 *           excluye la espera periodica y el registro de estos diagnosticos.
 */
static void acquisition_duration_record(uint32_t begin_cycles, bool maintenance)
{
    uint32_t elapsed = k_cycle_get_32() - begin_cycles;
    const uint32_t cycles_per_ms = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000U;
    uint32_t period_cycles = cycles_per_ms * ACQUISITION_PERIOD_MS;

    if (maintenance) {
        timing.maintenance_count++;
        if (elapsed > timing.maintenance_max_cycles) {
            timing.maintenance_max_cycles = elapsed;
            timing.maintenance_max_at_ms =
                (uint32_t)k_ticks_to_ms_floor64(k_uptime_ticks());
        }
        if (elapsed >= period_cycles) {
            timing.maintenance_ge_20_ms++;
        }
        return;
    }

    timing.normal_count++;
    if (elapsed > timing.normal_max_cycles) {
        timing.normal_max_cycles = elapsed;
        timing.normal_max_at_ms =
            (uint32_t)k_ticks_to_ms_floor64(k_uptime_ticks());
    }
    if (elapsed >= cycles_per_ms * ACQUISITION_NOMINAL_BUDGET_MS) {
        timing.normal_ge_10_ms++;
    }
    if (elapsed >= period_cycles) {
        timing.normal_ge_20_ms++;
    }
}

/**
 * Atiende el cierre o reinicio de ventanas estadísticas vencidas.
 *
 * Entradas: ninguna explícita; lee el tick actual y el contexto global stats.
 * Retorno: ninguno (void).
 * Modifica: statistics_advance puede actualizar tiempos, acumuladores y
 *           resultado de stats bajo acquisition_lock. Sin sesión activa no avanza.
 */
static void acquisition_statistics_step(void)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);

    statistics_advance(&stats, k_uptime_ticks());

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Registra que el hilo completó otra vuelta, aunque hayan fallado sensores.
 *
 * Entradas: ninguna explícita; utiliza el contador published.progress.
 * Retorno: ninguno (void).
 * Modifica: incrementa published.progress bajo acquisition_lock.
 */
static void acquisition_progress_step(void)
{
    k_mutex_lock(&acquisition_lock, K_FOREVER);

    published.progress++;

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Atiende la luz y publica el resultado cuando vence su cadencia de lectura.
 *
 * Entradas: ninguna explícita; utiliza light_context, el driver y el reloj.
 * Retorno: ninguno (void).
 * Modifica: acquisition_light_update actualiza light_context fuera del mutex.
 *           Si hay un intento nuevo, copia sus datos/calidad a published.light
 *           bajo acquisition_lock. Una lectura válida se ofrece a stats;
 *           statistics_add decide si pertenece a la sesión y ventana activas.
 */
static void acquisition_light_step(void)
{
    if (!acquisition_light_update(&light_context)) {
        return;
    }

    k_mutex_lock(&acquisition_lock, K_FOREVER);

    published.light = light_context.data;

    if (light_context.data.quality.valid) {
        statistics_add(&stats,
                       STATS_LIGHT,
                       light_context.data.light_pct,
                       light_context.capture_begin_ticks,
                       light_context.sample_ticks);
    }

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Ofrece las dos magnitudes de la muestra IMU nueva a la sesión estadística.
 *
 * Entradas: ninguna explícita; usa imu_context recién actualizado y stats.
 *           El llamador debe haber recibido true de acquisition_imu_update
 *           en esta vuelta y mantener acquisition_lock tomado.
 * Retorno: ninguno (void).
 * Modifica: statistics_add puede actualizar tiempos, acumuladores y resultado
 *           de stats. Excluye calidad inválida, calibración pendiente y magnitudes
 *           no finitas. Las dos métricas reciben las mismas fechas de captura
 *           y obtención; el motor existente decide sesión y ventana. Conserva
 *           imu_context; no calcula raíces, accede a hardware ni toma otro mutex.
 */
static void acquisition_imu_statistics_add(void)
{
    if (!imu_context.data.quality.valid ||
        imu_context.data.calibration_pending ||
        !isfinite(imu_context.accel_norm_mps2) ||
        !isfinite(imu_context.gyro_norm_dps)) {
        return;
    }

    statistics_add(&stats, STATS_ACCEL_NORM, imu_context.accel_norm_mps2,
                   imu_context.capture_begin_ticks, imu_context.sample_ticks);
    statistics_add(&stats, STATS_GYRO_NORM, imu_context.gyro_norm_dps,
                   imu_context.capture_begin_ticks, imu_context.sample_ticks);
}

/**
 * Atiende la IMU y publica juntos sus medidas, navegación y movimiento.
 *
 * Entradas: ninguna explícita; utiliza imu_context, el driver y el reloj.
 * Retorno: ninguno (void).
 * Modifica: acquisition_imu_update procesa imu_context fuera del mutex.
 *           Si devuelve true, copia data, nav y moving a published bajo
 *           acquisition_lock e incorpora las magnitudes nuevas admitidas a stats.
 *           Si devuelve false, conserva el estado publicado y no acumula,
 *           aunque el contexto privado puede haber fechado un intento NO_NEW.
 */
static void acquisition_imu_step(void)
{
    if (!acquisition_imu_update(&imu_context)) {
        return;
    }

    k_mutex_lock(&acquisition_lock, K_FOREVER);

    published.imu = imu_context.data;
    published.nav = imu_context.nav;
    published.moving = imu_context.moving;

    acquisition_imu_statistics_add();

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Atiende el ambiente y publica juntos temperatura, humedad y su calidad.
 *
 * Entradas: ninguna explícita; utiliza environment_context y la bandera de
 *           interrupción del bus IMU para posponer I2C tras error/recuperación física.
 * Retorno: ninguno (void).
 * Modifica: acquisition_environment_update atiende el contexto fuera del mutex.
 *           Si hay un par o calidad nuevos, copia data a published.environment
 *           bajo acquisition_lock. Un par válido se ofrece una sola vez a stats
 *           para STATS_TEMP y STATS_RH, con los mismos tiempos de captura/recogida.
 */
static void acquisition_environment_step(void)
{
    if (!acquisition_environment_update(&environment_context,
                                        imu_context.bus_interrupted)) {
        return;
    }

    k_mutex_lock(&acquisition_lock, K_FOREVER);

    published.environment = environment_context.data;

    if (environment_context.data.quality.valid) {
        statistics_add(&stats, STATS_TEMP,
                       environment_context.data.temperature_c,
                       environment_context.capture_begin_ticks,
                       environment_context.sample_ticks);
        statistics_add(&stats, STATS_RH,
                       environment_context.data.humidity_pct,
                       environment_context.capture_begin_ticks,
                       environment_context.sample_ticks);
    }

    k_mutex_unlock(&acquisition_lock);
}

/**
 * Atiende la comprobación lenta de configuración al terminar el trabajo I2C.
 * Entradas: ninguna explícita; usa los contextos IMU/ambiente y sus banderas
 *           de esta vuelta para respetar el primer fallo del bus compartido.
 * Retorno: ninguno (void).
 * Modifica: el paso privado puede invalidar IMU y programar recuperación; publica
 *           sus datos/nav/moving bajo mutex, sin acumular otra muestra. Ante un
 *           fallo tardío cancela también el pendiente ambiental, sin hacer I2C.
 */
static void acquisition_imu_configuration_step(void)
{
    bool bus_allowed = !imu_context.bus_interrupted &&
                       !environment_context.bus_interrupted;
    if (!acquisition_imu_check_configuration(&imu_context, bus_allowed)) {
        return;
    }

    k_mutex_lock(&acquisition_lock, K_FOREVER);
    published.imu = imu_context.data;
    published.nav = imu_context.nav;
    published.moving = imu_context.moving;
    k_mutex_unlock(&acquisition_lock);

    if (imu_context.bus_interrupted) {
        acquisition_environment_step();
    }
}

/**
 * Ejecuta el hilo: estabiliza el arranque, prepara los contextos y atiende
 * periódicamente estadísticas, luz, IMU, ambiente y progreso en un while (true).
 *
 * Entradas: arg1, arg2 y arg3 son los argumentos exigidos por Zephyr;
 *           sus valores se ignoran. Usa los contextos y el timer globales.
 * Retorno: no retorna durante el funcionamiento normal (firma void).
 * Modifica: inicializa los contextos de luz, IMU y ambiente, configura hardware mediante
 *           sus módulos y arranca el timer de 20 ms. Sus pasos actualizan
 *           timing, published y stats; la publicación utiliza acquisition_lock.
 */
static void acquisition_entry(void *arg1, void *arg2, void *arg3)
{
    uint32_t startup_begin = k_cycle_get_32();
    timing.counter_pair_cycles = k_cycle_get_32() - startup_begin;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    /* Estabilizacion inicial del encendido de los sensores. */
    k_sleep(K_MSEC(100));

    acquisition_light_init(&light_context);
    acquisition_imu_init(&imu_context);
    acquisition_environment_init(&environment_context);

    timing.startup_cycles = k_cycle_get_32() - startup_begin;

    k_timer_start(&acquisition_timer, K_MSEC(ACQUISITION_PERIOD_MS), K_MSEC(ACQUISITION_PERIOD_MS));

    while (true) {
        acquisition_wait_for_period();
        uint32_t begin_cycles = k_cycle_get_32();
        bool maintenance = !light_context.configured || !imu_context.configured ||
                           imu_context.review_pending || !environment_context.configured;
        acquisition_statistics_step();
        acquisition_light_step();
        acquisition_imu_step();
        acquisition_environment_step();
        acquisition_imu_configuration_step();
        acquisition_progress_step();

        maintenance = maintenance || imu_context.bus_interrupted ||
                      environment_context.bus_interrupted || imu_context.review_pending ||
                      !light_context.configured;
        acquisition_duration_record(begin_cycles, maintenance);
    }
}

K_THREAD_DEFINE(acquisition_tid, ACQUISITION_STACK_SIZE, acquisition_entry, NULL, NULL, NULL, ACQUISITION_PRIORITY, 0, 0);
