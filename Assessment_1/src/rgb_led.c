#include "rgb_led.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

static const struct gpio_dt_spec red_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(rgb_red), gpios);
static const struct gpio_dt_spec green_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(rgb_green), gpios);
static const struct gpio_dt_spec blue_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(rgb_blue), gpios);

static int set_channel(const struct gpio_dt_spec *led, bool enabled,
		       int first_error)
{
	/* gpio_pin_set_dt aplica automaticamente GPIO_ACTIVE_LOW del overlay. */
	int err = gpio_pin_set_dt(led, enabled ? 1 : 0);

	return (first_error == 0) ? err : first_error;
}

int rgb_init(void)
{
	const struct gpio_dt_spec *channels[] = {
		&red_led,
		&green_led,
		&blue_led,
	};

	for (size_t i = 0U; i < ARRAY_SIZE(channels); i++) {
		if (!gpio_is_ready_dt(channels[i])) {
			return -ENODEV;
		}

		int err = gpio_pin_configure_dt(channels[i], GPIO_OUTPUT_INACTIVE);
		if (err != 0) {
			return err;
		}
	}

	return 0;
}

int rgb_set(enum rgb_color color)
{
	bool red;
	bool green;
	bool blue;

	switch (color) {
	case RGB_OFF:
		red = false;
		green = false;
		blue = false;
		break;
	case RGB_RED:
		red = true;
		green = false;
		blue = false;
		break;
	case RGB_YELLOW:
		/* El amarillo se obtiene encendiendo rojo y verde a la vez. */
		red = true;
		green = true;
		blue = false;
		break;
	case RGB_GREEN:
		red = false;
		green = true;
		blue = false;
		break;
	case RGB_BLUE:
		red = false;
		green = false;
		blue = true;
		break;
	default:
		return -EINVAL;
	}

	int err = set_channel(&red_led, red, 0);

	err = set_channel(&green_led, green, err);
	err = set_channel(&blue_led, blue, err);
	return err;
}