#include <stdio.h>
#include <string.h>

#include "nmea.h"

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

/* Construye "$cuerpo*HH" con el checksum correcto (hex en mayusculas o minusculas). */
static char *sentence(char *buf, size_t cap, const char *body, bool lower)
{
    unsigned int cs = 0;
    for (const char *p = body; *p != '\0'; p++) {
        cs ^= (unsigned char)*p;
    }
    snprintf(buf, cap, lower ? "$%s*%02x" : "$%s*%02X", body, cs);
    return buf;
}

int main(void)
{
    unsigned int checks = 0;
    char line[128];
    GpsSnapshot gps = { .last_nmea_ms = -1, .last_fix_ms = -1 };

    /* Checksum: correcto, incorrecto, sin '$', sin '*', con un solo digito. */
    CHECK(nmea_checksum_ok(sentence(line, sizeof(line), "GPRMC,010203,V,,,,,,,,,,N", false)));
    CHECK(nmea_checksum_ok(sentence(line, sizeof(line), "GPRMC,010203,V,,,,,,,,,,N", true)));
    CHECK(!nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,,M,,*00"));
    CHECK(!nmea_checksum_ok("GPGGA,123519*47"));
    CHECK(!nmea_checksum_ok("$GPGGA,123519"));
    CHECK(!nmea_checksum_ok("$GPGGA*4"));

    /* GGA con fix: posicion en microgrados, altitud en mm, satelites y UTC. */
    sentence(line, sizeof(line),
             "GPGGA,123519.50,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", false);
    CHECK(nmea_parse_gga(line, &gps));
    CHECK(gps.fix_valid && gps.satellites == 8);
    CHECK(gps.lat_udeg == 48117300 && gps.lon_udeg == 11516667 && gps.alt_mm == 545400);
    CHECK(gps.utc_valid && gps.utc_ms_of_day == ((12 * 60 + 35) * 60 + 19) * 1000 + 500);
    CHECK(gps.last_nmea_ms == -1 && gps.last_fix_ms == -1); /* las fechas no son del parser */

    /* GN (GPS + GLONASS), hemisferios S y W, DGPS y altitud negativa. */
    sentence(line, sizeof(line),
             "GNGGA,000001,4023.3677,S,00337.6450,W,2,12,0.8,-10.5,M,,M,,", false);
    CHECK(nmea_parse_gga(line, &gps));
    CHECK(gps.fix_valid && gps.lat_udeg == -40389462 && gps.lon_udeg == -3627417);
    CHECK(gps.alt_mm == -10500 && gps.utc_ms_of_day == 1000);

    /* Sin fix y campos vacios: fix_valid = false, se conserva la ultima posicion. */
    sentence(line, sizeof(line), "GPGGA,,,,,,0,00,99.99,,,,,,", false);
    CHECK(nmea_parse_gga(line, &gps));
    CHECK(!gps.fix_valid && !gps.utc_valid && gps.lat_udeg == -40389462);

    /* Dice tener fix pero un campo es invalido: la frase se ignora entera. */
    const GpsSnapshot before = gps;
    sentence(line, sizeof(line),
             "GPGGA,010203,4023.3677,X,00337.6450,W,1,05,0.8,10.0,M,,M,,", false);
    CHECK(!nmea_parse_gga(line, &gps) && !gps.fix_valid);
    sentence(line, sizeof(line),
             "GPGGA,010203,4023.3677,N,00337.6450,W,1,5x,0.8,10.0,M,,M,,", false);
    CHECK(!nmea_parse_gga(line, &gps));
    sentence(line, sizeof(line),
             "GPGGA,010203,4023.3677,N,00337.6450,W,1,05,0.8,,M,,M,,", false);
    CHECK(!nmea_parse_gga(line, &gps));
    CHECK(memcmp(&gps, &before, sizeof(gps)) == 0);

    /* Otra frase o una GGA truncada: no es GGA valida. */
    CHECK(!nmea_parse_gga(sentence(line, sizeof(line), "GPRMC,010203,V,,,,,,,,,,N", false), &gps));
    CHECK(!nmea_parse_gga(sentence(line, sizeof(line), "GPGGA,010203,4023.3677,N", false), &gps));

    printf("PASS: %u NMEA checks\n", checks);
    return 0;
}
