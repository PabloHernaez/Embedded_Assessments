#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_tasks.h"
#include "../drivers/gps.h"
#include "../drivers/led.h"

void app_tasks_run(App_State state, bool entering)
{
    GPS_Struct gps_data;

    // Acciones que solo se ejecutan al entrar en un estado
    if (entering) {
        switch (state) {

        case STATE_TEST:
            led_set_color(LED_BLUE);
            printk("Modo TEST\n");
            break;

        case STATE_NORMAL:
            led_set_color(LED_GREEN);
            printk("Modo NORMAL\n");
            break;

        case STATE_SIMU:
            led_set_color(LED_RED);
            printk("Modo SIMU\n");
            break;
        }
    }

    // Acciones periódicas del estado
    switch (state) {

    case STATE_TEST:

        if (gps_get_data(&gps_data) == 0) {
            printk("TEST: Lat: %.6f | Lon: %.6f\n",
                   gps_data.latitude,
                   gps_data.longitude);
        }
        break;

    case STATE_NORMAL:

        if (gps_get_data(&gps_data) == 0 && gps_data.valid) {
            printk("NORMAL: Lat: %.6f | Lon: %.6f\n",
                   gps_data.latitude,
                   gps_data.longitude);
        }
        break;

    case STATE_SIMU:

        // Funcionalidad de simulacion
        break;
    }
}

int64_t app_tasks_get_interval(App_State state)
{
    switch (state) {

    case STATE_TEST:
        return 2000;

    case STATE_NORMAL:
        return 1000;

    case STATE_SIMU:
        return 1000;

    default:
        return 1000;
    }
}