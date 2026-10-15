#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rgb_led.h"
#include "sensor_service.h"

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
	uint32_t sample_count = 0U;
	int err = rgb_init();

	if (err != 0) {
		printk("Error al iniciar el RGB: %d\n", err);
		return 0;
	}

	err = sensor_service_init();
	if (err != 0) {
		printk("Error al iniciar el servicio ADC: %d\n", err);
		return 0;
	}

	printk("Demo NORMAL: primera lectura inmediata; periodo=%d ms\n",
	       SENSOR_SAMPLE_PERIOD_MS);
	err = sensor_start();
	if (err != 0) {
		printk("Error al activar el sensor: %d\n", err);
		return 0;
	}

	while (true) {
		struct sensor_result result;

		err = sensor_get_result(&result, K_FOREVER);
		if (err != 0) {
			printk("Error al recibir una muestra: %d\n", err);
			continue;
		}

		if (result.status != 0) {
			printk("Error al leer el ADC: %d\n", result.status);
			continue;
		}

		sample_count++;
		err = rgb_set(color_for_level(result.sample.level));
		if (err != 0) {
			printk("Error al controlar el RGB: %d\n", err);
		}

		printk("Muestra %u: ADC=%u, brillo=%u %%\n",
		       sample_count, result.sample.adc_raw,
		       result.sample.percent);
	}

	return 0;
}