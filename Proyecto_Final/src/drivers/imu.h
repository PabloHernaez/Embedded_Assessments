#ifndef DRONE_TRACKER_IMU_H
#define DRONE_TRACKER_IMU_H

#include <stdbool.h>

#include "model.h"

enum {
    IMU_NO_NEW = 0,
    IMU_NEW = 1
};

/**
 * Resultado privado del driver, antes de aplicar calibracion. 
 */
typedef struct {
    Vec3f accel_mps2;
    Vec3f gyro_dps;
    bool range_limited;
} ImuReading;

/*
 * Inicializa y verifica el MPU6050.
 * Retorna 0 o un error negativo.
 * Requiere alimentacion estabilizada y contexto de hilo.
 */
int imu_init(void);

/*
 * Requiere inicializacion correcta y un puntero out valido.
 *
 * IMU_NEW: actualiza out, incluida la indicacion de saturacion.
 * IMU_NO_NEW o error negativo: conserva out.
 *
 * Uso desde un unico hilo.
 */
int imu_read(ImuReading *out);

/*
 * Comprueba alimentacion/configuracion y escalas de muestreo.
 * Requiere matches valido y uso desde el mismo hilo del driver.
 *
 * Retorna 0: actualiza matches con el resultado de la comparacion.
 *      matches = true  -> Se leyó y los ajustes coinciden
 *      matches = false -> Se leyó pero al menos un ajuste difiere 
 * Retorna un error negativo: conserva matches.
 *      La comprobación falló
 *
 * Solo lee registros; no modifica la configuracion.
 * 
 */
int imu_check_configuration(bool *matches);

#endif