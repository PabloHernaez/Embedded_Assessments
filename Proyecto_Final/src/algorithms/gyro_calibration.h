#ifndef DRONE_TRACKER_GYRO_CALIBRATION_H
#define DRONE_TRACKER_GYRO_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "model.h"
#include "algorithm_config.h"

typedef struct {
    Vec3f sum_dps;
    Vec3f bias_dps;
    uint16_t count;
    bool ready;
} GyroCalibration;

/* Inicializar una vez al arrancar. */
void gyro_calibration_init(GyroCalibration *ctx);

/*
 * Procesa una muestra nueva utilizable y sus magnitudes.
 *
 * gyro_dps == NULL: interrumpe la secuencia pendiente.
 * NO_NEW: el llamador no debe invocar esta funcion.
 *
 * Devuelve true cuando la calibracion esta terminada.
 * Una calibracion terminada conserva su sesgo.
 */
bool gyro_calibration_update(
    GyroCalibration *ctx,
    const Vec3f *gyro_dps,
    float accel_norm,
    float gyro_norm
);

#endif