#ifndef DRONE_TRACKER_ACQUISITION_ENVIRONMENT_H
#define DRONE_TRACKER_ACQUISITION_ENVIRONMENT_H

#include "model.h"

typedef struct {
    bool configured;
    bool bus_interrupted; /* Fallo o cancelación del trabajo I2C en esta vuelta. */
    EnvironmentData data;
    int64_t next_read_ticks;
    int64_t ready_ticks; /* -1: no hay una conversión pendiente. */
    int64_t capture_begin_ticks;
    int64_t sample_ticks;
} AcquisitionEnvironment;

/**
 * Prepara el contexto ambiental para un primer intento en la siguiente vuelta.
 * Entradas: ctx apunta a memoria válida y escribible, sin contenido previo requerido.
 * Retorno: ninguno (void).
 * Modifica: sobrescribe *ctx con datos inválidos y sin conversión pendiente.
 *           La configuración del Si7021 se realiza al atender el primer intento.
 */
void acquisition_environment_init(AcquisitionEnvironment *ctx);

/**
 * Inicia o recoge una conversión ambiental cuando vence su plazo.
 * Entradas: ctx válido e inicializado; bus_interrupted indica un error previo
 *           o recuperación física en el bus compartido durante esta vuelta.
 * Retorno: true si hay un par o calidad que publicar; false mientras espera,
 *          incluido el arranque correcto o un bus no permitido sin conversión pendiente.
 * Modifica: tiempos, bus_interrupted, configuración y datos/calidad de *ctx. Conserva valores
 *           y última fecha buena ante fallos. Usa el driver sin esperar la conversión.
 *           Un fallo previo deja configured=false para revalidar antes del siguiente F5.
 *           Solo reprograma/invalida calidad si cancela una conversión pendiente;
 *           sin ella conserva calidad y vencimiento. Siempre omite I2C esa vuelta.
 */
bool acquisition_environment_update(AcquisitionEnvironment *ctx, bool bus_interrupted);

#endif
