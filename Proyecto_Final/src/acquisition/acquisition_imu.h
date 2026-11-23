#ifndef DRONE_TRACKER_ACQUISITION_IMU_H
#define DRONE_TRACKER_ACQUISITION_IMU_H

#include "model.h"
#include "../algorithms/imu_calibration.h"
#include "../algorithms/motion.h"

/* Estado privado de la IMU. Solo lo usa el hilo de adquisicion. */
typedef struct {
    /* Hardware y recuperacion */
    bool configured;
    bool bus_error;             /* esta vuelta fallo o se reinicio el bus I2C compartido */
    uint8_t consecutive_errors;
    int64_t last_init_ticks;
    int64_t last_new_ticks;     /* ultima muestra NEW: detecta un MPU que deja de medir */

    /* Datos publicados */
    ImuData data;
    NavigationView nav;
    bool moving;
    float accel_norm_mps2;      /* |a| con gravedad, para movimiento y estadisticas */
    float gyro_norm_dps;        /* |w| sin sesgo */
    int64_t sample_ticks;       /* instante de la ultima lectura */

    /* Calculo */
    ImuCalibration calibration;
    int64_t previous_imu_ticks; /* -1: la proxima muestra usa dt = 0 */
    MotionDetector motion;
} AcquisitionImu;

/* Deja el contexto en su estado inicial y configura el MPU6050 (puede dormir 140 ms). */
void acquisition_imu_init(AcquisitionImu *ctx);

/*
 * Una vuelta de IMU: lectura y procesado, o reinicio si hace falta.
 * Devuelve true si cambio algo que publicar (muestra nueva, fallo o reinicio)
 * y false si no hay muestra nueva (NO_NEW) o se espera para reintentar.
 */
bool acquisition_imu_update(AcquisitionImu *ctx);

#endif
