#ifndef DRONE_TRACKER_SIMU_FRAME_H
#define DRONE_TRACKER_SIMU_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "model.h"

/*
 *  221 bytes es la trama incluyengo CRLF
 *  222 bytes es la trama con '\0 para manejarla como cadena C'       
 */
#define SIMU_FRAME_WIRE_SIZE   221U
#define SIMU_FRAME_BUFFER_SIZE 222U

_Static_assert(
    SIMU_FRAME_BUFFER_SIZE > SIMU_FRAME_WIRE_SIZE,
    "El buffer SIMU necesita espacio para el NUL final"
);

/*
 * Los punteros apuntan a copias del llamador.
 * Deben permanecer validos durante la llamada.
 */
typedef struct {
    const AcquisitionSnapshot *acquisition;
    const GpsSnapshot *gps;
    const TrackerStatus *status;

    uint16_t seq;
    uint64_t uptime_ms;
} SimuFrameInput;

/*
 * Todos los punteros son obligatorios y deben ser validos.
 * uptime_ms debe caber en las diez cifras del protocolo.
 *
 * Al entrar: *len = 0 y *replacements = 0.
 *
 * Exito: retorna 0, *len = 221 y buf[*len] = '\0'.
 * *replacements cuenta campos no finitos sustituidos en esta llamada.
 *
 * Capacidad menor que SIMU_FRAME_BUFFER_SIZE:
 * retorna -EMSGSIZE y mantiene ambas salidas a cero.
 *
 * Funcion pura: no accede a hardware/reloj, no modifica las entradas
 * ni conserva sus punteros.
 */
int simu_frame_encode(const SimuFrameInput *in,
                      char *buf,
                      size_t cap,
                      size_t *len,
                      uint16_t *replacements);

#endif