#ifndef DRONE_TRACKER_ACQUISITION_ENVIRONMENT_H
#define DRONE_TRACKER_ACQUISITION_ENVIRONMENT_H

#include "model.h"

typedef struct {
    bool configured;
    EnvironmentData data;
    int64_t next_read_ticks;
    int64_t ready_ticks;  /* fin de la conversion en curso; -1 si no hay ninguna */
} AcquisitionEnvironment;

void acquisition_environment_init(AcquisitionEnvironment *ctx);

/*
 * Cada 2 s lanza una conversion (F5) y la recoge 40 ms despues, en otra vuelta,
 * sin bloquear el hilo. Si bus_error es true la IMU acaba de fallar o reiniciar
 * el bus compartido: no se usa el I2C en esta vuelta y el Si7021 se revalida
 * antes de su siguiente medida (una conversion en curso se recoge igual).
 * Devuelve true si hay un resultado nuevo (par valido o fallo).
 */
bool acquisition_environment_update(AcquisitionEnvironment *ctx, bool bus_error);

#endif
