#ifndef DRONE_TRACKER_APP_OUTPUT_H
#define DRONE_TRACKER_APP_OUTPUT_H

#include "model.h"

/* LPUART1 (ST-LINK VCP) a 115200 8N1 con TX por interrupcion. */
void output_init(void);

/* true mientras la IRQ transmite el ultimo mensaje. */
bool output_busy(void);

/*
 * Llamar en cada vuelta de main: aborta una transmision que supera su plazo.
 * Retorna true si el enlace esta en fallo (hasta el siguiente envio completo).
 */
bool output_service(int64_t now_ms);

/*
 * Formatea y empieza a enviar el mensaje del modo: trama SIMU o informe TEST/NORMAL.
 * Retorna false si la UART esta ocupada o el formato fallo (no se envia nada).
 */
bool output_report(AppMode mode, const AcquisitionSnapshot *acq, const GpsSnapshot *gps,
                   const TrackerStatus *status, int64_t now_ms, uint32_t reset_cause,
                   int watchdog_error);

#endif
