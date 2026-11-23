#include <stdio.h>

#include "tracker_status.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Linea %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

/* Todos los sensores con un dato bueno de ahora mismo. */
static AcquisitionSnapshot present_sensors(int64_t now_ms)
{
    const SampleQuality good = { .valid = true, .last_good_ms = now_ms };
    return (AcquisitionSnapshot){
        .imu = { .quality = good },
        .light = { .quality = good },
        .environment = { .quality = good },
    };
}

static TrackerState state_at(TrackerHealth *h, const AcquisitionSnapshot *a,
                             const GpsSnapshot *g, int64_t now_ms)
{
    return tracker_status_from_snapshots(h, a, g, now_ms).state;
}

static int imu_tests(void)
{
    TrackerHealth health = { .healthy_since_ms = -1 };
    AcquisitionSnapshot acq = present_sensors(0);
    GpsSnapshot gps = { .last_nmea_ms = 0, .last_fix_ms = 0, .fix_valid = true };

    TrackerStatus s = tracker_status_from_snapshots(&health, &acq, &gps, 0);
    CHECK(s.state == TRACKER_STATE_IDLE && s.flags == 0x00C0U);

    /* Sin IMU ni calibracion: flags ya, PROBLEM solo tras la gracia de 10 s. */
    acq = present_sensors(9999);
    gps.last_nmea_ms = 9999;
    acq.imu.quality = (SampleQuality){ .last_good_ms = -1 };
    acq.imu.calibration_pending = true;
    s = tracker_status_from_snapshots(&health, &acq, &gps, 9999);
    CHECK(s.state == TRACKER_STATE_IDLE);
    CHECK((s.flags & TRACKER_FLAG_IMU_INVALID) && (s.flags & TRACKER_FLAG_CALIBRATION_PENDING));
    CHECK(state_at(&health, &acq, &gps, 10000) == TRACKER_STATE_PROBLEM);

    /* 250 ms sin muestra de IMU: PROBLEM. */
    health = (TrackerHealth){ .healthy_since_ms = -1 };
    acq = present_sensors(10249);
    acq.imu.quality = (SampleQuality){ .valid = true, .last_good_ms = 10000 };
    CHECK(state_at(&health, &acq, &gps, 10249) == TRACKER_STATE_IDLE);
    CHECK(state_at(&health, &acq, &gps, 10250) == TRACKER_STATE_PROBLEM);
    return 0;
}

static int gps_tests(void)
{
    TrackerHealth health = { .healthy_since_ms = -1 };
    GpsSnapshot gps = { .last_nmea_ms = -1, .last_fix_ms = -1 };
    AcquisitionSnapshot acq = present_sensors(9999);

    /* Nunca hubo NMEA: enlace caido a los 10 s desde el arranque. */
    CHECK(state_at(&health, &acq, &gps, 9999) == TRACKER_STATE_IDLE);
    CHECK(state_at(&health, &acq, &gps, 10000) == TRACKER_STATE_PROBLEM);

    /* NMEA sin fix: flag GPS_INVALID, pero nunca PROBLEM (se vuela igual). */
    health = (TrackerHealth){ .healthy_since_ms = -1 };
    acq = present_sensors(29999);
    acq.moving = true;
    gps.last_nmea_ms = 20000;
    TrackerStatus s = tracker_status_from_snapshots(&health, &acq, &gps, 29999);
    CHECK(s.state == TRACKER_STATE_FLYING && (s.flags & TRACKER_FLAG_GPS_INVALID));

    /* 10 s sin NMEA: PROBLEM; vuelve a IDLE tras 2 s sin causas. */
    acq = present_sensors(30000);
    CHECK(state_at(&health, &acq, &gps, 30000) == TRACKER_STATE_PROBLEM);
    gps.last_nmea_ms = 30100;
    acq = present_sensors(30100);
    CHECK(state_at(&health, &acq, &gps, 30100) == TRACKER_STATE_PROBLEM);
    acq = present_sensors(32099);
    CHECK(state_at(&health, &acq, &gps, 32099) == TRACKER_STATE_PROBLEM);
    acq = present_sensors(32100);
    s = tracker_status_from_snapshots(&health, &acq, &gps, 32100);
    CHECK(s.state == TRACKER_STATE_IDLE && (s.flags & TRACKER_FLAG_GPS_INVALID));

    /* Fix valido: sin flag; caduca a los 3 s. */
    gps.last_fix_ms = 34200;
    gps.last_nmea_ms = 34200;
    gps.fix_valid = true;
    acq = present_sensors(37199);
    s = tracker_status_from_snapshots(&health, &acq, &gps, 37199);
    CHECK(!(s.flags & TRACKER_FLAG_GPS_INVALID));
    s = tracker_status_from_snapshots(&health, &acq, &gps, 37200);
    CHECK(s.state == TRACKER_STATE_IDLE && (s.flags & TRACKER_FLAG_GPS_INVALID));
    return 0;
}

static int slow_sensor_tests(void)
{
    TrackerHealth health = { .healthy_since_ms = -1 };
    GpsSnapshot gps = { .last_nmea_ms = 6000, .last_fix_ms = -1 };

    /* Dato invalido o recortado: flags, sin PROBLEM. */
    AcquisitionSnapshot acq = present_sensors(6000);
    acq.environment.quality.valid = false;
    acq.light.clipped = true;
    TrackerStatus s = tracker_status_from_snapshots(&health, &acq, &gps, 6000);
    CHECK(s.state == TRACKER_STATE_IDLE);
    CHECK((s.flags & TRACKER_FLAG_ENV_INVALID) && (s.flags & TRACKER_FLAG_LIGHT_WARNING));

    /* 10 s sin un dato bueno: PROBLEM. */
    acq = present_sensors(9999);
    acq.environment.quality = (SampleQuality){ .last_good_ms = 0 };
    gps.last_nmea_ms = 9999;
    CHECK(state_at(&health, &acq, &gps, 9999) == TRACKER_STATE_IDLE);
    CHECK(state_at(&health, &acq, &gps, 10000) == TRACKER_STATE_PROBLEM);

    /* Luz que nunca dio dato: cuenta desde el arranque. */
    health = (TrackerHealth){ .healthy_since_ms = -1 };
    gps.last_nmea_ms = 30000;
    acq = present_sensors(30000);
    acq.light.quality = (SampleQuality){ .last_good_ms = -1 };
    CHECK(state_at(&health, &acq, &gps, 30000) == TRACKER_STATE_PROBLEM);
    return 0;
}

static int impact_tests(void)
{
    TrackerHealth health = { .healthy_since_ms = -1 };
    GpsSnapshot gps = { .last_nmea_ms = 20000, .last_fix_ms = 20000, .fix_valid = true };
    AcquisitionSnapshot acq = present_sensors(20000);

    /* 0 = ningun golpe: ni flag ni PROBLEM, tambien en el arranque. */
    TrackerStatus s = tracker_status_from_snapshots(&health, &acq, &gps, 20000);
    CHECK(s.state == TRACKER_STATE_IDLE && !(s.flags & TRACKER_FLAG_IMPACT));
    AcquisitionSnapshot boot = present_sensors(100);
    GpsSnapshot boot_gps = { .last_nmea_ms = 100, .last_fix_ms = 100, .fix_valid = true };
    CHECK(state_at(&health, &boot, &boot_gps, 100) == TRACKER_STATE_IDLE);

    /* Golpe: flag 0x0010 y PROBLEM durante 5 s, aunque el dron este volando. */
    acq.imu.last_impact_ms = 20000;
    acq.moving = true;
    s = tracker_status_from_snapshots(&health, &acq, &gps, 20000);
    CHECK(s.state == TRACKER_STATE_PROBLEM && (s.flags & TRACKER_FLAG_IMPACT));
    acq = present_sensors(24999);
    acq.imu.last_impact_ms = 20000;
    gps.last_nmea_ms = gps.last_fix_ms = 24999;
    s = tracker_status_from_snapshots(&health, &acq, &gps, 24999);
    CHECK(s.state == TRACKER_STATE_PROBLEM && (s.flags & TRACKER_FLAG_IMPACT));

    /* A los 5 s se quita el flag; PROBLEM sigue 2 s mas (recuperacion comun). */
    acq = present_sensors(25000);
    acq.imu.last_impact_ms = 20000;
    gps.last_nmea_ms = gps.last_fix_ms = 25000;
    s = tracker_status_from_snapshots(&health, &acq, &gps, 25000);
    CHECK(s.state == TRACKER_STATE_PROBLEM && !(s.flags & TRACKER_FLAG_IMPACT));
    acq = present_sensors(27000);
    acq.imu.last_impact_ms = 20000;
    gps.last_nmea_ms = gps.last_fix_ms = 27000;
    CHECK(state_at(&health, &acq, &gps, 27000) == TRACKER_STATE_IDLE);
    return 0;
}

int main(void)
{
    if (imu_tests() || gps_tests() || slow_sensor_tests() || impact_tests()) {
        return 1;
    }
    puts("Salud: edades, gracia, flags y recuperacion OK");
    return 0;
}
