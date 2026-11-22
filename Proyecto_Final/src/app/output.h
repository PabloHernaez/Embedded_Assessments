#ifndef DRONE_TRACKER_APP_OUTPUT_H
#define DRONE_TRACKER_APP_OUTPUT_H

#include "model.h"

void output_init(void);
bool output_busy(void);
bool output_service(int64_t now_ms);
bool output_report(AppMode mode, const AcquisitionSnapshot *acq,
                   const GpsSnapshot *gps, const TrackerStatus *status,
                   int64_t now_ms, uint32_t reset_cause,
                   uint32_t omitted_reports);

#endif
