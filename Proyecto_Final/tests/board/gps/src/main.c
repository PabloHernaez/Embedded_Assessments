#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/task_wdt/task_wdt.h>

#include "gps_api.h"

/* El servicio registra su canal de task_wdt; en produccion lo inicia main.c. */
static int watchdog_init(void)
{
    return task_wdt_init(NULL);
}
SYS_INIT(watchdog_init, APPLICATION, 0);

/* Ensayo del servicio GPS real: imprime el snapshot cada segundo por la consola. */
int main(void)
{
    printk("\n=== Ensayo GPS ===\n");

    while (true) {
        GpsSnapshot gps;
        gps_snapshot_get(&gps);
        const int64_t now_ms = k_uptime_get();

        if (gps.last_nmea_ms < 0) {
            printk("NMEA: esperando primera sentencia\n");
        } else {
            printk("NMEA: ultima hace %lld ms\n", (long long)(now_ms - gps.last_nmea_ms));
        }

        printk("FIX=%u sats=%u edad=%lld ms lat_udeg=%ld lon_udeg=%ld alt_mm=%ld\n",
               (unsigned int)gps.fix_valid, (unsigned int)gps.satellites,
               (long long)(gps.last_fix_ms < 0 ? -1 : now_ms - gps.last_fix_ms),
               (long)gps.lat_udeg, (long)gps.lon_udeg, (long)gps.alt_mm);

        if (gps.utc_valid) {
            const uint32_t s = gps.utc_ms_of_day / 1000U;
            printk("UTC=%02u:%02u:%02u.%03u\n", (unsigned int)(s / 3600U),
                   (unsigned int)((s / 60U) % 60U), (unsigned int)(s % 60U),
                   (unsigned int)(gps.utc_ms_of_day % 1000U));
        } else {
            printk("UTC=N/D\n");
        }

        k_msleep(1000);
    }
}
