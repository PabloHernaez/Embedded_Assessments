#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "sensor_service.h"
#include "button.h"
#include "modes.h"
#include "rgb_led.h"


int main(void)
{

	//Inicializamos la parte del proceso del led
	int err = rgb_init();
	if (err != 0) {
		printk("Error initiating the RGB: %d\n", err);
		return 0;
	}

	//Inicializamos el servicio de los hilos que se encargan de leer
	err = sensor_service_init();
	if (err != 0) {
		printk("Error launching the ADC service: %d\n", err);
		return 0;
	}

	err = button_init();
	if (err != 0 ) {
		printk("Error launching the button callbacks %d\n", err);
		return 0;
	}

	modes_init();

	printk("System launched\n");

	while (true) {
		struct button_event button_event;

		err = button_get_event(&button_event, K_MSEC(20));

		if (err == 0) {
			modes_handle_button_event(&button_event);
		} else if (err != -EAGAIN) {
			printk("Error button timestamp skipped: %d\n", err);
		}

		struct sensor_result result;

		err = sensor_get_result(&result, K_NO_WAIT);

		if (err == 0) {
			modes_handle_sensor_result(&result);
		} else if (err != -EAGAIN) {
			printk("Error receiving sensor measure: %d\n", err);
		}
	}

    return 0;
}
