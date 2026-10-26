#ifndef DRONE_TRACKER_ESTIMATOR_H
#define DRONE_TRACKER_ESTIMATOR_H

#include "model.h"

#include <stdbool.h>

/*
 * Inicializar una vez al arrancar, con el sensor nivelado y +Z arriba.
 * El estado pertenece al llamador; los consumidores reciben una copia.
 */
void estimator_init(NavigationView *state);

/*
 * Actualizar la orientacion sensor -> local.
 *
 * gyro_dps: giro corregido por sesgo, expresado en grados/s.
 * dt_s: intervalo real en segundos; admite 0 hasta 50 ms inclusive.
 * Los punteros deben ser validos.
 *
 * true: q queda normalizado y actualizado.
 * false: intervalo o resultado numerico invalido; conserva q.
 *
 * El llamador gestiona la continuidad y omite NO_NEW.
 */
bool estimator_orientation_update(Quaternionf *q,
                                  const Vec3f *gyro_dps,
                                  float dt_s);


#endif
