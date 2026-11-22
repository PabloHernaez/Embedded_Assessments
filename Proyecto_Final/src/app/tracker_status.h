#ifndef DRONE_TRACKER_APP_TRACKER_STATUS_H
#define DRONE_TRACKER_APP_TRACKER_STATUS_H

#include "model.h"

typedef struct {
    bool problem;
    int64_t healthy_since_ms;
} TrackerHealth;

TrackerStatus tracker_status_from_snapshots(TrackerHealth *health,
                                            const AcquisitionSnapshot *acq,
                                            const GpsSnapshot *gps,
                                            int64_t now_ms, bool tx_failed);

#endif
