#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "brightness.h"
#include "rgb_led.h"

#define SAMPLE_PERIOD_MS 2000

static enum rgb_color color_for_level(enum brightness_level level)
{
	switch (level) {
	case BRIGHTNESS_RED:
		return RGB_RED;
	case BRIGHTNESS_YELLOW:
		return RGB_YELLOW;
	case BRIGHTNESS_GREEN:
		return RGB_GREEN;
	default:
		return RGB_OFF;
	}
}

int main(void)
{
	int err = rgb_init();

	if (err == 0) {
		err = brightness_init();
	}

	if (err != 0) {
		printk("Error de inicializacion: %d\n", err);
		return 0;
	}

	while (true) {
		struct brightness_sample sample;

		err = brightness_read(&sample);
		if (err != 0) {
			printk("Error al leer el ADC: %d\n", err);
			k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
			continue;
		}

		err = rgb_set(color_for_level(sample.level));
		if (err != 0) {
			printk("Error al controlar el RGB: %d\n", err);
		}

		printk("ADC=%u, brillo=%u %%\n", sample.adc_raw, sample.percent);
		k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
	}

	return 0;
}