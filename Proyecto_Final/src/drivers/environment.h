#ifndef DRONE_TRACKER_ENVIRONMENT_H
#define DRONE_TRACKER_ENVIRONMENT_H

/* Peor caso del datasheet para RH + T: 12 + 10,8 = 22,8 ms. 40 ms deja margen. */
#define ENVIRONMENT_CONVERSION_WAIT_MS 40U

typedef struct {
    float temperature_c;
    float humidity_pct;
} EnvironmentReading;

/* Deja el Si7021 en RH 12 bits / T 14 bits sin calentador. 0 o error negativo. */
int environment_init(void);

/* Envia F5 (medida sin hold). La lectura se recoge despues, sin esperar aqui. */
int environment_start(void);

/*
 * Recoge humedad (con CRC) y la temperatura de esa misma medida (E0).
 * Llamar al menos ENVIRONMENT_CONVERSION_WAIT_MS despues de environment_start.
 * 0: actualiza *out con el par completo. Error negativo: *out sin cambios.
 */
int environment_read(EnvironmentReading *out);

#endif
