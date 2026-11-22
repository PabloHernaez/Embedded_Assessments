/*
 * Servicio GPS (P1). USART1 a 9600 8N1 (RX en PB7, ver overlay). La ISR junta
 * bytes hasta el LF y entrega cada linea a una cola; un hilo de prioridad 5
 * comprueba el checksum NMEA, decodifica GGA y publica un GpsSnapshot.
 */
#include "gps_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>

#define GPS_LINE_MAX      128 /* NMEA admite 82 caracteres; el resto se descarta */
#define GPS_QUEUE_LINES   4
#define GPS_STACK_SIZE    1536
#define GPS_PRIORITY      5
#define GPS_WAIT_MS       1000 /* sin NMEA el hilo despierta igual para su watchdog */
#define GPS_WDT_MS        3000
#define GGA_FIELDS        15

static const struct device *const gps_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

K_MSGQ_DEFINE(gps_lines, GPS_LINE_MAX, GPS_QUEUE_LINES, 1);
K_MUTEX_DEFINE(gps_lock);

static GpsSnapshot published = {
    .last_nmea_ms = -1,
    .last_fix_ms = -1,
};

void gps_snapshot_get(GpsSnapshot *out)
{
    k_mutex_lock(&gps_lock, K_FOREVER);
    *out = published;
    k_mutex_unlock(&gps_lock);
}

/* ISR de RX: drena la FIFO sin esperar bytes futuros. */
static void gps_isr(const struct device *dev, void *user_data)
{
    static char line[GPS_LINE_MAX];
    static size_t len;
    static bool too_long;
    uint8_t c;

    ARG_UNUSED(user_data);
    (void)uart_irq_update(dev);

    while (uart_irq_rx_ready(dev) && uart_fifo_read(dev, &c, 1) == 1) {
        if (c == '\n') {
            if (!too_long && len > 0) {
                line[len] = '\0';
                (void)k_msgq_put(&gps_lines, line, K_NO_WAIT); /* cola llena: se pierde */
            }
            len = 0;
            too_long = false;
        } else if (c != '\r') {
            if (len < sizeof(line) - 1) {
                line[len++] = (char)c;
            } else {
                too_long = true;
            }
        }
    }
}

/* "$...*HH": XOR de los caracteres entre '$' y '*'. */
static bool checksum_ok(const char *s)
{
    const char *star = strchr(s, '*');
    char *end;
    uint8_t sum = 0;

    if (s[0] != '$' || star == NULL) {
        return false;
    }
    for (const char *p = s + 1; p < star; p++) {
        sum ^= (uint8_t)*p;
    }
    const unsigned long expected = strtoul(star + 1, &end, 16);
    return end == star + 3 && sum == expected;
}

/* "hhmmss[.sss]" -> milisegundos desde medianoche. */
static bool parse_utc(const char *f, uint32_t *ms)
{
    unsigned int h, m, s, frac_ms = 0;

    if (strlen(f) < 6 || strspn(f, "0123456789") < 6) {
        return false;
    }
    h = (f[0] - '0') * 10 + (f[1] - '0');
    m = (f[2] - '0') * 10 + (f[3] - '0');
    s = (f[4] - '0') * 10 + (f[5] - '0');
    if (f[6] == '.') {
        frac_ms = (unsigned int)(strtod(f + 6, NULL) * 1000.0 + 0.5);
    } else if (f[6] != '\0') {
        return false;
    }
    if (h >= 24 || m >= 60 || s >= 60 || frac_ms >= 1000) {
        return false;
    }
    *ms = ((h * 60 + m) * 60 + s) * 1000 + frac_ms;
    return true;
}

static bool parse_number(const char *f, double *out)
{
    char *end;

    *out = strtod(f, &end);
    return end != f && *end == '\0' && isfinite(*out);
}

/* "ddmm.mmmm" o "dddmm.mmmm" + hemisferio ("NS" o "EW") -> microgrados con signo. */
static bool parse_coordinate(const char *f, const char *hemisphere, const char *allowed,
                             double limit_deg, int32_t *udeg)
{
    double value;

    if (!parse_number(f, &value) || value < 0.0 || strlen(hemisphere) != 1 ||
        strchr(allowed, hemisphere[0]) == NULL) {
        return false;
    }
    const double degrees = floor(value / 100.0);
    const double minutes = value - degrees * 100.0;
    const double result = degrees + minutes / 60.0;
    if (minutes >= 60.0 || result > limit_deg) {
        return false;
    }
    const bool negative = hemisphere[0] == 'S' || hemisphere[0] == 'W';
    *udeg = (int32_t)lround((negative ? -result : result) * 1e6);
    return true;
}

/*
 * $xxGGA,hhmmss.ss,lat,N,lon,E,fix,sats,hdop,alt,M,...
 *    [0]     [1]   [2][3][4][5][6] [7]  [8] [9][10]
 * Calidad 1 (GPS) o 2 (DGPS) es fix. Sin fix se publica fix_valid = false
 * y se conservan la ultima posicion y su fecha.
 */
static void parse_gga(char *s)
{
    char *field[GGA_FIELDS] = { s };
    int n = 1;

    *strchr(s, '*') = '\0';
    for (char *p = s; *p != '\0' && n < GGA_FIELDS; p++) {
        if (*p == ',') {
            *p = '\0';
            field[n++] = p + 1;
        }
    }
    if (n < 11 || strlen(field[6]) != 1) {
        return;
    }

    const bool fix = field[6][0] == '1' || field[6][0] == '2';
    uint32_t utc_ms = 0;
    const bool utc_valid = parse_utc(field[1], &utc_ms);
    int32_t lat = 0, lon = 0;
    double alt_m = 0.0;
    char *end;
    const long sats = strtol(field[7], &end, 10);

    if (fix && (end == field[7] || *end != '\0' || sats < 0 || sats > 99 ||
                !parse_coordinate(field[2], field[3], "NS", 90.0, &lat) ||
                !parse_coordinate(field[4], field[5], "EW", 180.0, &lon) ||
                !parse_number(field[9], &alt_m) || fabs(alt_m) > 2.0e6)) {
        return; /* fix declarado pero campos corruptos: se ignora la frase */
    }

    k_mutex_lock(&gps_lock, K_FOREVER);
    published.utc_valid = utc_valid;
    published.utc_ms_of_day = utc_ms;
    published.fix_valid = fix;
    if (fix) {
        published.lat_udeg = lat;
        published.lon_udeg = lon;
        published.alt_mm = (int32_t)lround(alt_m * 1000.0);
        published.satellites = (uint8_t)sats;
        published.last_fix_ms = k_uptime_get();
    }
    k_mutex_unlock(&gps_lock);
}

static void process_line(char *line)
{
    if (!checksum_ok(line)) {
        return;
    }

    k_mutex_lock(&gps_lock, K_FOREVER);
    published.last_nmea_ms = k_uptime_get(); /* enlace vivo, haya fix o no */
    k_mutex_unlock(&gps_lock);

    if (strlen(line) > 6 && strncmp(line + 3, "GGA,", 4) == 0) {
        parse_gga(line);
    }
}

static void gps_entry(void *p1, void *p2, void *p3)
{
    char line[GPS_LINE_MAX];

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Sin UART el hilo sigue vivo: el enlace se vera caido por last_nmea_ms. */
    if (device_is_ready(gps_uart) &&
        uart_irq_callback_user_data_set(gps_uart, gps_isr, NULL) == 0) {
        uart_irq_rx_enable(gps_uart);
    }

    const int wdt = task_wdt_add(GPS_WDT_MS, NULL, NULL);

    while (true) {
        (void)task_wdt_feed(wdt);
        if (k_msgq_get(&gps_lines, line, K_MSEC(GPS_WAIT_MS)) == 0) {
            process_line(line);
        }
    }
}

K_THREAD_DEFINE(gps_tid, GPS_STACK_SIZE, gps_entry, NULL, NULL, NULL, GPS_PRIORITY, 0, 0);
