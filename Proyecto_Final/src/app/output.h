#ifndef DRONE_TRACKER_APP_OUTPUT_H
#define DRONE_TRACKER_APP_OUTPUT_H

#include "model.h"

/*
 * LPUART1 (ST-LINK VCP) a 115200 8N1 con TX por DMA (API asincrona de UART).
 * reset_cause y watchdog_status solo se muestran en el informe de TEST.
 */
void output_init(uint32_t reset_cause, int watchdog_status);

/* true mientras se transmite el ultimo mensaje. Si un envio supera su plazo,
 * el driver lo aborta: la salida nunca se queda ocupada. */
bool output_busy(void);

/* Formatea y empieza a enviar el mensaje del modo (trama SIMU o informe
 * TEST/NORMAL). Si la UART sigue ocupada, ese mensaje se omite. */
void output_report(AppMode mode, const AcquisitionSnapshot *acq, const GpsSnapshot *gps,
                   const TrackerStatus *status, int64_t now_ms);

#endif
