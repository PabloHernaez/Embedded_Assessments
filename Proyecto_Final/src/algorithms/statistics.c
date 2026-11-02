#include "statistics.h"

#define STATS_WINDOW_SECONDS 30

void statistics_metric_reset(MetricAccumulator *acc)
{
    *acc = (MetricAccumulator){0};
}

void statistics_metric_add(MetricAccumulator *acc, float value)
{
    if (acc->count == 0U) {
        acc->min = value;
        acc->max = value;
    } else {
        if (value < acc->min) {
            acc->min = value;
        }

        if (value > acc->max) {
            acc->max = value;
        }
    }

    acc->sum += value;
    acc->count++;
}

MetricStats statistics_metric_result(const MetricAccumulator *acc)
{
    MetricStats result = {
        .count = acc->count,
    };

    if (acc->count > 0U) {
        result.mean = acc->sum / (float)acc->count;
        result.min = acc->min;
        result.max = acc->max;
    }

    return result;
}

void statistics_begin(StatsCtx *ctx,
                      int64_t now_ticks,
                      int64_t ticks_per_second)
{
    *ctx = (StatsCtx){
        .active = true,
        .ticks_per_second = ticks_per_second,
        .session_begin_ticks = now_ticks,
        .window_begin_ticks = now_ticks,
        .window_end_ticks =
            now_ticks + STATS_WINDOW_SECONDS * ticks_per_second,
    };
}

void statistics_end(StatsCtx *ctx)
{
    *ctx = (StatsCtx){0};
}

static int64_t statistics_ticks_to_ms(int64_t ticks,
                                      int64_t ticks_per_second)
{
    return (ticks / ticks_per_second) * 1000 +
           ((ticks % ticks_per_second) * 1000) / ticks_per_second;
}

static void statistics_window_reset(StatsCtx *ctx, int64_t begin_ticks)
{
    ctx->window_begin_ticks = begin_ticks;
    ctx->window_end_ticks =
        begin_ticks + STATS_WINDOW_SECONDS * ctx->ticks_per_second;

    for (int i = 0; i < STATS_METRIC_COUNT; ++i) {
        statistics_metric_reset(&ctx->metric[i]);
    }
}

void statistics_advance(StatsCtx *ctx, int64_t now_ticks)
{
    if (!ctx->active || now_ticks < ctx->window_end_ticks) {
        return;
    }

    int64_t window_ticks = STATS_WINDOW_SECONDS * ctx->ticks_per_second;

    if (now_ticks - ctx->window_end_ticks >= window_ticks) {
        ctx->result = (StatsResult){0};
        statistics_window_reset(ctx, now_ticks);
        return;
    }

    ctx->result = (StatsResult){
        .ready = true,
        .begin_ms = statistics_ticks_to_ms(ctx->window_begin_ticks,
                                          ctx->ticks_per_second),
        .end_ms = statistics_ticks_to_ms(ctx->window_end_ticks,
                                        ctx->ticks_per_second),
    };

    for (int i = 0; i < STATS_METRIC_COUNT; ++i) {
        ctx->result.metric[i] =
            statistics_metric_result(&ctx->metric[i]);
    }

    statistics_window_reset(ctx, ctx->window_end_ticks);
}

void statistics_add(StatsCtx *ctx,
                    StatsMetric metric,
                    float value,
                    int64_t capture_begin_ticks,
                    int64_t sample_ticks)
{
    if (!ctx->active || capture_begin_ticks < ctx->session_begin_ticks) {
        return;
    }

    statistics_advance(ctx, sample_ticks);

    if (sample_ticks < ctx->window_begin_ticks) {
        return;
    }

    statistics_metric_add(&ctx->metric[metric], value);
}