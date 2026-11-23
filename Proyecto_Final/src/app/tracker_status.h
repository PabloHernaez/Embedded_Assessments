#ifndef DRONE_TRACKER_APP_TRACKER_STATUS_H
#define DRONE_TRACKER_APP_TRACKER_STATUS_H

#include "model.h"

/* Histeresis de PROBLEM: la guarda main entre llamadas. */
typedef struct {
    bool problem;
    int64_t healthy_since_ms;
} TrackerHealth;

/* Flags de la trama SIMU y estado IDLE/FLYING/PROBLEM a partir de las copias. */
TrackerStatus tracker_status_from_snapshots(TrackerHealth *health,
                                            const AcquisitionSnapshot *acq,
                                            const GpsSnapshot *gps, int64_t now_ms);

#endif
