#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "human_report.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Fallo linea %d: %s\n%s\n", __LINE__, #x, output); return 1; } } while (0)

int main(void)
{
    char output[HUMAN_REPORT_BUFFER_SIZE] = "";
    AcquisitionSnapshot acq = {
        .imu = { .quality = { .valid = true, .last_good_ms = 9990 },
                 .accel_mps2 = {0.0f, -0.12f, 9.81f} },
        .nav = { .valid = true, .relative_q = { .w = 1.0f } },
        .light.quality.last_good_ms = -1,
        .environment.quality.last_good_ms = -1,
    };
    GpsSnapshot gps = {
        .lat_udeg = 40395403, .lon_udeg = -3669052, .alt_mm = 600800,
        .satellites = 4U, .utc_ms_of_day = 43201000U, .utc_valid = true,
        .fix_valid = true, .last_nmea_ms = 9950, .last_fix_ms = 9900,
    };
    TrackerStatus status = { .state = TRACKER_STATE_IDLE, .flags = 0x00C0 };
    HumanReportInput in = {
        .acquisition = &acq, .gps = &gps, .status = &status,
        .uptime_ms = 10000, .reset_cause = UINT32_MAX,
    };

    int len = human_report_format(APP_MODE_TEST, &in, output, sizeof(output));
    CHECK(len > 0 && (size_t)len == strlen(output));
    CHECK(strstr(output, "TEST TMS=10000 STATE=IDLE FLAGS=00C0\r\n") != NULL);
    CHECK(strstr(output, "A=(0.00,-0.12,9.81) m/s2") != NULL);
    CHECK(strstr(output, "RPY=(0.00,0.00,0.00) deg") != NULL);
    CHECK(strstr(output, "GPS fix=1 SAT=4 lat=40.395403 lon=-3.669052 alt=600.8 m") != NULL);
    CHECK(strstr(output, "NMEA edad=50 ms UTC=12:00:01") != NULL);
    CHECK(strstr(output, "Luz=N/D % [aviso] Temp=N/D C HR=N/D % [invalido]") != NULL);
    CHECK(strstr(output, "RESET=0xFFFFFFFF") != NULL);
    CHECK(strstr(output, "WDT=ACTIVO") != NULL);
    in.watchdog_error = -ENODEV;
    CHECK(human_report_format(APP_MODE_TEST, &in, output, sizeof(output)) > 0);
    CHECK(strstr(output, "WDT=ERROR -") != NULL);

    /* Sin fix efectivo (flag GPS_INVALID): fix=0 y SAT=0, posicion retenida. */
    status.flags |= TRACKER_FLAG_GPS_INVALID;
    CHECK(human_report_format(APP_MODE_TEST, &in, output, sizeof(output)) > 0);
    CHECK(strstr(output, "GPS fix=0 SAT=0 lat=40.395403") != NULL);

    CHECK(human_report_format(APP_MODE_NORMAL, &in, output, sizeof(output)) > 0);
    CHECK(strstr(output, "Estadisticas 30 s: pendiente") != NULL);
    CHECK(strstr(output, "RESET=") == NULL);
    acq.stats.ready = true;
    acq.stats.end_ms = 30000;
    acq.stats.metric[STATS_TEMP] = (MetricStats){ .mean = 20.5f, .min = 20.0f, .max = 21.0f, .count = 2 };
    CHECK(human_report_format(APP_MODE_NORMAL, &in, output, sizeof(output)) > 0);
    CHECK(strstr(output, "Ventana [0,30000] ms") != NULL);
    CHECK(strstr(output, "Temp (C): 20.50/20.00/21.00/2") != NULL);
    CHECK(strstr(output, "Luz (%): N/D/N/D/N/D/0") != NULL);

    /* Sin espacio no hay informe parcial. */
    CHECK(human_report_format(APP_MODE_NORMAL, &in, output, 8U) == -EMSGSIZE);

    puts("Reportes TEST/NORMAL: OK");
    return 0;
}
