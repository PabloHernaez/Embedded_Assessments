#ifndef DRONE_TRACKER_SIMU_FRAME_H
#define DRONE_TRACKER_SIMU_FRAME_H

#include <stdint.h>

#include "model.h"

/* 221 bytes en el cable, CRLF incluido; el buffer reserva uno mas para '\0'. */
#define SIMU_FRAME_WIRE_SIZE   221U
#define SIMU_FRAME_BUFFER_SIZE (SIMU_FRAME_WIRE_SIZE + 1U)

/* Copias del llamador. ROLL/PITCH/YAW salen de acquisition->nav.relative_q. */
typedef struct {
    const AcquisitionSnapshot *acquisition;
    const GpsSnapshot *gps;
    const TrackerStatus *status;
    uint16_t seq;
    uint64_t uptime_ms;
} SimuFrameInput;

/*
 * Escribe una trama $SIMU1 completa en buf. Funcion pura.
 * Cada valor se redondea y se acota a su campo; NaN/Inf se escriben como cero.
 * Retorna 0, o -EIO si la trama no mide 221 bytes (uptime de mas de 10 cifras).
 */
int simu_frame_encode(const SimuFrameInput *in, char buf[SIMU_FRAME_BUFFER_SIZE]);

#endif
