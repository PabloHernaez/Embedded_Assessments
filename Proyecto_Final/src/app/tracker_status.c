#include "tracker_status.h"

#define START_GRACE_MS      10000 /* arranque: tiempo para inicializar y calibrar */
#define IMU_MAX_AGE_MS      250   /* ~12 muestras a 50 Hz */
#define SLOW_SENSOR_MAX_MS  10000 /* luz y ambiente leen cada 2 s */
#define GPS_LINK_MAX_MS     10000 /* el GPS emite NMEA cada segundo, con o sin fix */
#define GPS_FIX_MAX_AGE_MS  3000
#define IMPACT_HOLD_MS      5000  /* un golpe marca 0x0010 y PROBLEM durante 5 s */
#define PROBLEM_RECOVERY_MS 2000  /* sin causas durante 2 s para salir de PROBLEM */

/* Edad de un dato en ms; si nunca lo hubo, se cuenta desde el arranque. */
static int64_t age_ms(int64_t now_ms, int64_t last_ms)
{
    return now_ms - (last_ms < 0 ? 0 : last_ms);
}

TrackerStatus tracker_status_from_snapshots(TrackerHealth *health,
                                            const AcquisitionSnapshot *acq,
                                            const GpsSnapshot *gps, int64_t now_ms)
{
    const int64_t imu_age = age_ms(now_ms, acq->imu.quality.last_good_ms);
    const bool impact = acq->imu.last_impact_ms > 0 &&
                        now_ms - acq->imu.last_impact_ms < IMPACT_HOLD_MS;
    TrackerStatus status = {
        /* Posicion solo inercial y yaw sin referencia absoluta: siempre. */
        .flags = TRACKER_FLAG_INERTIAL_ONLY | TRACKER_FLAG_YAW_UNRELIABLE,
    };

    if (!acq->imu.quality.valid || acq->imu.range_limited || imu_age >= IMU_MAX_AGE_MS) {
        status.flags |= TRACKER_FLAG_IMU_INVALID;
    }
    if (impact) {
        status.flags |= TRACKER_FLAG_IMPACT;
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
    if (!gps->fix_valid || age_ms(now_ms, gps->last_fix_ms) >= GPS_FIX_MAX_AGE_MS) {
        status.flags |= TRACKER_FLAG_GPS_INVALID; /* sin fix: flag, pero nunca PROBLEM */
    }

    /* PROBLEM: un golpe (accidente) o fallos persistentes. La falta de fix GPS no cuenta. */
    const bool fault = impact ||
        (now_ms >= START_GRACE_MS && (imu_age >= IMU_MAX_AGE_MS || acq->imu.calibration_pending)) ||
        age_ms(now_ms, acq->light.quality.last_good_ms) >= SLOW_SENSOR_MAX_MS ||
        age_ms(now_ms, acq->environment.quality.last_good_ms) >= SLOW_SENSOR_MAX_MS ||
        age_ms(now_ms, gps->last_nmea_ms) >= GPS_LINK_MAX_MS;

    if (fault) {
        health->problem = true;
        health->healthy_since_ms = -1;
    } else if (health->problem) {
        if (health->healthy_since_ms < 0) {
            health->healthy_since_ms = now_ms;
        }
        health->problem = now_ms - health->healthy_since_ms < PROBLEM_RECOVERY_MS;
    }

    status.state = health->problem ? TRACKER_STATE_PROBLEM
                 : acq->moving     ? TRACKER_STATE_FLYING
                                   : TRACKER_STATE_IDLE;
    return status;
}
