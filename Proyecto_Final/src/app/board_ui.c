#include "board_ui.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define B1_MIN_PRESS_MS 30

enum {
    RGB_RED = BIT(0),
    RGB_GREEN = BIT(1),
    RGB_BLUE = BIT(2),
};

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec rgb[] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(rgb_red), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(rgb_green), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(rgb_blue), gpios),
};

static struct gpio_callback button_callback_data;
static atomic_t pending_presses;
static int64_t press_start_ms = -1;

static void on_button_edge(const struct device *dev, struct gpio_callback *callback,
                           uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);

    const int pressed = gpio_pin_get_dt(&button);
    if (pressed < 0) {
        return;
    }

    const int64_t now_ms = k_uptime_get();
    if (pressed) {
        if (press_start_ms < 0) {
            press_start_ms = now_ms;
        }
    } else if (press_start_ms >= 0) {
        if (now_ms - press_start_ms >= B1_MIN_PRESS_MS) {
            atomic_inc(&pending_presses);
        }
        press_start_ms = -1;
    }
}

int board_ui_init(void)
{
    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }
    for (size_t i = 0U; i < ARRAY_SIZE(rgb); ++i) {
        if (!gpio_is_ready_dt(&rgb[i])) {
            return -ENODEV;
        }
        int err = gpio_pin_configure_dt(&rgb[i], GPIO_OUTPUT_INACTIVE);
        if (err != 0) {
            return err;
        }
    }

    int err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (err != 0) {
        return err;
    }
    gpio_init_callback(&button_callback_data, on_button_edge, BIT(button.pin));
    err = gpio_add_callback(button.port, &button_callback_data);
    if (err != 0) {
        return err;
    }
    err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
    if (err != 0) {
        (void)gpio_remove_callback(button.port, &button_callback_data);
    }
    return err;
}

unsigned int board_ui_take_presses(void)
{
    return (unsigned int)atomic_set(&pending_presses, 0);
}

void board_ui_show(AppMode mode, TrackerState state)
{
    static unsigned int previous_color;
    unsigned int color = RGB_BLUE;
    if (mode == APP_MODE_SIMU) {
        color = RGB_GREEN | RGB_BLUE;
    } else if (mode == APP_MODE_NORMAL) {
        if (state == TRACKER_STATE_PROBLEM) {
            color = RGB_RED;
        } else if (state == TRACKER_STATE_FLYING) {
            color = RGB_RED | RGB_GREEN;
        } else {
            color = RGB_GREEN;
        }
    }

    if (color == previous_color) {
        return;
    }
    for (size_t i = 0U; i < ARRAY_SIZE(rgb); ++i) {
        (void)gpio_pin_set_dt(&rgb[i], (color & BIT(i)) != 0U);
    }
    previous_color = color;
}
