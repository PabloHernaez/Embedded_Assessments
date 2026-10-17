#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdbool.h>

#include "button.h"

#define B2_NODE DT_ALIAS(sw1)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(B2_NODE, gpios);

K_MSGQ_DEFINE(button_queue, sizeof(struct button_event), 8, 4);

static void long_press_expiry(struct k_timer *timer);
K_TIMER_DEFINE(long_press_timer, long_press_expiry, NULL);

static struct gpio_callback button_cb_data;

static void button_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int pressed = gpio_pin_get_dt(&button);
    if (pressed < 0) {
        return;
    }

    struct button_event event = {
        .timestamp = k_uptime_get()
    };

    if (pressed) {

        printk("The button is pressed");

        event.type = BUTTON_PRESSED;

        k_timer_start(&long_press_timer, K_MSEC(1000), K_NO_WAIT);
    } else {
        event.type = BUTTON_RELEASED;

        printk("The callback of the button is considered a short one");

        k_timer_stop(&long_press_timer);
    }

    (void)k_msgq_put(&button_queue, &event, K_NO_WAIT);
}

int button_init(void)
{
    int err;

    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (err != 0) {
        return err;
    }

    gpio_init_callback(&button_cb_data, button_callback, BIT(button.pin));

    err = gpio_add_callback(button.port, &button_cb_data);
    if (err != 0) {
        return err;
    }

    err = gpio_pin_interrupt_configure_dt( &button, GPIO_INT_EDGE_BOTH);

    if (err != 0) {
        (void)gpio_remove_callback(button.port, &button_cb_data);
        return err;
    }

    return 0;
}

int button_get_event(struct button_event *event, k_timeout_t timeout)
{
    if (event == NULL) {
        return -EINVAL;
    }

    return k_msgq_get(&button_queue, event, timeout);
}

static void long_press_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    printk("Long Button Press detected");

    struct button_event event = {
        .type = BUTTON_LONG_TIMEOUT,
        .timestamp = k_uptime_get()
    };

    (void)k_msgq_put(&button_queue, &event, K_NO_WAIT);
}