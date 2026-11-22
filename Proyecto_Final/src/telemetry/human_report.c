#include "human_report.h"

#include "attitude.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} Report;

static void append(Report *r, const char *format, ...)
{
    va_list args;

    if (r->overflow) {
        return;
    }
    va_start(args, format);
    const int n = vsnprintf(r->buf + r->len, r->cap - r->len, format, args);
    va_end(args);

    if (n < 0 || (size_t)n >= r->cap - r->len) {
        r->overflow = true;
    } else {
        r->len += (size_t)n;
    }
}

/* Texto de un valor con sus decimales, o "N/D" si no hay dato. */
typedef struct {
    char text[24];
} Number;

static Number number(double value, bool available, int decimals)
{
    Number n = { "N/D" };

    if (available && isfinite(value)) {
        snprintf(n.text, sizeof(n.text), "%.*f", decimals, value);
    }
    return n;
}

static void append_vector(Report *r, const char *label, Vec3f v, const char *unit, bool ok)
{
    append(r, "%s=(%s,%s,%s) %s\r\n", label, number(v.x, ok, 2).text,
           number(v.y, ok, 2).text, number(v.z, ok, 2).text, unit);
}

static void append_gps(Report *r, const GpsSnapshot *gps, const TrackerStatus *status,
                       uint64_t now_ms)
{
    const bool have_position = gps->last_fix_ms >= 0;
    const bool fix = (status->flags & TRACKER_FLAG_GPS_INVALID) == 0U;

    append(r, "GPS fix=%u SAT=%u lat=%s lon=%s alt=%s m\r\n", fix ? 1U : 0U,
           fix ? (unsigned int)gps->satellites : 0U,
           number(gps->lat_udeg / 1e6, have_position, 6).text,
           number(gps->lon_udeg / 1e6, have_position, 6).text,
           number(gps->alt_mm / 1e3, have_position, 1).text);

    if (gps->last_nmea_ms >= 0) {
        append(r, "NMEA edad=%" PRIu64 " ms ", now_ms - (uint64_t)gps->last_nmea_ms);
    } else {
        append(r, "NMEA edad=N/D ");
    }
    if (gps->utc_valid) {
        const uint32_t s = gps->utc_ms_of_day / 1000U;
        append(r, "UTC=%02u:%02u:%02u\r\n", (unsigned int)(s / 3600U),
               (unsigned int)(s / 60U % 60U), (unsigned int)(s % 60U));
    } else {
        append(r, "UTC=N/D\r\n");
    }
}

static void append_stats(Report *r, const StatsResult *stats)
{
    static const char *const labels[STATS_METRIC_COUNT] = {
        [STATS_LIGHT] = "Luz (%)",
        [STATS_TEMP] = "Temp (C)",
        [STATS_RH] = "HR (%)",
        [STATS_ACCEL_NORM] = "|A| (m/s2)",
        [STATS_GYRO_NORM] = "|G| (deg/s)",
    };

    if (!stats->ready) {
        append(r, "Estadisticas 30 s: pendiente\r\n");
        return;
    }
    append(r, "Ventana [%" PRId64 ",%" PRId64 "] ms (media/min/max/n)\r\n",
           stats->begin_ms, stats->end_ms);
    for (int i = 0; i < STATS_METRIC_COUNT; ++i) {
        const MetricStats *m = &stats->metric[i];
        const bool ok = m->count > 0U;
        append(r, "%s: %s/%s/%s/%" PRIu32 "\r\n", labels[i], number(m->mean, ok, 2).text,
               number(m->min, ok, 2).text, number(m->max, ok, 2).text, m->count);
    }
}

int human_report_format(AppMode mode, const HumanReportInput *in, char *buf, size_t cap)
{
    static const char *const states[] = { "IDLE", "FLYING", "PROBLEM" };
    const AcquisitionSnapshot *a = in->acquisition;
    const bool imu_seen = a->imu.quality.last_good_ms >= 0;
    const bool light_seen = a->light.quality.last_good_ms >= 0;
    const bool env_seen = a->environment.quality.last_good_ms >= 0;
    Report r = { .buf = buf, .cap = cap };

    append(&r, "%s TMS=%" PRIu64 " STATE=%s FLAGS=%04X\r\n",
           mode == APP_MODE_TEST ? "TEST" : "NORMAL", in->uptime_ms,
           states[in->status->state], (unsigned int)in->status->flags);
    append(&r, "IMU=%s CAL=%s\r\n", a->imu.quality.valid ? "OK" : "invalida",
           a->imu.calibration_pending ? "pendiente" : "lista");
    append_vector(&r, "A", a->imu.accel_mps2, "m/s2", imu_seen);
    append_vector(&r, "G", a->imu.gyro_dps, "deg/s", imu_seen);
    append_vector(&r, "RPY", euler_degrees(a->nav.relative_q), "deg", a->nav.valid);
    append_vector(&r, "XYZ", a->nav.position_m, "m", a->nav.valid);
    append_vector(&r, "VXYZ", a->nav.velocity_mps, "m/s", a->nav.valid);
    append_gps(&r, in->gps, in->status, in->uptime_ms);
    append(&r, "Luz=%s %% [%s] Temp=%s C HR=%s %% [%s]\r\n",
           number(a->light.light_pct, light_seen, 1).text,
           a->light.quality.valid && !a->light.clipped ? "OK" : "aviso",
           number(a->environment.temperature_c, env_seen, 1).text,
           number(a->environment.humidity_pct, env_seen, 1).text,
           a->environment.quality.valid ? "OK" : "invalido");

    if (mode == APP_MODE_NORMAL) {
        append_stats(&r, &a->stats);
    } else {
        append(&r, "RESET=0x%08" PRIX32 "\r\n", in->reset_cause);
        if (in->watchdog_error == 0) {
            append(&r, "WDT=ACTIVO\r\n");
        } else {
            append(&r, "WDT=ERROR %d\r\n", in->watchdog_error);
        }
    }

    return r.overflow ? -EMSGSIZE : (int)r.len;
}
