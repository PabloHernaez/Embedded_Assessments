#ifndef DRONE_TRACKER_STATISTICS_H
#define DRONE_TRACKER_STATISTICS_H

#include "model.h"

/* Acumulacion de una metrica durante una ventana. */
typedef struct {
    float sum;
    float min;
    float max;
    uint32_t count;
} MetricAccumulator;

typedef struct {
    bool active;

    int64_t ticks_per_second;
    int64_t session_begin_ticks;
    int64_t window_begin_ticks;
    int64_t window_end_ticks;

    MetricAccumulator metric[STATS_METRIC_COUNT];
    StatsResult result;
} StatsCtx;

void statistics_metric_reset(MetricAccumulator *acc);

/* Incorporar una muestra nueva, utilizable y finita. */
void statistics_metric_add(MetricAccumulator *acc, float value);

/* Obtener el resultado sin modificar el acumulador. */
MetricStats statistics_metric_result(const MetricAccumulator *acc);

/*
 * Iniciar una sesion nueva.
 * now_ticks: instante actual del kernel.
 * ticks_per_second: frecuencia del reloj de ticks, positiva.
 */
void statistics_begin(StatsCtx *ctx, int64_t now_ticks, int64_t ticks_per_second);

/* Desactivar y descartar acumuladores y resultados anteriores. */
void statistics_end(StatsCtx *ctx);

/* Atender el vencimiento aunque no haya muestras nuevas. */
void statistics_advance(StatsCtx *ctx, int64_t now_ticks);

/*
 * Incorporar un valor nuevo, utilizable y finito.
 * metric debe ser una de las cinco metricas de StatsMetric.
 *
 * capture_begin_ticks: instante anterior a iniciar la lectura.
 * sample_ticks: instante en que se obtuvo el resultado.
 */
void statistics_add(StatsCtx *ctx,
                    StatsMetric metric,
                    float value,
                    int64_t capture_begin_ticks,
                    int64_t sample_ticks);

#endif