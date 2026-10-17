#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rgb_led.h"
#include "sensor_service.h"

static enum rgb_color color_for_level(enum brightness_level level)
{
	/* Traduccion explicita: no acoplar los valores numericos de ambos enum. */
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

	//Inicializamos la parte del proceso del led
	int err = rgb_init();
	if (err != 0) {
		printk("Error al iniciar el RGB: %d\n", err);
		return 0;
	}

	//Inicializamos el servicio de los hilos que se encargan de leer
	err = sensor_service_init();
	if (err != 0) {
		printk("Error al iniciar el servicio ADC: %d\n", err);
		return 0;
	}


}

//OFF mode implementation
void OFF_execution(void) {

	enum rgb_color color = RGB_OFF;
	int err = rgb_set(color);
	if (err != 0) {
		printk("AN error has appeared while transitioning between colors %d", err);
	}

    err = sensor_pause(K_MSEC(1000));
    if (err != 0) {
		printk("An error has appeared while stoping the sensor %d", err);
    }

}

//BLUE mode implementation
void BLUE_execution(void) {

	enum rgb_color color = RGB_BLUE;
	int err = rgb_set(color);
	if (err != 0) {
		printk("AN error has appeared while transitioning between colors %d", err);
	}

    int err = sensor_pause(K_MSEC(1000));
    if (err != 0) {
		printk("An error has appeared while stoping the sensor %d", err);
    }
}

//NORMAL mode implementation
void NORMAL_execution(void){

	uint32_t sample_count = 0U;

	printk("Demo NORMAL: primera lectura inmediata; periodo=%d ms\n",
	       SENSOR_SAMPLE_PERIOD_MS);
	int err = sensor_start();
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

		/* La cola puede funcionar aunque la conversion ADC haya fallado. */
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