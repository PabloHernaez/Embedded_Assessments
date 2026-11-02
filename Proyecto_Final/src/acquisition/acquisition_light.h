#ifndef DRONE_TRACKER_ACQUISITION_LIGHT_H
#define DRONE_TRACKER_ACQUISITION_LIGHT_H

#include "model.h"

typedef struct {
    bool configured;
    LightData data;
    int64_t next_read_ticks;
    int64_t capture_begin_ticks;
    int64_t sample_ticks;
} AcquisitionLight;

/**
 * Prepara el contexto de luz; el ADC se configura al atender una lectura.
 * Entradas: ctx apunta a memoria válida y escribible, sin contenido previo requerido.
 * Retorno: ninguno (void).
 * Modifica: sobrescribe *ctx con datos inválidos y la primera lectura vencida.
 */
void acquisition_light_init(AcquisitionLight *ctx);

/**
 * Atiende una lectura de luz si vence su cadencia de 2 s.
 * Entradas: ctx válido, inicializado y usado por un único hilo; usa reloj y ADC.
 * Retorno: true si se atiende un intento, aunque falle; false si aún no vence.
 * Modifica: en un intento actualiza tiempos, configuración y datos/calidad de *ctx.
 *           Un fallo conserva valores y última fecha buena; false conserva todo ctx.
 */
bool acquisition_light_update(AcquisitionLight *ctx);

#endif
