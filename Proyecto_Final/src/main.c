#include "acquisition_api.h"
#include "gps_api.h"
#include "simu_frame.h"
#include "human_report.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
    int ret;

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
    }

    return 0;
}




/*
west build -p auto -b nucleo_wl55jc   /home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final   -d /home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/build --   -DZephyr-sdk_DIR=/home/pablo_bee_keeper/zephyr-sdk-1.0.1/cmake   -DUSER_CACHE_DIR=/home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/.zephyr-cache -DDTC_OVERLAY_FILE=/home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/boards/nucleo_wl55jc.overlay -DCONF_FILE=/home/pablo_bee_keeper/Escritorio/Embedded_Assessments/Proyecto_Final/confs/prj_nucleo_wl55jc.conf   -DUSE_CCACHE=0
*/