#ifndef DRONE_TRACKER_ACQUISITION_ENVIRONMENT_H
#define DRONE_TRACKER_ACQUISITION_ENVIRONMENT_H

#include "model.h"

typedef struct {
    bool configured;
    EnvironmentData data;
    int64_t next_read_ticks;
    int64_t ready_ticks;  /* fin de la conversion en curso; -1 si no hay ninguna */
    int64_t sample_ticks;
} AcquisitionEnvironment;

void acquisition_environment_init(AcquisitionEnvironment *ctx);

/*
 * Cada 2 s lanza una conversion (F5) y la recoge 40 ms despues, en otra vuelta,
 * sin bloquear el hilo. Si bus_error es true la IMU acaba de fallar o reiniciar
 * el bus compartido: no se usa el I2C en esta vuelta, se abandona la conversion
 * en curso y el Si7021 se revalida antes de la siguiente.
 * Devuelve true si hay un resultado nuevo (par valido o fallo).
 */
bool acquisition_environment_update(AcquisitionEnvironment *ctx, bool bus_error);

#endif
