#ifndef DRONE_TRACKER_HUMAN_REPORT_H
#define DRONE_TRACKER_HUMAN_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "model.h"

#define HUMAN_REPORT_BUFFER_SIZE 1024U

/* Copias del llamador. ROLL/PITCH/YAW salen de acquisition->nav.q. */
typedef struct {
    const AcquisitionSnapshot *acquisition;
    const GpsSnapshot *gps;
    const TrackerStatus *status;
    uint64_t uptime_ms;
    uint32_t reset_cause;   /* hwinfo, solo en TEST */
    int watchdog_error;     /* 0 si el watchdog esta activo, solo en TEST */
} HumanReportInput;

/*
 * Informe legible de TEST (cada 2 s) o NORMAL (cada 5 s, con estadisticas).
 * Funcion pura. Retorna la longitud escrita (sin '\0'), o -EMSGSIZE si no
 * cabe en cap; en ese caso no se debe transmitir nada.
 */
int human_report_format(AppMode mode, const HumanReportInput *in, char *buf, size_t cap);

#endif
