#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "light.h"

/* Codigo ADC bruto del LDR cada 500 ms para medir las referencias de calibracion. */
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
