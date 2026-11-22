#include <math.h>
#include <stdio.h>

#include "statistics.h"

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

#define TPS 10000          /* ticks por segundo, como en produccion */
#define WINDOW (30 * TPS)

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

int main(void)
{
    unsigned int checks = 0;
    StatsCtx ctx = {0};

    /* Sin sesion no se acumula ni se publica nada. */
    statistics_add(&ctx, STATS_TEMP, 20.0f, 100);
    statistics_advance(&ctx, 10 * WINDOW);
    CHECK(!ctx.active && !ctx.result.ready && ctx.metric[STATS_TEMP].count == 0U);

    /* Sesion desde t0 = 5 s. Una muestra anterior al comienzo no cuenta. */
    const int64_t t0 = 5 * TPS;
    statistics_begin(&ctx, t0, TPS);
    statistics_add(&ctx, STATS_TEMP, 99.0f, t0 - 1);
    CHECK(ctx.metric[STATS_TEMP].count == 0U);

    /* Valores repetidos cuentan cada vez: son muestras nuevas. */
    statistics_add(&ctx, STATS_TEMP, 20.0f, t0);
    statistics_add(&ctx, STATS_TEMP, 22.0f, t0 + TPS);
    statistics_add(&ctx, STATS_TEMP, 22.0f, t0 + 2 * TPS);
    statistics_add(&ctx, STATS_LIGHT, 50.0f, t0 + WINDOW - 1);
    statistics_advance(&ctx, t0 + WINDOW - 1);
    CHECK(!ctx.result.ready);

    /* Una muestra exactamente en t1 cierra la ventana y va a la siguiente. */
    statistics_add(&ctx, STATS_TEMP, 30.0f, t0 + WINDOW);
    CHECK(ctx.result.ready);
    CHECK(ctx.result.begin_ms == 5000 && ctx.result.end_ms == 35000);
    const MetricStats temp = ctx.result.metric[STATS_TEMP];
    CHECK(temp.count == 3U && near(temp.mean, 64.0f / 3.0f));
    CHECK(near(temp.min, 20.0f) && near(temp.max, 22.0f));
    CHECK(ctx.result.metric[STATS_LIGHT].count == 1U);
    CHECK(ctx.result.metric[STATS_RH].count == 0U); /* el informe lo muestra N/D */
    CHECK(ctx.metric[STATS_TEMP].count == 1U);

    /* La ventana se cierra a los 30 s aunque no lleguen muestras. */
    statistics_advance(&ctx, t0 + 2 * WINDOW);
    CHECK(ctx.result.end_ms == 65000 && ctx.result.metric[STATS_TEMP].count == 1U);
    CHECK(near(ctx.result.metric[STATS_TEMP].mean, 30.0f));
    statistics_advance(&ctx, t0 + 3 * WINDOW);
    CHECK(ctx.result.ready && ctx.result.metric[STATS_TEMP].count == 0U);

    /* Si se pierde mas de una ventana entera no se fabrican resultados atrasados. */
    statistics_advance(&ctx, t0 + 5 * WINDOW + 7);
    CHECK(!ctx.result.ready && ctx.window_begin_ticks == t0 + 5 * WINDOW + 7);

    /* begin reinicia una sesion activa; end la descarta y oculta el resultado. */
    statistics_add(&ctx, STATS_RH, 40.0f, t0 + 5 * WINDOW + 8);
    statistics_begin(&ctx, t0 + 6 * WINDOW, TPS);
    CHECK(ctx.metric[STATS_RH].count == 0U && !ctx.result.ready);
    statistics_advance(&ctx, t0 + 7 * WINDOW);
    CHECK(ctx.result.ready);
    statistics_end(&ctx);
    CHECK(!ctx.active && !ctx.result.ready);

    printf("PASS: %u statistics checks\n", checks);
    return 0;
}
