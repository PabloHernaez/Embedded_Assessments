#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "app_watchdog.h"

struct producer {
    atomic_t progress;
    int period_ms;
};

static struct producer acq = { .period_ms = 20 };
static struct producer gps = { .period_ms = 200 };

static void produce(void *arg, void *p2, void *p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct producer *const source = arg;
    while (true) {
        atomic_inc(&source->progress);
        k_msleep(source->period_ms);
    }
}

K_THREAD_DEFINE(test_acq, 768, produce, &acq, NULL, NULL, 3, 0, 0);
K_THREAD_DEFINE(test_gps, 768, produce, &gps, NULL, NULL, 5, 0, 0);

int main(void)
{
    const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
    if (!device_is_ready(uart)) {
        return -ENODEV;
    }

    uint32_t cause = 0U;
    if (hwinfo_get_reset_cause(&cause) == 0) {
        (void)hwinfo_clear_reset_cause();
    } else {
        cause = 0U;
    }
    printk("BANCO IWDG RESET=0x%08x WATCHDOG=%u\n", (unsigned int)cause,
           (unsigned int)((cause & RESET_WATCHDOG) != 0U));

    app_watchdog_start();
    int err = app_watchdog_observe((uint32_t)atomic_get(&acq.progress),
                                   (uint32_t)atomic_get(&gps.progress));
    if (err != 0) {
        printk("IWDG ERROR %d\n", err);
        return err;
    }
    printk("IWDG activo. Envia a=adquisicion, g=GPS o m=main.\n");

    bool fault_injected = false;
    while (true) {
        err = app_watchdog_observe((uint32_t)atomic_get(&acq.progress),
                                   (uint32_t)atomic_get(&gps.progress));
        if (err != 0) {
            printk("IWDG FEED ERROR %d\n", err);
        }

        unsigned char key;
        if (!fault_injected && uart_poll_in(uart, &key) == 0) {
            switch (key) {
            case 'a':
                printk("Parar adquisicion\n");
                k_thread_suspend(test_acq);
                fault_injected = true;
                break;
            case 'g':
                printk("Parar GPS\n");
                k_thread_suspend(test_gps);
                fault_injected = true;
                break;
            case 'm':
                printk("Parar main\n");
                k_thread_suspend(k_current_get());
                break;
            }
        }
        k_msleep(100);
    }
}
