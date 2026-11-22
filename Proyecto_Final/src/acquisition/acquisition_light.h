#ifndef DRONE_TRACKER_ACQUISITION_LIGHT_H
#define DRONE_TRACKER_ACQUISITION_LIGHT_H

#include "model.h"

typedef struct {
    bool configured;
    LightData data;
    int64_t next_read_ticks;
    int64_t sample_ticks;
} AcquisitionLight;

void acquisition_light_init(AcquisitionLight *ctx);

/* Cada 2 s lee el LDR. Devuelve true si hay un resultado nuevo (valido o no). */
bool acquisition_light_update(AcquisitionLight *ctx);

#endif
