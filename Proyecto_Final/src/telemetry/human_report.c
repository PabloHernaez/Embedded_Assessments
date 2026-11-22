#include "human_report.h"

#include "attitude.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    char *data;
    size_t capacity;
    size_t used;
    int error;
} ReportBuffer;

typedef struct {
    char text[20];
} ReportNumber;

static void append(ReportBuffer *report, const char *format, ...)
{
    if (report->error != 0) {
        return;
    }

    const size_t remaining = report->capacity - report->used;
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(report->data + report->used, remaining, format, args);
    va_end(args);

    if (n < 0) {
        report->error = -EIO;
    } else if ((size_t)n >= remaining) {
        report->error = -EMSGSIZE;
    } else {
        report->used += (size_t)n;
    }
}

static ReportNumber number(double value, bool available, unsigned int decimals)
{
    ReportNumber result = {"N/D"};
    const double magnitude = fabs(value);
    if (!available || !isfinite(value) || magnitude > 999999999.0 || decimals > 6U) {
        return result;
    }

    uint64_t scale = 1U;
    for (unsigned int i = 0U; i < decimals; ++i) {
        scale *= 10U;
    }
    const uint64_t scaled = (uint64_t)(magnitude * (double)scale + 0.5);
    const char *sign = (value < 0.0 && scaled != 0U) ? "-" : "";
    const int written = snprintf(result.text, sizeof(result.text), "%s%" PRIu32,
                                 sign, (uint32_t)(scaled / scale));
    if (written < 0 || (size_t)written + decimals + 2U > sizeof(result.text)) {
        return (ReportNumber){"N/D"};
    }

    const size_t point = (size_t)written;
    result.text[point] = '.';
    uint32_t fraction = (uint32_t)(scaled % scale);
    for (size_t i = point + decimals; i > point; --i) {
        result.text[i] = (char)('0' + fraction % 10U);
        fraction /= 10U;
    }
    result.text[point + decimals + 1U] = '\0';
    return result;
}

static void append_vector(ReportBuffer *report, const char *label, Vec3f value,
                          const char *unit, bool available)
{
    append(report, "%s=(%s,%s,%s) %s\r\n", label,
           number(value.x, available, 2U).text,
           number(value.y, available, 2U).text,
           number(value.z, available, 2U).text, unit);
}

static void append_gps(ReportBuffer *report, const HumanReportInput *in)
{
    const GpsSnapshot *gps = in->gps;
    const bool have_position = gps->last_fix_ms >= 0;
    const bool fix = have_position && gps->fix_valid &&
        !(in->status->flags & TRACKER_FLAG_GPS_INVALID);

    append(report, "GPS fix=%u SAT=%u lat=%s lon=%s alt=%s m\r\n",
           (unsigned int)fix, fix ? (unsigned int)gps->satellites : 0U,
           number(gps->lat_udeg / 1000000.0, have_position, 6U).text,
           number(gps->lon_udeg / 1000000.0, have_position, 6U).text,
           number(gps->alt_mm / 1000.0, have_position, 1U).text);

    if (gps->last_nmea_ms >= 0 && (uint64_t)gps->last_nmea_ms <= in->uptime_ms) {
        append(report, "NMEA edad=%" PRIu64 " ms ",
               in->uptime_ms - (uint64_t)gps->last_nmea_ms);
    } else {
        append(report, "NMEA edad=N/D ");
    }
    if (gps->utc_valid && gps->utc_ms_of_day < 86400000U) {
        const uint32_t sec = gps->utc_ms_of_day / 1000U;
        append(report, "UTC=%02u:%02u:%02u\r\n", (unsigned int)(sec / 3600U),
               (unsigned int)((sec / 60U) % 60U), (unsigned int)(sec % 60U));
    } else {
        append(report, "UTC=N/D\r\n");
    }
}

static void append_stats(ReportBuffer *report, const StatsResult *stats)
{
    static const char *const labels[] = {
        [STATS_LIGHT] = "Luz (%)",
        [STATS_TEMP] = "Temp (C)",
        [STATS_RH] = "HR (%)",
        [STATS_ACCEL_NORM] = "|A| (m/s2)",
        [STATS_GYRO_NORM] = "|G| (deg/s)",
    };
    if (!stats->ready) {
        append(report, "Estadisticas 30 s: pendiente\r\n");
        return;
    }

    append(report, "Ventana [%" PRId64 ",%" PRId64 "] ms (media/min/max/n)\r\n",
           stats->begin_ms, stats->end_ms);
    for (size_t i = 0U; i < STATS_METRIC_COUNT; ++i) {
        const MetricStats *metric = &stats->metric[i];
        const bool have_samples = metric->count != 0U;
        append(report, "%s: %s/%s/%s/%" PRIu32 "\r\n", labels[i],
               number(metric->mean, have_samples, 2U).text,
               number(metric->min, have_samples, 2U).text,
               number(metric->max, have_samples, 2U).text, metric->count);
    }
}

int human_report_format(AppMode mode, const HumanReportInput *in,
                        char *buf, size_t cap, size_t *len)
{
    static const char *const states[] = {
        [TRACKER_STATE_IDLE] = "IDLE",
        [TRACKER_STATE_FLYING] = "FLYING",
        [TRACKER_STATE_PROBLEM] = "PROBLEM",
    };

    *len = 0U;
    if (mode != APP_MODE_TEST && mode != APP_MODE_NORMAL) {
        return -EINVAL;
    }
    if (cap == 0U) {
        return -EMSGSIZE;
    }
    const unsigned int state = (unsigned int)in->status->state;
    if (state >= sizeof(states) / sizeof(states[0])) {
        return -EINVAL;
    }

    ReportBuffer report = { .data = buf, .capacity = cap };
    const AcquisitionSnapshot *acq = in->acquisition;
    uint16_t replaced_angles = 0U;
    const Vec3f angles = euler_degrees(acq->nav.relative_q, &replaced_angles);

    append(&report, "%s TMS=%" PRIu64 " STATE=%s FLAGS=%04X\r\n",
           mode == APP_MODE_TEST ? "TEST" : "NORMAL", in->uptime_ms,
           states[state], (unsigned int)in->status->flags);
    append(&report, "IMU=%s CAL=%s\r\n", acq->imu.quality.valid ? "OK" : "invalida",
           acq->imu.calibration_pending ? "pendiente" : "lista");
    append_vector(&report, "A", acq->imu.accel_mps2, "m/s2",
                  acq->imu.quality.last_good_ms >= 0);
    append_vector(&report, "G", acq->imu.gyro_dps, "deg/s",
                  acq->imu.quality.last_good_ms >= 0);
    append_vector(&report, "RPY", angles, "deg", acq->nav.valid && replaced_angles == 0U);
    append_vector(&report, "XYZ", acq->nav.position_m, "m", acq->nav.valid);
    append_vector(&report, "VXYZ", acq->nav.velocity_mps, "m/s", acq->nav.valid);
    append_gps(&report, in);
    append(&report, "Luz=%s %% [%s] Temp=%s C HR=%s %% [%s]\r\n",
           number(acq->light.light_pct, acq->light.quality.last_good_ms >= 0, 1U).text,
           acq->light.quality.valid && !acq->light.clipped ? "OK" : "aviso",
           number(acq->environment.temperature_c,
                  acq->environment.quality.last_good_ms >= 0, 1U).text,
           number(acq->environment.humidity_pct,
                  acq->environment.quality.last_good_ms >= 0, 1U).text,
           acq->environment.quality.valid ? "OK" : "invalido");

    if (mode == APP_MODE_NORMAL) {
        append_stats(&report, &acq->stats);
    } else {
        append(&report, "RESET=0x%08" PRIX32 " SIMU_REPLACED_FIELDS=%" PRIu32 "\r\n",
               in->reset_cause, in->simu_replaced_fields);
        append(&report, "TX_OK=%" PRIu32 " TX_ABORT=%" PRIu32 " TX_OMIT=%" PRIu32 "\r\n",
               in->tx_completed, in->tx_aborted, in->tx_omitted);
        if (in->watchdog_error == 0) {
            append(&report, "IWDG=ACTIVO\r\n");
        } else {
            append(&report, "IWDG=ERROR %d\r\n", in->watchdog_error);
        }
    }

    if (report.error == 0) {
        *len = report.used;
    }
    return report.error;
}

