#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>

#include "led.h"

#define RED_LED_NODE   DT_ALIAS(rgb_red)
#define BLUE_LED_NODE  DT_ALIAS(rgb_blue)
#define GREEN_LED_NODE DT_ALIAS(rgb_green)

static const struct gpio_dt_spec red_led =
    GPIO_DT_SPEC_GET(RED_LED_NODE, gpios);

static const struct gpio_dt_spec blue_led =
    GPIO_DT_SPEC_GET(BLUE_LED_NODE, gpios);

static const struct gpio_dt_spec green_led =
    GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios);

int led_init(void)
{
    if (!gpio_is_ready_dt(&red_led) ||
        !gpio_is_ready_dt(&blue_led) ||
        !gpio_is_ready_dt(&green_led)) {
        return -ENODEV;
    }

    int err;

    err = gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE);
    if (err != 0) return err;

    err = gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_INACTIVE);
    if (err != 0) return err;

    err = gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
    if (err != 0) return err;

    return 0;
}

int led_off(void)
{
    int err;

    err = gpio_pin_set_dt(&red_led, 0);
    if (err != 0) return err;

    err = gpio_pin_set_dt(&blue_led, 0);
    if (err != 0) return err;

    err = gpio_pin_set_dt(&green_led, 0);
    if (err != 0) return err;

    return 0;
}

int led_set_color(led_color_t color)
{
    int err = led_off();

    if (err != 0) {
        return err;
    }

    switch (color) {

    case LED_RED:
        return gpio_pin_set_dt(&red_led, 1);

    case LED_BLUE:
        return gpio_pin_set_dt(&blue_led, 1);

    case LED_GREEN:
        return gpio_pin_set_dt(&green_led, 1);

    default:
        return -EINVAL;
    }
}