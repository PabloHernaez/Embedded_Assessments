#ifndef DRONE_TRACKER_IMU_H
#define DRONE_TRACKER_IMU_H

#include <stdbool.h>

#include "model.h"

/* Resultados no negativos de imu_read; los errores I2C son negativos. */
enum {
    IMU_NO_NEW = 0, /* DATA_RDY a cero: no hay muestra nueva desde la ultima lectura */
    IMU_NEW = 1,
};

/* Muestra en unidades fisicas, antes de calibracion y correcciones. */
typedef struct {
    Vec3f accel_mps2;
    Vec3f gyro_dps;
    bool range_limited; /* algun eje cerca del fondo de escala */
} ImuReading;

/*
 * Reset, configuracion (+-4 g, +-500 grados/s, DLPF4, 50 Hz) con relectura
 * y 140 ms de esperas. Retorna 0 o error negativo. Solo desde un hilo.
 */
int imu_init(void);

/* IMU_NEW (actualiza *out), IMU_NO_NEW o error negativo (sin tocar *out). */
int imu_read(ImuReading *out);

/* Nueve pulsos SCL y STOP mediante i2c_recover_bus para liberar el bus compartido. */
int imu_recover_bus(void);

#endif
