#ifndef DRONE_TRACKER_STATISTICS_H
#define DRONE_TRACKER_STATISTICS_H

#include "model.h"

/*
 * Estadisticas de NORMAL en ventanas consecutivas de 30 s, [inicio, fin).
 * Codigo C puro: los tiempos llegan en ticks del kernel.
 */

typedef struct {
    float sum;
    float min;
    float max;
    uint32_t count;
} MetricAccumulator;

typedef struct {
    bool active;
    int64_t ticks_per_second;
    int64_t window_begin_ticks;
    int64_t window_end_ticks;
    MetricAccumulator metric[STATS_METRIC_COUNT];
    StatsResult result; /* ultima ventana completa */
} StatsCtx;

/* Empieza una sesion (al entrar en NORMAL) descartando todo lo anterior. */
void statistics_begin(StatsCtx *ctx, int64_t now_ticks, int64_t ticks_per_second);

/* Termina la sesion (al salir de NORMAL) y oculta el resultado. */
void statistics_end(StatsCtx *ctx);

/* Cierra la ventana si ya vencio, aunque no haya llegado ninguna muestra. */
void statistics_advance(StatsCtx *ctx, int64_t now_ticks);

/* Anade una muestra nueva y valida tomada en sample_ticks. */
void statistics_add(StatsCtx *ctx, StatsMetric metric, float value, int64_t sample_ticks);

#endif
