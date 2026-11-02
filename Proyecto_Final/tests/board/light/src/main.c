#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "light.h"

/**
 * Muestra una conversion ADC bruta cada 500 ms para caracterizar el LDR.
 * Entradas: ninguna explicita; usa el montaje ADC1/canal 5/PB1 del overlay comun.
 * Retorno: 0 si falla la inicializacion; en funcionamiento mantiene el bucle.
 * Modifica: configura el ADC, lee una conversion por vuelta y escribe por consola
 *           la fecha y raw o el error. No convierte a porcentaje ni cambia calibracion.
 */
int main(void)
{
    k_msleep(100);
    printk("LDR_RAW_START ADC1_CH5_PB1 period_ms=500\n");

    int ret = light_init();
    printk("light_init=%d\n", ret);
    if (ret < 0) {
        return 0;
    }

    while (true) {
        int16_t raw;
        ret = light_read_raw(&raw);
        long long now_ms = (long long)k_uptime_get();

        if (ret == 0) {
            printk("t_ms=%lld raw=%d\n", now_ms, (int)raw);
        } else {
            printk("t_ms=%lld err=%d\n", now_ms, ret);
        }
        k_msleep(500);
    }
}
