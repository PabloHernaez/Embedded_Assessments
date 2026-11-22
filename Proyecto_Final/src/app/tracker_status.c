#include "tracker_status.h"

#define GPS_FIX_MAX_AGE_MS 3000
#define GPS_LINK_MAX_AGE_MS 10000
#define GPS_START_GRACE_MS 30000
#define IMU_MAX_AGE_MS 250
#define IMU_START_GRACE_MS 10000
#define SLOW_SENSOR_MAX_AGE_MS 6000
#define SLOW_SENSOR_FAULT_MS 10000
#define PROBLEM_RECOVERY_MS 2000

static bool stale(int64_t now_ms, int64_t last_ms, int64_t limit_ms)
{
    return last_ms < 0 || now_ms - last_ms >= limit_ms;
}

static bool slow_sensor_failed(int64_t now_ms, int64_t last_good_ms)
{
    /* Si no hubo ninguna muestra utilizable, contar desde el arranque. */
    const int64_t reference_ms = last_good_ms < 0 ? 0 : last_good_ms;
    return now_ms - reference_ms >= SLOW_SENSOR_FAULT_MS;
}

TrackerStatus tracker_status_from_snapshots(TrackerHealth *health,
                                            const AcquisitionSnapshot *acq,
                                            const GpsSnapshot *gps,
                                            int64_t now_ms, bool tx_failed)
{
    const bool imu_stale = stale(now_ms, acq->imu.quality.last_good_ms, IMU_MAX_AGE_MS);
    const bool imu_warning = imu_stale || !acq->imu.quality.valid || acq->imu.range_limited;
    const bool light_warning = !acq->light.quality.valid || acq->light.clipped ||
        stale(now_ms, acq->light.quality.last_good_ms, SLOW_SENSOR_MAX_AGE_MS);
    const bool environment_warning = !acq->environment.quality.valid ||
        stale(now_ms, acq->environment.quality.last_good_ms, SLOW_SENSOR_MAX_AGE_MS);
    const bool gps_link_problem = gps->last_nmea_ms < 0
        ? now_ms >= GPS_START_GRACE_MS
        : stale(now_ms, gps->last_nmea_ms, GPS_LINK_MAX_AGE_MS);
    const bool gps_warning = gps_link_problem || !gps->fix_valid ||
        stale(now_ms, gps->last_fix_ms, GPS_FIX_MAX_AGE_MS);

    TrackerStatus status = {
        .flags = TRACKER_FLAG_INERTIAL_ONLY | TRACKER_FLAG_YAW_UNRELIABLE,
    };

    if (gps_warning) {
        status.flags |= TRACKER_FLAG_GPS_INVALID;
    }
    if (imu_warning) {
        status.flags |= TRACKER_FLAG_IMU_INVALID;
    }
    if (acq->imu.calibration_pending) {
        status.flags |= TRACKER_FLAG_CALIBRATION_PENDING;
    }
    if (light_warning) {
        status.flags |= TRACKER_FLAG_LIGHT_WARNING;
    }
    if (environment_warning) {
        status.flags |= TRACKER_FLAG_ENV_INVALID;
    }

    const bool imu_problem = now_ms >= IMU_START_GRACE_MS &&
        (imu_stale || acq->imu.calibration_pending);

    const bool active_fault = imu_problem || gps_link_problem || tx_failed ||
        slow_sensor_failed(now_ms, acq->light.quality.last_good_ms) ||
        slow_sensor_failed(now_ms, acq->environment.quality.last_good_ms);

    if (active_fault) {
        health->problem = true;
        health->healthy_since_ms = -1;
    } else if (health->problem) {
        if (health->healthy_since_ms < 0) {
            health->healthy_since_ms = now_ms;
        }
        if (now_ms - health->healthy_since_ms >= PROBLEM_RECOVERY_MS) {
            health->problem = false;
        }
    }

    if (health->problem) {
        status.state = TRACKER_STATE_PROBLEM;
    } else {
        status.state = acq->moving ? TRACKER_STATE_FLYING : TRACKER_STATE_IDLE;
    }
    return status;
}
