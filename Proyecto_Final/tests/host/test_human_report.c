#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "human_report.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Fallo linea %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    AcquisitionSnapshot acq = {0};
    GpsSnapshot gps = {
        .lat_udeg = 40395403, .lon_udeg = -3669052, .alt_mm = 600800,
        .satellites = 4U, .utc_ms_of_day = 43201000U, .utc_valid = true,
        .fix_valid = true, .last_nmea_ms = 9950, .last_fix_ms = 9900
    };
    TrackerStatus status = { .state = TRACKER_STATE_IDLE, .flags = 0x00C0 };
    HumanReportInput in = {
        .acquisition = &acq, .gps = &gps, .status = &status,
        .uptime_ms = 10000, .reset_cause = UINT32_MAX,
        .tx_completed = 123U, .tx_aborted = 4U, .tx_omitted = 5U
    };
    acq.nav.valid = true;
    acq.nav.relative_q = (Quaternionf){ .w = 1.0f };
    char output[HUMAN_REPORT_BUFFER_SIZE];
    size_t len;

    CHECK(human_report_format(APP_MODE_TEST, &in, output, sizeof(output), &len) == 0);
    CHECK(len == strlen(output));
    CHECK(strstr(output, "TEST TMS=10000 STATE=IDLE FLAGS=00C0") != NULL);
    CHECK(strstr(output, "GPS fix=1 SAT=4 lat=40.395403 lon=-3.669052") != NULL);
    CHECK(strstr(output, "UTC=12:00:01") != NULL);
    CHECK(strstr(output, "RESET=0xFFFFFFFF") != NULL);
    CHECK(strstr(output, "TX_OK=123 TX_ABORT=4 TX_OMIT=5") != NULL);

    CHECK(human_report_format(APP_MODE_NORMAL, &in, output, sizeof(output), &len) == 0);
    CHECK(strstr(output, "Estadisticas 30 s: pendiente") != NULL);
    acq.stats.ready = true;
    acq.stats.end_ms = 30000;
    acq.stats.metric[STATS_TEMP] = (MetricStats){ .mean = 20.5f, .min = 20.0f, .max = 21.0f, .count = 2 };
    CHECK(human_report_format(APP_MODE_NORMAL, &in, output, sizeof(output), &len) == 0);
    CHECK(strstr(output, "Temp (C): 20.50/20.00/21.00/2") != NULL);
    CHECK(strstr(output, "Luz (%): N/D/N/D/N/D/0") != NULL);
    CHECK(human_report_format(APP_MODE_NORMAL, &in, output, 8U, &len) == -EMSGSIZE && len == 0U);
    CHECK(human_report_format(APP_MODE_SIMU, &in, output, sizeof(output), &len) == -EINVAL && len == 0U);
    puts("Reportes TEST/NORMAL: OK");
    return 0;
}

