#include <math.h>
#include <stdio.h>

#include "statistics.h"

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

#define WINDOW_MS 30000

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

int main(void)
{
    unsigned int checks = 0;
    StatsCtx ctx = {0};

    /* Sin sesion no se acumula ni se publica nada. */
    statistics_add(&ctx, STATS_TEMP, 20.0f);
    statistics_advance(&ctx, 10 * WINDOW_MS);
    CHECK(!ctx.active && !ctx.result.ready && ctx.metric[STATS_TEMP].count == 0U);

    /* Sesion desde t0 = 5 s. Valores repetidos cuentan cada vez: son muestras nuevas. */
    const int64_t t0 = 5000;
    statistics_begin(&ctx, t0);
    statistics_add(&ctx, STATS_TEMP, 20.0f);
    statistics_add(&ctx, STATS_TEMP, 22.0f);
    statistics_add(&ctx, STATS_TEMP, 22.0f);
    statistics_add(&ctx, STATS_LIGHT, 50.0f);
    statistics_advance(&ctx, t0 + WINDOW_MS - 1);
    CHECK(!ctx.result.ready);

    /* A los 30 s exactos se publica la ventana [t0, t0 + 30 s) y se abre la siguiente. */
    statistics_advance(&ctx, t0 + WINDOW_MS);
    CHECK(ctx.result.ready && ctx.result.begin_ms == 5000 && ctx.result.end_ms == 35000);
    const MetricStats temp = ctx.result.metric[STATS_TEMP];
    CHECK(temp.count == 3U && near(temp.mean, 64.0f / 3.0f));
    CHECK(near(temp.min, 20.0f) && near(temp.max, 22.0f));
    CHECK(ctx.result.metric[STATS_LIGHT].count == 1U);
    CHECK(ctx.result.metric[STATS_RH].count == 0U); /* el informe lo muestra N/D */
    CHECK(ctx.metric[STATS_TEMP].count == 0U);

    /* Una muestra tras el cierre va a la ventana nueva; sin muestras tambien se cierra. */
    statistics_add(&ctx, STATS_TEMP, 30.0f);
    statistics_advance(&ctx, t0 + 2 * WINDOW_MS);
    CHECK(ctx.result.end_ms == 65000 && ctx.result.metric[STATS_TEMP].count == 1U);
    CHECK(near(ctx.result.metric[STATS_TEMP].mean, 30.0f));
    statistics_advance(&ctx, t0 + 3 * WINDOW_MS);
    CHECK(ctx.result.ready && ctx.result.metric[STATS_TEMP].count == 0U);

    /* begin reinicia una sesion activa; end la descarta y oculta el resultado. */
    statistics_add(&ctx, STATS_RH, 40.0f);
    statistics_begin(&ctx, t0 + 4 * WINDOW_MS);
    CHECK(ctx.metric[STATS_RH].count == 0U && !ctx.result.ready);
    statistics_advance(&ctx, t0 + 5 * WINDOW_MS);
    CHECK(ctx.result.ready);
    statistics_end(&ctx);
    CHECK(!ctx.active && !ctx.result.ready);

    printf("PASS: %u statistics checks\n", checks);
    return 0;
}
