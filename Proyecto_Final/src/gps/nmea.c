#include "nmea.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GGA_FIELDS 15

bool nmea_checksum_ok(const char *line)
{
    const char *star = strchr(line, '*');
    char *end;
    uint8_t sum = 0;

    if (line[0] != '$' || star == NULL) {
        return false;
    }
    for (const char *p = line + 1; p < star; p++) {
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
    h = (unsigned int)((f[0] - '0') * 10 + (f[1] - '0'));
    m = (unsigned int)((f[2] - '0') * 10 + (f[3] - '0'));
    s = (unsigned int)((f[4] - '0') * 10 + (f[5] - '0'));
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
 * $xxGGA,hhmmss.ss,lat,N,lon,E,fix,sats,hdop,alt,M,...*HH
 *    [0]     [1]   [2][3][4][5][6] [7]  [8] [9][10]
 */
bool nmea_parse_gga(char *line, GpsSnapshot *gps)
{
    char *field[GGA_FIELDS] = { line };
    int n = 1;
    char *star = strchr(line, '*');

    if (strlen(line) < 7 || strncmp(line + 3, "GGA,", 4) != 0 || star == NULL) {
        return false;
    }
    *star = '\0';
    for (char *p = line; *p != '\0' && n < GGA_FIELDS; p++) {
        if (*p == ',') {
            *p = '\0';
            field[n++] = p + 1;
        }
    }
    if (n < 11 || strlen(field[6]) != 1) {
        return false;
    }

    const bool fix = field[6][0] == '1' || field[6][0] == '2';
    int32_t lat = 0, lon = 0;
    double alt_m = 0.0;
    char *end;
    const long sats = strtol(field[7], &end, 10);

    if (fix && (end == field[7] || *end != '\0' || sats < 0 || sats > 99 ||
                !parse_coordinate(field[2], field[3], "NS", 90.0, &lat) ||
                !parse_coordinate(field[4], field[5], "EW", 180.0, &lon) ||
                !parse_number(field[9], &alt_m) || fabs(alt_m) > 2.0e6)) {
        return false; /* dice tener fix pero algun campo no es valido: se ignora */
    }

    gps->utc_valid = parse_utc(field[1], &gps->utc_ms_of_day);
    gps->fix_valid = fix;
    if (fix) {
        gps->lat_udeg = lat;
        gps->lon_udeg = lon;
        gps->alt_mm = (int32_t)lround(alt_m * 1000.0);
        gps->satellites = (uint8_t)sats;
    }
    return true;
}
