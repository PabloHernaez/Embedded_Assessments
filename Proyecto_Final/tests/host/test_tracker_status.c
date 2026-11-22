#include <stdio.h>

#include "tracker_status.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Linea %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static AcquisitionSnapshot present_sensors(int64_t now_ms)
{
    const SampleQuality good = { .valid = true, .last_good_ms = now_ms };
    return (AcquisitionSnapshot){
        .imu = { .quality = good },
        .light = { .quality = good },
        .environment = { .quality = good },
        .nav = { .valid = true },
    };
}

static int imu_tests(void)
{
    TrackerHealth health = { .healthy_since_ms = -1 };
    AcquisitionSnapshot acq = present_sensors(0);
    GpsSnapshot gps = { .last_nmea_ms = 0, .last_fix_ms = 0, .fix_valid = true };

    TrackerStatus s = tracker_status_from_snapshots(&health, &acq, &gps, 0, false);
    CHECK(s.state == TRACKER_STATE_IDLE && s.flags == 0x00C0U);

    acq = present_sensors(9999);
    gps.last_nmea_ms = 9999;
    acq.imu.quality = (SampleQuality){ .last_good_ms = -1 };
    acq.imu.calibration_pending = true;
    s = tracker_status_from_snapshots(&health, &acq, &gps, 9999, false);
    CHECK(s.state == TRACKER_STATE_IDLE);
    CHECK(s.flags & TRACKER_FLAG_IMU_INVALID);
    CHECK(s.flags & TRACKER_FLAG_CALIBRATION_PENDING);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 10000, false).state ==
          TRACKER_STATE_PROBLEM);

    health = (TrackerHealth){ .healthy_since_ms = -1 };
    acq = present_sensors(10249);
    acq.imu.quality = (SampleQuality){ .last_good_ms = 10000 };
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 10249, false).state ==
          TRACKER_STATE_IDLE);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 10250, false).state ==
          TRACKER_STATE_PROBLEM);

    health = (TrackerHealth){ .healthy_since_ms = -1 };
    acq = present_sensors(11200);
    acq.nav.valid = false;
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 11200, false).state ==
          TRACKER_STATE_PROBLEM);
    return 0;
}

static int gps_tests(void)
{
    TrackerHealth health = { .healthy_since_ms = -1 };
    GpsSnapshot gps = { .last_nmea_ms = -1, .last_fix_ms = -1 };
    AcquisitionSnapshot acq = present_sensors(29999);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 29999, false).state ==
          TRACKER_STATE_IDLE);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 30000, false).state ==
          TRACKER_STATE_PROBLEM);

    health = (TrackerHealth){ .healthy_since_ms = -1 };
    acq = present_sensors(29999);
    acq.moving = true;
    gps.last_nmea_ms = 20000;
    TrackerStatus s = tracker_status_from_snapshots(&health, &acq, &gps, 29999, false);
    CHECK(s.state == TRACKER_STATE_FLYING && (s.flags & TRACKER_FLAG_GPS_INVALID));
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 30000, false).state ==
          TRACKER_STATE_PROBLEM);

    gps.last_nmea_ms = 30100;
    acq = present_sensors(30100);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 30100, false).state ==
          TRACKER_STATE_PROBLEM);
    acq = present_sensors(32099);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 32099, false).state ==
          TRACKER_STATE_PROBLEM);

    /* Una nueva causa reinicia la espera de recuperacion. */
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 32100, true).state ==
          TRACKER_STATE_PROBLEM);
    acq = present_sensors(32200);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 32200, false).state ==
          TRACKER_STATE_PROBLEM);
    acq = present_sensors(34200);
    s = tracker_status_from_snapshots(&health, &acq, &gps, 34200, false);
    CHECK(s.state == TRACKER_STATE_IDLE && (s.flags & TRACKER_FLAG_GPS_INVALID));

    gps.last_fix_ms = 34200;
    gps.last_nmea_ms = 34200;
    gps.fix_valid = true;
    acq = present_sensors(37199);
    s = tracker_status_from_snapshots(&health, &acq, &gps, 37199, false);
    CHECK(!(s.flags & TRACKER_FLAG_GPS_INVALID));
    s = tracker_status_from_snapshots(&health, &acq, &gps, 37200, false);
    CHECK(s.state == TRACKER_STATE_IDLE && (s.flags & TRACKER_FLAG_GPS_INVALID));
    return 0;
}

static int slow_sensor_tests(void)
{
    TrackerHealth health = { .healthy_since_ms = -1 };
    GpsSnapshot gps = { .last_nmea_ms = 10000, .last_fix_ms = -1 };
    AcquisitionSnapshot acq = present_sensors(6000);
    acq.environment.quality.last_good_ms = 0;
    acq.light.clipped = true;
    gps.last_nmea_ms = 0;
    TrackerStatus s = tracker_status_from_snapshots(&health, &acq, &gps, 6000, false);
    CHECK(s.state == TRACKER_STATE_IDLE);
    CHECK(s.flags & TRACKER_FLAG_ENV_INVALID);
    CHECK(s.flags & TRACKER_FLAG_LIGHT_WARNING);

    acq = present_sensors(9999);
    acq.environment.quality.last_good_ms = 0;
    gps.last_nmea_ms = 9999;
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 9999, false).state ==
          TRACKER_STATE_IDLE);
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 10000, false).state ==
          TRACKER_STATE_PROBLEM);

    health = (TrackerHealth){ .healthy_since_ms = -1 };
    gps.last_nmea_ms = 30000;
    acq = present_sensors(30000);
    acq.light.clipped = true;
    s = tracker_status_from_snapshots(&health, &acq, &gps, 30000, false);
    CHECK(s.state == TRACKER_STATE_IDLE && (s.flags & TRACKER_FLAG_LIGHT_WARNING));

    acq.light.quality = (SampleQuality){.last_good_ms = -1};
    CHECK(tracker_status_from_snapshots(&health, &acq, &gps, 30000, false).state ==
          TRACKER_STATE_PROBLEM);
    return 0;
}

int main(void)
{
    if (imu_tests() || gps_tests() || slow_sensor_tests()) {
        return 1;
    }
    puts("Salud: edades, gracias, flags y recuperacion OK");
    return 0;
}
