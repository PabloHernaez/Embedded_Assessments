#include "statistics.h"

#define STATS_WINDOW_MS 30000

static void open_window(StatsCtx *ctx, int64_t begin_ms)
{
    ctx->window_begin_ms = begin_ms;
    ctx->window_end_ms = begin_ms + STATS_WINDOW_MS;
    for (int i = 0; i < STATS_METRIC_COUNT; ++i) {
        ctx->metric[i] = (MetricAccumulator){0};
    }
}

void statistics_begin(StatsCtx *ctx, int64_t now_ms)
{
    *ctx = (StatsCtx){ .active = true };
    open_window(ctx, now_ms);
}

void statistics_end(StatsCtx *ctx)
{
    *ctx = (StatsCtx){0};
}

void statistics_advance(StatsCtx *ctx, int64_t now_ms)
{
    if (!ctx->active || now_ms < ctx->window_end_ms) {
        return;
    }

    ctx->result = (StatsResult){
        .ready = true,
        .begin_ms = ctx->window_begin_ms,
        .end_ms = ctx->window_end_ms,
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
    open_window(ctx, ctx->window_end_ms);
}

void statistics_add(StatsCtx *ctx, StatsMetric metric, float value)
{
    MetricAccumulator *acc = &ctx->metric[metric];

    if (!ctx->active) {
        return;
    }
    if (acc->count == 0U || value < acc->min) {
        acc->min = value;
    }
    if (acc->count == 0U || value > acc->max) {
        acc->max = value;
    }
    acc->sum += value;
    acc->count++;
}
