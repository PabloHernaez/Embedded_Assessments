#ifndef DRONE_TRACKER_LIGHT_H
#define DRONE_TRACKER_LIGHT_H

#include <stdbool.h>
#include <stdint.h>

/* Luz relativa a la calibracion del montaje: 0 % oscuridad, 100 % maximo. */
typedef struct {
    float light_pct;
    bool clipped; /* El codigo ADC cayo fuera de las referencias y se recorto. */
} LightReading;

/* Configura ADC1 canal 5 (PB1) segun el devicetree. Retorna 0 o error negativo. */
int light_init(void);

/* Una conversion de 12 bits (0-4095). Retorna 0 o error negativo. */
int light_read_raw(int16_t *raw);

/* Convierte el codigo ADC a porcentaje con las referencias medidas. */
LightReading light_from_raw(int16_t raw);

#endif
