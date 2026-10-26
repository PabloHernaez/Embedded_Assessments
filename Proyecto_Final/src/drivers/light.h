#ifndef DRONE_TRACKER_LIGHT_H
#define DRONE_TRACKER_LIGHT_H

#include <stdint.h>
#include <stdbool.h>

/* Configura el canal ADC. Retorna 0 o un error negativo. */
int light_init(void);

/*
 * Obtiene una conversion ADC.
 *
 * out debe apuntar a memoria valida.
 * Retorna 0 y actualiza *out al completar correctamente.
 * Ante un error, conserva el contenido anterior de *out.
 *
 * Requiere una inicializacion previa correcta.
 * Uso desde un unico hilo.
 */
int light_read_raw(int16_t *out);

/* Resultado convertido; adquisicion añadira calidad y fecha. */
typedef struct {
    float light_pct;
    bool clipped;
} LightReading;

/*
 * Convierte un codigo ADC usando dos referencias de calibracion.
 *
 * Los tres valores deben estar entre 0 y 4095.
 * raw_dark y raw_bright deben ser diferentes.
 * out debe apuntar a memoria valida.
 *
 * Retorna 0 incluso cuando hay recorte.
 * Ante argumentos invalidos, retorna -EINVAL y conserva *out.
 */
int light_raw_to_percent(int16_t raw,
                         int16_t raw_dark,
                         int16_t raw_bright,
                         LightReading *out);


#endif