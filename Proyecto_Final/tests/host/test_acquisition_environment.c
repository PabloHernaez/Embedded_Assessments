#include <errno.h>
#include <math.h>
#include <stdio.h>

#include "acquisition_environment.h"
#include "environment.h"
#include "statistics.h"
#include <zephyr/kernel.h>

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int init_result;
static int start_result;
static int read_result;
static unsigned int init_count;
static unsigned int start_count;
static unsigned int read_count;
static int64_t now_ticks;
static int64_t init_delay_ticks;
static int64_t start_delay_ticks;
static int64_t read_delay_ticks;
static EnvironmentReading next_reading;

int environment_init(void)
{
    ++init_count;
    now_ticks += init_delay_ticks;
    return init_result;
}

int environment_start(void)
{
    ++start_count;
    now_ticks += start_delay_ticks;
    return start_result;
}

int environment_read(EnvironmentReading *out)
{
    ++read_count;
    now_ticks += read_delay_ticks;
    if (read_result == 0) {
        *out = next_reading;
    }
    return read_result;
}

int64_t k_uptime_ticks(void)
{
    return now_ticks;
}

int64_t k_ticks_to_ms_floor64(int64_t ticks)
{
    return ticks / 10;
}

int64_t k_ms_to_ticks_ceil64(int64_t ms)
{
    return ms * 10; /* A 10000 ticks/s, los ms enteros no requieren redondeo. */
}

static bool same_pair(EnvironmentData a, EnvironmentData b)
{
    return fabsf(a.temperature_c - b.temperature_c) < 1e-5f &&
           fabsf(a.humidity_pct - b.humidity_pct) < 1e-5f &&
           a.quality.last_good_ms == b.quality.last_good_ms;
}

int main(void)
{
    unsigned int checks = 0;
    AcquisitionEnvironment ctx;
    acquisition_environment_init(&ctx);
    CHECK(!ctx.configured && !ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == -1);
    CHECK(ctx.ready_ticks == -1 && ctx.next_read_ticks == 0);
    CHECK(ctx.capture_begin_ticks == -1 && ctx.sample_ticks == -1);
    CHECK(init_count == 0 && start_count == 0 && read_count == 0);

    /* Init fallido: publicar calidad y espaciar reintentos, sin lanzar F5. */
    now_ticks = 1000;
    init_delay_ticks = 70;
    init_result = -ENODEV;
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(!ctx.configured && !ctx.data.quality.valid);
    CHECK(init_count == 1 && start_count == 0 && read_count == 0);
    CHECK(ctx.next_read_ticks == now_ticks + 20000);
    CHECK(ctx.sample_ticks == now_ticks);
    now_ticks = ctx.next_read_ticks - 1;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(init_count == 1);

    /* Contar la espera desde DESPUES de F5, incluyendo un tick de margen. */
    now_ticks = ctx.next_read_ticks;
    init_result = 0;
    start_delay_ticks = 17;
    int64_t before_init_ticks = now_ticks;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(ctx.configured && !ctx.data.quality.valid);
    CHECK(init_count == 2 && start_count == 1 && read_count == 0);
    CHECK(now_ticks == before_init_ticks + init_delay_ticks + start_delay_ticks);
    CHECK(ctx.capture_begin_ticks == before_init_ticks + init_delay_ticks);
    CHECK(ctx.ready_ticks == now_ticks + 401);
    CHECK(ctx.next_read_ticks == now_ticks + 20000);
    int64_t capture_begin_ticks = ctx.capture_begin_ticks;
    int64_t next_begin_ticks = ctx.next_read_ticks;

    now_ticks = ctx.ready_ticks - 1;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(read_count == 0);
    now_ticks = ctx.ready_ticks;
    read_delay_ticks = 23;
    next_reading = (EnvironmentReading){23.5f, 55.0f};
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(read_count == 1 && ctx.ready_ticks == -1);
    CHECK(ctx.data.quality.valid);
    CHECK(ctx.data.temperature_c == 23.5f && ctx.data.humidity_pct == 55.0f);
    CHECK(ctx.data.quality.last_good_ms == now_ticks / 10);
    CHECK(ctx.sample_ticks == now_ticks);
    CHECK(ctx.capture_begin_ticks == capture_begin_ticks);
    CHECK(ctx.next_read_ticks == next_begin_ticks);
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(read_count == 1 && start_count == 1); /* No repetir el par retenido. */

    /* Las mismas fechas incorporan ambas metricas a una sesion compatible. */
    StatsCtx stats;
    statistics_begin(&stats, capture_begin_ticks, CONFIG_SYS_CLOCK_TICKS_PER_SEC);
    statistics_add(&stats, STATS_TEMP, ctx.data.temperature_c,
                   ctx.capture_begin_ticks, ctx.sample_ticks);
    statistics_add(&stats, STATS_RH, ctx.data.humidity_pct,
                   ctx.capture_begin_ticks, ctx.sample_ticks);
    CHECK(stats.metric[STATS_TEMP].count == 1);
    CHECK(stats.metric[STATS_RH].count == 1);
    CHECK(stats.metric[STATS_TEMP].sum == 23.5f);
    CHECK(stats.metric[STATS_RH].sum == 55.0f);

    /* Una sesion que empieza durante la conversion no debe heredar esa medida. */
    EnvironmentData retained = ctx.data;
    now_ticks = ctx.next_read_ticks;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(ctx.data.quality.valid && same_pair(ctx.data, retained));
    CHECK(init_count == 2 && start_count == 2);
    capture_begin_ticks = ctx.capture_begin_ticks;
    now_ticks += 100;
    statistics_begin(&stats, now_ticks, CONFIG_SYS_CLOCK_TICKS_PER_SEC);
    now_ticks = ctx.ready_ticks;
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(ctx.capture_begin_ticks == capture_begin_ticks);
    statistics_add(&stats, STATS_TEMP, ctx.data.temperature_c,
                   ctx.capture_begin_ticks, ctx.sample_ticks);
    statistics_add(&stats, STATS_RH, ctx.data.humidity_pct,
                   ctx.capture_begin_ticks, ctx.sample_ticks);
    CHECK(stats.metric[STATS_TEMP].count == 0);
    CHECK(stats.metric[STATS_RH].count == 0);

    /* Fallo de recogida: retener el par completo y preparar reconfiguracion. */
    retained = ctx.data;
    now_ticks = ctx.next_read_ticks;
    CHECK(!acquisition_environment_update(&ctx, false));
    read_result = -EIO;
    now_ticks = ctx.ready_ticks;
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(!ctx.configured && !ctx.data.quality.valid && ctx.ready_ticks == -1);
    CHECK(same_pair(ctx.data, retained));
    CHECK(ctx.sample_ticks == now_ticks);
    now_ticks = ctx.next_read_ticks - 1;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(init_count == 2);

    /* Fallo de F5: reconfigurar, invalidar y volver a esperar la cadencia. */
    now_ticks = ctx.next_read_ticks;
    start_result = -EIO;
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(init_count == 3 && !ctx.configured && ctx.ready_ticks == -1);
    CHECK(!ctx.data.quality.valid && same_pair(ctx.data, retained));
    CHECK(ctx.next_read_ticks == now_ticks + 20000);
    now_ticks = ctx.next_read_ticks;
    start_result = 0;
    read_result = 0;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(init_count == 4 && ctx.configured);
    now_ticks = ctx.ready_ticks;
    next_reading = (EnvironmentReading){21.25f, 52.5f};
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(ctx.data.quality.valid && ctx.data.temperature_c == 21.25f);
    CHECK(ctx.data.humidity_pct == 52.5f);

    /* Error IMU: abandonar pendiente sin mas transacciones en el bus esa vuelta. */
    retained = ctx.data;
    now_ticks = ctx.next_read_ticks;
    CHECK(!acquisition_environment_update(&ctx, false));
    unsigned int starts_before_error = start_count;
    unsigned int reads_before_error = read_count;
    now_ticks += 100;
    CHECK(acquisition_environment_update(&ctx, true));
    CHECK(ctx.ready_ticks == -1 && !ctx.data.quality.valid);
    CHECK(same_pair(ctx.data, retained));
    CHECK(ctx.next_read_ticks == now_ticks + 20000);
    CHECK(start_count == starts_before_error && read_count == reads_before_error);
    next_begin_ticks = ctx.next_read_ticks;
    now_ticks += 200;
    CHECK(!acquisition_environment_update(&ctx, true));
    CHECK(ctx.bus_interrupted && ctx.next_read_ticks == next_begin_ticks);
    now_ticks = ctx.next_read_ticks - 1;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(start_count == starts_before_error && read_count == reads_before_error);
    now_ticks = ctx.next_read_ticks;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(start_count == starts_before_error + 1 && init_count == 5);
    now_ticks = ctx.ready_ticks;
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(read_count == reads_before_error + 1 && ctx.data.quality.valid);

    /* Una IMU ausente revisada cada 1 s no debe aplazar indefinidamente ENV. */
    for (int cycle = 0; cycle < 3; ++cycle) {
        retained = ctx.data;
        next_begin_ticks = ctx.next_read_ticks;
        unsigned int starts_before_review = start_count;
        unsigned int reads_before_review = read_count;

        now_ticks = next_begin_ticks - 10000;
        CHECK(!acquisition_environment_update(&ctx, true));
        CHECK(ctx.bus_interrupted && ctx.data.quality.valid);
        CHECK(ctx.next_read_ticks == next_begin_ticks && same_pair(ctx.data, retained));
        now_ticks = next_begin_ticks;
        CHECK(!acquisition_environment_update(&ctx, true));
        CHECK(ctx.next_read_ticks == next_begin_ticks);
        CHECK(start_count == starts_before_review && read_count == reads_before_review);

        /* La siguiente vuelta permitida atiende el vencimiento ya alcanzado. */
        now_ticks += 200;
        CHECK(!acquisition_environment_update(&ctx, false));
        CHECK(!ctx.bus_interrupted && start_count == starts_before_review + 1);
        CHECK(ctx.ready_ticks >= 0);
        now_ticks = ctx.ready_ticks;
        CHECK(acquisition_environment_update(&ctx, false));
        CHECK(ctx.data.quality.valid && read_count == reads_before_review + 1);
        CHECK(ctx.data.quality.last_good_ms > retained.quality.last_good_ms);
    }

    /* Tras interrumpir el bus ya no se presupone configuracion conservada.
     * Si su revision falla, no se debe enviar F5 ni renovar la fecha buena.
     * El reintento siguiente mantiene la cadencia, sin exigir otra lectura IMU.
     */
    retained = ctx.data;
    next_begin_ticks = ctx.next_read_ticks;
    unsigned int starts_before_revalidation = start_count;
    now_ticks = next_begin_ticks - 10000;
    CHECK(!acquisition_environment_update(&ctx, true));
    CHECK(ctx.next_read_ticks == next_begin_ticks && ctx.data.quality.valid);
    init_result = -EIO;
    now_ticks = next_begin_ticks;
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(start_count == starts_before_revalidation && ctx.bus_interrupted);
    CHECK(!ctx.data.quality.valid && same_pair(ctx.data, retained));
    CHECK(ctx.data.quality.last_good_ms == retained.quality.last_good_ms);
    CHECK(ctx.next_read_ticks == now_ticks + 20000);
    init_result = 0;
    now_ticks = ctx.next_read_ticks;
    CHECK(!acquisition_environment_update(&ctx, false));
    CHECK(ctx.configured && start_count == starts_before_revalidation + 1);
    now_ticks = ctx.ready_ticks;
    CHECK(acquisition_environment_update(&ctx, false));
    CHECK(ctx.data.quality.valid && ctx.data.quality.last_good_ms > retained.quality.last_good_ms);

    printf("PASS: %u acquisition environment checks\n", checks);
    return 0;
}
