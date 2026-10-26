#ifndef DRONE_TRACKER_HUMAN_REPORT_H
#define DRONE_TRACKER_HUMAN_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "model.h"

#define HUMAN_REPORT_BUFFER_SIZE 1024U

/*
 *  Los punteros apuntan a copias del llamador.
 *  Deben permanecer validos durante la llamada.
 *  uptime_ms -> Tiempo desde arranque
 *  reset_cause -> causa de reinicio obtenida por la aplicación
 *  simu_replaced_fields -> contador de campos sustituidos en SIMU
 */
typedef struct {
    const AcquisitionSnapshot *acquisition;
    const GpsSnapshot *gps;
    const TrackerStatus *status;

    uint64_t uptime_ms;
    uint32_t reset_cause;
    uint32_t simu_replaced_fields;
} HumanReportInput;

/*
 * mode debe ser APP_MODE_TEST o APP_MODE_NORMAL.
 * Todos los punteros son obligatorios y deben ser validos.
 *
 * Al entrar: *len = 0.
 *
 * Exito: retorna 0 y termina el texto con '\0'.
 * *len indica los bytes del reporte, excluyendo ese terminador.
 *
 * Capacidad insuficiente: retorna -EMSGSIZE y deja *len = 0.
 * El llamador solo transmite cuando la funcion retorna 0.
 *
 * Funcion pura: no accede a hardware/reloj, no modifica las entradas
 * ni conserva sus punteros.
 * 
 *  in  -> Datos de entrada que el formateador consulta
 *  buf -> Memoria donde escribe el mensaje
 *  cap -> Capacidad real de la memoria en bytes
 *  len -> dirección de donde devolverá la longitud escrita
 */
int human_report_format(AppMode mode,
                        const HumanReportInput *in,
                        char *buf,
                        size_t cap,
                        size_t *len);

#endif