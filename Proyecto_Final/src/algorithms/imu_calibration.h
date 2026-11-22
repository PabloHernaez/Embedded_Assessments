#ifndef DRONE_TRACKER_IMU_CALIBRATION_H
#define DRONE_TRACKER_IMU_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "model.h"
#include "algorithm_config.h"

/*
 * Calibracion inicial con TRACKER_GYRO_CALIBRATION_SAMPLES (100) muestras
 * seguidas en reposo: | |a| - g | < 0,3 m/s2 y |w| < 5 grados/s.
 * Una muestra en movimiento reinicia la cuenta.
 *   gyro_bias_dps   media del giro: se resta a cada lectura
 *   gravity_mps2    media de la aceleracion (g0) en los ejes iniciales del sensor
 */
typedef struct {
    Vec3f gyro_sum_dps;
    Vec3f accel_sum_mps2;
    Vec3f gyro_bias_dps;
    Vec3f gravity_mps2;
    uint16_t count;
    bool ready;
} ImuCalibration;

void imu_calibration_init(ImuCalibration *c);

/* Anade una muestra NEW utilizable. Devuelve true cuando la calibracion esta lista;
 * una calibracion terminada ya no cambia. */
bool imu_calibration_update(ImuCalibration *c, const Vec3f *accel_mps2, const Vec3f *gyro_dps);

#endif
