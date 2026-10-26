#ifndef DRONE_TRACKER_MOTION_H
#define DRONE_TRACKER_MOTION_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool moving;
    int64_t pending_since_ms;
} MotionDetector;

/* Inicializar una vez al arrancar. */
void motion_init(MotionDetector *ctx);

/* Función para interrumpir la cuenta cuando falla una lectura */
void motion_interrupt(MotionDetector *ctx);

/*
 * Procesar una muestra NEW utilizable.
 *
 * accel_norm:  magnitud de aceleracion con gravedad, en m/s2.
 * gyro_norm:   magnitud del giro ya corregido, en grados/s.
 *                  Las normas deben ser finitas y no negativas.
 * now_ms:      tiempo del kernel, en milisegundos.
 *
 * NO_NEW: no llamar a esta funcion.
 * Error, saturacion o hueco >50 ms: el llamador debe ejecutar
 * motion_interrupt antes de comenzar otra secuencia.
 *
 * Devuelve el estado de movimiento confirmado.
 */
bool motion_update(MotionDetector *ctx,
                   float accel_norm,
                   float gyro_norm,
                   int64_t now_ms);

#endif