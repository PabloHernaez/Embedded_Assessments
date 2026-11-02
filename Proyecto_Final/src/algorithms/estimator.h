#ifndef DRONE_TRACKER_ESTIMATOR_H
#define DRONE_TRACKER_ESTIMATOR_H

#include "model.h"
#include <stdbool.h>

/* Preparar una vez al arrancar: ambos cuaterniones identidad, v/p cero,
 * valid=false. El llamador puede preparar despues la inclinacion inicial.
 */
void estimator_init(NavigationView *state);

/* Preparar la inclinacion antes de integrar, usando aceleracion corregida
 * promediada en reposo. Se elige yaw inicial cero en el marco local.
 * Exito: actualiza q, deja relative_q identidad y valid=false; conserva v/p.
 * Fallo: conserva todo el estado. Todos los punteros deben ser validos.
 */
bool estimator_set_initial_tilt(
    NavigationView *state,
    const Vec3f *accel_mean_mps2
);

/* Actualizar orientacion Hamilton sensor -> local con giro corregido en
 * grados/s y dt real entre 0 y 50 ms inclusive. Exito: q unitario actualizado.
 * Fallo: conserva q. El llamador omite NO_NEW y gestiona continuidad.
 * Todos los punteros deben ser validos.
 */
bool estimator_orientation_update(
    Quaternionf *q,
    const Vec3f *gyro_dps,
    float dt_s
);

/* Avanzar ambas orientaciones, velocidad y posicion como un candidato.
 * state: preparado por estimator_init y, si procede, inclinacion inicial.
 * initial_q: referencia unitaria fija, capturada al terminar la preparacion.
 * accel_mps2: aceleracion corregida en ejes del sensor, incluida gravedad.
 * gyro_dps: giro corregido por sesgo, en grados/s.
 * dt_s: intervalo real de 0 a 50 ms inclusive.
 *
 * Exito: confirma q, relative_q, v/p juntos y marca valid=true.
 * Fallo: conserva esos valores y marca valid=false.
 * El llamador exige preparacion terminada, omite NO_NEW y entrega dt=0 al
 * comenzar o retomar tras una ruptura. Un rechazo rompe la referencia temporal.
 * Todos los punteros deben ser validos.
 */
bool estimator_update(
    NavigationView *state,
    const Quaternionf *initial_q,
    const Vec3f *accel_mps2,
    const Vec3f *gyro_dps,
    float dt_s
);

/* Obtener q_rel = initial^-1 * current.
 * Entradas: cuaterniones unitarios Hamilton sensor -> local.
 * Salida: sensor actual -> ejes del sensor en la postura inicial.
 * Exito: salida unitaria. Fallo: conserva la salida.
 * Todos los punteros deben ser validos.
 */
bool estimator_relative_orientation(
    Quaternionf *relative,
    const Quaternionf *initial,
    const Quaternionf *current
);

#endif
