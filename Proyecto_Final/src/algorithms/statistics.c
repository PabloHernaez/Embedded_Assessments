#include "statistics.h"

#define STATS_WINDOW_SECONDS 30

static int64_t ticks_to_ms(int64_t ticks, int64_t ticks_per_second)
{
    return ticks * 1000 / ticks_per_second;
}

static void open_window(StatsCtx *ctx, int64_t begin_ticks)
{
    ctx->window_begin_ticks = begin_ticks;
    ctx->window_end_ticks = begin_ticks + STATS_WINDOW_SECONDS * ctx->ticks_per_second;
    for (int i = 0; i < STATS_METRIC_COUNT; ++i) {
        ctx->metric[i] = (MetricAccumulator){0};
    }
}

void statistics_begin(StatsCtx *ctx, int64_t now_ticks, int64_t ticks_per_second)
{
    *ctx = (StatsCtx){ .active = true, .ticks_per_second = ticks_per_second };
    open_window(ctx, now_ticks);
}

void statistics_end(StatsCtx *ctx)
{
    *ctx = (StatsCtx){0};
}

void statistics_advance(StatsCtx *ctx, int64_t now_ticks)
{
    if (!ctx->active || now_ticks < ctx->window_end_ticks) {
        return;
    }

    /* Si se perdio mas de una ventana entera, no fabricar resultados atrasados. */
    if (now_ticks - ctx->window_end_ticks >= STATS_WINDOW_SECONDS * ctx->ticks_per_second) {
        ctx->result = (StatsResult){0};
        open_window(ctx, now_ticks);
        return;
    }

    ctx->result = (StatsResult){
        .ready = true,
        .begin_ms = ticks_to_ms(ctx->window_begin_ticks, ctx->ticks_per_second),
        .end_ms = ticks_to_ms(ctx->window_end_ticks, ctx->ticks_per_second),
    };
    for (int i = 0; i < STATS_METRIC_COUNT; ++i) {
        const MetricAccumulator *acc = &ctx->metric[i];
        ctx->result.metric[i] = (MetricStats){
            .count = acc->count,
            .mean = acc->count > 0U ? acc->sum / (float)acc->count : 0.0f,
            .min = acc->min,
            .max = acc->max,
        };
    }
    open_window(ctx, ctx->window_end_ticks);
}

void statistics_add(StatsCtx *ctx, StatsMetric metric, float value, int64_t sample_ticks)
{
    if (!ctx->active || sample_ticks < ctx->window_begin_ticks) {
        return; /* sin sesion, o muestra anterior al comienzo */
    }
    statistics_advance(ctx, sample_ticks); /* una muestra en t1 ya es de la ventana siguiente */

    MetricAccumulator *acc = &ctx->metric[metric];
    if (acc->count == 0U || value < acc->min) {
        acc->min = value;
    }
    if (acc->count == 0U || value > acc->max) {
        acc->max = value;
    }
    acc->sum += value;
    acc->count++;
}
