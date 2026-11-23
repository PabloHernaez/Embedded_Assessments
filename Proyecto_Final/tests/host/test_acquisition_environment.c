#include <errno.h>
#include <math.h>
#include <stdio.h>

#include "acquisition_environment.h"
#include "environment.h"
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
static int64_t start_delay_ticks;
static EnvironmentReading next_reading;

int environment_init(void)
{
    ++init_count;
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
    if (read_result == 0) {
        *out = next_reading;
    }
    return read_result;
}

int64_t k_uptime_ticks(void) { return now_ticks; }
int64_t k_uptime_get(void) { return now_ticks / 10; }
int64_t k_ticks_to_ms_floor64(int64_t ticks) { return ticks / 10; }
int64_t k_ms_to_ticks_floor64(int64_t ms) { return ms * 10; }
int64_t k_ms_to_ticks_ceil64(int64_t ms) { return ms * 10; }

/* Una vuelta del hilo de adquisicion, 20 ms despues de la anterior. */
static bool loop(AcquisitionEnvironment *ctx, bool bus_error)
{
    now_ticks += 200;
    return acquisition_environment_update(ctx, bus_error);
}

int main(void)
{
    unsigned int checks = 0;
    AcquisitionEnvironment ctx;

    acquisition_environment_init(&ctx);
    CHECK(!ctx.configured && !ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == -1 && ctx.ready_ticks == -1);

    /* Primera vuelta: configura, lanza F5 y no espera la conversion. */
    start_delay_ticks = 7; /* F5 tarda 0,7 ms: la espera cuenta desde que termina */
    CHECK(!loop(&ctx, false));
    CHECK(init_count == 1 && start_count == 1 && read_count == 0);
    CHECK(ctx.configured && ctx.ready_ticks == now_ticks + 400);
    start_delay_ticks = 0;

    /* 20 ms: aun no. 40 ms despues de F5: recoge el par completo. */
    CHECK(!loop(&ctx, false) && read_count == 0);
    next_reading = (EnvironmentReading){ .temperature_c = 24.6f, .humidity_pct = 45.2f };
    CHECK(loop(&ctx, false) && read_count == 1);
    CHECK(ctx.data.quality.valid && ctx.ready_ticks == -1);
    CHECK(fabsf(ctx.data.temperature_c - 24.6f) < 1e-5f);
    CHECK(ctx.data.quality.last_good_ms == now_ticks / 10);
    const EnvironmentData good = ctx.data;

    /* Hasta los 2 s no hay mas I2C ni se repite el par retenido. */
    for (int i = 0; i < 97; ++i) {
        CHECK(!loop(&ctx, false));
    }
    CHECK(start_count == 1 && read_count == 1);
    CHECK(!loop(&ctx, false) && start_count == 2 && init_count == 1); /* sin reconfigurar */

    /* Fallo al recoger: calidad invalida, par y fecha retenidos, revalidar en la siguiente. */
    read_result = -EIO;
    CHECK(!loop(&ctx, false));
    CHECK(loop(&ctx, false) && !ctx.data.quality.valid && !ctx.configured);
    CHECK(ctx.data.quality.last_good_ms == good.quality.last_good_ms);
    CHECK(fabsf(ctx.data.humidity_pct - good.humidity_pct) < 1e-5f);
    read_result = 0;
    while (start_count == 2) {
        (void)loop(&ctx, false);
    }
    CHECK(init_count == 2); /* revalida antes de F5 */

    /* Error de bus de la IMU con conversion en curso: esa vuelta no toca el I2C,
     * la conversion se recoge despues y el Si7021 se revalida antes del siguiente F5. */
    const unsigned int reads = read_count;
    CHECK(!loop(&ctx, true));
    CHECK(read_count == reads && ctx.ready_ticks >= 0 && !ctx.configured);
    CHECK(loop(&ctx, false) && read_count == reads + 1 && ctx.data.quality.valid);
    while (start_count == 3) {
        (void)loop(&ctx, false);
    }
    CHECK(init_count == 3); /* regresion 03/11: Si7021 mudo tras un pulso en SDA */

    /* Fallo de F5: calidad invalida y nuevo intento a los 2 s, no en cada vuelta. */
    CHECK(!loop(&ctx, false) && loop(&ctx, false) && ctx.data.quality.valid);
    start_result = -EIO;
    while (start_count == 4) {
        (void)loop(&ctx, false);
    }
    CHECK(!ctx.data.quality.valid && !ctx.configured && ctx.ready_ticks == -1);
    for (int i = 0; i < 98; ++i) {
        CHECK(!loop(&ctx, false));
    }
    CHECK(start_count == 5);

    printf("PASS: %u acquisition environment checks\n", checks);
    return 0;
}
