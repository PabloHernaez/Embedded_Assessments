#ifndef DRONE_TRACKER_ENVIRONMENT_H
#define DRONE_TRACKER_ENVIRONMENT_H

/* Espera base; el futuro vencimiento absoluto de adquisicion sumara un tick. */
#define ENVIRONMENT_CONVERSION_WAIT_MS 40U

typedef struct {
    float temperature_c;
    float humidity_pct;
} EnvironmentReading;

/*
 * Configura el Si7021:
 * - Humedad: resolucion de 12 bits.
 * - Temperatura: resolucion de 14 bits.
 * - Calentador interno apagado.
 *
 * Requiere alimentacion estabilizada y contexto de hilo.
 * Retorna 0 o un error negativo.
 */
int environment_init(void);

/*
 * Inicia una conversion de humedad y temperatura.
 * Requiere inicializacion correcta y ninguna medida pendiente.
 * Retorna 0 o un error negativo.
 */
int environment_start(void);

/*
 * Recoge una conversion iniciada correctamente.
 *
 * Contar la espera desde que environment_start termina el comando F5.
 * Un hilo puede usar k_msleep(ENVIRONMENT_CONVERSION_WAIT_MS): el kernel
 * redondea hacia arriba y compensa el tick parcial del timeout relativo.
 * Para el futuro ready_at absoluto, conservar ceil(40 ms) + 1 tick:
 * los vencimientos absolutos no reciben esa compensacion del kernel.
 *
 * Requiere out valido y uso desde el mismo hilo.
 * Retorna 0: actualiza el par completo.
 * Error negativo: conserva out y finaliza este intento.
 */
int environment_read(EnvironmentReading *out);

#endif
