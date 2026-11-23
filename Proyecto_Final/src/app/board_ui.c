#include "board_ui.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>

/* B1 (sw0) es un nodo gpio-keys de la placa: el driver input de Zephyr hace el
 * antirrebote (30 ms por defecto) y entrega eventos de pulsar y soltar. */
#define B1_NODE DT_ALIAS(sw0)

enum {
    RGB_RED = BIT(0),
    RGB_GREEN = BIT(1),
    RGB_BLUE = BIT(2),
};

static const struct gpio_dt_spec rgb[] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(rgb_red), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(rgb_green), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(rgb_blue), gpios),
};

static atomic_t pending_presses;

/* Una pulsacion cuenta al soltar el boton. */
static void on_key(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    if (evt->code == DT_PROP(B1_NODE, zephyr_code) && evt->value == 0) {
        atomic_inc(&pending_presses);
    }
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_PARENT(B1_NODE)), on_key, NULL);

int board_ui_init(void)
{
    for (size_t i = 0U; i < ARRAY_SIZE(rgb); ++i) {
        if (!gpio_is_ready_dt(&rgb[i])) {
            return -ENODEV;
        }
        const int err = gpio_pin_configure_dt(&rgb[i], GPIO_OUTPUT_INACTIVE);
        if (err != 0) {
            return err;
        }
    }
    return 0;
}

unsigned int board_ui_take_presses(void)
{
    return (unsigned int)atomic_set(&pending_presses, 0);
}

void board_ui_show(AppMode mode, TrackerState state)
{
    static unsigned int previous_color;
    unsigned int color = RGB_BLUE; /* TEST */

    if (mode == APP_MODE_SIMU) {
        color = RGB_GREEN | RGB_BLUE; /* cian */
    } else if (mode == APP_MODE_NORMAL) {
        color = state == TRACKER_STATE_PROBLEM ? RGB_RED
              : state == TRACKER_STATE_FLYING  ? RGB_RED | RGB_GREEN /* amarillo */
                                               : RGB_GREEN;
    }

    if (color == previous_color) {
        return;
    }
    for (size_t i = 0U; i < ARRAY_SIZE(rgb); ++i) {
        (void)gpio_pin_set_dt(&rgb[i], (color & BIT(i)) != 0U);
    }
    previous_color = color;
}
