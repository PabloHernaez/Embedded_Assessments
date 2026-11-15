#include "acquisition_api.h"
#include "drivers/gps.h"
#include "drivers/led.h"
#include "app/state_machine.h"
#include "app/app_tasks.h"
#include "simu_frame.h"
#include "human_report.h"
#include "drivers/button.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>



int main(void)
{
    /*int ret;

    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return 0;
    }

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }*/

    int err;

    err = button_init();
    if (err != 0) {
        printk("Error inicializando boton\n");
        return 0;
    }

    err = led_init();
    if (err != 0) {
        printk("Error inicializando LEDs\n");
        return 0;
    }

    state_machine_init();

    App_State state = STATE_TEST;

    // Inicializamos el estado y ejecutamos sus acciones de entrada
    app_tasks_run(state, true);

    int64_t next_execution =
        k_uptime_get() + app_tasks_get_interval(state);

    struct button_event event;

    while (1) {

        int64_t remaining = next_execution - k_uptime_get();

        if (remaining < 0) {
            remaining = 0;
        }

        err = button_get_event(&event, K_MSEC(remaining));

        if (err == 0 && event.type == BUTTON_PRESSED) {

            state = state_machine_on_button();

            // Ejecutamos acciones de entrada y tareas del nuevo estado
            app_tasks_run(state, true);

            next_execution =
                k_uptime_get() + app_tasks_get_interval(state);
        }
        else if (err == -EAGAIN) {

            // Ejecutamos la tarea periódica del estado actual
            app_tasks_run(state, false);

            next_execution =
                k_uptime_get() + app_tasks_get_interval(state);
        }
    }

    /*
    struct button_event event;


    GPS_Struct gps_data;

    int ret;

    if (button_init() != 0) {
        printk("\n--- Error al inicializar el boton ---\n");
        return -1;
    }

    if (gps_init() != 0 ) {
        printk("\n--- Error al inicializar el GPS ---\n");
        return -1;
    }

    printk("\n--- INICIAMOS RECABADO GPS ---\n");

    while (1) {


    if (gps_has_fix()){
        ret = gps_get_data(&gps_data);

        if (ret == 0) {

            printk("\n--- DATOS GPS ---\n");

            printk("Latitud: %f\n", gps_data.latitude);
            printk("Longitud: %f\n", gps_data.longitude);
            printk("Altitud: %f\n", gps_data.altitude);

            printk("Satellites: %d\n", gps_data.satellites);
            printk("Fix quality: %d\n", gps_data.fix_quality);

            printk("Valid: %d\n", gps_data.valid);

        } else {
            printk("ERROR: No se pudieron obtener los datos\n");
        }
    }


        k_msleep(1000);
    }

    return 0;*/
}



/*
west build -p auto -b nucleo_wl55jc   /home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final   -d /home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/build --   -DZephyr-sdk_DIR=/home/pablo_bee_keeper/zephyr-sdk-1.0.1/cmake   -DUSER_CACHE_DIR=/home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/.zephyr-cache -DDTC_OVERLAY_FILE=/home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/boards/nucleo_wl55jc.overlay -DCONF_FILE=/home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/confs/prj_nucleo_wl55jc.conf   -DUSE_CCACHE=0
screen /dev/ttyACM0 115200

*/