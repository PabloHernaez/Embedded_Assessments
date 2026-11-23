#ifndef DRONE_TRACKER_STATISTICS_H
#define DRONE_TRACKER_STATISTICS_H

#include "model.h"

/*
 * Estadisticas de NORMAL en ventanas consecutivas de 30 s, [inicio, fin).
 * Codigo C puro: los tiempos llegan en ms desde el arranque.
 */

typedef struct {
    float sum;
    float min;
    float max;
    uint32_t count;
} MetricAccumulator;

typedef struct {
    bool active;
    int64_t window_begin_ms;
    int64_t window_end_ms;
    MetricAccumulator metric[STATS_METRIC_COUNT];
    StatsResult result; /* ultima ventana completa */
} StatsCtx;

/* Empieza una sesion (al entrar en NORMAL) descartando todo lo anterior. */
void statistics_begin(StatsCtx *ctx, int64_t now_ms);

/* Termina la sesion (al salir de NORMAL) y oculta el resultado. */
void statistics_end(StatsCtx *ctx);

/* Si la ventana ya vencio, publica su resultado y abre la siguiente. */
void statistics_advance(StatsCtx *ctx, int64_t now_ms);

/* Anade una muestra nueva y valida a la ventana abierta. */
void statistics_add(StatsCtx *ctx, StatsMetric metric, float value);

#endif
