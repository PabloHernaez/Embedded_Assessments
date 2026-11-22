#ifndef DRONE_TRACKER_GYRO_CALIBRATION_H
#define DRONE_TRACKER_GYRO_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "model.h"
#include "algorithm_config.h"

/*
 * Sesgo del giroscopio: media de TRACKER_GYRO_CALIBRATION_SAMPLES (100) muestras
 * seguidas en reposo. Una muestra en movimiento reinicia la cuenta.
 */
typedef struct {
    Vec3f sum_dps;
    Vec3f bias_dps;
    uint16_t count;
    bool ready;
} GyroCalibration;

/* Deja la calibracion a cero (sin sesgo). */
void gyro_calibration_init(GyroCalibration *ctx);

/*
 * Anade una muestra NEW utilizable (giro sin corregir y magnitudes de |a| y |w|).
 * Reposo: | |a| - g | < 0,3 m/s2 y |w| < 5 grados/s. Devuelve true cuando el
 * sesgo esta listo; una calibracion terminada ya no cambia.
 */
bool gyro_calibration_update(GyroCalibration *ctx, const Vec3f *gyro_dps,
                             float accel_norm, float gyro_norm);

#endif
