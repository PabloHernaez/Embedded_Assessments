#ifndef DRONE_TRACKER_APP_OUTPUT_H
#define DRONE_TRACKER_APP_OUTPUT_H

#include "model.h"

/*
 * LPUART1 (ST-LINK VCP) a 115200 8N1 con TX por interrupcion.
 * reset_cause y watchdog_status solo se muestran en el informe de TEST.
 */
void output_init(uint32_t reset_cause, int watchdog_status);

/* true mientras la IRQ transmite el ultimo mensaje. */
bool output_busy(void);

/* Llamar en cada vuelta: si el envio supera su plazo se aborta, para que la
 * salida nunca se quede ocupada y bloquee el cambio de modo. */
void output_service(int64_t now_ms);

/* Formatea y empieza a enviar el mensaje del modo (trama SIMU o informe
 * TEST/NORMAL). Si la UART sigue ocupada, ese mensaje se omite. */
void output_report(AppMode mode, const AcquisitionSnapshot *acq, const GpsSnapshot *gps,
                   const TrackerStatus *status, int64_t now_ms);

#endif
