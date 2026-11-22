#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "gps.h"
#include "gps_api.h"

static void report_gps(void)
{
    GPS_Struct sample;
    int ret = gps_get_data(&sample);

    if (ret != 0) {
        printk("GPS: error al copiar datos (%d)\n", ret);
        return;
    }

    if (sample.gga_count == 0U) {
        printk("GPS: esperando la primera GGA\n");
        return;
    }

    printk("GGA=%u | fix=%u | calidad=%u | satelites=%u",
           (unsigned int)sample.gga_count,
           (unsigned int)sample.valid,
           (unsigned int)sample.fix_quality,
           (unsigned int)sample.satellites);

    if (sample.valid) {
        printk(" | lat=%.6f | lon=%.6f | alt=%.1f m",
               sample.latitude, sample.longitude, sample.altitude);
    }

    printk("\n");
}

int main(void)
{
    printk("\n=== Ensayo GPS ===\n");

    int ret = gps_init();
    if (ret != 0) {
        printk("GPS: fallo de inicializacion (%d)\n", ret);
        return ret;
    }

    while (true) {
        report_gps();
        
        GpsSnapshot snapshot;
        gps_snapshot_get(&snapshot);

        if (snapshot.last_nmea_ms < 0) {
            printk("NMEA: esperando primera sentencia\n");
        } else {
            int64_t age_ms = k_uptime_get() - snapshot.last_nmea_ms;
            printk("NMEA: ultima sentencia hace %lld ms\n", (long long)age_ms);
        }

        const bool have_fix_history = snapshot.last_fix_ms >= 0;
        const int64_t fix_age_ms = have_fix_history
            ? k_uptime_get() - snapshot.last_fix_ms : -1;
        const bool fix_usable = snapshot.fix_valid &&
            have_fix_history && fix_age_ms < 3000;

        printk("FIX: util=%u | edad=%lld ms | lat_udeg=%ld | lon_udeg=%ld | alt_mm=%ld\n",
               (unsigned int)fix_usable, (long long)fix_age_ms,
               (long)snapshot.lat_udeg, (long)snapshot.lon_udeg, (long)snapshot.alt_mm);

        k_msleep(1000);
    }
}