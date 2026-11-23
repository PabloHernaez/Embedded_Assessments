#ifndef DRONE_TRACKER_ESTIMATOR_H
#define DRONE_TRACKER_ESTIMATOR_H

#include "model.h"

/*
 * Navegacion inercial en los ejes del sensor al terminar la calibracion
 * (orientacion inicial cero, SR4). C puro, sin hardware ni reloj.
 */

/* q identidad, velocidad y posicion a cero. */
void estimator_init(NavigationView *nav);

/*
 * Un paso de integracion (Euler explicito) con una muestra calibrada:
 *   gravity_mps2  g0, la aceleracion media en reposo medida al calibrar
 *   accel_mps2    aceleracion medida (con gravedad), ejes del sensor
 *   gyro_dps      giro sin sesgo, grados/s
 *   dt_s          tiempo desde la muestra anterior; 0 tras un hueco o un fallo
 */
void estimator_update(NavigationView *nav, const Vec3f *gravity_mps2,
                      const Vec3f *accel_mps2, const Vec3f *gyro_dps, float dt_s);

#endif
