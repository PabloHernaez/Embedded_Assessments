#ifndef DRONE_TRACKER_NMEA_H
#define DRONE_TRACKER_NMEA_H

#include <stdbool.h>

#include "model.h"

/* Parser NMEA 0183 en C puro: sin Zephyr, reloj ni estado propio. */

/* "$...*HH" con HH = XOR de los caracteres entre '$' y '*'. */
bool nmea_checksum_ok(const char *line);

/*
 * Si line es una GGA bien formada ($GPGGA, $GNGGA...) actualiza gps y devuelve true.
 *   Calidad 1 (GPS) o 2 (DGPS): fix_valid = true, posicion, altitud y satelites.
 *   Otra calidad: fix_valid = false y se conserva la ultima posicion.
 *   La hora UTC se valida aparte (puede llegar sin fix).
 * No toca last_nmea_ms ni last_fix_ms (las fechas las pone quien tiene el reloj).
 * Modifica line: separa los campos.
 */
bool nmea_parse_gga(char *line, GpsSnapshot *gps);

#endif
