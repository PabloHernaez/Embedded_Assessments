#ifndef DRONE_TRACKER_APP_TRACKER_STATUS_H
#define DRONE_TRACKER_APP_TRACKER_STATUS_H

#include "model.h"

TrackerStatus tracker_status_from_snapshots(const AcquisitionSnapshot *acq,
                                            const GpsSnapshot *gps, int64_t now_ms);

#endif

