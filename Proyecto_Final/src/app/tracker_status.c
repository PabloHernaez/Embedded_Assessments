#include "tracker_status.h"

#define GPS_FIX_MAX_AGE_MS 3000

TrackerStatus tracker_status_from_snapshots(const AcquisitionSnapshot *acq,
                                            const GpsSnapshot *gps, int64_t now_ms)
{
    TrackerStatus status = {
        .state = acq->moving ? TRACKER_STATE_FLYING : TRACKER_STATE_IDLE,
        .flags = TRACKER_FLAG_INERTIAL_ONLY | TRACKER_FLAG_YAW_UNRELIABLE,
    };

    if (!gps->fix_valid || gps->last_fix_ms < 0 ||
        now_ms - gps->last_fix_ms >= GPS_FIX_MAX_AGE_MS) {
        status.flags |= TRACKER_FLAG_GPS_INVALID;
    }
    if (!acq->imu.quality.valid || acq->imu.range_limited) {
        status.flags |= TRACKER_FLAG_IMU_INVALID;
    }
    if (acq->imu.calibration_pending) {
        status.flags |= TRACKER_FLAG_CALIBRATION_PENDING;
    }
    if (!acq->light.quality.valid || acq->light.clipped) {
        status.flags |= TRACKER_FLAG_LIGHT_WARNING;
    }
    if (!acq->environment.quality.valid) {
        status.flags |= TRACKER_FLAG_ENV_INVALID;
    }

    return status;
}

