
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>

#include "button.h"

#define B2_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(B2_NODE, gpios);

K_MSGQ_DEFINE(button_queue, sizeof(struct button_event), 8, 4);

static struct gpio_callback button_cb_data;

static void button_callback(const struct device *dev,
                            struct gpio_callback *cb,
                            uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int pressed = gpio_pin_get_dt(&button);

    if (pressed < 0) {
        return;
    }

    // Solo nos interesa el flanco de pulsación
    if (pressed) {

        struct button_event event = {
            .type = BUTTON_PRESSED,
            .timestamp = k_uptime_get()
        };

        (void)k_msgq_put(&button_queue, &event, K_NO_WAIT);
    }
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

    gpio_init_callback(&button_cb_data,
                       button_callback,
                       BIT(button.pin));

    err = gpio_add_callback(button.port, &button_cb_data);
    if (err != 0) {
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(
        &button,
        GPIO_INT_EDGE_TO_ACTIVE
    );

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
