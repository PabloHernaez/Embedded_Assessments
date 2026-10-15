#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "brightness.h"

#define SAMPLE_PERIOD_MS 2000

int main(void)
{
	int err = brightness_init();

	if (err != 0) {
		printk("Error al iniciar el ADC: %d\n", err);
		return 0;
	}

	while (true) {
		struct brightness_sample sample;

		err = brightness_read(&sample);
		if (err == 0) {
			printk("ADC=%u, brillo=%u %%\n",
			       sample.adc_raw, sample.percent);
		} else {
			printk("Error al leer el ADC: %d\n", err);
		}

		k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
	}

	return 0;
}